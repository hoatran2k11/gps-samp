#pragma once
#include <string>

#include "CRGBA.h"

namespace util
{
	struct Config
	{
		bool RESPECT_LANE_DIRECTION = true;
		float GPS_LINE_WIDTH = 3.0f;
		bool ENABLE_BMX = false;
		bool ENABLE_WATER_GPS = true;
		bool ENABLE_MOVING = false;
		float DISABLE_PROXIMITY = 40.0f;

		bool ENABLE_DISTANCE_TEXT = false;
		bool DISTANCE_UNITS = false;

		bool ENABLE_CUSTOM_CLRS = false;
		CRGBA CC_RED = {255, 0, 0, 255};
		CRGBA CC_GREEN = {0, 255, 0, 255};
		CRGBA CC_BLUE = {0, 0, 255, 255};
		CRGBA CC_WHITE = {255, 255, 255, 255};
		CRGBA CC_PURPLE = {255, 0, 255, 255};
		CRGBA CC_YELLOW = {255, 255, 0, 255};
		CRGBA CC_CYAN = {0, 255, 255, 255};
		CRGBA GPS_LINE_CLR = {200, 16, 16, 255};

		int UPDATE_INTERVAL_MS = 500;
		float MIN_MOVE_DISTANCE = 10.0f;

		bool MP_ENABLED = true;
		bool MP_TRACK_CHECKPOINTS = true;
		CRGBA CHECKPOINT_CLR = {200, 16, 16, 255};

		bool LOGFILE_ENABLED = false;
		int FORCE_MODE = 0;

		std::string loadedFrom;

		explicit Config(const char *filename);
	};

	std::string GetModuleDirectory();
}
