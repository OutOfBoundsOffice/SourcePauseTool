#include "stdafx.hpp"
#include "tas.hpp"
#include "convar.hpp"
#include "..\sptlib-wrapper.hpp"
#include "..\strafe\strafestuff.hpp"
#include "..\scripts\srctas_reader.hpp"
#include "aim.hpp"
#include "generic.hpp"
#include "hud.hpp"
#include "ent_utils.hpp"
#include "playerio.hpp"
#include "interfaces.hpp"
#include "tracing.hpp"
#include "tickrate.hpp"
#include "signals.hpp"
#include "visualizations/imgui/imgui_interface.hpp"
#include "spt/features/game_fixes/rng.hpp"
#include "spt/utils/serialize.hpp"
#include "spt/utils/file.hpp"
#include "spt/utils/game_detection.hpp"
#include "spt/spt-serverplugin.hpp"

#include <format>

ConVar tas_strafe("tas_strafe", "0", FCVAR_TAS_RESET);
ConVar tas_strafe_type(
    "tas_strafe_type",
    "0",
    FCVAR_TAS_RESET,
    "TAS strafe types:\n\t0 - Max acceleration strafing,\n\t1 - Max angle strafing.\n\t2 - Max accel strafing with a speed cap.\n\t3 - W strafing.\n");
ConVar tas_strafe_dir(
    "tas_strafe_dir",
    "3",
    FCVAR_TAS_RESET,
    "TAS strafe dirs:\n\t0 - to the left,\n\t1 - to the right,\n\t3 - to the yaw given in spt_tas_strafe_yaw.");
ConVar tas_strafe_scale("tas_strafe_scale",
                        "1",
                        FCVAR_TAS_RESET,
                        "Scales TAS input, can be used for partial inputs.\n",
                        true,
                        0.0f,
                        true,
                        1.0f);
ConVar tas_strafe_yaw("tas_strafe_yaw", "", FCVAR_TAS_RESET, "Yaw to strafe to with spt_tas_strafe_dir = 3.");
ConVar tas_strafe_buttons(
    "tas_strafe_buttons",
    "",
    FCVAR_TAS_RESET,
    "Sets the strafing buttons. The format is 4 digits: \"<AirLeft> <AirRight> <GroundLeft> <GroundRight>\". The default (auto-detect) is empty string: \"\".\n"
    "Table of buttons:\n\t0 - W\n\t1 - WA\n\t2 - A\n\t3 - SA\n\t4 - S\n\t5 - SD\n\t6 - D\n\t7 - WD\n");
ConVar tas_strafe_vectorial("tas_strafe_vectorial",
                            "0",
                            FCVAR_TAS_RESET,
                            "Determines if strafing uses vectorial strafing\n");
ConVar tas_strafe_vectorial_increment(
    "tas_strafe_vectorial_increment",
    "2.5",
    FCVAR_TAS_RESET,
    "Has no effect on spt_tas_strafe_version >= 4. Determines how fast the player yaw angle moves towards the target yaw angle. 0 for no movement, 180 for instant snapping. Has no effect on strafing speed.\n");
ConVar tas_strafe_vectorial_offset("tas_strafe_vectorial_offset",
                                   "0",
                                   FCVAR_TAS_RESET,
                                   "Determines the target view angle offset from spt_tas_strafe_yaw\n");
ConVar tas_strafe_vectorial_snap(
    "tas_strafe_vectorial_snap",
    "170",
    FCVAR_TAS_RESET,
    "Determines when the yaw angle snaps to the target yaw. Mainly used to prevent ABHing from resetting the yaw angle to the back on every jump.\n");
ConVar tas_strafe_allow_jump_override(
    "tas_strafe_allow_jump_override",
    "0",
    FCVAR_TAS_RESET,
    "Determines if the setyaw/pitch commands are ignored when jumping + TAS strafing. Primarily used in search mode for stucklaunches when the exact time of the jump isn't known prior to running the script.\n");
ConVar tas_strafe_capped_limit("tas_strafe_capped_limit",
                               "299.99",
                               FCVAR_TAS_RESET,
                               "Determines the speed cap while using capped strafing(type 4).\n");
