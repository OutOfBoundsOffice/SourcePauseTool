#include "stdafx.hpp"
#include "autojump.hpp"

#include "basehandle.h"
#include "SDK\hl_movedata.h"
#include "convar.hpp"
#include "dbg.h"
#include "signals.hpp"
#include "game_detection.hpp"
#include "visualizations\imgui\imgui_interface.hpp"
#include "tas.hpp"

#ifdef OE
#include "mathlib.h"
#endif

AutojumpFeature spt_autojump;
ConVar y_spt_autojump("y_spt_autojump", "0", FCVAR_ARCHIVE | FCVAR_DEMO);
ConVar _y_spt_autojump_ensure_legit("_y_spt_autojump_ensure_legit", "1", FCVAR_CHEAT);
ConVar y_spt_jumpboost("y_spt_jumpboost",
                       "0",
                       FCVAR_CHEAT,
                       "Enables special game movement mechanic.\n"
                       "0 = Default.\n"
                       "1 = ABH movement mechanic.\n"
                       "2 = OE movement mechanic.\n"
                       "3 = No jumpboost. (multiplayer mode movement)");
ConVar y_spt_aircontrol("y_spt_aircontrol", "0", FCVAR_CHEAT, "Enables HL2 air control.");

namespace patterns
{
	PATTERNS(
	    CheckJumpButton,
	    "5135",
	    "83 EC 1C 56 8B F1 8B 4E 04 80 B9 04 0A 00 00 00 74 0E 8B 76 08 83 4E 28 02 32 C0 5E 83 C4 1C C3 D9 EE D8 91 70 0D 00 00",
	    "4104",
	    "83 EC 1C 56 8B F1 8B 4E 08 80 B9 C4 09 00 00 00 74 0E 8B 76 04 83 4E 28 02 32 C0 5E 83 C4 1C C3 D9 EE D8 91 30 0D 00 00",
	    "5339",
	    "55 8B EC 83 EC 20 56 8B F1 8B ?? 04 80 ?? 40 ?? 00 00 00 74 0E 8B ?? 08 83 ?? 28 02 32 C0 5E 8B E5 5D C3",
	    "2257546",
	    "55 8B EC 83 EC 18 56 8B F1 8B ?? 04 80 ?? ?? ?? 00 00 00 74 0E 8B ?? 08 83 ?? 28 02 32 C0 5E 8B E5 5D C3",
	    "2257546-hl1",
	    "55 8B EC 51 56 8B F1 57 8B 7E 04 85 FF 74 10 8B 07 8B CF 8B 80 ?? ?? ?? ?? FF D0 84 C0 75 02 33 FF",
	    "2707",
	    "83 EC 10 53 56 8B F1 8B 4E 08 8A 81 ?? ?? ?? ?? 84 C0 74 0F 8B 76 04 83 4E ?? 02 5E 32 C0 5B 83 C4 10 C3 D9 81 30 0C 00 00 D8 1D",
	    "2949",
	    "83 EC 14 56 8B F1 8B 4E 08 80 B9 30 09 00 00 00 0F 85 E1 00 00 00 D9 05 ?? ?? ?? ?? D9 81 70 0C 00 00",
	    "dmomm",
	    "81 EC ?? ?? ?? ?? 56 8B F1 8B 4E 10 80 B9 ?? ?? ?? ?? ?? 74 11 8B 76 0C 83 4E 28 02 32 C0 5E 81 C4",
	    "4044-episodic",
	    "83 EC 14 56 8B F1 8B 4E 08 80 B9 ?? ?? ?? ?? ?? 0F 85 ?? ?? ?? ?? D9 05 ?? ?? ?? ?? D9 81",
	    "6879",
	    "55 8B EC 83 EC 0C 56 8B F1 8B 4E 04 80 B9 ?? ?? ?? ?? ?? 74 07 32 C0 5E 8B E5 5D C3 53 BB",
	    "missinginfo1_4_7",
	    "55 8B EC 83 EC 44 56 89 4D D0 8B 45 D0 8B 48 08 81 C1 ?? ?? ?? ?? E8 ?? ?? ?? ?? 0F B6 C8 85 C9",
	    "te120",
	    "55 8B EC 83 EC 20 56 8B F1 8B ?? 04 80 ?? 48 ?? 00 00 00 74 0E 8B ?? 08 83 ?? 28 02 32 C0 5E 8B E5 5D C3",
	    "BMS-Retail-3",
	    "55 8B EC 83 EC 0C 56 8B F1 8B ?? 04 80 ?? ?? ?? 00 00 00 74 0E 8B ?? 08 83 ?? 28 02 32 C0 5E 8B E5 5D C3",
	    "missinginfo1_6",
	    "55 8B EC 83 EC ?? 56 8B F1 8B 4E 04 80 B9 ?? ?? ?? ?? ?? 74 ?? 8B ?? 08 83 ?? 28 02 32 C0 5E 8B E5");

