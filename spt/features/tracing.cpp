#include "stdafx.hpp"
#include "tracing.hpp"
#include "ent_utils.hpp"
#include "game_detection.hpp"
#include "generic.hpp"
#include "hud.hpp"
#include "math.hpp"
#include "interfaces.hpp"
#include "convar.h"
#include "string_utils.hpp"
#include "..\sptlib-wrapper.hpp"
#include "..\strafe\strafestuff.hpp"
#include "portal_utils.hpp"
#include "ent_props.hpp"
#include "visualizations/imgui/imgui_interface.hpp"
#include "spt\utils\ent_list.hpp"

#include "model_types.h"

#undef min
#undef max

ConVar y_spt_hud_oob("y_spt_hud_oob", "0", FCVAR_CHEAT, "Is the player OoB?");

Tracing spt_tracing;

namespace patterns
{
	PATTERNS(
	    UTIL_TraceRay,
	    "5135",
	    "8B 44 24 10 8B 4C 24 0C 83 EC 10 56 6A 00 50 51 8D 4C 24 10 E8 ?? ?? ?? ?? 8B 74 24 28 8B 0D ?? ?? ?? ?? 8B 11 8B 52 10",
	    "3420",
	    "8B 44 24 10 8B 4C 24 0C 83 EC 0C 56 50 51 8D 4C 24 0C E8 ?? ?? ?? ?? 8B 74 24 24 8B 0D ?? ?? ?? ?? 8B 11 8B 52 10",
	    "7462488",
	    "55 8B EC 83 EC 10 8D 4D ?? 56 FF 75 ?? FF 75 ??");
	PATTERNS(CGameMovement__TracePlayerBBox,
	         "5135",
	         "55 8B EC 83 E4 F0 83 EC 5C 56 8B F1 8B 06 8B 50 24",
	         "7122284",
	         "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B ?? 89 6C 24 ?? 8B EC 83 EC 7C 56");
	PATTERNS(
	    CPortalGameMovement__TracePlayerBBox,
	    "5135",
	    "55 8B EC 83 E4 F0 81 EC C4 00 00 00 53 56 8B F1 8B 46 ?? 83 C0 04 8B 00 83 F8 FF 57",
	    "1910503",
	    "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B ?? 89 6C 24 ?? 8B EC 81 EC E8 00 00 00 56 8B F1 8B 46 ??",
	    "7122284",
	    "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B ?? 89 6C 24 ?? 8B EC 81 EC E8 00 00 00 56 8B F1 57 8B 46 ??");
	PATTERNS(TracePlayerBBoxForGround,
	         "5135",
	         "55 8B EC 83 E4 F0 81 EC 84 00 00 00 53 56 8B 75 24 8B 46 0C D9 46 2C 8B 4E 10",
	         "1910503",
	         "55 8B EC 83 EC 3C 53 56 57 8B F9 8B 07 8B 50 ??",
	         "7122284",
	         "55 8B EC 83 EC 3C 53 56 57 8B F9 8D 4D ??");
	PATTERNS(TracePlayerBBoxForGround2,
	         "5135",
	         "55 8B EC 83 E4 F0 8B 4D 18 8B 01 8B 50 08 81 EC 84 00 00 00 53 56 57 FF D2",
	         "1910503",
	         "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B ?? 89 6C 24 ?? 8B EC 8B 4B ?? 8B 01 8B 50 ??",
	         "7122284",
	         "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B ?? 89 6C 24 ?? 8B EC 8B 4B ?? 81 EC 98 00 00 00");
	PATTERNS(
	    CGameMovement__GetPlayerMins,
	    "4104",
	    "8B 41 ?? 8B 88 ?? ?? ?? ?? C1 E9 03 F6 C1 01 8B 0D ?? ?? ?? ?? 8B 11 74 09 8B 42 ?? FF D0 83 C0 48 C3",
	    "1910503",
	    "55 8B EC 83 EC 10 8B 41 ?? 8B 88 ?? ?? ?? ?? F3 0F 10 80 ?? ?? ?? ?? C1 E9 03 F6 C1 01 8B 0D ?? ?? ?? ?? 8B 11 F3 0F 11 45 ?? 74 ?? 8B 42 ?? FF D0 8B 4D ?? F3 0F 10 45 ?? F3 0F 10 48 48",
	    "7122284",
	    "55 8B EC 83 EC 10 8B 49 ?? 8B 81 ?? ?? ?? ?? F3 0F 10 81 ?? ?? ?? ?? C1 E8 03 F3 0F 11 45 ?? A8 01 74 ?? 8B 0D ?? ?? ?? ?? 8B 01 FF 50 ?? F3 0F 10 4D ?? 8B C8 8B 45 ?? F3 0F 10 41 48");
	PATTERNS(
	    CGameMovement__GetPlayerMaxs,
	    "4104",
	    "8B 41 ?? 8B 88 ?? ?? ?? ?? C1 E9 03 F6 C1 01 8B 0D ?? ?? ?? ?? 8B 11 74 09 8B 42 ?? FF D0 83 C0 54 C3",
	    "1910503",
	    "55 8B EC 83 EC 10 8B 41 ?? 8B 88 ?? ?? ?? ?? F3 0F 10 80 ?? ?? ?? ?? C1 E9 03 F6 C1 01 8B 0D ?? ?? ?? ?? 8B 11 F3 0F 11 45 ?? 74 ?? 8B 42 ?? FF D0 8B 4D ?? F3 0F 10 45 ?? F3 0F 10 48 54",
	    "7122284",
	    "55 8B EC 83 EC 10 8B 49 ?? 8B 81 ?? ?? ?? ?? F3 0F 10 81 ?? ?? ?? ?? C1 E8 03 F3 0F 11 45 ?? A8 01 74 ?? 8B 0D ?? ?? ?? ?? 8B 01 FF 50 ?? F3 0F 10 4D ?? 8B C8 8B 45 ?? F3 0F 10 41 54");
	PATTERNS(GetActiveWeapon,
	         "5135",
	         "8B 81 d8 07 00 00 83 F8 FF 74 ?? 8B 15 ?? ?? ?? ?? 8B C8",
	         "7197370",
	         "8B 91 10 08 00 00 83 FA FF 74 ?? A1 ?? ?? ?? ?? 8B CA 81 E1 FF 0F 00 00");
	PATTERNS(TraceFirePortal,
	         "5135",
	         "55 8B EC 83 E4 F0 B8 C4 11 00 00 E8 ?? ?? ?? ?? 53 56 57",
	         "7197370",
	         "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B ?? 89 6C 24 ?? 8B EC B8 C8 11 00 00");
	PATTERNS(CM_ClipBoxToBrush_1,
	         "5135-hl2",
	         "83 EC 08 56 8B F1 0F B7 4A 04 66 81 F9 FF FF",
	         "7122284-hl2",
	         "55 8B EC 83 EC 0C B8 FF FF 00 00 56");
	PATTERNS(CM_TraceToDispTree_1,
	         "5135-hl2",
	         "55 8B EC 83 E4 F0 83 EC 54 53 56 8B F1 F3 0F 10 06 F3 0F 11 44 24 ?? F3 0F 10 46 ?? 57",
	         "7122284-hl2",
	         "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B ?? 89 6C 24 ?? 8B EC 83 EC 68 8B C1 66 C7 45 ?? 01 01");
} // namespace patterns

