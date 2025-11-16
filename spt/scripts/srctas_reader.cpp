#include "stdafx.hpp"

#include "srctas_reader.hpp"

#include "..\spt-serverplugin.hpp"

#include "..\sptlib-wrapper.hpp"
#include "game_detection.hpp"
#include "file.hpp"
#include "interfaces.hpp"
#include "math.hpp"
#include "string_utils.hpp"
#include "..\features\tickrate.hpp"
#include "framebulk_handler.hpp"
#include "..\features\afterframes.hpp"
#include "..\features\demo.hpp"
#include "..\features\tas.hpp"

namespace scripts
{
	SourceTASReader g_TASReader;
	const std::string SCRIPT_EXT = ".srctas";

	const char* RESET_VARS[] = {"cl_forwardspeed", "cl_sidespeed", "cl_yawspeed"};

	const int RESET_VARS_COUNT = ARRAYSIZE(RESET_VARS);

	SourceTASReader::SourceTASReader()
	{
		InitPropertyHandlers();
		iterationFinished = true;
	}

	void SourceTASReader::ExecuteScript(const std::string& script)
	{
		freezeVariables = false;
		fileName = script;
		CommonExecuteScript(false);
	}

	void SourceTASReader::ExecuteScriptWithResume(const std::string& script, int resumeTicks)
	{
		char buffer[80];
		freezeVariables = false;
		fileName = script;
		CommonExecuteScript(false);

		spt_afterframes.AddAfterFramesEntry(afterframes_entry_t(0, "spt_cvar fps_max 0; mat_norendering 1"));
		tickTime = spt_tickrate.GetTickrate();
		snprintf(buffer, ARRAYSIZE(buffer), "spt_cvar fps_max %.6f; mat_norendering 0", 1 / tickTime);
		int resumeTick = GetCurrentScriptLength() - resumeTicks;
		spt_afterframes.AddAfterFramesEntry(afterframes_entry_t(resumeTick, buffer));
	}

	void SourceTASReader::StartSearch(const std::string& script)
	{
		freezeVariables = false;
		fileName = script;
		CommonExecuteScript(true);
		freezeVariables = true;
	}

	void SourceTASReader::SearchResult(scripts::SearchResult result)
	{
		try
		{
			variables.SetResult(result);
			CommonExecuteScript(true);
		}
		catch (const std::exception& ex)
		{
			Msg("Error setting result: %s\n", ex.what());
		}
		catch (const SearchDoneException&)
		{
			Msg("Search done.\n");
			variables.PrintBest();
			iterationFinished = true;
		}
		catch (...)
		{
			Msg("Unexpected exception on line %i\n", currentLine);
		}
	}

	void SourceTASReader::CommonExecuteScript(bool search)
	{
		try
		{
			DevMsg("Attempting to parse a version 1 TAS script...\n");
			Reset();

			std::string gameDir = GetGameDir();
			scriptStream.open(gameDir + "\\" + fileName + SCRIPT_EXT);

			if (!scriptStream.is_open())
				throw std::exception("File does not exist");
			ParseProps();

			if (search && searchType == SearchType::None)
				throw std::exception("In search mode but search property is not set");
			else if (!search && searchType != SearchType::None)
				throw std::exception("Not in search mode but search property is set");

			while (!scriptStream.eof())
			{
				if (IsFramesLine())
					ParseFrames();
				else if (IsVarsLine())
					ParseVariables();
				else
					throw std::exception(
					    "Unexpected section order in file. Expected order is props - variables - frames");
			}

			Execute();
		}
		catch (const std::exception& ex)
		{
			Msg("Error in line %i: %s!\n", currentLine, ex.what());
		}
		catch (const SearchDoneException&)
		{
			Msg("Search done.\n");
			variables.PrintBest();
		}
		catch (...)
		{
			Msg("Unexpected exception on line %i\n", currentLine);
		}

		scriptStream.close();
	}

