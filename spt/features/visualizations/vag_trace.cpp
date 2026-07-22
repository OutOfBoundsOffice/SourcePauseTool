#include "stdafx.hpp"

#include "renderer\mesh_renderer.hpp"
#include "spt\utils\portal_utils.hpp"

#if defined(SPT_MESH_RENDERING_ENABLED) && defined(SPT_PORTAL_UTILS)

#include "spt\utils\game_detection.hpp"
#include "spt\utils\signals.hpp"
#include "spt\utils\interfaces.hpp"
#include "spt\utils\ent_utils.hpp"
#include "spt\features\ent_props.hpp"
#include "spt\utils\math.hpp"
#include "spt\features\playerio.hpp"
#include "imgui\imgui_interface.hpp"

ConVar y_spt_draw_vag_trace("y_spt_draw_vag_trace", "0", FCVAR_NONE, "Draw VAG teleport destination(s)");

ConVar y_spt_draw_vag_target_trace("y_spt_draw_vag_target_trace",
                                   "0",
                                   FCVAR_NONE,
                                   "Draw where to place wall/floor/ceiling portal to get to VAG target");

constexpr int SPT_PORTAL_SELECT_FLAGS = GPF_ALLOW_AUTO | GPF_ALLOW_OVERLAY | GPF_ONLY_OPEN_PORTALS;

ConVar y_spt_draw_vag_entry_portal(
    "y_spt_draw_vag_entry_portal",
    "overlay",
    FCVAR_NONE,
    "Chooses the portal for the VAG trace, this is the portal you enter. Valid options are:\n"
    "" SPT_PORTAL_SELECT_DESCRIPTION_OVERLAY_PREFIX "" SPT_PORTAL_SELECT_DESCRIPTION_AUTO_PREFIX
    "" SPT_PORTAL_SELECT_DESCRIPTION);

ConVar y_spt_draw_vag_lock_entry("y_spt_draw_vag_lock_entry",
                                 "1",
                                 FCVAR_NONE,
                                 "VAG trace type:\n"
                                 "  0 - Lock the exit portal in place\n"
                                 "  1 - Lock the entry portal in place\n"
                                 "  2 - Show the overlays for both 0 & 1");

static bool ShouldDrawForFreeEntry()
{
	return y_spt_draw_vag_lock_entry.GetInt() == 0 || y_spt_draw_vag_lock_entry.GetInt() == 2;
}

static bool ShouldDrawForFreeExit()
{
	return y_spt_draw_vag_lock_entry.GetInt() == 1 || y_spt_draw_vag_lock_entry.GetInt() == 2;
}

static matrix3x4_t tpRotateMat{{-1, 0, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, 0}}; // 180 rotation about z

#define ARROW_PARAMS (ArrowParams{6, 20.f, 1.5f})

// visualizations for vertical angle glitches
class VagTrace : public FeatureWrapper<VagTrace>
{
public:
	Vector target{0};

	struct
	{
		std::string lastPortalType;
		utils::PortalInfo entryPortal;

		matrix3x4_t entryMat, exitMat, entryInvMat, exitInvMat, entryTpMat, exitTpMat;
		Vector entryNorm, exitNorm;

		Vector vagDestination;

		/*
		* All of these are matrices that are applied to a circle whos normal is facing x+.
		* destinationSet - rotating the entry/exit portal will put the VAG destination somewhere in this set
		* exitFloorSetForTarget - if exit portal is on floor, this is where it can be so we can VAG to target
		* exitCeilingSetForTarget - if exit portal is on ceiling, this is where it can be so we can VAG to target
		*/
		matrix3x4_t cDestinationSetMat, cExitFloorSetForTargetMat, cExitCeilingSetForTargetMat;

		// if entry portal is on floor/ceiling, the set of places it can be forms a line that may or may not exist
		struct
		{
			bool exists;
			Vector v[2];
		} entryFloorSetForTarget, entryCeilingSetForTarget;

		// for each local axes(forward / left / up) these tell us how to transform an arrow facing x+
		matrix3x4_t entryNudgeTransforms[3], exitNudgeTransforms[3];

