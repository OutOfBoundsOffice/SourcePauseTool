#include "stdafx.hpp"
#include "..\feature.hpp"
#include "ent_props.hpp"
#include "convar.hpp"
#include "hud.hpp"
#include "playerio.hpp"
#include "game_detection.hpp"
#include "interfaces.hpp"
#include "ent_utils.hpp"
#include "string_utils.hpp"
#include "spt\utils\portal_utils.hpp"
#include "SPTLib\patterns.hpp"
#include "visualizations/imgui/imgui_interface.hpp"
#include "thirdparty/imgui/imgui_internal.h"
#include "spt/utils/ent_list.hpp"

#include <string>
#include <unordered_map>
#include <inttypes.h>
#include <regex>

#ifdef SPT_HUD_ENABLED
ConVar y_spt_hud_portal_bubble("y_spt_hud_portal_bubble", "0", FCVAR_CHEAT, "Turns on portal bubble index hud.\n");
ConVar y_spt_hud_ent_info(
    "y_spt_hud_ent_info",
    "",
    FCVAR_CHEAT,
    "Display entity info on HUD. Format is \"[ent index],[prop regex],[prop regex],...,[prop regex];[ent index],...,[prop regex]\".\n");
#endif

static const int MAX_ENTRIES = 128;
static const int INFO_BUFFER_SIZE = 256;
static wchar INFO_ARRAY[MAX_ENTRIES * INFO_BUFFER_SIZE];
static const char ENT_SEPARATOR = ';';
static const char PROP_SEPARATOR = ',';

EntProps spt_entprops;

namespace patterns
{
	PATTERNS(Datamap,
	         "pattern1",
	         "C7 05 ?? ?? ?? ?? ?? ?? ?? ?? C7 05 ?? ?? ?? ?? ?? ?? ?? ?? B8",
	         "pattern2",
	         "C7 05 ?? ?? ?? ?? ?? ?? ?? ?? C7 05 ?? ?? ?? ?? ?? ?? ?? ?? C3",
	         "pattern3",
	         "C7 05 ?? ?? ?? ?? ?? ?? ?? ?? B8 ?? ?? ?? ?? C7 05");
}

void EntProps::InitHooks()
{
	AddMatchAllPattern(patterns::Datamap, "client", "Datamap", &clientPatterns);
	AddMatchAllPattern(patterns::Datamap, "server", "Datamap", &serverPatterns);
	tablesProcessed = false;
}

void EntProps::PreHook()
{
	ProcessTablesLazy();
}

void EntProps::UnloadFeature()
{
	for (auto* map : wrappers)
		delete map;
}

void EntProps::WalkDatamap(std::string key)
{
	ProcessTablesLazy();
	auto* result = GetDatamapWrapper(key);
	if (result)
		result->ExploreOffsets();
	else
		Msg("No datamap found with name \"%s\"\n", key.c_str());
}

void EntProps::PrintDatamaps()
{
	Msg("Printing all datamaps:\n");
	std::vector<std::string> names;
	names.reserve(wrappers.size());

	for (auto& pair : nameToMapWrapper)
		names.push_back(pair.first);

	std::sort(names.begin(), names.end());

	for (auto& name : names)
	{
		auto* map = nameToMapWrapper[name];

		Msg("\tDatamap: %s, server map %x, client map %x\n", name.c_str(), map->_serverMap, map->_clientMap);
	}
}

int EntProps::GetPlayerOffset(const std::string& key, bool server)
{
	ProcessTablesLazy();
	auto* playermap = GetPlayerDatamapWrapper();

	if (!playermap)
		return utils::INVALID_DATAMAP_OFFSET;

	if (server)
		return playermap->GetServerOffset(key);
	else
		return playermap->GetClientOffset(key);
}

int EntProps::GetFieldOffset(const std::string& mapKey, const std::string& key, bool server)
{
	auto map = GetDatamapWrapper(mapKey);
	if (!map)
	{
		return utils::INVALID_DATAMAP_OFFSET;
	}

	if (server)
		return map->GetServerOffset(key);
	else
		return map->GetClientOffset(key);
}

