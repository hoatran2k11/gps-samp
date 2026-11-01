#include "GPS.h"

#include <cstdint>

#include "CCheckpoints.h"

namespace
{
	constexpr int RADAR_TRACE_COUNT = 175; // CRadar::ms_RadarTrace[175]
}

void GPS::Run()
{
	// Toàn bộ địa chỉ patch bên dưới là của GTA SA 1.0 US — đúng cả cho bản
	// chạy SAMP (SAMP 0.3.7 bắt buộc dùng exe 1.0 US). Phiên bản khác thì không đụng vào gì.
	logger = util::Logger(cfg.LOGFILE_ENABLED);
	logger.Log("GPS Redux (SAMP + single player) starting");
	logger.LogF("config file: %s", cfg.loadedFrom.empty() ? "NOT FOUND - using defaults" : cfg.loadedFrom.c_str());
	logger.LogF("module dir: %s", util::GetModuleDirectory().c_str());

	const unsigned int ver = plugin::GetGameVersion();
	logger.LogF("game version id = %u (1001/1002 = GTA SA 1.0 US)", ver);
	if (ver != GAME_10US_COMPACT && ver != GAME_10US_HOODLUM)
	{
		logger.Log("unsupported game version - GPS DISABLED");
		return;
	}

	active = true;

	std::ranges::fill(pathNodesToStream, 1);
	std::ranges::fill(pathNodes, -1);

	plugin::patch::SetPointer(0x44DE3C, pathNodesToStream.data());
	plugin::patch::SetPointer(0x450D03, pathNodesToStream.data());
	plugin::patch::SetPointer(0x451782, pathNodes.data());
	plugin::patch::SetPointer(0x451904, pathNodes.data());
	plugin::patch::SetPointer(0x451AC3, pathNodes.data());
	plugin::patch::SetPointer(0x451B33, pathNodes.data());
	plugin::patch::SetUInt(0x4518F8, 50000);
	plugin::patch::SetUInt(0x4519B0, 49950);

	// KHÔNG đăng ký sự kiện ngay ở đây. SAMP (samp.dll) tự hook lại đúng lệnh call mà
	// gameProcessEvent dùng (0x53E981); nếu ta hook trước thì hook của ta bị ghi đè và
	// không bao giờ chạy -> không có đường GPS. Nên cài hook trên thread riêng, sau khi
	// samp.dll đã hook xong, để hook của ta nối chồng lên (kỹ thuật lấy từ janglapuk/SAMP-GPS).
	if (HANDLE worker = CreateThread(nullptr, 0, &GPS::WorkerThread, this, 0, nullptr))
		CloseHandle(worker);
	else
		logger.Log("CreateThread (path worker) failed - GPS DISABLED");

	HANDLE thread = CreateThread(nullptr, 0, &GPS::InstallThread, this, 0, nullptr);
	if (thread)
		CloseHandle(thread);
	else
		logger.Log("CreateThread failed - GPS DISABLED");
}

namespace
{
	constexpr uintptr_t GAME_PROCESS_CALL = 0x53E981; // call CGame::Process trong vòng lặp chính
}

// Chờ tới khi lệnh call tại 0x53E981 trỏ vào trong samp.dll (samp.dll đã hook xong).
bool GPS::waitForSampHook(int timeoutMs, util::Logger &log)
{
	for (int waited = 0; waited < timeoutMs; waited += 100)
	{
		HMODULE samp = GetModuleHandleA("samp.dll");
		const auto *callOp = reinterpret_cast<const unsigned char *>(GAME_PROCESS_CALL);
		if (samp && callOp[0] == 0xE8)
		{
			const auto base = reinterpret_cast<uintptr_t>(samp);
			const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(samp);
			const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
			const uintptr_t end = base + nt->OptionalHeader.SizeOfImage;

			const uintptr_t target = GAME_PROCESS_CALL + 5 + *reinterpret_cast<const int *>(GAME_PROCESS_CALL + 1);
			if (target >= base && target < end)
				return true;
		}
		Sleep(100);
	}
	log.Log("timed out waiting for samp.dll hook");
	return false;
}

DWORD WINAPI GPS::InstallThread(LPVOID param)
{
	static_cast<GPS *>(param)->installEventHooks();
	return 0;
}

