#pragma once
#include "CPed.h"
#include "CTheScripts.h"
#include "CVehicle.h"
#include "Config.h"

namespace util
{
	// Xe có bị loại khỏi GPS không (xe đạp BMX khi người dùng không bật).
	inline bool CheckBMX(const Config &cfg, const CPed *player)
	{
		if (cfg.ENABLE_BMX)
			return false;

		return player->m_pVehicle->m_nVehicleSubClass == VEHICLE_BMX;
	}

	// GPS chỉ hiện khi người chơi đang ngồi trong xe (không phải máy bay/trực thăng).
	// Dùng chung cho single player và SAMP.
	inline bool NavEnabled(const Config &cfg, const CPed *player)
	{
		return (player && player->m_pVehicle && player->m_nPedFlags.bInVehicle &&
				player->m_pVehicle->m_nVehicleSubClass != VEHICLE_PLANE &&
				player->m_pVehicle->m_nVehicleSubClass != VEHICLE_HELI && !CTheScripts::bMiniGameInProgress &&
				!CheckBMX(cfg, player));
	}
} // namespace util