_InternalPlayerField EntProps::_GetPlayerField(const std::string& key, PropMode mode)
{
	_InternalPlayerField out;
	// Do nothing if we didn't load
	if (!startedLoading)
		return out;

	bool getServer = mode == PropMode::Server || mode == PropMode::PreferClient || mode == PropMode::PreferServer;
	bool getClient = mode == PropMode::Client || mode == PropMode::PreferClient || mode == PropMode::PreferServer;

	if (getServer)
	{
		out.serverOffset = GetPlayerOffset(key, true);
	}

	if (getClient)
	{
		out.clientOffset = GetPlayerOffset(key, false);
	}

	if (out.serverOffset == utils::INVALID_DATAMAP_OFFSET && out.clientOffset == utils::INVALID_DATAMAP_OFFSET)
	{
		DevWarning("Was unable to find field %s offsets for server or client!\n", key.c_str());
	}

	return out;
}

static bool IsAddressLegal(uint8_t* address, uint8_t* start, std::size_t length)
{
	return address >= start && address <= start + length;
}

static bool DoesMapLookValid(datamap_t* map, uint8_t* moduleStart, std::size_t length)
{
	if (!IsAddressLegal(reinterpret_cast<uint8_t*>(map->dataDesc), moduleStart, length))
		return false;

	int MAX_LEN = 64;
	char* name = const_cast<char*>(map->dataClassName);

	if (!IsAddressLegal(reinterpret_cast<uint8_t*>(name), moduleStart, length))
		return false;

	for (int i = 0; i < MAX_LEN; ++i)
	{
		if (name[i] == '\0')
		{
			return i > 0;
		}
	}

	return false;
}

PropMode EntProps::ResolveMode(PropMode mode)
{
	auto svplayer = utils::spt_serverEntList.GetPlayer();
	auto clplayer = utils::spt_clientEntList.GetPlayer();

	switch (mode)
	{
	case PropMode::Client:
		if (clplayer)
			return PropMode::Client;
		else
			return PropMode::None;
	case PropMode::Server:
		if (svplayer)
			return PropMode::Server;
		else
			return PropMode::None;
	case PropMode::PreferServer:
		if (svplayer)
			return PropMode::Server;
		else if (clplayer)
			return PropMode::Client;
		else
			return PropMode::None;
	case PropMode::PreferClient:
		if (clplayer)
			return PropMode::Client;
		else if (svplayer)
			return PropMode::Server;
		else
			return PropMode::None;
	default:
		return PropMode::None;
	}
}

void EntProps::AddMap(datamap_t* map, bool server)
{
	std::string name = map->dataClassName;

	// if this is a client map, get the associated server name
	if (!server)
	{
		if (!strcmp(map->dataClassName, "C_BaseHLPlayer"))
			name = "CHL2_Player";
		else if (strstr(map->dataClassName, "C_") == map->dataClassName)
			name = name.erase(1, 1);
	}

	auto result = nameToMapWrapper.find(name);
	utils::DatamapWrapper* ptr;

	if (result == nameToMapWrapper.end())
	{
		ptr = new utils::DatamapWrapper();
		wrappers.push_back(ptr);
		nameToMapWrapper[name] = ptr;
	}
	else
	{
		ptr = result->second;
	}

	if (server)
		ptr->_serverMap = map;
	else
		ptr->_clientMap = map;
}

utils::DatamapWrapper* EntProps::GetDatamapWrapper(const std::string& key)
{
	auto result = nameToMapWrapper.find(key);
	if (result != nameToMapWrapper.end())
		return result->second;
	else
		return nullptr;
}