ConVar tas_strafe_hull_is_line(
    "tas_strafe_hull_is_line",
    "0",
    FCVAR_TAS_RESET,
    "Treats the collision hull as a line for ground checks. A hack to fix ground detections while going through portals.");
ConVar tas_strafe_use_tracing(
    "tas_strafe_use_tracing",
    "1",
    FCVAR_TAS_RESET,
    "Use tracing for detecting the ground and whether or not the player can unduck. Only turn off to enable backwards compability with old scripts.");

ConVar tas_force_airaccelerate(
    "tas_force_airaccelerate",
    "",
    0,
    "Sets the value of airaccelerate used in TAS calculations. If empty, uses the value of sv_airaccelerate.\n\nShould be set to 15 for Portal.\n");
ConVar tas_force_wishspeed_cap(
    "tas_force_wishspeed_cap",
    "",
    0,
    "Sets the value of the wishspeed cap used in TAS calculations. If empty, uses the default value: 30.\n\nShould be set to 60 for Portal.\n");
ConVar tas_reset_surface_friction(
    "tas_reset_surface_friction",
    "1",
    0,
    "If enabled, the surface friction is assumed to be reset in the beginning of CategorizePosition().\n\nShould be set to 0 for Portal.\n");

ConVar tas_force_onground(
    "tas_force_onground",
    "0",
    FCVAR_TAS_RESET,
    "If enabled, strafing assumes the player is on ground regardless of what the prediction indicates. Useful for save glitch in Portal where the prediction always reports the player being in the air.\n");
ConVar tas_strafe_version("tas_strafe_version",
                          "9",
                          FCVAR_TAS_RESET,
                          "Strafe version. For backwards compatibility with old scripts.");

ConVar tas_strafe_afh_length("tas_strafe_afh_length", "0.0000000000000000001", FCVAR_TAS_RESET, "Magnitude of AFHs");
ConVar tas_strafe_afh("tas_strafe_afh", "0", FCVAR_TAS_RESET, "Should AFH?");
ConVar tas_strafe_lgagst(
    "tas_strafe_lgagst",
    "0",
    FCVAR_TAS_RESET,
    "If enabled, jumps automatically when it's faster to move in the air than on ground. Incomplete, intended use is for spt_tas_strafe_glitchless only.\n");
ConVar tas_strafe_lgagst_minspeed(
    "tas_strafe_lgagst_minspeed",
    "30",
    FCVAR_TAS_RESET,
    "Prevents LGAGST from triggering when the player speed is below this value. The default should be fine.");
ConVar tas_strafe_lgagst_fullmaxspeed(
    "tas_strafe_lgagst_fullmaxspeed",
    "0",
    FCVAR_TAS_RESET,
    "If enabled, LGAGST assumes the player is standing regardless of the actual ducking state. Useful for when you land while crouching but intend to stand up immediately.\n");
ConVar tas_strafe_lgagst_min("tas_strafe_lgagst_min", "150", FCVAR_TAS_RESET, "");
ConVar tas_strafe_lgagst_max("tas_strafe_lgagst_max", "270", FCVAR_TAS_RESET, "");
ConVar tas_strafe_jumptype(
    "tas_strafe_jumptype",
    "1",
    FCVAR_TAS_RESET,
    "TAS jump strafe types:\n\t0 - Does nothing,\n\t1 - Looks directly opposite to desired direction (for games with ABH),\n\t2 - Looks in desired direction (games with speed boost upon jumping but no ABH),\n\t3 - Looks in direction that results in greatest speed loss (for glitchless TASes on game with ABH).\n");
ConVar tas_strafe_autojb("tas_strafe_autojb",
                         "0",
                         FCVAR_TAS_RESET,
                         "ONLY JUMPBUGS WHEN POSSIBLE. IF IT DOESN'T JUMPBUG IT'S NOT POSSIBLE.\n");
ConVar tas_script_printvars("tas_script_printvars",
                            "1",
                            0,
                            "Prints variable information when running .srctas scripts.\n");
