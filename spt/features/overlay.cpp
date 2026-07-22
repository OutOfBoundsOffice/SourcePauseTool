#include "stdafx.hpp"

#include "overlay.hpp"

#ifdef SPT_OVERLAY_ENABLED

#include <cinttypes> // SCNu8

#include "vguimatsurface\imatsystemsurface.h"
#include "convar.hpp"

#include "interfaces.hpp"

#include "spt\utils\portal_utils.hpp"
#include "spt\utils\signals.hpp"
#include "spt\utils\math.hpp"
#include "spt\utils\game_detection.hpp"
#include "spt\utils\vcall.hpp"
#include "visualizations\imgui\imgui_interface.hpp"

#include "playerio.hpp"
#include "hud.hpp"
#include "create_collide.hpp"

ConVar _y_spt_overlay("_y_spt_overlay",
                      "0",
                      FCVAR_NONE,
                      "Enables the overlay camera in the top left of the screen.\n");
ConVar _y_spt_overlay_type("_y_spt_overlay_type",
                           "0",
                           FCVAR_NONE,
                           "Overlay type:\n"
                           "  0 = save glitch body\n"
                           "  1 = angle glitch simulation\n"
                           "  2 = rear view mirror\n"
                           "  3 = havok view mirror\n"
                           "  4 = no camera transform (even when behind SG portal)\n",
                           true,
                           0,
                           true,
                           4);

#ifdef SPT_PORTAL_UTILS

constexpr int SPT_PORTAL_SELECT_FLAGS = GPF_ALLOW_AUTO | GPF_ALLOW_PLAYER_ENV;

ConVar _y_spt_overlay_portal(
    "_y_spt_overlay_portal",
    "env",
    FCVAR_NONE,
    "Chooses the portal for the overlay camera. For the SG camera this is the portal you save glitch on, for angle glitch simulation this is the portal you enter. Valid values are:\n"
    "" SPT_PORTAL_SELECT_DESCRIPTION_ENV_PREFIX "" SPT_PORTAL_SELECT_DESCRIPTION_AUTO_PREFIX
    "" SPT_PORTAL_SELECT_DESCRIPTION);
#endif

ConVar _y_spt_overlay_width("_y_spt_overlay_width",
                            "480",
                            FCVAR_NONE,
                            "Determines the width of the overlay. Height is determined automatically from width.\n",
                            true,
                            20.0f,
                            true,
                            3840.0f);
ConVar _y_spt_overlay_fov("_y_spt_overlay_fov",
                          "90",
                          FCVAR_NONE,
                          "Determines the FOV of the overlay.\n",
                          true,
                          1.f,
                          true,
                          175.f);
ConVar _y_spt_overlay_swap("_y_spt_overlay_swap", "0", FCVAR_NONE, "Swap alternate view and main screen?\n");

#if defined(SPT_HUD_ENABLED) && !defined(SPT_HUD_TEXTONLY)
ConVar _y_spt_overlay_crosshair_size("_y_spt_overlay_crosshair_size",
                                     "10",
                                     FCVAR_NONE,
                                     "Overlay crosshair size.",
                                     true,
                                     1,
                                     true,
                                     200);
ConVar _y_spt_overlay_crosshair_thickness("_y_spt_overlay_crosshair_thickness",
                                          "1",
                                          FCVAR_NONE,
                                          "Overlay crosshair thickness.",
                                          true,
                                          1,
                                          true,
                                          15);
ConVar _y_spt_overlay_crosshair_color("_y_spt_overlay_crosshair_color",
                                      "0 255 0 255",
                                      FCVAR_NONE,
                                      "Overlay crosshair RGBA color.");
#endif

ConVar _y_spt_overlay_no_roll("_y_spt_overlay_no_roll", "1", FCVAR_NONE, "Set the roll of overlay camera roll to 0.");

Overlay spt_overlay;