void GPS::installEventHooks()
{
	// 1. Chờ exe game giải mã xong (lệnh call hợp lệ).
	for (int i = 0; i < 1200 && *reinterpret_cast<const unsigned char *>(GAME_PROCESS_CALL) != 0xE8; ++i)
		Sleep(100);

	// 2. SAMP hay single player? samp.dll thường đã được nạp từ trước; đợi tối đa 3s cho chắc.
	bool samp = false;
	if (cfg.FORCE_MODE == 2)
		samp = true;
	else if (cfg.FORCE_MODE == 0)
	{
		for (int i = 0; i < 30 && !samp; ++i)
		{
			samp = GetModuleHandleA("samp.dll") != nullptr;
			if (!samp)
				Sleep(100);
		}
	}
	logger.LogF("hook installer: %s", samp ? "SAMP detected, waiting for samp.dll hook" : "single player");

	if (samp)
	{
		// 3. Chờ samp.dll hook xong, rồi chờ người chơi vào game (tránh xung đột lúc đang tải).
		const bool hooked = waitForSampHook(60000, logger);
		logger.LogF("samp.dll hook %s", hooked ? "detected" : "NOT detected (installing anyway)");
		for (int i = 0; i < 120 && !FindPlayerPed(0); ++i)
			Sleep(500);
	}

	plugin::Events::gameProcessEvent += [this]() { this->Update(); };
	plugin::Events::drawRadarOverlayEvent += [this]() { this->DrawRadarOverlay(); };
	plugin::Events::drawRadarEvent += [this]() { this->DrawHud(); };
	logger.Log("event hooks installed");
}

// ---------------------------------------------------------------------------
// Tính đường (thread nền)
// ---------------------------------------------------------------------------

GPS::PathQuery GPS::makeQuery(const CVector &dest) const
{
	// Gọi trên thread game, khi NavEnabled() đã đảm bảo player->m_pVehicle != nullptr.
	const auto subClass = player->m_pVehicle->m_nVehicleSubClass;
	const bool boat = subClass == VEHICLE_BOAT;

	PathQuery q;
	q.from = player->GetPosition();
	q.to = dest;
	q.respectLanes = !boat && subClass != VEHICLE_BMX && cfg.RESPECT_LANE_DIRECTION;
	q.boat = boat && cfg.ENABLE_WATER_GPS;
	return q;
}

void GPS::requestPath(Route &route, PathQuery query, bool needGroundZ)
{
	// Điều tiết: không tính lại mỗi khung hình. Chỉ tính khi (a) chưa từng tính cho đích này,
	// hoặc (b) đã qua updateInterval VÀ người chơi/đích đã dời đủ xa, hoặc (c) chưa có kết quả.
	const ULONGLONG now = GetTickCount64();
	bool due = !route.requested;
	if (!due)
	{
		const ULONGLONG elapsed = now - route.lastRequestMs;
		const float moved = DistanceBetweenPoints(query.from, route.lastFrom);
		const float destMoved = DistanceBetweenPoints(query.to, route.lastTo);
		bool noResult;
		{
			std::lock_guard<std::mutex> lock(pathMutex);
			noResult = !route.points || route.points->size() < 2;
		}
		// Đích không tới được (không có kết quả) thì thử lại thưa hơn, tránh tìm đường thất bại liên tục.
		const ULONGLONG retryMs = std::max<ULONGLONG>(2000, cfg.UPDATE_INTERVAL_MS);
		due = (elapsed >= static_cast<ULONGLONG>(cfg.UPDATE_INTERVAL_MS) &&
			   (moved >= cfg.MIN_MOVE_DISTANCE || destMoved >= cfg.MIN_MOVE_DISTANCE)) ||
			  (noResult && elapsed >= retryMs);
	}
	if (!due)
		return;

	// Dò độ cao mặt đất (va chạm) khá tốn, chỉ làm khi thật sự cần tính đường, không phải mỗi khung hình.
	if (needGroundZ)
		query.to.z = CWorld::FindGroundZForCoord(query.to.x, query.to.y);

	int epoch;
	{
		std::lock_guard<std::mutex> lock(pathMutex);
		epoch = route.epoch;
	}

	{
		std::lock_guard<std::mutex> lock(jobMutex);
		if (route.busy)
			return; // lượt trước chưa xong; khung hình sau thử lại
		route.busy = true;
		Job &job = jobs[&route == &targetRoute ? 0 : 1];
		job.route = &route;
		job.query = query;
		job.epoch = epoch;
	}

	route.requested = true;
	route.lastRequestMs = now;
	route.lastFrom = query.from;
	route.lastTo = query.to;
	jobCv.notify_one();
}

