#pragma once
#include <windows.h>

namespace util
{
	enum class GameMode
	{
		SinglePlayer = 0,
		SAMP = 1
	};

	inline GameMode DetectGameMode(int forceMode)
	{
		if (forceMode == 1)
			return GameMode::SinglePlayer;
		if (forceMode == 2)
			return GameMode::SAMP;

		// samp.dll may be loaded AFTER this .asi initializes, so it must be re-checked at runtime.
		// Once seen it stays set (samp.dll cannot be unloaded midway).
		static bool sampSeen = false;
		if (!sampSeen)
			sampSeen = GetModuleHandleA("samp.dll") != nullptr;

		return sampSeen ? GameMode::SAMP : GameMode::SinglePlayer;
	}
}
