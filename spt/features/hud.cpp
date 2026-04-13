#include "stdafx.hpp"

#include "hud.hpp"

#ifdef SPT_HUD_ENABLED

#include <algorithm>
#include "overlay.hpp"
#include "convar.hpp"
#include "game_detection.hpp"
#include "interfaces.hpp"
#include "tier0\basetypes.h"
#include "..\vgui\vgui_utils.hpp"
#include "string_utils.hpp"

extern ConVar _y_spt_overlay;

HUDFeature spt_hud_feat;

ConVar y_spt_hud("y_spt_hud", "1", 0, "When set to 1, displays SPT HUD.");
ConVar y_spt_hud_left("y_spt_hud_left", "0", FCVAR_CHEAT, "When set to 1, displays SPT HUD on the left.");

const std::string FONT_DefaultFixedOutline = "DefaultFixedOutline";
const std::string FONT_Trebuchet20 = "Trebuchet20";
const std::string FONT_Trebuchet24 = "Trebuchet24";
const std::string FONT_HudNumbers = "HudNumbers";

Color StringToColor(const std::string& color)
{
	int len = color.size();
	if (len != 6 && len != 8)
	{
		return Color(0, 0, 0);
	}
	int shift = len == 6 ? 16 : 24;
	unsigned int hex = std::stoul(color, NULL, 16);
	int r, g, b, a = 0xff;
	r = (hex >> shift) & 0xff;
	shift -= 8;
	g = (hex >> shift) & 0xff;
	shift -= 8;
	b = (hex >> shift) & 0xff;
	if (len == 8)
		a = hex & 0xff;
	return Color(r, g, b, a);
}

std::string ColorToString(Color color)
{
	char hexString[9];
	std::snprintf(hexString, sizeof(hexString), "%02X%02X%02X%02X", color.r(), color.g(), color.b(), color.a());
	return std::string(hexString);
}

static int HudGroupCompletionFunc(AUTOCOMPLETION_FUNCTION_PARAMS)
{
	std::string s(partial);
	size_t pos = s.find_last_of(' ') + 1;
	std::string base = s.substr(0, pos);
	std::string incomplete = s.substr(pos);

	std::istringstream iss(base);
	std::vector<std::string> args{std::istream_iterator<std::string>{iss}, std::istream_iterator<std::string>{}};

	int argc = args.size();
	switch (argc)
	{
	case 0:
		return 0;
	case 1:
	{
		std::vector<std::string> suggestions;
		for (const auto& group : spt_hud_feat.hudUserGroups)
		{
			suggestions.push_back(group.first);
		}
		suggestions.push_back("all");
		return AutoCompleteList::AutoCompleteSuggest(base, incomplete, suggestions, false, commands);
	}
	case 2:
	{
		if (args[1] == "all" || spt_hud_feat.hudUserGroups.contains(args[1]))
			return AutoCompleteList::AutoCompleteSuggest(base,
			                                             incomplete,
			                                             {
			                                                 "add",
			                                                 "remove",
			                                                 "edit",
			                                                 "show",
			                                                 "hide",
			                                                 "toggle",
			                                                 "x",
			                                                 "y",
			                                                 "pos",
			                                                 "font",
			                                                 "textcolor",
			                                                 "background",
			                                             },
			                                             false,
			                                             commands);
		return 0;
	}
	case 3:
	case 4:
	{
		if (args[2] == "add" || (argc == 4 && args[2] == "edit"))
		{
			std::vector<std::string> suggestions;
			for (const auto& group : spt_hud_feat.hudCallbacks)
			{
				suggestions.push_back(group.first);
			}
			return AutoCompleteList::AutoCompleteSuggest(base, incomplete, suggestions, false, commands);
		}
		return 0;
	}

	default:
		return 0;
	}
}

