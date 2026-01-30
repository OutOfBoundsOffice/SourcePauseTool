#include "stdafx.hpp"

#include "mon_feature.hpp"

/*
* TODO future features:
* - sliders for tooltip size/zoom
* - toggle for showing where in the portal the player will fit
* - make a version of the newlocation cmd which doesn't require the player to manually name the portals
* - compare the monocle portal values to the game to auto-detect which portal is "placed last"
*/

#ifdef SPT_MONOCLE_FEATURE

#include "spt\utils\game_detection.hpp"
#include "spt\utils\signals.hpp"
#include "spt\utils\interfaces.hpp"
#include "spt\features\ent_props.hpp"
#include "spt\features\playerio.hpp"
#include "spt\features\tracing.hpp"
#include "spt\features\visualizations\imgui\imgui_interface.hpp"

// dumb botched class for a setting which has custom options and an autodetect
template<typename T, size_t N>
class ComboWithAutoOpt
{
	static_assert(N > 0);

	std::string autoOptLabel;
	std::array<std::pair<T, const char*>, N> userOpts;
	int imguiOpt = 0; // 0 is auto
	std::optional<T> lastUpdate;
	T lastGrabbedVal;

	const char* label;

	static const char* GetLabel(void* userData, int idx)
	{
		ComboWithAutoOpt* thisptr = (ComboWithAutoOpt*)userData;
		if (idx > 0)
			return thisptr->userOpts[idx - 1].second;

		if (thisptr->autoOptLabel.empty())
		{
			// populate auto label
			if (!thisptr->lastUpdate.has_value())
				return "auto";
			T cur = thisptr->lastUpdate.value();
			auto it = std::find_if(thisptr->userOpts.begin(),
			                       thisptr->userOpts.end(),
			                       [cur](auto& p) { return p.first == cur; });
			Assert(it != thisptr->userOpts.end());
			thisptr->autoOptLabel = std::string("auto (") + it->second + ")";
		}
		return thisptr->autoOptLabel.c_str();
	}

	T GetSelectedVal() const
	{
		return imguiOpt == 0 ? GetLastUpdateVal() : userOpts[imguiOpt - 1].first;
	}

public:
	ComboWithAutoOpt(const char* label, decltype(userOpts) userOpts)
	    : label(label), userOpts(userOpts), lastGrabbedVal(userOpts[0].first)
	{
	}

	void DrawCombo()
	{
		(void)ImGui::Combo(label, &imguiOpt, &ComboWithAutoOpt::GetLabel, this, N + 1);
	}

	void UpdateAutoVal(T newVal)
	{
		if (newVal != lastUpdate)
		{
			lastUpdate = newVal;
			autoOptLabel.clear();
		}
	}

	T GetLastUpdateVal() const
	{
		return lastUpdate.value_or(userOpts[0].first);
	}

	// get selected value and store for comparing later
	T Grab()
	{
		T newVal = GetSelectedVal();
		if (lastGrabbedVal != newVal)
		{
			lastGrabbedVal = newVal;
			autoOptLabel.clear();
		}
		return newVal;
	}

	bool ChangedSinceLastGrab() const
	{
		return GetSelectedVal() != lastGrabbedVal;
	}

	bool LastGrabbedMatchesLastUpdate() const
	{
		return lastUpdate == lastGrabbedVal;
	}
};

struct ImGuiPersist
{
	struct
	{
		ComboWithAutoOpt<bool, 2> mapOriginEmpty{
		    "map origin empty?",
		    {{{false, "no"}, {true, "yes"}}},
		};

		ComboWithAutoOpt<mon::GameVersion, 2> monocleGameVersion{
		    "game version",
		    {{{mon::GV_5135, "5135"}, {mon::GV_9862575, "9862575 (steampipe)"}}},
		};

		ComboWithAutoOpt<bool, 2> playerCrouched{
		    "player crouched?",
		    {{{true, "yes"}, {false, "no"}}},
		};

	} combos;

	SptImGui::PortalSelectionPersist portalSelect{
	    .enableHighPrecision = true,
	    .showHighPrecisionOpt = false,
	};
	int imguiPlacementOrder = 0;
	bool usePlayer = true;
	float nonPlayerRadius = 5.f;
	int maxTeleports = 30;
	int imgWidth = -1;
	int minImgWidth = 20;
	std::string portalSelectStr = "overlay";

} imguiPersist;

