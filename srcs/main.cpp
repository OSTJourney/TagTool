#include "../includes/Utils.hpp"
#include "../includes/Database.hpp"

std::chrono::steady_clock::time_point	g_startTime;
std::atomic<size_t>						g_progressCount(0);
std::ofstream							g_logFile;

static void	processImages(const std::string &img_dir)
{
	std::vector<cv::String> imgFiles = getFiles<cv::String>(img_dir, ".jpg");
	size_t	total = imgFiles.size();

	log("Generating perceptual hashes for " + std::to_string(total) + " images...", true);
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
	log("Done! Processed " + std::to_string(total) + " images in " + oss.str() + " seconds.", true);
	g_progressCount = 0;
}

int	main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	g_logFile.open("info.log", std::ios::app);
	if (!g_logFile.is_open())
		std::cerr << "Failed to open log file\n";

	t_paths	paths = getPathsFromEnv(".env");
	redirectStderrToFile("errors.log");

	processImages(paths.images);
	processSongs(paths);

	if (g_logFile.is_open())
		g_logFile.close();

	return 0;
}


