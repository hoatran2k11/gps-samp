#pragma once
#include <fstream>
#include <string>

namespace util
{
	class Logger
	{
	  private:
		bool enabled = false;
		unsigned short logLines = 0;
		std::string path;
		std::ofstream logfile;

	  public:
		void Log(const char *val);
		Logger() = default;
		explicit Logger(bool enabled);
	};
} // namespace util