void MonocleFeature::UserPixelSelect::Update(const MonocleWorker& wd, WorkerPixel pxl, bool bluePlacedLast_)
{
	dirty = false;
	bluePlacedLast = bluePlacedLast_;
	selectedPxl = pxl;
	imgSize = wd.GetImageSize();
	params = wd.GetMonocleData()->paramsTemplate;
	params.record_flags = mon::TCRF_RECORD_ENTITY;
	params.n_max_teleports = MAX_EDICTS; // I think anything above this is guaranteed to crash
	wd.DoWorkForPixel(pxl, params, result);
}

void MonocleFeature::UserPixelSelect::DrawInfo()
{
	ImGui::Text("pixel: (%hu,%hu) / (%hu,%hu)", selectedPxl.x, selectedPxl.y, imgSize.x, imgSize.y);
	Assert((params.record_flags & mon::TCRF_RECORD_ENTITY) && !result.ents.empty());
	mon::Vector startCenter = result.ents[0].GetCenter();
	ImGui::Text("entity center: " V_FMT, V_UNP(startCenter));

	if (result.max_tps_exceeded)
	{
		ImGui::Text("result: unknown (possible crash)", result.total_n_teleports);
		ImGui::Text("total teleports: unknown (more than %u)", params.n_max_teleports);
		ImGui::Text("end behind portal: N/A");
	}
	else
	{
		ImGui::Text("result: %d CUM teleport(s)", result.cum_teleports);
		ImGui::Text("total teleports: %u", result.total_n_teleports);
		std::optional<bool> endBehind = MonocleWorker::EndBehindPortal(params, result);
		if (endBehind.has_value())
			ImGui::Text("end behind portal: %s", *endBehind ? "true" : "false");
		else
			ImGui::Text("end behind portal: N/A");
	}
}

// callbacks for imgui to disable/re-enable image interpolation for the zoomed in tooltip
struct SamplerState
{
	DWORD oldMin, oldMag;
	DWORD oldU, oldV;
	DWORD oldCol;

	static void SaveAndSetPixelPerfect(const ImDrawList*, const ImDrawCmd* cmd)
	{
		auto ss = (SamplerState*)cmd->UserCallbackData;
		IDirect3DDevice9* device = SptImGui::GetDx9Device();

		device->GetSamplerState(0, D3DSAMP_MINFILTER, &ss->oldMin);
		device->GetSamplerState(0, D3DSAMP_MAGFILTER, &ss->oldMag);
		device->GetSamplerState(0, D3DSAMP_ADDRESSU, &ss->oldU);
		device->GetSamplerState(0, D3DSAMP_ADDRESSV, &ss->oldV);
		device->GetSamplerState(0, D3DSAMP_BORDERCOLOR, &ss->oldCol);

		// nearest neighbor, extend transparently
		device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		device->SetSamplerState(0, D3DSAMP_BORDERCOLOR, D3DCOLOR_ARGB(0, 0, 0, 0));
		device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_BORDER);
		device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_BORDER);
	}

	static void Pop(const ImDrawList*, const ImDrawCmd* cmd)
	{
		auto ss = (SamplerState*)cmd->UserCallbackData;
		IDirect3DDevice9* device = SptImGui::GetDx9Device();
		device->SetSamplerState(0, D3DSAMP_MINFILTER, ss->oldMin);
		device->SetSamplerState(0, D3DSAMP_MAGFILTER, ss->oldMag);
		device->SetSamplerState(0, D3DSAMP_ADDRESSU, ss->oldU);
		device->SetSamplerState(0, D3DSAMP_ADDRESSV, ss->oldV);
		device->SetSamplerState(0, D3DSAMP_BORDERCOLOR, ss->oldCol);
	}
};

