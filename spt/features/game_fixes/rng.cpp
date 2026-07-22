#include "stdafx.hpp"
#include "game_detection.hpp"
#include "rng.hpp"
#include "tier1/checksum_md5.h"
#include "cmodel.h"
#include "spt\cvars.hpp"

#ifdef OE
#include "..\game_shared\usercmd.h"
#else
#include "usercmd.h"
#endif

ConVar y_spt_set_ivp_seed_on_load(
    "y_spt_set_ivp_seed_on_load",
    "",
    FCVAR_NONE,
    "Sets the ivp seed once during the next load, can prevent some physics rng when running a tas.");

ConVar spt_set_physics_hook_offset_on_load(
    "spt_set_physics_hook_offset_on_load",
    "",
    FCVAR_NONE,
    "Sets the offset of the physics hook timer once during the next load; this may contribute to the uniform random stream.\n"
    "Valid values are integer multiples of the tickrate in [0,0.05f].");

ConVar spt_set_all_sounds_available_after_load(
    "spt_set_all_sounds_available_after_load",
    "0",
    FCVAR_TAS_RESET,
    "Set to 1 for consistent sound rng, which may contribute to the uniform random stream. Useful for new TAS scripts, but may break old scripts.");

RNGStuff spt_rng;

namespace patterns
{
	PATTERNS(SetPredictionRandomSeed,
	         "5135",
	         "8B 44 24 ?? 85 C0 75 ?? C7 05 ?? ?? ?? ?? FF FF FF FF",
	         "hl1movement",
	         "55 8B EC 8B 45 ?? 85 C0 75 ?? C7 05 ?? ?? ?? ?? FF FF FF FF");
	PATTERNS(ivp_srand,
	         "5135",
	         "8B 44 24 04 85 C0 75 05 B8 01 00 00 00 A3 ?? ?? ?? ?? C3",
	         "7122284",
	         "55 8B EC 8B 45 08 B9 01 00 00 00 85 C0 0F 44 C1 A3 ?? ?? ?? ?? 5D C3");
	PATTERNS(CBasePlayer__InitVCollision,
	         "5135",
	         "57 8B F9 8B 07 8B 90 ?? ?? ?? ?? FF D2 A1 ?? ?? ?? ?? 83 78 30 00",
	         "7122284",
	         "55 8B EC 83 EC 34 57 8B F9 8B 07 FF 90 ?? ?? ?? ?? A1 ?? ?? ?? ?? 83 78 30 00",
	         "dmomm",
	         "57 8B F9 8B 07 FF 90 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? 83 79 ?? 00");
	PATTERNS(PhysFrame,
	         "5135",
	         "55 8B EC 83 EC 0C 83 3D ?? ?? ?? ?? 00 53 56 57 0F 84 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? 00");
	PATTERNS(CSoundEmitterSystemBase__EnsureAvailableSlotsForGender, "5135", "83 EC 14 55");
} // namespace patterns

void RNGStuff::InitHooks()
{
	HOOK_FUNCTION(server, SetPredictionRandomSeed);
	HOOK_FUNCTION(SoundEmitterSystem, CSoundEmitterSystemBase__EnsureAvailableSlotsForGender);
	if (!utils::DoesGameLookLikeBMSMod() && !utils::DoesGameLookLikeEstranged())
	{
		HOOK_FUNCTION(server, CBasePlayer__InitVCollision);
		FIND_PATTERN(vphysics, ivp_srand);
	}
	FIND_PATTERN(server, PhysFrame);
}

int RNGStuff::GetPredictionRandomSeed(int commandOffset)
{
	int command_number = spt_rng.commandNumber + commandOffset;
	return MD5_PseudoRandom(command_number) & 0x7fffffff;
}

bool RNGStuff::ShouldLoadFeature()
{
	return true;
}

void RNGStuff::PreHook()
{
	if (ORIG_ivp_srand)
	{
		uint32_t offs[] = {14, 17};
		int idx = GetPatternIndex((void**)&ORIG_ivp_srand);
		IVP_RAND_SEED = *(uint32_t**)((uintptr_t)ORIG_ivp_srand + offs[idx]);
	}
	if (ORIG_PhysFrame)
	{
		uint32_t offs[] = {24};
		int idx = GetPatternIndex((void**)&ORIG_PhysFrame);
		// PhysFrame() accesses m_bPaused which is the field immediately after m_impactSoundTime :)
		g_PhysicsHook__m_impactSoundTime = *(float**)((uintptr_t)ORIG_PhysFrame + offs[idx]) - 1;
	}
}