void Tracing::InitHooks()
{
	// reuse the same pattern for both client and server
	AddPatternHook(patterns::UTIL_TraceRay,
	               "client",
	               "UTIL_TraceRay_client",
	               reinterpret_cast<void**>(&ORIG_UTIL_TraceRay_client),
	               nullptr);
	AddPatternHook(patterns::UTIL_TraceRay,
	               "server",
	               "UTIL_TraceRay_server",
	               reinterpret_cast<void**>(&ORIG_UTIL_TraceRay_server),
	               nullptr);
	FIND_PATTERN(server, CGameMovement__TracePlayerBBox);
	FIND_PATTERN(server, TracePlayerBBoxForGround);
	FIND_PATTERN(server, TracePlayerBBoxForGround2);
	HOOK_FUNCTION(server, CGameMovement__GetPlayerMaxs);
	HOOK_FUNCTION(server, CGameMovement__GetPlayerMins);
	HOOK_FUNCTION(engine, CM_ClipBoxToBrush_1);
	HOOK_FUNCTION(engine, CM_TraceToDispTree_1);
#ifdef SPT_TRACE_PORTAL_ENABLED
	if (utils::DoesGameLookLikePortal())
	{
		FIND_PATTERN(server, CPortalGameMovement__TracePlayerBBox);
		FIND_PATTERN(server, GetActiveWeapon);
		FIND_PATTERN(server, TraceFirePortal);
	}
#endif
}

