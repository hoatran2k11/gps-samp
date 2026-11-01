#pragma once
#include <string>

#include "CRGBA.h"

namespace util
{
	struct Config
	{
		// [Navigation]
		bool RESPECT_LANE_DIRECTION = true;
		float GPS_LINE_WIDTH = 3.0f;
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
		CRGBA GPS_LINE_CLR = {200, 16, 16, 255}; // đỏ đậm, bão hoà

		// [Performance]
		int UPDATE_INTERVAL_MS = 500;	// Tối thiểu bao lâu mới tính lại đường (ms).
		float MIN_MOVE_DISTANCE = 10.0f; // Phải di chuyển ít nhất chừng này (m) mới tính lại.

		// [Multiplayer]
		bool MP_ENABLED = true;			 // Bật GPS khi chơi SAMP.
		bool MP_TRACK_CHECKPOINTS = true; // Dẫn đường tới checkpoint/blip toạ độ do server tạo.
		CRGBA CHECKPOINT_CLR = {200, 16, 16, 255}; // Màu đường dẫn tới checkpoint server.

		// [Misc]
		bool LOGFILE_ENABLED = false;
		int FORCE_MODE = 0; // 0 = auto, 1 = single player, 2 = SAMP

		// File ini thực sự đã đọc được (rỗng nếu không tìm thấy -> dùng mặc định).
		std::string loadedFrom;

		// `filename` được tìm cạnh file .asi trước, rồi mới tới thư mục hiện hành.
		explicit Config(const char *filename);
	};

	// Thư mục chứa file .asi (kết thúc bằng '\\'), hoặc chuỗi rỗng nếu không lấy được.
	std::string GetModuleDirectory();
} // namespace util
