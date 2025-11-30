#include "stdafx.hpp"

#include "..\feature.hpp"
#include "convar.hpp"
#include "ent_utils.hpp"
#include "signals.hpp"
#include "..\visualizations\imgui\imgui_interface.hpp"
#include "spt\utils\ent_list.hpp"

#ifdef OE
static void RenderDelayCVarCallback(ConVar* pConVar, const char* pOldValue);
#else
static void RenderDelayCVarCallback(IConVar* pConVar, const char* pOldValue, float flOldValue);
#endif

typedef void(__cdecl* _DoImageSpaceMotionBlur)(void* view, int x, int y, int w, int h);
typedef void(__fastcall* _CViewEffects__Fade)(void* thisptr, int edx, void* data);
typedef void(__fastcall* _CViewEffects__Shake)(void* thisptr, int edx, void* data);
typedef void(__cdecl* _ResetToneMapping)(float value);
typedef void(__fastcall* _C_BaseAnimating__SetSequence)(void* thisptr, int edx, int nSequence);
typedef void(__cdecl* _CAM_ToThirdPerson)();

ConVar y_spt_motion_blur_fix("y_spt_motion_blur_fix", "0", FCVAR_ARCHIVE, "Fixes motion blur for startmovie.");
ConVar y_spt_disable_fade("y_spt_disable_fade", "0", FCVAR_ARCHIVE, "Disables all fades.");
ConVar y_spt_disable_shake("y_spt_disable_shake", "0", FCVAR_ARCHIVE, "Disables all shakes.");
ConVar y_spt_disable_tone_map_reset(
    "y_spt_disable_tone_map_reset",
    "0",
    FCVAR_ARCHIVE,
    "Prevents the tone map getting reset (during each load), useful for keeping colors the same between demos.");

// Implemented as a fix for https://github.com/MattMcNam/SetSequence
ConVar y_spt_override_tpose("y_spt_override_tpose",
                            "0",
                            FCVAR_DONTRECORD,
                            "Override Chell's t-pose animation with the given sequence, use 17 for standing in air.");

ConVar spt_viewmodel_offset_x("spt_viewmodel_offset_x", "0.0", FCVAR_ARCHIVE, "The viewmodel offset from default in X");
ConVar spt_viewmodel_offset_y("spt_viewmodel_offset_y", "0.0", FCVAR_ARCHIVE, "The viewmodel offset from default in Y");
ConVar spt_viewmodel_offset_z("spt_viewmodel_offset_z", "0.0", FCVAR_ARCHIVE, "The viewmodel offset from default in Z");

ConVar spt_disable_render_delay("spt_disable_render_delay",
                                "0",
                                0,
                                "Removes the 0.25s render delay after load.",
                                RenderDelayCVarCallback);

// Misc visual fixes
class VisualFixes : public FeatureWrapper<VisualFixes>
{
public:
	void ToggleRenderDelay(bool enabled);

protected:
	virtual bool ShouldLoadFeature() override;

	virtual void InitHooks() override;

	virtual void PreHook() override;

	virtual void LoadFeature() override;

	virtual void UnloadFeature() override;

private:
	uintptr_t* pgpGlobals = nullptr;
	_DoImageSpaceMotionBlur ORIG_DoImageSpaceMotionBlur = nullptr;
	_CViewEffects__Fade ORIG_CViewEffects__Fade = nullptr;
	_CViewEffects__Shake ORIG_CViewEffects__Shake = nullptr;
	_ResetToneMapping ORIG_ResetToneMapping = nullptr;
	_CAM_ToThirdPerson ORIG_CAM_ToThirdPerson = nullptr;
	void* ORIG_MiddleOfCL_FullyConnected = nullptr;

	bool* thirdPersonFlag = nullptr;

	DECL_BYTE_REPLACE(renderDelayCmp, 1, 0x00);

