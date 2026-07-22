#include "stdafx.hpp"
#include "hud.hpp"
#include "..\feature.hpp"
#include "..\utils\game_detection.hpp"
#include "visualizations/imgui/imgui_interface.hpp"

#include <functional>
#include <memory>

#include "convar.hpp"

ConVar y_spt_hud_isg("y_spt_hud_isg", "0", FCVAR_NONE, "Is the ISG flag set?\n");

namespace patterns
{
	PATTERNS(MiddleOfRecheck_ov_element,
	         "5135",
	         "C6 05 ?? ?? ?? ?? 01 83 EE 01 3B 74 24 28 7D D3 8B 4C 24 38",
	         "1910503",
	         "C6 05 ?? ?? ?? ?? 01 4E 3B 75 ?? 7D ??",
	         "DMoMM",
	         "C6 05 ?? ?? ?? ?? 01 83 EE 01 3B 74 24 30 7D D5",
	         "BMS-0.9",
	         "C6 05 ?? ?? ?? ?? 01 4E 3B B5 ?? ?? ?? ?? 7D ?? 8B 8D ?? ?? ?? ?? 8B 9D ?? ?? ?? ?? 0F B7 85");
}

// This feature enables the ISG setting and HUD features
class ISGFeature : public FeatureWrapper<ISGFeature>
{
public:
	bool* isgFlagPtr = nullptr;

protected:
	uint32_t ORIG_MiddleOfRecheck_ov_element = 0;

	virtual void InitHooks() override
	{
		FIND_PATTERN(vphysics, MiddleOfRecheck_ov_element);
	}

	virtual void PreHook() override
	{
		if (ORIG_MiddleOfRecheck_ov_element)
			this->isgFlagPtr = *(bool**)(ORIG_MiddleOfRecheck_ov_element + 2);
		else
			Warning("spt_hud_isg 1 and spt_set_isg have no effect\n");
	}

	virtual void LoadFeature() override;

	virtual void UnloadFeature() override;

private:
	static void ImGuiCallback();
};

static ISGFeature spt_isg;

CON_COMMAND_F(y_spt_set_isg, "Sets the state of ISG in the game (1 or 0)", FCVAR_DONTRECORD)
{
	if (args.ArgC() < 2)
	{
		Warning("%s - %s\n", y_spt_set_isg_command.GetName(), y_spt_set_isg_command.GetHelpText());
		return;
	}
	if (spt_isg.isgFlagPtr)
		*spt_isg.isgFlagPtr = atoi(args.Arg(1));
	else
		Warning("%s has no effect\n", y_spt_set_isg_command.GetName());
}

bool IsISGActive()
{
	if (spt_isg.isgFlagPtr)
		return *spt_isg.isgFlagPtr;
	else
		return false;
}

void ISGFeature::LoadFeature()
{
	if (!ORIG_MiddleOfRecheck_ov_element)
		return;

	InitCommand(y_spt_set_isg);

	bool hudCallbackEnabled = false;

#ifdef SPT_HUD_ENABLED
	hudCallbackEnabled = AddHudCallback(
	    "isg", [](std::string) { spt_hud_feat.DrawTopHudElement(L"isg: %d", IsISGActive()); }, y_spt_hud_isg);
#endif

	SptImGuiGroup::Cheats_Misc_ISG.RegisterUserCallback(ImGuiCallback);
	if (hudCallbackEnabled)
		SptImGui::RegisterHudCvarCheckbox(y_spt_hud_isg);
}

void ISGFeature::UnloadFeature()
{
	ORIG_MiddleOfRecheck_ov_element = 0;
}

void ISGFeature::ImGuiCallback()
{
	if (ImGui::TreeNode("Info"))
	{
		ImGui::TextWrapped(
		    "Item Save Glitch is a game state which prevents the physics engine from handling rigid impacts, "
		    "this causes many entities to phase through each other and a lot of other peculiar behavior. "
		    "Once enabled, ISG cannot be disabled through normal means without restarting the game. SPT can "
		    "control the state of ISG with a single flag.");
		ImGui::TreePop();
	}

	SptImGui::CvarCheckbox(y_spt_hud_isg, "Show ISG state in HUD");
	ImGui::BeginDisabled(*spt_isg.isgFlagPtr);
	if (ImGui::Button("Enable ISG"))
		*spt_isg.isgFlagPtr = true;
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!*spt_isg.isgFlagPtr);
	if (ImGui::Button("Disable ISG"))
		*spt_isg.isgFlagPtr = false;
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled("ISG state: %d", *spt_isg.isgFlagPtr);
	ImGui::SameLine();
	SptImGui::HelpMarker("Can be set with %s.",
	                     WrangleLegacyCommandName(y_spt_set_isg_command.GetName(), true, nullptr));
}
