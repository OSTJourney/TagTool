#include "../includes/Utils.hpp"

namespace fs = std::filesystem;

int	main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	t_paths	paths = getPathsFromEnv(".env");

	std::vector<cv::String> files = getFiles<cv::String>(paths.images, ".jpg");
	log("Generating perceptual hashes for " + std::to_string(files.size()) + " images...");
	g_startTime = std::chrono::steady_clock::now();

	cv::Mat img, hash;
	auto hasher = cv::img_hash::PHash::create();
	std::vector<cv::Mat> hashes;

	for (auto &f : files)
	{
		img = cv::imread(f, cv::IMREAD_GRAYSCALE);
		if (img.empty())
		{
			std::cerr << "failed to load " << f << "\n";
			continue;
		}
		hasher->compute(img, hash);
		hashes.push_back(hash.clone());
		displayProgress(g_progressCount++, files.size());
	}
	displayProgress(files.size(), files.size());

	auto elapsed = std::chrono::steady_clock::now() - g_startTime;
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
	double seconds_with_ms = ms / 1000.0;

	std::ostringstream oss;
	oss << std::fixed << std::setprecision(3) << seconds_with_ms;

	std::cout << "\n";
	log("Done! Processed " + std::to_string(files.size()) + " images in " + oss.str() + " seconds.");

	return 0;
}

