#include "../includes/Utils.hpp"


static std::mutex	g_coutMutex;
static std::mutex	g_logMutex;

void	displayProgress(
	const size_t	current,
	const size_t	total)
{
	float			progress = (float)current / (float)total;	// Progress ratio between 0.0 and 1.0
	int				pos = (int)(PROGRESS_BAR_WIDTH * progress);	// Position in the progress bar
	float			percent = progress * 100.0f;				// Percentage completed

	static auto		startTime = std::chrono::steady_clock::now();	// Retain start time across calls
	static auto		lastTime = startTime;							// Retain last update time across calls
	static float	lastPercent = -1.0f;										// Retain last percent update across calls

	static std::deque<long long>	durationList;	// List of recent durations for averaging
	static const size_t				maxPoints = 50;	// Maximum number of points to keep for averaging

	auto now = std::chrono::steady_clock::now();	// Current time

	if (current == 0 || percent < lastPercent)
	{
		startTime = now;
		lastTime = now;
		lastPercent = -1.0f;
		durationList.clear();
	}
	else if (percent - lastPercent >= 0.1f)
	{
		auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime).count();	// Duration since last update
		lastTime = now;

		durationList.push_back(durationMs);
		if (durationList.size() > maxPoints)
			durationList.pop_front();

		long long	recentSum = 0; // Sum of recent durations
		for (long long d : durationList)
			recentSum += d;
		double	recentAvg = durationList.empty() ? 0.0 : (double)recentSum / durationList.size(); // Average of recent durations

		auto	totalElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();	// Total elapsed time since start
		double	percentPoints = percent * 10.0f;																			// Total points completed (0 to 1000)
		double	globalAvg = percentPoints > 0.0 ? totalElapsedMs / percentPoints : 0.0;										// Global average duration per point

		double	weightedAvg = 0.5 * recentAvg + 0.5 * globalAvg;	// Weighted average of recent and global averages

		int			remainingPoints = (int)((1000.0f - percentPoints) + 0.5f);	// Remaining points to complete
		long long	remainingMs = (long long)(weightedAvg * remainingPoints);	// Estimated remaining time in milliseconds

		int	remMin = (int)(remainingMs / 1000 / 60);	// Remaining minutes
		int	remSec = (int)((remainingMs / 1000) % 60);	// Remaining seconds

		lastPercent = percent;

		std::lock_guard<std::mutex> lock(g_coutMutex);
		
		std::cout	<< "\x1b[u\x1b[2K";	// Restore cursor position and clear line
		/* Print progress bar with the format
		 *				current/total remMin:remSec [##########----------] XX.X % | new: N, updated: N, images: N, errors: N */
		std::cout <<	current << "/" << total << " " << remMin << ":" << (remSec < 10 ? "0" : "") << remSec << " [";
		for (int i = 0; i < PROGRESS_BAR_WIDTH; ++i)
			std::cout << (i <= pos ? '#' : '-');
		std::cout << "] ";
		{
			std::lock_guard<std::mutex> stats_lock(g_statsMutex);
			std::cout << std::fixed << std::setprecision(1)
					  << percent << " % | new: " << g_stats.newFiles
					  << ", updated: "  << g_stats.updatedFiles
					  << ", images: "   << g_stats.newImages
					  << ", errors: "   << g_stats.errors;
		}

		std::cout.flush();
	}
}

void	log(
	std::string	message,
	bool		console)
{
	std::chrono::system_clock::time_point	now_sys = std::chrono::system_clock::now();					// Current system time
	std::time_t								now_c = std::chrono::system_clock::to_time_t(now_sys);	// Convert to time_t
	std::tm									local_tm;

	localtime_r(&now_c, &local_tm);

	std::chrono::duration<double>			time_span = now_sys.time_since_epoch();														// Duration since epoch
	long									ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_span).count() % 1000;	// Milliseconds part

	std::ostringstream oss;
	/* Format timestamp as [HH:MM:SS.ms] */
	oss << "[" << std::setfill('0')
		<< std::setw(2) << local_tm.tm_hour << ":"
		<< std::setw(2) << local_tm.tm_min << ":"
		<< std::setw(2) << local_tm.tm_sec << "."
		<< std::setw(3) << ms << "] " << message << "\n";

	{
		std::lock_guard<std::mutex> lock(g_logMutex);
		if (g_logFile.is_open())
			g_logFile << oss.str();
		{
			std::lock_guard<std::mutex> cout_lock(g_coutMutex);
			if (console)
				std::cout << oss.str();
		}
	}
}

