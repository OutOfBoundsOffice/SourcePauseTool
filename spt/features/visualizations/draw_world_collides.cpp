#include "stdafx.hpp"
#include "..\feature.hpp"

#include "worldsize.h"

#include "spt\utils\ent_utils.hpp"
#include "spt\utils\interfaces.hpp"
#include "spt\utils\portal_utils.hpp"
#include "spt\features\tracing.hpp"
#include "spt\features\generic.hpp"
#include "spt\features\create_collide.hpp"
#include "spt\features\tas.hpp"
#include "renderer\mesh_renderer.hpp"
#include "imgui\imgui_interface.hpp"

#ifdef SPT_MESH_RENDERING_ENABLED

ConVar spt_draw_world_collides(
    "spt_draw_world_collides",
    "0",
    FCVAR_DONTRECORD,
    "Draws the collision of whatever part of the world the player is looking at. Use 2 to ignore z testing. Uses the following colors:\n"
    "    - turquoise: box brush\n"
    "    - purple: complex (i.e. non-box) brush\n"
    "    - white: displacement\n"
    "    - red: static prop");

// this hidden cvar can be used to change the mask
const std::string _g_default_mask_str = std::to_string(MASK_PLAYERSOLID | MASK_VISIBLE);
ConVar spt_draw_world_collides_mask("spt_draw_world_collides_mask",
                                    _g_default_mask_str.c_str(),
                                    FCVAR_DONTRECORD | FCVAR_HIDDEN,
                                    "The tracing mask used by spt_draw_world_collides.");

#define _ZTEST (spt_draw_world_collides.GetInt() < 2)

#define SC_BOX_BRUSH (ShapeColor{C_OUTLINE(0, 255, 255, 60), _ZTEST, _ZTEST})
#define SC_COMPLEX_BRUSH (ShapeColor{C_OUTLINE(255, 0, 255, 60), _ZTEST, _ZTEST})
#define SC_STATIC_PROP (ShapeColor{C_OUTLINE(200, 50, 50, 40), _ZTEST, _ZTEST})
#define SC_DISPLACEMENT (ShapeColor{C_OUTLINE(255, 255, 255, 40), _ZTEST, _ZTEST})

extern class DrawWorldCollideFeature spt_DrawWorldCollides_feat;

// draws brushes and static props
class DrawWorldCollideFeature : public FeatureWrapper<DrawWorldCollideFeature>
{
protected:
	virtual void LoadFeature() override
	{
		if (!spt_meshRenderer.signal.Works)
			return;
		spt_meshRenderer.signal.Connect(this, &DrawWorldCollideFeature::OnMeshRenderSignal);
		cache.Clear();

		InitConcommandBase(spt_draw_world_collides);
		InitConcommandBase(spt_draw_world_collides_mask);

		spt_draw_world_collides.InstallChangeCallback(
		    [](IConVar* var, const char* pOldValue, auto)
		    {
			    // changing between 1 & 2 will change the mesh material
			    if (strcmp(((ConVar*)var)->GetString(), pOldValue))
				    spt_DrawWorldCollides_feat.cache.Clear();
		    });

		SptImGuiGroup::Draw_Collides_World.RegisterUserCallback(ImGuiCallback);
	};

	virtual void UnloadFeature() override
	{
		cache.Clear();
	};

	struct
	{
		StaticMesh mesh;
		matrix3x4_t matrix;
		WorldHitInfo lastHitInfo;

		void Clear()
		{
			mesh.Destroy();
			SetIdentityMatrix(matrix);
			lastHitInfo.Clear();
		}
	} cache;

private:
	static void ImGuiCallback()
	{
		// copied from the draw world collides callback
		ConVar& c = spt_draw_world_collides;
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
		int newVal = enabled ? 2 - zTest : 0;
		if (newVal != oldVal)
			c.SetValue(newVal);

		long long mask = strtoll(spt_draw_world_collides_mask.GetString(), nullptr, 10);
		if (SptImGui::InputTextInteger("##cvar_mask", "enter integer mask", mask, 10))
			spt_draw_world_collides_mask.SetValue((int)mask);
		ImGui::SameLine();
		SptImGui::CvarValue(spt_draw_world_collides_mask);

		ImGui::EndDisabled();
	}