void MonocleFeature::DrawWorkerImage(const char* label, MonocleWorker& wd)
{
	ImGui::PushID(label);
	ImGui::BeginGroup();

	auto& md = wd.GetMonocleData();
	WorkerPixel imgSize = wd.GetImageSize();
	Vector2D imgSizeVec = Vector2D(imgSize.x, imgSize.y);

	ImGui::TextUnformatted(label);

	ImGui::BeginDisabled(!md);
	if (ImGui::Button("Copy newlocation cmd"))
		ImGui::SetClipboardText(wd.GetMonocleData()->pp.NewLocationCmd("; ").c_str());
	ImGui::SameLine();
	SptImGui::HelpMarker(
	    "To use this command, you must manually name your portals 'blue' and 'orange'\n"
	    "using e.g. 'picker', looking at the portal bubble, and using 'ent_setname blue'.");
	imguiPersist.minImgWidth = (int)ImGui::GetItemRectSize().x;
	ImGui::EndDisabled();

	ImGui::PushStyleVar(ImGuiStyleVar_ImageBorderSize, 0.f);

	// main image padding in pixels
	constexpr int imgPad = 2;
	Vector2D imgPad2D(imgPad, imgPad);

	// main canvas (screen space)
	Vector2D canvasMin = ImGui::GetCursorScreenPos();
	Vector2D canvasMax = canvasMin + imgSizeVec + imgPad2D * 2.f;

	// keep a reference to the texture in case the worker gets deleted as imgui is still drawing
	static ComPtr<IDirect3DTexture9> tex;
	tex = wd.UpdateAndGetTexture();

	ImU32 borderColor = Color32ToImU32((color32)MonocleImageColors[MIC_BORDER]);

	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->AddImage((ImTextureID)tex.Get(), canvasMin + imgPad2D, canvasMax - imgPad2D);
	// padding rect
	dl->AddRect(canvasMin + imgPad2D * .5f, canvasMax - imgPad2D * .5f, borderColor, 0.f, 0, imgPad);

	ImGui::InvisibleButton(label, canvasMax - canvasMin);
	bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
	bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone);
	bool hasTooltip = false;
	Vector2D tooltipPx(tooltip.pxlWidth, tooltip.pxlWidth);

	// image coords
	Vector2D pxlSelect = Vector2D(ImGui::GetIO().MousePos) - canvasMin - imgPad2D;
	WorkerPixel pxlSelectCoord((uint16_t)pxlSelect.x, (uint16_t)pxlSelect.y);

	if (pxlSelect.x < 0 || pxlSelect.y < 0 || pxlSelect.x >= imgSizeVec.x || pxlSelect.y >= imgSizeVec.y)
		hovered = false; // out of bounds

	if (hovered && !!md && ImGui::BeginTooltip())
	{
		hasTooltip = true;

		// maked zoomed image be on the center of a pixel
		Vector2D pxlShift(.5f, .5f);
		// selected pixel of main image that we're zooming to (pixel space)
		Vector2D imgSelectMins = pxlSelect + pxlShift - tooltipPx * .5f;
		Vector2D imgSelectMaxs = pxlSelect + pxlShift + tooltipPx * .5f;
		// mins/maxs of selected pixel (uv space)
		ImVec2 uvMin = imgSelectMins / imgSizeVec;
		ImVec2 uvMax = imgSelectMaxs / imgSizeVec;

		ImDrawList* dltt = ImGui::GetWindowDrawList();
		ImGui::InvisibleButton("zoom_img", tooltipPx * tooltip.zoom);
		// tooltip canvas (screen space)
		Vector2D c0 = ImGui::GetItemRectMin();
		Vector2D c1 = ImGui::GetItemRectMax();

		static SamplerState ss;
		dltt->AddCallback(SamplerState::SaveAndSetPixelPerfect, &ss);
		dltt->AddImage((ImTextureID)tex.Get(), c0, c1, uvMin, uvMax);
		dltt->AddCallback(SamplerState::Pop, &ss);

		// manually add the border of the main image to the zoomed tooltip
		dltt->PushClipRect(c0, c1);
		dltt->AddRect(c0 + (Vector2D(0, 0) - imgSelectMins - imgPad2D * .5f) * tooltip.zoom,
		              c0 + (imgSizeVec - imgSelectMins + imgPad2D * .5f) * tooltip.zoom,
		              borderColor,
		              0.f,
		              ImDrawFlags_None,
		              tooltip.zoom * imgPad);
		dltt->PopClipRect();

		// selection rectangle on hovered pixel
		float pxlSelectThick = tooltip.zoom * 0.3f;
		Vector2D pxlSelectPad = Vector2D(pxlSelectThick, pxlSelectThick) * 0.5f;
		dltt->AddRect(c0 + (tooltipPx - Vector2D(1, 1)) * .5f * tooltip.zoom - pxlSelectPad,
		              c0 + (tooltipPx + Vector2D(1, 1)) * .5f * tooltip.zoom + pxlSelectPad,
		              borderColor,
		              0.f,
		              ImDrawFlags_None,
		              pxlSelectThick);
		// border
		dltt->AddRect(c0, c1, borderColor);

		bool curBluePlacedLast =
		    wd.GetMonocleData()->pp.order == mon::PlacementOrder::ORANGE_OPEN_BLUE_NEW_LOCATION;
		if (hoveredPxl.dirty || hoveredPxl.selectedPxl != pxlSelectCoord
		    || hoveredPxl.bluePlacedLast != curBluePlacedLast)
		{
			hoveredPxl.Update(wd, pxlSelectCoord, curBluePlacedLast);
		}

		ImGui::SameLine();
		ImGui::BeginGroup();
		hoveredPxl.DrawInfo();
		ImGui::EndGroup();

		ImGui::Text("Click to copy setpos cmd");

		// warning message
		static std::string setposWarningMsg;
		setposWarningMsg.clear();
		if (!hoveredPxl.result.ent.is_player)
			setposWarningMsg += "- the simulation was run on a non-player entity\n";
		auto& combos = imguiPersist.combos;
		if (!combos.playerCrouched.LastGrabbedMatchesLastUpdate())
		{
			setposWarningMsg += combos.playerCrouched.GetLastUpdateVal() ? "- the player is crouched\n"
			                                                             : "- the player is not crouched\n";
		}
		if (!combos.mapOriginEmpty.LastGrabbedMatchesLastUpdate())
		{
			setposWarningMsg += combos.mapOriginEmpty.GetLastUpdateVal()
			                        ? "- the map origin is a passable space\n"
			                        : "- the map origin is not a passable space\n";
		}
		else if (combos.mapOriginEmpty.GetLastUpdateVal())
		{
			setposWarningMsg += "- the portal might not teleport you (try going into its portal bubble)\n";
		}
		if (!combos.monocleGameVersion.LastGrabbedMatchesLastUpdate())
			setposWarningMsg += "- the simulation was done with a different game version\n";
		if (!setposWarningMsg.empty())
		{
			ImGui::TextColored(SPT_IMGUI_WARN_COLOR_YELLOW,
			                   ICON_CI_WARNING " the setpos command may not work as expected because:");
			ImGui::Text("%.*s", setposWarningMsg.size() - 1, setposWarningMsg.c_str());
		}

		if (clicked)
		{
			mon::Entity entInFront = hoveredPxl.result.ents[0].WithNewCenter(
			    hoveredPxl.result.ents[0].GetCenter() + hoveredPxl.params.EntryPortal().f);
			std::string cmd = std::format("{}; spt_afterticks 5 \"{}\"",
			                              entInFront.SetPosCmd(),
			                              hoveredPxl.result.ents[0].SetPosCmd());
			ImGui::SetClipboardText(cmd.c_str());
		}

		ImGui::EndTooltip();
	}

	if (hasTooltip)
	{
		dl->PushClipRect(canvasMin, canvasMax);
		dl->AddRect(canvasMin + pxlSelect - tooltipPx, canvasMin + pxlSelect + tooltipPx, borderColor);
		dl->PopClipRect();
	}

	ImGui::PopStyleVar();

	ImGui::EndGroup();
	ImGui::PopID();
}

