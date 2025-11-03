#include "stdafx.hpp"

#include "renderer\mesh_renderer.hpp"
#include "imgui/imgui_interface.hpp"
#include "thirdparty/imgui/imgui_internal.h"

#ifdef SPT_MESH_RENDERING_ENABLED

#include <vector>

class DrawLine : public FeatureWrapper<DrawLine>
{
public:
	struct LineInfo
	{
		Vector start, end;
		color32 color;
	};

	bool should_recompute_meshes = false;
	std::vector<LineInfo> lines;
	std::vector<StaticMesh> meshes;

protected:
	virtual void LoadFeature() override;

private:
	void OnMeshRenderSignal(MeshRendererDelegate& mr);
	static void ImGuiCallback();
};

static DrawLine feat_drawline;

CON_COMMAND(spt_draw_line,
            "Draws line between two 3D Points.\n"
            "spt_draw_line <x> <y> <z> <x> <y> <z> [r g b] [a]\n"
            "\"spt_draw_line 0\" to clear all lines")
{
	int argc = args.ArgC();
	if (argc == 2 && strcmp(args.Arg(1), "0") == 0)
	{
		feat_drawline.lines.clear();
		feat_drawline.meshes.clear();
		return;
	}

	if (argc != 7 && argc != 10 && argc != 11)
	{
		return Msg(
		    "spt_draw_line <x> <y> <z> <x> <y> <z> [r g b] [a]\n"
		    "\"spt_draw_line 0\" to clear all lines\n");
	}

	float x0 = atof(args[1]);
	float y0 = atof(args[2]);
	float z0 = atof(args[3]);
	float x1 = atof(args[4]);
	float y1 = atof(args[5]);
	float z1 = atof(args[6]);

	uint8_t r = 255;
	uint8_t g = 255;
	uint8_t b = 255;
	uint8_t a = 255;

	if (args.ArgC() >= 10)
	{
		r = (uint8_t)atoi(args[7]);
		g = (uint8_t)atoi(args[8]);
		b = (uint8_t)atoi(args[9]);
	}
	if (args.ArgC() == 11)
		a = (uint8_t)atoi(args[10]);

	feat_drawline.lines.emplace_back(Vector{x0, y0, z0}, Vector{x1, y1, z1}, color32{r, g, b, a});
	feat_drawline.should_recompute_meshes = true;
}

void DrawLine::OnMeshRenderSignal(MeshRendererDelegate& mr)
{
	if (!should_recompute_meshes)
		should_recompute_meshes = !StaticMesh::AllValid(meshes);

	if (should_recompute_meshes)
	{
		should_recompute_meshes = false;
		meshes.clear();

		spt_meshBuilder.CreateMultipleMeshes<StaticMesh>(std::back_inserter(meshes),
		                                                 lines.begin(),
		                                                 lines.cend(),
		                                                 [](MeshBuilderDelegate& mb, auto linesIter)
		                                                 {
			                                                 return mb.AddLine(linesIter->start,
			                                                                   linesIter->end,
			                                                                   LineColor{linesIter->color,
			                                                                             false});
		                                                 });
	}

	for (const auto& mesh : meshes)
	{
		mr.DrawMesh(mesh,
		            [](const CallbackInfoIn& infoIn, CallbackInfoOut& infoOut)
		            { RenderCallbackZFightFix(infoIn, infoOut); });
	}
}

