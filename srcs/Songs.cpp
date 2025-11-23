#include <cmath>
#include <cstddef>
#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <thread>

#include "../includes/Utils.hpp"
#include "../includes/Database.hpp"

/**
 * @brief Structure representing a partial file with metadata.
 * @details Contains:
 * 	- id:		Unique identifier.
 * 	- imageId:	Image identifier.
 * 	- duration:	Duration in seconds.
 * 	- path:		File path.
 * 	- metadata:	Metadata key-value pairs.
 */
struct s_partialFile
{
	size_t									id;			// unique identifier
	size_t									imageId;	// image identifier
	double									duration;	// duration in seconds
	std::string								path;		// file path
	std::multimap<std::string, std::string>	metadata;	// metadata key-value pairs
};

static std::atomic<size_t>	g_NumDbEntries(0);	// Global atomic counter for number of DB entries
static std::mutex			g_numDbEntriesMutex;	// Mutex for protecting g_NumDbEntries
std::mutex					g_statsMutex;			// mutex for statistics
t_stats						g_stats = {0, 0, 0, 0, 0};		// global statistics
static std::mutex			g_imageMutex;			// Mutex for protecting image processing

static const int			DB_COMMIT_INTERVAL = 200;	// Number of songs to process before committing to DB

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
 * @return The unique identifier assigned to the new file.
 */
