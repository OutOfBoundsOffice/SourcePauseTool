#include "stdafx.hpp"

#if !defined(OE)

#include "generic.hpp"
#include "..\feature.hpp"
#include "..\sptlib-wrapper.hpp"
#include "..\cvars.hpp"
#include "..\spt-serverplugin.hpp"
#include "..\utils\ent_utils.hpp"
#include "..\utils\ent_list.hpp"
#include "create_collide.hpp"
#include "signals.hpp"
#include "interfaces.hpp"

#define GAME_DLL
#include "cbase.h"

#include <sstream>

ConVar y_spt_stucksave("y_spt_stucksave", "", FCVAR_TAS_RESET, "Automatically saves when you get stuck in a prop.\n");
ConVar y_spt_piwsave("y_spt_piwsave", "", FCVAR_TAS_RESET, "Automatically saves when you push a prop into a wall.\n");

typedef int(__fastcall* _CheckStuck)(void* thisptr, int edx);

// Implements saving when the player gets stuck, enabled with spt_stucksave
class Stucksave : public FeatureWrapper<Stucksave>
{
public:
protected:
	virtual bool ShouldLoadFeature() override;

	virtual void InitHooks() override;

	virtual void LoadFeature() override;

	virtual void UnloadFeature() override;

private:
	_CheckStuck ORIG_CheckStuck = nullptr;

	static int __fastcall HOOKED_CheckStuck(void* thisptr, int edx);
	static void CheckPiwSave(bool simulating);
};

static Stucksave spt_stucksave;

bool Stucksave::ShouldLoadFeature()
{
	// Can't run commands without EngineConCmd
	return interfaces::engine_client != nullptr;
}

namespace patterns
{
	PATTERNS(
	    CheckStuck,
	    "5135",
	    "81 EC 80 00 00 00 56 8B F1 C7 44 24 04 FF FF FF FF E8 ?? ?? ?? ?? 8B 46 08 8B 16 8B 92 BC 00 00 00 8D 4C 24 30 51 05 98 00 00 00 6A 08 50 8D 44",
	    "2707",
	    "81 EC ?? ?? ?? ?? 53 56 8B F1 57 8D 4C 24 6C C7 44 24 ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? E8",
	    "4104",
	    "81 EC ?? ?? ?? ?? 56 8B F1 C7 44 24 ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B 56 04 8B 06 8D 4C 24 30 51",
	    "ghosting",
	    "55 8B EC 81 EC 80 00 00 00 56 8B F1 C7 45 FC FF FF FF FF E8 ?? ?? ?? ?? 8B 46 08 8B 16 8D 4D 80 51 05 98 00 00 00 6A 08 50 8D 45 EC 50 8B CE FF",
	    "1910503",
	    "55 8B EC 81 EC 80 00 00 00 57 8B F9 E8 ?? ?? ?? ?? 85 C0 0F 84 47 02 00 00 56 8B 77 04 8B 06 8B",
	    "BMS-Retail",
	    "55 8B EC 81 EC 84 00 00 00 A1 ?? ?? ?? ?? 33 C5 89 45 FC 56 8B F1 C7 45 A0 FF FF FF FF E8",
	    "BMS-Retail-2",
	    "55 8b ec 81 ec 84 00 00 00 a1 ?? ?? ?? ?? 33 c5 89 45 fc 56 8b f1 c7 45 a4 ff ff ff ff e8 ?? ?? ?? ?? 8b 46 08",
	    "4044-episodic",
	    "81 EC ?? ?? ?? ?? 56 57 8B F1 C7 44 24 ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B 4E 04 8D 44 24 34 50 6A 08",
	    "2257546",
	    "55 8B EC 81 EC ?? ?? ?? ?? 56 8B F1 C7 45 ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B 46 08 8D 4D 80 8B 16",
	    "6879",
	    "55 8B EC 81 EC ?? ?? ?? ?? 53 56 57 8B F1 C7 45 ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B 46 08 8B 16 8B 92",
	    "missinginfo1_4_7",
	    "55 8B EC 81 EC ?? ?? ?? ?? 89 8D ?? ?? ?? ?? 8D 4D EC E8 ?? ?? ?? ?? 8D 4D 80 E8 ?? ?? ?? ?? 8D 8D",
	    "missinginfo1_6",
	    "55 8B EC 81 EC ?? ?? ?? ?? 56 8B F1 C7 45 ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B 46 08 8B 16 8B 92");
}

void Stucksave::InitHooks()
{
	HOOK_FUNCTION(server, CheckStuck);
}

void Stucksave::LoadFeature()
{
	TickSignal.Connect(CheckPiwSave);

	// CheckStuck
	if (ORIG_CheckStuck)
	{
		InitConcommandBase(y_spt_stucksave);
	}

	if (TickSignal.Works)
	{
		InitConcommandBase(y_spt_piwsave);
	}
}

void Stucksave::UnloadFeature() {}

int __fastcall Stucksave::HOOKED_CheckStuck(void* thisptr, int edx)
{
	auto ret = spt_stucksave.ORIG_CheckStuck(thisptr, edx);

	if (ret && y_spt_stucksave.GetString()[0] != '\0')
	{
		std::ostringstream oss;
		oss << "save " << y_spt_stucksave.GetString();
		EngineConCmd(oss.str().c_str());
		y_spt_stucksave.SetValue("");
	}

	return ret;
}

void Stucksave::CheckPiwSave(bool simulating)
{
	if (!simulating || y_spt_piwsave.GetString()[0] == '\0')
		return;

	IPhysicsObject* pphys = spt_collideToMesh.GetPhysObj(utils::spt_serverEntList.GetPlayer());
	if (!pphys || (pphys->GetGameFlags() & FVPHYSICS_PENETRATING))
		return;

	for (auto ent : utils::spt_serverEntList.GetEntList())
	{
		auto phys = spt_collideToMesh.GetPhysObj(ent);
		if (!phys)
			continue;

		const auto mask = FVPHYSICS_PLAYER_HELD | FVPHYSICS_PENETRATING;

		if ((pphys->GetGameFlags() & mask) == mask)
		{
			std::ostringstream oss;
			oss << "save " << y_spt_piwsave.GetString();
			EngineConCmd(oss.str().c_str());
			y_spt_piwsave.SetValue("");
		}
	}
}

#endif
