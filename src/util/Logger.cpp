#include "Logger.h"

#include <chrono>
#include <ctime>

#include "Config.h"

namespace util
{
	void Logger::Log(const char *val)
	{
		if (!enabled)
			return;

		if (logLines >= 2048)
		{
			logfile.close();
			logfile.open(path, std::ios::out);
			logLines = 0;
		}

		char stime[128] = "";
		const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		if (const std::tm *tm = std::localtime(&now))
			std::strftime(stime, sizeof(stime), "%c", tm);

		logfile << stime << " | " << val << '\n';
		logfile.flush();
		logLines++;
	}

	Logger::Logger(const bool enabled) : enabled(enabled)
	{
		if (enabled)
		{
			path = GetModuleDirectory() + "SA.GPS.LOG.txt";
			logfile.open(path, std::ios::out);
		}
	}
} // namespace util
