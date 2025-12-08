#include "stdafx.hpp"

#include <chrono>

#include "worldsize.h"

#include "renderer\mesh_renderer.hpp"
#include "spt\features\tracing.hpp"

#if defined(SPT_MESH_RENDERING_ENABLED) && defined(SPT_TRACE_PORTAL_ENABLED)

#include "spt\feature.hpp"
#include "spt\features\hud.hpp"
#include "spt\features\generic.hpp"
#include "spt\features\ent_props.hpp"
#include "spt\utils\ent_utils.hpp"
#include "spt\utils\portal_utils.hpp"
#include "spt\utils\game_detection.hpp"
#include "spt\utils\signals.hpp"
#include "spt\utils\interfaces.hpp"
#include "spt\sptlib-wrapper.hpp"
#include "spt\utils\math.hpp"
#include "spt\utils\portal_utils.hpp"
#include "spt\utils\game_detection.hpp"
#include "imgui\imgui_interface.hpp"

#define PORTAL_HALF_WIDTH 32.0f
#define PORTAL_HALF_HEIGHT 54.0f
#define PORTAL_HALF_DEPTH 2.0f
#define PORTAL_BUMP_FORGIVENESS 2.0f

#define PORTAL_PLACEMENT_FAIL_NO_SERVER -1.0f
#define PORTAL_PLACEMENT_FAIL_NO_WEAPON -2.0f

#define PORTAL_PLACEMENT_SUCCESS_NO_BUMP 1.0f
#define PORTAL_PLACEMENT_SUCCESS_CANT_FIT 0.1f
#define PORTAL_PLACEMENT_SUCCESS_CLEANSER 0.028f
#define PORTAL_PLACEMENT_SUCCESS_OVERLAP_LINKED 0.027f
#define PORTAL_PLACEMENT_SUCCESS_NEAR 0.0265f
#define PORTAL_PLACEMENT_SUCCESS_INVALID_VOLUME 0.026f
#define PORTAL_PLACEMENT_SUCCESS_INVALID_SURFACE 0.025f
#define PORTAL_PLACEMENT_SUCCESS_PASSTHROUGH_SURFACE 0.0f

ConVar y_spt_hud_portal_placement("y_spt_hud_portal_placement",
                                  "0",
                                  FCVAR_CHEAT,
                                  "Show portal placement info\n"
                                  "      1 = Boolean result\n"
                                  "      2 = String result\n"
                                  "      3 = Float result");
ConVar y_spt_draw_pp("y_spt_draw_pp", "0", FCVAR_CHEAT, "Draw portal placement.");
ConVar y_spt_draw_pp_blue("y_spt_draw_pp_blue", "1", FCVAR_CHEAT, "Draw blue portal placement.");
ConVar y_spt_draw_pp_orange("y_spt_draw_pp_orange", "1", FCVAR_CHEAT, "Draw orange portal placement.");
ConVar y_spt_draw_pp_failed("y_spt_draw_pp_failed", "0", FCVAR_CHEAT, "Draw failed portal placement.");
ConVar y_spt_draw_pp_bbox("y_spt_draw_pp_bbox", "0", FCVAR_CHEAT, "Draw the bounding box of the portal placement.");

enum PlacementGridFlags
{
	PLACEMENT_GRID_NONE = 0,
	PLACEMENT_GRID_ENABLED = 1,        // enable/disable
	PLACEMENT_GRID_BLUE = 2,           // blue/orange
	PLACEMENT_GRID_SHOOT_LOCATION = 4, // show shoot/bump location
};

// Portal placement related features
class PortalPlacement : public FeatureWrapper<PortalPlacement>
{
public:
	struct PlacementInfo
	{
		float placementResult;
		// checked after placement, so I don't want to merge with the placement result, only valid if result > 0.5
		bool fizzleAfterPlace;
		Vector finalPos;
		QAngle finalAngles;
		trace_t tr;
	};

	void UpdatePlacementInfo();

	PlacementInfo p1;
	PlacementInfo p2;

	struct
	{
		std::vector<StaticMesh> meshes;
		std::vector<std::pair<Vector, color32>> unmergedPts;
		int flags; // NOT the same as spt_draw_pp_grid_type
		Vector camPos;
		QAngle camAng;
		int gridWidth = 0;
		int gridIdx = 0;           // which point are we on? in [0, gridWidth * gridWidth)
		float gridAngDiameter = 0; // angular diameter of the grid in degrees
		std::string mapName;