void MonocleFeature::ImGuiDrawImages()
{
	if (ImGui::TreeNode("Color options"))
	{
		ImGuiColorEditSettings();
		ImGui::TreePop();
	}

	if (!worker1 || !worker1->GetMonocleData())
		ImGui::TextColored(SPT_IMGUI_WARN_COLOR_YELLOW, "Select a portal");

	SptImGui::HelpMarker(
	    "For each pixel in the images below, SPT chooses a specific point really close behind\n"
	    "the entry portal and shows what would happen if you actually teleported there. Sometimes,\n"
	    "this may cause a 'teleport chain'. If you sum all of the teleports in a chain as +1 for\n"
	    "the entry portal and -1 for the exit portal, this cumulative teleport count (CUM teleports)\n"
	    "represents the final outcome. Common numbers are +1 for a normal teleport and -1 for a VAG.");

	ImGui::BeginGroup();

	DrawWorkerImage("Blue placed last", *worker1);

	ImGui::SameLine();
	ImGui::Dummy(ImVec2(10, 0));
	ImGui::SameLine();

	DrawWorkerImage("Orange placed last", *worker2);

	ImGui::EndGroup();
}

void MonocleFeature::ImGuiColorEditSettings()
{
	auto colorTypeEdit = [](const char* label, MonocleImageColorType type)
	{
		ImGuiColorEditFlags flags =
		    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_AlphaBar;
		color32 c = (color32)MonocleImageColors[type];
		if (SptImGui::ColorEdit4(label, c, flags))
		{
			MonocleImageColors[type] = d3d9_argb::FromColor32(c);
			return true;
		}
		return false;
	};

	bool dirty = false;

	// not part of the image so doesn't need to mark dirty
	colorTypeEdit("Border", MIC_BORDER);
	ImGui::SameLine();
	dirty |= colorTypeEdit("Max teleports exceeded", MIC_TP_EXCEEDED);

	dirty |= colorTypeEdit("0 CUM (end in front)", MIC_0CUM_FRONT);
	ImGui::SameLine();
	dirty |= colorTypeEdit("(behind)##0tp", MIC_0CUM_BEHIND);

	dirty |= colorTypeEdit("1 CUM (end in front)", MIC_1CUM_FRONT);
	ImGui::SameLine();
	dirty |= colorTypeEdit("(behind)##1tp", MIC_1CUM_BEHIND);

	dirty |= colorTypeEdit("-1 CUM (VAG)", MIC_NEG1CUM);

	dirty |= colorTypeEdit("-2 CUM", MIC_NEG2CUM);
	ImGui::SameLine();
	dirty |= colorTypeEdit("2 CUM", MIC_2CUM);

	dirty |= colorTypeEdit("-3 CUM", MIC_NEG3CUM);
	ImGui::SameLine();
	dirty |= colorTypeEdit("3 CUM", MIC_3CUM);

	dirty |= colorTypeEdit("Misc. result", MIC_MISC);

	if (ImGui::Button("Reset colors to default"))
	{
		MonocleImageColors = DefaultMonocleImageColors;
		dirty = true;
	}

	if (dirty)
	{
		if (worker1)
			worker1->MarkTextureDirty();
		if (worker2)
			worker2->MarkTextureDirty();
	}
}