	void SourceTASReader::OnAfterFrames()
	{
		if (currentTick <= currentScript.GetScriptLength())
			++currentTick;

		if (conditions.empty() || iterationFinished)
			return;

		bool allTrue = true;

		for (auto& pointer : conditions)
		{
			allTrue = allTrue && pointer->IsTrue(currentTick, currentScript.GetScriptLength());
			if (pointer->ShouldTerminate(currentTick, currentScript.GetScriptLength()))
			{
				iterationFinished = true;
				SearchResult(SearchResult::Fail);

				return;
			}
		}

		if (allTrue)
		{
			auto onsuccessCmd = tas_script_onsuccess.GetString();
			if (onsuccessCmd && onsuccessCmd[0])
				EngineConCmd(onsuccessCmd);

			iterationFinished = true;
			SearchResult(SearchResult::Success);
		}
	}

	int SourceTASReader::GetCurrentTick()
	{
		return currentTick;
	}

	int SourceTASReader::GetCurrentScriptLength()
	{
		return currentScript.GetScriptLength();
	}

	bool SourceTASReader::IsExecutingScript()
	{
		return currentTick <= currentScript.GetScriptLength();
	}

	void SourceTASReader::Execute()
	{
		iterationFinished = false;
		SetFpsAndPlayspeed();
		spt_demostuff.Demo_StopRecording();
		currentTick = 0;
		spt_afterframes.ResetAfterframesQueue();
		currentScript.Init(fileName);

		auto demoName = currentScript.GetDemoName();

		if (spt_demostuff.Demo_IsAutoRecordingAvailable())
		{
			currentScript.AddAfterFramesEntry(demoDelay, "spt_record " + demoName);
		}
		else
		{
			currentScript.AddAfterFramesEntry(demoDelay, "record " + demoName);
		}

		for (auto& entry : currentScript.afterFramesEntries)
		{
			if (entry.framesLeft == NO_AFTERFRAMES_BULK)
			{
				currentScript.AddDuringLoadCmd(entry.command);
			}
		}

		std::string startCmd(currentScript.initCommand + ";" + currentScript.duringLoad);
		EngineConCmd(startCmd.c_str());
		DevMsg("Executing start command: %s\n", startCmd.c_str());

		for (auto& entry : currentScript.afterFramesEntries)
		{
			if (entry.framesLeft != NO_AFTERFRAMES_BULK)
			{
				spt_afterframes.AddAfterFramesEntry(entry);
			}
		}
	}

	void SourceTASReader::SetFpsAndPlayspeed()
	{
		std::ostringstream os;
		tickTime = spt_tickrate.GetTickrate();
		float fps = 1.0f / tickTime * playbackSpeed;
		os << "host_framerate " << tickTime << "; fps_max " << fps;
		currentScript.AddDuringLoadCmd(os.str());
	}

	bool SourceTASReader::ParseLine()
	{
		if (!scriptStream.good())
		{
			return false;
		}

		std::getline(scriptStream, line);
		SetNewLine();

		return true;
	}

	void SourceTASReader::SetNewLine()
	{
		const std::string COMMENT_START = "//";
		int end = line.find(COMMENT_START); // ignore comments
		if (end != std::string::npos)
			line.erase(end);

		ReplaceVariables();
		lineStream.str(line);
		lineStream.clear();
		++currentLine;
	}

	void SourceTASReader::ReplaceVariables()
	{
		for (auto& variable : variables.variableMap)
		{
			ReplaceAll(line, GetVarIdentifier(variable.first), variable.second.GetValue());
		}
	}

	void SourceTASReader::ResetConvars()
	{
#ifndef OE
		ConCommandBase* cmd = g_pCVar->GetCommands();

		// Loops through the console variables and commands
		while (cmd != NULL)
		{
			const char* name = cmd->GetName();
			// Reset any variables that have been marked to be reset for TASes
			if (!cmd->IsCommand() && name != NULL && cmd->IsFlagSet(FCVAR_TAS_RESET))
			{
				auto convar = g_pCVar->FindVar(name);
				DevMsg("Trying to reset variable %s\n", name);

				// convar found
				if (convar != NULL)
				{
					DevMsg("Resetting var %s to value %s\n", name, convar->GetDefault());
					convar->SetValue(convar->GetDefault());
				}
				else
					throw std::exception("Unable to find listed console variable!");
			}

			// Issue minus commands to reset any keypresses
			else if (cmd->IsCommand() && cmd->GetName() != NULL && cmd->GetName()[0] == '-')
			{
				DevMsg("Running command %s\n", cmd->GetName());
				EngineConCmd(cmd->GetName());
			}

			cmd = cmd->GetNext();
		}

		// Reset any variables selected above
		for (int i = 0; i < RESET_VARS_COUNT; ++i)
		{
			auto resetCmd = g_pCVar->FindVar(RESET_VARS[i]);
			if (resetCmd != NULL)
			{
				DevMsg("Resetting var %s to value %s\n", resetCmd->GetName(), resetCmd->GetDefault());
				resetCmd->SetValue(resetCmd->GetDefault());
			}
			else
				DevWarning("Unable to find console variable %s\n", RESET_VARS[i]);
		}
#endif
	}

