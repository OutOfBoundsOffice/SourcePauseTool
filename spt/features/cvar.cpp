#include "stdafx.hpp"

#include <algorithm>
#include <string>

#include "cvar.hpp"
#include "..\utils\convar.hpp"
#include "..\utils\math.hpp"
#include "interfaces.hpp"

namespace patterns
{
	PATTERNS(RebuildCompletionList, "4044", "B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 53 55 56")
}

CvarStuff spt_cvarstuff;

static int DevCvarCompletionFunc(AUTOCOMPLETION_FUNCTION_PARAMS)
{
	AutoCompleteList devCvarComplete(spt_cvarstuff.dev_cvars);
	return devCvarComplete.AutoCompletionFunc(partial, commands);
}

CON_COMMAND_F_COMPLETION(y_spt_cvar, "CVar manipulation.", 0, DevCvarCompletionFunc)
{
	if (!g_pCVar)
		return;

	if (args.ArgC() < 2)
	{
		Msg("Usage: spt_cvar <name> [value]\n");
		return;
	}

	ConVar* cv = g_pCVar->FindVar(args.Arg(1));
	if (!cv)
	{
		Warning("Couldn't find the cvar: %s\n", args.Arg(1));
		return;
	}

	if (args.ArgC() == 2)
	{
		Msg("\"%s\" = \"%s\"\n", cv->GetName(), cv->GetString(), cv->GetHelpText());
		Msg("Default: %s\n", cv->GetDefault());

		float val;
		if (cv->GetMin(val))
		{
			Msg("Min: %f\n", val);
		}

		if (cv->GetMax(val))
		{
			Msg("Max: %f\n", val);
		}

		const char* helpText = cv->GetHelpText();
		if (helpText[0] != '\0')
			Msg("- %s\n", cv->GetHelpText());

		return;
	}

	const char* value = args.Arg(2);
	cv->SetValue(value);
}

CON_COMMAND(y_spt_cvar_random, "Randomize CVar value.")
{
	if (!g_pCVar)
		return;

	if (args.ArgC() != 4)
	{
		Msg("Usage: spt_cvar_random <name> <min> <max>\n");
		return;
	}

	ConVar* cv = g_pCVar->FindVar(args.Arg(1));
	if (!cv)
	{
		Warning("Couldn't find the cvar: %s\n", args.Arg(1));
		return;
	}

	float min = std::stof(args.Arg(2));
	float max = std::stof(args.Arg(3));

	float r = utils::RandomFloat(min, max);
	cv->SetValue(r);
}

#if !defined(OE)

CON_COMMAND_F_COMPLETION(y_spt_cvar2,
                         "CVar manipulation, sets the CVar value to the rest of the argument string.",
                         0,
                         DevCvarCompletionFunc)
{
	if (!g_pCVar)
		return;

	if (args.ArgC() < 2)
	{
		Msg("Usage: spt_cvar <name> [value]\n");
		return;
	}

	ConVar* cv = g_pCVar->FindVar(args.Arg(1));
	if (!cv)
	{
		Warning("Couldn't find the cvar: %s\n", args.Arg(1));
		return;
	}

	if (args.ArgC() == 2)
	{
		Msg("\"%s\" = \"%s\"\n", cv->GetName(), cv->GetString(), cv->GetHelpText());
		Msg("Default: %s\n", cv->GetDefault());

		float val;
		if (cv->GetMin(val))
		{
			Msg("Min: %f\n", val);
		}

		if (cv->GetMax(val))
		{
			Msg("Max: %f\n", val);
		}

		const char* helpText = cv->GetHelpText();
		if (helpText[0] != '\0')
			Msg("- %s\n", cv->GetHelpText());

		return;
	}

	const char* value = args.ArgS() + strlen(args.Arg(1)) + 1;
	cv->SetValue(value);
}

CON_COMMAND(y_spt_dev_cvars, "Prints all developer/hidden CVars.")
{
	spt_cvarstuff.UpdateCommandList();
	for (const auto& name : spt_cvarstuff.dev_cvars)
	{
		Msg("%s\n", name.c_str());
	}
}
#else
IMPL_HOOK_THISCALL(CvarStuff, void, RebuildCompletionList, void*, const char* text)
{
	spt_cvarstuff.ORIG_RebuildCompletionList(thisptr, text);
	if (!spt_cvarstuff.m_CompletionListOffset)
		return;
	CUtlVector<CompletionItem*>* m_CompletionList =
	    (CUtlVector<CompletionItem*>*)((uint32_t)thisptr + spt_cvarstuff.m_CompletionListOffset);
	for (int i = 0; i < m_CompletionList->Count(); i++)
	{
		ConCommandBase* command = m_CompletionList->Element(i)->m_pCommand;
		// Check if FCVAR_HIDDEN is set to avoid traversing the vector everytime
		// Decrement i to compensate for the new list size
		auto& hv = spt_cvarstuff.hidden_cvars;
		if (command && command->IsFlagSet(FCVAR_HIDDEN) && std::find(hv.begin(), hv.end(), command) != hv.end())
			m_CompletionList->Remove(i--);
	}
}

void CvarStuff::InitHooks()
{
	HOOK_FUNCTION(gameui, RebuildCompletionList);
}

void CvarStuff::PreHook()
{
	if (ORIG_RebuildCompletionList)
		m_CompletionListOffset = *(int*)((uint32_t)ORIG_RebuildCompletionList + 81);
	DevWarning("m_CompletionListOffset %d\n", m_CompletionListOffset);
}
#endif

void CvarStuff::LoadFeature()
{
#ifdef OE
	if (g_pCVar || interfaces::engine_client)
	{
		InitCommand(y_spt_cvar);
		InitCommand(y_spt_cvar_random);
	}
#else
	if (g_pCVar)
	{
		UpdateCommandList();
		InitCommand(y_spt_cvar);
		InitCommand(y_spt_cvar_random);
		InitCommand(y_spt_dev_cvars);
		InitCommand(y_spt_cvar2);
	}
#endif
}

#ifndef OE
void CvarStuff::UpdateCommandList()
{
	dev_cvars.clear();
	const ConCommandBase* cmd = g_pCVar->GetCommands();
	for (; cmd; cmd = cmd->GetNext())
	{
		if (!cmd->IsCommand() && cmd->IsFlagSet(FCVAR_DEVELOPMENTONLY | FCVAR_HIDDEN))
		{
			dev_cvars.push_back(cmd->GetName());
		}
	}
}
#endif
