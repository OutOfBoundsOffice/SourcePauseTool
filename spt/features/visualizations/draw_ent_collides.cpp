#include "stdafx.hpp"
#include "..\feature.hpp"

// oh OE, why must you trouble me so?
#ifndef OE

#define GAME_DLL
#include "cbase.h"
#include "physics_shared.h"
#include "baseentity_shared.h"

#include "spt\utils\interfaces.hpp"
#include "spt\features\ent_props.hpp"
#include "spt\features\create_collide.hpp"
#include "renderer\mesh_renderer.hpp"
#include "imgui\imgui_interface.hpp"

#ifdef SPT_MESH_RENDERING_ENABLED

ConVar spt_draw_ent_collides(
    "spt_draw_ent_collides",
    "0",
    FCVAR_DONTRECORD,
    "Draws the physics objects of all entities (except for the world and player), use 2 to ignore z testing.\n"
    "    - a shaded color means that the player likely collides with the entity\n"
    "    - a green mesh means that the server/vphysics agree on where the entity is\n"
    "    - a yellow mesh is an entity that contains multiple physics objects\n"
    "    - otherwise, server & vphysics disagree on where the entity is, red is server & blue is vphysics");

#define _SC(r, g, b, faces, zTest) \
	((faces) ? ShapeColor{_COLOR(r, g, b, 15), _COLOR(0, 0, 0, 255), zTest, zTest} \
	         : ShapeColor{C_WIRE(r, g, b, 255), zTest, zTest})

#define SC_SERVER_VPHYS(flags) _SC(0, 255, 0, (flags) & CEF_COLLIDES_WITH_PLAYER, spt_draw_ent_collides.GetInt() < 2)
#define SC_SERVER(flags) _SC(255, 0, 0, (flags) & CEF_COLLIDES_WITH_PLAYER, spt_draw_ent_collides.GetInt() < 2)
#define SC_VPHYS(flags) _SC(0, 0, 255, (flags) & CEF_COLLIDES_WITH_PLAYER, spt_draw_ent_collides.GetInt() < 2)
#define SC_MULTI_VPHYS(flags) _SC(255, 255, 0, (flags) & CEF_COLLIDES_WITH_PLAYER, spt_draw_ent_collides.GetInt() < 2)

extern class DrawEntCollideFeature spt_DrawEntCollides_feat;

// draws all entity collides
class DrawEntCollideFeature : public FeatureWrapper<DrawEntCollideFeature>
{
private:
	enum CachedEntFlags : int
	{
		CEF_NONE = 0,
		CEF_EXISTS = 1,
		CEF_SERVER_VPHYS_SEPARATE = 2,
		CEF_MULTIPLE_VPHYS = 4,
		CEF_COLLIDES_WITH_PLAYER = 8,
	};

	struct CachedEnt
	{
		int serial : NUM_SERIAL_NUM_BITS;
		CachedEntFlags flags : (32 - NUM_SERIAL_NUM_BITS);

		struct CachedMesh
		{
			IPhysicsObject* pPhysObj;
			StaticMesh mesh;
			matrix3x4_t mat;
		};

		// used only when server/vphys agree
		CachedMesh serverMesh;
		// drawn when server/vphys disagree or multiple vphys objects
		std::vector<CachedMesh> vphysMeshes;

		void Destroy()
		{
			flags = CEF_NONE;
			serverMesh.mesh.Destroy();
			serverMesh.pPhysObj = nullptr;
			vphysMeshes.clear();
		}
	};

	std::array<CachedEnt, MAX_EDICTS> cachedEnts;
	std::vector<int> entsToDraw;

	struct
	{
		utils::CachedField<matrix3x4_t, "CBaseEntity", "m_rgflCoordinateFrame", true> coordFrame;
		utils::CachedField<int, "CBaseEntity", "m_CollisionGroup", true> collisionGroup;
		utils::CachedField<ushort, "CBaseEntity", "m_Collision.m_usSolidFlags", true> solidFlags;
		utils::CachedField<string_t, "CBaseEntity", "m_iClassname", true> className;
	} entFields;

	int lastUpdateTick = -1;

public:
	void ClearCache()
	{
		for (auto& e : cachedEnts)
			e.Destroy();
		lastUpdateTick = -1;
		entsToDraw.clear();
	}

protected:
	virtual void LoadFeature() override
	{
		if (!spt_meshRenderer.signal.Works)
			return;

		spt_meshRenderer.signal.Connect(this, &DrawEntCollideFeature::OnMeshRenderSignal);

		InitConcommandBase(spt_draw_ent_collides);
		spt_draw_ent_collides.InstallChangeCallback(
		    [](IConVar* var, const char* pOldValue, float flOldValue)
		    {
			    if (((ConVar*)var)->GetFloat() != flOldValue)
				    spt_DrawEntCollides_feat.ClearCache();
		    });
		ClearCache();

		SptImGuiGroup::Draw_Collides_Ents.RegisterUserCallback(ImGuiCallback);
	};