	void SourceTASReader::Reset()
	{
		if (!freezeVariables)
		{
			variables.Clear();
		}
		ResetIterationState();
	}

	void SourceTASReader::ResetIterationState()
	{
		ResetConvars();
		conditions.clear();
		scriptStream.clear();
		lineStream.clear();
		line.clear();
		currentLine = 0;
		searchType = SearchType::None;
		playbackSpeed = 1.0f;
		demoDelay = 0;
		currentScript.Reset();
	}

	void SourceTASReader::ParseProps()
	{
		while (ParseLine())
		{
			if (IsFramesLine() || IsVarsLine())
			{
				break;
			}
			ParseProp();
		}
	}

	void SourceTASReader::ParseProp()
	{
		if (isLineEmpty())
		{
			return;
		}

		std::string prop;
		std::string value;
		GetDoublet(lineStream, prop, value, ' ');

		if (propertyHandlers.find(prop) != propertyHandlers.end())
		{
			(this->*propertyHandlers[prop])(value);
		}
		else
			throw std::exception("Unknown property name");
	}

	void SourceTASReader::HandleSettings(const std::string& value)
	{
		currentScript.AddDuringLoadCmd(value);
	}

	void SourceTASReader::ParseVariables()
	{
		while (ParseLine())
		{
			if (IsFramesLine())
			{
				break;
			}

			if (!freezeVariables)
				ParseVariable();
		}

		variables.Iteration(searchType);
		variables.PrintState();
	}

	void SourceTASReader::ParseVariable()
	{
		if (isLineEmpty())
			return;

		std::string type;
		std::string name;
		std::string value;
		GetTriplet(lineStream, type, name, value, ' ');
		variables.AddNewVariable(type, name, value);
	}

	void SourceTASReader::ParseFrames()
	{
		while (ParseLine())
		{
			ParseFrameBulk();
		}
	}

	void SourceTASReader::ParseFrameBulk()
	{
		if (isLineEmpty())
		{
			return;
		}
		else if (line.find("ss") == 0)
		{
			currentScript.AddSaveState();
		}
		else if (line.find("sl") == 0)
		{
			currentScript.AddSaveLoad();
		}
		else
		{
			FrameBulkInfo info(lineStream);
			auto output = HandleFrameBulk(info);

			currentScript.AddFrameBulk(output);
		}
	}

	void SourceTASReader::InitPropertyHandlers()
	{
		propertyHandlers["save"] = &SourceTASReader::HandleSave;
		propertyHandlers["demo"] = &SourceTASReader::HandleDemo;
		propertyHandlers["demodelay"] = &SourceTASReader::HandleDemoDelay;
		propertyHandlers["search"] = &SourceTASReader::HandleSearch;
		propertyHandlers["playspeed"] = &SourceTASReader::HandlePlaybackSpeed;
		propertyHandlers["settings"] = &SourceTASReader::HandleSettings;

		// Conditions for automated searching
		propertyHandlers["tick"] = &SourceTASReader::HandleTickRange;
		propertyHandlers["tickend"] = &SourceTASReader::HandleTicksFromEndRange;
		propertyHandlers["posx"] = &SourceTASReader::HandleXPos;
		propertyHandlers["posy"] = &SourceTASReader::HandleYPos;
		propertyHandlers["posz"] = &SourceTASReader::HandleZPos;
		propertyHandlers["velx"] = &SourceTASReader::HandleXVel;
		propertyHandlers["vely"] = &SourceTASReader::HandleYVel;
		propertyHandlers["velz"] = &SourceTASReader::HandleZVel;
		propertyHandlers["vel2d"] = &SourceTASReader::Handle2DVel;
		propertyHandlers["velabs"] = &SourceTASReader::HandleAbsVel;
		propertyHandlers["alive"] = &SourceTASReader::HandleAliveCondition;
		propertyHandlers["jb"] = &SourceTASReader::HandleJBCondition;
		propertyHandlers["changelevel"] = &SourceTASReader::HandleCLCondition;
		propertyHandlers["velyaw"] = &SourceTASReader::HandleVelYaw;
		propertyHandlers["velpitch"] = &SourceTASReader::HandleVelPitch;
#if SSDK2007
		if (utils::DoesGameLookLikePortal())
		{
			propertyHandlers["portal_bubble"] = &SourceTASReader::HandlePBubbleCondition;
		}
#endif
	}

