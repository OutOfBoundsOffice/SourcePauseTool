#include "stdafx.hpp"

#include "..\sptlib-wrapper.hpp"
#include "signals.hpp"
#include "convar.hpp"
#include "..\features\afterticks.hpp"
#include "spt\features\hud.hpp"
#include "visualizations/imgui/imgui_interface.hpp"
#include "interfaces.hpp"

#include <format>

static std::vector<const char*> _saveloadTypes = {"segment", "execute", "render"};
static const char saveloads_info[] =
    "Initiates a new save/load session.\n"
    "Arguments: spt_saveloads <type> <segment name> <start index> <end index> [<ticks to wait>] [<extra commands>].\n"
    "\n"
    "* Required arguments:\n"
    "   - <type> determines what the command does during for this session. Valid values include:\n"
    "      - 'segment': Full save/load segment creation. Saves and demos for each save/load will be kept and named properly.\n"
    "      - 'execute': Partial save/load execution. No demos will be made and only the last save/load's save is kept.\n"
    "      - 'render':  Renders an existing save/load segment. The provided segment name will be used to load up saves for screenshotting.\n"
    "   - <segment name> determines the name that the session will refer to for saves and demos.\n"
    "   - <start index> determines the starting index for saves and demos.\n"
    "   - <end index> determines the ending index for saves and demos.\n"
    "\n"
    "* Optional arguments:\n"
    "   - <ticks to wait> is the number of ticks to wait after a save is loaded before executing commands. Default value is 0.\n"
    "   - <extra commands> determines the commands to be executed right after a save has finished loading. These commands are not deferred by the delay specified by ticks to wait. Default is empty.\n"
    "\n"
    "* Examples:\n"
    "   - 'spt_saveloads segment 001-abc 0 100' will:\n"
    "       - Do 101 save/loads.\n"
    "       - Generate '001-abc-000.dem', '001-abc-000.sav'; '001-abc-001.dem', '001-abc-001.sav'; and so on.\n"
    "   - 'spt_saveloads execute 001-abc 0 100' will:\n"
    "       - Do 101 save/loads.\n"
    "       - Only generate a '001-abc.sav' file which is of the last save/load.\n"
    "   - 'spt_saveloads render 001-abc 0 100' will:\n"
    "       - Load the save '001-abc-000', then screenshot.\n"
    "       - Load the save '001-abc-001', then screenshot.\n"
    "       - ... repeat until '001-abc-100' is loaded and screenshotted.\n"
    "   - 'spt_saveloads segment 001-abc 0 100 100 \"+duck\"' will:\n"
    "       - Do 101 save/loads.\n"
    "       - Generate '001-abc-000.dem', '001-abc-000.sav'; '001-abc-001.dem', '001-abc-001.sav'; and so on.\n"
    "       - Right after a save is loaded, it will send the command '+duck', then wait 100 ticks before running save and demo commands."
    "\n"
    "* Usage: \n"
    "   - Enter in the command.\n"
    "   - Load the save from which the session should begin from. The save/loading will begin automatically.\n"
    "   - Use the 'spt_saveloads_stop' command at any time to stop the session.";

// Feature that does automated save/loading operations, such as save/load segment creation or rendering
class SaveloadsFeature : public FeatureWrapper<SaveloadsFeature>
{
public:
	void Begin(int type_,
	           const char* segName_,
	           int startIndex_,
	           int endIndex_,
	           int ticksToWait_,
	           const char* extra);
	void Stop();

protected:
	virtual bool ShouldLoadFeature() override;
	virtual void LoadFeature() override;
	//virtual void UnloadFeature() override;

private:
	int type;
	std::string prefixName;
	int startIndex;
	int endIndex;
	int ticksToWait;

	std::string extraCommands;
	bool execute = false;
	void OnSetSignonState(void* thisptr, int state);
};

static SaveloadsFeature spt_saveloads;

CON_COMMAND(y_spt_saveloads,
            "Initiates a new save/load session. Enter this command without arguments for detailed help.")
{
	if (args.ArgC() < 4)
	{
		Msg("%s\n", saveloads_info);
		return;
	}

	int type = -1;
	for (int i = 0; i < (int)_saveloadTypes.size(); i++)
	{
		if (strcmp(args.Arg(1), _saveloadTypes[i]) == 0)
		{
			type = i;
			break;
		}
	}

	if (type == -1)
	{
		type = 0;
		Warning("SAVELOADS: Invalid save/load session type! Using \"segment\" instead.\n");
	}

	spt_saveloads.Begin(type,
	                    args.Arg(2),
	                    atoi(args.Arg(3)),
	                    atoi(args.Arg(4)),
	                    args.ArgC() == 5 ? 0 : atoi(args.Arg(5)),
	                    args.ArgC() == 6 ? nullptr : args.Arg(6));
}

CON_COMMAND(y_spt_saveloads_stop, "Stops the current save/loading session.")
{
	spt_saveloads.Stop();
}