	PATTERNS(
	    FinishGravity,
	    "bms",
	    "8B 51 04 F3 0F 10 82 7C 0D 00 00 0F 57 C9 0F 2E C1 9F F6 C4 44 7A 51 F3 0F 10 82 28 02 00 00 0F 2E C1 9F F6 C4 44",
	    "2707",
	    "8B 51 08 D9 05 ?? ?? ?? ?? D9 82 ?? ?? ?? ?? DA E9 DF E0 F6 C4 44 7A 4F D9 05 ?? ?? ?? ?? D9 82",
	    "4044",
	    "8B 51 08 D9 82 ?? ?? ?? ?? D8 1D ?? ?? ?? ?? DF E0 F6 C4 44 7A 4D D9 82 ?? ?? ?? ?? D8 1D",
	    "4104",
	    "D9 EE 8B 51 08 D8 92 ?? ?? ?? ?? DF E0 F6 C4 44 7A 3F D8 9A ?? ?? ?? ?? DF E0 F6 C4 44 7B 08 D9 82",
	    "5135",
	    "D9 EE 8B 51 04 D8 92 ?? ?? ?? ?? DF E0 F6 C4 44 7A 3F D8 9A ?? ?? ?? ?? DF E0 F6 C4 44 7B 08 D9 82",
	    "2257546",
	    "55 8B EC 51 0F 57 C9 57 8B F9 8B 57 04 F3 0F 10 82 ?? ?? ?? ?? 0F 2E C1 9F F6 C4 44 7A 47 F3 0F 10 82",
	    "6879",
	    "8B 51 04 F3 0F 10 8A ?? ?? ?? ?? 0F 57 C0 0F 2E C8 9F F6 C4 44 0F 8A ?? ?? ?? ?? F3 0F 10 8A",
	    "missinginfo1_4_7",
	    "55 8B EC 83 EC 0C 89 4D F8 8B 45 F8 8B 48 08 D9 05 ?? ?? ?? ?? D9 81 ?? ?? ?? ?? DA E9 DF E0 F6 C4 44",
	    "missinginfo1_6",
	    "8B 51 04 D9 82 ?? ?? ?? ?? D9 EE D9 C0 DD EA DF E0 DD D9 F6 C4 44 7A 3F D9 82 ?? ?? ?? ?? DA E9",
	    "1910503",
	    "55 8B EC 51 0F 57 C9 57 8B F9 8B 4F ?? F3 0F 10 81 ?? ?? ?? ?? 0F 2E C1 9F",
	    "7197370",
	    "55 8B EC 51 F3 0F 10 0D ?? ?? ?? ?? 57 8B F9 8B 57 ?? F3 0F 10 82 ?? ?? ?? ?? 0F 2E C1 9F");
	PATTERNS(
	    CheckJumpButton_client,
	    "5135",
	    "83 EC 18 56 8B F1 8B 4E 04 80 B9 74 0F 00 00 00 74 0E 8B 76 08 83 4E 28 02 32 C0 5E 83 C4 18 C3 D9 EE D8 91 6C 10 00 00",
	    "4104",
	    "83 EC 18 56 8B F1 8B 4E 08 80 B9 3C 0F 00 00 00 74 0E 8B 76 04 83 4E 28 02 32 C0 5E 83 C4 18 C3 D9 EE D8 91 30 10 00 00",
	    "5339",
	    "55 8B EC 83 EC 1C 56 8B F1 8B 4E 04 80 B9 24 10 00 00 00 74 0E 8B 76 08 83 4E 28 02 32 C0 5E 8B E5 5D C3 F3 0F 10 89 20 11 00 00",
	    "2257546",
	    "55 8B EC 83 EC 14 56 8B F1 8B 4E 04 80 B9 ?? ?? ?? ?? ?? 74 0E 8B 46 08 83 48 28 02 32 C0 5E 8B E5",
	    "2257546-hl1",
	    "55 8B EC 51 56 8B F1 57 8B 7E 04 85 FF 74 2B 8B 07 8B CF 8B 80 ?? ?? ?? ?? FF D0 84 C0 74 1B 6A 00",
	    "2707",
	    "83 EC 10 53 56 8B F1 8B 4E 08 8A 81 ?? ?? ?? ?? 84 C0 74 0F 8B 76 04 83 4E 28 02 5E 32 C0 5B 83 C4 10",
	    "4044-episodic",
	    "83 EC 10 56 8B F1 8B 4E 08 80 B9 ?? ?? ?? ?? ?? 0F 85 ?? ?? ?? ?? D9 05 ?? ?? ?? ?? D9 81",
	    "1910503",
	    "55 8B EC 83 EC 14 56 8B F1 8B 4E 04 80 B9 ?? ?? ?? ?? ?? 74 0E 8B 76 08 83 4E 28 02 32 C0 5E 8B E5",
	    "6879",
	    "55 8B EC 83 EC 0C 56 8B F1 8B 4E 04 80 B9 ?? ?? ?? ?? ?? 74 07 32 C0 5E 8B E5 5D C3 53 BB",
	    "missinginfo1_4_7",
	    "55 8B EC 83 EC 3C 56 89 4D D8 8B 45 D8 8B 48 08 81 C1 ?? ?? ?? ?? E8 ?? ?? ?? ?? 0F B6 C8 85 C9",
	    "missinginfo1_6",
	    "55 8B EC 83 EC 18 56 8B F1 8B 4E 04 80 B9 ?? ?? ?? ?? ?? 74 0E 8B 76 08 83 4E 28 02 32 C0 5E 8B E5");
	PATTERNS(
	    CPortalGameMovement__AirMove,
	    "5135",
	    "83 EC 38 56 8B F1 8D 44 24 ?? 50 8B 46 ?? 8D 4C 24 ?? 51 8D 54 24 ?? 52 83 C0 0C 50 E8 ?? ?? ?? ?? 8B 46 ?? D9 40 ?? 83 C4 10 D9 5C 24 ?? 8D 4C 24 ?? D9 40 ?? D9 5C 24 ?? D9 EE D9 54 24 ?? D9 5C 24 ?? FF 15 ?? ?? ?? ?? DD D8 8D 4C 24 ?? FF 15 ?? ?? ?? ?? DD D8 D9 44 24 ?? 8B 46 ??",
	    "1910593",
	    "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B ?? 89 6C 24 ?? 8B EC 83 EC 78 56 57 8B F1",
	    "7197370",
	    "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B ?? 89 6C 24 ?? 8B EC 83 EC 68 56 57 8D 45 ?? 8B F1");
	PATTERNS(
	    CGameMovement__AirMove,
	    "5135",
	    "83 EC 38 56 8B F1 8D 44 24 ?? 50 8B 46 ?? 8D 4C 24 ?? 51 8D 54 24 ?? 52 83 C0 0C 50 E8 ?? ?? ?? ?? 8B 46 ?? D9 40 ?? 83 C4 10 D9 5C 24 ?? 8D 4C 24 ?? D9 40 ?? D9 5C 24 ?? D9 EE D9 54 24 ?? D9 5C 24 ?? FF 15 ?? ?? ?? ?? DD D8 8D 4C 24 ?? FF 15 ?? ?? ?? ?? DD D8 D9 44 24 ?? 8D 4C 24 ??",
	    "1910593",
	    "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B ?? 89 6C 24 ?? 8B EC 83 EC 5C 56 8B F1 8D 45 ??",
	    "7197370",
	    "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B ?? 89 6C 24 ?? 8B EC 83 EC 6C 56 8D 45 ??");
} // namespace patterns

