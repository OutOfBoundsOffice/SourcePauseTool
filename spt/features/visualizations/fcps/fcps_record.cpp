#include <stdafx.hpp>

#include "fcps_event.hpp"

#ifdef SPT_FCPS_ENABLED

#include "spt/features/ent_props.hpp"
#include "spt/features/tracing.hpp"
#include "spt/features/create_collide.hpp"
#include "spt/utils/interfaces.hpp"
#include "spt/utils/game_detection.hpp"
#include "thirdparty/x86.h"

#include "predictable_entity.h"
#include "collisionproperty.h"

struct FcpsEntFieldEntries
{
	FcpsEntFieldEntries() = default;
	FcpsEntFieldEntries(FcpsEntFieldEntries&) = delete;

	utils::CachedField<string_t, "CBaseEntity", "m_iClassname", true> fClassName;
	utils::CachedField<string_t, "CBaseEntity", "m_iName", true> fName;
	utils::CachedField<CBaseHandle, "CBaseEntity", "m_hMoveParent", true> fMoveParent;
	utils::CachedField<CCollisionProperty, "CBaseEntity", "m_hMovePeer", true, sizeof CBaseHandle> fCol;
	utils::CachedField<int, "CBaseEntity", "m_CollisionGroup", true> fColGroup;
	utils::CachedField<Vector, "CBaseEntity", "m_vecAbsOrigin", true> fPos;
	utils::CachedField<QAngle, "CBaseEntity", "m_angAbsRotation", true> fAng;
	utils::CachedField<int, "CBaseEntity", "m_iEFlags", true> fEFlags;

	auto AllFields()
	{
		return utils::CachedFields{
		    fClassName,
		    fName,
		    fMoveParent,
		    fCol,
		    fColGroup,
		    fPos,
		    fAng,
		    fEFlags,
		};
	}
};

static auto& FcpsEntFields()
{
	static FcpsEntFieldEntries fields;
	return fields;
}

void FcpsGetEntityOriginAngles(void* pEntity, Vector* pos, QAngle* ang)
{
	auto& fields = FcpsEntFields();
#ifdef DEBUG
	Assert(!(fields.fEFlags.GetValueOrDefault(pEntity, EFL_DIRTY_ABSTRANSFORM) & EFL_DIRTY_ABSTRANSFORM));
#endif
	if (pos)
		*pos = fields.fPos.GetValueOrDefault(pEntity, Vector{NAN});
	if (ang)
		*ang = fields.fAng.GetValueOrDefault(pEntity, QAngle{NAN, NAN, NAN});
}

struct FcpsEntRecordCache
{
	FcpsEvent& event;
	std::unordered_map<void*, fcps_ent_idx> map;

	fcps_ent_idx GetIndexOfEnt(void* vEnt)
	{
		IServerEntity* ent = (IServerEntity*)vEnt;
		if (!ent)
			return FCPS_INVALID_ENT_IDX;

		auto [it, isNew] = map.try_emplace(ent, event.detail.ents.size());
		if (!isNew)
			return it->second;
		if (event.detail.ents.size() >= FCPS_MAX_NUM_ENTS)
		{
			map.erase(it);
			return FCPS_INVALID_ENT_IDX;
		}

		auto& fields = FcpsEntFields();

		FcpsEntInfo& info = event.detail.ents.emplace_back();
		info.handle = ent->GetRefEHandle();
		info.networkClassName = fields.fClassName.GetValueOrDefault(ent).ToCStr();
		info.name = fields.fName.GetValueOrDefault(ent).ToCStr();
		FcpsGetEntityOriginAngles(ent, &info.pos, &info.ang);
		CCollisionProperty* col = fields.fCol.GetPtr(ent);
		info.obbMins = col ? col->OBBMins() : Vector{NAN};
		info.obbMaxs = col ? col->OBBMaxs() : Vector{NAN};

		if (ent->GetRefEHandle().GetEntryIndex() == 0)
			return it->second; // don't collect world phys objects (although there shouldn't be any)

		std::array<IPhysicsObject*, 1024> physObjects;
		int nPhys = spt_collideToMesh.GetPhysObjList(ent, physObjects.data(), physObjects.size());
		for (int i = 0; i < nPhys; i++)
		{
			IPhysicsObject* phys = physObjects[i];
			if (!phys)
				continue;
			FcpsEntMesh& mesh = info.meshes.emplace_back();
			mesh.ballRadius = phys->GetSphereRadius();
			phys->GetPosition(&mesh.pos, &mesh.ang);
			if (mesh.ballRadius <= 0)
			{
				spt_meshBuilder.CreateMeshContext(
				    [phys, &mesh](MeshBuilderDelegate& mb)
				    {
					    int numTris;
					    auto verts = spt_collideToMesh.CreatePhysObjMesh(phys, numTris);
					    mb.AddTris(verts.get(), numTris, {C_OUTLINE(255, 255, 255, 50)});
					    mb.DumpMbCompactMesh(mesh.physMesh);
				    });
			}
		}

		return it->second;
	}
};

