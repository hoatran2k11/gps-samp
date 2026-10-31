#pragma once

/*
	Plugin-SDK (Grand Theft Auto) source file
	Authors: GTA Community. See more here
	https://github.com/DK22Pac/plugin-sdk
	Do not delete this comment block. Respect others' work!
*/

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <future>
#include <limits>
#include <mutex>
#include <vector>

#include "CFont.h"
#include "CGeneral.h"
#include "CHudColours.h"
#include "CMenuManager.h"
#include "CObject.h"
#include "CPed.h"
#include "CPickup.h"
#include "CPickups.h"
#include "CPools.h"
#include "CRadar.h"
#include "CTheScripts.h"
#include "CVehicle.h"
#include "CWorld.h"
#include "Color.h"
#include "RenderWare.h"
#include "common.h"
#include "d3d9.h"
#include "plugin.h"

#include "util/Bools.h"
#include "util/Config.h"
#include "util/GameMode.h"
#include "util/Logger.h"
#include "util/Misc.h"
#include "util/Render.h"

#define MAX_NODE_POINTS 5000

/*
	GPS Redux — dùng được cho cả single player lẫn SAMP (GTA SA 1.0 US).

	Single player : dẫn đường tới waypoint (bản đồ pause menu) và blip nhiệm vụ.
	SAMP          : dẫn đường tới waypoint; tuỳ chọn thêm blip toạ độ do server tạo
	                ([Multiplayer] trackServerBlips = 1). SAMP không chạy script nhiệm vụ
	                của game nên không có "mission blip" như single player.
*/
class GPS
{
  private:
	// Một tuyến đường tính bất đồng bộ. `nodes`/`distance` chỉ được đụng khi giữ pathMutex.
	struct Route
	{
		std::future<void> pending;
		std::vector<CNodeAddress> nodes;
		float distance = 0.0f;
		int epoch = 0; // tăng mỗi khi đổi đích, để bỏ kết quả của lượt tính đã cũ
	};

	// Ảnh chụp trạng thái người chơi, lấy trên thread game để thread tính đường
	// không phải chạm vào con trỏ CPed/CVehicle (có thể bị giải phóng bất kỳ lúc nào).
	struct PathQuery
	{
		CVector from;
		CVector to;
		bool respectLanes;
		bool boat;
	};

	void Run();
	void Update();
	void DrawHud();
	void DrawRadarOverlay();

	void updateTargetRoute(const CVector &playerPos);
	void updateMissionRoute(const CVector &playerPos);
	tRadarTrace *findTargetTrace() const;
	tRadarTrace *findMissionTrace(const CVector &playerPos, CVector &destOut) const;
	bool resolveTraceDestination(const tRadarTrace *trace, CVector &out) const;
	void clearRoute(Route &route);

	PathQuery makeQuery(const CVector &dest) const;
	void requestPath(Route &route, const PathQuery &query);
	void renderPath(Route &route, const CRGBA &color);
	void drawDistanceText(const CVector2D &radarAnchor, float yOffset, const CRGBA &color, float distance);

	util::Config cfg = util::Config("SA.GPS.CONF.ini");
	util::Logger logger = util::Logger(false);
	util::GameMode mode = util::GameMode::SinglePlayer;
	bool active = false; // false nếu không phải GTA SA 1.0 US (không patch/không vẽ gì cả)

	CPed *player = nullptr;

	bool renderTargetRoute = false;
	bool renderMissionRoute = false;
	CVector targetPos;
	CVector missionPos;
	CRGBA missionColor = {180, 24, 24, 255};
	int lastTargetBlip = 0;
	const tRadarTrace *lastMissionTrace = nullptr;

	Route targetRoute;
	Route missionRoute;
	std::mutex pathMutex;

	// Bộ đệm game giữ con trỏ tới (patch bằng địa chỉ cố định) nên phải sống suốt phiên chơi.
	std::array<char, 1024> pathNodesToStream{};
	std::array<int, 50000> pathNodes{};

	// Bộ đệm vẽ (chỉ dùng trên thread game).
	std::vector<CVector2D> tmpNodePoints = std::vector<CVector2D>(MAX_NODE_POINTS);
	std::vector<RwIm2DVertex> lineVerts = std::vector<RwIm2DVertex>(MAX_NODE_POINTS * 4);

  public:
	inline GPS()
	{
		this->Run();
	}
} GPSLineRedux;