CON_COMMAND_F_COMPLETION(spt_hud_group,
                         "Modifies parameters in given HUD group.\n"
                         "HUD element:\n"
                         "    spt_hud_group <group name|all> add <HUD element> [args]\n"
                         "    spt_hud_group <group name|all> remove <index>\n"
                         "    spt_hud_group <group name|all> edit <index> <HUD element> [args]\n"
                         "Display: spt_hud_group <group name|all> <show|hide|toggle>\n"
                         "Attributes: spt_hud_group <group name|all> <x|y|pos|font|textcolor|background> [value]",
                         0,
                         HudGroupCompletionFunc)
{
	if (args.ArgC() < 2)
	{
		for (const auto& group : spt_hud_feat.hudUserGroups)
		{
			Msg("%s\n", group.first.c_str());
		}
		return;
	}

	auto commandAction = [args](std::string name) -> bool
	{
		HudUserGroup& group = spt_hud_feat.hudUserGroups[name];

		int argc = args.ArgC();
		if (argc == 2)
		{
			Msg("%s:\n"
			    "%s\n",
			    name.c_str(),
			    group.ToString().c_str());
			return true;
		}

		std::string param = args.Arg(2);
		if (param == "add")
		{
			if (argc != 4 && argc != 5)
			{
				Msg("Usage: spt_hud_group <group name|all> add <HUD element> [args]\n");
				return false;
			}

			std::string element = args.Arg(3);
			if (!spt_hud_feat.hudCallbacks.contains(element))
			{
				Msg("%s is not a HUD element.\n", element.c_str());
				return false;
			}
			group.callbacks.push_back({element, argc == 5 ? args.Arg(4) : ""});
		}
		else if (param == "remove")
		{
			if (argc != 4)
			{
				Msg("Usage: spt_hud_group <group name|all> remove <index>\n");
				return false;
			}
			int index = std::atoi(args.Arg(3));
			if (index < 0 || (unsigned)index >= group.callbacks.size())
			{
				Msg("Invalid HUD element index.\n");
				return false;
			}
			group.callbacks.erase(group.callbacks.begin() + index);
		}
		else if (param == "edit")
		{
			if (argc != 5 && argc != 6)
			{
				Msg("spt_hud_group <group name|all> edit <index> <HUD element> [args]\n");
				return false;
			}
			int index = std::atoi(args.Arg(3));
			if (index < 0 || (unsigned)index >= group.callbacks.size())
			{
				Msg("Invalid HUD element index.\n");
				return false;
			}
			std::string element = args.Arg(4);
			if (!spt_hud_feat.hudCallbacks.contains(element))
			{
				Msg("%s is not a HUD element.\n", element.c_str());
				return false;
			}
			group.callbacks[index] = {element, argc == 6 ? args.Arg(5) : ""};
		}
		else if (param == "show")
		{
			group.shouldDraw = true;
		}
		else if (param == "hide")
		{
			group.shouldDraw = false;
		}
		else if (param == "toggle")
		{
			group.shouldDraw = !group.shouldDraw;
		}
		else if (param == "x" || param == "y" || param == "pos")
		{
			if (argc == 3)
			{
				Msg("%s: pos = (%.2f, %.2f)\n", name.c_str(), group.x, group.y);
			}
			else if (param == "pos")
			{
				if (argc != 5)
				{
					Msg("Usage: spt_hud_group <group name|all> pos [<x> <y>]\n");
					return false;
				}
				group.x = std::atof(args.Arg(3));
				group.y = std::atof(args.Arg(4));
			}
			else
			{
				// x y
				if (argc != 4)
				{
					Msg("Usage: spt_hud_group <group name|all> <x|y> [value]\n");
					return false;
				}
				float value = std::atof(args.Arg(3));
				if (param == "x")
					group.x = value;
				else
					group.y = value;
			}
		}
		else if (param == "font")
		{
			if (argc == 3)
			{
				std::string fontName = "Invalid";
				for (const auto& kv : spt_hud_feat.fonts)
				{
					if (kv.second == group.font)
					{
						fontName = kv.first;
						break;
					}
				}
				Msg("%s: font = %s\n", name.c_str(), fontName.c_str());
			}
			else if (argc == 4)
			{
				if (!spt_hud_feat.GetFont(args.Arg(3), group.font))
				{
					Msg("Invalid font name.\n");
					return false;
				}
			}
			else
			{
				Msg("Usage: spt_hud_group <group name|all> font [name]\n");
				return false;
			}
		}
		else if (param == "textcolor" || param == "background")
		{
			Color& elementColor = (param == "textcolor") ? group.textcolor : group.background;
			if (argc == 3)
			{
				Msg("%s: %s = %s\n", name.c_str(), param.c_str(), ColorToString(elementColor).c_str());
			}
			else if (argc == 4)
			{
				elementColor = StringToColor(args.Arg(3));
			}
			else
			{
				Msg("Usage: spt_hud_group <group name|all> <textcolor|background> [color]\n");
				return false;
			}
		}
		else
		{
			Msg("Invalid argument %s.\n"
			    "Params: add, remove, show, hide, toggle, x, y, pos, font, textcolor, background.\n",
			    param.c_str());
			return false;
		}
		return true;
	};

	std::string name = args.Arg(1);
	if (name == "all")
	{
		for (auto& kv : spt_hud_feat.hudUserGroups)
		{
			if (!commandAction(kv.first))
				return;
		}
	}
	else if (spt_hud_feat.hudUserGroups.contains(name))
	{
		commandAction(name);
	}
	else
	{
		Msg("Group %s does not exist!\n", name.c_str());
	}
}