static size_t handleNewFile(
	const std::string	&path,
	TagLib::ID3v2::Tag	*tag,
	TagLib::MPEG::File	&file)
{
	{
		std::lock_guard<std::mutex>	lock(g_numDbEntriesMutex);	// Lock mutex to protect g_NumDbEntries
		size_t	id = g_NumDbEntries + 1;									// New unique ID
		auto	*frame_id = new TagLib::ID3v2::UserTextIdentificationFrame;	// Create new TXXX frame
		frame_id->setDescription("42id");
		frame_id->setText(std::to_string(id));
		tag->addFrame(frame_id);

		if (!file.save()) {
			std::cerr << "Failed to save ID3v2 tag for " << path << "\n";
			std::lock_guard<std::mutex> lock(g_statsMutex);	// Lock stats mutex
			g_stats.errors++;
			return (0);
		}
		g_NumDbEntries++;
	}
	{
		std::lock_guard<std::mutex> lock(g_statsMutex); // Lock stats mutex
		g_stats.newFiles++;
	}
	log("Adding new song: " + path, false);
	return (g_NumDbEntries);
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
static bool	extractID3v2Metadata(
	const TagLib::ID3v2::FrameList			&frames,
	std::multimap<std::string, std::string>	&metadata)
{
	bool	has42id = false; // Flag to indicate presence of "42id" frame

	for (TagLib::ID3v2::FrameList::ConstIterator it = frames.begin(); it != frames.end(); ++it)	// Iterate through all frames
	{
		TagLib::String	id = (*it)->frameID();	// Get frame ID
		std::string		key = id.to8Bit(true);	// Convert frame ID to std::string

		if (id == "APIC") // Skip Cover Art frames
			continue;

		if (key == "TXXX") { // User-defined text information frame
			TagLib::ID3v2::UserTextIdentificationFrame	*uf
				= dynamic_cast<TagLib::ID3v2::UserTextIdentificationFrame *>(*it);

			if (uf) {
				std::string	desc = uf->description().to8Bit(true);	// The TXXX frame description
				std::string	val = (uf->fieldList().size() > 1)				// The TXXX frame value
					? uf->fieldList()[1].to8Bit(true) : ""; // If multiple fields, take the second field as value, else empty.		

				metadata.insert(std::make_pair("TXXX:" + desc, val)); // Store as "TXXX:description" -> value

				if (desc == "42id")
					has42id = true;
			}
		}
		else {
			std::string	val = (*it)->toString().to8Bit(true);	// Convert frame value to std::string
			if (!val.empty())
				metadata.insert(std::make_pair(key, val));
		}
	}

	return (has42id);
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
 * @param hashes Vector to store perceptual names and hashes of processed images.
 * @param hasher OpenCV perceptual hash algorithm instance.
 * @param tag Pointer to the ID3v2 tag of the song file.
 * @return The unique ID assigned to the image, or 0 if no image or duplicate found.
 */
static size_t	processSongImage(
	const t_paths					&paths,
	std::vector<s_imageHash>		&hashes,
	cv::Ptr<cv::img_hash::PHash>	&hasher,
	TagLib::ID3v2::Tag				*tag)
{
	// Retrieve ID3v2 tag and get the list of attached pictures (APIC frames)
	const TagLib::ID3v2::FrameList	&frames = tag->frameList("APIC");

	if (frames.isEmpty())
		return 0;

	// Extract the first attached picture frame
	TagLib::ID3v2::AttachedPictureFrame	*apic = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame *>(frames.front());
	if (!apic)
		return 0;

	// Get raw image data from the attached picture frame
	TagLib::ByteVector	imgData = apic->picture();

	std::vector<uchar>	imgBuffer(imgData.begin(), imgData.end());								// Convert ByteVector to std::vector<uchar>
	cv::Mat				rawData(1, imgBuffer.size(), CV_8UC1, imgBuffer.data());	// Create OpenCV Mat from raw image data

	// Decode the image from memory buffer as a color image
	cv::Mat	img = cv::imdecode(rawData, cv::IMREAD_COLOR);
	if (img.empty())
		return 0;

	// Resize image to fixed size with high-quality Lanczos interpolation
	cv::resize(img, img, cv::Size(PIC_QUALITY, PIC_QUALITY), 0, 0, cv::INTER_LANCZOS4);

	// Set JPEG compression params: quality = 95 (high quality)
	std::vector<int>	compression_params;
	compression_params.push_back(cv::IMWRITE_JPEG_QUALITY);
	compression_params.push_back(95);

	// Convert resized image to grayscale for hashing
	cv::Mat	gray;
	cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);

	cv::Mat	hash;
	hasher->compute(gray, hash);

	std::string	output_path;
	size_t		id;
	{
		std::lock_guard<std::mutex>	lock(g_imageMutex);

		// Check if this hash is already present (duplicate detection)
		for (const s_imageHash &existing : hashes)
		{
			if (cv::norm(hash, existing.hash, cv::NORM_HAMMING) < HAMMING_THRESHOLD)
			{
				size_t	id = 0;	// Extract ID from existing image filename
				try
				{
					size_t start = existing.filename.find_last_of("/\\") + 1;
					size_t len   = existing.filename.find_last_of('.') - start;
					id = static_cast<size_t>(std::stoul(existing.filename.substr(start, len)));
				}
				catch (const std::exception &)
				{
					std::cerr << "Error extracting image ID from filename: " << existing.filename << "\n";
					id = 0;
				}
				return (id);
			}
		}

		id = hashes.size() + 1;
		output_path = paths.images + "/" + std::to_string(id) + ".jpg";
		hashes.push_back({output_path, hash.clone()});
	}

	cv::imwrite(output_path, img, compression_params);	// Save image as JPEG

	{
		std::lock_guard<std::mutex> stats_lock(g_statsMutex);
		g_stats.newImages++;
	}
	return (id);
}

/**
 * @brief Update a song record with data from a partial file.
 * @param song Reference to the song record to update.
 * @param partialFile Reference to the partial file containing new data.
 * @return true if any changes were made, false otherwise.
 */
static bool	updates_songRecord(
	s_songRecord	&song,
	s_partialFile	&partialFile)
{
	std::string	jsonMeta	= multimapToJson(partialFile.metadata);	// Convert metadata to JSON string
	bool		hasChanges	= false;	// Flag to indicate if any changes were made

	// Generic updater for any assignable type
	auto updateField = [&](auto &field, const auto &value) {
		if (field != value) {
			field = value;
			hasChanges = true;
		}
	};

	// Specific updater for metadata strings
	auto updateStringFromMeta = [&](std::string &field, const std::string &key) {
		auto range = partialFile.metadata.equal_range(key);
		if (range.first != range.second) {
			const std::string &newValue = range.first->second;
			updateField(field, newValue);
		}
	};

	// Id
	updateField(song.id, std::to_string(partialFile.id));

	// Cover
	{
		std::optional<size_t> newCover = (partialFile.imageId != 0)
			? std::optional<size_t>{partialFile.imageId}
			: std::nullopt;

		updateField(song.cover, newCover);
	}

	// Duration, Path, JSON Tags
	updateField(song.duration, partialFile.duration);
	updateField(song.path, partialFile.path);
	updateField(song.tags, jsonMeta);

	// Metadata fields
	updateStringFromMeta(song.title, "TIT2");
	updateStringFromMeta(song.artist, "TPE1");
	updateStringFromMeta(song.album, "TALB");

	return (hasChanges);
}

/**
 * @brief Save or update a song record in the database.
 * 
 * This function saves a new song record or updates an existing one
 * in the database based on the provided metadata. It constructs a
 * s_songRecord from the metadata and uses the Database instance to
 * insert or update the record.
 * 
 * @param db Reference to the Database instance.
 * @param partialFile Reference to the s_partialFile containing song metadata.
 * @param isNew Flag indicating whether to insert a new record (true) or update an existing one (false).
 * @return true if the operation was successful, false otherwise.
 */
static bool saveSong(
	Database					&db,
	std::vector<s_songRecord>	&threadBuffer,
	s_partialFile				&partialFile,
	bool						isNew)
{
	s_songRecord	song;  // Song record to be saved or updated
	bool			isRecovered = false;

	// Only try fetching if caller says it's an update
	if (!isNew) {
		try {
			song = db.getSongById(std::to_string(partialFile.id));
		}
		catch (const Database::RecordNotFound &) {
			isNew = true;
			isRecovered = true;
		}
		catch (const std::exception &e) {
			std::cerr << "Error fetching song with id " << partialFile.id << ": " << e.what() << "\n";
			return (false);
		}
	}

	if (!updates_songRecord(song, partialFile))
		return (false);

	threadBuffer.push_back(std::move(song));

	// Update stats based on final resolved state
	{
		std::lock_guard<std::mutex> lock(g_statsMutex);
		if (isRecovered)
			g_stats.recoveredFiles++;
		else if (isNew)
			g_stats.newFiles++;
		else
			g_stats.updatedFiles++;
	}

	return (true);
}

/**
 * @brief Thread function to process a range of song files.
 *
 * @param song_files Vector of song file paths to process.
 * @param start Starting index of the range to process.
 * @param end Ending index (exclusive) of the range to process.
 * @param paths Struct containing output paths (e.g., image directory).
 * @param hashes Vector to store perceptual hashes of processed images.
 */
static void	songsThread(
	const std::vector<std::string>	&song_files,
	size_t							start,
	size_t							end,
	const t_paths					&paths,
	std::vector<s_imageHash>		&hashes)
{
	size_t						i;										// Loop index
	int							c;										// DB Commit index
	Database					db(paths.root + "/songs.db");	// SQLite database instance
	std::vector<s_songRecord>	threadBuffer;							// Buffer for batch DB operations

	if (start >= end || end > song_files.size()) {
		std::cerr << "Invalid range [" << start << ", " << end << ")\n";
		return;
	}

	if (!db.open()) {
		std::cerr << "Failed to open database.\n";
		return;
	}

	cv::Ptr<cv::img_hash::PHash>	hasher = cv::img_hash::PHash::create();	// Perceptual hash algorithm instance

	i = start;
	c = 0;
	//db.beginTransaction();
	while (i < end)
	{
		const std::string	&path = song_files[i];	// Current song file path

		try {
			TagLib::MPEG::File						file(path.c_str());	// Open MP3 file with TagLib
			std::multimap<std::string, std::string>	metadata;					// Metadata storage
			double									duration = 0.0;				// Song duration

			if (!file.isValid() || !file.ID3v2Tag() || file.audioProperties() == nullptr)
			{
				std::cerr << "Failed to read file: `" << path << "`\n";
				{
					std::lock_guard<std::mutex> lock(g_statsMutex);
					g_stats.errors++;
				}
			}
			else {
				TagLib::ID3v2::Tag				*tag	= file.ID3v2Tag();								// Get ID3v2 tag
				const TagLib::ID3v2::FrameList	&frames	= tag->frameList();								// Get all frames in the tag
				bool							has42id	= extractID3v2Metadata(frames, metadata);	// Extract metadata and check for "42id"
				s_partialFile					pfile;													// Partial file structure

				duration = file.audioProperties()->lengthInMilliseconds() / 1000.0;	// Get song duration in seconds
				if (!has42id)
					pfile.id = handleNewFile(path, tag, file);
				else {
					auto	it = metadata.find("TXXX:42id");
					if (it != metadata.end())
						pfile.id = static_cast<size_t>(std::stoul(it->second));
				}
				pfile.imageId	= processSongImage(paths, hashes, hasher, tag);
				pfile.duration	= duration;
				pfile.path		= path;
				pfile.metadata	= metadata;

				c += static_cast<int>(saveSong(db, threadBuffer, pfile, !has42id));
			}
		} catch (const std::exception &e) {
			std::cerr << "Exception while processing file: `" << path << ": " << e.what() << "`\n";
			{
				std::lock_guard<std::mutex> lock(g_statsMutex);
				g_stats.errors++;
			}
		}

		if (c >= DB_COMMIT_INTERVAL) {
			db.beginTransaction();
			for (const auto &song : threadBuffer)
			{
				try {
					bool	result = db.upsertSong(song, false);	// Upsert song record
					if (!result)
					{
						std::cerr << "Failed to upsert song ID " << song.id << "\n";
						std::lock_guard<std::mutex> lock(g_statsMutex);
						g_stats.errors++;
					}
				} catch (const std::exception &e) {
					std::cerr << "Exception while upserting song ID " << song.id << ": " << e.what() << "\n";
					std::lock_guard<std::mutex> lock(g_statsMutex);
					g_stats.errors++;
				}
				
				
			}
			db.commitTransaction();
			threadBuffer.clear();
			c = 0;
		}
		displayProgress(g_progressCount++, song_files.size());
		++i;
	}
	db.close();
}


void	processSongs(
	const t_paths			&paths,
	std::vector<s_imageHash>	&hashes)
{
	Database	db(paths.root + "/songs.db");	// SQLite database instance

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

	const std::vector<std::string>	songFiles = getFiles<std::string>(paths.songs, ".mp3");	// Get all MP3 files in songs directory
	size_t							total = songFiles.size();						// Total number of song files

	log("Found " + std::to_string(total) + " songs in " + paths.songs + ".", true);
	size_t	diff = (total > g_NumDbEntries.load()) ? (total - g_NumDbEntries.load()) : 0;
	log("Db entries: " + std::to_string(g_NumDbEntries.load()) + ", files to process: " + std::to_string(total) + " (new: " + std::to_string(diff) + ")", true);

	g_startTime = std::chrono::steady_clock::now();

	unsigned int	Nthreads = std::thread::hardware_concurrency();	// Get number of available hardware threads
	if (Nthreads == 0)
		Nthreads = 4;

	std::vector<std::thread>	threads;								// Vector to hold threads
	for (unsigned int i = 0; i < Nthreads; ++i)
	{
		size_t	start = i * (total / Nthreads);									// Calculate start index for this thread
		size_t	end = (i == Nthreads - 1) ? total : start + (total / Nthreads);	// Calculate end index for this thread
		threads.emplace_back(songsThread, std::ref(songFiles), start, end, std::ref(paths), std::ref(hashes));
	}
	for (auto &t : threads)
		t.join();

	displayProgress(total, total);

	auto	elapsed = std::chrono::steady_clock::now() - g_startTime;								// Calculate elapsed time
	auto	ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();	// Convert to milliseconds
	double	seconds = ms / 1000.0;																	// Convert to seconds

	std::ostringstream	oss;
	oss << std::fixed << std::setprecision(3) << seconds;

	std::cout << "\n";
	log("Done! Processed " + std::to_string(total) + " songs in " + oss.str() + " seconds.", true);
	g_progressCount = 0;
}
