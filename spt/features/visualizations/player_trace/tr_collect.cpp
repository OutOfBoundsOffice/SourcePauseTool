#include "stdafx.hpp"

#include "tr_record_cache.hpp"

#ifdef SPT_PLAYER_TRACE_ENABLED

#include "spt/utils/interfaces.hpp"
#include "spt/utils/map_utils.hpp"
#include "spt/utils/ent_list.hpp"
#include "spt/utils/game_detection.hpp"
#include "spt/utils/file.hpp"
#include "spt/features/playerio.hpp"
#include "spt/features/ent_props.hpp"
#include "spt/utils/portal_utils.hpp"
#include "spt/features/create_collide.hpp"
#include "spt/features/property_getter.hpp"

#include "PlayerState.h"
#include "vphysics/friction.h"

using namespace player_trace;

void TrPlayerTrace::StartRecording()
{
	Clear();

	firstRecordedInfo.gameName = utils::GetGameName();
	if (interfaces::engine_server)
	{
		try
		{
			firstRecordedInfo.gameModName =
			    std::filesystem::path{GetGameDir()}.filename().string();
		}
		catch (...)
		{
		}
		firstRecordedInfo.playerName = interfaces::engine_server->GetClientConVarValue(1, "name");
	}
	firstRecordedInfo.gameVersion = utils::GetBuildNumber();

	hasStartRecordingBeenCalled = true;

	// set all import version to most up-to-date version
	std::apply(
	    [](auto&... h)
	    { ((h.firstExportVersion = TR_LUMP_VERSION(typename std::remove_reference_t<decltype(h)>::type)), ...); },
	    versions);

	TrReadContextScope scope{*this};
	auto& rc = *(recordingCache = std::make_unique<TrRecordingCache>());
	rc.StartRecording();

	// hardcoding player hulls for now...
	playerStandBboxIdx = rc.GetCachedIdx(TrAbsBox{
	    rc.GetCachedIdx(Vector{-16, -16, 0}),
	    rc.GetCachedIdx(Vector{16, 16, 72}),
	});
	playerDuckBboxIdx = rc.GetCachedIdx(TrAbsBox{
	    rc.GetCachedIdx(Vector{-16, -16, 0}),
	    rc.GetCachedIdx(Vector{16, 16, 36}),
	});
}

void TrPlayerTrace::StopRecording()
{
	TrReadContextScope scope{*this};
	recordingCache->StopRecording();
	recordingCache.reset();
	std::apply([](auto&... vecs) { (vecs.shrink_to_fit(), ...); }, storage);
}

void TrPlayerTrace::HostTickCollect(bool simulated, TrSegmentReason segmentReason, float entCollectRadius)
{
	AssertMsg(recordingCache.get(), "SPT: this trace is not being recorded to!");
	if (!recordingCache)
		return;

	if (!utils::spt_serverEntList.Valid())
		return;

	TrReadContextScope scope{*this};

	const char* curLoadedMap = utils::GetLoadedMap();
	if (!curLoadedMap || !curLoadedMap[0])
		return;

	if (Get<TrMapTransition>().empty() || strcmp(*Get<TrMapTransition>().back().toMapIdx->nameIdx, curLoadedMap))
	{
		segmentReason = TR_SR_MAP_TRANSITION;
		CollectMapTransition();
	}
	if (Get<TrSegmentStart>().empty())
		segmentReason = TR_SR_TRACE_START;
	if (segmentReason != TR_SR_NONE)
		Get<TrSegmentStart>().emplace_back(numRecordedTicks, segmentReason);

	CollectServerState(simulated);
	CollectPlayerData();
	CollectTraceState(entCollectRadius); // collect after player data
	CollectPortals();
	CollectEntities(entCollectRadius); // collect after trace state & after portals
	numRecordedTicks++;
}

void TrPlayerTrace::CollectServerState(bool hostTickSimulating)
{
	int newServerTick = interfaces::engine_tool->ServerTick();
	bool newPausedState = interfaces::engine_tool->IsGamePaused();
	auto& vec = Get<TrServerState>();

	if (vec.empty() || newPausedState != vec.back().paused
	    || newServerTick != vec.back().GetServerTickFromThisAsLastState(numRecordedTicks))
	{
		vec.emplace_back(numRecordedTicks, newServerTick, newPausedState, hostTickSimulating);
	}
}

