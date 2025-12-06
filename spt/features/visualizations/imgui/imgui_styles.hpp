#pragma once

#include <map>
#include <functional>
#include <string>
#include "SPTLib/patterns.hpp"
#include "imgui_interface.hpp"

/*
* Includes the 3 default ImGui color themes as well as some stuff from:
* https://github.com/GraphicsProgramming/dear-imgui-styles. You can add your own themes here by
* exporting them from Dear ImGui Demo -> Tools -> Style Editor -> Colors -> Export. Just make
* sure to uncheck "Only Modified Colors". IDK if there's a good way to export style pixel sizes
* in the same way. Probably best to avoid changing them unless you set the values for all themes.
*/

#pragma warning(push)
#pragma warning(disable : 4189)

namespace SptImGuiStyles
{
	inline static std::map<std::string, std::function<void()>> registeredStyles;
	inline static std::pair<std::string, std::function<void()>> defaultStyle;

	struct _StaticRegisterStyle
	{
		_StaticRegisterStyle(const std::string& name,
		                     const std::function<void()>& setStyleFunc,
		                     bool default_ = false)
		{
			auto emp = registeredStyles.emplace(name, setStyleFunc);
			AssertMsg(emp.second, "spt: registering style with the same name twice");
			if (default_)
			{
				AssertMsg(defaultStyle.first.empty(), "spt: default style already registered");
				defaultStyle = *emp.first;
			}
		}
	};

#define REGISTER_IMGUI_STYLE(name, func) \
	inline static _StaticRegisterStyle CONCATENATE(_Style_, __COUNTER__){name, func};
#define REGISTER_DEFAULT_IMGUI_STYLE(name, func) \
	inline static _StaticRegisterStyle CONCATENATE(_Style_, __COUNTER__){name, func, true};

	REGISTER_DEFAULT_IMGUI_STYLE("ImGui Dark", []() { ImGui::StyleColorsDark(); });

	REGISTER_IMGUI_STYLE("ImGui Classic", []() { ImGui::StyleColorsClassic(); });

	REGISTER_IMGUI_STYLE("ImGui Light",
	                     []()
	                     {
		                     ImGui::StyleColorsLight();
		                     ImGui::GetStyle().Alpha = 0.8f;
	                     });

