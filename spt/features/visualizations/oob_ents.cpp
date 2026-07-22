#include "stdafx.hpp"
#include "..\feature.hpp"

#ifndef OE

#define GAME_DLL
#include "cbase.h"

#include "spt\utils\interfaces.hpp"
#include "spt\utils\signals.hpp"
#include "spt\features\ent_props.hpp"
#include "spt\features\hud.hpp"
#include "renderer\mesh_renderer.hpp"
#include "imgui\imgui_interface.hpp"

class HudOobEntsFeature : public FeatureWrapper<HudOobEntsFeature>
{
private:
	struct EntInfo
	{
		CBaseHandle handle;
		// index into string pool (null terminated)
		int strIdx;
		Vector pos; // this is the OBB center, *not* the origin
		QAngle ang;
	};

	std::string stringPool;
	std::vector<EntInfo> oobEnts;
	std::vector<EntInfo> nonOobEnts;

protected:
	virtual void LoadFeature() override;
	virtual void UnloadFeature() override
	{
		oobEnts.clear();
		nonOobEnts.clear();
	}

private:
	// collect all ents every tick instead of every frame
	void OnTickSignal(bool simulating);

public:
	void PrintEntsCon();
#ifdef SPT_HUD_ENABLED
	void PrintEntsHud();
#endif

#ifdef SPT_MESH_RENDERING_ENABLED
	void DrawEntsPos(MeshRendererDelegate& mr);
#endif
};

static HudOobEntsFeature spt_hud_oob_ents_feat;

#ifdef SPT_HUD_ENABLED
ConVar spt_hud_oob_ents("spt_hud_oob_ents", "0", FCVAR_NONE, "Shows entities that are oob.");
#endif

#ifdef SPT_MESH_RENDERING_ENABLED
ConVar spt_draw_oob_ents("spt_draw_oob_ents",
                         "0",
                         FCVAR_NONE,
                         "Draws a position indicator for entities.\n"
                         "    1 - draw for oob ents only\n"
                         "    2 - no z-test\n"
                         "    3 - draw for all ents & no-ztest\n");
#endif

CON_COMMAND_F(spt_print_oob_ents, "Prints entities that are oob", FCVAR_DONTRECORD)
{
	spt_hud_oob_ents_feat.PrintEntsCon();
}

void HudOobEntsFeature::LoadFeature()
{
	if (!TickSignal.Works)
		return;
	TickSignal.Connect(this, &HudOobEntsFeature::OnTickSignal);

#ifdef SPT_HUD_ENABLED
	bool hudEnabled = AddHudCallback(
	    "hud oob ents", [this](std::string) { PrintEntsHud(); }, spt_hud_oob_ents);
	if (hudEnabled)
		SptImGui::RegisterHudCvarCheckbox(spt_hud_oob_ents);
#endif

	InitCommand(spt_print_oob_ents);
#ifdef SPT_MESH_RENDERING_ENABLED
	if (spt_meshRenderer.signal.Works)
	{
		InitConcommandBase(spt_draw_oob_ents);
		spt_meshRenderer.signal.Connect(this, &HudOobEntsFeature::DrawEntsPos);

		SptImGuiGroup::Draw_Misc_OobEnts.RegisterUserCallback(
		    []()
		    {
			    const char* opts[] = {
			        "Disabled",
			        "OOB entities",
			        "OOB entities + no z-test",
			        "All entities + no z-test",
			    };
			    SptImGui::CvarCombo(spt_draw_oob_ents, "entity position indicator", opts, ARRAYSIZE(opts));
#ifdef SPT_HUD_ENABLED
			    SptImGui::CvarCheckbox(spt_hud_oob_ents, "##checkbox");
#endif
		    });
	}
#endif
}