void TrPlayerTrace::CollectPlayerData()
{
	auto& rc = GetRecordingCache();

	TrPlayerData data{numRecordedTicks};

	Vector qPos;
	bool qPhysPosValid = false;

	auto serverPlayer = utils::spt_serverEntList.GetPlayer();

	if (serverPlayer)
	{
		// qphys transform

		if (spt_playerio.m_vecAbsOrigin.ServerOffsetFound())
		{
			data.qPosIdx = rc.GetCachedIdx(qPos = spt_playerio.m_vecAbsOrigin.GetValue());
			qPhysPosValid = true;
		}

		if (spt_playerio.m_vecAbsVelocity.ServerOffsetFound())
			data.qVelIdx = rc.GetCachedIdx(spt_playerio.m_vecAbsVelocity.GetValue());

		// eyes

		static utils::CachedField<QAngle, "CBasePlayer", "pl.v_angle", true> fVangle;

		if (spt_playerio.m_vecViewOffset.ServerOffsetFound() && fVangle.Exists() && qPhysPosValid)
		{
			Vector eyePos = qPos + spt_playerio.m_vecViewOffset.GetValue();
			QAngle eyeAng = *fVangle.GetPtr(serverPlayer);

			data.transEyesIdx = rc.GetCachedIdx(TrTransform{
			    rc.GetCachedIdx(eyePos),
			    rc.GetCachedIdx(eyeAng),
			});
#ifdef SPT_PORTAL_UTILS
			transformThroughPortal(utils::GetEnvironmentPortal(), eyePos, eyeAng, eyePos, eyeAng);
#endif
			data.transSgEyesIdx = rc.GetCachedIdx(TrTransform{
			    rc.GetCachedIdx(eyePos),
			    rc.GetCachedIdx(eyeAng),
			});
		}

		// vphys transform

		IPhysicsObject* playerPhysObj = spt_collideToMesh.GetPhysObj(serverPlayer);
		if (playerPhysObj)
		{
			Vector vPos, vVel;
			QAngle vAng;
			playerPhysObj->GetPosition(&vPos, &vAng);
			data.transVPhysIdx = rc.GetCachedIdx(TrTransform{
			    rc.GetCachedIdx(vPos),
			    rc.GetCachedIdx(vAng),
			});
			playerPhysObj->GetVelocity(&vVel, nullptr);
			data.vVelIdx = rc.GetCachedIdx(vVel);

			// contact points

			static std::vector<TrIdx<TrPlayerContactPoint>> playerContactPoints;
			playerContactPoints.clear();

			IPhysicsFrictionSnapshot* snap = playerPhysObj->CreateFrictionSnapshot();
			for (; snap->IsValid(); snap->NextFrictionData())
			{
				Vector pos, norm;
				snap->GetContactPoint(pos);
				snap->GetSurfaceNormal(norm);
				IPhysicsObject* obj0 = snap->GetObject(0);
				IPhysicsObject* obj1 = snap->GetObject(1);

				playerContactPoints.push_back(rc.GetCachedIdx(TrPlayerContactPoint{
				    .posIdx = rc.GetCachedIdx(pos),
				    .normIdx = rc.GetCachedIdx(norm),
				    .force = snap->GetNormalForce(),
				    .objNameIdx =
				        rc.GetStringIdx(obj0 == playerPhysObj ? obj1->GetName() : obj0->GetName()),
				    .playerIsObj0 = obj0 == playerPhysObj,
				}));
			}
			playerPhysObj->DestroyFrictionSnapshot(snap);
			data.contactPtsSp = rc.GetCachedSpan(playerContactPoints);
		}

		// flags

		static utils::CachedField<int, "CBaseEntity", "m_fFlags", true> fFlags;
		static utils::CachedField<int, "CBaseEntity", "m_iHealth", true> fHealth;
		static utils::CachedField<char, "CBaseEntity", "m_lifeState", true> fLifeState;
		static utils::CachedField<int, "CBaseEntity", "m_CollisionGroup", true> fColGroup;
		static utils::CachedField<unsigned char, "CBaseEntity", "m_MoveType", true> fMoveType;

		data.m_fFlags = fFlags.GetValueOrDefault(serverPlayer);
		data.fov = spt_propertyGetter.GetProperty<int>(1, "m_iFOV");
		if (data.fov == 0)
			data.fov = spt_propertyGetter.GetProperty<int>(1, "m_iDefaultFOV");
		data.m_iHealth = fHealth.GetValueOrDefault(serverPlayer, -1);
		data.m_lifeState = fLifeState.GetValueOrDefault(serverPlayer);
		data.m_CollisionGroup = fColGroup.GetValueOrDefault(serverPlayer);
		data.m_MoveType = fMoveType.GetValueOrDefault(serverPlayer);

		auto envPortal = utils::spt_serverEntList.GetEnvironmentPortal();
		if (envPortal)
			data.envPortalHandle = envPortal->handle;
	}

	auto& vec = Get<TrPlayerData>();
	if (vec.empty() || !MemSameExceptTick(vec.back(), data))
		vec.push_back(data);
}