void AutojumpFeature::PreHook()
{
	ptrCheckJumpButton = (uintptr_t)ORIG_CheckJumpButton;
}

void AutojumpFeature::InitHooks()
{
	HOOK_FUNCTION(server, CheckJumpButton);
	HOOK_FUNCTION(client, CheckJumpButton_client);
	HOOK_FUNCTION(server, FinishGravity);
	if (utils::DoesGameLookLikePortal())
	{
		HOOK_FUNCTION(server, CPortalGameMovement__AirMove);
		FIND_PATTERN(server, CGameMovement__AirMove);
	}
}

bool AutojumpFeature::ShouldLoadFeature()
{
	return spt_entprops.ShouldLoadFeature();
}

void AutojumpFeature::LoadFeature()
{
	// Server-side CheckJumpButton
	if (ORIG_CheckJumpButton)
	{
		InitConcommandBase(y_spt_autojump);
		InitConcommandBase(_y_spt_autojump_ensure_legit);
		JumpSignal.Works = true;
		int ptnNumber = GetPatternIndex((void**)&ORIG_CheckJumpButton);

		if (utils::DoesGameLookLikeSteampipe() || 
			utils::DoesGameLookLikeBMSRetail() || 
			utils::GetBuildNumber() >= 5135)
		{
			off_mv_ptr = 2;
			off_player_ptr = 1;
		}
		else if (utils::DoesGameLookLikeDMoMM())
		{
			off_mv_ptr = 3;
			off_player_ptr = 4;
		}
		else
		{
			off_mv_ptr = 1;
			off_player_ptr = 2;
		}

		SptImGuiGroup::Cheats_Misc_Jumping.RegisterUserCallback(
		    []()
		    {
			    SptImGui::CvarCheckbox(y_spt_autojump, "##checkbox_autojump");
			    SptImGui::CvarCheckbox(_y_spt_autojump_ensure_legit, "##checkbox_legit");
			    const char* opts[] = {"Default", "ABH jumpboost", "OE jumpboost", "No jumpboost"};
			    SptImGui::CvarCombo(y_spt_jumpboost, "jumpboost type", opts, ARRAYSIZE(opts));
		    });
	}
	else
	{
		Warning("spt_autojump has no effect.\n");
	}

	if (ORIG_FinishGravity)
	{
		InitConcommandBase(y_spt_jumpboost);
		m_bDucked = spt_entprops.GetPlayerField<bool>("m_Local.m_bDucked");
	}

	if (utils::DoesGameLookLikePortal() && ORIG_CGameMovement__AirMove && ORIG_CPortalGameMovement__AirMove)
	{
		InitConcommandBase(y_spt_aircontrol);
		SptImGuiGroup::Cheats_Misc_HL2AirControl.RegisterUserCallback(
		    []() { SptImGui::CvarCheckbox(y_spt_aircontrol, "##checkbox"); });
	}
}