	void OnMeshRenderSignal(MeshRendererDelegate& mr)
	{
		if (!spt_draw_world_collides.GetBool())
			return;
		if (!interfaces::engine_server->PEntityOfEntIndex(0))
			return;

		DoTrace(mr); // fills cache

		if (cache.mesh.Valid())
		{
			mr.DrawMesh(cache.mesh,
			            [this](const CallbackInfoIn& infoIn, CallbackInfoOut& infoOut)
			            {
				            infoOut.mat = cache.matrix;
				            RenderCallbackZFightFix(infoIn, infoOut);
			            });
		}
	}

	void DoTrace(MeshRendererDelegate& mr)
	{
		// this does not work when we're in a vehicle, we'd want to use a modified version of CBasePlayer::CalcView
		Vector eyePos = utils::GetPlayerEyePosition();
		static utils::CachedField<QAngle, "CBasePlayer", "pl.v_angle", true> vangle;
		QAngle eyeAng = tas_pause.GetBool() ? utils::GetPlayerEyeAngles() : *vangle.GetPtrPlayer();

#ifdef SPT_PORTAL_UTILS
		auto env = utils::GetEnvironmentPortal();
		transformThroughPortal(env, eyePos, eyeAng, eyePos, eyeAng);
#endif
		Vector dir;
		AngleVectors(eyeAng, &dir);

		Ray_t ray;
		ray.Init(eyePos, eyePos + dir * MAX_TRACE_LENGTH);

		TraceFilterIgnorePlayer filter{};
		trace_t tr;

		int fMask = strtol(spt_draw_world_collides_mask.GetString(), nullptr, 0);
		WorldHitInfo hitInfo = spt_tracing.TraceLineWithWorldInfoServer(ray, fMask, &filter, tr);

		if (tr.startsolid)
		{
			cache.Clear();
			return; // traces starting in world geo are cursed, ignore them
		}

		// draw line from player eyes to hit point
		mr.DrawMesh(
		    spt_meshBuilder.CreateDynamicMesh(
		        [&tr](MeshBuilderDelegate& mb)
		        { mb.AddLine(tr.startpos, tr.endpos, {_COLOR(0, 255, 0, 255)}); }),
		    [this
#ifdef SPT_PORTAL_UTILS
		     ,
		     env
#endif
		](const CallbackInfoIn& infoIn, CallbackInfoOut& infoOut)
		    {
			    // always show this through portals
			    if (infoIn.portalRenderDepth > 0)
				    return;

			    // Since the server eyes don't match up with the client eyes, this line can be very annoying, and
			    // we want to turn it off when we're looking from the player's eyes. In the quest to find a way to
			    // do that I have gone through the seven stages of grief. So we're just gonna go with something
			    // that's good enough. This does not work that well through portals or in vehicles, but it does
			    // take care of stepping up/down which was the main annoyance I had.

			    static utils::CachedField<Vector, "CBasePlayer", "m_vecAbsOrigin", false> v1;
			    static utils::CachedField<Vector, "CBasePlayer", "m_vecViewOffset", false> v2;

			    if (v1.Exists() && v2.Exists())
			    {
				    Vector clientEyes = *v1.GetPtrPlayer() + *v2.GetPtrPlayer();
				    QAngle qa;
#ifdef SPT_PORTAL_UTILS
				    transformThroughPortal(env, clientEyes, qa, clientEyes, qa);
#endif
				    Vector diff = clientEyes - infoIn.cvs.origin;
				    if (fabsf(diff.x) < 0.01 && fabsf(diff.y) < 0.01 && fabsf(diff.z) < 32)
					    infoOut.skipRender = true;
			    }
		    });

		// (always) draw hit point
		mr.DrawMesh(spt_meshBuilder.CreateDynamicMesh(
		    [&tr](MeshBuilderDelegate& mb)
		    {
			    mb.AddLine(tr.endpos, tr.endpos + tr.plane.normal * 5, {_COLOR(255, 255, 0, 255)});
			    if (tr.DidHit())
				    mb.AddCross(tr.endpos, 7, {_COLOR(255, 0, 0, 255)});
		    }));

		if (!memcmp(&cache.lastHitInfo, &hitInfo, sizeof WorldHitInfo) && cache.mesh.Valid())
			return; // cache is valid

		cache.Clear();
		cache.lastHitInfo = hitInfo;

		if (hitInfo.brush)
			CacheBrush();
		else if (hitInfo.staticProp)
			CacheStaticProp();
		else if (hitInfo.dispTree)
			CacheDisplacement();
	}

