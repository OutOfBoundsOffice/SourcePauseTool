#include "stdafx.hpp"
#include "game_detection.hpp"
#include "rng.hpp"
#include "tier1/checksum_md5.h"
#include "cmodel.h"
#include "spt\cvars.hpp"
#include "vstdlib/random.h"
#include "features/afterticks.hpp"
#include "eiface.h"
#ifdef OE
#include "..\game_shared\usercmd.h"
#else
#include "usercmd.h"
#endif

//#define SPT_DEBUG_RNG_CALLERS 1

typedef void (*Func_V_I)(int i);
typedef float (*Func_F_FF)(float f, float f2);
typedef float (*Func_F_FFF)(float f, float f2, float f3);
typedef int (*Func_I_II)(int i, int i2);

Func_V_I _RandomSeed = nullptr;
Func_F_FF _RandomFloat = nullptr;
Func_F_FFF _RandomFloatExp = nullptr;
Func_I_II _RandomInt = nullptr;

ConVar y_spt_set_ivp_seed_on_load(
    "y_spt_set_ivp_seed_on_load",
    "",
    FCVAR_CHEAT,
    "Sets the ivp seed once during the next load, can prevent some physics rng when running a tas.");

ConVar spt_set_physics_hook_offset_on_load(
    "spt_set_physics_hook_offset_on_load",
    "",
    FCVAR_CHEAT,
    "Sets the offset of the physics hook timer once during the next load; this may contribute to the uniform random stream.\n"
    "Valid values are integer multiples of the tickrate in [0,0.05f].");

ConVar spt_set_all_sounds_available_after_load(
    "spt_set_all_sounds_available_after_load",
    "0",
    FCVAR_CHEAT | FCVAR_TAS_RESET,
    "Set to 1 for consistent sound rng, which may contribute to the uniform random stream. Useful for new TAS scripts, but may break old scripts.");

ConVar spt_set_game_seed_on_load(
    "spt_set_game_seed_on_load",
    "",
    FCVAR_NONE,
    "Sets the game seed once during the next load.");

ConVar spt_use_separate_rng(
	"spt_use_separate_rng",
    "",
    FCVAR_NONE,
    "Separates RNG for server and client, helps with consistency in tas.");

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

namespace interfaces
{
	extern IUniformRandomStream* serverRandomStream;
	extern IVEngineServer* _engine_server;
}

static void SetFuncIfFound(void** pTarget, void* func)
{
	if (func)
	{
		*pTarget = func;
	}
}

HMODULE CLIENT_MODULE;
HMODULE SERVER_MODULE;

#define STACK_DEPTH 64

int ucIndex = 1;
HMODULE unknownCallers[64];

HMODULE GetFunctionCaller()
{
	void* stack[STACK_DEPTH];
	int count = CaptureStackBackTrace(2, STACK_DEPTH, stack, nullptr);

	HMODULE module = nullptr;

	for (int i = 0; i < STACK_DEPTH; i++)
	{
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)stack[i], &module);

		if (module == CLIENT_MODULE || module == SERVER_MODULE)
		{
			return module;
		}
	}
	
	bool checked = false;

	for (int i = 0; i < ucIndex; i++)
	{
		if (unknownCallers[i] == module)
		{
			checked = true;
			break;
		}
			
	}

	//Only print this once per unknown function caller as to not spam the console
	if (!checked)
	{
		unknownCallers[ucIndex++] = module;
		DevWarning("spt: GetFunctionCaller unknown function caller.\n");
		for (int i = 0; i < count; i++)
		{
			HMODULE module = nullptr;
			GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)stack[i], &module);
			char name[MAX_PATH];
						
			GetModuleFileName(module, name, MAX_PATH);
			DevWarning("stacktrace %i: %s\n", i, name);
		}
		DevWarning("\n");

		//We will probably never get these many unknown calls but reset the index just to be safe =)
		if (ucIndex == 64)
		{
			ucIndex = 0;
		}
	}
	
	return (HMODULE)NULL;
}

CUniformRandomStream serverStream;
CUniformRandomStream clientStream;

#ifdef SPT_DEBUG_RNG_CALLERS
HMODULE SPT_MODULE = GetModuleHandleA("spt-2013.dll");
int lastcall = 0;
#endif

