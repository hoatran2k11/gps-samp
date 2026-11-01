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
#include <condition_variable>
#include <limits>
#include <memory>
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

// GPS Redux: works for both single player and SAMP (GTA SA 1.0 US).
// Single player: routes to the waypoint and to mission blips.
// SAMP: routes to the waypoint, plus optional server-created coordinate blips
// ([Multiplayer] trackServerBlips = 1). SAMP runs no mission scripts, so there
// are no mission blips there.
class GPS
{
  private:
	// An asynchronously computed route. `points`/`distance`/`epoch` may only be touched while holding pathMutex.
	// `points` is an immutable vector published as a whole (the pathfinding thread builds a new vector and swaps
	// the pointer), so the game thread only needs the lock while copying the pointer, not while drawing.
	struct Route
	{
		std::shared_ptr<const std::vector<CVector2D>> points;
		float distance = 0.0f;
		int epoch = 0;

		bool requested = false;
		ULONGLONG lastRequestMs = 0;
		CVector lastFrom, lastTo;

		bool busy = false;
	};

	// Snapshot of the player state, taken on the game thread, so the pathfinding thread never
	// dereferences CPed/CVehicle pointers (they can be freed at any time).
	struct PathQuery
	{
		CVector from;
		CVector to;
		bool respectLanes;
		bool boat;
	};

	void Run();
	void Update();

	static DWORD WINAPI WorkerThread(LPVOID param);
	void workerLoop();

	static DWORD WINAPI InstallThread(LPVOID param);
	void installEventHooks();
	static bool waitForSampHook(int timeoutMs, util::Logger &log);
	void DrawHud();
	void DrawRadarOverlay();

	void updateTargetRoute(const CVector &playerPos);
	void updateMissionRoute(const CVector &playerPos);
	tRadarTrace *findTargetTrace() const;
	tRadarTrace *findMissionTrace(const CVector &playerPos, CVector &destOut) const;

	struct Target
	{
		CVector pos;
		CRGBA color;
		const void *id = nullptr;
	};
	bool findServerTarget(const CVector &playerPos, Target &out);
	bool resolveTraceDestination(const tRadarTrace *trace, CVector &out) const;
	void clearRoute(Route &route);

	PathQuery makeQuery(const CVector &dest) const;
	void requestPath(Route &route, PathQuery query, bool needGroundZ = false);
	void renderPath(Route &route, const CRGBA &color);
	void drawDistanceText(const CVector2D &radarAnchor, float yOffset, const CRGBA &color, float distance);

	util::Config cfg = util::Config("SA.GPS.CONF.ini");
	util::Logger logger = util::Logger(false);
	util::GameMode mode = util::GameMode::SinglePlayer;
	bool active = false;

	CPed *player = nullptr;

	bool renderTargetRoute = false;
	bool renderMissionRoute = false;
	CVector targetPos;
	CVector missionPos;
	CRGBA missionColor = {180, 24, 24, 255};
	int lastTargetBlip = 0;
	const void *lastMissionTrace = nullptr;

	bool updatedSinceDraw = false;
	bool diagFallbackLogged = false;
	bool diagFirstFrame = true;
	int diagNavEnabled = -1;
	int diagTargetHasNodes = -1;
	int diagMissionHasNodes = -1;
	int diagMode = -1;
	int diagCheckpoints = -1;
	int diagCoordBlips = -1;
	const void *diagServerTarget = nullptr;

	Route targetRoute;
	Route missionRoute;
	std::mutex pathMutex;

	struct Job
	{
		Route *route = nullptr;
		PathQuery query{};
		int epoch = 0;
	};
	Job jobs[2];
	std::mutex jobMutex;
	std::condition_variable jobCv;
	bool workerStop = false;
	std::array<CNodeAddress, MAX_NODE_POINTS> workerNodes{};

	// The game holds pointers into this buffer (patched at fixed addresses), so it must live for the whole session.
	std::array<char, 1024> pathNodesToStream{};
	std::array<int, 50000> pathNodes{};

	std::vector<CVector2D> tmpNodePoints = std::vector<CVector2D>(MAX_NODE_POINTS);
	std::vector<RwIm2DVertex> lineVerts = std::vector<RwIm2DVertex>(MAX_NODE_POINTS * 4);

  public:
	inline GPS()
	{
		this->Run();
	}
	~GPS();
} GPSLineRedux;