bool Tracing::CanTracePlayerBBox()
{
	if (utils::DoesGameLookLikePortal())
	{
		return interfaces::gm != nullptr && ORIG_TracePlayerBBoxForGround2
		       && ORIG_CGameMovement__TracePlayerBBox && ORIG_CGameMovement__GetPlayerMaxs
		       && ORIG_CGameMovement__GetPlayerMins;
	}
	else
	{
		return interfaces::gm != nullptr && ORIG_TracePlayerBBoxForGround && ORIG_CGameMovement__TracePlayerBBox
		       && ORIG_CGameMovement__GetPlayerMaxs && ORIG_CGameMovement__GetPlayerMins;
	}
}

void Tracing::TracePlayerBBox(const Vector& start,
                              const Vector& end,
                              const Vector& mins,
                              const Vector& maxs,
                              unsigned int fMask,
                              int collisionGroup,
                              trace_t& pm)
{
	overrideMinMax = true;
	_mins = mins;
	_maxs = maxs;

	if (utils::DoesGameLookLikePortal())
		ORIG_CPortalGameMovement__TracePlayerBBox(interfaces::gm, start, end, fMask, collisionGroup, pm);
	else
		ORIG_CGameMovement__TracePlayerBBox(interfaces::gm, start, end, fMask, collisionGroup, pm);
	overrideMinMax = false;
}

#if defined(SSDK2013)
void Tracing::TracePlayerBBoxForGround(const Vector& start,
                                       const Vector& end,
                                       const Vector& mins,
                                       const Vector& maxs,
                                       unsigned int fMask,
                                       int collisionGroup,
                                       trace_t& pm)
{
	if (utils::DoesGameLookLikePortal())
	{
		ORIG_TracePlayerBBoxForGround2(start,
		                               end,
		                               mins,
		                               maxs,
		                               utils::spt_serverEntList.GetPlayer(),
		                               fMask,
		                               collisionGroup,
		                               pm);
	}
	else
	{
		overrideMinMax = true;
		_mins = mins;
		_maxs = maxs;
		ORIG_TracePlayerBBoxForGround(interfaces::gm, start, end, fMask, collisionGroup, pm);
		overrideMinMax = false;
	}
}
#else
void Tracing::TracePlayerBBoxForGround(const Vector& start,
                                       const Vector& end,
                                       const Vector& mins,
                                       const Vector& maxs,
                                       unsigned int fMask,
                                       int collisionGroup,
                                       trace_t& pm)
{
	if (utils::DoesGameLookLikePortal())
	{
		ORIG_TracePlayerBBoxForGround2(start,
		                               end,
		                               mins,
		                               maxs,
		                               utils::spt_serverEntList.GetPlayer(),
		                               fMask,
		                               collisionGroup,
		                               pm);
	}
	else
	{
		ORIG_TracePlayerBBoxForGround(start,
		                              end,
		                              mins,
		                              maxs,
		                              utils::spt_serverEntList.GetPlayer(),
		                              fMask,
		                              collisionGroup,
		                              pm);
	}
}
#endif