void MonocleFeature::ImGuiTabCallback()
{
	ImGui::Text("Select angle glitch entry portal");
	auto newPortalType =
	    SptImGui::PortalSelectionTypeCombo(imguiPersist.portalSelectStr.c_str(), SPT_PORTAL_SELECT_FLAGS);
	if (newPortalType[0])
		imguiPersist.portalSelectStr.assign(newPortalType.data());
	const utils::PortalInfo* newEntryPortal =
	    SptImGui::PortalSelectionWidgetFromString(imguiPersist.portalSelectStr.c_str(),
	                                              imguiPersist.portalSelect,
	                                              SPT_PORTAL_SELECT_FLAGS);
	if (imguiPersist.portalSelect.userSelectedPortalByIndexLastCall && !!newEntryPortal)
		imguiPersist.portalSelectStr = std::to_string(newEntryPortal->handle.GetEntryIndex());

	bool restart = ImGuiDrawSettings();
	if (!restart)
		restart = CheckPortalsChanged(newEntryPortal);

	if (restart)
		RestartWorkers(newEntryPortal);

	ImGuiDrawImages();
}

bool MonocleFeature::ImGuiDrawSettings()
{
	bool settingsChanged = false;

	// TODO prevent restart when scrollbar appears
	int newImgWidth = std::max(imguiPersist.minImgWidth, (int)(ImGui::GetContentRegionAvail().x * .45f));
	if (std::min(imguiPersist.imgWidth, newImgWidth) < 0 || std::abs(imguiPersist.imgWidth - newImgWidth) > 25)
	{
		imguiPersist.imgWidth = newImgWidth;
		settingsChanged = true;
	}

	imguiPersist.combos.monocleGameVersion.DrawCombo();
	ImGui::SameLine();
	SptImGui::HelpMarker(
	    "VAGs behave differently on different game versions.\n"
	    "This doesn't exactly need to match the actual version -\n"
	    "e.g. 3420/5135 should have the same behavior.");
	settingsChanged |= imguiPersist.combos.monocleGameVersion.ChangedSinceLastGrab();

	if (ImGui::Checkbox("simulate player teleports", &imguiPersist.usePlayer))
		settingsChanged = true;
	ImGui::SameLine();
	SptImGui::HelpMarker(
	    "If checked, treat the teleported entity as a player.\n"
	    "Otherwise, treat as a non-player ball entity with a radius of %g.",
	    imguiPersist.nonPlayerRadius);

	imguiPersist.combos.mapOriginEmpty.DrawCombo();
	ImGui::SameLine();
	SptImGui::HelpMarker(
	    "If the player were teleported to the origin, would they be in a passable space?\n"
	    "e.g. true in 02/04/08, false in 09/13/14; 'auto' checks the current loaded map.");
	settingsChanged |= imguiPersist.combos.mapOriginEmpty.ChangedSinceLastGrab();

	ImGui::BeginDisabled(!imguiPersist.usePlayer);
	imguiPersist.combos.playerCrouched.DrawCombo();
	ImGui::SameLine();
	SptImGui::HelpMarker(
	    "Some VAGs work differently if the player is standing/crouched, especially\n"
	    "if the portals are angled. For example, if the entry portal is a floor/ceiling\n"
	    "and the exit is a wall, a VAG will only work if you're crouched.");
	ImGui::EndDisabled();
	if (imguiPersist.usePlayer)
		settingsChanged |= imguiPersist.combos.playerCrouched.ChangedSinceLastGrab();

	if (ImGui::SliderInt("max teleports", &imguiPersist.maxTeleports, 0, 100))
		settingsChanged = true;
	ImGui::SameLine();
	SptImGui::HelpMarker(
	    "This is the maximum number of teleports SPT will allow at each point.\n"
	    "Some teleport chains finish after a few hundred teleports, most finish\n"
	    "in less than 10. Setting this higher may make the image creation slower.");

	return settingsChanged;
}