		// possible world->local transforms of entry/exit portals that allow a VAG to get to the target,
		// detailed for full drawing, simple for just local forward/up vector
		std::vector<matrix3x4_t> possibleEntryMatsDetailed, possibleExitMatsDetailed;
		std::vector<matrix3x4_t> possibleEntryMatsSimple, possibleExitMatsSimple;

	} cache;

	bool ShouldLoadFeature() override
	{
		return utils::DoesGameLookLikePortal();
	}

	void LoadFeature() override;

	/*
	* The first implementation of this was by evanlin - it was very cool.
	* The second implementation added more features, but all the code was in one function which was :(.
	* This is an attempt to keep things somewhat modular - the data calculation and drawing are done separately.
	*/

	void OnMeshRenderSignal(MeshRendererDelegate& mr);
	static void ImGuiCallback();

	void RecomputeCache();
	bool CalcFreeEntryPortalPos(const matrix3x4_t& entryRotMat, Vector& entryPosOut);
	void CalcFreeExitPortalPos(const matrix3x4_t& exitRotMat, Vector& exitPosOut);

	void DrawTrace(MeshRendererDelegate& mr);
	void DrawTargetTrace(MeshRendererDelegate& mr);
};

static VagTrace spt_vag_trace;

CON_COMMAND(y_spt_draw_vag_target_set, "Set VAG target\n")
{
	spt_vag_trace.target = spt_playerio.GetPlayerEyePos(true);
	spt_vag_trace.cache.entryPortal.Invalidate();
}

void VagTrace::LoadFeature()
{
	if (!spt_meshRenderer.signal.Works)
		return;
	spt_meshRenderer.signal.Connect(this, &VagTrace::OnMeshRenderSignal);
	InitCommand(y_spt_draw_vag_target_set);
	InitConcommandBase(y_spt_draw_vag_trace);
	InitConcommandBase(y_spt_draw_vag_target_trace);
	InitConcommandBase(y_spt_draw_vag_entry_portal);
	InitConcommandBase(y_spt_draw_vag_lock_entry);
	SptImGuiGroup::Draw_VagTrace.RegisterUserCallback(ImGuiCallback);
}

void VagTrace::OnMeshRenderSignal(MeshRendererDelegate& mr)
{
	if (!y_spt_draw_vag_trace.GetBool() && !y_spt_draw_vag_target_trace.GetBool())
		return;

	if (y_spt_draw_vag_target_trace.GetBool())
	{
		// draw the target even if we don't have valid portals
		static StaticMesh targetMesh;
		if (!targetMesh.Valid())
		{
			targetMesh = MB_STATIC({
				mb.AddBox({0, 0, 0},
				          {-15, -15, -15},
				          {15, 15, 15},
				          {0, 0, 0},
				          ShapeColor{C_OUTLINE(255, 255, 255, 40), false, true, WD_BOTH});
			});
		}
		mr.DrawMesh(targetMesh,
		            [this](const CallbackInfoIn& infoIn, CallbackInfoOut& infoOut)
		            { MatrixSetColumn(target, 3, infoOut.mat); });
	}

	const utils::PortalInfo* newPortal =
	    getPortal(y_spt_draw_vag_entry_portal.GetString(), SPT_PORTAL_SELECT_FLAGS);

	if (!newPortal)
	{
		cache.entryPortal.Invalidate();
		return;
	}
	if (cache.entryPortal != *newPortal)
	{
		cache.entryPortal = *newPortal;
		RecomputeCache();
	}

	if (y_spt_draw_vag_trace.GetBool())
		DrawTrace(mr);
	if (y_spt_draw_vag_target_trace.GetBool())
		DrawTargetTrace(mr);
}