	virtual void UnloadFeature() override
	{
		ClearCache();
	};

private:
	void UpdateCache()
	{
		entsToDraw.clear();

		const int groupsCollideWithPlayer[] = {
		    COLLISION_GROUP_NONE,
		    COLLISION_GROUP_INTERACTIVE,
		    COLLISION_GROUP_PLAYER,
		    COLLISION_GROUP_BREAKABLE_GLASS,
		    COLLISION_GROUP_VEHICLE,
		    COLLISION_GROUP_PLAYER_MOVEMENT,
		    COLLISION_GROUP_NPC,
		    COLLISION_GROUP_PROJECTILE,
		    COLLISION_GROUP_PUSHAWAY, // technically not solid but used in movement code?
		    COLLISION_GROUP_NPC_ACTOR,
		    COLLISION_GROUP_NPC_SCRIPTED,
		};

		const char* excludedClassNames[] = {"portalsimulator_collisionentity"};

		// don't draw world & player
		for (size_t i = 2; i < cachedEnts.size(); i++)
		{
			auto& cachedEnt = cachedEnts[i];

			edict_t* ed = interfaces::engine_server->PEntityOfEntIndex(i);
			if (!ed || !ed->GetIServerEntity())
			{
				cachedEnt.Destroy();
				continue; // no entity in this slot
			}

			CBaseEntity* pEnt = ed->GetIServerEntity()->GetBaseEntity();

			const char* className = entFields.className.GetPtr(pEnt)->ToCStr();
			if (className)
			{
				if (std::any_of(excludedClassNames,
				                excludedClassNames + ARRAYSIZE(excludedClassNames),
				                [className](auto exName) { return !strcmp(className, exName); }))
				{
					continue;
				}
			}

			IPhysicsObject* physObjs[VPHYSICS_MAX_OBJECT_LIST_COUNT];
			int numPhysObjs = spt_collideToMesh.GetPhysObjList(pEnt, physObjs, ARRAYSIZE(physObjs));

			if (numPhysObjs == 0)
			{
				cachedEnt.Destroy();
				continue; // no vphys object
			}

			int newFlags = CEF_EXISTS;

			if (!(*entFields.solidFlags.GetPtr(pEnt) & FSOLID_NOT_SOLID))
			{
				// TODO add more checks here
				int collisionGroup = *entFields.collisionGroup.GetPtr(pEnt);

				if (std::any_of(groupsCollideWithPlayer,
				                groupsCollideWithPlayer + ARRAYSIZE(groupsCollideWithPlayer),
				                [collisionGroup](int cg) { return collisionGroup == cg; }))
				{
					newFlags |= CEF_COLLIDES_WITH_PLAYER;
				}
			}

			cachedEnt.serverMesh.mat = *entFields.coordFrame.GetPtr(pEnt);

			if (numPhysObjs > 1)
			{
				newFlags |= CEF_MULTIPLE_VPHYS;
			}
			else
			{
				matrix3x4_t vphysMat;
				physObjs[0]->GetPositionMatrix(&vphysMat);

				// jank matrix norm, check if server matrix matches vphys matrix
				for (int j = 0; j < 12; j++)
				{
					if (fabsf(cachedEnt.serverMesh.mat.Base()[j] - vphysMat.Base()[j]) > 0.1)
					{
						newFlags |= CEF_SERVER_VPHYS_SEPARATE;
						break;
					}
				}
			}

			entsToDraw.push_back(i);
			CBaseHandle newHandle = ed->GetIServerEntity()->GetRefEHandle();

			// serial doesn't match or flags have changed
			bool clear = newHandle.GetSerialNumber() != cachedEnt.serial || newFlags != cachedEnt.flags;

			if (!clear)
			{
				// now check if the cached ent meshes make sense

				if (newFlags & CEF_MULTIPLE_VPHYS)
				{
					// only store vphys meshes, check if count & pointers match
					cachedEnt.serverMesh.mesh.Destroy();
					clear = numPhysObjs != (int)cachedEnt.vphysMeshes.size();
					if (!clear)
					{
						for (int j = 0; j < numPhysObjs; j++)
						{
							if (cachedEnt.vphysMeshes[j].pPhysObj != physObjs[j]
							    || !cachedEnt.vphysMeshes[j].mesh.Valid())
							{
								clear = true;
								break;
							}
						}
					}
				}
				else if (newFlags & CEF_SERVER_VPHYS_SEPARATE)
				{
					// should have 1 server & 1 vphys mesh
					clear = !cachedEnt.serverMesh.mesh.Valid() || cachedEnt.vphysMeshes.size() != 1
					        || cachedEnt.vphysMeshes[0].pPhysObj != physObjs[0]
					        || !cachedEnt.vphysMeshes[0].mesh.Valid();
				}
				else
				{
					// only use server mesh for drawing, but check if its phys pointer has changed
					cachedEnt.vphysMeshes.clear();
					clear = !cachedEnt.serverMesh.mesh.Valid()
					        || cachedEnt.serverMesh.pPhysObj != physObjs[0];
				}
			}

			if (clear)
				cachedEnt.Destroy();

			// create vphys-side mesh(es) if needed, update all matrices
			if (newFlags & (CEF_MULTIPLE_VPHYS | CEF_SERVER_VPHYS_SEPARATE))
			{
				for (int j = 0; j < numPhysObjs; j++)
				{
					if (cachedEnt.flags == CEF_NONE)
					{
						auto color = (newFlags & CEF_MULTIPLE_VPHYS) ? SC_MULTI_VPHYS(newFlags)
						                                             : SC_VPHYS(newFlags);

						if (physObjs[j]->GetSphereRadius() > 0)
						{
							cachedEnt.vphysMeshes.emplace_back(
							    physObjs[j],
							    spt_meshBuilder.CreateStaticMesh(
							        [&physObjs, j, newFlags, &color](
							            MeshBuilderDelegate& mb) {
								        mb.AddSphere(vec3_origin,
								                     physObjs[j]->GetSphereRadius(),
								                     2,
								                     color);
							        }));
						}
						else
						{
							cachedEnt.vphysMeshes.emplace_back(
							    physObjs[j],
							    spt_meshBuilder.CreateStaticMesh(
							        [&physObjs, j, newFlags, &color](
							            MeshBuilderDelegate& mb)
							        {
								        int numTris;
								        auto verts = spt_collideToMesh
								                         .CreatePhysObjMesh(physObjs[j],
								                                            numTris);
								        mb.AddTris(verts.get(), numTris, color);
							        }));
						}
					}
					physObjs[j]->GetPositionMatrix(&cachedEnt.vphysMeshes[j].mat);
				}
			}

			// create server-side mesh if needed
			if (!(newFlags & CEF_MULTIPLE_VPHYS) && cachedEnt.flags == CEF_NONE)
			{
				cachedEnt.serverMesh.pPhysObj = physObjs[0];
				auto color = (newFlags & CEF_SERVER_VPHYS_SEPARATE) ? SC_SERVER(newFlags)
				                                                    : SC_SERVER_VPHYS(newFlags);

				if (physObjs[0]->GetSphereRadius() > 0)
				{
					cachedEnt.serverMesh.mesh = spt_meshBuilder.CreateStaticMesh(
					    [&physObjs, newFlags, &color](MeshBuilderDelegate& mb)
					    { mb.AddSphere(vec3_origin, physObjs[0]->GetSphereRadius(), 2, color); });
				}
				else
				{
					cachedEnt.serverMesh.mesh = spt_meshBuilder.CreateStaticMesh(
					    [&physObjs, newFlags, &color](MeshBuilderDelegate& mb)
					    {
						    int numTris;
						    auto verts =
						        spt_collideToMesh.CreatePhysObjMesh(physObjs[0], numTris);
						    mb.AddTris(verts.get(), numTris, color);
					    });
				}
			}

			cachedEnt.flags = (CachedEntFlags)newFlags;
			cachedEnt.serial = newHandle.GetSerialNumber();
		}
	}