ConVar tas_script_savestates("tas_script_savestates", "1", 0, "Enables/disables savestates in .srctas scripts.\n");
ConVar tas_script_onsuccess("tas_script_onsuccess", "", 0, "Commands to be executed when a search concludes.\n");
ConVar y_spt_hud_script_progress("y_spt_hud_script_progress", "0", FCVAR_CHEAT, "Turns on the script progress hud.\n");

TASFeature spt_tas;

bool TASFeature::ShouldLoadFeature()
{
	return interfaces::engine_client != nullptr;
}

Strafe::StrafeInput GetStrafeInput(float forwardmove, float sidemove, float yaw)
{
	Strafe::StrafeInput input;
	input.Scale = tas_strafe_scale.GetFloat();
	input.Version = tas_strafe_version.GetInt();
	input.Strafe = tas_strafe.GetBool();
	input.VectorialOffset = tas_strafe_vectorial_offset.GetFloat();
	const float eps = 0.001f;

	if (tas_strafe_dir.GetInt() == static_cast<int>(Strafe::StrafeDir::BUTTONS))
	{
		double theta = std::atan2(-sidemove, forwardmove);

		if ((std::abs(forwardmove) < eps && std::abs(sidemove) < eps) || !std::isfinite(theta))
		{
			input.TargetYaw = utils::NormalizeDeg(yaw);
		}
		else
		{
			input.TargetYaw = utils::NormalizeDeg(yaw + utils::M_RAD2DEG * theta);
		}

		input.Vectorial = true;
		input.AFH = true;
		input.JumpOverride = false;
		input.AngleSpeed = 0.0f;
	}
	else
	{
		input.TargetYaw = tas_strafe_yaw.GetFloat();
		input.Vectorial = tas_strafe_vectorial.GetBool();
		input.AFH = tas_strafe_afh.GetBool();
		input.JumpOverride = tas_strafe_allow_jump_override.GetBool();
		input.AngleSpeed = tas_anglespeed.GetFloat();
	}

	return input;
}

void TASFeature::Strafe()
{
	float va[3];
	bool yawChanged = false;
	EngineGetViewAngles(va);

	float forwardmove, sidemove;
	spt_playerio.GetMoveInput(forwardmove, sidemove);
	auto input = GetStrafeInput(forwardmove, sidemove, va[YAW]);

	spt_aim.HandleAiming(va, yawChanged, input);

	if (spt_tas.tasAddressesWereFound && input.Strafe)
	{
		auto vars = spt_playerio.GetMovementVars();
		auto pl = spt_playerio.GetPlayerData();

		bool jumped = false;

		auto btns = Strafe::StrafeButtons();
		bool usingButtons = (sscanf(tas_strafe_buttons.GetString(),
		                            "%hhu %hhu %hhu %hhu",
		                            &btns.AirLeft,
		                            &btns.AirRight,
		                            &btns.GroundLeft,
		                            &btns.GroundRight)
		                     == 4);
		auto type = static_cast<Strafe::StrafeType>(tas_strafe_type.GetInt());
		auto dir = static_cast<Strafe::StrafeDir>(tas_strafe_dir.GetInt());

		Strafe::ProcessedFrame out;
		out.Jump = false;

		if (!vars.CantJump && vars.OnGround)
		{
			if (tas_strafe_lgagst.GetBool())
			{
				bool jump = Strafe::LgagstJump(pl, vars);
				if (jump)
				{
					vars.OnGround = false;
					out.Jump = true;
					jumped = true;
				}
			}

			if (spt_playerio.TryJump())
			{
				vars.OnGround = false;
				jumped = true;
			}
		}

		Strafe::Friction(pl, vars.OnGround, vars);

		if (input.Vectorial) // Can do vectorial strafing even with locked camera, provided we are not jumping
			Strafe::StrafeVectorial(pl, vars, input, jumped, type, dir, va[YAW], out, yawChanged);
		else if (!yawChanged) // not changing yaw, can do regular strafe
			Strafe::Strafe(pl, vars, input, jumped, type, dir, va[YAW], out, btns, usingButtons);

		spt_playerio.SetTASInput(va, out);
	}

	EngineSetViewAngles(va);
}