void TrPlayerTrace::CollectPortals()
{
	auto& rc = GetRecordingCache();
	rc.simToPortalMap.clear();
	static std::vector<TrIdx<TrPortal>> portalSnapshot;
	portalSnapshot.clear();

	// CProp_Portal::simulator
	static utils::CachedField<CPortalSimulator, "CProp_Portal", "m_vPortalCorners", true, sizeof(Vector[4])> fSim;

	for (auto& p : utils::spt_serverEntList.GetPortalList())
	{
		auto portalIdx = rc.GetCachedIdx(TrPortal{
		    .handle = p.handle,
		    .linkedHandle = p.linkedHandle,
		    .transIdx = rc.GetCachedIdx(TrTransform{
		        rc.GetCachedIdx(p.pos),
		        rc.GetCachedIdx(p.ang),
		    }),
		    .linkageId = p.linkageId,
		    .isOrange = p.isOrange,
		    .isOpen = p.isOpen,
		    .isActivated = p.isActivated,
		});
		portalSnapshot.push_back(portalIdx);

		CPortalSimulator* pSim = fSim.GetPtr(p.pEnt);
		if (pSim)
			rc.simToPortalMap.emplace(pSim, portalIdx);
	}

	TrPortalSnapshot newSnapshot{
	    .tick = numRecordedTicks,
	    .portalsSp = rc.GetCachedSpan(portalSnapshot),
	};

	auto& vec = Get<TrPortalSnapshot>();
	if (vec.empty() || !MemSameExceptTick(vec.back(), newSnapshot))
		vec.push_back(newSnapshot);
}