	static void Cinder()
	{
		auto& mStyle = ImGui::GetStyle();

		// mStyle.WindowMinSize = ImVec2(160, 20);
		// mStyle.FramePadding = ImVec2(4, 2);
		// mStyle.ItemSpacing = ImVec2(6, 2);
		// mStyle.ItemInnerSpacing = ImVec2(6, 4);
		mStyle.Alpha = 0.95f;
		// mStyle.WindowRounding = 4.0f;
		// mStyle.FrameRounding = 2.0f;
		// mStyle.IndentSpacing = 6.0f;
		// mStyle.ItemInnerSpacing = ImVec2(2, 4);
		// mStyle.ColumnsMinSpacing = 50.0f;
		// mStyle.GrabMinSize = 14.0f;
		// mStyle.GrabRounding = 16.0f;
		// mStyle.ScrollbarSize = 12.0f;
		// mStyle.ScrollbarRounding = 16.0f;

		ImGuiStyle& style = mStyle;
		style.Colors[ImGuiCol_Text] = ImVec4(0.86f, 0.93f, 0.89f, 0.78f);
		style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.86f, 0.93f, 0.89f, 0.28f);
		style.Colors[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
		style.Colors[ImGuiCol_Border] = ImVec4(0.31f, 0.31f, 1.00f, 0.00f);
		style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
		style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
		style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_TitleBg] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
		style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.20f, 0.22f, 0.27f, 0.75f);
		style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.20f, 0.22f, 0.27f, 0.47f);
		style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
		style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.09f, 0.15f, 0.16f, 1.00f);
		style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
		style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_CheckMark] = ImVec4(0.71f, 0.22f, 0.27f, 1.00f);
		style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.47f, 0.77f, 0.83f, 0.14f);
		style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_Button] = ImVec4(0.47f, 0.77f, 0.83f, 0.14f);
		style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.86f);
		style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_Header] = ImVec4(0.92f, 0.18f, 0.29f, 0.76f);
		style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.86f);
		style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_Separator] = ImVec4(0.14f, 0.16f, 0.19f, 1.00f);
		style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
		style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.47f, 0.77f, 0.83f, 0.04f);
		style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
		style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_PlotLines] = ImVec4(0.86f, 0.93f, 0.89f, 0.63f);
		style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.86f, 0.93f, 0.89f, 0.63f);
		style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
		style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.92f, 0.18f, 0.29f, 0.43f);
		style.Colors[ImGuiCol_PopupBg] = ImVec4(0.20f, 0.22f, 0.27f, 0.9f);
		style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.20f, 0.22f, 0.27f, 0.73f);
	}
	REGISTER_IMGUI_STYLE("Cinder", Cinder);

	static void SpectrumLight()
	{
		using namespace ImGui;

		auto Color = [](unsigned int c)
		{
			unsigned int tmp = c | ((unsigned int)255 << 24);
			return Color32ToImU32(*(color32*)&tmp);
		};

		ImGuiStyle* style = &ImGui::GetStyle();
		style->Alpha = 0.85f;
		// style->GrabRounding = 4.0f;

		const unsigned int NONE = 0x00000000;
		const unsigned int GRAY50 = Color(0xFFFFFF);
		const unsigned int GRAY75 = Color(0xFAFAFA);
		const unsigned int GRAY100 = Color(0xF5F5F5);
		const unsigned int GRAY200 = Color(0xEAEAEA);
		const unsigned int GRAY300 = Color(0xE1E1E1);
		const unsigned int GRAY400 = Color(0xCACACA);
		const unsigned int GRAY500 = Color(0xB3B3B3);
		const unsigned int GRAY600 = Color(0x8E8E8E);
		const unsigned int GRAY700 = Color(0x707070);
		const unsigned int GRAY800 = Color(0x4B4B4B);
		const unsigned int GRAY900 = Color(0x2C2C2C);
		const unsigned int BLUE400 = Color(0x2680EB);
		const unsigned int BLUE500 = Color(0x1473E6);
		const unsigned int BLUE600 = Color(0x0D66D0);
		const unsigned int BLUE700 = Color(0x095ABA);
		const unsigned int RED400 = Color(0xE34850);
		const unsigned int RED500 = Color(0xD7373F);
		const unsigned int RED600 = Color(0xC9252D);
		const unsigned int RED700 = Color(0xBB121A);
		const unsigned int ORANGE400 = Color(0xE68619);
		const unsigned int ORANGE500 = Color(0xDA7B11);
		const unsigned int ORANGE600 = Color(0xCB6F10);
		const unsigned int ORANGE700 = Color(0xBD640D);
		const unsigned int GREEN400 = Color(0x2D9D78);
		const unsigned int GREEN500 = Color(0x268E6C);
		const unsigned int GREEN600 = Color(0x12805C);
		const unsigned int GREEN700 = Color(0x107154);
		const unsigned int INDIGO400 = Color(0x6767EC);
		const unsigned int INDIGO500 = Color(0x5C5CE0);
		const unsigned int INDIGO600 = Color(0x5151D3);
		const unsigned int INDIGO700 = Color(0x4646C6);
		const unsigned int CELERY400 = Color(0x44B556);
		const unsigned int CELERY500 = Color(0x3DA74E);
		const unsigned int CELERY600 = Color(0x379947);
		const unsigned int CELERY700 = Color(0x318B40);
		const unsigned int MAGENTA400 = Color(0xD83790);
		const unsigned int MAGENTA500 = Color(0xCE2783);
		const unsigned int MAGENTA600 = Color(0xBC1C74);
		const unsigned int MAGENTA700 = Color(0xAE0E66);
		const unsigned int YELLOW400 = Color(0xDFBF00);
		const unsigned int YELLOW500 = Color(0xD2B200);
		const unsigned int YELLOW600 = Color(0xC4A600);
		const unsigned int YELLOW700 = Color(0xB79900);
		const unsigned int FUCHSIA400 = Color(0xC038CC);
		const unsigned int FUCHSIA500 = Color(0xB130BD);
		const unsigned int FUCHSIA600 = Color(0xA228AD);
		const unsigned int FUCHSIA700 = Color(0x93219E);
		const unsigned int SEAFOAM400 = Color(0x1B959A);
		const unsigned int SEAFOAM500 = Color(0x16878C);
		const unsigned int SEAFOAM600 = Color(0x0F797D);
		const unsigned int SEAFOAM700 = Color(0x096C6F);
		const unsigned int CHARTREUSE400 = Color(0x85D044);
		const unsigned int CHARTREUSE500 = Color(0x7CC33F);
		const unsigned int CHARTREUSE600 = Color(0x73B53A);
		const unsigned int CHARTREUSE700 = Color(0x6AA834);
		const unsigned int PURPLE400 = Color(0x9256D9);
		const unsigned int PURPLE500 = Color(0x864CCC);
		const unsigned int PURPLE600 = Color(0x7A42BF);
		const unsigned int PURPLE700 = Color(0x6F38B1);

		ImVec4* colors = style->Colors;
		colors[ImGuiCol_Text] = ColorConvertU32ToFloat4(GRAY800); // text on hovered controls is gray900
		colors[ImGuiCol_TextDisabled] = ColorConvertU32ToFloat4(GRAY500);
		colors[ImGuiCol_WindowBg] = ColorConvertU32ToFloat4(GRAY100);
		colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_PopupBg] =
		    ColorConvertU32ToFloat4(GRAY50); // not sure about this. Note: applies to tooltips too.
		colors[ImGuiCol_Border] = ColorConvertU32ToFloat4(GRAY300);
		colors[ImGuiCol_BorderShadow] = ColorConvertU32ToFloat4(NONE); // We don't want shadows. Ever.
		colors[ImGuiCol_FrameBg] = ColorConvertU32ToFloat4(
		    GRAY75); // this isnt right, spectrum does not do this, but it's a good fallback
		colors[ImGuiCol_FrameBgHovered] = ColorConvertU32ToFloat4(GRAY50);
		colors[ImGuiCol_FrameBgActive] = ColorConvertU32ToFloat4(GRAY200);
		colors[ImGuiCol_TitleBg] = ColorConvertU32ToFloat4(
		    GRAY300); // those titlebar values are totally made up, spectrum does not have this.
		colors[ImGuiCol_TitleBgActive] = ColorConvertU32ToFloat4(GRAY200);
		colors[ImGuiCol_TitleBgCollapsed] = ColorConvertU32ToFloat4(GRAY400);
		colors[ImGuiCol_MenuBarBg] = ColorConvertU32ToFloat4(GRAY100);
		colors[ImGuiCol_ScrollbarBg] = ColorConvertU32ToFloat4(GRAY100); // same as regular background
		colors[ImGuiCol_ScrollbarGrab] = ColorConvertU32ToFloat4(GRAY400);
		colors[ImGuiCol_ScrollbarGrabHovered] = ColorConvertU32ToFloat4(GRAY600);
		colors[ImGuiCol_ScrollbarGrabActive] = ColorConvertU32ToFloat4(GRAY700);
		colors[ImGuiCol_CheckMark] = ColorConvertU32ToFloat4(BLUE500);
		colors[ImGuiCol_SliderGrab] = ColorConvertU32ToFloat4(GRAY700);
		colors[ImGuiCol_SliderGrabActive] = ColorConvertU32ToFloat4(GRAY800);
		colors[ImGuiCol_Button] =
		    ColorConvertU32ToFloat4(GRAY75); // match default button to Spectrum's 'Action Button'.
		colors[ImGuiCol_ButtonHovered] = ColorConvertU32ToFloat4(GRAY50);
		colors[ImGuiCol_ButtonActive] = ColorConvertU32ToFloat4(GRAY200);
		colors[ImGuiCol_Header] = ColorConvertU32ToFloat4(BLUE400);
		colors[ImGuiCol_HeaderHovered] = ColorConvertU32ToFloat4(BLUE500);
		colors[ImGuiCol_HeaderActive] = ColorConvertU32ToFloat4(BLUE600);
		colors[ImGuiCol_Separator] = ColorConvertU32ToFloat4(GRAY400);
		colors[ImGuiCol_SeparatorHovered] = ColorConvertU32ToFloat4(GRAY600);
		colors[ImGuiCol_SeparatorActive] = ColorConvertU32ToFloat4(GRAY700);
		colors[ImGuiCol_ResizeGrip] = ColorConvertU32ToFloat4(GRAY400);
		colors[ImGuiCol_ResizeGripHovered] = ColorConvertU32ToFloat4(GRAY600);
		colors[ImGuiCol_ResizeGripActive] = ColorConvertU32ToFloat4(GRAY700);
		colors[ImGuiCol_PlotLines] = ColorConvertU32ToFloat4(BLUE400);
		colors[ImGuiCol_PlotLinesHovered] = ColorConvertU32ToFloat4(BLUE600);
		colors[ImGuiCol_PlotHistogram] = ColorConvertU32ToFloat4(BLUE400);
		colors[ImGuiCol_PlotHistogramHovered] = ColorConvertU32ToFloat4(BLUE600);
		colors[ImGuiCol_TextSelectedBg] = ColorConvertU32ToFloat4((BLUE400 & 0x00FFFFFF) | 0x33000000);
		colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
		// colors[ImGuiCol_NavHighlight] = ColorConvertU32ToFloat4((GRAY900 & 0x00FFFFFF) | 0x0A000000);
		colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
		colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
		colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
	}
	REGISTER_IMGUI_STYLE("Spectrum Light", SpectrumLight);

	static void SpectrumDark()
	{
		using namespace ImGui;

		auto Color = [](unsigned int c)
		{
			unsigned int tmp = c | ((unsigned int)255 << 24);
			return Color32ToImU32(*(color32*)&tmp);
		};

		ImGuiStyle* style = &ImGui::GetStyle();
		style->Alpha = 0.85f;
		// style->GrabRounding = 4.0f;

		const unsigned int NONE = 0x00000000;
		const unsigned int GRAY50 = Color(0x252525);
		const unsigned int GRAY75 = Color(0x2F2F2F);
		const unsigned int GRAY100 = Color(0x323232);
		const unsigned int GRAY200 = Color(0x393939);
		const unsigned int GRAY300 = Color(0x3E3E3E);
		const unsigned int GRAY400 = Color(0x4D4D4D);
		const unsigned int GRAY500 = Color(0x5C5C5C);
		const unsigned int GRAY600 = Color(0x7B7B7B);
		const unsigned int GRAY700 = Color(0x999999);
		const unsigned int GRAY800 = Color(0xCDCDCD);
		const unsigned int GRAY900 = Color(0xFFFFFF);
		const unsigned int BLUE400 = Color(0x2680EB);
		const unsigned int BLUE500 = Color(0x378EF0);
		const unsigned int BLUE600 = Color(0x4B9CF5);
		const unsigned int BLUE700 = Color(0x5AA9FA);
		const unsigned int RED400 = Color(0xE34850);
		const unsigned int RED500 = Color(0xEC5B62);
		const unsigned int RED600 = Color(0xF76D74);
		const unsigned int RED700 = Color(0xFF7B82);
		const unsigned int ORANGE400 = Color(0xE68619);
		const unsigned int ORANGE500 = Color(0xF29423);
		const unsigned int ORANGE600 = Color(0xF9A43F);
		const unsigned int ORANGE700 = Color(0xFFB55B);
		const unsigned int GREEN400 = Color(0x2D9D78);
		const unsigned int GREEN500 = Color(0x33AB84);
		const unsigned int GREEN600 = Color(0x39B990);
		const unsigned int GREEN700 = Color(0x3FC89C);
		const unsigned int INDIGO400 = Color(0x6767EC);
		const unsigned int INDIGO500 = Color(0x7575F1);
		const unsigned int INDIGO600 = Color(0x8282F6);
		const unsigned int INDIGO700 = Color(0x9090FA);
		const unsigned int CELERY400 = Color(0x44B556);
		const unsigned int CELERY500 = Color(0x4BC35F);
		const unsigned int CELERY600 = Color(0x51D267);
		const unsigned int CELERY700 = Color(0x58E06F);
		const unsigned int MAGENTA400 = Color(0xD83790);
		const unsigned int MAGENTA500 = Color(0xE2499D);
		const unsigned int MAGENTA600 = Color(0xEC5AAA);
		const unsigned int MAGENTA700 = Color(0xF56BB7);
		const unsigned int YELLOW400 = Color(0xDFBF00);
		const unsigned int YELLOW500 = Color(0xEDCC00);
		const unsigned int YELLOW600 = Color(0xFAD900);
		const unsigned int YELLOW700 = Color(0xFFE22E);
		const unsigned int FUCHSIA400 = Color(0xC038CC);
		const unsigned int FUCHSIA500 = Color(0xCF3EDC);
		const unsigned int FUCHSIA600 = Color(0xD951E5);
		const unsigned int FUCHSIA700 = Color(0xE366EF);
		const unsigned int SEAFOAM400 = Color(0x1B959A);
		const unsigned int SEAFOAM500 = Color(0x20A3A8);
		const unsigned int SEAFOAM600 = Color(0x23B2B8);
		const unsigned int SEAFOAM700 = Color(0x26C0C7);
		const unsigned int CHARTREUSE400 = Color(0x85D044);
		const unsigned int CHARTREUSE500 = Color(0x8EDE49);
		const unsigned int CHARTREUSE600 = Color(0x9BEC54);
		const unsigned int CHARTREUSE700 = Color(0xA3F858);
		const unsigned int PURPLE400 = Color(0x9256D9);
		const unsigned int PURPLE500 = Color(0x9D64E1);
		const unsigned int PURPLE600 = Color(0xA873E9);
		const unsigned int PURPLE700 = Color(0xB483F0);

		ImVec4* colors = style->Colors;
		colors[ImGuiCol_Text] = ColorConvertU32ToFloat4(GRAY800); // text on hovered controls is gray900
		colors[ImGuiCol_TextDisabled] = ColorConvertU32ToFloat4(GRAY500);
		colors[ImGuiCol_WindowBg] = ColorConvertU32ToFloat4(GRAY100);
		colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_PopupBg] =
		    ColorConvertU32ToFloat4(GRAY50); // not sure about this. Note: applies to tooltips too.
		colors[ImGuiCol_Border] = ColorConvertU32ToFloat4(GRAY300);
		colors[ImGuiCol_BorderShadow] = ColorConvertU32ToFloat4(NONE); // We don't want shadows. Ever.
		colors[ImGuiCol_FrameBg] = ColorConvertU32ToFloat4(
		    GRAY75); // this isnt right, spectrum does not do this, but it's a good fallback
		colors[ImGuiCol_FrameBgHovered] = ColorConvertU32ToFloat4(GRAY50);
		colors[ImGuiCol_FrameBgActive] = ColorConvertU32ToFloat4(GRAY200);
		colors[ImGuiCol_TitleBg] = ColorConvertU32ToFloat4(
		    GRAY300); // those titlebar values are totally made up, spectrum does not have this.
		colors[ImGuiCol_TitleBgActive] = ColorConvertU32ToFloat4(GRAY200);
		colors[ImGuiCol_TitleBgCollapsed] = ColorConvertU32ToFloat4(GRAY400);
		colors[ImGuiCol_MenuBarBg] = ColorConvertU32ToFloat4(GRAY100);
		colors[ImGuiCol_ScrollbarBg] = ColorConvertU32ToFloat4(GRAY100); // same as regular background
		colors[ImGuiCol_ScrollbarGrab] = ColorConvertU32ToFloat4(GRAY400);
		colors[ImGuiCol_ScrollbarGrabHovered] = ColorConvertU32ToFloat4(GRAY600);
		colors[ImGuiCol_ScrollbarGrabActive] = ColorConvertU32ToFloat4(GRAY700);
		colors[ImGuiCol_CheckMark] = ColorConvertU32ToFloat4(BLUE500);
		colors[ImGuiCol_SliderGrab] = ColorConvertU32ToFloat4(GRAY700);
		colors[ImGuiCol_SliderGrabActive] = ColorConvertU32ToFloat4(GRAY800);
		colors[ImGuiCol_Button] =
		    ColorConvertU32ToFloat4(GRAY75); // match default button to Spectrum's 'Action Button'.
		colors[ImGuiCol_ButtonHovered] = ColorConvertU32ToFloat4(GRAY50);
		colors[ImGuiCol_ButtonActive] = ColorConvertU32ToFloat4(GRAY200);
		colors[ImGuiCol_Header] = ColorConvertU32ToFloat4(BLUE400);
		colors[ImGuiCol_HeaderHovered] = ColorConvertU32ToFloat4(BLUE500);
		colors[ImGuiCol_HeaderActive] = ColorConvertU32ToFloat4(BLUE600);
		colors[ImGuiCol_Separator] = ColorConvertU32ToFloat4(GRAY400);
		colors[ImGuiCol_SeparatorHovered] = ColorConvertU32ToFloat4(GRAY600);
		colors[ImGuiCol_SeparatorActive] = ColorConvertU32ToFloat4(GRAY700);
		colors[ImGuiCol_ResizeGrip] = ColorConvertU32ToFloat4(GRAY400);
		colors[ImGuiCol_ResizeGripHovered] = ColorConvertU32ToFloat4(GRAY600);
		colors[ImGuiCol_ResizeGripActive] = ColorConvertU32ToFloat4(GRAY700);
		colors[ImGuiCol_PlotLines] = ColorConvertU32ToFloat4(BLUE400);
		colors[ImGuiCol_PlotLinesHovered] = ColorConvertU32ToFloat4(BLUE600);
		colors[ImGuiCol_PlotHistogram] = ColorConvertU32ToFloat4(BLUE400);
		colors[ImGuiCol_PlotHistogramHovered] = ColorConvertU32ToFloat4(BLUE600);
		colors[ImGuiCol_TextSelectedBg] = ColorConvertU32ToFloat4((BLUE400 & 0x00FFFFFF) | 0x33000000);
		colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
		// colors[ImGuiCol_NavHighlight] = ColorConvertU32ToFloat4((GRAY900 & 0x00FFFFFF) | 0x0A000000);
		colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
		colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
		colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
	}
	REGISTER_IMGUI_STYLE("Spectrum Dark", SpectrumDark);

	static void EnemyMouse()
	{
		ImGuiStyle& style = ImGui::GetStyle();
		style.Alpha = 0.9f;
		// style.WindowFillAlphaDefault = 0.83;
		// style.ChildRounding = 3;
		// style.WindowRounding = 3;
		// style.GrabRounding = 1;
		// style.GrabMinSize = 20;
		// style.FrameRounding = 3;

		style.Colors[ImGuiCol_Text] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
		style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.00f, 0.40f, 0.41f, 1.00f);
		style.Colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_Border] = ImVec4(0.00f, 1.00f, 1.00f, 0.65f);
		style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_FrameBg] = ImVec4(0.44f, 0.80f, 0.80f, 0.18f);
		style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.44f, 0.80f, 0.80f, 0.27f);
		style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.44f, 0.81f, 0.86f, 0.66f);
		style.Colors[ImGuiCol_TitleBg] = ImVec4(0.14f, 0.18f, 0.21f, 0.73f);
		style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.54f);
		style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.00f, 1.00f, 1.00f, 0.27f);
		style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.20f);
		style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.22f, 0.29f, 0.30f, 0.71f);
		style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.00f, 1.00f, 1.00f, 0.44f);
		style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.00f, 1.00f, 1.00f, 0.74f);
		style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
		// style.Colors[ImGuiCol_ComboBg] = ImVec4(0.16f, 0.24f, 0.22f, 0.60f);
		style.Colors[ImGuiCol_CheckMark] = ImVec4(0.00f, 1.00f, 1.00f, 0.68f);
		style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.00f, 1.00f, 1.00f, 0.36f);
		style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.00f, 1.00f, 1.00f, 0.76f);
		style.Colors[ImGuiCol_Button] = ImVec4(0.00f, 0.65f, 0.65f, 0.46f);
		style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.01f, 1.00f, 1.00f, 0.43f);
		style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.00f, 1.00f, 1.00f, 0.62f);
		style.Colors[ImGuiCol_Header] = ImVec4(0.00f, 1.00f, 1.00f, 0.33f);
		style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 1.00f, 1.00f, 0.42f);
		style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.00f, 1.00f, 1.00f, 0.54f);
		style.Colors[ImGuiCol_Separator] = ImVec4(0.00f, 0.50f, 0.50f, 0.33f);
		style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.00f, 0.50f, 0.50f, 0.47f);
		style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.00f, 0.70f, 0.70f, 1.00f);
		style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.00f, 1.00f, 1.00f, 0.54f);
		style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.00f, 1.00f, 1.00f, 0.74f);
		style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
		style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.08f, 0.19f, 0.32f, 1.00f);
		style.Colors[ImGuiCol_PopupBg] = ImVec4(0.06f, 0.19f, 0.17f, 1.00f);
		// style.Colors[ImGuiCol_CloseButton] = ImVec4(0.00f, 0.78f, 0.78f, 0.35f);
		// style.Colors[ImGuiCol_CloseButtonHovered] = ImVec4(0.00f, 0.78f, 0.78f, 0.47f);
		// style.Colors[ImGuiCol_CloseButtonActive] = ImVec4(0.00f, 0.78f, 0.78f, 1.00f);
		style.Colors[ImGuiCol_PlotLines] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
		style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
		style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.00f, 1.00f, 1.00f, 0.22f);
		// style.Colors[ImGuiCol_TooltipBg] = ImVec4(0.00f, 0.13f, 0.13f, 0.90f);
		style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.04f, 0.10f, 0.09f, 0.51f);
	}
	REGISTER_IMGUI_STYLE("Enemy Mouse", EnemyMouse);

	static void SynthMaster()
	{
		ImGuiStyle* style = &ImGui::GetStyle();

		// style->WindowPadding = ImVec2(15, 15);
		// style->WindowRounding = 5.0f;
		// style->FramePadding = ImVec2(5, 5);
		// style->FrameRounding = 4.0f;
		// style->ItemSpacing = ImVec2(12, 8);
		// style->ItemInnerSpacing = ImVec2(8, 6);
		// style->IndentSpacing = 25.0f;
		// style->ScrollbarSize = 15.0f;
		// style->ScrollbarRounding = 9.0f;
		// style->GrabMinSize = 5.0f;
		// style->GrabRounding = 3.0f;

		style->Colors[ImGuiCol_Text] = ImVec4(0.40f, 0.39f, 0.38f, 1.00f);
		style->Colors[ImGuiCol_TextDisabled] = ImVec4(0.40f, 0.39f, 0.38f, 0.77f);
		style->Colors[ImGuiCol_WindowBg] = ImVec4(0.92f, 0.91f, 0.88f, 0.70f);
		style->Colors[ImGuiCol_ChildBg] = ImVec4(1.00f, 0.98f, 0.95f, 0.58f);
		style->Colors[ImGuiCol_PopupBg] = ImVec4(0.92f, 0.91f, 0.88f, 0.92f);
		style->Colors[ImGuiCol_Border] = ImVec4(0.84f, 0.83f, 0.80f, 0.65f);
		style->Colors[ImGuiCol_BorderShadow] = ImVec4(0.92f, 0.91f, 0.88f, 0.00f);
		style->Colors[ImGuiCol_FrameBg] = ImVec4(1.00f, 0.98f, 0.95f, 1.00f);
		style->Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.99f, 1.00f, 0.40f, 0.78f);
		style->Colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 1.00f, 0.00f, 1.00f);
		style->Colors[ImGuiCol_TitleBg] = ImVec4(1.00f, 0.98f, 0.95f, 1.00f);
		style->Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.00f, 0.98f, 0.95f, 0.75f);
		style->Colors[ImGuiCol_TitleBgActive] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
		style->Colors[ImGuiCol_MenuBarBg] = ImVec4(1.00f, 0.98f, 0.95f, 0.47f);
		style->Colors[ImGuiCol_ScrollbarBg] = ImVec4(1.00f, 0.98f, 0.95f, 1.00f);
		style->Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.00f, 0.00f, 0.00f, 0.21f);
		style->Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.90f, 0.91f, 0.00f, 0.78f);
		style->Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
		// style->Colors[ImGuiCol_ComboBg] = ImVec4(1.00f, 0.98f, 0.95f, 1.00f);
		style->Colors[ImGuiCol_CheckMark] = ImVec4(0.25f, 1.00f, 0.00f, 0.80f);
		style->Colors[ImGuiCol_SliderGrab] = ImVec4(0.00f, 0.00f, 0.00f, 0.14f);
		style->Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
		style->Colors[ImGuiCol_Button] = ImVec4(0.00f, 0.00f, 0.00f, 0.14f);
		style->Colors[ImGuiCol_ButtonHovered] = ImVec4(0.99f, 1.00f, 0.22f, 0.86f);
		style->Colors[ImGuiCol_ButtonActive] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
		style->Colors[ImGuiCol_Header] = ImVec4(0.25f, 1.00f, 0.00f, 0.76f);
		style->Colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 1.00f, 0.00f, 0.86f);
		style->Colors[ImGuiCol_HeaderActive] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
		style->Colors[ImGuiCol_Header] = ImVec4(0.00f, 0.00f, 0.00f, 0.32f);
		style->Colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 1.00f, 0.00f, 0.78f);
		style->Colors[ImGuiCol_HeaderActive] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
		style->Colors[ImGuiCol_ResizeGrip] = ImVec4(0.00f, 0.00f, 0.00f, 0.04f);
		style->Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.25f, 1.00f, 0.00f, 0.78f);
		style->Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
		// style->Colors[ImGuiCol_CloseButton] = ImVec4(0.40f, 0.39f, 0.38f, 0.16f);
		// style->Colors[ImGuiCol_CloseButtonHovered] = ImVec4(0.40f, 0.39f, 0.38f, 0.39f);
		// style->Colors[ImGuiCol_CloseButtonActive] = ImVec4(0.40f, 0.39f, 0.38f, 1.00f);
		style->Colors[ImGuiCol_PlotLines] = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
		style->Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
		style->Colors[ImGuiCol_PlotHistogram] = ImVec4(0.40f, 0.39f, 0.38f, 0.63f);
		style->Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.25f, 1.00f, 0.00f, 1.00f);
		style->Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.25f, 1.00f, 0.00f, 0.43f);
		style->Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(1.00f, 0.98f, 0.95f, 0.73f);
	}
	REGISTER_IMGUI_STYLE("Synth Master", SynthMaster);

	static void DougBinks(bool dark, float alpha_)
	{
		ImGuiStyle& style = ImGui::GetStyle();

		style.Alpha = 1.0f;
		// style.FrameRounding = 3.0f;

		style.Colors[ImGuiCol_Text] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
		style.Colors[ImGuiCol_WindowBg] = ImVec4(0.94f, 0.94f, 0.94f, 0.94f);
		style.Colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		style.Colors[ImGuiCol_PopupBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.94f);
		style.Colors[ImGuiCol_Border] = ImVec4(0.00f, 0.00f, 0.00f, 0.39f);
		style.Colors[ImGuiCol_BorderShadow] = ImVec4(1.00f, 1.00f, 1.00f, 0.10f);
		style.Colors[ImGuiCol_FrameBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.94f);
		style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
		style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
		style.Colors[ImGuiCol_TitleBg] = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);
		style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(1.00f, 1.00f, 1.00f, 0.51f);
		style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.82f, 0.82f, 0.82f, 1.00f);
		style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);
		style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.98f, 0.98f, 0.98f, 0.53f);
		style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.69f, 0.69f, 0.69f, 1.00f);
		style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.59f, 0.59f, 0.59f, 1.00f);
		style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.49f, 0.49f, 0.49f, 1.00f);
		// style.Colors[ImGuiCol_ComboBg] = ImVec4(0.86f, 0.86f, 0.86f, 0.99f);
		style.Colors[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.24f, 0.52f, 0.88f, 1.00f);
		style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_Button] = ImVec4(0.26f, 0.59f, 0.98f, 0.40f);
		style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_Header] = ImVec4(0.26f, 0.59f, 0.98f, 0.31f);
		style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
		style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_Header] = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
		style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.78f);
		style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		style.Colors[ImGuiCol_ResizeGrip] = ImVec4(1.00f, 1.00f, 1.00f, 0.50f);
		style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
		style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
		// style.Colors[ImGuiCol_CloseButton] = ImVec4(0.59f, 0.59f, 0.59f, 0.50f);
		// style.Colors[ImGuiCol_CloseButtonHovered] = ImVec4(0.98f, 0.39f, 0.36f, 1.00f);
		// style.Colors[ImGuiCol_CloseButtonActive] = ImVec4(0.98f, 0.39f, 0.36f, 1.00f);
		style.Colors[ImGuiCol_PlotLines] = ImVec4(0.39f, 0.39f, 0.39f, 1.00f);
		style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
		style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);

		if (dark)
		{
			for (int i = 0; i <= ImGuiCol_COUNT; i++)
			{
				ImVec4& col = style.Colors[i];
				float H, S, V;
				ImGui::ColorConvertRGBtoHSV(col.x, col.y, col.z, H, S, V);

				if (S < 0.1f)
				{
					V = 1.0f - V;
				}
				ImGui::ColorConvertHSVtoRGB(H, S, V, col.x, col.y, col.z);
				if (col.w < 1.00f)
				{
					col.w *= alpha_;
				}
			}
		}
		else
		{
			for (int i = 0; i <= ImGuiCol_COUNT; i++)
			{
				ImVec4& col = style.Colors[i];
				if (col.w < 1.00f)
				{
					col.x *= alpha_;
					col.y *= alpha_;
					col.z *= alpha_;
					col.w *= alpha_;
				}
			}
		}
	}
	REGISTER_IMGUI_STYLE("Doug Binks Light", []() { DougBinks(false, 0.85f); });
	REGISTER_IMGUI_STYLE("Doug Binks Dark", []() { DougBinks(true, 0.85f); });

} // namespace SptImGuiStyles

#pragma warning(pop)