CON_COMMAND_AUTOCOMPLETEFILE(
    tas_script_load,
    "Loads and executes an .srctas script. If an extra ticks argument is given, the script is played back at maximal FPS and without rendering until that many ticks before the end of the script",
    0,
    "",
    ".srctas")
{
	if (args.ArgC() == 2)
		scripts::g_TASReader.ExecuteScript(args.Arg(1));
	else if (args.ArgC() == 3)
	{
		scripts::g_TASReader.ExecuteScriptWithResume(args.Arg(1), (int)strtol(args.Arg(2), nullptr, 10));
	}
	else
		Msg("Usage: spt_tas_load_script <script> [ticks]\n");
}

CON_COMMAND_AUTOCOMPLETEFILE(tas_script_search, "Starts a variable search for an .srctas script", 0, "", ".srctas")
{
	if (args.ArgC() > 1)
		scripts::g_TASReader.StartSearch(args.Arg(1));
	else
		Msg("Usage: spt_tas_load_search [script]\n");
}

CON_COMMAND(tas_script_result_success, "Signals a successful result in a variable search.")
{
	scripts::g_TASReader.SearchResult(scripts::SearchResult::Success);
}

CON_COMMAND(tas_script_result_fail, "Signals an unsuccessful result in a variable search.")
{
	scripts::g_TASReader.SearchResult(scripts::SearchResult::Fail);
}

CON_COMMAND(tas_script_result_stop, "Signals a stop in a variable search.")
{
	scripts::g_TASReader.SearchResult(scripts::SearchResult::NoSearch);
}

CON_COMMAND_F(spt_tas_script_new,
              "Generates a template .srctas script at the given path which loads the given save",
              FCVAR_DONTRECORD)
{
	if (args.ArgC() < 3)
	{
		Msg("Usage: %s <path> <save_name>\n", spt_tas_script_new_command.GetName());
		return;
	}

	std::filesystem::path filePath{GetGameDir()};
	filePath /= args.Arg(1);
	filePath += ".srctas";
	filePath = std::filesystem::absolute(filePath);

	std::string errMsg;
	if (!spt_tas.WriteTemplateScript(filePath.string().c_str(), args.Arg(2), errMsg))
		Warning("Failed to create template script: %s\n", errMsg.c_str());
	else
		Msg("Template script written to %s\n", filePath.string().c_str());
}

void TASFeature::LoadFeature()
{
	if (AfterFramesSignal.Works && interfaces::engine_client != nullptr)
	{
		InitCommand(tas_script_load);
		InitCommand(tas_script_search);
		InitCommand(tas_script_result_success);
		InitCommand(tas_script_result_fail);
		InitCommand(tas_script_result_stop);
		InitCommand(spt_tas_script_new);

		InitConcommandBase(tas_script_printvars);
		InitConcommandBase(tas_script_savestates);
		InitConcommandBase(tas_script_onsuccess);

		AfterFramesSignal.Connect(&scripts::g_TASReader, &scripts::SourceTASReader::OnAfterFrames);
#ifdef SPT_HUD_ENABLED
		bool hudEnabled = AddHudCallback(
		    "script_progress",
		    [this](std::string)
		    {
			    spt_hud_feat.DrawTopHudElement(L"frame: %d / %d",
			                                   scripts::g_TASReader.GetCurrentTick(),
			                                   scripts::g_TASReader.GetCurrentScriptLength());
		    },
		    y_spt_hud_script_progress);

		if (hudEnabled)
			SptImGui::RegisterHudCvarCheckbox(y_spt_hud_script_progress);
#endif
	}

	tasAddressesWereFound = spt_generic.ORIG_ControllerMove && spt_playerio.PlayerIOAddressesFound();

	if (tasAddressesWereFound)
	{
		InitConcommandBase(tas_strafe);
		InitConcommandBase(tas_strafe_type);
		InitConcommandBase(tas_strafe_dir);
		InitConcommandBase(tas_strafe_scale);
		InitConcommandBase(tas_strafe_yaw);
		InitConcommandBase(tas_strafe_buttons);
		InitConcommandBase(tas_strafe_vectorial);
		InitConcommandBase(tas_strafe_vectorial_increment);
		InitConcommandBase(tas_strafe_vectorial_offset);
		InitConcommandBase(tas_strafe_vectorial_snap);
		InitConcommandBase(tas_strafe_allow_jump_override);
		InitConcommandBase(tas_strafe_capped_limit);
		InitConcommandBase(tas_force_airaccelerate);
		InitConcommandBase(tas_force_wishspeed_cap);
		InitConcommandBase(tas_reset_surface_friction);
		InitConcommandBase(tas_force_onground);
		InitConcommandBase(tas_strafe_version);
		InitConcommandBase(tas_strafe_afh_length);
		InitConcommandBase(tas_strafe_afh);
		InitConcommandBase(tas_strafe_lgagst);
		InitConcommandBase(tas_strafe_lgagst_minspeed);
		InitConcommandBase(tas_strafe_lgagst_fullmaxspeed);
		InitConcommandBase(tas_strafe_lgagst_min);
		InitConcommandBase(tas_strafe_lgagst_max);
		InitConcommandBase(tas_strafe_jumptype);

		// Tracing related
		if (spt_tracing.CanTracePlayerBBox())
		{
			InitConcommandBase(tas_strafe_hull_is_line);
			InitConcommandBase(tas_strafe_use_tracing);
			InitConcommandBase(tas_strafe_autojb);
		}
		else
		{
			tas_strafe_use_tracing.SetValue(0);
		}
	}
}