// gets virtual offset for CBaseEntity::Teleport
static int FcpsGetEntTeleportVOffset()
{
	static int tpVOff = -1;
	if (tpVOff != -1)
		return tpVOff;

	// using world so we can use a known vtable
	IServerEntity* world = utils::spt_serverEntList.GetEnt(0);
	if (!world)
		return -1;

	void* modBase;
	size_t modSize;
	MemUtils::GetModuleInfo(L"server.dll", nullptr, &modBase, &modSize);

	uint8_t** vtable = *(uint8_t***)world;
	int vTableStartOff = 50, maxVtableSearch = 200;
	if (!utils::DataInModule(vtable, sizeof(uint8_t*) * maxVtableSearch, modBase, modSize))
		return -1;

	// look for the teleport function based off of the functions around it
	for (int i = vTableStartOff; i < maxVtableSearch; ++i)
	{
		{
			// void StopLoopingSounds()
			uint8_t* funcPtr = vtable[i - 2];
			if (!utils::DataInModule(funcPtr, 1, modBase, modSize))
				continue;
			if (funcPtr[0] != X86_RET)
				continue;
		}
		{
			// void NotifySystemEvent(CBaseEntity*, notify_system_event_t, notify_system_event_params_t&)
			uint8_t* funcPtr = vtable[i + 1];
			if (!utils::DataInModule(funcPtr, 3, modBase, modSize))
				continue;
			if (funcPtr[0] != X86_RETI16 || *(uint16_t*)(funcPtr + 1) != 12)
				continue;
		}
		{
			// int GetTracerAttachment()
			uint8_t* funcPtr = vtable[i + 3];
			int maxInstrSearch = 50;
			if (!utils::DataInModule(funcPtr, maxInstrSearch, modBase, modSize))
				continue;
			int len = 0;
			bool foundTest = false, foundRet = false;
			for (uint8_t* addr = funcPtr;
			     (!foundTest || !foundRet) && len != -1 && addr - funcPtr < maxInstrSearch;
			     len = x86_len(addr), addr += len)
			{
				if ((addr[0] == X86_TESTMR8 || addr[0] == X86_TESTMRW) && addr[1] == 0xc0) // test al,al
					foundTest = true;
				else if (addr[0] == X86_RET)
					foundRet = true;
			}
			if (!foundTest || !foundRet)
				continue;
		}
		return tpVOff = i;
	}

	return -1;
}

static void FcpsTeleport(IServerEntity* ent,
                         const Vector* newPosition,
                         const QAngle* newAngles,
                         const Vector* newVelocity)
{
	int vOff = FcpsGetEntTeleportVOffset();
	Assert(vOff != -1);
	using tpFunc = void(__thiscall*)(IServerEntity*, const Vector*, const QAngle*, const Vector*);
	(*(tpFunc**)ent)[vOff](ent, newPosition, newAngles, newVelocity);
}

