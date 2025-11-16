#include "stdafx.hpp"

#include "portalled_pause.hpp"
#include "..\sptlib-wrapper.hpp"
#include "afterticks.hpp"
#include "ent_utils.hpp"
#include "game_detection.hpp"
#include "spt\utils\ent_list.hpp"
#include "interfaces.hpp"

ConVar spt_on_portalled_pause_for("spt_on_portalled_pause_for",
                                  "0",
                                  FCVAR_TAS_RESET | FCVAR_DONTRECORD,
                                  "Whenever player portalled, pause for this many ticks.");

namespace patterns
{
	PATTERNS(TeleportTouchingEntity,
	         "5135",
	         "81 EC 1C 01 00 00 55 8B E9 89 6C 24 ?? E8 ?? ?? ?? ?? 85 C0",
	         "7197370",
	         "55 8B EC 81 EC 28 01 00 00 56 8B F1");
}

bool PortalledPause::ShouldLoadFeature()
{
	return utils::DoesGameLookLikePortal();
}

void PortalledPause::InitHooks()
{
	HOOK_FUNCTION(server, TeleportTouchingEntity);
}

void PortalledPause::PreHook()
{
	if (ORIG_TeleportTouchingEntity)
		portalTeleportedEntitySignal.Works = true;
}

void PortalledPause::LoadFeature()
{
	if (ORIG_TeleportTouchingEntity && interfaces::engine_client != nullptr)
		InitConcommandBase(spt_on_portalled_pause_for);
}

IMPL_HOOK_THISCALL(PortalledPause, void, TeleportTouchingEntity, IServerEntity*, IServerEntity* pOther)
{
	ORIG_TeleportTouchingEntity(thisptr, pOther);

	Assert(spt_portalled_pause_feat.portalTeleportedEntitySignal.Works);
	spt_portalled_pause_feat.portalTeleportedEntitySignal(thisptr, pOther);

	if (pOther != utils::spt_serverEntList.GetPlayer())
		return;

	const auto pauseFor = spt_on_portalled_pause_for.GetInt();
	if (pauseFor > 0)
	{
		EngineConCmd("setpause");

		afterticks_entry_t entry;
		entry.ticksLeft = pauseFor;
		entry.command = "unpause";
		spt_afterticks.AddAfterticksEntry(entry);
	}
}