	void SourceTASReader::HandleSave(const std::string& value)
	{
		currentScript.SetSave(value);
	}

	void SourceTASReader::HandleDemo(const std::string& value)
	{
		currentScript.SetDemoName(value);
	}

	void SourceTASReader::HandleDemoDelay(const std::string& value)
	{
		demoDelay = ParseValue<int>(value);
	}

	void SourceTASReader::HandleSearch(const std::string& value)
	{
		if (value == "low")
			searchType = SearchType::Lowest;
		else if (value == "high")
			searchType = SearchType::Highest;
		else if (value == "random")
			searchType = SearchType::Random;
		else if (value == "randomlow")
			searchType = SearchType::RandomLowest;
		else if (value == "randomhigh")
			searchType = SearchType::RandomHighest;
		else
			throw std::exception("Search type was invalid");
	}

	void SourceTASReader::HandlePlaybackSpeed(const std::string& value)
	{
		playbackSpeed = ParseValue<float>(value);
		if (playbackSpeed <= 0.0f)
			throw std::exception("Playback speed has to be positive");
	}

	void SourceTASReader::HandleTickRange(const std::string& value)
	{
		int min, max;
		GetDoublet(value, min, max, '|');
		conditions.push_back(std::unique_ptr<Condition>(new TickRangeCondition(min, max, false)));
	}

	void SourceTASReader::HandleTicksFromEndRange(const std::string& value)
	{
		int min, max;
		GetDoublet(value, min, max, '|');
		conditions.push_back(std::unique_ptr<Condition>(new TickRangeCondition(min, max, true)));
	}

	void SourceTASReader::HandleJBCondition(const std::string& value)
	{
		auto height = ParseValue<float>(value);
		conditions.push_back(std::unique_ptr<Condition>(new JBCondition(height)));
	}

	void SourceTASReader::HandleAliveCondition(const std::string& value)
	{
		conditions.push_back(std::unique_ptr<Condition>(new AliveCondition()));
	}

#if SSDK2007
	void SourceTASReader::HandlePBubbleCondition(const std::string& value)
	{
		auto inBubble = ParseValue<bool>(value);
		conditions.push_back(std::unique_ptr<Condition>(new PBubbleCondition(inBubble)));
	}
#endif

	void SourceTASReader::HandleCLCondition(const std::string& value)
	{
		conditions.push_back(std::unique_ptr<Condition>(new LoadCondition()));
	}

	void SourceTASReader::HandleVelPitch(const std::string& value)
	{
		float low, high;
		GetDoublet(value, low, high, '|');
		conditions.push_back(std::unique_ptr<Condition>(new VelAngleCondition(low, high, AngleAxis::Pitch)));
	}

	void SourceTASReader::HandleVelYaw(const std::string& value)
	{
		float low, high;
		GetDoublet(value, low, high, '|');
		conditions.push_back(std::unique_ptr<Condition>(new VelAngleCondition(low, high, AngleAxis::Yaw)));
	}

	void SourceTASReader::HandlePosVel(const std::string& value, Axis axis, bool isPos)
	{
		float min, max;
		GetDoublet(value, min, max, '|');
		conditions.push_back(std::unique_ptr<Condition>(new PosSpeedCondition(min, max, axis, isPos)));
	}

	bool SourceTASReader::isLineEmpty()
	{
		return line.find_first_not_of(' ') == std::string::npos;
	}

	bool SourceTASReader::IsFramesLine()
	{
		return line.find("frames") == 0;
	}

	bool SourceTASReader::IsVarsLine()
	{
		return line.find("vars") == 0;
	}

	std::string GetVarIdentifier(std::string name)
	{
		std::ostringstream os;
		os << "[" << name << "]";

		return os.str();
	}
} // namespace scripts