	static void __cdecl HOOKED_DoImageSpaceMotionBlur(void* view, int x, int y, int w, int h);
	static void __fastcall HOOKED_CViewEffects__Fade(void* thisptr, int edx, void* data);
	static void __fastcall HOOKED_CViewEffects__Shake(void* thisptr, int edx, void* data);
	static void __cdecl HOOKED_ResetToneMapping(float value);

#ifndef OE
	_C_BaseAnimating__SetSequence ORIG_C_BaseAnimating__SetSequence = nullptr;
	static void __fastcall HOOKED_C_BaseAnimating__SetSequence(void* thisptr, int edx, int nSequence);
#endif

	DECL_HOOK_THISCALL(void,
	                   C_BaseViewModel__CalcViewModelView,
	                   void*,
	                   void* owner,
	                   const Vector& eyePosition,
	                   const QAngle& eyeAngles);

	void OnFinishRestore(void* thisptr);
};

static VisualFixes spt_visual_fixes;

#ifdef OE
static void RenderDelayCVarCallback(ConVar* pConVar, const char* pOldValue)
#else
static void RenderDelayCVarCallback(IConVar* pConVar, const char* pOldValue, float flOldValue)
#endif
{
	spt_visual_fixes.ToggleRenderDelay(((ConVar*)pConVar)->GetBool());
}

namespace patterns
{
	PATTERNS(
	    DoImageSpaceMotionBlur,
	    "5135",
	    "A1 ?? ?? ?? ?? 81 EC 8C 00 00 00 83 78 30 00 0F 84 F3 06 00 00 8B 0D ?? ?? ?? ?? 8B 11 8B 82 80 00 00 00 FF D0",
	    "5339",
	    "55 8B EC A1 ?? ?? ?? ?? 83 EC 7C 83 78 30 00 0F 84 4A 08 00 00 8B 0D ?? ?? ?? ?? 8B 11 8B 82 80 00 00 00 FF D0",
	    "4104",
	    "A1 ?? ?? ?? ?? 81 EC 8C 00 00 00 83 78 30 00 0F 84 EE 06 00 00 8B 0D ?? ?? ?? ?? 8B 11 8B 42 7C FF D0",
	    "2257546",
	    "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC A1 ?? ?? ?? ?? 81 EC 98 00 00 00 83 78 30 00",
	    "1910503",
	    "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC A1 ?? ?? ?? ?? 81 EC ?? ?? ?? ?? 83 78 30 00 56 57 0F 84 ?? ?? ?? ?? 8B 0D",
	    "missinginfo1_6",
	    "55 8B EC A1 ?? ?? ?? ?? 81 EC ?? ?? ?? ?? 83 78 30 00 0F 84 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? 8B 11",
	    "BMS 0.9",
	    "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B ?? 89 6C 24 ?? 8B EC 81 EC A8 00 00 00 A1 ?? ?? ?? ?? 33 C5 89 45 ?? A1");
	PATTERNS(
	    CViewEffects__Fade,
	    "dmomm-4104-5135",
	    "51 56 6A 14 8B F1 E8 ?? ?? ?? ?? 8B 54 24 10 8B C8 0F B7 02 89 44 24 10 83 C4 04 89 4C 24 04 DB 44 24 0C",
	    "5377866",
	    "55 8B EC 51 56 57 6A 14 89 4D FC E8 ?? ?? ?? ?? 8B 7D 08 8B F0");
	PATTERNS(CViewEffects__Shake,
	         "4104-5135",
	         "56 8B 74 24 08 8B 06 85 C0 57 8B F9 74 05 83 F8 04 75 54 83 7F 24 20 7D 4E 6A 28",
	         "5377866",
	         "55 8B EC 56 8B 75 ?? 57 8B F9 8B 06 85 C0 74 ?? 83 F8 04");
	PATTERNS(
	    ResetToneMapping,
	    "5135",
	    "8B 0D ?? ?? ?? ?? 8B 01 8B 90 7C 01 00 00 56 FF D2 8B F0 85 F6 74 09 8B 06 8B 50 08 8B CE FF D2 8B 06 D9 44 24 08",
	    "1910503",
	    "55 8B EC 8B 0D ?? ?? ?? ?? 8B 01 8B 90 88 01 00 00 56 FF D2 8B F0 85 F6 74 09 8B 06 8B 50 08 8B CE FF D2 8B 06 F3 0F 10 45 08",
	    "5377866",
	    "55 8B EC 8B 0D ?? ?? ?? ?? 56 8B 01 FF 90 88 01 00 00 8B F0 85 F6 74 07 8B 06 8B CE FF 50 08 8B 06 D9 45 08",
	    "BMS-Retail",
	    "55 8B EC 8B 0D ?? ?? ?? ?? 56 8B 01 FF 90 ?? ?? ?? ?? 8B F0 85 F6 74 ?? 8B 06 8B CE FF 50 ?? 8B 06 D9 45 ?? 51 8B CE",
	    "te120",
	    "55 8B EC 8B 0D ?? ?? ?? ?? 8B 01 8B 90 ?? ?? ?? ?? 56 FF D2 8B F0 85 F6 74 ?? 8B 06 8B 50 ?? 8B CE FF D2 8B 06");
	PATTERNS(C_BaseAnimating__SetSequence,
	         "5135",
	         "8B 44 24 04 56 8B F1 39 86",
	         "7197370",
	         "55 8B EC 8B 45 ?? 56 8B F1 39 86 ?? ?? ?? ?? 74 ?? 6A 08");
	// this pattern is specifically made to match the messed up version of this function which has a cmp at the start
	PATTERNS(CAM_ToThirdPerson,
	         "5135",
	         "A1 ?? ?? ?? ?? 83 78 30 00 75 ?? C6 05 ?? ?? ?? ?? 01",
	         "7122284",
	         "A1 ?? ?? ?? ?? BA 01 00 00 00 0F B6 0D ?? ?? ?? ?? 83 78 30 00");
	PATTERNS(C_BaseViewModel__CalcViewModelView,
	         "5135",
	         "83 EC 24 8B 44 24 ?? 8B 50 ?? 56 8B f1 8B 08",
	         "7122284",
	         "55 8B EC 83 EC 24 8B 55 ?? 56 57 8B F9");
	PATTERNS(
	    MiddleOfCL_FullyConnected,
	    "5135",
	    "83 3D ?? ?? ?? ?? 01 75 ?? 8B 0D ?? ?? ?? ?? 8B 11",
	    "7122284",
	    "83 3D ?? ?? ?? ?? 01 75 ?? 8B 0D ?? ?? ?? ?? 8B 01 8B 40 ?? FF D0 84 C0 75 ?? F3 0F 10 05 ?? ?? ?? ??");
} // namespace patterns