void AutojumpFeature::UnloadFeature()
{
	ptrCheckJumpButton = NULL;
}

static Vector old_vel(0.0f, 0.0f, 0.0f);

IMPL_HOOK_THISCALL(AutojumpFeature, bool, CheckJumpButton, void*)
{
	const int IN_JUMP = (1 << 1);

	int* pM_nOldButtons = NULL;
	int origM_nOldButtons = 0;

	CHLMoveData* mv = (CHLMoveData*)(*((uintptr_t*)thisptr + spt_autojump.off_mv_ptr));
	if (tas_log.GetBool())
		DevMsg("[CheckJumpButton PRE ] origin: %.8f %.8f %.8f; velocity: %.8f %.8f %.8f\n",
		       mv->GetAbsOrigin().x,
		       mv->GetAbsOrigin().y,
		       mv->GetAbsOrigin().z,
		       mv->m_vecVelocity.x,
		       mv->m_vecVelocity.y,
		       mv->m_vecVelocity.z);

	if (y_spt_autojump.GetBool())
	{
		pM_nOldButtons = &mv->m_nOldButtons;
		origM_nOldButtons = *pM_nOldButtons;

		if (!spt_autojump.cantJumpNextTime) // Do not do anything if we jumped on the previous tick.
		{
			*pM_nOldButtons &= ~IN_JUMP; // Reset the jump button state as if it wasn't pressed.
		}
	}

	spt_autojump.cantJumpNextTime = false;

	spt_autojump.insideCheckJumpButton = true;
	old_vel = mv->m_vecVelocity;
	bool rv = spt_autojump.ORIG_CheckJumpButton(thisptr); // This function can only change the jump bit.
	spt_autojump.insideCheckJumpButton = false;

	if (y_spt_autojump.GetBool())
	{
		if (!(*pM_nOldButtons & IN_JUMP)) // CheckJumpButton didn't change anything (we didn't jump).
		{
			*pM_nOldButtons = origM_nOldButtons; // Restore the old jump button state.
		}
	}

	if (rv)
	{
		JumpSignal();
		// We jumped.
		if (_y_spt_autojump_ensure_legit.GetBool())
		{
			spt_autojump.cantJumpNextTime = true; // Prevent consecutive jumps.
		}
	}

	if (tas_log.GetBool())
		DevMsg("[CheckJumpButton POST] origin: %.8f %.8f %.8f; velocity: %.8f %.8f %.8f\n",
		       mv->GetAbsOrigin().x,
		       mv->GetAbsOrigin().y,
		       mv->GetAbsOrigin().z,
		       mv->m_vecVelocity.x,
		       mv->m_vecVelocity.y,
		       mv->m_vecVelocity.z);

	return rv;
}

