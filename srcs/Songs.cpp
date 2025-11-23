#include "../includes/Utils.hpp"
#include "../includes/Database.hpp"

static std::atomic<size_t>	g_NumDbEntries(0);
static std::mutex			g_numDbEntriesMutex;
std::mutex					g_statsMutex;
t_stats						g_stats = {0, 0, 0, 0};
static std::mutex			g_imageMutex;

/**
 * @brief Handle a new file by adding a "42id" frame to the ID3v2 tag.
 *
 * This function creates a new "42id" frame with a unique identifier
 * corresponding to the current number of database entries + 1.
 * It adds this frame to the ID3v2 tag of the given file and saves it.
 * 
 * @param path The file path of the song being processed.
 * @param tag Pointer to the ID3v2 tag of the file.
 * @param file Reference to the TagLib::MPEG::File object representing the song.
 */
static void handleNewFile(const std::string &path, TagLib::ID3v2::Tag *tag, TagLib::MPEG::File &file)
{
	{
		std::lock_guard<std::mutex> lock(g_numDbEntriesMutex);
		size_t id = g_NumDbEntries + 1;
		auto *frame_id = new TagLib::ID3v2::UserTextIdentificationFrame;
		frame_id->setDescription("42id");
		frame_id->setText(std::to_string(id));
		tag->addFrame(frame_id);

		if (!file.save()) {
			std::cerr << "Failed to save ID3v2 tag for " << path << "\n";
			std::lock_guard<std::mutex> lock(g_statsMutex);
			g_stats.errors++;
		}
		g_NumDbEntries++;
	}
	{
		std::lock_guard<std::mutex> lock(g_statsMutex);
		g_stats.newFiles++;
	}
	log("Adding new song: " + path, false);
}

/**
 * @brief Extract metadata from the ID3v2 frame list and detect if "42id" exists.
 * 
 * This function iterates through all frames in the ID3v2 tag, extracting
 * relevant metadata into a multimap and checking if a "42id" frame exists.
 * The "APIC" and empty frames are skipped.
 * 
 * @param frames The list of ID3v2 frames.
 * @param metadata Output multimap to store extracted metadata key-value pairs.
 * @return true if a "42id" frame was found, false otherwise.
 */
static bool	extractID3v2Metadata(const TagLib::ID3v2::FrameList &frames,
									std::multimap<std::string, std::string> &metadata)
{
	bool	has42id = false;

	for (TagLib::ID3v2::FrameList::ConstIterator it = frames.begin(); it != frames.end(); ++it)
	{
		TagLib::String id = (*it)->frameID();
		std::string key = id.to8Bit(true);

		if (id == "APIC")
			continue;

		if (key == "TXXX") {
			TagLib::ID3v2::UserTextIdentificationFrame *uf =
			dynamic_cast<TagLib::ID3v2::UserTextIdentificationFrame *>(*it);
			if (uf) {
				std::string desc = uf->description().to8Bit(true);
				std::string val = (uf->fieldList().size() > 1)
					? uf->fieldList()[1].to8Bit(true)
					: "";

				metadata.insert(std::make_pair("TXXX:" + desc, val));

				if (desc == "42id")
					has42id = true;
			}
		}
		else {
			std::string val = (*it)->toString().to8Bit(true);
			if (!val.empty())
				metadata.insert(std::make_pair(key, val));
		}
	}

	return has42id;
}

/**
 * @brief Process a song's embedded image: decode, resize, hash and save.
 * 
 * Extracts the embedded picture (APIC frame) from an MP3 file's ID3v2 tag,
 * resizes it to PIC_QUALITY x PIC_QUALITY using high-quality interpolation,
 * converts it to grayscale to compute a perceptual hash, checks for duplicates,
 * and saves the image in JPEG format with high quality if unique.
 * 
 * @param paths Struct containing output paths (e.g., image directory).
 * @param hashes Vector storing perceptual hashes of previously processed images.
 * @param hasher OpenCV perceptual hash algorithm instance.
 * @param tag Pointer to the ID3v2 tag of the song file.
 */