	void OnMeshRenderSignal(MeshRendererDelegate& mr)
	{
		if (!spt_draw_ent_collides.GetBool())
			return;

		if (!interfaces::engine_server->PEntityOfEntIndex(0))
			return;

		// this is less for performance and more for organization
		int curTick = interfaces::engine_tool->HostTick();
		if (lastUpdateTick != curTick)
		{
			UpdateCache();
			lastUpdateTick = curTick;
		}

		for (int i : entsToDraw)
		{
			auto& cachedEnt = cachedEnts[i];

			if (cachedEnt.serverMesh.mesh.Valid())
			{
				mr.DrawMesh(cachedEnt.serverMesh.mesh,
				            [&](const CallbackInfoIn& infoIn, CallbackInfoOut& infoOut)
				            {
					            infoOut.mat = cachedEnt.serverMesh.mat;
					            RenderCallbackZFightFix(infoIn, infoOut);
				            });
			}

			for (auto& vphysMesh : cachedEnt.vphysMeshes)
			{
				if (vphysMesh.mesh.Valid())
				{
					mr.DrawMesh(vphysMesh.mesh,
					            [&](const CallbackInfoIn& infoIn, CallbackInfoOut& infoOut)
					            {
						            infoOut.mat = vphysMesh.mat;
						            RenderCallbackZFightFix(infoIn, infoOut);
					            });
				}
			}
		}
	}

	static void ImGuiCallback()
	{
		ConVar& c = spt_draw_ent_collides;
		int oldVal = c.GetInt();
		bool enabled = !!oldVal;
		SptImGui::CvarValue(c);
		ImGui::Checkbox("Enabled", &enabled);
		ImGui::BeginDisabled(!enabled);
		// done with a static value to persist the checkbox state even if the cvar is disabled
		static bool zTest = true;
		if (enabled && !!oldVal)
			zTest = oldVal < 2;
		ImGui::Checkbox("Z test", &zTest);
		ImGui::SetItemTooltip("If disabled, will draw on top of everything else.");
		ImGui::EndDisabled();
		int newVal = enabled ? 2 - zTest : 0;
		if (newVal != oldVal)
			c.SetValue(newVal);
	}
};

DrawEntCollideFeature spt_DrawEntCollides_feat;

#endif
#endif
