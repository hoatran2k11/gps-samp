#include "GPS.h"

#include <cstdint>

namespace
{
	constexpr int RADAR_TRACE_COUNT = 175; // CRadar::ms_RadarTrace[175]
}

void GPS::Run()
{
	// Toàn bộ địa chỉ patch bên dưới là của GTA SA 1.0 US — đúng cả cho bản
	// chạy SAMP (SAMP 0.3.7 bắt buộc dùng exe 1.0 US). Phiên bản khác thì không đụng vào gì.
	const unsigned int ver = plugin::GetGameVersion();
	if (ver != GAME_10US_COMPACT && ver != GAME_10US_HOODLUM)
		return;

	logger = util::Logger(cfg.LOGFILE_ENABLED);
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

	plugin::Events::gameProcessEvent += [this]() { this->Update(); };
	plugin::Events::drawRadarOverlayEvent += [this]() { this->DrawRadarOverlay(); };
	plugin::Events::drawRadarEvent += [this]() { this->DrawHud(); };
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

void GPS::requestPath(Route &route, const PathQuery &query)
{
	// Đang tính dở thì bỏ qua; khung hình sau sẽ yêu cầu lại với vị trí mới.
	if (route.pending.valid() && route.pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
		return;

	int epoch;
	{
		std::lock_guard<std::mutex> lock(pathMutex);
		epoch = route.epoch;
	}

	route.pending = std::async(std::launch::async, [this, &route, query, epoch]() {
		short count = 0;
		float distance = 0.0f;
		std::array<CNodeAddress, MAX_NODE_POINTS> found{};

		ThePaths.DoPathSearch(0, query.from, CNodeAddress(), query.to, found.data(), &count, MAX_NODE_POINTS, &distance,
							  999999.0f, NULL, 999999.0f, query.respectLanes, CNodeAddress(), false, query.boat);

		count = std::clamp<short>(count, 0, MAX_NODE_POINTS);

		std::lock_guard<std::mutex> lock(pathMutex);
		if (route.epoch != epoch)
			return; // đích đã đổi trong lúc tính — bỏ kết quả cũ
		route.nodes.assign(found.begin(), found.begin() + count);
		route.distance = distance;
	});
}

void GPS::clearRoute(Route &route)
{
	std::lock_guard<std::mutex> lock(pathMutex);
	route.nodes.clear();
	route.distance = 0.0f;
	route.epoch++;
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
	else if (!cfg.MP_TRACK_SERVER_BLIPS)
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
			clearRoute(targetRoute);
		lastTargetBlip = 0;
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
	}

	targetPos = trace->m_vecPos;
	requestPath(targetRoute, makeQuery(targetPos));
	renderTargetRoute = true;
}

void GPS::updateMissionRoute(const CVector &playerPos)
{
	CVector dest;
	const tRadarTrace *trace = findMissionTrace(playerPos, dest);

	if (!trace)
	{
		if (lastMissionTrace)
			clearRoute(missionRoute);
		lastMissionTrace = nullptr;
		return;
	}

	if (trace != lastMissionTrace)
	{
		clearRoute(missionRoute); // đổi mục tiêu: bỏ đường cũ
		lastMissionTrace = trace;
	}

	missionPos = dest;
	missionColor = util::SetupColor(trace->m_nColour, trace->m_bFriendly, cfg);
	requestPath(missionRoute, makeQuery(dest));
	renderMissionRoute = true;
}

void GPS::Update()
{
	mode = util::DetectGameMode(cfg.FORCE_MODE);
	player = FindPlayerPed(0);

	renderTargetRoute = false;
	renderMissionRoute = false;

	if (mode == util::GameMode::SAMP && !cfg.MP_ENABLED)
		return;

	if (!util::NavEnabled(cfg, player))
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
	std::lock_guard<std::mutex> lock(pathMutex);

	const size_t count = std::min<size_t>(route.nodes.size(), MAX_NODE_POINTS);
	if (count < 2)
		return;

	const bool fullMap = FrontEndMenuManager.m_bDrawRadarOrMap;

	for (size_t i = 0; i < count; i++)
	{
		const CPathNode *node = ThePaths.GetPathNode(route.nodes[i]);
		if (!node)
			return; // vùng đường chưa nạp — bỏ qua khung hình này

		const CVector nodePosn = node->GetNodeCoors();
		CVector2D tmpPoint;

		CRadar::TransformRealWorldPointToRadarSpace(tmpPoint, CVector2D(nodePosn.x, nodePosn.y));
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

	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, NULL);

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