		void Reset()
		{
			meshes.clear();
			unmergedPts.clear();
			flags = PLACEMENT_GRID_NONE;
			gridWidth = 0;
			mapName.clear();
		}
	} ppGrid;

protected:
	virtual bool ShouldLoadFeature() override;
	virtual void InitHooks() override;
	virtual void LoadFeature() override;

private:
	bool placementInfoUpdateRequested = false;
	ConVar* sv_portal_placement_never_fail = nullptr;

	DECL_MEMBER_CDECL(bool,
	                  UTIL_TestForOrientationVolumes,
	                  QAngle& vecCurAngles,
	                  Vector& vecCurOrigin,
	                  CBaseEntity* pPortal);

	DECL_MEMBER_CDECL(CBaseEntity*,
	                  CProp_Portal__FindPortal,
	                  uchar iLinkageGroupID,
	                  bool bPortal2,
	                  bool bCreateIfNothingFound);

	void OnMeshRenderSignal(MeshRendererDelegate& mr);

	void RunPpGridIteration(MeshRendererDelegate& mr);
	void AddUnmergedGridPointsToBuilder(MeshBuilderDelegate& mb, size_t startIdx = 0, size_t endIdx = INT_MAX);

	void TestForOrientationVolumes(QAngle& placedAngles,
	                               Vector& placedPos,
	                               bool bPortal2,
	                               CBaseCombatWeapon* portalGun) const;

	// our version of CProp_Portal::TestRestingSurfaceThink(), returns false if fizzle
	bool GoodRestingSurface(const QAngle& placedAngles, const Vector& placedPos) const;

	void PostPlacementChecks(QAngle& placedAngles,   // in/out
	                         Vector& placedPos,      // in/out
	                         float& placementResult, // in/out
	                         bool bPortal2,
	                         bool& fizzle, // out
	                         CBaseCombatWeapon* portalGun) const;

	static void ImGuiGunPlacementCallback();
	static void ImGuiGridPlacementCallback();
	static void ImGuiTextHudPortalPlacementCallback(ConVar& var);
};

static PortalPlacement spt_pp;

ConVar y_spt_draw_pp_grid_width("y_spt_draw_pp_grid_width",
                                "200",
                                FCVAR_NONE,
                                "Width of portal placement grid in number of points");

ConVar y_spt_draw_pp_grid_fov("y_spt_draw_pp_grid_fov",
                              "60",
                              FCVAR_NONE,
                              "Angular diameter of portal placement grid in degrees");

ConVar y_spt_draw_pp_grid_type("y_spt_draw_pp_grid_type",
                               "0",
                               FCVAR_NONE,
                               "Portal placement grid type:\n"
                               "  0 - place cubes at shoot locations\n"
                               "  1 - place cubes at portal bump locations");

ConVar y_spt_draw_pp_grid_ms_per_frame(
    "y_spt_draw_pp_grid_ms_per_frame",
    "7",
    FCVAR_NONE,
    "How many milliseconds to spend per frame to try portal placements. Larger values compute the grid faster but make the game laggier.");