void RNGStuff::LoadFeature()
{
	if (ORIG_CBasePlayer__InitVCollision)
	{
		if (ORIG_ivp_srand && spt_rng.IVP_RAND_SEED)
			InitConcommandBase(y_spt_set_ivp_seed_on_load);
		if (g_PhysicsHook__m_impactSoundTime)
			InitConcommandBase(spt_set_physics_hook_offset_on_load);
		if (ORIG_CSoundEmitterSystemBase__EnsureAvailableSlotsForGender)
			InitConcommandBase(spt_set_all_sounds_available_after_load);
	}
}

void RNGStuff::UnloadFeature() {}

IMPL_HOOK_CDECL(RNGStuff, void, SetPredictionRandomSeed, void* usercmd)
{
	CUserCmd* ptr = reinterpret_cast<CUserCmd*>(usercmd);
	if (ptr)
	{
		spt_rng.commandNumber = ptr->command_number;
	}

	spt_rng.ORIG_SetPredictionRandomSeed(usercmd);
}

#ifdef OE
IMPL_HOOK_THISCALL(RNGStuff, void, CBasePlayer__InitVCollision, void*)
{
	spt_rng.ORIG_CBasePlayer__InitVCollision(thisptr);
#else
IMPL_HOOK_THISCALL(RNGStuff,
                   void,
                   CBasePlayer__InitVCollision,
                   void*,
                   const Vector& vecAbsOrigin,
                   const Vector& vecAbsVelocity)
{
	spt_rng.ORIG_CBasePlayer__InitVCollision(thisptr, vecAbsOrigin, vecAbsVelocity);
#endif
	if (spt_rng.ORIG_ivp_srand && spt_rng.IVP_RAND_SEED)
	{
		// set the seed before any vphys sim happens, don't use GetInt() since that's casted from a float
		if (y_spt_set_ivp_seed_on_load.GetString()[0] != '\0')
		{
			spt_rng.ORIG_ivp_srand((uint32_t)strtoul(y_spt_set_ivp_seed_on_load.GetString(), nullptr, 10));
			y_spt_set_ivp_seed_on_load.SetValue("");
		}
		DevWarning("spt: ivp seed is %u\n", *spt_rng.IVP_RAND_SEED);
	}

	if (spt_rng.g_PhysicsHook__m_impactSoundTime)
	{
		// same deal here, but we clamp the cvar value to [0,0.05f]
		if (spt_set_physics_hook_offset_on_load.GetString()[0] != '\0')
		{
			*spt_rng.g_PhysicsHook__m_impactSoundTime =
			    clamp(spt_set_physics_hook_offset_on_load.GetFloat(), 0, 0.05f);
			spt_set_physics_hook_offset_on_load.SetValue("");
		}
		DevWarning("spt: physics hook timer offset is %f\n", *spt_rng.g_PhysicsHook__m_impactSoundTime);
	}

	if (spt_rng.ORIG_CSoundEmitterSystemBase__EnsureAvailableSlotsForGender)
		spt_rng.resetSounds.clear();
}

IMPL_HOOK_THISCALL(RNGStuff,
                   void,
                   CSoundEmitterSystemBase__EnsureAvailableSlotsForGender,
                   void*,
                   SoundFile* pSoundnames,
                   int c,
                   gender_t gender)
{
	if (spt_rng.ORIG_CBasePlayer__InitVCollision && spt_set_all_sounds_available_after_load.GetBool())
	{
		// go through all sounds, and mark them as available if we haven't done so yet (since the start of the load)
		for (int i = 0; i < c; i++)
			if (spt_rng.resetSounds.insert(pSoundnames[i].symbol).second)
				pSoundnames[i].available = true;
	}
	spt_rng.ORIG_CSoundEmitterSystemBase__EnsureAvailableSlotsForGender(thisptr, pSoundnames, c, gender);
}