void VisualFixes::InitHooks()
{
	HOOK_FUNCTION(client, DoImageSpaceMotionBlur);
	HOOK_FUNCTION(client, CViewEffects__Fade);
	HOOK_FUNCTION(client, CViewEffects__Shake);
	HOOK_FUNCTION(client, ResetToneMapping);
#ifndef OE
	HOOK_FUNCTION(client, C_BaseAnimating__SetSequence);
#endif
	FIND_PATTERN(client, CAM_ToThirdPerson);
	HOOK_FUNCTION(client, C_BaseViewModel__CalcViewModelView);
	FIND_PATTERN(engine, MiddleOfCL_FullyConnected);
}

bool VisualFixes::ShouldLoadFeature()
{
	return true;
}

void VisualFixes::PreHook()
{
	if (ORIG_DoImageSpaceMotionBlur)
	{
		int ptnNumber = GetPatternIndex((void**)&ORIG_DoImageSpaceMotionBlur);

		switch (ptnNumber)
		{
		case 0: // 5135
			pgpGlobals = *(uintptr_t**)((uintptr_t)ORIG_DoImageSpaceMotionBlur + 132);
			break;
		case 1: // 5339
			pgpGlobals = *(uintptr_t**)((uintptr_t)ORIG_DoImageSpaceMotionBlur + 153);
			break;
		case 2: // 4104
			pgpGlobals = *(uintptr_t**)((uintptr_t)ORIG_DoImageSpaceMotionBlur + 129);
			break;
		case 3: // 2257546
			pgpGlobals = *(uintptr_t**)((uintptr_t)ORIG_DoImageSpaceMotionBlur + 171);
			break;
		case 4: // 1910503
			pgpGlobals = *(uintptr_t**)((uintptr_t)ORIG_DoImageSpaceMotionBlur + 177);
			break;
		case 5: // missinginfo1_6
			pgpGlobals = *(uintptr_t**)((uintptr_t)ORIG_DoImageSpaceMotionBlur + 128);
			break;
		case 6: // BMS 0.9
			pgpGlobals = *(uintptr_t**)((uintptr_t)ORIG_DoImageSpaceMotionBlur + 0x18C);
			break;
		}

		DevMsg("[client dll] pgpGlobals is %p.\n", pgpGlobals);
		InitConcommandBase(y_spt_motion_blur_fix);
	}
}