void RandomSeedNew(int seed)
{
#ifdef SPT_DEBUG_RNG_CALLERS
	void* stack[STACK_DEPTH];
	int count = CaptureStackBackTrace(1, STACK_DEPTH, stack, nullptr);

	HMODULE module = nullptr;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)stack[0], &module);

	if (module != SPT_MODULE)
	{
		void* stack[STACK_DEPTH];
		int count = CaptureStackBackTrace(1, STACK_DEPTH, stack, nullptr);

		int now = interfaces::_engine_server->GetServerTime() / 0.015;
		DevWarning("spt: RandomSeed called from outside spt: %i (last call %i ticks ago) %i.\n",
		           seed,
		           now - lastcall,
		                        now);
		lastcall = now;
	}
#endif
	if (GetFunctionCaller() == SERVER_MODULE)
	{
		serverStream.SetSeed(seed);
	}
	else if(GetFunctionCaller() == CLIENT_MODULE)
	{
		clientStream.SetSeed(seed);
	}
}
	
float RandomFloatNew(float min, float max)
{
	if (GetFunctionCaller() == SERVER_MODULE)
	{
		return serverStream.RandomFloat(min, max);
	}
	else if (GetFunctionCaller() == CLIENT_MODULE)
	{
		return clientStream.RandomFloat(min, max);
	}

	return 0.0f;
}

#ifndef OE
float RandomFloatExpNew(float min, float max, float exp)
{
	if (GetFunctionCaller() == SERVER_MODULE)
	{
		return serverStream.RandomFloatExp(min, max, exp);
	}
	else if (GetFunctionCaller() == CLIENT_MODULE)
	{
		return clientStream.RandomFloatExp(min, max, exp);
	}

	return 0.0f;
}
#endif

int RandomIntNew(int min, int max)
{
	if (GetFunctionCaller() == SERVER_MODULE)
	{
		return serverStream.RandomInt(min, max);
	}
	else if (GetFunctionCaller() == CLIENT_MODULE)
	{
		return clientStream.RandomInt(min, max);
	}

	return 0;
}

//Old opcodes for the random functions, used to restore them when disabling the feature
bool bStoredRNGOps = false;

byte oldops_RS[5];
byte oldops_RF[5];
#ifndef OE
byte oldops_RFE[5];
#endif
byte oldops_RI[5];

bool bUsingSeparateRNG = false;

//Detour random functions
void DetourRandom()
{
	DWORD oldProtect;

	VirtualProtect((void*)_RandomSeed, 5, PAGE_EXECUTE_READWRITE, &oldProtect);

	if (!bStoredRNGOps)
		for (int i = 0; i < 5; i++)
		{
			oldops_RS[i] = *(byte*)((uintptr_t)_RandomSeed + i);
		}

	*(byte*)_RandomSeed = 0xE9; // JMP
	*(DWORD*)((uintptr_t)_RandomSeed + 1) = (uintptr_t)RandomSeedNew - (uintptr_t)_RandomSeed - 5;

	VirtualProtect((void*)_RandomSeed, 5, oldProtect, &oldProtect);

	VirtualProtect((void*)_RandomFloat, 5, PAGE_EXECUTE_READWRITE, &oldProtect);

	if (!bStoredRNGOps)
		for (int i = 0; i < 5; i++)
		{
			oldops_RF[i] = *(byte*)((uintptr_t)_RandomFloat + i);
		}

	*(byte*)_RandomFloat = 0xE9; // JMP
	*(DWORD*)((uintptr_t)_RandomFloat + 1) = (uintptr_t)RandomFloatNew - (uintptr_t)_RandomFloat - 5;

	VirtualProtect((void*)_RandomFloat, 5, oldProtect, &oldProtect);

#ifndef OE
	VirtualProtect((void*)_RandomFloatExp, 5, PAGE_EXECUTE_READWRITE, &oldProtect);

	if (!bStoredRNGOps)
		for (int i = 0; i < 5; i++)
		{
			oldops_RFE[i] = *(byte*)((uintptr_t)_RandomFloatExp + i);
		}

	*(byte*)_RandomFloatExp = 0xE9; // JMP
	*(DWORD*)((uintptr_t)_RandomFloatExp + 1) = (uintptr_t)RandomFloatExpNew - (uintptr_t)_RandomFloatExp - 5;

	VirtualProtect((void*)_RandomFloatExp, 5, oldProtect, &oldProtect);
#endif
	VirtualProtect((void*)_RandomInt, 5, PAGE_EXECUTE_READWRITE, &oldProtect);

	if (!bStoredRNGOps)
		for (int i = 0; i < 5; i++)
		{
			oldops_RI[i] = *(byte*)((uintptr_t)_RandomInt + i);
		}

	*(byte*)_RandomInt = 0xE9; // JMP
	*(DWORD*)((uintptr_t)_RandomInt + 1) = (uintptr_t)RandomIntNew - (uintptr_t)_RandomInt - 5;

	VirtualProtect((void*)_RandomInt, 5, oldProtect, &oldProtect);

	bStoredRNGOps = true;
}

