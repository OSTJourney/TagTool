#ifndef UTILS_HPP
# define UTILS_HPP

# include <atomic>
# include <chrono>
# include <cstdlib>
# include <filesystem>
# include <fstream>
# include <iomanip>
# include <iostream>
# include <sstream>
# include <vector>

# pragma GCC diagnostic ignored "-Woverloaded-virtual"
	# include <opencv2/opencv.hpp>
	# include <opencv2/img_hash.hpp>
# pragma GCC diagnostic pop

/**
 * @brief Atomic counter for progress tracking
 */
extern std::atomic<size_t>						g_progressCount;

/**
 * @brief Global start time for measuring elapsed time
 */
extern std::chrono::steady_clock::time_point	g_startTime;

typedef struct s_paths
{
	std::string	images;
	std::string	songs;
}	t_paths;

/**
 * @brief Prints a console progress bar with estimated remaining time.
 * 
 * Shows current progress out of total, a 60-character bar, percentage,
 * and estimated time left based on elapsed time from global g_startTime.
 * 
 * @param current Current progress step (0 to total).
 * @param total Total number of steps.
 */
void	displayProgress(const size_t current, const size_t total);

/**
 * @brief Logs a message with current time timestamp [HH:MM:SS.ms]
 *
 * @param message The string message to log
 */
void	log(std::string message);

/**
 * @brief Reads the .env file and returns a t_paths struct with images and songs paths
 */
t_paths	getPathsFromEnv(const std::string &env_path);

/**
 * @brief Recursively find files with a given extension
 *
 * @param start_path Path to start the search
 * @param extension File extension to match (e.g. ".txt")
 * @return std::vector<StringType> A vector of file paths with the specified extension
 */
template<typename StringType>
std::vector<StringType> getFiles(const std::string &path, const std::string &extension)
{
	std::vector<StringType> result;
	std::filesystem::recursive_directory_iterator it(path);
	std::filesystem::recursive_directory_iterator end;

	while (it != end)
	{
		if (it->is_regular_file() && it->path().extension() == extension)
			result.push_back(StringType(it->path().string()));
		++it;
	}
	return result;
}

#endif