void GPS::clearRoute(Route &route)
{
	{
		std::lock_guard<std::mutex> lock(pathMutex);
		route.points.reset();
		route.distance = 0.0f;
		route.epoch++;
	}
	route.requested = false; // đích mới: tính ngay, không chờ updateInterval
}

DWORD WINAPI GPS::WorkerThread(LPVOID param)
{
	static_cast<GPS *>(param)->workerLoop();
	return 0;
}

void GPS::workerLoop()
{
	// Tìm đường là việc nặng; hạ ưu tiên để không giành CPU với thread game.
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

	for (;;)
	{
		Job job;
		{
			std::unique_lock<std::mutex> lock(jobMutex);
			jobCv.wait(lock, [this] { return workerStop || jobs[0].route || jobs[1].route; });
			if (workerStop)
				return;

			Job &slot = jobs[jobs[0].route ? 0 : 1];
			job = slot;
			slot.route = nullptr;
		}

		short count = 0;
		float distance = 0.0f;
		ThePaths.DoPathSearch(0, job.query.from, CNodeAddress(), job.query.to, workerNodes.data(), &count,
							  MAX_NODE_POINTS, &distance, 999999.0f, NULL, 999999.0f, job.query.respectLanes,
							  CNodeAddress(), false, job.query.boat);
		count = std::clamp<short>(count, 0, MAX_NODE_POINTS);

		// Đổi node -> toạ độ XY một lần ở đây; thread game không phải tra bảng node mỗi khung hình nữa.
		auto points = std::make_shared<std::vector<CVector2D>>();
		points->reserve(count);
		for (short i = 0; i < count; ++i)
		{
			if (CPathNode *node = ThePaths.GetPathNode(workerNodes[i]))
			{
				const CVector p = node->GetNodeCoors();
				points->emplace_back(p.x, p.y);
			}
		}

		{
			std::lock_guard<std::mutex> lock(pathMutex);
			if (job.route->epoch == job.epoch) // đích đã đổi trong lúc tính thì bỏ kết quả cũ
			{
				job.route->points = std::move(points);
				job.route->distance = distance;
			}
		}
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			job.route->busy = false;
		}
	}
}

GPS::~GPS()
{
	{
		std::lock_guard<std::mutex> lock(jobMutex);
		workerStop = true;
	}
	jobCv.notify_all();
	// Không chờ thread kết thúc: destructor chạy dưới loader lock, chờ ở đây dễ gây deadlock.
}

// ---------------------------------------------------------------------------
// Logic mỗi khung hình (thread game)
// ---------------------------------------------------------------------------

tRadarTrace *GPS::findTargetTrace() const
{
	const int idx = FrontEndMenuManager.m_nTargetBlipIndex;
	if (!idx)
		return nullptr;

	const int slot = LOWORD(idx);
	if (slot >= RADAR_TRACE_COUNT)
		return nullptr;

	tRadarTrace *trace = &CRadar::ms_RadarTrace[slot];
	if (trace->m_nCounter != HIWORD(idx) || !trace->m_nBlipDisplay)
		return nullptr;

	return trace;
}

bool GPS::resolveTraceDestination(const tRadarTrace *trace, CVector &out) const
{
	switch (trace->m_nBlipType)
	{
	case BLIP_CAR:
	{
		CVehicle *veh = CPools::GetVehicle(trace->m_nEntityHandle);
		if (!veh)
			return false;
		out = veh->GetPosition();
		return true;
	}
	case BLIP_CHAR:
	{
		CPed *ped = CPools::GetPed(trace->m_nEntityHandle);
		if (!ped)
			return false;
		out = ped->GetPosition();
		return true;
	}
	case BLIP_OBJECT:
	{
		CObject *obj = CPools::GetObject(trace->m_nEntityHandle);
		if (!obj)
			return false;
		out = obj->GetPosition();
		return true;
	}
	default:
		out = trace->m_vecPos;
		return true;
	}
}