namespace patterns
{
	PATTERNS(CViewRender__RenderView,
	         "3420",
	         "55 8B EC 83 E4 F8 81 EC 24 01 00 00",
	         "1910503",
	         "55 8B EC 81 EC 10 02 00 00 53 56 8B F1",
	         "7462488",
	         "55 8B EC 81 EC FC 01 00 00 53 56 57",
	         "BMS-Retail-Xen",
	         "55 8B EC 81 EC DC 02 00 00 A1 ?? ?? ?? ?? 33 C5 89 45 FC 53 8B 5D 08 56 8B F1",
	         "ep3-mod",
	         "55 8B EC 81 EC D8 01 00 00 A1 ?? ?? ?? ?? 33 C5");
} // namespace patterns

#ifdef SPT_PORTAL_UTILS
const utils::PortalInfo* Overlay::GetOverlayPortal()
{
	return getPortal(_y_spt_overlay_portal.GetString(), SPT_PORTAL_SELECT_FLAGS);
}
#endif

void Overlay::InitHooks()
{
	HOOK_FUNCTION(client, CViewRender__RenderView);
}

void Overlay::PreHook()
{
	if (!ORIG_CViewRender__RenderView)
		return;

#if defined(BMS) || defined(SSDK2013)
	QueueOverlayRenderView_Offset = 26;
#else
	if (utils::GetBuildNumber() >= 5135)
		QueueOverlayRenderView_Offset = 26;
	else
		QueueOverlayRenderView_Offset = 24;
#endif

	RenderViewPre_Signal.Works = true; // we use this as a "loading successful" flag
}

void Overlay::LoadFeature()
{
	if (!RenderViewPre_Signal.Works)
		return;

	InitConcommandBase(_y_spt_overlay);
	InitConcommandBase(_y_spt_overlay_type);
#ifdef SPT_PORTAL_UTILS
	InitConcommandBase(_y_spt_overlay_portal);
#endif
	InitConcommandBase(_y_spt_overlay_width);
	InitConcommandBase(_y_spt_overlay_fov);
	InitConcommandBase(_y_spt_overlay_swap);
	InitConcommandBase(_y_spt_overlay_no_roll);

#if defined(SPT_HUD_ENABLED) && !defined(SPT_HUD_TEXTONLY)
	bool result = spt_hud_feat.AddHudDefaultGroup(
	    HudCallback(std::bind(&Overlay::DrawCrosshair, this), []() { return true; }, true));

	if (result)
	{
		InitConcommandBase(_y_spt_overlay_crosshair_size);
		InitConcommandBase(_y_spt_overlay_crosshair_thickness);
		InitConcommandBase(_y_spt_overlay_crosshair_color);
	}
#endif

	SptImGuiGroup::Overlay.RegisterUserCallback(ImGuiCallback);
}

IMPL_HOOK_THISCALL(Overlay,
                   void,
                   CViewRender__RenderView,
                   void*,
                   CViewSetup* cameraView,
                   int nClearFlags,
                   int whatToDraw)
{
	/*
	* If the overlay is enabled, we'll trigger a recursive call to RenderView() via QueueOverlayRenderView(). You
	* could in theory use this to have several overlays, in which case you'd need to keep track of the overlay
	* depth to make everyone happy.
	*/

	static int callDepth = 0;
	callDepth++;

	// this is hardcoded to false for overlays, so we copy its value to be the same as for the main view
	static bool doBloomAndToneMapping;

	auto& ovr = spt_overlay;

	if (callDepth == 1)
	{
		doBloomAndToneMapping = cameraView->m_bDoBloomAndToneMapping;
	}
	else
	{
		ovr.renderingOverlay = true;
		cameraView->m_bDoBloomAndToneMapping = doBloomAndToneMapping;
	}
	RenderViewPre_Signal(thisptr, cameraView);

	if (_y_spt_overlay.GetBool())
	{
		if (!ovr.renderingOverlay)
		{
			// Queue a RenderView() call, this will make a copy of the params that we got called with. This
			// function is in the SDK but has different virtual offsets for 3420/5135 :/.
			ovr.CViewRender__QueueOverlayRenderView(thisptr, *cameraView, nClearFlags, whatToDraw);
		}
		ovr.ModifyView(cameraView);
		ovr.ModifyScreenFlags(nClearFlags, whatToDraw);
	}

	// it just so happens that we don't need to make a full copy of the view and can use pointers instead
	if (ovr.renderingOverlay)
		ovr.overlayView = cameraView;
	else
		ovr.mainView = cameraView;

	ovr.ORIG_CViewRender__RenderView(thisptr, cameraView, nClearFlags, whatToDraw);
	callDepth--;
	if (callDepth == 1)
		ovr.renderingOverlay = false;
}

