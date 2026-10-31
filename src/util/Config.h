#pragma once
#include <string>

#include "CRGBA.h"

namespace util
{
	struct Config
	{
		// [Navigation]
		bool RESPECT_LANE_DIRECTION = true;
		float GPS_LINE_WIDTH = 2.5f;
		bool ENABLE_BMX = false;
		bool ENABLE_WATER_GPS = true;
		bool ENABLE_MOVING = false;
		float DISABLE_PROXIMITY = 40.0f;

		// [Extras]
		bool ENABLE_DISTANCE_TEXT = false;
		bool DISTANCE_UNITS = false;

		// [Custom Colors]
		bool ENABLE_CUSTOM_CLRS = false;
		CRGBA CC_RED = {255, 0, 0, 255};
		CRGBA CC_GREEN = {0, 255, 0, 255};
		CRGBA CC_BLUE = {0, 0, 255, 255};
		CRGBA CC_WHITE = {255, 255, 255, 255};
		CRGBA CC_PURPLE = {255, 0, 255, 255};
		CRGBA CC_YELLOW = {255, 255, 0, 255};
		CRGBA CC_CYAN = {0, 255, 255, 255};
		CRGBA GPS_LINE_CLR = {180, 24, 24, 255};

		// [Multiplayer]
		bool MP_ENABLED = true;			 // Bật GPS khi chơi SAMP.
		bool MP_TRACK_SERVER_BLIPS = false; // Dẫn đường tới blip toạ độ do server tạo.

		// [Misc]
		bool LOGFILE_ENABLED = false;
		int FORCE_MODE = 0; // 0 = auto, 1 = single player, 2 = SAMP

		// `filename` được tìm cạnh file .asi trước, rồi mới tới thư mục hiện hành.
		explicit Config(const char *filename);
	};

	// Thư mục chứa file .asi (kết thúc bằng '\\'), hoặc chuỗi rỗng nếu không lấy được.
	std::string GetModuleDirectory();
} // namespace util