void VagTrace::RecomputeCache()
{
	// start with world->portal & teleport matrices, we'll be using those for everything else

	AngleMatrix(cache.entryPortal.ang, cache.entryPortal.pos, cache.entryMat);
	AngleMatrix(cache.entryPortal.linkedAng, cache.entryPortal.linkedPos, cache.exitMat);
	MatrixInvert(cache.entryMat, cache.entryInvMat);
	MatrixInvert(cache.exitMat, cache.exitInvMat);
	MatrixGetColumn(cache.entryMat, 0, cache.entryNorm);
	MatrixGetColumn(cache.exitMat, 0, cache.exitNorm);

	MatrixMultiply(tpRotateMat, cache.entryInvMat, cache.entryTpMat);
	MatrixMultiply(cache.exitMat, cache.entryTpMat, cache.entryTpMat);
	MatrixInvert(cache.entryTpMat, cache.exitTpMat);

	VectorTransform(cache.entryPortal.pos, cache.exitTpMat, cache.vagDestination);

	// stuff for the trace command
	{
		/*
		* First thing to visualize: if either portal is on a floor/ceiling, where can we VAG to as we rotate them?
		* It turns that the set of all positions we can VAG to forms a circle, and if you steal mlugg's brain it
		* turns out its pretty easy to explicitly calculate its dimensions.
		*/

		{
			Vector vagDelta = cache.vagDestination - cache.entryPortal.pos;
			AngleMatrix(cache.entryPortal.ang, cache.cDestinationSetMat);
			matrix3x4_t scaleMat;
			SetScaleMatrix((vagDelta - cache.entryNorm * vagDelta.Dot(cache.entryNorm)).Length(), scaleMat);
			MatrixMultiply(scaleMat, cache.cDestinationSetMat, cache.cDestinationSetMat);
			Vector circleCenter = cache.entryPortal.pos + cache.entryNorm * vagDelta.Dot(cache.entryNorm);
			MatrixSetColumn(circleCenter, 3, cache.cDestinationSetMat);
		}

		/*
		* Second thing to visualize: if we move either portal, how will that effect where we VAG to? We couldn't
		* figure out a "nicer" way to calculate this but a "brute force" approach is pretty simple. We will simply
		* nudge the exit/entry portals one unit in their local forward/left/up directions and see how that effects
		* the output point, (luckily it does so linearly). The final result will be 3 matrices that will be used
		* to transform the arrow meshes that represent each nudge axis.
		*/

		// nudge entry portal
		for (int ax = 0; ax < 3; ax++)
		{
			// delta is a unit vector towards local forward, left, or up
			Vector delta;
			MatrixGetColumn(cache.entryMat, ax, delta);
			matrix3x4_t fakeEntryMat = cache.entryMat;
			MatrixSetColumn(cache.entryPortal.pos + delta, 3, fakeEntryMat);
			Vector pos = cache.entryPortal.pos + delta;
			utils::VectorTransform(cache.exitInvMat, pos);
			utils::VectorTransform(tpRotateMat, pos);
			utils::VectorTransform(fakeEntryMat, pos);
			// we now know where the new vag destination is (pos), setup arrow matrix
			matrix3x4_t scaleMat;
			SetScaleMatrix(pos.DistTo(cache.vagDestination), scaleMat);
			Vector dir = pos - cache.vagDestination;
			dir.NormalizeInPlace();
			VectorMatrix(dir, cache.entryNudgeTransforms[ax]);
			MatrixMultiply(scaleMat, cache.entryNudgeTransforms[ax], cache.entryNudgeTransforms[ax]);
			MatrixSetColumn(cache.vagDestination, 3, cache.entryNudgeTransforms[ax]);
		}

		// nudge exit portal
		for (int ax = 0; ax < 3; ax++)
		{
			Vector delta;
			MatrixGetColumn(cache.exitMat, ax, delta);
			matrix3x4_t fakeExitMat = cache.exitMat;
			MatrixSetColumn(cache.entryPortal.linkedPos + delta, 3, fakeExitMat);
			MatrixInvert(fakeExitMat, fakeExitMat);
			Vector pos = cache.entryPortal.pos;
			utils::VectorTransform(fakeExitMat, pos);
			utils::VectorTransform(tpRotateMat, pos);
			utils::VectorTransform(cache.entryMat, pos);

			matrix3x4_t scaleMat;
			SetScaleMatrix(pos.DistTo(cache.vagDestination), scaleMat);
			Vector dir = pos - cache.vagDestination;
			dir.NormalizeInPlace();
			VectorMatrix(dir, cache.exitNudgeTransforms[ax]);
			MatrixMultiply(scaleMat, cache.exitNudgeTransforms[ax], cache.exitNudgeTransforms[ax]);
			MatrixSetColumn(cache.vagDestination, 3, cache.exitNudgeTransforms[ax]);
		}
	}

	// stuff for the target trace command
	{
		// The question we're trying to answer here is: given a locked portal & VAG target, where can we place the
		// other portal? There are infinite possibilities, so we only consider wall, floor, & ceiling portals.

		auto addPossiblePortalMat =
		    [this](const QAngle& possibleFreeAng, std::vector<matrix3x4_t>& matList, bool freeEntry)
		{
			matrix3x4_t possibleFreeMat;
			AngleMatrix(possibleFreeAng, possibleFreeMat);
			Vector possibleFreePos;
			if (freeEntry)
			{
				if (!CalcFreeEntryPortalPos(possibleFreeMat, possibleFreePos))
					return;
			}
			else
			{
				CalcFreeExitPortalPos(possibleFreeMat, possibleFreePos);
			}
			MatrixSetColumn(possibleFreePos, 3, possibleFreeMat);
			matList.push_back(possibleFreeMat);
		};

		cache.possibleEntryMatsDetailed.clear();
		cache.possibleExitMatsDetailed.clear();
		cache.possibleEntryMatsSimple.clear();
		cache.possibleExitMatsSimple.clear();
		// assume free portal is an axis aligned wall portal
		for (int yaw = 0; yaw < 360; yaw += 90)
		{
			addPossiblePortalMat(QAngle(0, yaw, 0), cache.possibleEntryMatsDetailed, true);
			addPossiblePortalMat(QAngle(0, yaw, 0), cache.possibleExitMatsDetailed, false);
		}
		// assume free portal is floor or ceiling portal
		for (int yaw = 0; yaw < 360; yaw += 20)
		{
			addPossiblePortalMat(QAngle(90, yaw, 0), cache.possibleEntryMatsSimple, true);
			addPossiblePortalMat(QAngle(90, yaw, 0), cache.possibleExitMatsSimple, false);
			addPossiblePortalMat(QAngle(-90, yaw, 0), cache.possibleEntryMatsSimple, true);
			addPossiblePortalMat(QAngle(-90, yaw, 0), cache.possibleExitMatsSimple, false);
		}

		auto angToMat = [](const QAngle& ang)
		{
			matrix3x4_t mat;
			AngleMatrix(ang, mat);
			return mat;
		};

		/*
		* If we assume the entry portal is on the floor/ceiling, the set of all possible locations it can be at
		* forms a line. I have no idea how to explicitly calculate it. We hope to get 2 points to connect them,
		* but this may not be possible depending on the exit portal orientation. We test three evenly spaced
		* angles - if two are valid we'll use them to create a line, otherwise no such line exists.
		*/

		int numValid = 0;
		for (int yaw = 0; yaw < 360 && numValid < 2; yaw += 120)
			if (CalcFreeEntryPortalPos(angToMat({90, static_cast<float>(yaw), 0}),
			                           cache.entryCeilingSetForTarget.v[numValid]))
				numValid++;
		cache.entryCeilingSetForTarget.exists = numValid == 2;

		numValid = 0;
		for (int yaw = 0; yaw < 360 && numValid < 2; yaw += 120)
			if (CalcFreeEntryPortalPos(angToMat({-90, static_cast<float>(yaw), 0}),
			                           cache.entryFloorSetForTarget.v[numValid]))
				numValid++;
		cache.entryFloorSetForTarget.exists = numValid == 2;

		/*
		* If we assume the exit portal is on the floor/ceiling, the set of all possible locations it can be at
		* forms a circle. However, I am not smart enough to figure out how to explicitly calculate its dimensions
		* so we will use a "brute force" approach - we will calculate two opposing points on it and a different
		* third point. These three points will uniquely give us our circle.
		*/

		Vector v[3];

		CalcFreeExitPortalPos(angToMat({90, 0, 0}), v[0]);
		CalcFreeExitPortalPos(angToMat({90, 90, 0}), v[1]);
		CalcFreeExitPortalPos(angToMat({90, 180, 0}), v[2]);
		Vector circleCenter = (v[0] + v[2]) / 2.f;
		Vector circleNorm = (v[0] - circleCenter).Cross(v[1] - circleCenter);
		circleNorm.NormalizeInPlace();
		MatrixSetColumn(vec3_origin, 3, cache.cExitCeilingSetForTargetMat);
		VectorMatrix(circleNorm, cache.cExitCeilingSetForTargetMat);
		matrix3x4_t scaleMat;
		SetScaleMatrix(circleCenter.DistTo(v[0]), scaleMat);
		MatrixMultiply(scaleMat, cache.cExitCeilingSetForTargetMat, cache.cExitCeilingSetForTargetMat);
		MatrixSetColumn(circleCenter, 3, cache.cExitCeilingSetForTargetMat);

		// the other circle will be the same, but below/above the entry portal by the same amount
		cache.cExitFloorSetForTargetMat = cache.cExitCeilingSetForTargetMat;
		cache.cExitFloorSetForTargetMat[2][3] = 2 * cache.entryPortal.pos.z - circleCenter.z;
	}
}