WorldHitInfo Tracing::TraceLineWithWorldInfoServer(const Ray_t& ray,
                                                   unsigned int fMask,
                                                   ITraceFilter* filter,
                                                   trace_t& tr)
{
	// if we need this for non-zero extents for some reason, we can hook the <IS_POINT=false> versions
	Assert(ray.m_IsSwept && ray.m_IsRay);
	curTraceInfo.bestFrac = 1;
	auto& hitInfo = curTraceInfo.hitInfo;
	hitInfo.Clear();

	interfaces::engineTraceServer->TraceRay(ray, fMask, filter, &tr);

	if (tr.DidHit() && !tr.startsolid
	    && tr.m_pEnt == interfaces::engine_server->PEntityOfEntIndex(0)->GetIServerEntity()->GetBaseEntity())
	{
		if (tr.hitbox != 0)
		{
			// we hit a static prop
			hitInfo.Clear();
			// no clue why we need to subtract 1
			hitInfo.staticProp = interfaces::staticpropmgr->GetStaticPropByIndex(tr.hitbox - 1);
		}
	}
	else
	{
		// didn't hit the world or not enough information
		hitInfo.Clear();
	}
	return hitInfo;
}

IMPL_HOOK_FASTCALL(Tracing,
                   void,
                   CM_ClipBoxToBrush_1,
                   TraceInfo_t* __restrict pTraceInfo,
                   const cbrush_t* __restrict brush)
{
	// do trace against brush, then overwrite the hitInfo if we got something better
	spt_tracing.ORIG_CM_ClipBoxToBrush_1(pTraceInfo, brush);

	auto& curInfo = spt_tracing.curTraceInfo;
	if (pTraceInfo->m_trace.fraction < curInfo.bestFrac)
	{
		curInfo.bestFrac = pTraceInfo->m_trace.fraction;
		curInfo.hitInfo.Clear();
		curInfo.hitInfo.bspData = pTraceInfo->m_pBSPData;
		curInfo.hitInfo.brush = brush;
	}
}

IMPL_HOOK_FASTCALL(Tracing,
                   void,
                   CM_TraceToDispTree_1,
                   TraceInfo_t* pTraceInfo,
                   CDispCollTree* pDispTree,
                   float startFrac,
                   float endFrac)
{
	// do trace against displacement, then overwrite the hitInfo if we got something better
	spt_tracing.ORIG_CM_TraceToDispTree_1(pTraceInfo, pDispTree, startFrac, endFrac);

	auto& curInfo = spt_tracing.curTraceInfo;
	if (pTraceInfo->m_trace.fraction < curInfo.bestFrac)
	{
		curInfo.bestFrac = pTraceInfo->m_trace.fraction;
		curInfo.hitInfo.Clear();
		curInfo.hitInfo.bspData = pTraceInfo->m_pBSPData;
		curInfo.hitInfo.dispTree = pDispTree;
	}
}

#ifdef SPT_TRACE_PORTAL_ENABLED

CBaseCombatWeapon* Tracing::GetActiveWeapon()
{
	auto player = utils::spt_serverEntList.GetPlayer();
	if (!player)
		return nullptr;

	return ORIG_GetActiveWeapon(player);
}

float Tracing::TraceFirePortal(trace_t& tr, const Vector& startPos, const Vector& vDirection)
{
	auto weapon = GetActiveWeapon();
	if (!weapon)
	{
		tr.fraction = 1.0f;
		return 0;
	}

	const int PORTAL_PLACED_BY_PLAYER = 2;
	Vector vFinalPosition;
	QAngle qFinalAngles;

	return ORIG_TraceFirePortal(weapon,
	                            false,
	                            startPos,
	                            vDirection,
	                            tr,
	                            vFinalPosition,
	                            qFinalAngles,
	                            PORTAL_PLACED_BY_PLAYER,
	                            true);
}

float Tracing::TraceTransformFirePortal(trace_t& tr, const Vector& startPos, const QAngle& startAngles)
{
	Vector finalPos;
	QAngle finalAngles;
	return TraceTransformFirePortal(tr, startPos, startAngles, finalPos, finalAngles, false);
}

