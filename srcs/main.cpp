#include "../includes/Utils.hpp"

std::chrono::steady_clock::time_point	g_startTime;
std::atomic<size_t>						g_progressCount(0);
std::ofstream							g_logFile;

/**
 * @brief Process images in the specified directory to generate perceptual hashes.
 * @param img_dir The directory containing images.
 * @return A vector of s_imageHash structs containing the filenames and their hashes.
 */
static std::vector<s_imageHash>	processImages(const std::string &img_dir)
{
	std::vector<cv::String>	imgFiles = getFiles<cv::String>(img_dir, ".jpg"); // vector of image file paths
	size_t					total = imgFiles.size(); // total number of images

	if (total == 0)
	{
		log("No images found in " + img_dir + ".", true);
		return {};
	}

	log("Generating perceptual hashes for " + std::to_string(total) + " images...", true);
	g_startTime = std::chrono::steady_clock::now();

	std::vector<s_imageHash>	hashes;
	cv::Mat					img, hash;
	auto					hasher = cv::img_hash::PHash::create();	// perceptual hash algorithm

	for (auto &f : imgFiles)
	{
		img = cv::imread(f, cv::IMREAD_GRAYSCALE);
		if (img.empty())
		{
			std::cerr << "failed to load " << f << "\n";
			continue;
		}
		hasher->compute(img, hash);
		hashes.push_back({f, hash.clone()});
		displayProgress(g_progressCount++, total);
	}
	displayProgress(total, total);

	auto	elapsed = std::chrono::steady_clock::now() - g_startTime;								// elapsed time
	auto	ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();	// milliseconds
	double	seconds = ms / 1000.0;																	// convert to seconds

	std::ostringstream	oss;	// output string stream for formatting
	oss << std::fixed << std::setprecision(3) << seconds;

	std::cout << "\n";
	log("Done! Processed " + std::to_string(total) + " images in " + oss.str() + " seconds.", true);
	g_progressCount = 0;
	return (hashes);
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

	std::vector<s_imageHash>	hashes = processImages(paths.images);
	processSongs(paths, hashes);

	if (g_logFile.is_open())
		g_logFile.close();

	return (0);
}
