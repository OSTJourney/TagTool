#include "../includes/Utils.hpp"

namespace			fs = std::filesystem;

static std::mutex	g_titlesMutex;

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

static void	songsThread(const std::vector<std::string> &songFiles, size_t start, size_t end, std::vector<std::string> &titles)
{
	for (size_t i = start; i < end; ++i)
	{
		const std::string &path = songFiles[i];
		TagLib::FileRef file(path.c_str());
		std::string title;

		if (!file.isNull() && file.tag())
			title = file.tag()->title().to8Bit(true);
		else
			title = "Unknown Title";

		{
			std::lock_guard<std::mutex> lock(g_titlesMutex);
			titles.push_back(title);
		}
		displayProgress(g_progressCount++, songFiles.size());
	}
}

static void	processSongs(const std::string &songs_dir)
{
	const std::vector<std::string> songFiles = getFiles<std::string>(songs_dir, ".mp3");
	size_t	total = songFiles.size();

	log("Found " + std::to_string(total) + " songs in " + songs_dir + ".");
	g_startTime = std::chrono::steady_clock::now();

	std::vector<std::string>	titles;
	unsigned int				Nthreads = std::thread::hardware_concurrency();
	if (Nthreads == 0)
		Nthreads = 4;

	std::vector<std::thread> threads;
	for (unsigned int i = 0; i < Nthreads; ++i)
	{
		size_t start = i * (total / Nthreads);
		size_t end = (i == Nthreads - 1) ? total : start + (total / Nthreads);
		threads.emplace_back(songsThread, std::ref(songFiles), start, end, std::ref(titles));
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
	processSongs(paths.songs);

	return 0;
}