static void	processSongImage(const t_paths &paths, std::vector<cv::Mat> &hashes, cv::Ptr<cv::img_hash::PHash> &hasher, TagLib::ID3v2::Tag *tag)
{
	// Retrieve ID3v2 tag and get the list of attached pictures (APIC frames)
	const TagLib::ID3v2::FrameList &frames = tag->frameList("APIC");

	if (frames.isEmpty())
		return;

	// Extract the first attached picture frame
	TagLib::ID3v2::AttachedPictureFrame *apic = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame *>(frames.front());
	if (!apic)
		return;

	// Get raw image data from the attached picture frame
	TagLib::ByteVector imgData = apic->picture();

	std::vector<uchar> imgBuffer(imgData.begin(), imgData.end());
	cv::Mat rawData(1, imgBuffer.size(), CV_8UC1, imgBuffer.data());

	// Decode the image from memory buffer as a color image
	cv::Mat img = cv::imdecode(rawData, cv::IMREAD_COLOR);
	if (img.empty())
		return;

	// Resize image to fixed size with high-quality Lanczos interpolation
	cv::resize(img, img, cv::Size(PIC_QUALITY, PIC_QUALITY), 0, 0, cv::INTER_LANCZOS4);

	// Set JPEG compression params: quality = 95 (high quality)
	std::vector<int> compression_params;
	compression_params.push_back(cv::IMWRITE_JPEG_QUALITY);
	compression_params.push_back(95);

	// Convert resized image to grayscale for hashing
	cv::Mat gray;
	cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);

	cv::Mat hash;
	hasher->compute(gray, hash);

	std::lock_guard<std::mutex> lock(g_imageMutex);

	// Check if this hash is already present (duplicate detection)
	for (const cv::Mat &existing : hashes)
	{
		if (cv::norm(hash, existing, cv::NORM_HAMMING) < HAMMING_THRESHOLD)
			return; // Duplicate found: skip saving
	}

	hashes.push_back(hash.clone());

	std::string output_path = paths.images + "/" + std::to_string(hashes.size()) + ".jpg";
	cv::imwrite(output_path, img, compression_params);
	{
		std::lock_guard<std::mutex> stats_lock(g_statsMutex);
		g_stats.newImages++;
	}
}

static void	songsThread(const std::vector<std::string> &song_files, size_t start, size_t end, const t_paths &paths, std::vector<cv::Mat> &hashes)
{
	size_t		i;
	Database	db(paths.root + "/songs.db");

	if (start >= end || end > song_files.size()) {
		std::cerr << "Invalid range [" << start << ", " << end << ")\n";
		return;
	}

	if (!db.open()) {
		std::cerr << "Failed to open database.\n";
		return;
	}

	cv::Ptr<cv::img_hash::PHash> hasher = cv::img_hash::PHash::create();

	i = start;
	while (i < end)
	{
		const std::string	&path = song_files[i];

		try {
			TagLib::MPEG::File	file(path.c_str());
			std::multimap<std::string, std::string> metadata;

			if (!file.isValid() || !file.ID3v2Tag()) {
				std::cerr << "Failed to read ID3v2 tag for " << path << "\n";
				std::lock_guard<std::mutex> lock(g_statsMutex);
				g_stats.errors++;
			}
			else {
				TagLib::ID3v2::Tag *tag = file.ID3v2Tag();
				const TagLib::ID3v2::FrameList &frames = tag->frameList();
				bool has42id = extractID3v2Metadata(frames, metadata);
				if (!has42id)
					handleNewFile(path, tag, file);
				processSongImage(paths, hashes, hasher, tag);
			}
		} catch (const std::exception &e) {
			std::cerr << "Exception while processing " << path << ": " << e.what() << "\n";
			std::lock_guard<std::mutex> lock(g_statsMutex);
			g_stats.errors++;
		}

		displayProgress(g_progressCount++, song_files.size());
		++i;
	}
	db.close();
}


void	processSongs(const t_paths &paths, std::vector<cv::Mat> &hashes)
{
	Database db(paths.root + "/songs.db");

	if (!db.open()) {
		std::cerr << "Failed to open database.\n";
		return;
	}

	if (!db.initSchema()) {
		std::cerr << "Failed to initialize schema.\n";
		db.close();
		return;
	}

	g_NumDbEntries = db.getLastSongId();

	db.close();

	const std::vector<std::string> songFiles = getFiles<std::string>(paths.songs, ".mp3");
	size_t	total = songFiles.size();

	log("Found " + std::to_string(total) + " songs in " + paths.songs + ".", true);
	log("Db entries: " + std::to_string(g_NumDbEntries.load()) + ", diff: " + std::to_string(total - g_NumDbEntries.load()), true);
	g_startTime = std::chrono::steady_clock::now();

	unsigned int				Nthreads = std::thread::hardware_concurrency();
	if (Nthreads == 0)
		Nthreads = 4;

	auto hasher = cv::img_hash::PHash::create();
	std::vector<std::thread> threads;
	for (unsigned int i = 0; i < Nthreads; ++i)
	{
		size_t start = i * (total / Nthreads);
		size_t end = (i == Nthreads - 1) ? total : start + (total / Nthreads);
		threads.emplace_back(songsThread, std::ref(songFiles), start, end, std::ref(paths), std::ref(hashes));
	}
	for (auto &t : threads)
		t.join();

	displayProgress(total, total);

	auto elapsed = std::chrono::steady_clock::now() - g_startTime;
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
	double seconds = ms / 1000.0;

	std::ostringstream oss;
	oss << std::fixed << std::setprecision(3) << seconds;

	std::cout << "\n";
	log("Done! Processed " + std::to_string(total) + " songs in " + oss.str() + " seconds.", true);
	g_progressCount = 0;
}