IMPL_HOOK_THISCALL(AutojumpFeature, bool, CheckJumpButton_client, void*)
{
	/*
	Not sure if this gets called at all from the client dll, but
	I will just hook it in exactly the same way as the server one.
	*/
	const int IN_JUMP = (1 << 1);

	int* pM_nOldButtons = NULL;
	int origM_nOldButtons = 0;

	CHLMoveData* mv = (CHLMoveData*)(*((uintptr_t*)thisptr + spt_autojump.off_mv_ptr));
	if (y_spt_autojump.GetBool())
	{
		pM_nOldButtons = &mv->m_nOldButtons;
		origM_nOldButtons = *pM_nOldButtons;

		if (!spt_autojump.client_cantJumpNextTime) // Do not do anything if we jumped on the previous tick.
		{
			*pM_nOldButtons &= ~IN_JUMP; // Reset the jump button state as if it wasn't pressed.
		}
	}

	spt_autojump.client_cantJumpNextTime = false;

	spt_autojump.client_insideCheckJumpButton = true;
	bool rv = spt_autojump.ORIG_CheckJumpButton_client(thisptr); // This function can only change the jump bit.
	spt_autojump.client_insideCheckJumpButton = false;

	if (y_spt_autojump.GetBool())
	{
		if (!(*pM_nOldButtons & IN_JUMP)) // CheckJumpButton didn't change anything (we didn't jump).
		{
			*pM_nOldButtons = origM_nOldButtons; // Restore the old jump button state.
		}
	}

	if (rv)
	{
		// We jumped.
		if (_y_spt_autojump_ensure_legit.GetBool())
		{
			spt_autojump.client_cantJumpNextTime = true; // Prevent consecutive jumps.
		}
	}

	DevMsg("Engine call: [client dll] CheckJumpButton() => %s\n", (rv ? "true" : "false"));

	return rv;
}

IMPL_HOOK_THISCALL(AutojumpFeature, void, FinishGravity, void*)
{
	if (spt_autojump.insideCheckJumpButton && y_spt_jumpboost.GetBool())
	{
		CHLMoveData* mv = (CHLMoveData*)(*((uintptr_t*)thisptr + spt_autojump.off_mv_ptr));
		bool ducked = spt_autojump.m_bDucked.GetValue();

		// Reset
		mv->m_vecVelocity.x = old_vel.x;
		mv->m_vecVelocity.y = old_vel.y;

		// <stolen from gamemovement.cpp>
		{
			Vector vecForward;
			AngleVectors(mv->m_vecViewAngles, &vecForward);
			vecForward.z = 0;
			VectorNormalize(vecForward);

			// We give a certain percentage of the current forward movement as a bonus to the jump speed.  That bonus is clipped
			// to not accumulate over time.
			float flSpeedBoostPerc = (!mv->m_bIsSprinting && !ducked) ? 0.5f : 0.1f;
			float flSpeedAddition = fabs(mv->m_flForwardMove * flSpeedBoostPerc);
			float flMaxSpeed = mv->m_flMaxSpeed + (mv->m_flMaxSpeed * flSpeedBoostPerc);
			float flNewSpeed = (flSpeedAddition + mv->m_vecVelocity.Length2D());

			// If we're over the maximum, we want to only boost as much as will get us to the goal speed
			switch (y_spt_jumpboost.GetInt())
			{
			case 1:
				if (flNewSpeed > flMaxSpeed)
				{
					flSpeedAddition -= flNewSpeed - flMaxSpeed;
				}

				if (mv->m_flForwardMove < 0.0f)
					flSpeedAddition *= -1.0f;
				vecForward *= flSpeedAddition;
				break;
			case 2:
				vecForward *= mv->m_flForwardMove * flSpeedBoostPerc;
				break;
			}
			// Add it on
			VectorAdd(vecForward, mv->m_vecVelocity, mv->m_vecVelocity);
		}
		// </stolen from gamemovement.cpp>
	}

	return spt_autojump.ORIG_FinishGravity(thisptr);
}

IMPL_HOOK_THISCALL(AutojumpFeature, void, CPortalGameMovement__AirMove, void*)
{
	if (y_spt_aircontrol.GetBool())
	{
		return spt_autojump.ORIG_CGameMovement__AirMove(thisptr);
	}
	return spt_autojump.ORIG_CPortalGameMovement__AirMove(thisptr);
}