/**
 * @brief Parse a simple .env file and return the value of the given key
 *
 * @param filepath The path to the .env file
 * @param key The environment variable key to look for
 * @return The value of the environment variable, or an empty string if not found
 */
static std::string	getEnvVar(
	const std::string	&filepath,
	const std::string	&key)
{
	std::ifstream	file(filepath); // Stream of the .env file
	if (!file.is_open())
		return std::string(); // Return empty string if file cannot be opened

	std::string line;
	while (std::getline(file, line))
	{
		if (line.empty() || line[0] == '#')	// Ignore empty lines and comments
			continue;

		std::size_t	pos = line.find('='); // Find the '=' character
		if (pos == std::string::npos)
			continue;

		std::string	k = line.substr(0, pos);		// Key is the part before '='
		std::string	v = line.substr(pos + 1);			// Value is the part after '='

		if (k == key)
			return v;
	}
	return (std::string());
}

t_paths	getPathsFromEnv(const std::string &env_path)
{
	t_paths	paths;	// Structure to hold the paths
	paths.images	= getEnvVar(env_path, "IMG_DIR");
	paths.songs		= getEnvVar(env_path, "SONGS_DIR");
	paths.root		= getEnvVar(env_path, "ROOT_DIR");
	if (paths.root.empty())
		paths.root = env_path.substr(0, env_path.find_last_of("/\\"));	// If ROOT_DIR is not set, use the directory of the .env file

	if (paths.images.empty() || paths.songs.empty())
	{
		std::cerr << "Error: Missing IMG_DIR or SONGS_DIR in " << env_path << "\n";
		exit(EXIT_FAILURE);	// Exit if required paths are missing
	}

	return (paths);
}

void	redirectStderrToFile(const std::string &filepath)
{
	int	fd = open(filepath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd != -1)
		dup2(fd, STDERR_FILENO);
	else
		std::cerr << "Failed to redirect stderr to " << filepath << "\n";
}

/**
 * @brief Escape special characters in a JSON string
 * @param s The input string to escape
 * @return The escaped JSON string
 */
static std::string escapeJson(const std::string &s)
{
	std::ostringstream	o;
	for (char c : s) {
		switch (c) {
			case '"': o << "\\\""; break;
			case '\\': o << "\\\\"; break;
			case '\b': o << "\\b"; break;
			case '\f': o << "\\f"; break;
			case '\n': o << "\\n"; break;
			case '\r': o << "\\r"; break;
			case '\t': o << "\\t"; break;
			default: o << c;
		}
	}
	return (o.str());
}

std::string multimapToJson(const std::multimap<std::string, std::string> &metadata)
{
	std::map<std::string, std::vector<std::vector<std::string>>>	grouped;	// Grouped key-value pairs

	for (auto &kv : metadata)	// Sort TXXX and others frames into two categories
	{
		const std::string	&key = kv.first;	// Key from the metadata
		const std::string	&value = kv.second;	// Value from the metadata

		if (key == "TXXX")
			grouped["TXXX"].push_back({key, value});
		else
			grouped["Other"].push_back({key, value});
	}

	std::ostringstream	json;
	json << "{";

	bool	firstCategory = true;
	for (auto &cat : grouped) {
		if (!firstCategory) json << ", ";
		firstCategory = false;

		json << "\"" << cat.first << "\": [";
		bool firstPair = true;
		for (auto &pair : cat.second) {
			if (!firstPair) json << ", ";
			firstPair = false;
			json << "[\"" << escapeJson(pair[0]) << "\", \"" << escapeJson(pair[1]) << "\"]";
		}
		json << "]";
	}

	json << "}";
	return (json.str());
}
