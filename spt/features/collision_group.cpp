#include "stdafx.hpp"
#include "ent_utils.hpp"
#include "..\feature.hpp"
#include "visualizations\imgui\imgui_interface.hpp"
#include "spt\utils\ent_list.hpp"

namespace patterns
{
	PATTERNS(SetCollisionGroup,
	         "4044",
	         "8B 54 24 ?? 8B 81 ?? ?? ?? ?? 3B C2",
	         "5135",
	         "56 8B F1 8B 86 ?? ?? ?? ?? 3B 44 24 08 8D",
	         "7467727",
	         "55 8B EC 53 8B 5D ?? 56 57 8B F9 39 9F ?? ?? ?? ?? 74 ?? 8B ?? ?? ?? ?? ?? 8D",
	         "BMS-Retail",
	         "55 8B EC 53 8B D9 56 57 8B 7D ?? 39 BB ?? ?? ?? ?? 74 ?? 80 79 ?? 00");
}

class CollisionGroup : public FeatureWrapper<CollisionGroup>
{
public:
	DECL_MEMBER_THISCALL(void, SetCollisionGroup, void*, int collisionGroup);

protected:
	virtual bool ShouldLoadFeature() override
	{
		return true;
	}

	virtual void InitHooks() override
	{
		FIND_PATTERN(server, SetCollisionGroup);
	}

	virtual void LoadFeature() override;

private:
	static void ImGuiCallback();
};

CollisionGroup spt_collisiongroup;

CON_COMMAND_F(y_spt_set_collision_group,
              "Set player's collision group\nUsually:\n- 5 is normal collisions\n- 10 is quickclip\n",
              FCVAR_NONE)
{
	if (!spt_collisiongroup.ORIG_SetCollisionGroup)
		return;

	if (args.ArgC() < 2)
	{
		Warning(
		    "Format: spt_set_collision_group <collision group index>\nUsually:\n- 5 is normal collisions\n- 10 is quickclip\n");
		return;
	}

	auto playerPtr = utils::spt_serverEntList.GetPlayer();
	if (!playerPtr)
	{
		Warning("Server player not available\n");
		return;
	}
	int collide = atoi(args.Arg(1));

	spt_collisiongroup.ORIG_SetCollisionGroup(playerPtr, collide);
}

#ifndef STR
#define STR(x) #x
#endif

void CollisionGroup::LoadFeature()
{
	if (ORIG_SetCollisionGroup)
	{
		InitCommand(y_spt_set_collision_group);
		SptImGuiGroup::Cheats_Misc_PlayerCollisionGroup.RegisterUserCallback(ImGuiCallback);
	}
}

void CollisionGroup::ImGuiCallback()
{
	auto playerPtr = utils::spt_serverEntList.GetPlayer();
	ImGui::BeginDisabled(!playerPtr);
	ImGui::BeginGroup();

	const char* groups[LAST_SHARED_COLLISION_GROUP] = {
	    STR(COLLISION_GROUP_NONE),
	    STR(COLLISION_GROUP_DEBRIS),
	    STR(COLLISION_GROUP_DEBRIS_TRIGGER),
	    STR(COLLISION_GROUP_INTERACTIVE_DEBRIS),
	    STR(COLLISION_GROUP_INTERACTIVE),
	    STR(COLLISION_GROUP_PLAYER),
	    STR(COLLISION_GROUP_BREAKABLE_GLASS),
	    STR(COLLISION_GROUP_VEHICLE),
	    STR(COLLISION_GROUP_PLAYER_MOVEMENT),
	    STR(COLLISION_GROUP_NPC),
	    STR(COLLISION_GROUP_IN_VEHICLE),
	    STR(COLLISION_GROUP_WEAPON),
	    STR(COLLISION_GROUP_VEHICLE_CLIP),
	    STR(COLLISION_GROUP_PROJECTILE),
	    STR(COLLISION_GROUP_DOOR_BLOCKER),
	    STR(COLLISION_GROUP_PASSABLE_DOOR),
	    STR(COLLISION_GROUP_DISSOLVING),
	    STR(COLLISION_GROUP_PUSHAWAY),
	    STR(COLLISION_GROUP_NPC_ACTOR),
#ifndef OE
	    STR(COLLISION_GROUP_NPC_SCRIPTED),
#endif
	};

	static int val = 5;
	if (ImGui::BeginCombo("##combo", groups[val], ImGuiComboFlags_WidthFitPreview))
	{
		for (int i = 0; i < LAST_SHARED_COLLISION_GROUP; i++)
		{
			char buf[64];
			sprintf_s(buf, "%02d: %s", i, groups[i]);
			const bool is_selected = (val == i);
			if (ImGui::Selectable(buf, is_selected))
				val = i;
			if (is_selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if (ImGui::Button("set"))
		spt_collisiongroup.ORIG_SetCollisionGroup(playerPtr, val);
	ImGui::EndGroup();
	ImGui::EndDisabled();
	if (!playerPtr)
		ImGui::SetItemTooltip("server player not available");
	ImGui::SameLine();
	SptImGui::CmdHelpMarkerWithName(y_spt_set_collision_group_command);
}
