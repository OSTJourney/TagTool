#include "../includes/Utils.hpp"


static std::mutex	g_coutMutex;
static std::mutex	g_logMutex;

void	displayProgress(const size_t current, const size_t total)
{
	float			progress = (float)current / (float)total;
	int				pos = (int)(PROGRESS_BAR_WIDTH * progress);
	float			percent = progress * 100.0f;

	static auto		startTime = std::chrono::steady_clock::now();
	static auto		lastTime = startTime;
	static float	lastPercent = -1.0f;

	static std::deque<long long>	durationList;
	static const size_t				maxPoints = 50;

	auto now = std::chrono::steady_clock::now();

	if (current == 0 || percent < lastPercent)
	{
		startTime = now;
		lastTime = now;
		lastPercent = -1.0f;
		durationList.clear();
	}
	else if (percent - lastPercent >= 0.1f)
	{
		auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTime).count();
		lastTime = now;

		durationList.push_back(durationMs);
		if (durationList.size() > maxPoints)
			durationList.pop_front();

		long long recentSum = 0;
		for (long long d : durationList)
			recentSum += d;
		double recentAvg = durationList.empty() ? 0.0 : (double)recentSum / durationList.size();

		auto totalElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
		double percentPoints = percent * 10.0f;
		double globalAvg = percentPoints > 0.0 ? totalElapsedMs / percentPoints : 0.0;

		double weightedAvg = 0.5 * recentAvg + 0.5 * globalAvg;

		int	remainingPoints = (int)((1000.0f - percentPoints) + 0.5f);
		long long remainingMs = (long long)(weightedAvg * remainingPoints);

		int	remMin = (int)(remainingMs / 1000 / 60);
		int	remSec = (int)((remainingMs / 1000) % 60);

		lastPercent = percent;

		std::lock_guard<std::mutex> lock(g_coutMutex);
		
		std::cout << "\r\033[K";
		std::cout << current << "/" << total << " "
				  << remMin << ":" << (remSec < 10 ? "0" : "") << remSec << " [";
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


void	log(std::string message, bool console)
{
	std::chrono::system_clock::time_point	now_sys = std::chrono::system_clock::now();
	std::time_t								now_c = std::chrono::system_clock::to_time_t(now_sys);
	std::tm									local_tm;

	localtime_r(&now_c, &local_tm);

	std::chrono::duration<double>			time_span = now_sys.time_since_epoch();
	long									ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_span).count() % 1000;

	std::ostringstream oss;
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
 */
static std::string	getEnvVar(const std::string &filepath, const std::string &key)
{
	std::ifstream file(filepath);
	if (!file.is_open())
		return std::string();

	std::string line;
	while (std::getline(file, line))
	{
		// Ignore empty lines and comments
		if (line.empty() || line[0] == '#')
			continue;

		std::size_t pos = line.find('=');
		if (pos == std::string::npos)
			continue;

		std::string k = line.substr(0, pos);
		std::string v = line.substr(pos + 1);

		// trim whitespaces from key and value if needed (optional)
		if (k == key)
			return v;
	}
	return std::string();
}

t_paths	getPathsFromEnv(const std::string &env_path)
{
	t_paths paths;
	paths.images = getEnvVar(env_path, "IMG_DIR");
	paths.songs = getEnvVar(env_path, "SONGS_DIR");
	paths.root = getEnvVar(env_path, "ROOT_DIR");
	if (paths.root.empty())
	{
		// If ROOT_DIR is not set, use the directory of the .env file
		paths.root = env_path.substr(0, env_path.find_last_of("/\\"));
	}

	if (paths.images.empty() || paths.songs.empty())
	{
		std::cerr << "Error: Missing IMG_DIR or SONGS_DIR in " << env_path << "\n";
		exit(EXIT_FAILURE);
	}

	return paths;
}

void	redirectStderrToFile(const std::string &filepath)
{
	int	fd = open(filepath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd != -1)
		dup2(fd, STDERR_FILENO);
	else
		std::cerr << "Failed to redirect stderr to " << filepath << "\n";
}