float Tracing::TraceTransformFirePortal(trace_t& tr,
                                        const Vector& startPos,
                                        const QAngle& startAngles,
                                        Vector& finalPos,
                                        QAngle& finalAngles,
                                        bool isPortal2)
{
	auto weapon = GetActiveWeapon();
	if (!weapon)
	{
		tr.fraction = 1.0f;
		return 0;
	}

	// Transform through portal
	Vector transformedPos = startPos;
	QAngle transformedAngles = startAngles;
	Vector vDirection;
	const utils::PortalInfo* env = utils::GetEnvironmentPortal();
	transformThroughPortal(env, startPos, startAngles, transformedPos, transformedAngles);
	AngleVectors(transformedAngles, &vDirection);

	const int PORTAL_PLACED_BY_PLAYER = 2;

	return ORIG_TraceFirePortal(weapon,
	                            isPortal2,
	                            transformedPos,
	                            vDirection,
	                            tr,
	                            finalPos,
	                            finalAngles,
	                            PORTAL_PLACED_BY_PLAYER,
	                            true);
}

ITraceFilter* Tracing::GetPortalTraceFilter()
{
	struct
	{
		int name;
		int solidType;
		int modelIndex;
		int renderMode;
		int moveType;
		int collisionGroup;
	} static offsets;

	static bool offsetsCached = false;

	if (!offsetsCached)
	{
		offsets.name = spt_entprops.GetFieldOffset("CBaseEntity", "m_iClassname", true);
		offsets.solidType = spt_entprops.GetFieldOffset("CBaseEntity", "m_Collision.m_nSolidType", true);
		offsets.modelIndex = spt_entprops.GetFieldOffset("CBaseEntity", "m_nModelIndex", true);
		offsets.renderMode = spt_entprops.GetFieldOffset("CBaseEntity", "m_nRenderMode", true);
		offsets.moveType = spt_entprops.GetFieldOffset("CBaseEntity", "m_MoveType", true);
		offsets.collisionGroup = spt_entprops.GetFieldOffset("CBaseEntity", "m_CollisionGroup", true);

		for (int i = 0; i < sizeof(offsets) / 4; i++)
		{
			if ((reinterpret_cast<int*>(&offsets))[i] == utils::INVALID_DATAMAP_OFFSET)
			{
				Assert(0);
				return nullptr; // should never happen, and I'm assuming all these offsets are valid below
			}
		}
		offsetsCached = true;
	}

	// if we need to make other filters from scratch we could break this apart into smaller pieces
	struct PortalGunFilter : CTraceFilter
	{
		virtual bool ShouldHitEntity(IHandleEntity* pHandle, int contentsMask)
		{
			if (!pHandle)
				return false;
			CBaseHandle eHandle = pHandle->GetRefEHandle();
			if ((eHandle.GetSerialNumber() & ~ENT_ENTRY_MASK) == 0x40000000) // static prop
				return false;
			CBaseEntity* pEnt = ((IServerUnknown*)pHandle)->GetBaseEntity();

			// not implementing CTraceFilterTranslateClones - I don't know of any case where portals hit shadow clones

			const char* ignoreList[] = {
			    "prop_physics",
			    "func_physbox",
			    "npc_portal_turret_floor",
			    "prop_energy_ball",
			    "npc_security_camera",
			    "player",
			    "simple_physics_prop",
			    "simple_physics_brush",
			    "prop_ragdoll",
			    "prop_glados_core",
			    "updateitem2",
			};

			// CTraceFilterSimpleClassnameList rules
			for (int i = 0; i < ARRAYSIZE(ignoreList); i++)
				if (!strcmp(ignoreList[i], ((string_t*)((uint)pEnt + offsets.name))->ToCStr()))
					return false;

			SolidType_t solid = *(SolidType_t*)((uint)pEnt + offsets.solidType);
			short modelIndex = *(short*)((uint)pEnt + offsets.modelIndex);
			const model_t* pModel = interfaces::modelInfo->GetModel(modelIndex);
			modtype_t modelType = (modtype_t)interfaces::modelInfo->GetModelType(pModel);
			RenderMode_t renderMode = (RenderMode_t)(*((uchar*)pEnt + offsets.renderMode));
			MoveType_t moveType = (MoveType_t)(*((uchar*)pEnt + offsets.moveType));
			Collision_Group_t collisionGroup = *(Collision_Group_t*)((uint)pEnt + offsets.collisionGroup);

			// StandardFilterRules
			if (modelType != mod_brush || (solid != SOLID_BSP && solid != SOLID_VPHYSICS))
				if (!(contentsMask & CONTENTS_MONSTER))
					return false;
			if (contentsMask & CONTENTS_WINDOW && renderMode != kRenderNormal)
				return false;
			if (!(contentsMask & CONTENTS_MOVEABLE) && moveType == MOVETYPE_PUSH)
				return false;
			// CTraceFilterSimple::ShouldHitEntity
			if (collisionGroup == COLLISION_GROUP_DEBRIS && !(contentsMask & CONTENTS_DEBRIS))
				return false;

			return true; // CGameRules::ShouldCollide always returns true with COLLISION_GROUP_NONE
		}
	} static filterInstance;

	return &filterInstance;
}