CON_COMMAND(spt_hud_group_add, "Add a HUD group. Usage: spt_hud_group_add <group name>")
{
	if (args.ArgC() != 2)
	{
		Msg("Usage: spt_hud_group_add <group name>\n");
		return;
	}

	std::string name = args.Arg(1);
	if (name == "all")
	{
		Msg("\"all\" cannot be a group name!\n");
	}
	else if (spt_hud_feat.hudUserGroups.contains(name))
	{
		Msg("Group %s already exist!\n", name.c_str());
	}
	else
	{
		spt_hud_feat.hudUserGroups[name] = HudUserGroup();
	}
}

static int HudGroupRemoveCompletionFunc(AUTOCOMPLETION_FUNCTION_PARAMS)
{
	std::vector<std::string> suggestions;
	for (const auto& group : spt_hud_feat.hudUserGroups)
	{
		suggestions.push_back(group.first);
	}
	suggestions.push_back("all");

	AutoCompleteList completeList(suggestions);
	return completeList.AutoCompletionFunc(partial, commands);
}

CON_COMMAND_F_COMPLETION(spt_hud_group_remove,
                         "Remove HUD group(s). Usage: spt_hud_group_remove <group name|all>",
                         0,
                         HudGroupRemoveCompletionFunc)
{
	if (args.ArgC() != 2)
	{
		Msg("Usage: spt_hud_group_remove <group name|all>\n");
		return;
	}

	std::string name = args.Arg(1);
	if (name == "all")
	{
		spt_hud_feat.hudUserGroups.clear();
	}
	else if (spt_hud_feat.hudUserGroups.contains(name))
	{
		spt_hud_feat.hudUserGroups.erase(name);
	}
	else
	{
		Msg("Group %s does not exist!\n", name.c_str());
	}
}