//Restore random functions
void RestoreRandom()
{	
	DWORD oldProtect;

	VirtualProtect((void*)_RandomSeed, 5, PAGE_EXECUTE_READWRITE, &oldProtect);

	for (int i = 0; i < 5; i++)
	{
		*(byte*)((uintptr_t)_RandomSeed + i) = oldops_RS[i];
	}

	VirtualProtect((void*)_RandomSeed, 5, oldProtect, &oldProtect);

	VirtualProtect((void*)_RandomFloat, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
	
	for (int i = 0; i < 5; i++)
	{
		*(byte*)((uintptr_t)_RandomFloat + i) = oldops_RF[i];
	}

	VirtualProtect((void*)_RandomFloat, 5, oldProtect, &oldProtect);

#ifndef OE
	VirtualProtect((void*)_RandomFloatExp, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
	
	for (int i = 0; i < 5; i++)
	{
		*(byte*)((uintptr_t)_RandomFloatExp + i) = oldops_RFE[i];
	}

	VirtualProtect((void*)_RandomFloatExp, 5, oldProtect, &oldProtect);
#endif
	VirtualProtect((void*)_RandomInt, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
	
	for (int i = 0; i < 5; i++)
	{
		*(byte*)((uintptr_t)_RandomInt + i) = oldops_RI[i];
	}

	VirtualProtect((void*)_RandomInt, 5, oldProtect, &oldProtect);
}

static void UseSeparateRNGCallback( ConVar* cvar, const char* pOld, float flOld )
{
	int iOld = (int)flOld;
	int iNew = cvar->GetInt();

	Msg("spt_use_separate rng changed old: %i new: %i", iOld, iNew);
	if (iOld == iNew or iNew > 1 or iNew < 0)
	{
		Msg("Usage: spt_use_separate_rng <0/1>\n");
		return;
	}

	bool useSeparateRNG = iNew == 1;

	if (useSeparateRNG == bUsingSeparateRNG)
		return;

	if (useSeparateRNG)
	{
		DetourRandom();
	}
	else
	{
		RestoreRandom();
	}

	bUsingSeparateRNG = useSeparateRNG;
}

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
		if (RandomSeed)
			InitConcommandBase(spt_set_game_seed_on_load);
	}

	// spt_use_separate_rng stuff below
	CLIENT_MODULE = GetModuleHandleA("client.dll");
	SERVER_MODULE = GetModuleHandleA("server.dll");
	
	HMODULE moduleHandleVstdlib = GetModuleHandleA("vstdlib.dll");

	if (moduleHandleVstdlib != NULL)
	{
		SetFuncIfFound((void**)&_RandomSeed, GetProcAddress(moduleHandleVstdlib, "RandomSeed"));
		SetFuncIfFound((void**)&_RandomFloat, GetProcAddress(moduleHandleVstdlib, "RandomFloat"));
		SetFuncIfFound((void**)&_RandomFloatExp, GetProcAddress(moduleHandleVstdlib, "RandomFloatExp"));
		SetFuncIfFound((void**)&_RandomInt, GetProcAddress(moduleHandleVstdlib, "RandomInt"));
	}

	//Make sure these are all valid
	if (_RandomSeed == NULL)
		return;
	if (_RandomFloat == NULL)
		return;
#ifndef OE
	if (_RandomFloatExp == NULL)
		return;
#endif
	if (_RandomInt == NULL)
		return;

	InitConcommandBase(spt_use_separate_rng);
	//spt_use_separate_rng.InstallChangeCallback(UseSeparateRNGCallback);
}

void RNGStuff::UnloadFeature()
{
	if (bStoredRNGOps)
	{
		RestoreRandom();
	}
}

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
	
	if (_RandomSeed)
	{
		if (spt_set_game_seed_on_load.GetString()[0] != '\0')
		{
			int seed = strtoul(spt_set_game_seed_on_load.GetString(), nullptr, 10);
			if (seed != 0)
			{
				_RandomSeed(seed);
				spt_set_game_seed_on_load.SetValue("");				
			}
		}
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
