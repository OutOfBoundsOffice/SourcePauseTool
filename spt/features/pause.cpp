#include "stdafx.hpp"
#include "..\feature.hpp"
#include "generic.hpp"
#include "convar.hpp"
#include "signals.hpp"
#include "visualizations\imgui\imgui_interface.hpp"
#include "tas.hpp"
#include "game_detection.hpp"

ConVar y_spt_pause("y_spt_pause", "0", FCVAR_ARCHIVE);

// spt_pause stuff
class PauseFeature : public FeatureWrapper<PauseFeature>
{
public:
protected:
	virtual void InitHooks() override;
	virtual void PreHook() override;
	virtual void LoadFeature() override;

private:
	uintptr_t ORIG_SpawnPlayer = 0;
	bool* pM_bLoadgame = nullptr;
	void* pGameServer = nullptr;

	void SV_ActivateServer(bool result);
	void FinishRestore(void* thisptr);
	void SetPaused(void* thisptr, bool paused);
};

static PauseFeature spt_pause;

namespace patterns
{
	PATTERNS(
	    SpawnPlayer,
	    "5135",
	    "83 EC 14 80 3D ?? ?? ?? ?? 00 56 8B F1 74 ?? 6A 00 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? EB 23 8B 0D ?? ?? ?? ?? 8B 01 8B 96 ?? ?? ?? ?? 8B 40 0C 52 FF D0 8B 8E ?? ?? ?? ?? 51 E8 ?? ?? ?? ?? 83 C4 04 8B 46 0C 8B 56 04 8B 52 74 83 C0 01",
	    "5339",
	    "55 8B EC 83 EC 14 80 3D ?? ?? ?? ?? 00 56 8B F1 74 ?? 6A 00 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? EB 23 8B 0D ?? ?? ?? ?? 8B 01 8B 96 ?? ?? ?? ?? 8B 40 0C 52 FF D0 8B 8E ?? ?? ?? ?? 51 E8 ?? ?? ?? ?? 83 C4 04 8B 46 0C 8B 56 04 8B 52 74 40",
	    "4104",
	    "83 EC 14 80 3D ?? ?? ?? ?? 00 56 8B F1 74 ?? 6A 00 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? EB 23 8B 0D ?? ?? ?? ?? 8B 01 8B 96 ?? ?? ?? ?? 8B 40 0C 52 FF D0 8B 8E ?? ?? ?? ?? 51 E8 ?? ?? ?? ?? 83 C4 04 8B 46 0C 8B 56 04 8B 52 70 83 C0 01",
	    "2257546",
	    "55 8B EC 83 EC 14 80 3D ?? ?? ?? ?? 00 56 8B F1 74 ?? 6A 00 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? EB 1F 8B 0D ?? ?? ?? ?? FF B6 ?? ?? ?? ?? 8B 01 FF 50 0C FF B6 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 C4 04 8B 46 0C 8D 4E 04 40",
	    "2707",
	    "64 A1 00 00 00 00 6A FF 68 ?? ?? ?? ?? 50 64 89 25 00 00 00 00 83 EC 14 80 3D ?? ?? ?? ?? 00 56 8B F1 74 0E 6A 00 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? EB 21 8B 0D",
	    "6879",
	    "55 8B EC 83 EC 14 80 3D ?? ?? ?? ?? ?? 56 8B F1 74 0E 6A 00 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? EB 23");
}

void PauseFeature::InitHooks()
{
	FIND_PATTERN(engine, SpawnPlayer);
}