bool TASFeature::WriteTemplateScript(const char* filePath, const char* saveName, std::string& errMsg)
{
	std::error_code ec;
	if (std::filesystem::exists(filePath, ec))
	{
		errMsg = "file already exists";
		return false;
	}

	auto wrangleName = [](const ConVar& cv) { return WrangleLegacyCommandName(cv.GetName(), true, nullptr); };

	std::vector<std::pair<bool, std::string>> settingsList = {
	    {true, std::format("{} {}", wrangleName(tas_strafe_version), tas_strafe_version.GetDefault())},
	    {true, std::format("{} 1", wrangleName(tas_strafe_vectorial))},
	    {true, std::format("{} 3", wrangleName(tas_strafe_dir))},
	    {true, "cl_mouseenable 0"},
	};

	if (utils::DoesGameLookLikePortal())
		settingsList.push_back({true, "sv_player_funnel_into_portals 1"});

	settingsList.push_back(
	    {y_spt_set_ivp_seed_on_load.IsRegistered(), std::format("{} 1", wrangleName(y_spt_set_ivp_seed_on_load))});
	settingsList.push_back(
	    {spt_set_physics_hook_offset_on_load.IsRegistered(),
	     std::format("{} {}", spt_set_physics_hook_offset_on_load.GetName(), spt_tickrate.GetTickrate())});
	settingsList.push_back({spt_set_all_sounds_available_after_load.IsRegistered(),
	                        std::format("{} 1", spt_set_all_sounds_available_after_load.GetName())});

	std::ostringstream settingsStream;
	for (const auto& setting : settingsList)
		settingsStream << (setting.first ? "" : "// ") << "settings " << setting.second << '\n';

	constexpr const char* scriptTemplate = R"(save {}
demo {}
playspeed 1

{}
vars

frames

// sXXljdbcgu|flrbud|jdu12rws|yaw|pitch|tick|
----------|------|--------|-|-|60|
----------|f-----|--------|-|-|20|
----------|------|--------|-|-|1| cl_mouseenable 1; stop

// SPT version {}
)";

	std::string demoName = "tas_" + std::filesystem::path(filePath).filename().stem().string();
	std::replace(demoName.begin(), demoName.end(), ' ', '_');

	std::string script = std::format(scriptTemplate, saveName, demoName, settingsStream.str(), SPT_VERSION);

	ser::FileWriter fWr(filePath, std::ios::out);
	fWr.WriteSpan(std::span{script.c_str(), script.size()});
	fWr.Finish();

	if (!fWr.Ok())
	{
		errMsg = fWr.GetStatus().errMsg;
		return false;
	}

	return true;
}