utils::DatamapWrapper* EntProps::GetPlayerDatamapWrapper()
{
	// Grab cached result if exists, can also be NULL!
	if (playerDatamapSearched)
		return __playerdatamap;

	if (utils::DoesGameLookLikePortal())
	{
		__playerdatamap = GetDatamapWrapper("CPortal_Player");
	}
	else if (utils::DoesGameLookLikeDMoMM())
	{
		__playerdatamap = GetDatamapWrapper("CBasePlayer");
	}

	// Add any other game specific class names here
	if (!__playerdatamap)
	{
		__playerdatamap = GetDatamapWrapper("CHL2_Player");
	}

	if (!__playerdatamap)
	{
		__playerdatamap = GetDatamapWrapper("CBasePlayer");
	}

	if (!__playerdatamap)
	{
		__playerdatamap = GetDatamapWrapper("CBaseCombatCharacter");
	}

	if (!__playerdatamap)
	{
		__playerdatamap = GetDatamapWrapper("CBaseEntity");
	}

	playerDatamapSearched = true;
	return __playerdatamap;
}

static void GetDatamapInfo(patterns::MatchedPattern pattern, int& numfields, datamap_t**& pmap)
{
	switch (pattern.ptnIndex)
	{
	case 0:
		numfields = *reinterpret_cast<int*>(pattern.ptr + 6);
		pmap = reinterpret_cast<datamap_t**>(pattern.ptr + 12);
		break;
	case 1:
		numfields = *reinterpret_cast<int*>(pattern.ptr + 6);
		pmap = reinterpret_cast<datamap_t**>(pattern.ptr + 12);
		break;
	case 2:
		numfields = *reinterpret_cast<int*>(pattern.ptr + 6);
		pmap = reinterpret_cast<datamap_t**>(pattern.ptr + 17);
		break;
	}
}

void EntProps::ProcessTablesLazy()
{
	if (tablesProcessed)
		return;

	void* clhandle;
	void* clmoduleStart;
	size_t clmoduleSize;
	void* svhandle;
	void* svmoduleStart;
	size_t svmoduleSize;

	if (MemUtils::GetModuleInfo(L"server.dll", &svhandle, &svmoduleStart, &svmoduleSize))
	{
		for (auto serverPattern : serverPatterns)
		{
			int numfields;
			datamap_t** pmap;
			GetDatamapInfo(serverPattern, numfields, pmap);
			if (numfields > 0
			    && IsAddressLegal(reinterpret_cast<uint8_t*>(pmap),
			                      reinterpret_cast<uint8_t*>(svmoduleStart),
			                      svmoduleSize))
			{
				datamap_t* map = *pmap;
				if (DoesMapLookValid(map, reinterpret_cast<uint8_t*>(svmoduleStart), svmoduleSize))
					AddMap(map, true);
			}
		}
	}

	if (MemUtils::GetModuleInfo(L"client.dll", &clhandle, &clmoduleStart, &clmoduleSize))
	{
		for (auto& clientPattern : clientPatterns)
		{
			int numfields;
			datamap_t** pmap;
			GetDatamapInfo(clientPattern, numfields, pmap);
			if (numfields > 0
			    && IsAddressLegal(reinterpret_cast<uint8_t*>(pmap),
			                      reinterpret_cast<uint8_t*>(clmoduleStart),
			                      clmoduleSize))
			{
				datamap_t* map = *pmap;
				if (DoesMapLookValid(map, reinterpret_cast<uint8_t*>(clmoduleStart), clmoduleSize))
					AddMap(map, false);
			}
		}
	}

	clientPatterns.clear();
	serverPatterns.clear();
	tablesProcessed = true;
}

CON_COMMAND(y_spt_canjb, "Tests if player can jumpbug on a given height, with the current position and speed.")
{
	if (args.ArgC() < 2)
	{
		Msg("Usage: spt_canjb [height]\n");
		return;
	}

	float height = std::stof(args.Arg(1));
	auto can = utils::CanJB(height);

	if (can.canJB)
		Msg("Yes, projected landing height %.6f in %d ticks\n", can.landingHeight - height, can.ticks);
	else
		Msg("No, missed by %.6f in %d ticks.\n", can.landingHeight - height, can.ticks);
}