void HudOobEntsFeature::OnTickSignal(bool simulating)
{
	if (!simulating)
		return;

	using namespace utils;

	oobEnts.clear();
	nonOobEnts.clear();
	stringPool.clear();

	if (!interfaces::engine_server->PEntityOfEntIndex(0))
		return;

	// loop through all ents and save them to the appropriate list
	for (int i = 2; i < MAX_EDICTS; i++)
	{
		edict_t* ed = interfaces::engine_server->PEntityOfEntIndex(i);
		if (!ed || !ed->GetIServerEntity())
			continue;
		CBaseEntity* ent = ed->GetIServerEntity()->GetBaseEntity();

		static CachedField<string_t, "CBaseEntity", "m_iName", true> nf;
		static CachedField<string_t, "CBaseEntity", "m_iClassname", true> cnf;
		static CachedField<string_t, "CBaseEntity", "m_iGlobalname", true> gnf;
		const char* name = nf.GetPtr(ent)->ToCStr();
		const char* className = cnf.GetPtr(ent)->ToCStr();
		const char* globalName = gnf.GetPtr(ent)->ToCStr();

		// this list is probably nowhere near complete, should this be a whitelist instead?
		static const char* const ignoreClasses[] = {
		    "physicsshadowclone",
		    "portalsimulator_collisionentity",
		    "phys_bone_follower",
		    "generic_actor",
		    "prop_dynamic",
		    "prop_door_rotating",
		    "prop_portal_stats_display",
		    "func_brush",
		    "func_door",
		    "func_door_rotating",
		    "func_rotating",
		    "func_tracktrain",
		    "func_rot_button",
		    "func_button",
		};
		if (std::any_of(ignoreClasses,
		                ignoreClasses + ARRAYSIZE(ignoreClasses),
		                [className](const char* cmp) { return !strcmp(className, cmp); }))
		{
			continue;
		}

		// m_Collision is not in datamap, use field before it
		static CachedField<CCollisionProperty, "CBaseEntity", "m_hMovePeer", true, sizeof EHANDLE> cf;
		static CachedField<QAngle, "CBaseEntity", "m_angAbsRotation", true> rotField;
		const CCollisionProperty* colProp = cf.GetPtr(ent);
		// arbitary decision: we don't care about things being oob that aren't solid according to this function
		if (!colProp->IsSolid())
			continue;
		Vector pos = colProp->WorldSpaceCenter();
		const QAngle* ang = rotField.GetPtr(ent);
		// check if ent is oob
		bool oob = interfaces::engineTraceServer->PointOutsideWorld(pos);
		if (oob)
		{
			trace_t tr;
			Ray_t ray;
			ray.Init(pos, pos + Vector{1, 1, 1});
			// arbtirarily chosen filter for world only & no ents or static props, same mask as hud_oob
			CTraceFilterWorldOnly filter{};
			interfaces::engineTraceServer->TraceRay(ray, MASK_PLAYERSOLID_BRUSHONLY, &filter, &tr);
			oob = !tr.startsolid;
		}
		auto& vec = oob ? oobEnts : nonOobEnts;
		vec.emplace_back(ent->GetRefEHandle(), stringPool.size(), pos, *ang);
		// determine a good name for this ent
		assert(className);
		if (name && *name)
			stringPool += std::format("{} ({})", name, className);
		else if (globalName && *globalName)
			stringPool += std::format("{} ({})", globalName, className);
		else
			stringPool += className;
		stringPool += '\0';
	}
}

void HudOobEntsFeature::PrintEntsCon()
{
	if (!interfaces::engine_server->PEntityOfEntIndex(0))
	{
		Msg("server not loaded\n");
		return;
	}
	Msg("%lu oob ent%s\n", oobEnts.size(), oobEnts.empty() ? "s" : (oobEnts.size() == 1 ? ":" : "s:"));
	if (oobEnts.empty())
		return;
	for (const EntInfo& ent : oobEnts)
	{
		Msg("%-45s, index: %04d, pos: <%7.3f, %7.3f, %7.3f>\n",
		    stringPool.data() + ent.strIdx,
		    ent.handle.GetEntryIndex(),
		    ent.pos.x,
		    ent.pos.y,
		    ent.pos.z);
	}
}