void TrPlayerTrace::CollectEntities(float entCollectRadius)
{
	auto& rc = GetRecordingCache();

	Assert(!Get<TrPlayerData>().empty());
	auto& playerDataVec = Get<TrPlayerData>();
	if (!interfaces::spatialPartition || !interfaces::staticpropmgr || entCollectRadius < 0
	    || !playerDataVec.back().qPosIdx.IsValid())
	{
		return;
	}

	const Vector& lastPlayerPos = **playerDataVec.back().qPosIdx;
	if (!lastPlayerPos.IsValid())
		return;

	static std::vector<TrRecordingCache::EntSnapshotEntry> newSnapshot;
	newSnapshot.clear();

	static utils::CachedField<unsigned char, "CBaseEntity", "m_Collision.m_nSolidType", true> fSolidType;
	static utils::CachedField<unsigned short, "CBaseEntity", "m_Collision.m_usSolidFlags", true> fSolidFlags;
	static utils::CachedField<int, "CBaseEntity", "m_CollisionGroup", true> fColGroup;
	static utils::CachedField<string_t, "CBaseEntity", "m_iName", true> fName;
	static utils::CachedField<string_t, "CBaseEntity", "m_iClassname", true> fClassName;
	// CPSCollisionEntity::m_pOwningSimulator
	constexpr int simOff = sizeof(Vector) + sizeof(int) + sizeof(CBaseHandle) + sizeof(int);
	static utils::CachedField<CPortalSimulator*, "CBaseEntity", "m_vecViewOffset", true, simOff> fSim;

	struct EntCollector : IPartitionEnumerator
	{
		TrPlayerTrace& tr;
		TrRecordingCache& rc;
		bool enumeratingPortalSims = false;

		EntCollector(TrPlayerTrace& tr, TrRecordingCache& rc) : tr{tr}, rc{rc} {}

		virtual IterationRetval_t EnumElement(IHandleEntity* pHandleEntity)
		{
			if (interfaces::staticpropmgr->IsStaticProp(pHandleEntity))
				return ITERATION_CONTINUE;
			IServerEntity* serverEnt = static_cast<IServerEntity*>(pHandleEntity);

			CBaseHandle handle = serverEnt->GetRefEHandle();
			if (handle.GetEntryIndex() <= 1)
				return ITERATION_CONTINUE;
			ICollideable* coll = serverEnt->GetCollideable();
			if (!coll)
				return ITERATION_CONTINUE;

			const char* className = fClassName.GetValueOrDefault(serverEnt).ToCStr();
			if (!strcmp(className, "prop_portal"))
				return ITERATION_CONTINUE; // portals are recorded separately
			if (!enumeratingPortalSims && !strcmp(className, "portalsimulator_collisionentity"))
				return ITERATION_CONTINUE; // we'll do these in a second pass

			// casually use 12kb of stack space
			constexpr int MAX_PHYS_OBJECTS = 1024;
			std::array<IPhysicsObject*, MAX_PHYS_OBJECTS> physObjects;
			std::array<TrIdx<TrTransform>, MAX_PHYS_OBJECTS> physTransforms;
			std::array<TrIdx<TrPhysicsObject>, MAX_PHYS_OBJECTS> physObjSp;

			int nPhysObjects =
			    spt_collideToMesh.GetPhysObjList(serverEnt, physObjects.data(), physObjects.size());

			TrPhysicsObjectInfo::SourceEntity::Id physInfoId{.pServerEnt = serverEnt};
			if (enumeratingPortalSims)
			{
				/*
				* Special case for portalsimulator_collisionentity. Using a IServerEntity*,
				* IPhysicsObject*, and a CBaseHandle as a key for its mesh is not good enough! This
				* is because the mesh changes as the portal OR the linked portal moves around.
				* Also, the OBB and pos/ang is all ZERO according to the ICollideable. Instead, we
				* have to use the portal TrIdx AND linked portal TrIdx as the key (because those
				* will be different every time the portals move around).
				*/
				CPortalSimulator* sim = fSim.GetValueOrDefault(serverEnt);
				auto it = rc.simToPortalMap.find(sim);
				if (it != rc.simToPortalMap.cend())
				{
					physInfoId.portalIdxIfThisIsCollisionEntity.primaryPortal = it->second;
					// CPortalSimulator::m_pLinkedPortal
					CPortalSimulator* linkedSim = ((CPortalSimulator**)sim)[1];
					it = rc.simToPortalMap.find(linkedSim);
					physInfoId.portalIdxIfThisIsCollisionEntity.linkedPortal =
					    it == rc.simToPortalMap.cend() ? TrIdx<TrPortal>{} : it->second;
				}
			}

			size_t nNonNullPhysObjects = 0;
			for (auto physObj : std::span<IPhysicsObject*>{&physObjects.front(), (size_t)nPhysObjects})
			{
				if (!physObj)
					continue;

				// check if this is a new physics object
				TrPhysicsObjectInfo newPhysInfo{
				    .sourceEntity{
				        .extraId = physInfoId,
				        .handle = handle,
				    },
				    .extraId{.pPhysicsObject = physObj},
				    .nameIdx = rc.GetStringIdx(physObj->GetName()),
				    .flags = (physObj->IsAsleep() ? TR_POF_ASLEEP : 0)
				             | (physObj->IsMoveable() ? TR_POF_MEOVEABLE : 0)
				             | (physObj->IsTrigger() ? TR_POF_IS_TRIGGER : 0)
				             | (physObj->IsGravityEnabled() ? TR_POF_GRAVITY_ENABLED : 0),
				};

				auto [it, new_elem] = rc.entMeshMap.try_emplace(newPhysInfo, TrIdx<TrPhysMesh>{});

				if (new_elem)
				{
					// set the pointer just like how the recording cache does
					it->first.idx = tr.Get<TrPhysicsObjectInfo>().size();
					tr.Get<TrPhysicsObjectInfo>().push_back(newPhysInfo);

					TrPhysMesh newTrMesh{
					    .ballRadius = physObj->GetSphereRadius(),
					    .vertIdxSp{},
					};

					if (newTrMesh.ballRadius <= 0)
					{
						// new object, so possibly a new mesh
						static std::vector<TrIdx<Vector>> ptIdxVec;
						ptIdxVec.clear();
						int nTris;
						auto pts = spt_collideToMesh.CreatePhysObjMesh(physObj, nTris);
						for (int i = 0; i < nTris * 3; i++)
							ptIdxVec.push_back(rc.GetCachedIdx(pts.get()[i]));
						newTrMesh.vertIdxSp = rc.GetCachedSpan(ptIdxVec);
					}

					it->second = rc.GetCachedIdx(newTrMesh);
				}

				Assert(it->first.idx.IsValid());
				physObjSp[nNonNullPhysObjects] = rc.GetCachedIdx(TrPhysicsObject{
				    .infoIdx = it->first.idx,
				    .meshIdx = it->second,
				});

				Vector pos;
				QAngle ang;
				physObj->GetPosition(&pos, &ang);
				physTransforms[nNonNullPhysObjects] = rc.GetCachedIdx(TrTransform{
				    rc.GetCachedIdx(pos),
				    rc.GetCachedIdx(ang),
				});

				nNonNullPhysObjects++;
			}

			TrEnt newEnt{
			    .extraId{.pServerEnt = serverEnt},
			    .handle = handle,
			    .networkClassNameIdx =
			        rc.GetStringIdx(utils::SptEntListServer::NetworkClassName(serverEnt)),
			    .classNameIdx = rc.GetStringIdx(className),
			    .nameIdx = rc.GetStringIdx(fName.GetValueOrDefault(serverEnt).ToCStr()),
			    .physSp = rc.GetCachedSpan(std::span<const TrIdx<TrPhysicsObject>>{
			        &physObjSp.front(),
			        nNonNullPhysObjects,
			    }),
			    .m_nSolidType = fSolidType.GetValueOrDefault(serverEnt),
			    .m_usSolidFlags = fSolidFlags.GetValueOrDefault(serverEnt),
			    .m_CollisionGroup = (uint32_t)fColGroup.GetValueOrDefault(serverEnt),
			};

			TrEntTransform newEntTrans{
			    .obbIdx = rc.GetCachedIdx(TrAbsBox{
			        .minsIdx = rc.GetCachedIdx(coll->OBBMins()),
			        .maxsIdx = rc.GetCachedIdx(coll->OBBMaxs()),
			    }),
			    .obbTransIdx = rc.GetCachedIdx(TrTransform{
			        rc.GetCachedIdx(coll->GetCollisionOrigin()),
			        rc.GetCachedIdx(coll->GetCollisionAngles()),
			    }),
			    .physTransSp = rc.GetCachedSpan(std::span<const TrIdx<TrTransform>>{
			        &physTransforms.front(),
			        nNonNullPhysObjects,
			    }),
			};

			newSnapshot.emplace_back(rc.GetCachedIdx(newEnt), rc.GetCachedIdx(newEntTrans));
			return ITERATION_CONTINUE;
		}
	} enumerator{*this, rc};

	Assert(!Get<TrTraceState>().empty());
	TrAbsBox entCollectBbox = **Get<TrTraceState>().back().entCollectBboxAroundPlayerIdx;
	interfaces::spatialPartition->EnumerateElementsInBox(PARTITION_SERVER_GAME_EDICTS,
	                                                     lastPlayerPos + **entCollectBbox.minsIdx,
	                                                     lastPlayerPos + **entCollectBbox.maxsIdx,
	                                                     false,
	                                                     &enumerator);

	/*
	* Enumerate ALL portal collision entities separately. This is because these entities don't get
	* picked up by EnumerateElementsInBox unless the collect AABB contains the origin (because the
	* entity position is at <0,0,0> according to the ICollideable.
	*/
	enumerator.enumeratingPortalSims = true;
	for (auto serverEnt : utils::spt_serverEntList.GetEntList())
		if (!strcmp(fClassName.GetValueOrDefault(serverEnt).ToCStr(), "portalsimulator_collisionentity"))
			if (enumerator.EnumElement(serverEnt) == ITERATION_STOP)
				break;

	std::ranges::sort(newSnapshot);

	auto& snapshots = Get<TrEntSnapshot>();

	if (!snapshots.empty() && rc.entSnapshot == newSnapshot)
		return; // nothing new

	rc.CollectEntityDelta(newSnapshot);
	rc.entSnapshot.swap(newSnapshot);
	if (snapshots.empty() || rc.nEntDeltasWithoutSnapshot >= MAX_DELTAS_WITHOUT_SNAPSHOT)
		rc.CollectEntitySnapshot();
}