tRadarTrace *GPS::findMissionTrace(const CVector &playerPos, CVector &destOut) const
{
	const bool singlePlayer = mode == util::GameMode::SinglePlayer;

	if (singlePlayer)
	{
		// Single player: chỉ dẫn đường khi đang làm nhiệm vụ.
		if (!CTheScripts::IsPlayerOnAMission())
			return nullptr;
	}
	else if (!cfg.MP_TRACK_CHECKPOINTS)
	{
		// SAMP: không có nhiệm vụ của game; chỉ theo blip server tạo nếu được bật.
		return nullptr;
	}

	const int targetSlot = FrontEndMenuManager.m_nTargetBlipIndex ? LOWORD(FrontEndMenuManager.m_nTargetBlipIndex) : -1;

	float bestDist = std::numeric_limits<float>::infinity();
	tRadarTrace *best = nullptr;

	for (int i = 0; i < RADAR_TRACE_COUNT; ++i)
	{
		tRadarTrace *trace = &CRadar::ms_RadarTrace[i];

		// Bỏ blip không dùng, blip có icon riêng (cửa hàng, nhà an toàn...), blip ẩn và chính waypoint.
		if (!trace->m_bInUse || trace->m_nRadarSprite != 0 || trace->m_nBlipDisplay < 2 || i == targetSlot)
			continue;

		switch (trace->m_nBlipType)
		{
		case BLIP_CAR:
		case BLIP_CHAR:
			if (!cfg.ENABLE_MOVING || !singlePlayer) // SAMP: blip người chơi/xe không phải đích đến
				continue;
			break;
		case BLIP_OBJECT:
		case BLIP_CONTACTPOINT:
			if (!singlePlayer)
				continue;
			break;
		case BLIP_COORD:
			break;
		default: // NONE, SPOTLIGHT, PICKUP (không hỗ trợ — xem README), AIRSTRIP
			continue;
		}

		CVector dest;
		if (!resolveTraceDestination(trace, dest))
			continue;

		const float d = DistanceBetweenPoints(playerPos, dest);
		if (d <= cfg.DISABLE_PROXIMITY) // đã tới nơi
			continue;

		if (d < bestDist)
		{
			bestDist = d;
			best = trace;
			destOut = dest;
		}
	}

	return best;
}

void GPS::updateTargetRoute(const CVector &playerPos)
{
	tRadarTrace *trace = findTargetTrace();
	const int idx = FrontEndMenuManager.m_nTargetBlipIndex;

	if (!trace)
	{
		if (lastTargetBlip)
		{
			clearRoute(targetRoute);
			logger.Log("waypoint removed");
		}
		lastTargetBlip = 0;
		diagTargetHasNodes = -1;
		return;
	}

	// Tới nơi thì tự gỡ waypoint.
	if (DistanceBetweenPoints(playerPos, trace->m_vecPos) <= cfg.DISABLE_PROXIMITY)
	{
		CRadar::ClearBlip(idx);
		FrontEndMenuManager.m_nTargetBlipIndex = 0;
		clearRoute(targetRoute);
		lastTargetBlip = 0;
		return;
	}

	if (idx != lastTargetBlip)
	{
		clearRoute(targetRoute); // waypoint mới: bỏ đường cũ
		lastTargetBlip = idx;
		diagTargetHasNodes = -1;
		logger.LogF("waypoint blip detected (index %d)", idx);
	}

	targetPos = trace->m_vecPos;
	// Waypoint đặt trên bản đồ không có độ cao; tìm node đường gần đích cần z đúng
	// (cách làm của SAMP-GPS). Chỉ SAMP, để single player giữ nguyên như bản gốc.
	requestPath(targetRoute, makeQuery(targetPos), mode == util::GameMode::SAMP);
	renderTargetRoute = true;

	size_t nodes;
	{
		std::lock_guard<std::mutex> lock(pathMutex);
		nodes = targetRoute.points ? targetRoute.points->size() : 0;
	}
	if (diagTargetHasNodes != (nodes > 0))
	{
		diagTargetHasNodes = nodes > 0;
		logger.LogF("waypoint at (%.0f, %.0f) - route %s (%u nodes)", targetPos.x, targetPos.y,
					nodes ? "FOUND" : "not available yet", static_cast<unsigned>(nodes));
	}
}