void VisualFixes::LoadFeature()
{
	if (ORIG_CViewEffects__Fade)
		InitConcommandBase(y_spt_disable_fade);

	if (ORIG_CViewEffects__Shake)
		InitConcommandBase(y_spt_disable_shake);

	if (ORIG_ResetToneMapping)
		InitConcommandBase(y_spt_disable_tone_map_reset);

#ifndef OE
	if (ORIG_C_BaseAnimating__SetSequence)
		InitConcommandBase(y_spt_override_tpose);
#endif

	if (ORIG_CAM_ToThirdPerson && FinishRestoreSignal.Works)
	{
		FinishRestoreSignal.Connect(this, &VisualFixes::OnFinishRestore);
		thirdPersonFlag = *(bool**)((uintptr_t)ORIG_CAM_ToThirdPerson + 13);
	}

	if (!g_pCVar->FindVar("viewmodel_offset_x") && ORIG_C_BaseViewModel__CalcViewModelView)
	{
		InitConcommandBase(spt_viewmodel_offset_x);
		InitConcommandBase(spt_viewmodel_offset_y);
		InitConcommandBase(spt_viewmodel_offset_z);
	}

	if (ORIG_MiddleOfCL_FullyConnected)
	{
		/*
		* The 0.25s render delay is under the condition `if (cl.m_nMaxClients == 1 && !demoplayer->IsPlayingBack())`
		* 
		* The instructions for the first condition:
		* 83 3D ?? ?? ?? ?? 01	CMP dword ptr [cl.m_nMaxClients], 0x1
		* 75 ??					JNZ [location]
		* 
		* We replace the imm8 of the CMP from 1 to 0 (making it `cl.m_nMaxClients == 0`), forcing it to always be false.
		*/

		auto ptrRenderDelayCmp = (uintptr_t)ORIG_MiddleOfCL_FullyConnected + 6;
		INIT_BYTE_REPLACE(renderDelayCmp, ptrRenderDelayCmp);
		InitConcommandBase(spt_disable_render_delay);
	}

	SptImGuiGroup::QoL_Visual.RegisterUserCallback(
	    []()
	    {
		    SptImGui::CvarCheckbox(y_spt_disable_fade, "##checkbox_fade");
		    SptImGui::CvarCheckbox(y_spt_disable_shake, "##checkbox_shake");
		    SptImGui::CvarCheckbox(y_spt_disable_tone_map_reset, "##checkbox_tone_map");
		    SptImGui::CvarCheckbox(spt_disable_render_delay, "##checkbox_render_delay");
		    SptImGui::CvarInputTextInteger(y_spt_override_tpose, "T-pose sequence override", "");

		    ConVar* cvars[] = {&spt_viewmodel_offset_x, &spt_viewmodel_offset_y, &spt_viewmodel_offset_z};
		    float f[ARRAYSIZE(cvars)];
		    SptImGui::CvarsDragScalar(cvars, f, ARRAYSIZE(cvars), true, "viewmodel offset", 0.05f);
		    ImGui::SameLine();
		    SptImGui::HelpMarker("Can be set with spt_viewmodel_offset_x/y/z");
	    });
}

