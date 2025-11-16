#include "stdafx.hpp"
#include "afterframes.hpp"
#include "generic.hpp"
#include "..\sptlib-wrapper.hpp"
#include "..\cvars.hpp"
#include "signals.hpp"
#include "dbg.h"
#include "interfaces.hpp"
#include <sstream>

AfterframesFeature spt_afterframes;
static std::vector<afterframes_entry_t> afterframesQueue;
static bool afterframesPaused = false;
static int afterframesDelay = 0;

ConVar _y_spt_afterframes_await_legacy("_y_spt_afterframes_await_legacy",
                                       "0",
                                       FCVAR_TAS_RESET,
                                       "Set to 1 for backwards compatibility with old scripts.");
ConVar _y_spt_afterframes_reset_on_server_activate("_y_spt_afterframes_reset_on_server_activate", "1", FCVAR_ARCHIVE);

void AfterframesFeature::AddAfterFramesEntry(afterframes_entry_t entry)
{
	afterframesQueue.push_back(entry);
}

void AfterframesFeature::DelayAfterframesQueue(int delay)
{
	afterframesDelay = delay;
}

void AfterframesFeature::ResetAfterframesQueue()
{
	afterframesQueue.clear();
}

void AfterframesFeature::PauseAfterframesQueue()
{
	afterframesPaused = true;
}

void AfterframesFeature::ResumeAfterframesQueue()
{
	afterframesPaused = false;
}

bool AfterframesFeature::ShouldLoadFeature()
{
	// Can't run commands without EngineConCmd
	return interfaces::engine_client != nullptr;
}

void AfterframesFeature::UnloadFeature() {}

void AfterframesFeature::PreHook()
{
	if (spt_generic.ORIG_HudUpdate)
	{
		AfterFramesSignal.Works = true;
	}
}

void AfterframesFeature::OnFrame()
{
	if (afterframesPaused)
	{
		return;
	}

	if (afterframesDelay > 0)
	{
		--afterframesDelay;
		return;
	}

	for (auto it = afterframesQueue.begin(); it != afterframesQueue.end();)
	{
		it->framesLeft--;
		if (it->framesLeft <= 0)
		{
			EngineConCmd(it->command.c_str());
			it = afterframesQueue.erase(it);
		}
		else
			++it;
	}

	AfterFramesSignal();
}

void AfterframesFeature::SV_ActivateServer(bool result)
{
	if (_y_spt_afterframes_reset_on_server_activate.GetBool())
		ResetAfterframesQueue();
}

void AfterframesFeature::FinishRestore(void* thisptr)
{
	if (_y_spt_afterframes_await_legacy.GetBool())
		ResumeAfterframesQueue();
}

void AfterframesFeature::SetPaused(void* thisptr, bool paused)
{
	if (!paused && !_y_spt_afterframes_await_legacy.GetBool())
		ResumeAfterframesQueue();
}

CON_COMMAND(_y_spt_afterframes_wait, "Delays the afterframes queue")
{
	if (args.ArgC() != 2)
	{
		Msg("Usage: spt_afterframes_wait <delay>\n");
		return;
	}

	int delay = (int)strtol(args.Arg(1), nullptr, 10);

	spt_afterframes.DelayAfterframesQueue(delay);
}

CON_COMMAND(_y_spt_afterframes, "Add a command into an afterframes queue")
{
	if (args.ArgC() != 3)
	{
		Msg("Usage: spt_afterframes <count> <command>\n");
		return;
	}

	afterframes_entry_t entry;

	std::istringstream ss(args.Arg(1));
	ss >> entry.framesLeft;
	entry.command.assign(args.Arg(2));

	spt_afterframes.AddAfterFramesEntry(entry);
}

#ifndef OE
CON_COMMAND(
    _y_spt_afterframes2,
    "Add everything after count as a command into the queue. Do not insert the command in quotes")
{
	if (args.ArgC() < 3)
	{
		Msg("Usage: spt_afterframes2 <count> <command>\n");
		return;
	}

	afterframes_entry_t entry;

	std::istringstream ss(args.Arg(1));
	ss >> entry.framesLeft;
	const char* cmd = args.ArgS() + strlen(args.Arg(1)) + 1;
	entry.command.assign(cmd);

	spt_afterframes.AddAfterFramesEntry(entry);
}
#endif

CON_COMMAND(
    _y_spt_afterframes_await_load,
    "Pause reading from the afterframes queue until the next load or changelevel. Useful for writing scripts spanning multiple maps or save-load segments.")
{
	spt_afterframes.PauseAfterframesQueue();
}

CON_COMMAND(_y_spt_afterframes_reset, "Reset the afterframes queue.")
{
	spt_afterframes.ResetAfterframesQueue();
}

void AfterframesFeature::LoadFeature()
{
	if (AfterFramesSignal.Works)
	{
		InitCommand(_y_spt_afterframes);
		InitCommand(_y_spt_afterframes_reset);
		InitCommand(_y_spt_afterframes_wait);
#ifndef OE
		InitCommand(_y_spt_afterframes2);
#endif

		FrameSignal.Connect(this, &AfterframesFeature::OnFrame);

		if (SV_ActivateServerSignal.Works)
		{
			SV_ActivateServerSignal.Connect(this, &AfterframesFeature::SV_ActivateServer);
			InitConcommandBase(_y_spt_afterframes_reset_on_server_activate);
		}

		if (FinishRestoreSignal.Works || SetPausedSignal.Works)
		{
			FinishRestoreSignal.Connect(this, &AfterframesFeature::FinishRestore);
			SetPausedSignal.Connect(this, &AfterframesFeature::SetPaused);
			InitCommand(_y_spt_afterframes_await_load);
			InitConcommandBase(_y_spt_afterframes_await_legacy);

			if (!SetPausedSignal.Works)
			{
				_y_spt_afterframes_await_legacy.SetValue(1);
				Warning("spt_afterframes_await_legacy 0 has no effect.\n");
			}
			else if (!FinishRestoreSignal.Works)
			{
				_y_spt_afterframes_await_legacy.SetValue(0);
				Warning("spt_afterframes_await_legacy 1 has no effect.\n");
			}
		}
	}
}