bool GPS::findServerTarget(const CVector &playerPos, Target &out)
{
	if (!cfg.MP_TRACK_CHECKPOINTS)
		return false;

	float bestDist = std::numeric_limits<float>::infinity();
	bool found = false;
	int usedCheckpoints = 0, coordBlips = 0;

	// 1. Checkpoint 3D của game (cột sáng/vòng tròn mà server đặt).
	if (CCheckpoints::m_aCheckPtArray)
	{
		for (unsigned int i = 0; i < 32; ++i)
		{
			const CCheckpoint *cp = &CCheckpoints::m_aCheckPtArray[i];
			if (!cp->m_bIsUsed)
				continue;
			usedCheckpoints++;

			const float d = DistanceBetweenPoints(playerPos, cp->m_vecPosition);
			if (d <= cfg.DISABLE_PROXIMITY || d >= bestDist)
				continue;

			bestDist = d;
			found = true;
			out.pos = cp->m_vecPosition;
			out.color = cfg.CHECKPOINT_CLR;
			out.id = cp;
		}
	}

	// 2. Blip toạ độ trên radar (checkpoint thường kèm một chấm trên radar).
	const int targetSlot = FrontEndMenuManager.m_nTargetBlipIndex ? LOWORD(FrontEndMenuManager.m_nTargetBlipIndex) : -1;
	for (int i = 0; i < RADAR_TRACE_COUNT; ++i)
	{
		const tRadarTrace *trace = &CRadar::ms_RadarTrace[i];
		if (!trace->m_bInUse || trace->m_nRadarSprite != 0 || trace->m_nBlipDisplay < 2 ||
			trace->m_nBlipType != BLIP_COORD || i == targetSlot)
			continue;
		coordBlips++;

		const float d = DistanceBetweenPoints(playerPos, trace->m_vecPos);
		if (d <= cfg.DISABLE_PROXIMITY || d >= bestDist)
			continue;

		bestDist = d;
		found = true;
		out.pos = trace->m_vecPos;
		out.color = cfg.CHECKPOINT_CLR;
		out.id = trace;
	}

	if (diagCheckpoints != usedCheckpoints || diagCoordBlips != coordBlips)
	{
		diagCheckpoints = usedCheckpoints;
		diagCoordBlips = coordBlips;
		logger.LogF("server targets: %d game checkpoint(s), %d coordinate blip(s)", usedCheckpoints, coordBlips);
	}
	if (found && diagServerTarget != out.id)
	{
		diagServerTarget = out.id;
		logger.LogF("tracking server target at (%.0f, %.0f, %.0f), %.0f m away", out.pos.x, out.pos.y, out.pos.z,
					bestDist);
	}
	return found;
}

void GPS::updateMissionRoute(const CVector &playerPos)
{
	Target target;
	bool have = false;

	if (mode == util::GameMode::SinglePlayer)
	{
		CVector dest;
		if (const tRadarTrace *trace = findMissionTrace(playerPos, dest))
		{
			have = true;
			target.pos = dest;
			target.color = util::SetupColor(trace->m_nColour, trace->m_bFriendly, cfg);
			target.id = trace;
		}
	}
	else
	{
		have = findServerTarget(playerPos, target);
	}

	if (!have)
	{
		if (lastMissionTrace)
			clearRoute(missionRoute);
		lastMissionTrace = nullptr;
		diagServerTarget = nullptr;
		return;
	}

	if (target.id != lastMissionTrace)
	{
		clearRoute(missionRoute); // đổi mục tiêu: bỏ đường cũ
		lastMissionTrace = target.id;
	}

	missionPos = target.pos;
	missionColor = target.color;
	// Blip không có độ cao (z ~ 0) -> lấy độ cao mặt đất để tìm node đường gần đích.
	requestPath(missionRoute, makeQuery(target.pos),
				mode == util::GameMode::SAMP && std::fabs(target.pos.z) < 0.01f);
	renderMissionRoute = true;
}

void GPS::Update()
{
	updatedSinceDraw = true;
	mode = util::DetectGameMode(cfg.FORCE_MODE);
	player = FindPlayerPed(0);

	renderTargetRoute = false;
	renderMissionRoute = false;

	if (diagFirstFrame)
	{
		diagFirstFrame = false;
		logger.Log("first game frame reached (events are firing)");
	}
	if (diagMode != static_cast<int>(mode))
	{
		diagMode = static_cast<int>(mode);
		logger.LogF("mode = %s", mode == util::GameMode::SAMP ? "SAMP" : "single player");
	}

	if (mode == util::GameMode::SAMP && !cfg.MP_ENABLED)
		return;

	const int navOn = util::NavEnabled(cfg, player) ? 1 : 0;
	if (diagNavEnabled != navOn)
	{
		diagNavEnabled = navOn;
		logger.LogF("navigation %s (player=%s, in vehicle=%s)", navOn ? "ENABLED" : "disabled", player ? "yes" : "no",
					(player && player->m_pVehicle) ? "yes" : "no");
	}
	if (!navOn)
		return;

	const CVector playerPos = player->GetPosition();
	updateTargetRoute(playerPos);
	updateMissionRoute(playerPos);
}