ConVar y_spt_hud_saveloads_showcurindex("y_spt_hud_saveloads_showcurindex",
                                        "0",
                                        0,
                                        "Shows the current save/load index of the current save/load session.\n");

bool SaveloadsFeature::ShouldLoadFeature()
{
	// Can't run commands without EngineConCmd
	return interfaces::engine_client != nullptr;
}

void SaveloadsFeature::LoadFeature()
{
	if (!spt_afterticks.Works || !SetSignonStateSignal.Works)
		return;

	//FinishRestoreSignal.Connect(this, &SaveloadsFeature::FinishRestore);
	InitCommand(y_spt_saveloads);
	InitCommand(y_spt_saveloads_stop);

#ifdef SPT_HUD_ENABLED
	bool hudEnabled = AddHudCallback(
	    "saveloads_showcurindex",
	    [this](std::string)
	    {
		    if (execute)
			    spt_hud_feat.DrawTopHudElement(L"SAVELOADS: %i / %i (%i left)",
			                                   startIndex,
			                                   endIndex,
			                                   endIndex - startIndex);
		    else
			    spt_hud_feat.DrawTopHudElement(L"SAVELOADS: Not executing");
	    },
	    y_spt_hud_saveloads_showcurindex);

	if (hudEnabled)
		SptImGui::RegisterHudCvarCheckbox(y_spt_hud_saveloads_showcurindex);
#endif

	SetSignonStateSignal.Connect(this, &SaveloadsFeature::OnSetSignonState);
}

void SaveloadsFeature::Begin(int type_,
                             const char* segName_,
                             int startIndex_,
                             int endIndex_,
                             int ticksToWait_,
                             const char* extra)
{
	if (type_ > 2 || type_ < 0)
	{
		Warning("SAVELOADS: Invalid save/load session type! Not continuing.\n");
		return;
	}

	if (startIndex_ < 0 || endIndex_ < 0 || startIndex_ > endIndex_)
	{
		Warning("SAVELOADS: Invalid start and/or end index.\n");
		return;
	}

	this->type = type_;
	this->prefixName.assign(segName_);
	this->startIndex = startIndex_;
	this->endIndex = endIndex_;
	this->ticksToWait = ticksToWait_;
	this->extraCommands.assign(extra == nullptr ? "" : extra);

	execute = true;

	Msg("------\n\nSAVELOADS: Save/load session:\n\
    - Type: %s\n\
    - Segment name: \"%s\"\n\
    - From index %i to index %i (%i save/loads)\n\
    - Wait time before action: %i\n\
    - Extra commands: \"%s\"\n\n\
Please load the save from which save/loading should begin.\n",
	    _saveloadTypes[type_],
	    segName_,
	    startIndex_,
	    endIndex_,
	    endIndex_ - startIndex_ + 1,
	    ticksToWait_,
	    this->extraCommands.c_str());
	Warning("Use \"spt_saveloads_stop\" to stop the session!!! You should bind it to something.\n");
	Msg("\n------\n");
}

void SaveloadsFeature::Stop()
{
	if (!execute)
		return;

	this->extraCommands.assign("");
	execute = false;
	;
	spt_afterticks.ResetAfterticksQueue();
	Warning("SAVELOADS: Save/load session stopped.\n");
}

void SaveloadsFeature::OnSetSignonState(void* thisptr, int state)
{
	static int lastSignOnState = state;
	int cur = state;

	if (lastSignOnState == cur)
		return;

	lastSignOnState = cur;

	if (cur != 6)
		return;

	if (!execute)
		return;

	std::string segName = std::format("{}-{:03d}", prefixName, startIndex);
	std::string command;

	EngineConCmd("unpause");
	EngineConCmd(extraCommands.c_str());

	switch (this->type)
	{
	case 0: // normal segment

		EngineConCmd(std::format("record {}", segName).c_str());
		command = std::format(
		    "save {0}; spt_afterticks 20 \"echo #SAVE#\"; spt_afterticks 25 \"stop\"; spt_afterticks 30 \"load {0}\"",
		    segName,
		    extraCommands,
		    prefixName);
		break;
	case 1: // normal save/load
		command = std::format("save {2}; spt_afterticks 30 \"load {2}\"", segName, extraCommands, prefixName);
		break;
	case 2:                                     // screenshot
		ticksToWait = MAX(ticksToWait, 20); // else we won't get any screenshots...
		command =
		    std::format("screenshot; {0}; spt_afterticks 30 \"load {0}\"", segName, extraCommands, prefixName);
		break;
	};

	if (ticksToWait > 0)
	{
		afterticks_entry_t entry;
		entry.command.assign(command);
		entry.ticksLeft = ticksToWait;

		spt_afterticks.AddAfterticksEntry(entry);
	}
	else
	{
		EngineConCmd(command.c_str());
	}

	Warning("SAVELOADS: Processed segment %i, working until %i (%i left)\n",
	        startIndex,
	        endIndex,
	        endIndex - startIndex);

	startIndex++;
	if (startIndex > endIndex)
	{
		execute = false;
		Warning("SAVELOADS: Save/load session finished.\n");
	}
}