bool VagTrace::CalcFreeEntryPortalPos(const matrix3x4_t& entryRotMat, Vector& entryPosOut)
{
	/*
	* Assume blue is our entry portal.
	*  B  - world to blue matrix
	*  R  - 180 rotation about z
	*  O  - world to orange matrix
	* [P] - the point we wish to vag (blue portal pos)
	* [V] - the VAG target
	* Everything here is known except for the position component of B. The VAG formula™ gives us:
	*    B * R * O{-1} * [P] = [V]
	* => B * R * O{-1} * [B{pos only}] = [V]
	* => B{rot only} * R * O{-1} * [B{pos only}] + [B{pos only}] = [V]
	* => (B{rot only} * R * O{-1} + I) * [B{pos only}] = [V]
	* => [B{pos only}] = (B{rot only} * R * O{-1} + I){-1} * [V]
	* => [B{pos only}] = (magicMat){-1} * [V]
	*/
	VMatrix magicMat, magicMatInv;
	magicMat.Identity();
	matrix3x4_t& magic3x4 = *const_cast<matrix3x4_t*>(&magicMat.As3x4()); // steampipe SDK moment
	magic3x4 = cache.exitInvMat;
	MatrixMultiply(tpRotateMat, magic3x4, magic3x4);
	MatrixMultiply(entryRotMat, magic3x4, magic3x4);
	for (int j = 0; j < 3; j++)
		magicMat[j][j] += 1;
	bool ret = magicMat.InverseGeneral(magicMatInv);
	if (!ret)
		return false;
	entryPosOut = magicMatInv * target;
	return true;
}

