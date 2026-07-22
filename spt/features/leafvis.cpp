#include "stdafx.hpp"
#include "..\feature.hpp"
#include "visualizations\imgui\imgui_interface.hpp"

namespace patterns
{
	PATTERNS(MiddleOfLeafVisBuild,
	         "3420", // also 5135, old steampipe and new steampipe
	         "74 ?? 39 35 ?? ?? ?? ?? 0F 84 ?? ?? ?? ??");
}

ConVar y_spt_leafvis_index("y_spt_leafvis_index",
                           "0",
                           FCVAR_NONE,
                           "Choose which BSP leaf mat_leafvis will use. Requires mat_leafvis to be set\n");

// Enhance mat_leafvis by allowing you to choose the BSP leaf by index
class LeafVisFeature : public FeatureWrapper<LeafVisFeature>
{
public:
protected:
	virtual void InitHooks() override;
	virtual void LoadFeature() override;

private:
	void* ORIG_MiddleOfLeafVisBuild = nullptr;
	static void HOOKED_MiddleOfLeafVisBuild();
};
static LeafVisFeature spt_leafvis;

void LeafVisFeature::InitHooks()
{
	HOOK_FUNCTION(engine, MiddleOfLeafVisBuild);
}

void LeafVisFeature::LoadFeature()
{
	if (ORIG_MiddleOfLeafVisBuild)
	{
		InitConcommandBase(y_spt_leafvis_index);

		if (g_pCVar && g_pCVar->FindVar("mat_leafvis"))
		{
			SptImGuiGroup::Draw_Misc_LeafVis.RegisterUserCallback(
			    []()
			    {
				    ConVar* mat_leafvis = g_pCVar->FindVar("mat_leafvis");
				    ImGui::BeginDisabled(mat_leafvis->GetInt() != 1);
					
				    SptImGui::CvarInputTextInteger(y_spt_leafvis_index,
				                                   "leaf index",
				                                   "enter integer index");
				    ImGui::EndDisabled();
				    ImGui::Text("Edit mat_leafvis:");
				    ImGui::SameLine();
				    if (ImGui::Button("set to 0"))
					    mat_leafvis->SetValue(0);
				    ImGui::SameLine();
				    if (ImGui::Button("set to 1"))
					    mat_leafvis->SetValue(1);
				    ImGui::SameLine();
				    if (ImGui::Button("set to 2"))
					    mat_leafvis->SetValue(2);
				    ImGui::SameLine();
				    SptImGui::CvarValue(*mat_leafvis, SptImGui::CVF_NO_WRANGLE);
			    });
		}
	}
}

static __forceinline int GetLeafIndex()
{
	return y_spt_leafvis_index.GetInt();
}

#pragma warning(push)
#pragma warning(disable : 4740)

/**
* The game calls CM_PointLeafnum, returning the leaf index of the current view
* point and loads ESI with it. This hooks is right after that, setting the leaf
* index to our cvar value
*/
__declspec(naked) void LeafVisFeature::HOOKED_MiddleOfLeafVisBuild()
{
	__asm {
		push eax;
		pushfd;
	}
	// this loads eax
	GetLeafIndex();
	__asm {
		cmp eax, 0;
		cmova esi, eax; // override leafIndex in LeafVisBuild if spt_leafvis_index > 0
		popfd;
		pop eax;
		jmp spt_leafvis.ORIG_MiddleOfLeafVisBuild;
	}
}

#pragma warning(pop)