bool MonocleFeature::CheckPortalsChanged(const utils::PortalInfo* newEntryPortal)
{
	const utils::PortalInfo* newLinkedPortal = newEntryPortal ? newEntryPortal->GetLinkedPortal() : nullptr;
	bool newPortalsExist =
	    !!newEntryPortal && newEntryPortal->isOpen && !!newLinkedPortal && newLinkedPortal->isOpen;

	std::shared_ptr<const WorkerMonocleData> oldMonData = worker1 ? worker1->GetMonocleData() : nullptr;

	if (!newPortalsExist)
		return !!oldMonData; // no portals
	if (!oldMonData)
		return true; // portals didn't exist before now

	if (oldMonData->paramsTemplate.first_tp_from_blue == newEntryPortal->isOrange)
		return true; // color changed

	// check pos/ang change

	auto& oldEntry = oldMonData->paramsTemplate.EntryPortal();
	auto& oldExit = oldMonData->paramsTemplate.ExitPortal();
	auto vec_match = [](auto& a, auto& b) { return *(Vector*)&a == *(Vector*)&b; };

	return !vec_match(oldEntry.pos, newEntryPortal->pos) || !vec_match(oldExit.pos, newLinkedPortal->pos)
	       || !vec_match(oldEntry.ang, newEntryPortal->ang) || !vec_match(oldExit.ang, newLinkedPortal->ang);
}