FcpsCaller FcpsReturnAddressToFcpsCaller(uint8_t* retAddr)
{
	if (retAddr == nullptr)
		return FcpsCaller::Unknown;

	static std::unordered_map<uint8_t*, FcpsCaller> lookup;

	auto [it, isNew] = lookup.try_emplace(retAddr, FcpsCaller::Unknown);
	if (!isNew)
		return it->second;

	auto& caller = it->second;

	// was the condition to run FCPS really close?
	bool hasTestBytes = false;
	std::array<uint8_t, 2> testBytes{0x84, 0xc0};
	hasTestBytes = retAddr != std::search(retAddr - 20, retAddr, testBytes.cbegin(), testBytes.cend());

	bool hasPush0 = false;

	int len = 0;
	for (uint8_t* addr = retAddr; len != -1 && addr - retAddr < 512; len = x86_len(addr), addr += len)
	{
		// not in function anymore
		if (addr[0] == X86_INT3)
			break;

		// RET shortly after returning from FCPS
		if (addr[0] == X86_RET && addr - retAddr < 10)
			return caller = FcpsCaller::Debug_FixMyPosition;

		if (addr[0] == X86_PUSHIW)
		{
			const char* str = *(const char**)(addr + 1);
			if (!strcmp(str, "Hurting the player for FindClosestPassableSpaceFailure!"))
			{
				// checkstuck prints above message immediately, shadow update moves player closer to portal
				if (addr - retAddr < 20)
					return caller = FcpsCaller::CheckStuck;
				else
					return caller = FcpsCaller::VPhysicsShadowUpdate;
			}
			else if (!strcmp(str, "EntityPortalled"))
			{
				// string used in user message
				return caller = FcpsCaller::TeleportTouchingEntity;
			}
		}

		// PUSH 0x0 - used as an indicator for ReleaseOwnershipOfEntity(pEntity, 0)
		if (addr[0] == X86_PUSHI8 && addr[1] == 0 && addr - retAddr < 20)
			hasPush0 = true;

		// check if FCPS is called from within a loop
		if (addr[0] == X86_JNZ && (int8_t)addr[1] < 0)
		{
			// we're relying on RemoveEntityFromPortalHole being inlined for this to work
			if (hasTestBytes && hasPush0)
				return caller = FcpsCaller::RemoveEntityFromPortalHole;

			// if PortalSimulator::MoveTo() ever triggers FCPS - add the check here
		}
	}

	return caller;
}

static void FcpsGetAabb(const CCollisionProperty& col, Vector& mins, Vector& maxs)
{
	const Vector& vEntLocalMins = col.OBBMins();
	const Vector& vEntLocalMaxs = col.OBBMaxs();

	if (!col.IsBoundsDefinedInEntitySpace() || (col.GetCollisionAngles() == vec3_angle))
	{
		const Vector& origin = col.GetCollisionOrigin();
		mins = vEntLocalMins + origin;
		maxs = vEntLocalMaxs + origin;
	}
	else
	{
		TransformAABB(col.CollisionToWorldTransform(), vEntLocalMins, vEntLocalMaxs, mins, maxs);
	}
}