void VisualFixes::UnloadFeature()
{
	DESTROY_BYTE_REPLACE(renderDelayCmp);
}

void __cdecl VisualFixes::HOOKED_DoImageSpaceMotionBlur(void* view, int x, int y, int w, int h)
{
	uintptr_t origgpGlobals = NULL;

	/*
	Replace gpGlobals with (gpGlobals + 12). gpGlobals->realtime is the first variable,
	so it is located at gpGlobals. (gpGlobals + 12) is gpGlobals->curtime. This
	function does not use anything apart from gpGlobals->realtime from gpGlobals,
	so we can do such a replace to make it use gpGlobals->curtime instead without
	breaking anything else.
	*/
	if (spt_visual_fixes.pgpGlobals)
	{
		if (y_spt_motion_blur_fix.GetBool())
		{
			origgpGlobals = *spt_visual_fixes.pgpGlobals;
			*spt_visual_fixes.pgpGlobals = *spt_visual_fixes.pgpGlobals + 12;
		}
	}

	spt_visual_fixes.ORIG_DoImageSpaceMotionBlur(view, x, y, w, h);

	if (spt_visual_fixes.pgpGlobals)
	{
		if (y_spt_motion_blur_fix.GetBool())
		{
			*spt_visual_fixes.pgpGlobals = origgpGlobals;
		}
	}
}

void __fastcall VisualFixes::HOOKED_CViewEffects__Fade(void* thisptr, int edx, void* data)
{
	if (!y_spt_disable_fade.GetBool())
		spt_visual_fixes.ORIG_CViewEffects__Fade(thisptr, edx, data);
}

void __fastcall VisualFixes::HOOKED_CViewEffects__Shake(void* thisptr, int edx, void* data)
{
	if (!y_spt_disable_shake.GetBool())
		spt_visual_fixes.ORIG_CViewEffects__Shake(thisptr, edx, data);
}

void __cdecl VisualFixes::HOOKED_ResetToneMapping(float value)
{
	if (!y_spt_disable_tone_map_reset.GetBool())
		spt_visual_fixes.ORIG_ResetToneMapping(value);
}

#ifndef OE
void __fastcall VisualFixes::HOOKED_C_BaseAnimating__SetSequence(void* thisptr, int edx, int nSequence)
{
	if (nSequence == 0 && thisptr == utils::spt_clientEntList.GetPlayer()) // t-pose player
		nSequence = y_spt_override_tpose.GetInt();
	spt_visual_fixes.ORIG_C_BaseAnimating__SetSequence(thisptr, edx, nSequence);
}
#endif

IMPL_HOOK_THISCALL(VisualFixes,
                   void,
                   C_BaseViewModel__CalcViewModelView,
                   void*,
                   void* owner,
                   const Vector& eyePosition,
                   const QAngle& eyeAngles)
{
	Vector vmorigin = eyePosition;

	Vector vecRight;
	Vector vecUp;
	Vector vecForward;
	AngleVectors(eyeAngles, &vecForward, &vecRight, &vecUp);

	vmorigin += (vecForward * spt_viewmodel_offset_y.GetFloat()) + (vecUp * spt_viewmodel_offset_z.GetFloat())
	            + (vecRight * spt_viewmodel_offset_x.GetFloat());

	spt_visual_fixes.ORIG_C_BaseViewModel__CalcViewModelView(thisptr, owner, vmorigin, eyeAngles);
}

void VisualFixes::OnFinishRestore(void* thisptr)
{
	// I have no idea what this flag is, but setting it unconditionally seems to
	// (not entirely seamlessly) fix thirdperson not being preserved accross saveloads.
	*thirdPersonFlag = true;
}

void VisualFixes::ToggleRenderDelay(bool enabled)
{
	if (enabled)
	{
		DO_BYTE_REPLACE(renderDelayCmp);
	}
	else
	{
		UNDO_BYTE_REPLACE(renderDelayCmp);
	}
}
