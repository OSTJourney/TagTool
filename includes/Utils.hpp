#ifndef UTILS_HPP
# define UTILS_HPP

# include <atomic>
# include <chrono>
# include <cstdlib>
# include <fcntl.h>
# include <filesystem>
# include <fstream>
# include <iostream>
# include <mutex>
# include <system_error>
# include <unistd.h>
# include <vector>

# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Woverloaded-virtual"
	#include <opencv2/opencv.hpp>
	#include <opencv2/img_hash.hpp>
# pragma GCC diagnostic pop

# include <taglib/attachedpictureframe.h>
# include <taglib/id3v2tag.h>
# include <taglib/mpegfile.h>
# include <taglib/textidentificationframe.h>

# define PROGRESS_BAR_WIDTH	60
# define PIC_QUALITY		512 // 512 is a good compromise between quality and size for images
# define HAMMING_THRESHOLD	8 // Threshold for perceptual hash similarity

/**
 * @brief Atomic counter for progress tracking
 */
extern	std::atomic<size_t>						g_progressCount;

/**
 * @brief Global start time for measuring elapsed time
 */
extern	std::chrono::steady_clock::time_point	g_startTime;

/**
 * @brief Mutex for protecting statistics updates
 * @details Contains:
 * 	- images:	Path to images directory
 * 	- songs:	Path to songs directory
 * 	- root:		Root directory
 */
typedef struct	s_paths
{
	std::string	images;	// path to images directory
	std::string	songs;	// path to songs directory
	std::string	root;	// root directory
}	t_paths;

/**
 * @brief Structure to hold image filename and its perceptual hash
 * @details Contains:
 * 	- filename:	The name of the image file
 * 	- hash:		The perceptual hash of the image
 */
struct s_imageHash {
	std::string	filename;	// image file name
	cv::Mat		hash;		// perceptual hash
};

/**
 * @brief Structure to hold processing statistics
 * @details Contains:
 * 	- newFiles:		number of new files processed
 * 	- updatedFiles:	number of files updated
 * 	- newImages:	number of new images added
 * 	- errors:		number of errors encountered
 */
typedef struct	s_stats
{
	size_t	newFiles;		// number of new files processed
	size_t	updatedFiles;	// number of files updated
	size_t	newImages;		// number of new images added
	size_t	errors;			// number of errors encountered
}	t_stats;

extern std::mutex			g_statsMutex;	// mutex for statistics
extern t_stats				g_stats;		// global statistics
extern std::ofstream		g_logFile;		// global log file


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
 * @brief Parse a simple .env file and return paths
 * @param env_path The path to the .env file
 * @return t_paths The structure containing the paths
 */
t_paths	getPathsFromEnv(const std::string &env_path);

/**
 * @brief Redirects stderr to a file
 *
 * @param filepath Path to the file where stderr will be redirected
 */
void	redirectStderrToFile(const std::string &filepath);

/**
 * @brief Convert a multimap of metadata to a JSON string
 * @param metadata The multimap containing metadata key-value pairs
 * @return A JSON formatted string representing the metadata
 */
std::string multimapToJson(const std::multimap<std::string, std::string> &metadata);

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

/**
 * @brief Process songs to compute their perceptual hashes
 * @param paths The paths structure containing songs directory
 * @param hashes Vector to store computed hashes
 */
void	processSongs(const t_paths &paths, std::vector<s_imageHash> &hashes);

#endif