void TrPlayerTrace::CollectTraceState(float entCollectRadius)
{
	auto& rc = GetRecordingCache();
	Assert(!Get<TrPlayerData>().empty());

	TrIdx<TrAbsBox> playerBboxIdx =
	    (Get<TrPlayerData>().back().m_fFlags & FL_DUCKING) ? playerDuckBboxIdx : playerStandBboxIdx;

	TrTraceState newTraceState{
	    .tick = numRecordedTicks,
	    .entCollectBboxAroundPlayerIdx = rc.GetCachedIdx(TrAbsBox{
	        rc.GetCachedIdx(**playerBboxIdx->minsIdx - Vector{entCollectRadius}),
	        rc.GetCachedIdx(**playerBboxIdx->maxsIdx + Vector{entCollectRadius}),
	    }),
	};

	auto& vec = Get<TrTraceState>();
	if (vec.empty() || !MemSameExceptTick(vec.back(), newTraceState))
		vec.push_back(newTraceState);
}

void TrPlayerTrace::CollectMapTransition()
{
	auto& rc = GetRecordingCache();

	auto& maps = Get<TrMap>();
	auto& landmarks = Get<TrLandmark>();
	auto& transitions = Get<TrMapTransition>();

	const char* loadedMapName = utils::GetLoadedMap();
	TrIdx<TrMap> toMapIdx = 0;
	for (; toMapIdx < maps.size(); toMapIdx++)
		if (!strcmp(*toMapIdx->nameIdx, loadedMapName))
			break;

	if (!toMapIdx.IsValid())
	{
		static std::vector<TrLandmark> newLandmarks;
		newLandmarks.clear();
		for (auto& [name, pos] : utils::GetLandmarksInLoadedMap())
			newLandmarks.emplace_back(rc.GetStringIdx(name), rc.GetCachedIdx(pos));

		if (!transitions.empty())
		{
			rc.landmarkDeltaToFirstMap +=
			    GetAdjacentLandmarkDelta(*transitions.back().toMapIdx->landmarkSp, newLandmarks);
		}
		maps.push_back(TrMap{
		    .nameIdx = rc.GetStringIdx(loadedMapName),
		    .landmarkDeltaToFirstMapIdx = rc.GetCachedIdx(rc.landmarkDeltaToFirstMap),
		    .landmarkSp = {landmarks.size(), newLandmarks.size()},
		});
		landmarks.insert(landmarks.cend(), newLandmarks.cbegin(), newLandmarks.cend());
	}

	transitions.emplace_back(numRecordedTicks,
	                         transitions.empty() ? TrIdx<TrMap>{} : transitions.back().toMapIdx,
	                         toMapIdx);
}

#endif