void VagTrace::CalcFreeExitPortalPos(const matrix3x4_t& exitRotMat, Vector& exitPosOut)
{
	/*
	* Use the same assumption & variables as above.
	*    B * R * O{-1} * [P] = [V]
	* => [P] = O * R * B{-1} * [V]
	* => [P] = O * [rhs]
	* => [P] = O{rot only} * [rhs] + [O{pos only}]
	* => [O{pos only}] = [P] - O{rot only} * [rhs]
	* => [O{pos only}] = [B{pos only}] - O{rot only} * [rhs]
	*/
	Vector rhs;
	VectorITransform(target, cache.entryMat, rhs);
	utils::VectorTransform(tpRotateMat, rhs);
	Vector rhsWithRotation;
	VectorTransform(rhs, exitRotMat, rhsWithRotation);
	exitPosOut = cache.entryPortal.pos - rhsWithRotation;
}

void VagTrace::DrawTrace(MeshRendererDelegate& mr)
{
	// draw lines between portals and to VAG destination
	mr.DrawMesh(spt_meshBuilder.CreateDynamicMesh(
	                [&](MeshBuilderDelegate& mb)
	                {
		                mb.AddLine(cache.entryPortal.linkedPos,
		                           cache.entryPortal.pos,
		                           {{255, 0, 255, 255}, false});
		                mb.AddLine(cache.entryPortal.pos, cache.vagDestination, {{175, 0, 175, 255}, false});
		                mb.AddSphere(cache.vagDestination, 1, 1, {C_FACE(255, 255, 255, 255), false});
	                }),
	            [](const CallbackInfoIn& infoIn, CallbackInfoOut& infoOut)
	            {
		            if (infoIn.portalRenderDepth > 0)
			            infoOut.skipRender = true;
	            });

	static StaticMesh arrowMesh, circleMesh;
	if (!arrowMesh.Valid() || !circleMesh.Valid())
	{
		arrowMesh =
		    MB_STATIC({ mb.AddArrow3D({0, 0, 0}, {1, 0, 0}, ARROW_PARAMS, {C_OUTLINE(255, 255, 255, 40)}); });
		circleMesh = MB_STATIC({ mb.AddCircle({0, 0, 0}, {0, 0, 0}, 1, 500, {C_WIRE(255, 255, 255, 255)}); });
	}

	auto drawNudgeArrows = [&](const color32 colors[3], int ax, bool freeEntry)
	{
		// draw local portal axes
		mr.DrawMesh(
		    arrowMesh,
		    [=](const CallbackInfoIn& infoIn, CallbackInfoOut& infoOut)
		    {
			    // don't show through portals
			    if (infoIn.portalRenderDepth > 0)
			    {
				    infoOut.skipRender = true;
				    return;
			    }
			    infoOut.colorModulate = colors[ax];
			    Vector localDir, portalNorm, portalPos;
			    matrix3x4_t& portalMat = freeEntry ? cache.entryMat : cache.exitMat;
			    MatrixGetColumn(portalMat, 0, portalNorm);
			    MatrixGetColumn(portalMat, 3, portalPos);
			    MatrixGetColumn(portalMat, ax, localDir);
			    // GAME BAD!! VectorMatrix doesn't work if the vector is extremely close (within float
			    // rounding) to up/down, but floor/ceiling portals may have very small x/y components.
			    if (abs(abs(localDir.z) - 1) < 0.01)
				    localDir.x = localDir.y = 0;
			    VectorMatrix(localDir, infoOut.mat);
			    MatrixSetColumn(portalPos + portalNorm * 3.5, 3, infoOut.mat);
		    });
		// draw respective nudge at vag destination
		mr.DrawMesh(arrowMesh,
		            [=](const CallbackInfoIn& infoIn, CallbackInfoOut& infoOut)
		            {
			            // don't show through portals
			            if (infoIn.portalRenderDepth > 0)
			            {
				            infoOut.skipRender = true;
				            return;
			            }
			            infoOut.colorModulate = colors[ax];
			            infoOut.mat =
			                freeEntry ? cache.entryNudgeTransforms[ax] : cache.exitNudgeTransforms[ax];
		            });
	};

	for (int ax = 0; ax < 3; ax++)
	{
		if (ShouldDrawForFreeEntry())
		{
			static color32 colors[] = {{0, 255, 255, 255}, {255, 0, 255, 255}, {255, 255, 0, 255}};
			drawNudgeArrows(colors, ax, true);
		}
		if (ShouldDrawForFreeExit())
		{
			static color32 colors[] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}};
			drawNudgeArrows(colors, ax, false);
		}
	}

	// draw the set of all possible VAG destinations that can be achieved by rotating entry/exit portal
	bool floorOrCeilingEntry = abs(abs(cache.entryPortal.ang.x) - 90) < 0.04;
	bool floorOrCeilingExit = abs(abs(cache.entryPortal.linkedAng.x) - 90) < 0.04;
	if ((floorOrCeilingEntry && ShouldDrawForFreeEntry()) || (floorOrCeilingExit && ShouldDrawForFreeExit()))
	{
		mr.DrawMesh(circleMesh,
		            [this](const CallbackInfoIn& infoIn, CallbackInfoOut& infoOut)
		            { infoOut.mat = cache.cDestinationSetMat; });
	}
}