	void CacheBrush()
	{
		auto bspData = cache.lastHitInfo.bspData;
		auto brush = cache.lastHitInfo.brush;

		if (brush->IsBox())
		{
			cache.mesh = spt_meshBuilder.CreateStaticMesh(
			    [=](MeshBuilderDelegate& mb)
			    {
				    const cboxbrush_t* box = &bspData->map_boxbrushes[brush->GetBox()];
				    mb.AddBox({0, 0, 0}, box->mins, box->maxs, {0, 0, 0}, SC_BOX_BRUSH);
			    });
		}
		else
		{
			static std::vector<VPlane> planes;
			planes.clear();
			planes.reserve(brush->numsides);
			for (int i = 0; i < brush->numsides; i++)
			{
				const cbrushside_t* side = &bspData->map_brushsides[brush->firstbrushside + i];
				if (side->bBevel)
					continue;
				planes.emplace_back(side->plane->normal, side->plane->dist);
			}

			CPolyhedron* poly =
			    GeneratePolyhedronFromPlanes((float*)planes.data(), planes.size(), 0.001, true);

			cache.mesh = spt_meshBuilder.CreateStaticMesh([=](MeshBuilderDelegate& mb)
			                                              { mb.AddCPolyhedron(poly, SC_COMPLEX_BRUSH); });

			poly->Release();
		}
	}

	void CacheStaticProp()
	{
		auto staticProp = cache.lastHitInfo.staticProp;
		vcollide_t* vc = interfaces::modelInfo->GetVCollide(staticProp->GetCollisionModel());
		Assert(vc);
		if (!vc)
			return;
		cache.mesh = spt_meshBuilder.CreateStaticMesh(
		    [vc](MeshBuilderDelegate& mb)
		    {
			    for (int i = 0; i < vc->solidCount; i++)
			    {
				    int numFaces;
				    auto verts = spt_collideToMesh.CreateCollideMesh(vc->solids[i], numFaces);
				    mb.AddTris(verts.get(), numFaces, SC_STATIC_PROP);
			    }
		    });
		cache.matrix = staticProp->CollisionToWorldTransform();
	}

	void CacheDisplacement()
	{
		struct CDispCollTree_guts : public CDispCollTree
		{
			CDispVector<Vector>& GetVerts()
			{
				return m_aVerts;
			}
			CDispVector<CDispCollTri>& GetTris()
			{
				return m_aTris;
			}
		};

		auto guts = reinterpret_cast<CDispCollTree_guts*>(cache.lastHitInfo.dispTree);
		int numTris = guts->GetTriSize();
		auto meshVerts = std::unique_ptr<Vector[]>(new Vector[numTris * 3]);

		for (int i = 0; i < numTris; i++)
		{
			meshVerts[3 * i + 0] = guts->GetVerts()[guts->GetTris()[i].GetVert(0)];
			meshVerts[3 * i + 1] = guts->GetVerts()[guts->GetTris()[i].GetVert(1)];
			meshVerts[3 * i + 2] = guts->GetVerts()[guts->GetTris()[i].GetVert(2)];
		}

		cache.mesh =
		    spt_meshBuilder.CreateStaticMesh([numTris, &meshVerts](MeshBuilderDelegate& mb)
		                                     { mb.AddTris(meshVerts.get(), numTris, SC_DISPLACEMENT); });
	}
};

DrawWorldCollideFeature spt_DrawWorldCollides_feat;

#endif