CON_COMMAND_F(
    y_spt_draw_pp_grid,
    "Enables or disables the portal placement grid.\n Optionally takes one of the following arguments (no args is treated as 1):\n"
    "  0 - disable portal placement grid\n"
    "  1 - create portal placement grid for blue portal\n"
    "  2 - create portal placement grid for orange portal\n"
    "Shows the following colors:\n"
    "  green - portal placement success, with darker shades meaning a further portal bump\n"
    "  red   - portal placement failure\n"
    "  blue  - portal placement success but the portal will fizzle 0.1s after landing",
    FCVAR_CHEAT)
{
	auto& grid = spt_pp.ppGrid;
	int newFlags = PLACEMENT_GRID_ENABLED;

	int argVal = args.ArgC() > 1 ? atoi(args[1]) : 1;

	if (argVal < 0 || argVal > 2)
	{
		Warning("Bad argument, expected 0, 1, or 2\n");
		return;
	}
	if (argVal == 0)
	{
		grid.Reset();
		return;
	}
	if (!interfaces::engine_server->PEntityOfEntIndex(1))
	{
		Warning("No server player\n");
		return;
	}
	if (argVal == 1)
		newFlags |= PLACEMENT_GRID_BLUE;
	if (!y_spt_draw_pp_grid_type.GetBool())
		newFlags |= PLACEMENT_GRID_SHOOT_LOCATION;

	grid.flags = newFlags;
	grid.meshes.clear();
	grid.unmergedPts.clear();
	grid.gridIdx = 0;
	grid.gridWidth = clamp(y_spt_draw_pp_grid_width.GetInt(), 0, 20'000);
	grid.camPos = utils::GetPlayerEyePosition();
	grid.gridAngDiameter = clamp(y_spt_draw_pp_grid_fov.GetFloat(), 0.1, 179.9);
	grid.camAng = utils::GetPlayerEyeAngles();
	const utils::PortalInfo* env = utils::GetEnvironmentPortal();
	transformThroughPortal(env, grid.camPos, grid.camAng, grid.camPos, grid.camAng);
	grid.mapName = interfaces::engine_tool->GetCurrentMap();
}

void PortalPlacement::UpdatePlacementInfo()
{
	auto player = utils::spt_serverEntList.GetPlayer();
	if (!player)
	{
		p1.placementResult = PORTAL_PLACEMENT_FAIL_NO_SERVER;
		return;
	}

	auto pgun = spt_tracing.ORIG_GetActiveWeapon(player);

	if (!pgun)
	{
		p1.placementResult = PORTAL_PLACEMENT_FAIL_NO_WEAPON;
		return;
	}
	Vector camPos = utils::GetPlayerEyePosition();
	QAngle angles = utils::GetPlayerEyeAngles();

	p1.placementResult =
	    spt_tracing.TraceTransformFirePortal(p1.tr, camPos, angles, p1.finalPos, p1.finalAngles, false);
	PostPlacementChecks(p1.finalAngles, p1.finalPos, p1.placementResult, false, p1.fizzleAfterPlace, pgun);

	p2.placementResult =
	    spt_tracing.TraceTransformFirePortal(p2.tr, camPos, angles, p2.finalPos, p2.finalAngles, true);
	PostPlacementChecks(p2.finalAngles, p2.finalPos, p2.placementResult, true, p2.fizzleAfterPlace, pgun);
}

static void DrawPortal(MeshBuilderDelegate& mb, const PortalPlacement::PlacementInfo& info, color32 col)
{
	if (!info.tr.DidHit())
		return;

	const color32 failedColor = {255, 0, 0, 127};
	const color32 noDrawColor = {0, 0, 0, 0};
	const color32 fizzleColor = {100, 100, 100, 127};

	color32 portalColor;
	if (info.fizzleAfterPlace)
	{
		portalColor = fizzleColor;
	}
	else if (info.placementResult <= 0.5f)
	{
		if (y_spt_draw_pp_failed.GetBool()
		    && info.placementResult != PORTAL_PLACEMENT_SUCCESS_PASSTHROUGH_SURFACE
		    && info.placementResult != PORTAL_PLACEMENT_SUCCESS_CLEANSER)
		{
			portalColor = failedColor;
		}
		else
		{
			portalColor = noDrawColor;
		}
	}
	else
	{
		portalColor = col;
	}

	ShapeColor facePortalColor{C_FACE(portalColor), false};
	mb.AddEllipse(info.finalPos, info.finalAngles, PORTAL_HALF_WIDTH, PORTAL_HALF_HEIGHT, 32, facePortalColor);

	if (y_spt_draw_pp_bbox.GetBool())
	{
		const Vector portalMaxs(1, PORTAL_HALF_WIDTH, PORTAL_HALF_HEIGHT);
		ShapeColor outlinePortalColor{C_WIRE(portalColor), false};
		mb.AddBox(info.finalPos, -portalMaxs, portalMaxs, info.finalAngles, outlinePortalColor);
	}
}

void PortalPlacement::OnMeshRenderSignal(MeshRendererDelegate& mr)
{
	if (ppGrid.flags & PLACEMENT_GRID_ENABLED)
	{
		if (ppGrid.gridIdx < ppGrid.gridWidth * ppGrid.gridWidth)
			RunPpGridIteration(mr);
		if (ppGrid.mapName != interfaces::engine_tool->GetCurrentMap() || !StaticMesh::AllValid(ppGrid.meshes))
			ppGrid.Reset();
		for (auto& mesh : ppGrid.meshes)
			mr.DrawMesh(mesh);

		// draw the unmerged points as dynamic meshes

		size_t maxVerts, maxIndices;
		GetMaxMeshSize(maxVerts, maxIndices, true);
		const int maxCubesPerMesh = MIN(maxIndices / 36, maxVerts / 8) - 1;

		for (size_t start = 0; start < ppGrid.unmergedPts.size(); start += maxCubesPerMesh)
		{
			mr.DrawMesh(spt_meshBuilder.CreateDynamicMesh(
			    [this, start, maxCubesPerMesh](MeshBuilderDelegate& mb)
			    { AddUnmergedGridPointsToBuilder(mb, start, start + maxCubesPerMesh); }));
		}
	}

	if (placementInfoUpdateRequested || y_spt_draw_pp.GetBool())
	{
		UpdatePlacementInfo();
		placementInfoUpdateRequested = false;
	}

	if (!y_spt_draw_pp.GetBool())
		return;

	// No portalgun
	if (p1.placementResult == PORTAL_PLACEMENT_FAIL_NO_SERVER
	    || p1.placementResult == PORTAL_PLACEMENT_FAIL_NO_WEAPON)
	{
		return;
	}

	mr.DrawMesh(spt_meshBuilder.CreateDynamicMesh(
	    [&](MeshBuilderDelegate& mb)
	    {
		    const color32 blueColor = {64, 160, 255, 127};
		    const color32 orangeColor = {255, 160, 32, 127};

		    // Draw in this order so the color will mix to purple when overlapping.
		    if (y_spt_draw_pp_orange.GetBool())
			    DrawPortal(mb, p2, orangeColor);

		    if (y_spt_draw_pp_blue.GetBool())
			    DrawPortal(mb, p1, blueColor);
	    }));
}

void PortalPlacement::RunPpGridIteration(MeshRendererDelegate& mr)
{
	auto pgun = spt_tracing.GetActiveWeapon();
	if (!pgun)
	{
		ppGrid.Reset();
		return;
	}

	bool bPortal2 = !(ppGrid.flags & PLACEMENT_GRID_BLUE);

	matrix3x4_t playerRotMat;
	AngleMatrix(ppGrid.camAng, playerRotMat);

	size_t maxVerts, maxIndices;
	GetMaxMeshSize(maxVerts, maxIndices, false);
	const size_t maxCubesPerMesh = MIN(maxIndices / 36, maxVerts / 8) - 1;

	int gridWidth = ppGrid.gridWidth;
	int numGridPts = gridWidth * gridWidth;

	using namespace std::chrono;
	auto startTime = high_resolution_clock::now();

	for (int iter = 0; iter < 10'000; iter++)
	{
		// Step 1: map ppGrid.gridIdx to a different index to make the grid points appear
		// to uniformly generate. This is achieved by doing a perfect shuffle a few times.

		size_t newIdx = ppGrid.gridIdx++;
		size_t halfNumGridPts = (numGridPts + 1) / 2;
		for (size_t i = 0; i < 7; i++)
		{
			if (newIdx < halfNumGridPts)
				newIdx *= 2;
			else
				newIdx = (newIdx - halfNumGridPts) * 2 + 1;
		}
		size_t gridX = newIdx % gridWidth;
		size_t gridY = newIdx / gridWidth;

		// Step 2: get the direction of this ray, rotate it to the player eye angles, and shoot it

		Vector dir;
		if (gridWidth == 1)
		{
			dir = Vector(1, 0, 0);
		}
		else
		{
			dir = Vector{
			    1,
			    tanf(DEG2RAD((gridX - (gridWidth - 1) * 0.5f) / (gridWidth - 1) * ppGrid.gridAngDiameter)),
			    tanf(DEG2RAD((gridY - (gridWidth - 1) * 0.5f) / (gridWidth - 1) * ppGrid.gridAngDiameter)),
			};
		}
		utils::VectorTransform(playerRotMat, dir);

		Vector spherePos;
		trace_t tr;

		Ray_t ray;
		ray.Init(ppGrid.camPos, ppGrid.camPos + dir * MAX_TRACE_LENGTH);
		interfaces::engineTraceServer->TraceRay(ray, MASK_SHOT_PORTAL, spt_tracing.GetPortalTraceFilter(), &tr);
		spherePos = tr.endpos;

		// shoot portal ray!!!

		Vector placePos;
		QAngle placeAng;
		bool fizzle;

		const int PORTAL_PLACED_BY_PLAYER = 2;
		float placementResult = spt_tracing.ORIG_TraceFirePortal(pgun,
		                                                         bPortal2,
		                                                         ppGrid.camPos,
		                                                         dir,
		                                                         tr,
		                                                         placePos,
		                                                         placeAng,
		                                                         PORTAL_PLACED_BY_PLAYER,
		                                                         true);

		if (!tr.DidHit())
			continue;

		color32 c;

		PostPlacementChecks(placeAng, placePos, placementResult, bPortal2, fizzle, pgun);
		if (fizzle)
		{
			c = {0, 0, 255, 255};
		}
		else
		{
			if (placementResult > 0.5f)
				c = {0, (byte)(255 * (placementResult * 1.8f - 0.8f)), 50, 255};
			else
				c = {150, 0, (byte)(255 * placementResult), 255};
		}

		if (!(ppGrid.flags & PLACEMENT_GRID_SHOOT_LOCATION) && placementResult > 0.5)
			spherePos = placePos;

		// Step 3: if we have enough points, merge them into one mesh

		ppGrid.unmergedPts.emplace_back(spherePos, c);

		if (ppGrid.unmergedPts.size() >= maxCubesPerMesh || ppGrid.gridIdx >= numGridPts)
		{
			ppGrid.meshes.emplace_back(spt_meshBuilder.CreateStaticMesh(
			    [this](MeshBuilderDelegate& mb) { AddUnmergedGridPointsToBuilder(mb); }));
			ppGrid.unmergedPts.clear();
		}

		if (ppGrid.gridIdx >= numGridPts)
			break;

		// limit how much time we spend in this loop, check every 8 iterations
		if ((iter & 7) == 7
		    && duration_cast<microseconds>(high_resolution_clock::now() - startTime).count() / 1000.f
		           > y_spt_draw_pp_grid_ms_per_frame.GetFloat())
		{
			break;
		}
	}
}

void PortalPlacement::AddUnmergedGridPointsToBuilder(MeshBuilderDelegate& mb, size_t startIdx, size_t endIdx)
{
	if (ppGrid.gridWidth == 1)
	{
		mb.AddSphere(ppGrid.unmergedPts[0].first, 3, 0, {C_FACE(ppGrid.unmergedPts[0].second), false});
	}
	else
	{
		float ratio = tan(DEG2RAD(1.f / (ppGrid.gridWidth - 1) * ppGrid.gridAngDiameter) / 2.2f);
		for (size_t i = startIdx; i < endIdx && i < ppGrid.unmergedPts.size(); i++)
		{
			auto& ppGridPt = ppGrid.unmergedPts[i];
			float rad = MIN(3, ratio * ppGridPt.first.DistTo(ppGrid.camPos));
			mb.AddSphere(ppGridPt.first, rad, 0, {C_FACE(ppGridPt.second), false});
		}
	}
}

bool PortalPlacement::ShouldLoadFeature()
{
	if (!utils::DoesGameLookLikePortal())
		return false;
	// can't use a static ConVarRef cuz that'll be invalidated on every spt_tas_restart_game
	sv_portal_placement_never_fail = g_pCVar->FindVar("sv_portal_placement_never_fail");
	return !!sv_portal_placement_never_fail;
}

namespace patterns
{
	PATTERNS(UTIL_TestForOrientationVolumes,
	         "5135-portal1",
	         "83 7C 24 ?? 00 75 ?? 32 C0 C3 53 55 56 8B 35 ?? ?? ?? ??",
	         "7197370-portal1",
	         "55 8B EC 83 EC 08 83 7D ?? 00 75 ??");
	PATTERNS(CProp_Portal__FindPortal,
	         "5135-portal1",
	         "53 8A 5C 24 ?? 0F B6 C3",
	         "7197370-portal1",
	         "55 8B EC 53 8A 7D ?? 8A 5D 0C");
} // namespace patterns

void PortalPlacement::InitHooks()
{
	FIND_PATTERN(server, UTIL_TestForOrientationVolumes);
	FIND_PATTERN(server, CProp_Portal__FindPortal);
}

static const wchar_t* PlacementResultToString(PortalPlacement::PlacementInfo& info)
{
	float placement = info.placementResult;
	if (placement > 0.5f && info.fizzleAfterPlace)
		return L"Fizzle spot (bad surface)";
	if (placement == PORTAL_PLACEMENT_SUCCESS_NO_BUMP)
		return L"Success with no bump";
	if (placement == PORTAL_PLACEMENT_SUCCESS_CANT_FIT)
		return L"Can't fit";
	if (placement == PORTAL_PLACEMENT_SUCCESS_CLEANSER)
		return L"Fizzler";
	if (placement == PORTAL_PLACEMENT_SUCCESS_OVERLAP_LINKED)
		return L"Overlaps existing portal";
	if (placement == PORTAL_PLACEMENT_SUCCESS_NEAR)
		// Not possible for non-test shot
		return L"Near existing portal";
	if (placement == PORTAL_PLACEMENT_SUCCESS_INVALID_VOLUME)
		return L"Invalid volume";
	if (placement == PORTAL_PLACEMENT_SUCCESS_INVALID_SURFACE)
		return L"Invalid surface";
	if (placement == PORTAL_PLACEMENT_SUCCESS_PASSTHROUGH_SURFACE)
		return L"Passthrough surface";
	if (placement > 0.5f)
		return L"Success with bump";
	return L"Bump too far"; // Is this possible?
}

void PortalPlacement::LoadFeature()
{
	bool hudCallbackEnabled = false;
	if (spt_tracing.ORIG_TraceFirePortal && spt_tracing.ORIG_GetActiveWeapon)
	{
#ifdef SPT_HUD_ENABLED
		hudCallbackEnabled = AddHudCallback(
		    "portal_placement",
		    [](std::string args)
		    {
			    int mode = args == "" ? y_spt_hud_portal_placement.GetInt()
			                          : (int)strtol(args.c_str(), nullptr, 10);
			    spt_pp.placementInfoUpdateRequested = true;

			    float res1 = spt_pp.p1.placementResult;
			    float res2 = spt_pp.p2.placementResult;

			    const Color blue = {64, 160, 255, 255};
			    const Color orange = {255, 160, 32, 255};
			    const Color failed = {150, 150, 150, 255};

			    const Color& blueTextColor = res1 > 0.5f ? blue : failed;
			    const Color& orangeTextColor = res2 > 0.5f ? orange : failed;

			    if (res1 == PORTAL_PLACEMENT_FAIL_NO_SERVER)
			    {
				    spt_hud_feat.DrawTopHudElement(L"Portal: No server player");
			    }
			    else if (res1 == PORTAL_PLACEMENT_FAIL_NO_WEAPON)
			    {
				    spt_hud_feat.DrawTopHudElement(L"Portal: No portalgun");
			    }
			    else if (mode == 0 || mode == 1)
			    {
				    spt_hud_feat.DrawColorTopHudElement(blueTextColor, L"Portal1: %d", res1 > 0.5f);
				    spt_hud_feat.DrawColorTopHudElement(orangeTextColor, L"Portal2: %d", res2 > 0.5f);
			    }
			    else if (mode == 2)
			    {
				    spt_hud_feat.DrawColorTopHudElement(blueTextColor,
				                                        L"Portal1: %s",
				                                        PlacementResultToString(spt_pp.p1));
				    spt_hud_feat.DrawColorTopHudElement(orangeTextColor,
				                                        L"Portal2: %s",
				                                        PlacementResultToString(spt_pp.p2));
			    }
			    else if (mode > 0)
			    {
				    spt_hud_feat.DrawColorTopHudElement(blueTextColor, L"Portal1: %f", res1);
				    spt_hud_feat.DrawColorTopHudElement(orangeTextColor, L"Portal2: %f", res2);
			    }
		    },
		    y_spt_hud_portal_placement);
#endif
#ifdef SPT_MESH_RENDERING_ENABLED
		if (spt_meshRenderer.signal.Works)
		{
			InitConcommandBase(y_spt_draw_pp);
			InitConcommandBase(y_spt_draw_pp_blue);
			InitConcommandBase(y_spt_draw_pp_orange);
			InitConcommandBase(y_spt_draw_pp_failed);
			InitConcommandBase(y_spt_draw_pp_bbox);

			InitCommand(y_spt_draw_pp_grid);
			InitConcommandBase(y_spt_draw_pp_grid_width);
			InitConcommandBase(y_spt_draw_pp_grid_fov);
			InitConcommandBase(y_spt_draw_pp_grid_type);
			InitConcommandBase(y_spt_draw_pp_grid_ms_per_frame);

			spt_meshRenderer.signal.Connect(this, &PortalPlacement::OnMeshRenderSignal);
			SptImGuiGroup::Draw_PpPlacement_Gun.RegisterUserCallback(ImGuiGunPlacementCallback);
			SptImGuiGroup::Draw_PpPlacement_Grid.RegisterUserCallback(ImGuiGridPlacementCallback);
			if (hudCallbackEnabled)
				SptImGui::RegisterHudCvarCallback(y_spt_hud_portal_placement,
				                                  ImGuiTextHudPortalPlacementCallback,
				                                  false);
		}
#endif
	}
}

void PortalPlacement::TestForOrientationVolumes(QAngle& placedAngles,
                                                Vector& placedPos,
                                                bool bPortal2,
                                                CBaseCombatWeapon* portalGun) const
{
	if (!ORIG_UTIL_TestForOrientationVolumes)
		return;

	static bool offsetsFound = false;
	static int linkageIDOffset = utils::INVALID_DATAMAP_OFFSET;
	static int linkedPortalOffset = utils::INVALID_DATAMAP_OFFSET;

	if (!offsetsFound)
	{
		linkageIDOffset = spt_entprops.GetFieldOffset("CWeaponPortalgun", "m_iPortalLinkageGroupID", true);
		linkedPortalOffset = spt_entprops.GetFieldOffset("CProp_Portal", "m_hLinkedPortal", true);
		offsetsFound = true;
	}
	/*
	* We're gonna do an epic hack here. UTIL_TestForOrientationVolumes requires a non-null pointer to a portal,
	* but it only uses it for a funny orientation volume to match up the portal angles with its linked portal.
	* So we'll search for the linked portal and get a handle to it. Then we subtract an offset from a pointer to
	* that handle to create a "fake portal" such that fakePortal->m_hLinkedPortal refers to our handle. This
	* headache could probably be avoided if we instead called TraceFirePortal(bTest=true) but that seems to fizzle
	* travelling portals sometimes ://.
	*/

	CBaseHandle linkedPortalHandle{};
	if (ORIG_CProp_Portal__FindPortal && linkageIDOffset != utils::INVALID_DATAMAP_OFFSET && portalGun)
	{
		uchar linkageID = *((uchar*)portalGun + linkageIDOffset);
		linkedPortalHandle = (IServerEntity*)ORIG_CProp_Portal__FindPortal(linkageID, !bPortal2, false);
	}
	if (linkedPortalOffset != utils::INVALID_DATAMAP_OFFSET)
	{
		CBaseEntity* fakePortal = (CBaseEntity*)((uint32_t)&linkedPortalHandle - linkedPortalOffset);
		ORIG_UTIL_TestForOrientationVolumes(placedAngles, placedPos, fakePortal);
	}
}

bool PortalPlacement::GoodRestingSurface(const QAngle& placedAngles, const Vector& placedPos) const
{
	static bool offsetCached = false;
	static int classNameOffset = utils::INVALID_DATAMAP_OFFSET;

	if (!offsetCached)
	{
		classNameOffset = spt_entprops.GetFieldOffset("CBaseEntity", "m_iClassname", true);
		offsetCached = true;
	}

	Vector forward, right, up;
	AngleVectors(placedAngles, &forward, &right, &up);
	Vector sideOff = right * (PORTAL_HALF_WIDTH - PORTAL_BUMP_FORGIVENESS * 1.1f);
	Vector verticalOff = up * (PORTAL_HALF_HEIGHT - PORTAL_BUMP_FORGIVENESS * 1.1f);

	for (int i = 0; i < 4; i++)
	{
		Vector corner = placedPos;
		corner += i % 2 ? sideOff : -sideOff;
		corner += i < 2 ? verticalOff : -verticalOff;

		Ray_t ray;
		trace_t tr;
		ray.Init(corner, corner - forward);
		interfaces::engineTraceServer->TraceRay(ray,
		                                        MASK_SOLID_BRUSHONLY,
		                                        spt_tracing.GetPortalTraceFilter(),
		                                        &tr);

		if (tr.fraction == 1 && !tr.startsolid)
		{
			if (!tr.m_pEnt)
				return false;
			if (classNameOffset != utils::INVALID_DATAMAP_OFFSET)
			{
				const char* name = ((string_t*)((uint)tr.m_pEnt + classNameOffset))->ToCStr();
				if (strcmp(name, "func_physbox") && strcmp(name, "simple_physics_brush"))
					return false;
			}
		}
	}
	return true; // no fizzle
}

void PortalPlacement::PostPlacementChecks(QAngle& placedAngles,
                                          Vector& placedPos,
                                          float& placementResult,
                                          bool bPortal2,
                                          bool& fizzle,
                                          CBaseCombatWeapon* portalGun) const
{
	if (sv_portal_placement_never_fail->GetBool())
		placementResult = 1.0f;

	if (placementResult > 0.5)
	{
		TestForOrientationVolumes(placedAngles, placedPos, bPortal2, portalGun);
		fizzle = !GoodRestingSurface(placedAngles, placedPos);
	}
	else
	{
		fizzle = false;
	}
}

void PortalPlacement::ImGuiGunPlacementCallback()
{
	ImGui::BeginDisabled(!SptImGui::CvarCheckbox(y_spt_draw_pp, "Draw portal placement"));
	SptImGui::CvarCheckbox(y_spt_draw_pp_blue, "Draw for blue portal");
	SptImGui::CvarCheckbox(y_spt_draw_pp_orange, "Draw for orange portal");
	SptImGui::CvarCheckbox(y_spt_draw_pp_failed, "Draw failed portal placement");
	SptImGui::CvarCheckbox(y_spt_draw_pp_bbox, "Draw BBOX of placed portal");
	ImGui::EndDisabled();
	ImGuiTextHudPortalPlacementCallback(y_spt_hud_portal_placement);
}

void PortalPlacement::ImGuiGridPlacementCallback()
{
	ConCommand& cmd = y_spt_draw_pp_grid_command;
	const char* wrangledName = WrangleLegacyCommandName(cmd.GetName(), true, nullptr);
	if (ImGui::Button(ICON_CI_TRASH " Clear grid"))
	{
		const char* strArgs[] = {wrangledName, "0"};
		cmd.Dispatch({ARRAYSIZE(strArgs), strArgs});
	}
	ImGui::SetItemTooltip("%s 0", wrangledName);
	ImGui::SameLine();

	auto player = utils::spt_serverEntList.GetPlayer();
	bool disabled = !player || !spt_tracing.ORIG_GetActiveWeapon(player);
	ImGui::BeginDisabled(disabled);

	const ImVec4 blueColor = {64 / 255.f, 160 / 255.f, 255 / 255.f, 255 / 255.f};
	const ImVec4 orangeColor = {255 / 255.f, 160 / 255.f, 32 / 255.f, 255 / 255.f};
	ImGui::PushStyleColor(ImGuiCol_Text, blueColor);
	if (ImGui::Button(ICON_CI_CHECK_ALL " Draw for blue portal"))
	{
		const char* strArgs[] = {wrangledName, "1"};
		cmd.Dispatch({ARRAYSIZE(strArgs), strArgs});
	}
	ImGui::PopStyleColor();
	ImGui::SetItemTooltip("%s 1", wrangledName);
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Text, orangeColor);
	if (ImGui::Button(ICON_CI_CHECK_ALL " Draw for orange portal"))
	{
		const char* strArgs[] = {wrangledName, "2"};
		cmd.Dispatch({ARRAYSIZE(strArgs), strArgs});
	}
	ImGui::PopStyleColor();
	ImGui::SetItemTooltip("%s 2", wrangledName);
	ImGui::SameLine();
	SptImGui::HelpMarker("Help text for %s:\n\n%s", wrangledName, cmd.GetHelpText());

	// Neither V_snprintf nor snprintf are technically safe to use here lmao.
	// Just use the "secure" version since that'll give an assert if buf is too small.

	{
		ConVar& c = y_spt_draw_pp_grid_width;
		int oldVal = c.GetInt();
		int newVal = oldVal;
		char buf[64];
		_snprintf_s(buf, sizeof buf, "%s %%d", WrangleLegacyCommandName(c.GetName(), true, nullptr));
		ImGui::SliderInt("##grid_width", &newVal, 30, 500, buf, ImGuiSliderFlags_AlwaysClamp);
		if (oldVal != newVal)
			c.SetValue(newVal);
		ImGui::SameLine();
		SptImGui::HelpMarker("%s", c.GetHelpText());
	}
	{
		ConVar& c = y_spt_draw_pp_grid_fov;
		float oldVal = c.GetFloat();
		float newVal = oldVal;
		char buf[64];
		_snprintf_s(buf, sizeof buf, "%s %%.1f", WrangleLegacyCommandName(c.GetName(), true, nullptr));
		ImGui::SliderFloat("##grid_fov", &newVal, 5, 170, buf, ImGuiSliderFlags_AlwaysClamp);
		if (oldVal != newVal)
			c.SetValue(newVal);
		ImGui::SameLine();
		SptImGui::HelpMarker("%s", c.GetHelpText());
	}
	{
		ConVar& c = y_spt_draw_pp_grid_ms_per_frame;
		int oldVal = c.GetInt();
		int newVal = oldVal;
		char buf[64];
		_snprintf_s(buf, sizeof buf, "%s %%d", WrangleLegacyCommandName(c.GetName(), true, nullptr));
		ImGui::SliderInt("##grid_ms", &newVal, 1, 100, buf, ImGuiSliderFlags_AlwaysClamp);
		if (oldVal != newVal)
			c.SetValue(newVal);
		ImGui::SameLine();
		SptImGui::HelpMarker("%s", c.GetHelpText());
	}
	{
		const char* opts[] = {"Draw at shoot locations", "Draw at bump locations"};
		SptImGui::CvarCombo(y_spt_draw_pp_grid_type, "##grid_type", opts, ARRAYSIZE(opts));
	}

	ImGui::EndDisabled();
	if (disabled)
	{
		ImGui::TextColored(SPT_IMGUI_WARN_COLOR_YELLOW,
		                   ICON_CI_WARNING " Player must exist and have a portal gun " ICON_CI_WARNING);
	}
}

void PortalPlacement::ImGuiTextHudPortalPlacementCallback(ConVar& var)
{
	const char* opts[] = {"Disabled", "Boolean result", "String result", "Float result"};
	SptImGui::CvarCombo(var, "##hud_portal_placement", opts, ARRAYSIZE(opts));
}

#endif
