#include "../includes/Utils.hpp"
#include "../includes/Database.hpp"

namespace			fs = std::filesystem;
static std::mutex g_file_mutex;

static void	processImages(const std::string &img_dir)
{
	std::vector<cv::String> imgFiles = getFiles<cv::String>(img_dir, ".jpg");
	size_t	total = imgFiles.size();

	log("Generating perceptual hashes for " + std::to_string(total) + " images...");
	g_startTime = std::chrono::steady_clock::now();

	cv::Mat img, hash;
	auto hasher = cv::img_hash::PHash::create();
	std::vector<cv::Mat> hashes;

	for (auto &f : imgFiles)
	{
		img = cv::imread(f, cv::IMREAD_GRAYSCALE);
		if (img.empty())
		{
			std::cerr << "failed to load " << f << "\n";
			continue;
		}
		hasher->compute(img, hash);
		hashes.push_back(hash.clone());
		displayProgress(g_progressCount++, total);
	}
	displayProgress(total, total);

	auto elapsed = std::chrono::steady_clock::now() - g_startTime;
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
	double seconds = ms / 1000.0;

	std::ostringstream oss;
	oss << std::fixed << std::setprecision(3) << seconds;

	std::cout << "\n";
	log("Done! Processed " + std::to_string(total) + " images in " + oss.str() + " seconds.");
	g_progressCount = 0;
}

static void	songsThread(const std::vector<std::string> &songFiles, size_t start, size_t end, const std::string &dbPath)
{
	size_t	i;
	Database	db(dbPath + "/songs.db");

	if (!db.open()) {
		std::cerr << "Failed to open database.\n";
		return;
	}

	i = start;
	while (i < end)
	{
		const std::string			&path = songFiles[i];
		TagLib::MPEG::File			file(path.c_str());
		std::ostringstream			buffer;
		TagLib::ID3v2::Tag			*tag;
		TagLib::ID3v2::FrameList	frames;

		buffer << "=== Metadata for: " << path << " ===" << std::endl;
		if (!file.isValid() || !file.ID3v2Tag())
			buffer << "No valid ID3v2 tag found." << std::endl << std::endl;
		else
		{
			tag = file.ID3v2Tag();
			frames = tag->frameList();
			for (TagLib::ID3v2::FrameList::Iterator it = frames.begin(); it != frames.end(); ++it)
			{
				TagLib::String		id = (*it)->frameID();
				TagLib::String		val = (*it)->toString();

				if (id == "APIC" || val.isEmpty())
					continue;
				buffer << "[" << id.to8Bit(true) << "] ";
				buffer << val.to8Bit(true) << std::endl;
			}

			buffer << std::endl;
		}

		{
			std::lock_guard<std::mutex>	lock(g_file_mutex);
			std::ofstream				outfile("all_metadata_dump.txt", std::ios::app);
			if (outfile.is_open())
				outfile << buffer.str();
		}

		displayProgress(g_progressCount++, songFiles.size());
		++i;
	}
	db.close();
}

static void	processSongs(const t_paths &paths)
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

	db.close();

	const std::vector<std::string> songFiles = getFiles<std::string>(paths.songs, ".mp3");
	size_t	total = songFiles.size();

	log("Found " + std::to_string(total) + " songs in " + paths.songs + ".");
	g_startTime = std::chrono::steady_clock::now();

	unsigned int				Nthreads = std::thread::hardware_concurrency();
	if (Nthreads == 0)
		Nthreads = 4;

	std::vector<std::thread> threads;
	for (unsigned int i = 0; i < Nthreads; ++i)
	{
		size_t start = i * (total / Nthreads);
		size_t end = (i == Nthreads - 1) ? total : start + (total / Nthreads);
		threads.emplace_back(songsThread, std::ref(songFiles), start, end, std::ref(paths.root));
	}
	for (auto &t : threads)
		t.join();

	displayProgress(total, total);

	auto elapsed = std::chrono::steady_clock::now() - g_startTime;
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
	double seconds = ms / 1000.0;

	std::ostringstream oss;
	oss << std::fixed << std::setprecision(3) << seconds;

	log("Done! Collected titles for " + std::to_string(total) + " songs in " + oss.str() + " seconds.");
	g_progressCount = 0;
}

int	main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	t_paths	paths = getPathsFromEnv(".env");
	redirectStderrToFile("errors.log");

	processImages(paths.images);
	processSongs(paths);

	return 0;
}