void Overlay::CViewRender__QueueOverlayRenderView(void* thisptr,
                                                  const CViewSetup& renderView,
                                                  int nClearFlags,
                                                  int whatToDraw)
{
	Assert(QueueOverlayRenderView_Offset != -1);
	utils::vcall<void>(QueueOverlayRenderView_Offset, thisptr, &renderView, nClearFlags, whatToDraw);
}

#if defined(SPT_HUD_ENABLED) && !defined(SPT_HUD_TEXTONLY)
void Overlay::DrawCrosshair()
{
	static std::string color = "";
	static int r = 0, g = 0, b = 0, a = 0;

	if (strcmp(color.c_str(), _y_spt_overlay_crosshair_color.GetString()) != 0)
	{
		color = _y_spt_overlay_crosshair_color.GetString();
		sscanf(color.c_str(), "%d %d %d %d", &r, &g, &b, &a);
	}

	interfaces::surface->DrawSetColor(Color(r, g, b, a));
	int x = overlayView->x + overlayView->width / 2;
	int y = overlayView->y + overlayView->height / 2;
	int width = _y_spt_overlay_crosshair_size.GetInt();
	int thickness = _y_spt_overlay_crosshair_thickness.GetInt();

	if (thickness > width)
		std::swap(thickness, width);

	interfaces::surface->DrawFilledRect(x - thickness / 2, y - width / 2, x + thickness / 2 + 1, y + width / 2 + 1);
	interfaces::surface->DrawFilledRect(x - width / 2, y - thickness / 2, x - thickness / 2, y + thickness / 2 + 1);
	interfaces::surface->DrawFilledRect(x + thickness / 2 + 1,
	                                    y - thickness / 2,
	                                    x + width / 2 + 1,
	                                    y + thickness / 2 + 1);
}
#endif

void Overlay::ModifyView(CViewSetup* renderView)
{
	if (renderingOverlay)
	{
		// scale this view to be smol

		renderView->x = 0;
		renderView->y = 0;
		renderView->fov =
		    utils::ScaleFOVByWidthRatio(_y_spt_overlay_fov.GetFloat(), renderView->m_flAspectRatio * 3.f / 4.f);
		renderView->width = _y_spt_overlay_width.GetFloat();
		renderView->height = static_cast<int>(renderView->width / renderView->m_flAspectRatio);
	}

	if (renderingOverlay != _y_spt_overlay_swap.GetBool())
	{
		// move/rotate this view

		switch (_y_spt_overlay_type.GetInt())
		{
#ifdef SPT_PORTAL_UTILS
		case 0: // saveglitch offset
			if (utils::DoesGameLookLikePortal())
				calculateSGPosition(GetOverlayPortal(), renderView->origin, renderView->angles);
			break;
		case 1: // angle glitch tp pos
			if (utils::DoesGameLookLikePortal())
				calculateAGPosition(GetOverlayPortal(), renderView->origin, renderView->angles);
			break;
#endif
		case 2: // rear view cam
			renderView->angles.y += 180;
			break;
		case 3: // havok pos
		{
			IPhysicsObject* physObj = spt_collideToMesh.GetPhysObj(utils::spt_serverEntList.GetPlayer());
			if (physObj)
			{
				physObj->GetPosition(&renderView->origin, nullptr);
				renderView->origin += spt_playerio.m_vecViewOffset.GetValue();
			}
			break;
		}
		case 4: // don't transform when behind sg portal
			renderView->origin = utils::GetPlayerEyePosition();
			renderView->angles = utils::GetPlayerEyeAngles();
			break;
		default:
			break;
		}
		// normalize yaw
		renderView->angles.y = utils::NormalizeDeg(renderView->angles.y);

		if (_y_spt_overlay_no_roll.GetBool())
		{
			renderView->angles.z = 0;
		}
	}
}