void DrawLine::ImGuiCallback()
{
	auto& lines = feat_drawline.lines;
	bool& recompute = feat_drawline.should_recompute_meshes;

	static SptImGui::TimedToolTip errTip;
	static char errTipBuf[64];

	static int actionIdx = -1;
	enum
	{
		ACT_NONE,
		ACT_DELETE, // user requested to delete line [actionIdx]
		ACT_COPY,   // user requested to copy line [actionIdx]
		ACT_APPEND, // user requested to append a new blank line
	} static action = ACT_NONE;

	int focusIdx = -1;

	switch (action)
	{
	case ACT_NONE:
		break;
	case ACT_DELETE:
		if (actionIdx >= 0 && actionIdx < (int)lines.size())
		{
			lines.erase(lines.begin() + actionIdx, lines.begin() + actionIdx + 1);
			recompute = true;
		}
		break;
	case ACT_COPY:
		if (actionIdx >= 0 && actionIdx < (int)lines.size())
		{
			focusIdx = actionIdx + 1;
			lines.insert(lines.begin() + actionIdx, lines[actionIdx]);
			recompute = true;
		}
		break;
	case ACT_APPEND:
		if (!lines.empty())
			focusIdx = lines.size();
		lines.emplace_back(vec3_origin, vec3_origin, color32{255, 255, 255, 255});
		recompute = true;
		break;
	default:
		Assert(0);
	}
	action = ACT_NONE;
	actionIdx = -1;

	if (ImGui::Button("Clear"))
	{
		lines.clear();
		focusIdx = -1;
		recompute = true;
	}
	int importType = 0;
	ImGui::Text("Import from clipboard:");
	ImGui::SameLine();
	if (ImGui::Button("Replace"))
		importType = 1;
	ImGui::SameLine();
	if (ImGui::Button("Append"))
		importType = 2;
	if (importType)
	{
		std::vector<DrawLine::LineInfo> newLines;
		const char* origClipboard = ImGui::GetClipboardText();
		const char* clipboard = origClipboard;
		bool err = false;
		if (!origClipboard)
		{
			err = true;
			errTip.StartShowing("nothing in clipboard...");
		}
		while (!err && *clipboard)
		{
			const char* nextNlChar = strchr(clipboard, '\n');
			DrawLine::LineInfo line{{}, {}, color32{255, 255, 255, 255}};
			int nFilled = _snscanf_s(clipboard,
			                         nextNlChar - clipboard,
			                         "%*s %g %g %g %g %g %g %hhu %hhu %hhu %hhu",
			                         &line.start.x,
			                         &line.start.y,
			                         &line.start.z,
			                         &line.end.x,
			                         &line.end.y,
			                         &line.end.z,
			                         &line.color.r,
			                         &line.color.g,
			                         &line.color.b,
			                         &line.color.a);
			if (nFilled != 6 && nFilled != 9 && nFilled != 10)
			{
				err = true;
				
				int written = snprintf(errTipBuf,
				                       sizeof errTipBuf,
				                       "syntax error at byte %u: \"%s",
				                       clipboard - origClipboard,
				                       clipboard);
				strcpy(MIN(errTipBuf + sizeof(errTipBuf) - 5, errTipBuf + written), "...\"");
				errTip.StartShowing(errTipBuf);
			}
			else
			{
				newLines.push_back(line);
				if (!nextNlChar)
					break;
				for (clipboard = nextNlChar + 1; isspace(*clipboard); clipboard++)
					;
			}
		};
		if (!err)
		{
			if (importType == 1)
				lines.clear();
			lines.insert(lines.end(), newLines.cbegin(), newLines.cend());
			errTip.StopShowing();
			focusIdx = -1;
			recompute = true;
		}
	}
	errTip.Show(SPT_IMGUI_WARN_COLOR_YELLOW, 2.0);
	if (ImGui::Button("Export to clipboard"))
	{
		ImGui::LogToClipboard();
		const char* cmdName = WrangleLegacyCommandName(spt_draw_line_command.GetName(), true, nullptr);
		for (auto& line : lines)
		{
			ImGui::LogText("%s %g %g %g %g %g %g %hhu %hhu %hhu %hhu\n",
			               cmdName,
			               line.start.x,
			               line.start.y,
			               line.start.z,
			               line.end.x,
			               line.end.y,
			               line.end.z,
			               line.color.r,
			               line.color.g,
			               line.color.b,
			               line.color.a);
		}
		ImGui::LogFinish();
	}

	bool drawChildWnd = !lines.empty();
	if (drawChildWnd)
	{
		/*
		* For small amounts of items, we let the AutoResizeY flag handle the work for us. Nested
		* scroll bars are evil though - so to prevent them set a maximum size for the window here
		* (and leave enough space for the append button). For some reason negative y values are not
		* handled the way that I expect them to here, hence the funny size_max calculation. We do
		* get a single frame of the window being the wrong size if we e.g. clear all items and then
		* add a new one, but the only way to fix that would be to use SetNextWindowSize().
		*/
		float sizeMaxY = MAX(ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeight(), FLT_MIN);
		ImGui::SetNextWindowSizeConstraints({-FLT_MIN, FLT_MIN}, {-FLT_MIN, sizeMaxY});
	}

	/*
	* To gracefully handle modification of the line vector and setting focus, nothing in this child
	* window should modify the vector. Instead, save any changes to 'action'/'actionIdx' and apply
	* them next frame if possible.
	* 
	* There may be a better way to layout all of these elements than the group crap I threw
	* together... that'll be for another day :).
	*/
	if (drawChildWnd && ImGui::BeginChild("##lines", {0, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
	{
		ImGuiListClipper clipper;
		clipper.Begin(lines.size());
		if (focusIdx != -1)
			clipper.IncludeItemByIndex(focusIdx);
		int nLinesLog10 = (int)floor(log10(lines.size() - 1)) + 1;
		while (clipper.Step())
		{
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
			{
				ImGui::PushID(i);
				auto& line = lines[i];
				ImGui::BeginGroup(); // BEGIN LINE GROUP
				ImGui::BeginGroup(); // BEGIN TEXT/DRAG GROUP

				ImGui::Text("[%.*d]", nLinesLog10, i);
				ImGui::SameLine();

				ImGui::BeginGroup(); // BEGIN DRAG GROUP

				if (ImGui::DragFloat3(
				        "start", line.start.Base(), 1, 0, 0, "%g", ImGuiSliderFlags_NoRoundToFormat))
				{
					recompute = true;
				}
				if (ImGui::DragFloat3(
				        "end", line.end.Base(), 1, 0, 0, "%g", ImGuiSliderFlags_NoRoundToFormat))
				{
					recompute = true;
				}
				ImGui::EndGroup(); // END DRAG GROUP
				ImGui::EndGroup(); // END TEXT/DRAG GROUP
				ImGui::SameLine();
				ImGui::BeginGroup(); // BEGIN COLOR/COPY/DELETE GROUP

				ImVec4 colf = ImGui::ColorConvertU32ToFloat4(Color32ToImU32(line.color));
				if (ImGui::ColorEdit4("color",
				                      (float*)&colf,
				                      ImGuiColorEditFlags_NoInputs
				                          | ImGuiColorEditFlags_AlphaPreviewHalf
				                          | ImGuiColorEditFlags_AlphaBar))
				{
					line.color = ImU32ToColor32(ImGui::ColorConvertFloat4ToU32(colf));
					recompute = true;
				}

				if (ImGui::SmallButton("+"))
				{
					action = ACT_COPY;
					actionIdx = i;
				}
				ImGui::SetItemTooltip("copy this line");
				ImGui::SameLine();
				if (ImGui::SmallButton("-"))
				{
					action = ACT_DELETE;
					actionIdx = i;
				}
				ImGui::SetItemTooltip("delete this line");
				ImGui::EndGroup(); // END COLOR/COPY/DELETE GROUP

				ImGui::PopID();

				if (i != (int)lines.size() - 1)
					ImGui::Separator();

				ImGui::EndGroup(); // END LINE GROUP

				if (i == focusIdx)
					ImGui::ScrollToItem(); // clipper.SeekCursorForItem(i); doesn't work here...
			}
		}
	}
	if (drawChildWnd)
		ImGui::EndChild();

	ImGui::Text("%u line%s total", lines.size(), lines.size() == 1 ? "" : "s");
	ImGui::SameLine();
	if (ImGui::SmallButton("+"))
		action = ACT_APPEND;
	ImGui::SetItemTooltip("append a new line");
}

void DrawLine::LoadFeature()
{
	if (!spt_meshRenderer.signal.Works)
		return;

	InitCommand(spt_draw_line);
	spt_meshRenderer.signal.Connect(this, &DrawLine::OnMeshRenderSignal);
	SptImGuiGroup::Draw_Lines.RegisterUserCallback(ImGuiCallback);
}

#endif