#endif

bool Tracing::ShouldLoadFeature()
{
#if defined(SSDK2007) || defined(SSDK2013)
	return true;
#else
	return false;
#endif
}

void Tracing::UnloadFeature() {}
#if defined(SSDK2013)
IMPL_HOOK_THISCALL(Tracing, void, CGameMovement__GetPlayerMins, IGameMovement*, Vector* out)
{
	if (spt_tracing.overrideMinMax)
	{
		*out = spt_tracing._mins;
	}
	else
		spt_tracing.ORIG_CGameMovement__GetPlayerMins(thisptr, out);
}

IMPL_HOOK_THISCALL(Tracing, void, CGameMovement__GetPlayerMaxs, IGameMovement*, Vector* out)
{
	if (spt_tracing.overrideMinMax)
	{
		*out = spt_tracing._maxs;
	}
	else
		spt_tracing.ORIG_CGameMovement__GetPlayerMaxs(thisptr, out);
}
#else
IMPL_HOOK_THISCALL(Tracing, const Vector&, CGameMovement__GetPlayerMins, IGameMovement*)
{
	if (spt_tracing.overrideMinMax)
	{
		return spt_tracing._mins;
	}
	else
		return spt_tracing.ORIG_CGameMovement__GetPlayerMins(thisptr);
}

IMPL_HOOK_THISCALL(Tracing, const Vector&, CGameMovement__GetPlayerMaxs, IGameMovement*)
{
	if (spt_tracing.overrideMinMax)
	{
		return spt_tracing._maxs;
	}
	else
		return spt_tracing.ORIG_CGameMovement__GetPlayerMaxs(thisptr);
}
#endif
#ifdef SPT_TRACE_PORTAL_ENABLED
double trace_fire_portal(QAngle angles, Vector& normal)
{
	trace_t tr;
	spt_tracing.TraceTransformFirePortal(tr, utils::GetPlayerEyePosition(), angles);

	if (tr.DidHit())
	{
		normal = tr.plane.normal;
	}

	return (tr.endpos - tr.startpos).LengthSqr();
}

static QAngle firstAngle;
static Vector firstPos;
static bool firstInvocation = true;

static const char find_seam_shot_help[] =
    "Usage: spt_find_seam_shot <pitch1> <yaw1> <pitch2> <yaw2> <epsilon>\n"
    "Tries to find a seam shot on a \"line\" between viewangles (pitch1; yaw1) and (pitch2; yaw2) with binary search. "
    "Decreasing epsilon will result in more viewangles checked. A default value is 0.00001. "
    "If no arguments are given, first invocation selects the first point, second invocation selects the second point and searches between them.";