// ---------------------------------------------------------------------------
// Vẽ
// ---------------------------------------------------------------------------

void GPS::DrawRadarOverlay()
{
	// Phòng khi gameProcessEvent bị hook khác (SAMP...) chiếm mất: vẽ radar vẫn chạy mỗi
	// khung hình, nên tự cập nhật ở đây nếu Update() chưa được gọi kể từ lần vẽ trước.
	if (!updatedSinceDraw)
	{
		if (!diagFallbackLogged)
		{
			diagFallbackLogged = true;
			logger.Log("gameProcessEvent not firing - updating from the radar draw event instead");
		}
		Update();
	}
	updatedSinceDraw = false;

	if (!util::NavEnabled(cfg, player))
		return;

	if (renderTargetRoute)
		renderPath(targetRoute, cfg.GPS_LINE_CLR);

	if (renderMissionRoute)
		renderPath(missionRoute, missionColor);
}

void GPS::drawDistanceText(const CVector2D &radarAnchor, float yOffset, const CRGBA &color, float distance)
{
	const float sx = static_cast<float>(RsGlobal.maximumWidth) / 640.0f;
	const float sy = static_cast<float>(RsGlobal.maximumHeight) / 448.0f;

	CFont::SetOrientation(ALIGN_CENTER);
	CFont::SetColor(color);
	CFont::SetBackground(false, false);
	CFont::SetWrapx(500.0f);
	CFont::SetScale(0.3f * sx, 0.6f * sy);
	CFont::SetFontStyle(FONT_SUBTITLES);
	CFont::SetProportional(true);
	CFont::SetDropShadowPosition(1);
	CFont::SetDropColor(CRGBA(0, 0, 0, 180));

	CVector2D point;
	CRadar::TransformRadarPointToScreenSpace(point, radarAnchor);

	std::string text = util::makeDist(distance, cfg.DISTANCE_UNITS);
	CFont::PrintString(point.x, point.y + yOffset * sy, text.data());
}

void GPS::DrawHud()
{
	if (!cfg.ENABLE_DISTANCE_TEXT || !util::NavEnabled(cfg, player))
		return;

	const CVector playerPos = player->GetPosition();

	if (renderMissionRoute)
		drawDistanceText(CVector2D(0.0f, -1.0f), 8.0f, missionColor, DistanceBetweenPoints(playerPos, missionPos));

	if (renderTargetRoute)
		drawDistanceText(CVector2D(0.0f, 1.0f), -20.0f, cfg.GPS_LINE_CLR, DistanceBetweenPoints(playerPos, targetPos));
}