void Overlay::ModifyScreenFlags(int& clearFlags, int& drawFlags)
{
	if (renderingOverlay)
	{
		drawFlags = RENDERVIEW_UNSPECIFIED;
		clearFlags |= VIEW_CLEAR_COLOR;
	}
	else if (_y_spt_overlay_swap.GetBool())
	{
		drawFlags &= ~RENDERVIEW_DRAWVIEWMODEL;
	}
}

void Overlay::ImGuiCallback()
{
	SptImGui::BeginCmdGroup(_y_spt_overlay);
	SptImGui::CvarCheckbox(_y_spt_overlay, "Enable overlay");
	ImGui::BeginDisabled(!_y_spt_overlay.GetBool());

	SptImGui::CvarDraggableInt(_y_spt_overlay_width, "Width", nullptr, true);
	SptImGui::CvarDraggableFloat(_y_spt_overlay_fov, "FOV", 1.f, nullptr, true);
	SptImGui::CvarCheckbox(_y_spt_overlay_swap, "Swap overlay and main View");
	SptImGui::CvarCheckbox(_y_spt_overlay_no_roll, "No roll");

#if defined(SPT_HUD_ENABLED) && !defined(SPT_HUD_TEXTONLY)
	ImGui::SeparatorText("Crosshair");
	SptImGui::CvarDraggableInt(_y_spt_overlay_crosshair_size, "Size", nullptr, true);
	SptImGui::CvarDraggableInt(_y_spt_overlay_crosshair_thickness, "Thickness", nullptr, true);

	// clang-format off
	color32 color;
	if (sscanf_s(_y_spt_overlay_crosshair_color.GetString(),
	             "%" PRIu8 " %" PRIu8 " %" PRIu8 " %" PRIu8,
	             &color.r, &color.g, &color.b, &color.a) != 4)
	{
		color = color32(0, 255, 0, 255);
	}
	ImVec4 colf = ImGui::ColorConvertU32ToFloat4(Color32ToImU32(color));
	if (ImGui::ColorEdit4("Color",
	                      (float*)&colf,
	                      ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_AlphaBar))
	{
		color = ImU32ToColor32(ImGui::ColorConvertFloat4ToU32(colf));
		char buf[32];
		snprintf(buf,
		         sizeof buf,
		         "%" SCNu8 " %" SCNu8 " %" SCNu8 " %" SCNu8,
		         color.r, color.g, color.b, color.a);
		_y_spt_overlay_crosshair_color.SetValue(buf);
	}
	// clang-format on
#endif

	ImGui::SeparatorText("Overlay type");

	std::array<const char*, 5> opts = {"Save glitch", "Angle glitch", "Rear view", "Havok", "No transform"};
	SptImGui::CvarCombo(_y_spt_overlay_type, "Overlay type", opts.data(), opts.size());

#ifdef SPT_PORTAL_UTILS
	if (_y_spt_overlay_type.GetInt() < 2)
	{
		static SptImGui::PortalSelectionPersist persist;
		SptImGui::PortalSelectionWidgetCvar(_y_spt_overlay_portal, persist, SPT_PORTAL_SELECT_FLAGS);
	}
	else
	{
		ImGui::TextDisabled("Portal selection disabled");
	}
#endif

	ImGui::EndDisabled();
	SptImGui::EndCmdGroup();
}

#endif