CON_COMMAND(y_spt_find_seam_shot, find_seam_shot_help)
{
	auto player = utils::spt_serverEntList.GetPlayer();
	if (!player)
	{
		Msg("Cannot find server player.\n");
		return;
	}

	if (!spt_tracing.ORIG_GetActiveWeapon(player))
	{
		Msg("You need to be holding a portal gun.\n");
		return;
	}

	QAngle a, b;
	double eps = 0.00001 * 0.00001;

	if (args.ArgC() == 1)
	{
		if (firstInvocation)
		{
			interfaces::engine_client->GetViewAngles(firstAngle);
			firstPos = utils::GetPlayerEyePosition();
			firstInvocation = !firstInvocation;

			Msg("First point set.\n");
			return;
		}
		else
		{
			firstInvocation = !firstInvocation;
			if (firstPos != utils::GetPlayerEyePosition())
			{
				Msg("Don't move when finding seamshot!\n");
				return;
			}
			a = firstAngle;
			interfaces::engine_client->GetViewAngles(b);
		}
	}
	else
	{
		if (args.ArgC() != 5 && args.ArgC() != 6)
		{
			Msg("%s\n", find_seam_shot_help);
			return;
		}

		a = QAngle(atof(args.Arg(1)), atof(args.Arg(2)), 0);
		b = QAngle(atof(args.Arg(3)), atof(args.Arg(4)), 0);
		eps = (args.ArgC() == 5) ? eps : std::pow(atof(args.Arg(5)), 2);
	}

	Vector a_normal, b_normal;
	const auto distance = std::max(trace_fire_portal(a, a_normal), trace_fire_portal(b, b_normal));

	// If our trace had a distance greater than the a or b distance by this amount, treat it as a seam shot.
	constexpr double GOOD_DISTANCE_DIFFERENCE = 50.0 * 50.0;

	QAngle test;
	utils::GetMiddlePoint(a, b, test);
	double difference;

	do
	{
		Vector test_normal;

		if (trace_fire_portal(test, test_normal) - distance > GOOD_DISTANCE_DIFFERENCE)
		{
			Msg("Found a seam shot at setang %.8f %.8f 0\n", test.x, test.y);
			interfaces::engine_client->SetViewAngles(test);
			return;
		}

		if (test_normal == a_normal)
		{
			a = test;
			utils::GetMiddlePoint(a, b, test);
			difference = (test - a).LengthSqr();
		}
		else
		{
			b = test;
			utils::GetMiddlePoint(a, b, test);
			difference = (test - b).LengthSqr();
		}

		Msg("Difference: %.8f\n", std::sqrt(difference));
	} while (difference > eps);

	Msg("Could not find a seam shot. Best guess: setang %.8f %.8f 0\n", test.x, test.y);
	interfaces::engine_client->SetViewAngles(test);
}
#endif

void Tracing::LoadFeature()
{
	if (!ORIG_UTIL_TraceRay_client)
		Warning("spt_tas_strafe_version 1 not available\n");

	if (!CanTracePlayerBBox())
		Warning("spt_tas_strafe_version 2 not available\n");

#ifdef SPT_TRACE_PORTAL_ENABLED
	if (interfaces::engine_client != nullptr && utils::DoesGameLookLikePortal() && ORIG_TraceFirePortal)
	{
		InitCommand(y_spt_find_seam_shot);
	}
#endif

#ifdef SPT_HUD_ENABLED
	if (interfaces::engineTraceClient)
	{
		bool hudEnabled = AddHudCallback(
		    "oob",
		    [](std::string)
		    {
			    Vector v = spt_generic.GetCameraOrigin();
			    trace_t tr;
			    Strafe::Trace(tr, v, v + Vector(1, 1, 1));
			    int oob = interfaces::engineTraceClient->PointOutsideWorld(v) && !tr.startsolid;
			    spt_hud_feat.DrawTopHudElement(L"oob: %d", oob);
		    },
		    y_spt_hud_oob);

		if (hudEnabled)
			SptImGui::RegisterHudCvarCheckbox(y_spt_hud_oob);
	}
#endif
}