namespace patterns
{
	PATTERNS(CEngineVGui__Paint,
	         "5135",
	         "83 EC 18 56 6A 04 6A 00",
	         "BMS-Retail-0.9",
	         "55 8B EC 83 EC 18 53 8B D9 8B 0D ?? ?? ?? ?? FF 15 ?? ?? ?? ?? 88 45 FE 84 C0",
	         "7462488",
	         "55 8B EC 83 EC 2C 53 8B D9 8B 0D ?? ?? ?? ?? 56",
	         "4044",
	         "6A FF 68 ?? ?? ?? ?? 64 A1 ?? ?? ?? ?? 50 64 89 25 ?? ?? ?? ?? 83 EC 1C 56 6A 04",
	         "5288",
	         "55 8B EC 83 EC 30 A1 ?? ?? ?? ?? 53 56 8B F1 33 C9");
	PATTERNS(
	    CMatSystemSurface__StartDrawing,
	    "5135",
	    "55 8B EC 83 E4 C0 83 EC 38 80 ?? ?? ?? ?? ?? ?? 56 57 8B F9 75 57 8B ?? ?? ?? ?? ?? C6 ?? ?? ?? ?? ?? ?? FF ?? 8B 10 6A 00 8B C8 8B 42 20",
	    "7462488",
	    "55 8B EC 64 A1 ?? ?? ?? ?? 6A FF 68 ?? ?? ?? ?? 50 64 89 25 ?? ?? ?? ?? 83 EC 14",
	    "BMS-Retail-0.9",
	    "55 8B EC 6A FF 68 ?? ?? ?? ?? 64 A1 ?? ?? ?? ?? 50 83 EC 14 56 57 A1 ?? ?? ?? ?? 33 C5 50 8D 45 F4 64 A3 ?? ?? ?? ?? 8B F9 80 3D ?? ?? ?? ?? 00");
	PATTERNS(
	    CMatSystemSurface__FinishDrawing,
	    "5135",
	    "56 6A 00 E8 ?? ?? ?? ?? 8B ?? ?? ?? ?? ?? 8B 01 8B ?? ?? ?? ?? ?? 83 C4 04 FF D2 8B F0 85 F6 74 09 8B 06 8B 50 08 8B CE FF D2",
	    "7462488",
	    "55 8B EC 6A FF 68 ?? ?? ?? ?? 64 A1 ?? ?? ?? ?? 50 64 89 25 ?? ?? ?? ?? 51 56 6A 00",
	    "BMS-Retail-0.9",
	    "55 8B EC 6A FF 68 ?? ?? ?? ?? 64 A1 ?? ?? ?? ?? 50 51 56 A1 ?? ?? ?? ?? 33 C5 50 8D 45 ?? 64 A3 ?? ?? ?? ?? 6A 00");
} // namespace patterns

void HUDFeature::InitHooks()
{
	FIND_PATTERN(vguimatsurface, CMatSystemSurface__StartDrawing);
	FIND_PATTERN(vguimatsurface, CMatSystemSurface__FinishDrawing);
	HOOK_FUNCTION(engine, CEngineVGui__Paint);
}

bool HUDFeature::AddHudCallback(std::string key, HudCallback callback)
{
	if (!this->loadingSuccessful)
		return false;
	if (hudCallbacks.contains(key))
		return false;
	hudCallbacks[key] = callback;
	return true;
}

bool HUDFeature::AddHudDefaultGroup(HudCallback callback)
{
	if (!this->loadingSuccessful)
		return false;
	hudDefaultCallbacks.push_back(callback);
	return true;
}

void HUDFeature::vDrawTopHudElement(Color color, const wchar* format, va_list args)
{
	const wchar* text = FormatTempString(format, args);

	if (!drawText)
	{
		int tw, th;
		interfaces::surface->GetTextSize(font, text, tw, th);
		if (tw > textWidth)
			textWidth = tw;
		textHeight += th + 2;
		return;
	}

	interfaces::surface->DrawSetTextFont(font);
	interfaces::surface->DrawSetTextColor(color);
	interfaces::surface->DrawSetTextPos(topX, 2 + topY + (topFontTall + 2) * topVertIndex);
	interfaces::surface->DrawPrintText(text);
	++topVertIndex;
}

void HUDFeature::DrawTopHudElement(const wchar* format, ...)
{
	va_list args;
	va_start(args, format);
	vDrawTopHudElement(hudTextColor, format, args);
	va_end(args);
}

void HUDFeature::DrawColorTopHudElement(Color color, const wchar* format, ...)
{
	va_list args;
	va_start(args, format);
	vDrawTopHudElement(color, format, args);
	va_end(args);
}

bool HUDFeature::ShouldLoadFeature()
{
	return interfaces::surface && !utils::DoesGameLookLikeBMSMod();
}