CON_COMMAND(y_spt_print_ents, "Prints all entity indices and their corresponding classes.")
{
	auto& serverEnts = utils::spt_serverEntList.GetEntList();
	auto& clientEnts = utils::spt_clientEntList.GetEntList();

	if (serverEnts.empty() && clientEnts.empty())
	{
		Msg("no entities\n");
		return;
	}

	auto serverIt = serverEnts.cbegin();
	auto clientIt = clientEnts.cbegin();

	for (;;)
	{
		bool hasServerEnt = serverIt < serverEnts.cend();
		bool hasClientEnt = clientIt < clientEnts.cend();
		if (!hasServerEnt && !hasClientEnt)
			break;

		CBaseHandle serverHandle = hasServerEnt ? (**serverIt).GetRefEHandle() : CBaseHandle{};
		CBaseHandle clientHandle = hasClientEnt ? (**clientIt).GetRefEHandle() : CBaseHandle{};
		const char* serverName = hasServerEnt ? utils::spt_serverEntList.NetworkClassName(*serverIt) : "<null>";
		serverName = serverName ? serverName : "<null>";
		const char* clientName = hasClientEnt ? utils::spt_clientEntList.NetworkClassName(*clientIt) : "<null>";
		clientName = clientName ? clientName : "<null>";

		if (serverHandle.GetEntryIndex() == clientHandle.GetEntryIndex())
		{
			Msg("[%4d] SERVER (serial %5d) : %s, CLIENT (serial %5d) : %s\n",
			    serverHandle.GetEntryIndex(),
			    serverHandle.GetSerialNumber(),
			    serverName,
			    clientHandle.GetSerialNumber(),
			    clientName);
			++serverIt;
			++clientIt;
		}
		else if (serverHandle.GetEntryIndex() < clientHandle.GetEntryIndex())
		{
			Msg("[%4d] SERVER (serial %5d) : %s\n",
			    serverHandle.GetEntryIndex(),
			    serverHandle.GetSerialNumber(),
			    serverName);
			++serverIt;
		}
		else
		{
			Msg("[%4d] CLIENT (serial %5d) : %s\n",
			    clientHandle.GetEntryIndex(),
			    clientHandle.GetSerialNumber(),
			    clientName);
			++clientIt;
		}
	}
}

CON_COMMAND(y_spt_print_ent_props, "Prints all props for a given entity index.")
{
	if (args.ArgC() < 2)
	{
		Msg("Usage: spt_print_ent_props [index]\n");
	}
	else
	{
		utils::PrintAllProps((int)strtol(args.Arg(1), nullptr, 10));
	}
}

CON_COMMAND(_y_spt_datamap_print, "Prints all datamaps.")
{
	spt_entprops.PrintDatamaps();
}

CON_COMMAND(_y_spt_datamap_walk, "Walk through a datamap and print all offsets.")
{
	spt_entprops.WalkDatamap(args.Arg(1));
}

void EntProps::LoadFeature()
{
	InitCommand(_y_spt_datamap_print);
	InitCommand(_y_spt_datamap_walk);
	InitCommand(y_spt_canjb);
	InitCommand(y_spt_print_ents);
	InitCommand(y_spt_print_ent_props);

#if defined(SPT_HUD_ENABLED) && defined(SPT_PORTAL_UTILS)
	if (utils::DoesGameLookLikePortal())
	{
		bool hudEnabled = AddHudCallback(
		    "portal_bubble",
		    [this](std::string)
		    { spt_hud_feat.DrawTopHudElement(L"portal bubble: %d", !!utils::GetEnvironmentPortal()); },
		    y_spt_hud_portal_bubble);

		if (hudEnabled)
			SptImGui::RegisterHudCvarCheckbox(y_spt_hud_portal_bubble);
	}
#endif

#ifdef SPT_HUD_ENABLED
	bool result = spt_hud_feat.AddHudCallback("ent_info",
	                                          HudCallback(
	                                              [](std::string args)
	                                              {
		                                              std::string info;
		                                              if (args == "")
			                                              info = y_spt_hud_ent_info.GetString();
		                                              else
			                                              info = args;
		                                              if (!whiteSpacesOnly(info))
		                                              {
			                                              int entries =
			                                                  utils::FillInfoArray(info,
			                                                                       INFO_ARRAY,
			                                                                       MAX_ENTRIES,
			                                                                       INFO_BUFFER_SIZE,
			                                                                       PROP_SEPARATOR,
			                                                                       ENT_SEPARATOR);
			                                              for (int i = 0; i < entries; ++i)
			                                              {
				                                              spt_hud_feat.DrawTopHudElement(
				                                                  INFO_ARRAY + i * INFO_BUFFER_SIZE);
			                                              }
		                                              }
	                                              },
	                                              []() { return true; },
	                                              false));

	if (result)
	{
		InitConcommandBase(y_spt_hud_ent_info);
		SptImGui::RegisterHudCvarCallback(y_spt_hud_ent_info, ImGuiEntInfoCvarCallback, true);
	}
#endif
}

