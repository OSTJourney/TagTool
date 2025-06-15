#ifndef UTILS_HPP
# define UTILS_HPP

# include <atomic>
# include <chrono>
# include <cstdlib>
# include <fcntl.h>
# include <filesystem>
# include <fstream>
# include <iomanip>
# include <iostream>
# include <mutex>
# include <sstream>
# include <system_error>
# include <thread>
# include <unistd.h>
# include <vector>

# pragma GCC diagnostic ignored "-Woverloaded-virtual"
	# include <opencv2/opencv.hpp>
	# include <opencv2/img_hash.hpp>
# pragma GCC diagnostic pop

# include <taglib/id3v2tag.h>
# include <taglib/mpegfile.h>
# include <taglib/textidentificationframe.h>


# define PROGRESS_BAR_WIDTH 60

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
	std::string	root;
}	t_paths;

typedef struct s_stats
{
	size_t	newFiles;
	size_t	updatedFiles;
	size_t	newImages;
	size_t	errors;
}	t_stats;

extern std::mutex			g_statsMutex;
extern t_stats				g_stats;
extern std::ofstream		g_logFile;


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
 * @param console If true, also prints to console
 */
void	log(std::string message, bool console);

/**
 * @brief Reads the .env file and returns a t_paths struct with images and songs paths
 */
t_paths	getPathsFromEnv(const std::string &env_path);

/**
 * @brief Redirects stderr to a file
 *
 * @param filepath Path to the file where stderr will be redirected
 */
void	redirectStderrToFile(const std::string &filepath);

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

	try {
		if (!std::filesystem::exists(path))
		{
			if (!std::filesystem::create_directories(path))
			{
				std::cerr << "Failed to create directory: " << path << "\n";
				return result;
			}
		}

		std::error_code ec;
		std::filesystem::recursive_directory_iterator it(path, std::filesystem::directory_options::skip_permission_denied, ec);
		std::filesystem::recursive_directory_iterator end;

		while (it != end)
		{
			if (ec)
			{
				std::cerr << "Error while iterating: " << ec.message() << "\n";
				break;
			}

			if (it->is_regular_file(ec) && it->path().extension() == extension)
				result.push_back(StringType(it->path().string()));

			it.increment(ec);
		}
	}
	catch (const std::filesystem::filesystem_error &e)
	{
		std::cerr << "Filesystem error: " << e.what() << "\n";
	}
	catch (const std::exception &e)
	{
		std::cerr << "Unexpected error: " << e.what() << "\n";
	}

	return result;
}

void	processSongs(const t_paths &paths);

#endif