void PauseFeature::PreHook()
{
	pM_bLoadgame = nullptr;
	pGameServer = nullptr;

	if (ORIG_SpawnPlayer)
	{
		int ptnNumber = GetPatternIndex((void**)&ORIG_SpawnPlayer);

		switch (ptnNumber)
		{
		case 0: // 5135
		case 2: // 4104 is the same as 5135 here
			pM_bLoadgame = (*(bool**)(ORIG_SpawnPlayer + 5));
			pGameServer = (*(void**)(ORIG_SpawnPlayer + 18));
			break;
		case 1: // 5339
		case 3: // 2257546 is the same as 5339 here
		case 5: // 6879 is the same as 5339 here
			pM_bLoadgame = (*(bool**)(ORIG_SpawnPlayer + 8));
			pGameServer = (*(void**)(ORIG_SpawnPlayer + 21));
			break;
		case 4: // 2707
			pM_bLoadgame = (*(bool**)(ORIG_SpawnPlayer + 26));
			//pGameServer = (*(void **)(pSpawnPlayer + 21)); - We get this one from SV_ActivateServer in OE.
			break;
		default:
			Warning("Spawnplayer did not have a matching switch-case statement!\n");
			break;
		}

		DevMsg("m_bLoadGame is situated at %p.\n", pM_bLoadgame);

#if !defined(OE)
		DevMsg("pGameServer is %p.\n", pGameServer);
#endif
	}

	// SV_ActivateServer
	if (spt_generic.ORIG_SV_ActivateServer)
	{
		int ptnNumber = GetPatternIndex((void**)&spt_generic.ORIG_SV_ActivateServer);
		switch (ptnNumber)
		{
		case 3:
			if (utils::GetBuildNumber() <= 2707)
				pGameServer = (*(void**)((int)spt_generic.ORIG_SV_ActivateServer + 223));
			else
				pGameServer = (*(void**)((int)spt_generic.ORIG_SV_ActivateServer + 222));
			break;
		}

#if defined(OE)
		DevMsg("pGameServer is %p.\n", pGameServer);
#endif
	}
}

void PauseFeature::LoadFeature()
{
	SV_ActivateServerSignal.Connect(this, &PauseFeature::SV_ActivateServer);
	SetPausedSignal.Connect(this, &PauseFeature::SetPaused);
	FinishRestoreSignal.Connect(this, &PauseFeature::FinishRestore);

	bool pause1_works = spt_generic.ORIG_FinishRestore;
	bool pause2_works = ORIG_SpawnPlayer && spt_generic.ORIG_SV_ActivateServer;
	bool pause_works = spt_generic.ORIG_SetPaused && (pause1_works || pause2_works);

	if (!pause_works)
	{
		Warning("spt_pause has no effect.\n");
	}
	else
	{
		if (!pause1_works)
		{
			Warning("spt_pause 1 has no effect.\n");
		}

		if (!pause2_works)
		{
			Warning("spt_pause 2 has no effect.\n");
		}

		InitConcommandBase(y_spt_pause);

		SptImGuiGroup::Cheats_Misc_Pause.RegisterUserCallback(
		    [pause1_works, pause2_works]()
		    {
			    const char* opts[] = {
			        "Disabled",
			        pause1_works ? "Pause after load" : "N/A",
			        pause2_works ? "Pause during load" : "N/A",
			    };
			    SptImGui::CvarCombo(y_spt_pause, "##combo", opts, ARRAYSIZE(opts));
		    });
	}
}

void PauseFeature::SV_ActivateServer(bool result)
{
	DevMsg("Engine call: SV_ActivateServer() => %s;\n", (result ? "true" : "false"));

	if (tas_pause.GetBool())
	{
		tas_pause.SetValue(0);
	}

	if (spt_generic.ORIG_SetPaused && pM_bLoadgame && pGameServer)
	{
		if ((y_spt_pause.GetInt() >= 2) && *pM_bLoadgame)
		{
			spt_generic.ORIG_SetPaused((void*)pGameServer, 0, true);
			DevMsg("Pausing...\n");

			spt_generic.shouldPreventNextUnpause = true;
		}
	}
}

void PauseFeature::FinishRestore(void* thisptr)
{
	DevMsg("Engine call: FinishRestore();\n");

	if (spt_generic.ORIG_SetPaused && (y_spt_pause.GetInt() == 1))
	{
		spt_generic.ORIG_SetPaused(thisptr, 0, true);
		DevMsg("Pausing...\n");

		spt_generic.shouldPreventNextUnpause = true;
	}
}

void PauseFeature::SetPaused(void* thisptr, bool paused)
{
	if (pM_bLoadgame)
	{
		DevMsg("Engine call: SetPaused( %s ); m_bLoadgame = %s\n",
		       (paused ? "true" : "false"),
		       (*pM_bLoadgame ? "true" : "false"));
	}
	else
	{
		DevMsg("Engine call: SetPaused( %s );\n", (paused ? "true" : "false"));
	}
}