bool HUDFeature::GetFont(const std::string& fontName, vgui::HFont& fontOut)
{
	if (!loadingSuccessful)
	{
		return false;
	}

	if (fonts.find(fontName) != fonts.end())
	{
		fontOut = fonts[fontName];
		return true;
	}
	else
	{
		auto scheme = vgui::GetScheme();

		if (scheme)
		{
			fontOut = scheme->GetFont(fontName.c_str());
			fonts[fontName] = fontOut;
			return true;
		}
		else
		{
			return false;
		}
	}
}

void HUDFeature::PreHook()
{
	loadingSuccessful = !!ORIG_CEngineVGui__Paint;
}

void HUDFeature::LoadFeature()
{
	if (!loadingSuccessful)
		return;

	cl_showpos = g_pCVar->FindVar("cl_showpos");
	cl_showfps = g_pCVar->FindVar("cl_showfps");
	bool result = spt_hud_feat.AddHudDefaultGroup(
	    HudCallback(std::bind(&HUDFeature::DrawDefaultHUD, this), []() { return y_spt_hud.GetBool(); }, false));

	if (ORIG_CMatSystemSurface__StartDrawing && ORIG_CMatSystemSurface__FinishDrawing)
	{
		// These two functions are optional. HUD looks better with them, but it can work without them too (just with some visual glitches).
		foundDrawingFuncs = true;
	}

	if (result)
	{
		InitConcommandBase(y_spt_hud);
		InitConcommandBase(y_spt_hud_left);
		InitCommand(spt_hud_group);
		InitCommand(spt_hud_group_add);
		InitCommand(spt_hud_group_remove);
	}
}

void HUDFeature::UnloadFeature()
{
	cl_showpos = cl_showfps = nullptr;
}

void HUDFeature::DrawDefaultHUD()
{
	if (!GetFont(FONT_DefaultFixedOutline, font))
		return;

	try
	{
		// Reset top HUD stuff
		drawText = true;
		hudTextColor = Color(255, 255, 255, 255);
		topVertIndex = 0;
		topFontTall = interfaces::surface->GetFontTall(font);
		if (y_spt_hud_left.GetBool())
		{
			topX = 6;
		}
		else
		{
			topX = screen.width - 300 + 2;
			if (cl_showpos && cl_showpos->GetBool())
				topVertIndex += 3;
			if (cl_showfps && cl_showfps->GetBool())
				++topVertIndex;
		}

		topY = 0;

		for (auto& kv : hudCallbacks)
		{
			auto& callback = kv.second;
			if (callback.shouldDraw())
				callback.draw("");
		}
	}
	catch (const std::exception& e)
	{
		Msg("Error drawing HUD: %s\n", e.what());
	}
}

void HUDFeature::DrawHUD(bool overlay)
{
	if (foundDrawingFuncs)
		ORIG_CMatSystemSurface__StartDrawing(interfaces::mat_system_surface);

	try
	{
		// Draw default callbacks
		for (auto& callback : hudDefaultCallbacks)
		{
#ifdef SPT_OVERLAY_ENABLED
			if (overlay == callback.drawInOverlay && callback.shouldDraw())
				callback.draw("");
#else
			if (!callback.drawInOverlay && callback.shouldDraw())
				callback.draw("");
#endif
		}

#ifndef SPT_HUD_TEXTONLY
		if (!overlay)
		{
			// Draw user groups
			for (const auto& kv : hudUserGroups)
			{
				const auto& group = kv.second;
				if (!group.shouldDraw)
					continue;

				// Reset top HUD stuff
				hudTextColor = group.textcolor;
				topX = screen.width * group.x * 0.01f;
				topY = screen.height * group.y * 0.01f;
				topVertIndex = 0;
				topFontTall = interfaces::surface->GetFontTall(group.font);
				font = group.font;

				// Getting the text width
				if (group.background.a())
				{
					textWidth = 0;
					textHeight = 0;
					drawText = false;
					for (const auto& callback : group.callbacks)
					{
						hudCallbacks[callback.name].draw(callback.args);
					}

					interfaces::surface->DrawSetColor(group.background);
					interfaces::surface->DrawFilledRect(topX - 2,
					                                    topY,
					                                    topX + textWidth + 2,
					                                    topY + textHeight);
				}

				// Draw HUD
				drawText = true;
				for (const auto& callback : group.callbacks)
				{
					hudCallbacks[callback.name].draw(callback.args);
				}
			}
		}
#endif
	}
	catch (const std::exception& e)
	{
		Msg("Error drawing HUD: %s\n", e.what());
	}

	if (foundDrawingFuncs)
		ORIG_CMatSystemSurface__FinishDrawing(interfaces::mat_system_surface);
}

