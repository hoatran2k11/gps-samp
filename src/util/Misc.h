#pragma once
#include <cstdio>
#include <string>

namespace util
{
	constexpr float mtoyard(float m)
	{
		return m * 1.094f;
	}

	inline std::string makeDist(float dist, const bool units)
	{
		char buf[64];
		if (!units)
		{
			if (dist > 999)
				std::snprintf(buf, sizeof(buf), "%.1f KM", dist / 1000.0f);
			else
				std::snprintf(buf, sizeof(buf), "%.0f m", dist);
		}
		else
		{
			dist = mtoyard(dist);
			if (dist > 599)
				std::snprintf(buf, sizeof(buf), "%.1f Mi", dist / 1760.0f);
			else
				std::snprintf(buf, sizeof(buf), "%.0f yrds", dist);
		}
		return buf;
	}
}
