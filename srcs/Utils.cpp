#include "../includes/Utils.hpp"

std::atomic<size_t> g_progressCount(0);
std::chrono::steady_clock::time_point g_startTime;

void	displayProgress(const size_t current, const size_t total)
{
	const int	barWidth = 60;
	float		progress = float(current) / float(total);
	int			pos = static_cast<int>(barWidth * progress);

	auto	elapsed = std::chrono::steady_clock::now() - g_startTime;
	auto	elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

	long long	estimatedTotalMs = 0;
	long long	remainingMs = 0;

	if (current > 0)
	{
		estimatedTotalMs = static_cast<long long>(elapsedMs / progress);
		remainingMs = estimatedTotalMs - elapsedMs;
	}

	int	rem_min = static_cast<int>(remainingMs / 1000 / 60);
	int	rem_sec = static_cast<int>((remainingMs / 1000) % 60);

	std::cout << current << "/" << total << " "
			<< rem_min << ":" << (rem_sec < 10 ? "0" : "") << rem_sec << " "
			<< "[";

	for (int i = 0; i < barWidth; ++i)
	{
		if (i <= pos)
			std::cout << "#";
		else
			std::cout << "-";
	}

	std::cout << "] " << int(progress * 100.0) << " %\r";
	std::cout.flush();
}

void	log(std::string message)
{
	std::chrono::steady_clock::time_point	now;
	std::chrono::duration<double>			time_span;
	std::time_t								now_c;
	std::tm									local_tm;

	now = std::chrono::steady_clock::now();

	std::chrono::system_clock::time_point	now_sys = std::chrono::system_clock::now();
	now_c = std::chrono::system_clock::to_time_t(now_sys);

	localtime_r(&now_c, &local_tm);

	std::cout << "[" << std::setfill('0')
		<< std::setw(2) << local_tm.tm_hour << ":"
		<< std::setw(2) << local_tm.tm_min << ":"
		<< std::setw(2) << local_tm.tm_sec;

	time_span = now_sys.time_since_epoch();
	long ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_span).count() % 1000;

	std::cout << "." << std::setw(3) << ms << "] " << message << "\n";
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

	if (paths.images.empty() || paths.songs.empty())
	{
		std::cerr << "Error: Missing IMG_DIR or SONGS_DIR in " << env_path << "\n";
		exit(EXIT_FAILURE);
	}

	return paths;
}