void FcpsImpl(FcpsEvent& event,
              IServerEntity* pEntity,
              const Vector& vIndecisivePush,
              unsigned int fMask,
              void* callerReturnAddress)
{
	event.imported = false;

	auto& params = event.params;
	params.entHandle = pEntity->GetRefEHandle();
	params.vIndecisivePush = vIndecisivePush;
	params.fMask = fMask;
	params.caller = FcpsReturnAddressToFcpsCaller((uint8_t*)callerReturnAddress);

	auto& fields = FcpsEntFields();

	bool badImpl = false;

	if (!interfaces::engine_tool || !spt_tracing.ORIG_UTIL_TraceRay_server || !interfaces::engineTraceServer)
		badImpl = true;
	else if (!fields.AllFields().HasAll())
		badImpl = true;
	else if (FcpsGetEntTeleportVOffset() == -1)
		badImpl = true;
	else if (!sv_use_find_closest_passable_space)
		badImpl = true;

	if (badImpl)
	{
		event.outcome.result = FCPS_IMPLEMENTATION_INCOMPLETE;
		return;
	}

	event.gameInfo.Populate();
	FcpsGetEntityOriginAngles(pEntity, &event.outcome.fromPos, &event.outcome.ang);
	event.outcome.toPos = event.outcome.fromPos;

	if (!sv_use_find_closest_passable_space->GetBool())
	{
		event.outcome.result = FCPS_NOT_RUN_CVAR_DISABLED;
		return;
	}
	if (fields.fMoveParent.GetPtr(pEntity)->IsValid())
	{
		event.outcome.result = FCPS_NOT_RUN_HAS_MOVE_PARENT;
		return;
	}

	FcpsEntRecordCache cache{.event = event};

	auto& detail = event.detail;
	detail.runOnEnt = cache.GetIndexOfEnt(pEntity);

	Vector vEntityMins, vEntityMaxs;
	const CCollisionProperty& pEntityCollision = *fields.fCol.GetPtr(pEntity);
	FcpsGetAabb(pEntityCollision, vEntityMins, vEntityMaxs);

	Vector ptEntityCenter = (vEntityMins + vEntityMaxs) / 2.0f;
	vEntityMins -= ptEntityCenter;
	vEntityMaxs -= ptEntityCenter;

	detail.aabbExtents = vEntityMaxs;
	detail.entCenterToOriginDiff = event.outcome.fromPos - ptEntityCenter;

	Vector ptEntityOriginalCenter = ptEntityCenter;
	ptEntityCenter.z += 0.001f;
	int iEntityCollisionGroup = *fields.fColGroup.GetPtr(pEntity);
	detail.collisionGroup = iEntityCollisionGroup;

	trace_t traces[2];
	Ray_t entRay;
	entRay.m_Extents = vEntityMaxs;
	entRay.m_IsRay = false;
	entRay.m_IsSwept = true;
	entRay.m_StartOffset = vec3_origin;

	Vector vOriginalExtents = vEntityMaxs;
	Vector vGrowSize = vEntityMaxs / 101.0f;
	vEntityMaxs -= vGrowSize;
	vEntityMins += vGrowSize;

	Ray_t testRay;
	testRay.m_Extents = vGrowSize;
	testRay.m_IsRay = false;
	testRay.m_IsSwept = true;
	testRay.m_StartOffset = vec3_origin;

	std::array<Vector, 8> ptExtents;
	std::array<float, 8> fExtentsValidation;

	for (unsigned int iFailCount = 0; iFailCount != 100; ++iFailCount)
	{
		entRay.m_Start = ptEntityCenter;
		entRay.m_Delta = ptEntityOriginalCenter - ptEntityCenter;

		spt_tracing.UTIL_TraceRay_server(entRay, fMask, pEntity, iEntityCollisionGroup, &traces[0]);
		detail.entTraces.emplace_back(entRay, traces[0], cache.GetIndexOfEnt(traces[0].m_pEnt));

		if (traces[0].startsolid == false)
		{
			Vector vNewPos = traces[0].endpos + (*fields.fPos.GetPtr(pEntity) - ptEntityOriginalCenter);
			FcpsTeleport(pEntity, &vNewPos, nullptr, nullptr);
			event.outcome.result = FCPS_SUCESS;
			FcpsGetEntityOriginAngles(pEntity, &event.outcome.toPos, nullptr);
			return;
		}

		auto& itNudge = detail.nudges.emplace_back();
		itNudge.testExtents = vEntityMaxs;
		itNudge.cornerOobBits = 0;

		bool bExtentInvalid[8];
		for (int i = 0; i != 8; ++i)
		{
			fExtentsValidation[i] = 0.0f;
			ptExtents[i] = ptEntityCenter;
			ptExtents[i].x += i & (1 << 0) ? vEntityMaxs.x : vEntityMins.x;
			ptExtents[i].y += i & (1 << 1) ? vEntityMaxs.y : vEntityMins.y;
			ptExtents[i].z += i & (1 << 2) ? vEntityMaxs.z : vEntityMins.z;
			bExtentInvalid[i] = interfaces::engineTraceServer->PointOutsideWorld(ptExtents[i]);
			itNudge.cornerOobBits |= bExtentInvalid[i] << i;
		}

		for (unsigned int counter = 0; counter != 7; ++counter)
		{
			for (unsigned int counter2 = counter + 1; counter2 != 8; ++counter2)
			{
				testRay.m_Delta = ptExtents[counter2] - ptExtents[counter];
				if (bExtentInvalid[counter])
				{
					traces[0].startsolid = true;
				}
				else
				{
					testRay.m_Start = ptExtents[counter];
					spt_tracing.UTIL_TraceRay_server(testRay,
					                                 fMask,
					                                 pEntity,
					                                 iEntityCollisionGroup,
					                                 &traces[0]);
					itNudge.testTraces.emplace_back(testRay,
					                                traces[0],
					                                cache.GetIndexOfEnt(traces[0].m_pEnt));
					itNudge.testTraceCornerBits.push_back(counter | (counter2 << 3));
				}

				if (bExtentInvalid[counter2])
				{
					traces[1].startsolid = true;
				}
				else
				{
					testRay.m_Start = ptExtents[counter2];
					testRay.m_Delta = -testRay.m_Delta;
					spt_tracing.UTIL_TraceRay_server(testRay,
					                                 fMask,
					                                 pEntity,
					                                 iEntityCollisionGroup,
					                                 &traces[1]);
					itNudge.testTraces.emplace_back(testRay,
					                                traces[1],
					                                cache.GetIndexOfEnt(traces[1].m_pEnt));
					itNudge.testTraceCornerBits.push_back(counter2 | (counter << 3));
				}

				float fDistance = testRay.m_Delta.Length();

				for (int i = 0; i != 2; ++i)
				{
					int iExtent = i == 0 ? counter : counter2;
					if (traces[i].startsolid)
						fExtentsValidation[iExtent] -= 100.0f;
					else
						fExtentsValidation[iExtent] += traces[i].fraction * fDistance;
				}
			}
		}

		Vector vNewOriginDirection = vec3_origin;
		float fTotalValidation = 0.0f;
		for (unsigned int counter = 0; counter != 8; ++counter)
		{
			if (fExtentsValidation[counter] > 0.0f)
			{
				vNewOriginDirection +=
				    (ptExtents[counter] - ptEntityCenter) * fExtentsValidation[counter];
				fTotalValidation += fExtentsValidation[counter];
			}
			itNudge.weights[counter] = fExtentsValidation[counter];
		}

		if (fTotalValidation != 0.0f)
		{
			itNudge.nudgeVec = vNewOriginDirection / fTotalValidation;
			ptEntityCenter += itNudge.nudgeVec;
			testRay.m_Extents += vGrowSize;
			vEntityMaxs -= vGrowSize;
			vEntityMins = -vEntityMaxs;
		}
		else
		{
			itNudge.nudgeVec = vIndecisivePush;
			ptEntityCenter += vIndecisivePush;
			testRay.m_Extents = vGrowSize;
			vEntityMaxs = vOriginalExtents;
			vEntityMins = -vEntityMaxs;
		}
	}

	event.outcome.result = FCPS_FAIL;
}

void FcpsEvent::GameInfo::Populate()
{
	if (!interfaces::engine_tool || !interfaces::engine_server)
		return;
	gameVersion = utils::GetBuildNumber();
	hostTick = interfaces::engine_tool->HostTick();
	serverTick = interfaces::engine_tool->ServerTick();
	mapName = interfaces::engine_tool->GetCurrentMap();
	playerName = interfaces::engine_server->GetClientConVarValue(1, "name");
}

#endif
