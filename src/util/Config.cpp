#include "Config.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "mini/ini.h"

namespace util
{
	namespace
	{
		// Trả về BẢN SAO: mINI::get() trả giá trị theo kiểu by-value, nên không được giữ/trả tham chiếu tới nó.
		std::string Get(const mINI::INIStructure &ini, const char *section, const char *key,
						const std::string &fallback)
		{
			if (!ini.has(section))
				return fallback;
			const auto sec = ini.get(section);
			if (!sec.has(key))
				return fallback;
			std::string v = sec.get(key);
			return v.empty() ? fallback : v;
		}

		bool GetBool(const mINI::INIStructure &ini, const char *s, const char *k, bool def)
		{
			const std::string fb = def ? "1" : "0";
			return std::atoi(Get(ini, s, k, fb).c_str()) != 0;
		}

		float GetFloat(const mINI::INIStructure &ini, const char *s, const char *k, float def)
		{
			const std::string fb = std::to_string(def);
			return static_cast<float>(std::atof(Get(ini, s, k, fb).c_str()));
		}

		int GetInt(const mINI::INIStructure &ini, const char *s, const char *k, int def)
		{
			const std::string fb = std::to_string(def);
			return std::atoi(Get(ini, s, k, fb).c_str());
		}

		// "r, g, b, a" -> CRGBA. Sai định dạng thì giữ nguyên màu mặc định.
		void GetColor(const mINI::INIStructure &ini, const char *k, CRGBA &out)
		{
			const std::string v = Get(ini, "Custom Colors", k, "");
			if (v.empty())
				return;

			int r, g, b, a = 255;
			const int n = std::sscanf(v.c_str(), " %d , %d , %d , %d", &r, &g, &b, &a);
			if (n < 3)
				return;

			auto clamp = [](int x) { return static_cast<unsigned char>(x < 0 ? 0 : (x > 255 ? 255 : x)); };
			out.r = clamp(r);
			out.g = clamp(g);
			out.b = clamp(b);
			out.a = clamp(a);
		}
	} // namespace

	std::string GetModuleDirectory()
	{
		HMODULE self = nullptr;
		if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
								reinterpret_cast<LPCSTR>(&GetModuleDirectory), &self))
			return "";

		char path[MAX_PATH] = {};
		const DWORD len = GetModuleFileNameA(self, path, MAX_PATH);
		if (len == 0 || len >= MAX_PATH)
			return "";

		std::string dir(path, len);
		const size_t slash = dir.find_last_of("\\/");
		return slash == std::string::npos ? "" : dir.substr(0, slash + 1);
	}

	Config::Config(const char *filename)
	{
		// Ưu tiên file cạnh .asi (đúng cả khi chạy từ scripts/, modloader/ hay thư mục game).
		mINI::INIStructure ini;
		const std::string beside = GetModuleDirectory() + filename;
		if (mINI::INIFile(beside).read(ini))
		{
			loadedFrom = beside;
		}
		else
		{
			ini.clear();
			if (mINI::INIFile(filename).read(ini))
				loadedFrom = filename;
		}

		/* Navigation */
		RESPECT_LANE_DIRECTION = GetBool(ini, "Navigation", "respectTrafficLaneDirection", RESPECT_LANE_DIRECTION);
		GPS_LINE_WIDTH = GetFloat(ini, "Navigation", "lineWidth", GPS_LINE_WIDTH);
		ENABLE_BMX = GetBool(ini, "Navigation", "enableOnBicycles", ENABLE_BMX);
		ENABLE_WATER_GPS = GetBool(ini, "Navigation", "enableOnBoats", ENABLE_WATER_GPS);
		ENABLE_MOVING = GetBool(ini, "Navigation", "trackMovingTargets", ENABLE_MOVING);
		DISABLE_PROXIMITY = GetFloat(ini, "Navigation", "removeRadius", DISABLE_PROXIMITY);

		/* Extras */
		ENABLE_DISTANCE_TEXT = GetBool(ini, "Extras", "displayDistance", ENABLE_DISTANCE_TEXT);
		DISTANCE_UNITS = GetBool(ini, "Extras", "distanceUnits", DISTANCE_UNITS);

		/* Custom Colors */
		ENABLE_CUSTOM_CLRS = GetBool(ini, "Custom Colors", "enabled", ENABLE_CUSTOM_CLRS);
		GetColor(ini, "waypoint", GPS_LINE_CLR); // màu đường waypoint: luôn áp dụng
		if (ENABLE_CUSTOM_CLRS)
		{
			GetColor(ini, "red", CC_RED);
			GetColor(ini, "green", CC_GREEN);
			GetColor(ini, "blue", CC_BLUE);
			GetColor(ini, "white", CC_WHITE);
			GetColor(ini, "yellow", CC_YELLOW);
			GetColor(ini, "purple", CC_PURPLE);
			GetColor(ini, "cyan", CC_CYAN);
		}

		/* Performance */
		UPDATE_INTERVAL_MS = std::clamp(GetInt(ini, "Performance", "updateInterval", UPDATE_INTERVAL_MS), 0, 10000);
		MIN_MOVE_DISTANCE = std::max(0.0f, GetFloat(ini, "Performance", "minMoveDistance", MIN_MOVE_DISTANCE));

		/* Multiplayer */
		MP_ENABLED = GetBool(ini, "Multiplayer", "enabled", MP_ENABLED);
		MP_TRACK_CHECKPOINTS = GetBool(ini, "Multiplayer", "trackServerCheckpoints", MP_TRACK_CHECKPOINTS);
		CHECKPOINT_CLR = GPS_LINE_CLR; // mặc định cùng màu waypoint
		{
			// [Multiplayer] checkpointColor = r, g, b, a  (tuỳ chọn)
			const std::string v = Get(ini, "Multiplayer", "checkpointColor", "");
			int r, g, b, a = 255;
			if (!v.empty() && std::sscanf(v.c_str(), " %d , %d , %d , %d", &r, &g, &b, &a) >= 3)
			{
				auto clamp = [](int x) { return static_cast<unsigned char>(x < 0 ? 0 : (x > 255 ? 255 : x)); };
				CHECKPOINT_CLR = CRGBA(clamp(r), clamp(g), clamp(b), clamp(a));
			}
		}

		/* Misc */
		LOGFILE_ENABLED = GetBool(ini, "Misc", "enableLog", LOGFILE_ENABLED);
		FORCE_MODE = GetInt(ini, "Misc", "forceMode", FORCE_MODE);
		// Không tìm thấy file cấu hình: bật log để còn biết lý do khi mod "không chạy".
		if (loadedFrom.empty())
			LOGFILE_ENABLED = true;

		// Không ghi ngược file: mINI sẽ xoá hết comment trong file cấu hình của người dùng.
	}
} // namespace util