void GPS::renderPath(Route &route, const CRGBA &vColor)
{
	// Chỉ giữ khoá đủ lâu để copy con trỏ; vẽ (nặng) thì không giữ khoá, thread tính đường không bị chặn.
	std::shared_ptr<const std::vector<CVector2D>> points;
	{
		std::lock_guard<std::mutex> lock(pathMutex);
		points = route.points;
	}
	if (!points)
		return;

	const size_t count = std::min<size_t>(points->size(), MAX_NODE_POINTS);
	if (count < 2)
		return;

	const bool fullMap = FrontEndMenuManager.m_bDrawRadarOrMap;

	for (size_t i = 0; i < count; i++)
	{
		CVector2D tmpPoint;

		CRadar::TransformRealWorldPointToRadarSpace(tmpPoint, (*points)[i]);
		if (!fullMap)
		{
			CRadar::TransformRadarPointToScreenSpace(tmpNodePoints[i], tmpPoint);
		}
		else
		{
			CRadar::LimitRadarPoint(tmpPoint);
			CRadar::TransformRadarPointToScreenSpace(tmpNodePoints[i], tmpPoint);
			tmpNodePoints[i].x *= static_cast<float>(RsGlobal.maximumWidth) / 640.0f;
			tmpNodePoints[i].y *= static_cast<float>(RsGlobal.maximumHeight) / 448.0f;
			CRadar::LimitToMap(&tmpNodePoints[i].x, &tmpNodePoints[i].y);
		}
	}

	const bool scissor =
		!fullMap && (reinterpret_cast<D3DCAPS9 const *>(RwD3D9GetCaps())->RasterCaps & D3DPRASTERCAPS_SCISSORTEST);

	CRect scissorRect(0, 0, 0, 0);
	if (scissor)
	{
		RECT rect;
		CVector2D posn;
		CRadar::TransformRadarPointToScreenSpace(posn, CVector2D(-1.0f, -1.0f));
		rect.left = static_cast<LONG>(posn.x + 2.0f);
		rect.bottom = static_cast<LONG>(posn.y - 2.0f);
		CRadar::TransformRadarPointToScreenSpace(posn, CVector2D(1.0f, 1.0f));
		rect.right = static_cast<LONG>(posn.x - 2.0f);
		rect.top = static_cast<LONG>(posn.y + 2.0f);
		reinterpret_cast<IDirect3DDevice9 *>(GetD3DDevice())->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
		reinterpret_cast<IDirect3DDevice9 *>(GetD3DDevice())->SetScissorRect(&rect);

		scissorRect.Grow(rect.left, rect.right, rect.top, rect.bottom);
	}

	// Đặt rõ ràng mọi trạng thái vẽ thay vì thừa hưởng từ lần vẽ trước: sương mù (fog) còn bật,
	// alpha/blend sai... sẽ làm màu đường bị nhạt và ngả trắng.
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);
	RwRenderStateSet(rwRENDERSTATEFOGENABLE, reinterpret_cast<void *>(FALSE));
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, reinterpret_cast<void *>(TRUE));
	RwRenderStateSet(rwRENDERSTATESRCBLEND, reinterpret_cast<void *>(rwBLENDSRCALPHA));
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, reinterpret_cast<void *>(rwBLENDINVSRCALPHA));
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, reinterpret_cast<void *>(FALSE));
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, reinterpret_cast<void *>(FALSE));

	float widthScale = 1.0f;
	if (fullMap)
	{
		float mp = FrontEndMenuManager.m_fMapZoom - 140.0f;
		mp = std::clamp(mp, 140.0f, 960.0f);
		widthScale = mp / 960.0f + 0.4f;
	}
	const float width = cfg.GPS_LINE_WIDTH * widthScale;

	// Mỗi đoạn nối hai node liên tiếp là một hình chữ nhật (4 đỉnh) trong dải tam giác.
	unsigned int vertIndex = 0;
	for (size_t i = 0; i + 1 < count; i++)
	{
		const CVector2D dir = tmpNodePoints[i + 1] - tmpNodePoints[i];
		const float angle = atan2(dir.y, dir.x);

		CVector2D shift[2];
		shift[0].x = cosf(angle - M_PI_2) * width;
		shift[0].y = sinf(angle - M_PI_2) * width;
		shift[1].x = cosf(angle + M_PI_2) * width;
		shift[1].y = sinf(angle + M_PI_2) * width;

		// Chỉ dựng đỉnh cho điểm nằm trong radar. Nếu rect = 0 nghĩa là đang mở bản đồ
		// toàn màn hình nên không cắt.
		if (scissorRect.IsPointInside(tmpNodePoints[i]) ||
			(scissorRect.bottom + scissorRect.top + scissorRect.left + scissorRect.right) == 0)
		{
			util::Setup2dVertex(lineVerts[vertIndex + 0], tmpNodePoints[i].x + shift[0].x,
								tmpNodePoints[i].y + shift[0].y, vColor);
			util::Setup2dVertex(lineVerts[vertIndex + 1], tmpNodePoints[i].x + shift[1].x,
								tmpNodePoints[i].y + shift[1].y, vColor);
			util::Setup2dVertex(lineVerts[vertIndex + 2], tmpNodePoints[i + 1].x + shift[0].x,
								tmpNodePoints[i + 1].y + shift[0].y, vColor);
			util::Setup2dVertex(lineVerts[vertIndex + 3], tmpNodePoints[i + 1].x + shift[1].x,
								tmpNodePoints[i + 1].y + shift[1].y, vColor);
			vertIndex += 4;
		}
	}

	if (vertIndex)
		RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, lineVerts.data(), vertIndex);

	if (scissor)
		reinterpret_cast<IDirect3DDevice9 *>(GetD3DDevice())->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
}
