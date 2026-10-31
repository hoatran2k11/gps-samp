#pragma once
#include <windows.h>

namespace util
{
	// Chế độ chơi hiện tại. SAMP (và open.mp) đều nạp samp.dll vào gta_sa.exe,
	// còn bản đơn (single player) thì không có module này.
	enum class GameMode
	{
		SinglePlayer = 0,
		SAMP = 1
	};

	// 0 = tự nhận diện, 1 = ép single player, 2 = ép SAMP (đọc từ [Misc] forceMode).
	inline GameMode DetectGameMode(int forceMode)
	{
		if (forceMode == 1)
			return GameMode::SinglePlayer;
		if (forceMode == 2)
			return GameMode::SAMP;

		// samp.dll có thể được nạp SAU khi file .asi này khởi tạo, nên phải kiểm tra
		// lại ở runtime. Đã thấy một lần thì giữ nguyên (không thể "gỡ" samp.dll giữa chừng).
		static bool sampSeen = false;
		if (!sampSeen)
			sampSeen = GetModuleHandleA("samp.dll") != nullptr;

		return sampSeen ? GameMode::SAMP : GameMode::SinglePlayer;
	}
} // namespace util