#ifdef SPT_HUD_ENABLED
void HudOobEntsFeature::PrintEntsHud()
{
	if (!interfaces::engine_server->PEntityOfEntIndex(0))
	{
		spt_hud_feat.DrawTopHudElement(L"oob ents: server not loaded");
		return;
	}
	spt_hud_feat.DrawTopHudElement(L"%lu oob ent%S",
	                               oobEnts.size(),
	                               oobEnts.empty() ? "s" : (oobEnts.size() == 1 ? ":" : "s:"));
	if (oobEnts.empty())
		return;
	constexpr size_t MAX_HUD_OOB_ENTS = 10;
	for (size_t i = 0; i < MAX_HUD_OOB_ENTS && i < oobEnts.size(); i++)
	{
		spt_hud_feat.DrawColorTopHudElement(Color{255, 200, 200, 255},
		                                    L"%S",
		                                    stringPool.data() + oobEnts[i].strIdx);
	}
	if (oobEnts.size() > MAX_HUD_OOB_ENTS)
		spt_hud_feat.DrawTopHudElement(L"%lu remaining...", oobEnts.size() - MAX_HUD_OOB_ENTS);
}
#endif

#ifdef SPT_MESH_RENDERING_ENABLED
void HudOobEntsFeature::DrawEntsPos(MeshRendererDelegate& mr)
{
	if (!spt_draw_oob_ents.GetBool() || !interfaces::engine_server->PEntityOfEntIndex(0))
		return;

	bool zTest = spt_draw_oob_ents.GetInt() <= 1;
	bool drawNonOobEnts = spt_draw_oob_ents.GetInt() >= 3;
	// use raw pointers so that we can switch to iterating over a different vector
	const EntInfo* it = oobEnts.data();
	if (oobEnts.empty())
	{
		if (drawNonOobEnts && !nonOobEnts.empty())
			it = nonOobEnts.data();
		else
			return;
	}

	// mesh may overflow, repeat until we've gone through all ents
	for (;;)
	{
		mr.DrawMesh(spt_meshBuilder.CreateDynamicMesh(
		    [this, zTest, &it](MeshBuilderDelegate& mb)
		    {
			    bool curEntOob = it >= oobEnts.data() && it < oobEnts.data() + oobEnts.size();
			    for (;;)
			    {
				    // break if we've reached the end of a vector
				    if (it == oobEnts.data() + oobEnts.size()) [[unlikely]]
					    break;
				    else if (it == nonOobEnts.data() + nonOobEnts.size()) [[unlikely]]
					    break;

				    // axis indicator for all ents
				    Vector v[3];
				    AngleVectors(it->ang, &v[0], &v[1], &v[2]);
				    for (int ax = 0; ax < 3; ax++)
				    {
					    uint32_t bitCol = (0xffu << 24) | (0xffu << (ax * 8));
					    bool ret =
					        mb.AddLine(it->pos,
					                   it->pos + v[ax] * (curEntOob + 2) * 5.f,
					                   LineColor{*reinterpret_cast<color32*>(&bitCol), zTest});
					    if (!ret)
						    return;
				    }
				    // additional box indicator for oob ents only
				    if (curEntOob)
				    {
					    Vector boxExt{1.5f, 1.5f, 1.5f};
					    bool ret = mb.AddBox(it->pos,
					                         -boxExt,
					                         boxExt,
					                         it->ang,
					                         ShapeColor{C_OUTLINE(255, 255, 0, 50), zTest, zTest});
					    if (!ret)
						    return;
				    }
				    it++;
			    }
		    }));
		// switch iterator to other vector if we need to iterate over all ents
		if (it == oobEnts.data() + oobEnts.size())
		{
			if (drawNonOobEnts && !nonOobEnts.empty())
				it = nonOobEnts.data();
			else
				break;
		}
		else if (it == nonOobEnts.data() + nonOobEnts.size())
		{
			break;
		}
		if (!it)
			break;
	}
}
#endif

#endif