void MonocleFeature::RestartWorkers(const utils::PortalInfo* newEntryPortal)
{
	const utils::PortalInfo* newLinkedPortal = newEntryPortal ? newEntryPortal->GetLinkedPortal() : nullptr;
	bool hasOpenPortals =
	    !!newEntryPortal && newEntryPortal->isOpen && !!newLinkedPortal && newLinkedPortal->isOpen;

	uint16_t w = (uint16_t)imguiPersist.imgWidth;
	uint16_t h = w * mon::PORTAL_HALF_HEIGHT / mon::PORTAL_HALF_WIDTH;

	std::shared_ptr<WorkerMonocleData> newMonData1, newMonData2;

	if (hasOpenPortals)
	{
		// create new data

		mon::TeleportChainParams paramsTemplate;

		mon::Vector entCenter = *(mon::Vector*)&newEntryPortal->pos;
		paramsTemplate.ent =
		    imguiPersist.usePlayer
		        ? mon::Entity::CreatePlayerFromCenter(entCenter, imguiPersist.combos.playerCrouched.Grab())
		        : mon::Entity::CreateBall(entCenter, imguiPersist.nonPlayerRadius);
		paramsTemplate.first_tp_from_blue = !newEntryPortal->isOrange;
		paramsTemplate.record_flags = mon::TCRF_NONE;
		paramsTemplate.n_max_teleports = imguiPersist.maxTeleports;
		paramsTemplate.map_origin_empty = imguiPersist.combos.mapOriginEmpty.Grab();

		auto blue = newEntryPortal->isOrange ? newLinkedPortal : newEntryPortal;
		auto orange = newEntryPortal->isOrange ? newEntryPortal : newLinkedPortal;

		newMonData1 = std::make_shared<WorkerMonocleData>(blue->pos,
		                                                  blue->ang,
		                                                  orange->pos,
		                                                  orange->ang,
		                                                  mon::PlacementOrder::ORANGE_OPEN_BLUE_NEW_LOCATION,
		                                                  imguiPersist.combos.monocleGameVersion.Grab(),
		                                                  paramsTemplate);

		newMonData2 = std::make_shared<WorkerMonocleData>(blue->pos,
		                                                  blue->ang,
		                                                  orange->pos,
		                                                  orange->ang,
		                                                  mon::PlacementOrder::BLUE_OPEN_ORANGE_NEW_LOCATION,
		                                                  imguiPersist.combos.monocleGameVersion.Grab(),
		                                                  paramsTemplate);
	}

	if (!worker1)
		worker1 = std::make_shared<MonocleWorker>();
	if (!worker2)
		worker2 = std::make_shared<MonocleWorker>();

	worker1->RestartWork(std::move(newMonData1), {w, h});
	worker2->RestartWork(std::move(newMonData2), {w, h});

	hoveredPxl.dirty = true;
}

void MonocleFeature::OnTickSignal(bool)
{
	// map origin empty?
	if (interfaces::engineTraceServer)
	{
		static utils::CachedField<Vector, "CBaseEntity", "m_Collision.m_vecMins", true> fMins;
		static utils::CachedField<Vector, "CBaseEntity", "m_Collision.m_vecMaxs", true> fMaxs;
		// UTIL_IsSpaceEmpty for the player
		Vector* mins = fMins.GetPtrPlayer();
		Vector* maxs = fMaxs.GetPtrPlayer();
		if (mins && maxs)
		{
			Vector half = (*maxs - *mins) * 0.5f;
			Vector center = *mins + half;
			Ray_t ray;
			trace_t trace;
			ray.Init(center, center, -half, half);
			TraceFilterIgnorePlayer filter{};
			interfaces::engineTraceServer->TraceRay(ray, MASK_SOLID, &filter, &trace);
			bool mapOriginEmpty = trace.fraction == 1 && !trace.allsolid && !trace.startsolid;
			imguiPersist.combos.mapOriginEmpty.UpdateAutoVal(mapOriginEmpty);
		}
	}

	// player crouched?

	imguiPersist.combos.playerCrouched.UpdateAutoVal(spt_playerio.m_fFlags.GetValue() & FL_DUCKING);
}

bool MonocleFeature::ShouldLoadFeature()
{
	return utils::DoesGameLookLikePortal();
}

void MonocleFeature::LoadFeature()
{
	imguiPersist.combos.monocleGameVersion.UpdateAutoVal(utils::GetBuildNumber() <= 5135 ? mon::GV_5135
	                                                                                     : mon::GV_9862575);
	SptImGuiGroup::Draw_Monocle.RegisterUserCallback([this]() { ImGuiTabCallback(); });
	TickSignal.Connect(this, &MonocleFeature::OnTickSignal);
}

void MonocleFeature::UnloadFeature()
{
	worker1.reset();
	worker2.reset();
}

#endif