IMPL_HOOK_THISCALL(HUDFeature, void, CEngineVGui__Paint, void*, PaintMode_t mode)
{
	if (spt_hud_feat.loadingSuccessful && mode & PAINT_INGAMEPANELS)
	{
#ifdef SPT_OVERLAY_ENABLED
		/*
		* Getting the HUD & overlay to not fight turns out to be somewhat tricky. There might be a better way, but
		* the only approach that I've found so far is to draw the HUD for both the main view and overlay at the
		* same time, otherwise the HUD for the main view gets cleared. The old version did this from VGui_Paint(),
		* but the pattern for that isn't unique on steampipe so we use CEngineVGui::Paint() instead. By the time
		* this is called, we're rendering the main view (after the overlay) so spt_overlay has the pointers for
		* both views.
		*/
		if (spt_overlay.mainView)
		{
			spt_hud_feat.screen = Screen(spt_overlay.mainView->x,
			                             spt_overlay.mainView->y,
			                             spt_overlay.mainView->width,
			                             spt_overlay.mainView->height);
			spt_hud_feat.DrawHUD(false);
			if (_y_spt_overlay.GetBool())
			{
				spt_hud_feat.screen = Screen(spt_overlay.overlayView->x,
				                             spt_overlay.overlayView->y,
				                             spt_overlay.overlayView->width,
				                             spt_overlay.overlayView->height);
				spt_hud_feat.DrawHUD(true);
			}
		}
		else
#endif
		{
			int width, height;
			interfaces::surface->GetScreenSize(width, height);
			spt_hud_feat.screen = Screen(0, 0, width, height);
			spt_hud_feat.DrawHUD(false);
		}
	}
	spt_hud_feat.ORIG_CEngineVGui__Paint(thisptr, mode);
}

HudCallback::HudCallback() : drawInOverlay(false) {}

HudCallback::HudCallback(std::function<void(std::string)> drawable, std::function<bool()> shouldDrawable, bool overlay)
    : drawInOverlay{overlay}, draw{drawable}, shouldDraw{shouldDrawable}
{
}

HudUserGroup::HudUserGroup() : x(0), y(0), shouldDraw(true), textcolor(255, 255, 255, 255), background(0, 0, 0, 0)
{
	if (!spt_hud_feat.GetFont(FONT_DefaultFixedOutline, font))
	{
		font = 0;
	}
}

std::string HudUserGroup::ToString() const
{
	std::stringstream ss;
	ss << "state: " << (shouldDraw ? "Shown" : "Hidden") << "\n";
	ss << "pos: (" << x << ", " << y << ")\n";

	std::string fontName = "Invalid";
	for (const auto& kv : spt_hud_feat.fonts)
	{
		if (kv.second == font)
		{
			fontName = kv.first;
			break;
		}
	}
	ss << "font: " << fontName << "\n";

	ss << "textcolor: " << ColorToString(textcolor) << "\n";
	ss << "background: " << ColorToString(background) << "\n";

	ss << "HUD elements:\n";

	for (size_t i = 0; i < callbacks.size(); i++)
	{
		ss << i << ": " << callbacks[i].name << " " << callbacks[i].args << "\n";
	}

	return ss.str();
}

#endif