void VagTrace::DrawTargetTrace(MeshRendererDelegate& mr)
{
	static StaticMesh portalMeshDetailed, portalMeshSimple, circleMesh;
	if (!StaticMesh::AllValidV(portalMeshDetailed, portalMeshSimple, circleMesh))
	{
		// box representing a portal w/ arrow towards local forward
		portalMeshDetailed = MB_STATIC({
			const Vector portalMaxs(1, 32, 54);
			ShapeColor boxColor(C_OUTLINE(255, 255, 255, 40), false);
			mb.AddBox({0, 0, 0}, -portalMaxs, portalMaxs, vec3_angle, boxColor);
			mb.AddArrow3D({1, 0, 0}, {2, 0, 0}, ARROW_PARAMS, {C_WIRE(255, 255, 255, 255), false});
		});
		// an arrow for local forward & local up
		portalMeshSimple = MB_STATIC({
			mb.AddArrow3D({0, 0, 0}, {1, 0, 0}, ARROW_PARAMS, {C_WIRE(255, 255, 255, 255)});
			mb.AddArrow3D({0, 0, 0}, {0, 0, 1}, ARROW_PARAMS, {C_WIRE(255, 255, 255, 255)});
		});
		circleMesh = MB_STATIC({ mb.AddCircle({0, 0, 0}, {0, 0, 0}, 1, 500, {C_WIRE(255, 255, 255, 255)}); });
	}

	const color32 orangeColor{255, 100, 0, 255};
	const color32 blueColor{25, 25, 255, 255};

	color32 entryColor = cache.entryPortal.isOrange ? orangeColor : blueColor;
	color32 exitColor = cache.entryPortal.isOrange ? blueColor : orangeColor;

	// draw detailed & simple portals

	auto drawPortals = [&mr](const std::vector<matrix3x4_t>& detailedMats,
	                         const std::vector<matrix3x4_t>& simpleMats,
	                         color32 modColor)
	{
		const std::vector<matrix3x4_t>* possibleEntryMatLists[] = {&detailedMats, &simpleMats};
		StaticMesh* meshes[] = {&portalMeshDetailed, &portalMeshSimple};
		for (int i = 0; i < 2; i++)
		{
			for (auto& entryMat : *possibleEntryMatLists[i])
			{
				mr.DrawMesh(*meshes[i],
				            [=](const CallbackInfoIn& infoIn, CallbackInfoOut& infoOut)
				            {
					            infoOut.mat = entryMat;
					            infoOut.colorModulate = modColor;
					            // don't show detailed portals through real portals
					            if (i == 0 && infoIn.portalRenderDepth > 0)
						            infoOut.skipRender = true;
				            });
			}
		}
	};

	if (ShouldDrawForFreeEntry())
	{
		drawPortals(cache.possibleEntryMatsDetailed, cache.possibleEntryMatsSimple, entryColor);

		auto drawSetLines = [&](const Vector v[2])
		{
			RENDER_DYNAMIC(mr, {
				Vector center = (v[0] + v[1]) / 2.f;
				// shoot the end points into the void
				Vector v0 = center + (v[0] - center) * 100;
				Vector v1 = center + (v[1] - center) * 100;
				mb.AddLine(v0, v1, entryColor);
			});
		};

		if (cache.entryCeilingSetForTarget.exists)
			drawSetLines(cache.entryCeilingSetForTarget.v);
		if (cache.entryFloorSetForTarget.exists)
			drawSetLines(cache.entryFloorSetForTarget.v);
	}

	if (ShouldDrawForFreeExit())
	{
		drawPortals(cache.possibleExitMatsDetailed, cache.possibleExitMatsSimple, exitColor);

		matrix3x4_t* circleMats[] = {&cache.cExitCeilingSetForTargetMat, &cache.cExitFloorSetForTargetMat};
		for (int i = 0; i < 2; i++)
		{
			mr.DrawMesh(circleMesh,
			            [=](const CallbackInfoIn& infoIn, CallbackInfoOut& infoOut)
			            {
				            infoOut.mat = *circleMats[i];
				            infoOut.colorModulate = exitColor;
			            });
		}
	}
}

void VagTrace::ImGuiCallback()
{
	ImGui::BeginDisabled(!SptImGui::CvarCheckbox(y_spt_draw_vag_trace, "Draw VAG trace"));

	ImGui::BeginDisabled(!SptImGui::CvarCheckbox(y_spt_draw_vag_target_trace, "Draw VAG target trace"));
	auto& targetSetCmd = y_spt_draw_vag_target_set_command;
	const char* targetSetCmdName = targetSetCmd.GetName();
	if (SptImGui::CmdButton("Set target", targetSetCmd))
		targetSetCmd.Dispatch(CCommand{1, &targetSetCmdName});
	ImGui::EndDisabled();

	std::array<const char*, 3> opts{
	    "Lock exit portal",
	    "Lock entry portal",
	    "Show visualization for both",
	};
	SptImGui::CvarCombo(y_spt_draw_vag_lock_entry, "Lock portals in place", opts.data(), opts.size());
	static SptImGui::PortalSelectionPersist persist;
	SptImGui::PortalSelectionWidgetCvar(y_spt_draw_vag_entry_portal, persist, SPT_PORTAL_SELECT_FLAGS);

	ImGui::EndDisabled();
}

#endif