void EntProps::ImGuiEntInfoCvarCallback(ConVar& var)
{
	SptImGui::CvarValue(var, SptImGui::CVF_ALWAYS_QUOTE);

	const char* oldVal = var.GetString();
	static ImGuiTextBuffer newVal;
	newVal.Buf.resize(0);
	newVal.append(""); // explicit null terminator
	bool updateCvar = false;

	int nEnts = 0;
	int nRegexesForEnt = 0;

	int i = 0;
	int len = strlen(oldVal);

	if (whiteSpacesOnly(oldVal))
		i = len;

	/*
	* Goal:
	* 
	* - Parse the cvar value. The logic for this is intentially meant to be a duplicate of
	*   utils::FillInfoArray(). The exact flow control is slightly different in order to handle
	*   some ImGui state, but the parsed entity indices & regex should be the same.
	* 
	* - As we parse, reconstruct the cvar value from left to right. If the user interacts with the
	*   GUI, edit the reconstructed value on the fly (e.g. add an extra regex, drop an ent index,
	*   etc.) and set the cvar value at the end.
	*/

	// go up to len so that we can cleanup the stack ID, indent, etc. w/o duplicating code
	while (i <= len && nEnts < MAX_ENTRIES)
	{
		bool atEnd = i >= len;
		bool readEntityIndex = !atEnd && (i == 0 || oldVal[i] == ENT_SEPARATOR);
		i += i > 0 || nEnts > 0;
		int nextSepIdx = i;

		// find the next separator
		while (nextSepIdx < len && oldVal[nextSepIdx] != PROP_SEPARATOR && oldVal[nextSepIdx] != ENT_SEPARATOR)
			++nextSepIdx;

		// new entity index, we're at the end, or parse error - cleanup ID, indent, etc.
		if ((readEntityIndex || atEnd) && nEnts > 0)
		{
			if (SptImGui::SmallIconButton(ICON_CI_ADD))
			{
				newVal.append(",.*");
				updateCvar = true;
			}
			ImGui::SetItemTooltip("add regex");
			ImGui::PopID(); // pop nEnts ID
			ImGui::Unindent();
			SptImGui::EndBordered();
		}

		if (atEnd)
			break;

		// read an index or a regex

		if (readEntityIndex)
		{
			SptImGui::BeginBordered();

			long long entIndex = atoll(oldVal + i);
			IClientEntity* ent = utils::spt_clientEntList.GetEnt(entIndex);
			nRegexesForEnt = 0;

			if (SptImGui::InputTextInteger("entity index,", "", entIndex, 10))
				updateCvar = true;

			ImGui::SameLine();
			if (ent)
				ImGui::Text("(class name: %s)", ent->GetClientClass()->GetName());
			else
				ImGui::TextColored(SPT_IMGUI_WARN_COLOR_YELLOW,
				                   ICON_CI_WARNING " invalid entity " ICON_CI_WARNING);

			ImGui::PushID(nEnts++);

			ImGui::SameLine();
			bool includeEntInNewVal = !SptImGui::SmallIconButton(ICON_CI_TRASH);
			ImGui::SetItemTooltip("remove entity");
			if (includeEntInNewVal)
			{
				// newVal.empty() instead of e.g. (i == 0) or (nEnts == 0) prevents weird parsing edge cases
				newVal.appendf("%s%" PRId64, newVal.empty() ? "" : ";", entIndex);
			}
			else
			{
				/*
				* Jump ahead to the next entity. Note that we still entered the ID & indent scope
				* for this entity - that will be popped in the next loop. This jump means that the
				* newVal will not get any data (including the index) associated with this entity.
				* This does cause one broken frame because the button for the ent deletion must
				* exist but I don't draw the regexes for that entity. This is fixable but I cbf.
				*/
				while (nextSepIdx < len && oldVal[nextSepIdx] != ENT_SEPARATOR)
					++nextSepIdx;
				updateCvar = true;
			}
			ImGui::Indent();
		}
		else
		{
			ImGui::PushID(nRegexesForEnt++);

			const char* regexStart = oldVal + i;
			int regexLen = nextSepIdx - i;
			char regexBuf[INFO_BUFFER_SIZE];
			strncpy_s(regexBuf, regexStart, regexLen);
			// don't allow the characters: ;,"
			if (ImGui::InputTextEx(
			        "##regex",
			        "enter regex",
			        regexBuf,
			        sizeof regexBuf,
			        ImVec2{0, 0},
			        ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_CallbackCharFilter,
			        [](ImGuiInputTextCallbackData* data) -> int
			        { return data->EventChar >= 256 || strchr(";,\"", (char)data->EventChar); },
			        nullptr))
			{
				updateCvar = true;
			}
			if (ImGui::IsItemHovered() && regexBuf[0] != '\0')
				ImGui::SetItemTooltip("enter regex");
			ImGui::SameLine();
			if (SptImGui::SmallIconButton(ICON_CI_TRASH))
				updateCvar = true;
			else
				newVal.appendf(",%s", regexBuf);
			ImGui::SetItemTooltip("remove regex");

			// test if the regex is valid, report error if not
			try
			{
				std::regex re{regexBuf};
			}
			catch (const std::regex_error& exp)
			{
				ImGui::SameLine();
				ImGui::TextColored(SPT_IMGUI_WARN_COLOR_YELLOW,
				                   ICON_CI_WARNING " invalid regex " ICON_CI_WARNING);
				ImGui::SetItemTooltip("%s", exp.what());
			}

			ImGui::PopID();
		}

		i = nextSepIdx;
	}

	if (SptImGui::SmallIconButton(ICON_CI_ADD))
	{
		newVal.appendf("%s-1,.*", nEnts == 0 ? "" : ";");
		updateCvar = true;
	}
	ImGui::SetItemTooltip("add entity index");

	if (updateCvar)
		var.SetValue(newVal.c_str());
}

void** _InternalPlayerField::GetServerPtr() const
{
	auto serverplayer = utils::spt_serverEntList.GetPlayer();
	if (serverplayer && serverOffset != utils::INVALID_DATAMAP_OFFSET)
		return reinterpret_cast<void**>(reinterpret_cast<uint32_t>(serverplayer) + serverOffset);
	else
		return nullptr;
}

void** _InternalPlayerField::GetClientPtr() const
{
	auto clientPlayer = utils::spt_clientEntList.GetPlayer();
	if (clientPlayer && clientOffset != utils::INVALID_DATAMAP_OFFSET)
		return reinterpret_cast<void**>(reinterpret_cast<uint32_t>(clientPlayer) + clientOffset);
	else
		return nullptr;
}

bool _InternalPlayerField::ClientOffsetFound() const
{
	return clientOffset != utils::INVALID_DATAMAP_OFFSET;
}

bool _InternalPlayerField::ServerOffsetFound() const
{
	return serverOffset != utils::INVALID_DATAMAP_OFFSET;
}
