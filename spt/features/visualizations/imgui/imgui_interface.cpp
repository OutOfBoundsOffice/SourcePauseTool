#include "stdafx.hpp"

#include <set>

#include "..\feature.hpp"
#include "interfaces.hpp"
#include "spt/spt-serverplugin.hpp"
#include "spt/utils/spt_vprof.hpp"
#include "spt/utils/signals.hpp"
#include "spt/utils/game_detection.hpp"
#include "spt/features/hud.hpp"

#include "thirdparty/imgui/imgui.h"
#include "thirdparty/imgui/imgui_internal.h"
#include "thirdparty/imgui/imgui_impl_dx9.h"
#include "thirdparty/imgui/imgui_impl_win32.h"
#include "thirdparty/flatbuffers/base.h"

#include "thirdparty/x86.h"
#include "thirdparty/json_spt_include.hpp"
#include "thirdparty/curl/include/curl/curlver.h"
#include "thirdparty/xz/include/lzma.h"

#include "d3d9helper.h"

#include "imgui_interface.hpp"
#include "jetbrains_mono_bold.hpp"
#include "imgui_styles.hpp"

#define VPROF_BUDGETGROUP_IMGUI _T("SPT_ImGui")

IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct ImGuiHudCvar
{
	static inline ImGuiTextBuffer buffer;
	ConVar& var;
	uint32_t bufOffset : 31;
	uint32_t putInCollapsible : 1;
	SptImGuiHudTextCallback cb;

	ImGuiHudCvar(ConVar& var, const SptImGuiHudTextCallback& cb, bool putInCollapsible)
	    : var{var}, bufOffset{(uint32_t)buffer.size()}, cb{cb}, putInCollapsible{putInCollapsible}
	{
		const char* wrangled = WrangleLegacyCommandName(var.GetName(), true, nullptr);
		buffer.append(wrangled, wrangled + strlen(wrangled) + 1);
	}

	const bool operator<(const ImGuiHudCvar& other) const
	{
		return strcmp(buffer.Buf.Data + bufOffset, buffer.Buf.Data + other.bufOffset) < 0;
	}
};

static void* alloc_func(size_t sz, void* user_data)
{
	return g_pMemAlloc->Alloc(sz);
}

static void free_func(void* ptr, void* user_data)
{
	g_pMemAlloc->Free(ptr);
}

class SptImGuiFeature : public FeatureWrapper<SptImGuiFeature>
{
	// welcome to inline static hell

public:
	inline static bool showMainWindow = false;
	inline static bool showAboutWindow = false;
	inline static bool showExampleWindow = false;
	inline static bool forceMainWindowFocus = false;
	inline static bool drawNonRegisteredCallbacks = false;
	inline static bool doWindowCallbacks = false;
	inline static std::vector<SptImGuiWindowCallback> windowCallbacks;
	inline static std::set<ImGuiHudCvar> hudCvars;

private:
	inline static HWND gameWnd = nullptr;
	inline static WNDPROC origWndProc = nullptr;
	inline static IDirect3DDevice9* dx9Device = nullptr;
	inline static std::atomic<int> fontSize;
	inline static std::atomic<bool> recreateDeviceObjects = false;
	inline static std::atomic<bool> reloadFontSize = false;
	inline static std::atomic<bool> reloadImguiStyle = false;
	inline static std::atomic<bool> inImGuiUpdateSection = false;
	inline static std::pair<std::string, std::function<void()>> imguiStyle;

	inline static const int defaultFontSize = 18;
	inline static const int minFontSize = 8;
	inline static const int maxFontSize = 50;

	DECL_STATIC_HOOK_THISCALL(void, CShaderDeviceDx8__Present, void*);
	DECL_STATIC_HOOK_STDCALL(HCURSOR, SetCursor, HCURSOR hCursor);

	enum class LoadState
	{
		None,
		CreatedContext,
		InitWin32,
		InitDx9,
		OverrideWndProc,
		FrameSignalCalledAtLeastOnce,

		Loaded = OverrideWndProc,
	};

	inline static std::atomic<LoadState> loadState = LoadState::None;
	inline static std::recursive_mutex imguiMutex;

	static bool GameUiFocused()
	{
		// if GetFocus returns (VPANEL)0, the game is teleporting the mouse and we should not try to steal focus
		return interfaces::vgui_input->GetFocus();
	}

	static LRESULT CALLBACK CustomWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		std::scoped_lock lk{imguiMutex};

		bool forwardToGame = true;
		bool forwardToImGui = GameUiFocused();
		if (!forwardToImGui)
		{
			switch (msg)
			{
			case WM_SETFOCUS:
			case WM_KILLFOCUS:
			case WM_INPUTLANGCHANGE:
			case WM_DEVICECHANGE:
			case WM_DISPLAYCHANGE:
				// These events should always be forwarded to imgui, all other key state is cleared
				// just before NewFrame() if the game does not have any UI open.
				forwardToImGui = true;
				break;
			}
		}
		if (forwardToImGui)
		{
			auto ctx = ImGui::GetCurrentContext();
			auto& io = ctx->IO;
			/*
			* With default settings, the game & ImGui will have an epic dual because they can't
			* decide what cursor to set. This happens because:
			* 
			* - The game calls SetCursor a lot (every frame?), but ImGui only sets it when it changes.
			* 
			* - When the cursor moves, we get a WM_SETCURSOR msg which causes ImGui to reset the
			*   cursor to what it thinks it should be.
			* 
			* The solution is to not forward the WM_SETCURSOR msg to ImGui, but tell it that its
			* cursor is dirty so that when we let it capture the mouse, it'll set the cursor to
			* what it thinks is correct. ImGui will change the cursor in NewFrame if
			* bd->LastMouseCursor != ImGui::GetMouseCursor(). Since we don't have access to the
			* backend struct (slightly cringe), we set the current ctx cursor to be an invalid
			* value which will be updated by ImGui in NewFrame. We still need a hook in SetCursor
			* to prevent the cursor from flickering.
			*/
			if (msg == WM_SETCURSOR && !io.WantCaptureMouse)
				ctx->MouseCursor = ImGuiMouseCursor_None;
			else if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
				return true;

			if ((msg >= WM_KEYFIRST && msg <= WM_KEYLAST && io.WantCaptureKeyboard)
			    || (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && io.WantCaptureMouse))
				forwardToGame = false;
		}
		if (forwardToGame)
			return CallWindowProc(origWndProc, hWnd, msg, wParam, lParam);
		return true;
	}

	static void MainWindowCallback()
	{
		if (forceMainWindowFocus)
		{
			forceMainWindowFocus = false;
			showMainWindow = true;
			ImGui::SetNextWindowFocus();
		}
		else if (!showMainWindow)
		{
			return;
		}
		if (ImGui::Begin(SptImGuiGroup::Root.name, &showMainWindow, ImGuiWindowFlags_MenuBar))
		{
			if (ImGui::BeginMenuBar())
			{
				if (ImGui::BeginMenu("Menu"))
				{
					ImGui::MenuItem("About SPT", NULL, &showAboutWindow);
					ImGui::EndMenu();
				}
				ImGui::EndMenuBar();
			}
			SptImGuiGroup::Root.Draw();
		}
		ImGui::End();
	}

	static void AboutWindowCallback()
	{
		if (!showAboutWindow)
			return;
		if (ImGui::Begin("About SPT", &showAboutWindow, ImGuiWindowFlags_AlwaysAutoResize))
		{
			bool copyToClipboard = ImGui::Button("Copy to clipboard");
			if (copyToClipboard)
			{
				ImGui::LogToClipboard();
				ImGui::LogText("```\n");
			}

			ImGui::Separator();
			ImGui::Text("SPT version: %s", SPT_VERSION);
			ImGui::Text("#define __cplusplus %d", (int)__cplusplus);
#ifdef _MSC_VER
			ImGui::Text("#define _MSC_VER %d", _MSC_VER);
#endif
#ifdef _MSVC_LANG
			ImGui::Text("#define _MSVC_LANG %d", (int)_MSVC_LANG);
#endif
#ifdef __MINGW32__
			ImGui::Text("#define __MINGW32__");
#endif
#ifdef __GNUC__
			ImGui::Text("#define __GNUC__ %d", (int)__GNUC__);
#endif
#ifdef __clang_version__
			ImGui::Text("#define __clang_version__ %s", __clang_version__);
#endif
			ImGui::TextLinkOpenURL("Github##spt", "https://github.com/OutOfBoundsOffice/SourcePauseTool");
			ImGui::TextDisabled("%s", "Copyright (c) 2014-2025 SPT contributors");

			ImGui::Separator();
			ImGui::Text("SPTlib");
			ImGui::TextLinkOpenURL("Github##sptlib", "https://github.com/YaLTeR/SPTLib");
			ImGui::TextDisabled("%s", "Copyright (c) 2014-2017 Ivan Molodetskikh");

			ImGui::Separator();
			ImGui::TextUnformatted("Dear ImGui v" IMGUI_VERSION);
			auto& io = ImGui::GetIO();
#ifdef IMGUI_HAS_DOCK
			ImGui::Text("Docking branch: true, enabled: %d",
			            !!(io.ConfigFlags & ImGuiConfigFlags_DockingEnable));
#else
			ImGui::TextUnformatted("Docking branch: false");
#endif
#ifdef IMGUI_HAS_VIEWPORT
			ImGui::Text("Viewport branch: true, enabled: %d",
			            !!(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable));
#else
			ImGui::TextUnformatted("Viewport branch: false");
#endif
			ImGui::TextLinkOpenURL("Github##imgui", "https://github.com/ocornut/imgui");
			ImGui::Text(
			    "%s",
			    "To view more version info, check DEV -> ImGui Developer Settings -> Show ImGui example window.\n"
			    "Then in the demo window go to Tools -> About Dear ImGui.");
			ImGui::TextDisabled("%s", "Copyright (c) 2014-2024 Omar Cornut");

			ImGui::Separator();
			ImGui::TextUnformatted("MinHook");
			ImGui::TextLinkOpenURL("Github##minhook", "https://github.com/TsudaKageyu/minhook");
			ImGui::TextDisabled("%s", "Copyright (C) 2009-2017 Tsuda Kageyu.");

			ImGui::Separator();
			ImGui::TextUnformatted("libcurl v" LIBCURL_VERSION " (" LIBCURL_TIMESTAMP ")");
			ImGui::TextLinkOpenURL("Github##libcurl", "https://github.com/curl/curl");
			ImGui::TextDisabled("Copyright %s", LIBCURL_COPYRIGHT);

			ImGui::Separator();
			ImGui::Text("flatbuffers v%d.%d.%d",
			            FLATBUFFERS_VERSION_MAJOR,
			            FLATBUFFERS_VERSION_MINOR,
			            FLATBUFFERS_VERSION_REVISION);
			ImGui::TextLinkOpenURL("Github##flatbuffers", "https://github.com/google/flatbuffers");
			ImGui::TextDisabled("%s", "Copyright (c) Google");

			ImGui::Separator();
			ImGui::TextUnformatted("XZ Utils v" LZMA_VERSION_STRING);
			ImGui::TextLinkOpenURL("Github##xz-utils", "https://github.com/tukaani-project/xz");
			ImGui::TextDisabled("%s", "Copyright (C) The XZ Utils authors and contributors");

			ImGui::Separator();
			ImGui::Text("nlohmann/json v%d.%d.%d",
			            NLOHMANN_JSON_VERSION_MAJOR,
			            NLOHMANN_JSON_VERSION_MINOR,
			            NLOHMANN_JSON_VERSION_PATCH);
			ImGui::TextLinkOpenURL("Github##nlohmann", "https://github.com/nlohmann/json");
			ImGui::TextDisabled("Copyright (c) 2013-2019 Niels Lohmann");

			ImGui::Separator();
			ImGui::TextUnformatted("x86 opcode analyzer");
			ImGui::TextLinkOpenURL("Github##sst-x86", "https://github.com/mikesmiffy128/sst");
			ImGui::TextDisabled("%s", "Copyright (c) 2022 Michael Smith <mikesmiffy128@gmail.com>");

			ImGui::Separator();
			ImGui::TextUnformatted("Fast delegate/slots implementation");
			ImGui::TextLinkOpenURL("Github##signals", "https://github.com/pbhogan/Signals");
			ImGui::TextDisabled("%s", "Don Clugston, Mar 2004.");

			ImGui::Separator();
			ImGui::TextUnformatted("md5 hash");
			ImGui::TextLinkOpenURL("Github##md5", "https://github.com/yaoyao-cn/md5");
			ImGui::TextDisabled(
			    "%s", "Copyright (c) 1991-2, RSA Data Security, Inc. Created 1991. All rights reserved.");

			ImGui::Separator();
			ImGui::TextUnformatted("KMP searching algorithm");
			ImGui::TextLinkOpenURL("Github##kmp", "https://github.com/santazhang/kmp-cpp");
			ImGui::TextDisabled("%s", "Santa Zhang (santa1987@gmail.com)");

			if (copyToClipboard)
			{
				ImGui::LogText("\n```\n");
				ImGui::LogFinish();
			}
		}
		ImGui::End();
	}

	static void ExampleWindowCallback()
	{
		if (showExampleWindow)
			ImGui::ShowDemoWindow(&showExampleWindow);
	}

	static void DevSectionCallback()
	{
		char buf[70];
		snprintf(buf,
		         sizeof buf,
		         "Draw %d/%d non-registered callback(s)###non_registered",
		         SptImGuiGroup::Root.nLeafUserCallbacks - SptImGuiGroup::Root.nRegisteredUserCallbacks,
		         SptImGuiGroup::Root.nLeafUserCallbacks);
		ImGui::BeginDisabled(SptImGuiGroup::Root.nLeafUserCallbacks
		                     == SptImGuiGroup::Root.nRegisteredUserCallbacks);
		ImGui::Checkbox(buf, &drawNonRegisteredCallbacks);
		ImGui::SameLine();
		SptImGui::HelpMarker("%s",
		                     "If checked, renders all tabs & sections even if\n"
		                     "they don't have an associated callback. This can\n"
		                     "be used to quickly track down non-working features.\n"
		                     "I only made the colors work with the default theme.");
		ImGui::EndDisabled();
		ImGui::Checkbox("Show ImGui example window", &showExampleWindow);
	}

	static void SettingsTabCallback()
	{
		int tmpFontSize = fontSize;
		if (ImGui::SliderInt("Font size (pixels)",
		                     &tmpFontSize,
		                     minFontSize,
		                     maxFontSize,
		                     "%d",
		                     ImGuiSliderFlags_AlwaysClamp))
		{
			fontSize = tmpFontSize;
			reloadFontSize = true;
			ImGui::MarkIniSettingsDirty();
		}

		if (ImGui::BeginCombo("Style", imguiStyle.first.c_str(), ImGuiComboFlags_WidthFitPreview))
		{
			for (auto& style : SptImGuiStyles::registeredStyles)
			{
				bool selected = style.first == imguiStyle.first;
				if (ImGui::Selectable(style.first.c_str(), selected))
				{
					imguiStyle = style;
					reloadImguiStyle = true;
					ImGui::MarkIniSettingsDirty();
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		static ImGuiTextBuffer buf;
		buf.Buf.resize(0);
		buf.appendf("Clear %s###clear_ini", ImGui::GetIO().IniFilename);
		if (ImGui::Button(buf.c_str()))
			ImGui::ClearIniSettings();
	}

	static void SetupSettingsTabIniHandler()
	{
		/*
		* Handler for saving stuff in the settings tab. This API is a bit weird to use for our
		* use case. Normally ImGui uses it to store e.g. window data for lines that look like
		* [Window][Name of the window]. The code that reads the .ini file assumes that entries
		* will always have user data, which isn't really the case here. Maybe it'll make more
		* sense if I save more stuff than just this tab.
		*/
		ImGuiSettingsHandler handler{};
		handler.TypeName = "SPT";
		handler.TypeHash = ImHashStr(handler.TypeName);

		handler.ClearAllFn = [](ImGuiContext* ctx, ImGuiSettingsHandler* handler) { SetDefaultSettings(); };

		handler.ReadOpenFn = [](ImGuiContext* ctx, ImGuiSettingsHandler* handler, const char* name)
		{ return (void*)0x69420; };

		handler.ReadLineFn = [](ImGuiContext* ctx, ImGuiSettingsHandler* handler, void* entry, const char* line)
		{
			int tmpFontSize;
			if (sscanf(line, "FontSize=%d", &tmpFontSize) == 1)
			{
				fontSize = tmpFontSize;
				reloadFontSize = true;
				return;
			}
			{
				const char prefix[] = "Style=";
				if (!strncmp(line, prefix, sizeof(prefix) - 1))
				{
					const char* loadStyleName = line + sizeof(prefix) - 1;
					auto it = SptImGuiStyles::registeredStyles.find(loadStyleName);
					if (it == SptImGuiStyles::registeredStyles.end())
					{
						imguiStyle = SptImGuiStyles::defaultStyle;
						ImGui::MarkIniSettingsDirty();
						Warning(
						    "SPT: ImGui style \"%s\" not registered, reverting to \"%s\".\n",
						    loadStyleName,
						    imguiStyle.first.c_str());
					}
					else
					{
						imguiStyle = *it;
					}
					reloadImguiStyle = true;
					return;
				}
			}
		};

		handler.WriteAllFn = [](ImGuiContext* ctx, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out_buf)
		{
			out_buf->appendf("[SPT][Main Window Settings]\n", handler->TypeName);
			out_buf->appendf("FontSize=%d\n", fontSize.load());
			out_buf->appendf("Style=%s", imguiStyle.first.c_str());
			out_buf->append("\n");
		};

		ImGui::AddSettingsHandler(&handler);
	}

#ifdef SPT_HUD_ENABLED
	static void TextHudTabCallback()
	{
		// Maybe this callback should be in the HUD feature file? Something to think about..

		extern ConVar y_spt_hud;
		extern ConVar y_spt_hud_left;

		ImGui::BeginDisabled(!SptImGui::CvarCheckbox(y_spt_hud, "enable text HUD"));
		SptImGui::CvarCheckbox(y_spt_hud_left, "left HUD");
		ImGui::Separator();

		int i = 0;
		for (auto& cvarInfo : hudCvars)
		{
			ImGui::PushID(i);
			Assert(cvarInfo.cb);
			const char* name = ImGuiHudCvar::buffer.Buf.Data + cvarInfo.bufOffset;
			if (cvarInfo.putInCollapsible)
			{
				float oldIndent = *(float*)ImGui::GetStyleVarInfo(ImGuiStyleVar_IndentSpacing)
				                       ->GetVarPtr(&ImGui::GetStyle());
				ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 0.0f);
				if (ImGui::TreeNodeEx(name, ImGuiTreeNodeFlags_Framed))
				{
					ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, oldIndent);
					if (SptImGui::BeginBordered())
					{
						cvarInfo.cb(cvarInfo.var);
						SptImGui::EndBordered();
					}
					ImGui::PopStyleVar();
					ImGui::TreePop();
				}
				ImGui::PopStyleVar();
			}
			else
			{
				cvarInfo.cb(cvarInfo.var);
			}
			ImGui::PopID();
			i++;
		}

		ImGui::EndDisabled();
	}
#endif

	static void ReloadFontSize()
	{
		int tmpFontSize = fontSize;
		fontSize = clamp(tmpFontSize, minFontSize, maxFontSize);
		auto& io = ImGui::GetIO();
		io.Fonts->Clear();
		ImFontConfig fontCfg{};
		strncpy(fontCfg.Name, "JetBrainsMono-Bold.ttf", sizeof fontCfg.Name);
		io.Fonts->AddFontFromMemoryCompressedTTF(JetBrainsMono_Bold_compressed_data,
		                                         JetBrainsMono_Bold_compressed_data_size,
		                                         fontSize,
		                                         &fontCfg,
		                                         io.Fonts->GetGlyphRangesDefault());
		io.Fonts->Build();
		recreateDeviceObjects = true;
		reloadFontSize = false;
	}

	static void ReloadStyle()
	{
		Assert(imguiStyle.second);
		imguiStyle.second();
		reloadImguiStyle = false;
	}

	static void SetDefaultSettings()
	{
		fontSize = defaultFontSize;
		reloadFontSize = true;
		Assert(SptImGuiStyles::defaultStyle.second);
		imguiStyle = SptImGuiStyles::defaultStyle;
		reloadImguiStyle = true;
	}

	static void OnFrameSignal()
	{
		std::scoped_lock lk{imguiMutex};

		SPT_VPROF_BUDGET(__FUNCTION__, VPROF_BUDGETGROUP_IMGUI);

		inImGuiUpdateSection = true;

		/*
		* The window callbacks used to be in the Present function. Since that ran on a different
		* thread, we had some issues with race conditions (mostly just misbehaving asserts). It
		* wasn't disastrous but it was extremely annoying, and more issues kept popping up as the
		* window callbacks got more complicated.
		* 
		* The hope is that we can solve these issues by moving the window callback logic to the
		* game's main thread here in frame signal. However, this means that our ImGui::NewFrame()
		* is no longer synced up with ImGui::Render(). This is mostly fine, but it does mean that
		* e.g. an alt-tab may make the gui disappear until framesignal is called again, and we
		* don't update the ImGui windows during e.g. loads.
		*/

		if (reloadFontSize)
			ReloadFontSize();
		if (reloadImguiStyle)
			ReloadStyle();

		if (recreateDeviceObjects)
		{
			ImGui_ImplDX9_InvalidateDeviceObjects();
			ImGui_ImplDX9_CreateDeviceObjects();
			recreateDeviceObjects = false;
		}

		if (!GameUiFocused())
		{
			/*
			* In our WndProc, we currently don't forward events to ImGui if there's no game UI
			* focused. This means ImGui doesn't get any key up/down events if we're playing the
			* game, so keys can get stuck (according to ImGui). Forwarding only key up events
			* doesn't work perfectly since ImGui explicitly calls ::GetKeyState() sometimes.
			* Clearing the key state explicitly seems to do the trick.
			*/
			ImGui_ImplWin32_WndProcHandler(gameWnd, WM_MOUSELEAVE, 0, 0);
			ImGui::GetIO().ClearInputKeys();
			ImGui::GetIO().ClearInputMouse();
		}

		ImGui_ImplDX9_NewFrame();
		ImGui_ImplWin32_NewFrame();
#ifdef DEBUG
		ImGui::GetIO().ConfigDebugIsDebuggerPresent = IsDebuggerPresent();
#endif
		ImGui::NewFrame();

		if (doWindowCallbacks)
			for (auto& cb : windowCallbacks)
				cb();

		/*
		* Normally ImGui doesn't require us to explicitly call EndFrame() because Render() will
		* call it. It is necessary in our case because ImGui has some sanity checks that check if
		* EndFrame() is called before the next NewFrame().
		*/
		ImGui::EndFrame();

		loadState = LoadState::FrameSignalCalledAtLeastOnce;
		inImGuiUpdateSection = false;
	}

protected:
	virtual bool ShouldLoadFeature()
	{
		return interfaces::shaderDevice && interfaces::vgui_input && TickSignal.Works;
	}

	virtual void InitHooks()
	{
		/*
		* We need:
		* - a hook for CShaderDeviceDx8::Present()
		* - the IDirect3DDevice9 to give to ImGui
		* 
		* Conveniently, the Present() function references the device, so we first find the
		* Present() function in the device wrapper that the SDK exposes, then look for the
		* device in it.
		*/

		void* moduleBase;
		size_t moduleSize;
		MemUtils::GetModuleInfo(L"shaderapidx9.dll", nullptr, &moduleBase, &moduleSize);

		const int maxInstrSearch = 64;
		const int maxVtableSearch = 16;
		IShaderDevice* deviceWrapper = interfaces::shaderDevice;
		if (!utils::DataInModule(deviceWrapper, maxVtableSearch * sizeof(void*), moduleBase, moduleSize))
			return;

		dx9Device = nullptr;
		int vOff = -1;
		while (!dx9Device && ++vOff < maxVtableSearch)
		{
			void** pPresentFunc = ((void***)deviceWrapper)[0] + vOff;
			if (!utils::DataInModule(pPresentFunc, maxInstrSearch, moduleBase, moduleSize))
				continue;

			/*
			* CShaderDeviceDx8::Present() does two virtual calls prior to calling Dx9Device().
			* Dx9Device() may or may not be inlined, so follow a single call if necessary. Once
			* we see the two virtual calls, we expect something of the form `MOV REG, ADDR` to
			* tell us the address of the IDirect3DDevice9.
			*/

			uint8_t* func = (uint8_t*)*pPresentFunc;
			int len = 0;
			int nVirtualCalls = 0;

			for (uint8_t* addr = func; len != -1 && addr - func < maxInstrSearch && nVirtualCalls <= 2;
			     len = x86_len(addr), addr += len)
			{
				if (addr[0] == X86_MISCMW && (addr[1] & 0b10111000) == 0b10010000)
					nVirtualCalls++; // CALL REG | CALL [REG + OFF]
				else if (nVirtualCalls == 2 && addr[0] == X86_CALL)
					func = addr = addr + 5 + *(uint32_t*)(addr + 1); // CALL OFF
				else if (addr[0] == X86_INT3)
					break;
				if (nVirtualCalls == 2
				    && ((addr[0] & 0b11111000) == 0b10111000 || (addr[0] & 0b11111000) == 0b10100000))
				{
					// MOV REG ADDR | MOV REG [ADDR]
					IDirect3DDevice9** ppDevice = *(IDirect3DDevice9***)(addr + 1);
					if (!utils::DataInModule(ppDevice, sizeof *ppDevice, moduleBase, moduleSize))
						continue;
					dx9Device = *ppDevice;
					break;
				}
			}
		}
		if (!dx9Device)
			return;

		D3DDEVICE_CREATION_PARAMETERS params;
		if (dx9Device->GetCreationParameters(&params) != D3D_OK)
			return;
		gameWnd = params.hFocusWindow;

		AddVFTableHook(
		    VFTableHook{
		        *(void***)deviceWrapper,
		        vOff,
		        (void*)HOOKED_CShaderDeviceDx8__Present,
		        (void**)&ORIG_CShaderDeviceDx8__Present,
		    },
		    "shaderapidx9");

		if (!IMGUI_CHECKVERSION())
			return;

		ImGui::SetAllocatorFunctions(alloc_func, free_func, NULL);
		if (!ImGui::CreateContext())
			return;

		ImGuiIO& io = ImGui::GetIO();
		// gamepad inputs untested
		// io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		/*
		* For now, I'm leaving the viewports feature disabled for two reasons:
		* - RInput.dll hooks ::GetCursorPos() and that kills UI interactions for non-main viewports
		* - resizing the game application crashes imgui
		* 
		* Both of these should be fixable, but I don't want to figure it out right now.
		*/
		// io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
		io.ConfigDebugIniSettings = true;

		loadState = LoadState::CreatedContext;
		if (!ImGui_ImplWin32_Init(gameWnd))
			return;
		loadState = LoadState::InitWin32;
		if (!ImGui_ImplDX9_Init(dx9Device))
			return;
		loadState = LoadState::InitDx9;
		Assert(!origWndProc);
		origWndProc = (WNDPROC)SetWindowLongPtr(gameWnd, GWLP_WNDPROC, (LONG_PTR)CustomWndProc);
		if (!origWndProc)
			return;
		loadState = LoadState::OverrideWndProc;

		ORIG_SetCursor = ::SetCursor;
		ADD_RAW_HOOK(User32, SetCursor);

		DevMsg("Initialized Dear ImGui.\n");

		// these settings should be loaded from the .ini file by imgui anyways, but idk if that's guaranteed
		SetDefaultSettings();
	};

	virtual void LoadFeature()
	{
		if (!Loaded())
			return;
		InitConcommandBase(spt_gui);
		SptImGui::RegisterWindowCallback(MainWindowCallback);
		SptImGui::RegisterWindowCallback(AboutWindowCallback);
		SptImGui::RegisterWindowCallback(ExampleWindowCallback);
		SptImGuiGroup::Dev_ImGui.RegisterUserCallback(DevSectionCallback);
		SptImGuiGroup::Settings.RegisterUserCallback(SettingsTabCallback);
		SetupSettingsTabIniHandler();
#ifdef SPT_HUD_ENABLED
		if (spt_hud_feat.LoadingSuccessful())
			SptImGuiGroup::Hud_TextHud.RegisterUserCallback(TextHudTabCallback);
#endif
		FrameSignal.Connect(OnFrameSignal);
	};

	virtual void UnloadFeature()
	{
#pragma warning(push)
#pragma warning(disable : 26819)
		// cleanup state in case of tas_restart
		switch (loadState)
		{
		case LoadState::FrameSignalCalledAtLeastOnce:
		case LoadState::OverrideWndProc:
			SetWindowLongPtr(gameWnd, GWLP_WNDPROC, (LONG_PTR)origWndProc);
		case LoadState::InitDx9:
			ImGui_ImplDX9_Shutdown();
		case LoadState::InitWin32:
			ImGui_ImplWin32_Shutdown();
		case LoadState::CreatedContext:
			ImGui::DestroyContext();
		case LoadState::None:
		default:
			ImGuiHudCvar::buffer.Buf.clear();
			windowCallbacks.clear();
			origWndProc = nullptr;
			dx9Device = nullptr;
			gameWnd = nullptr;
			hudCvars.clear();
			loadState = LoadState::None;
			SptImGuiGroup::Root.ClearCallbacksRecursive();
			inImGuiUpdateSection = false;
			break;
		}
#pragma warning(pop)
	};

public:
	static bool Loaded()
	{
		return loadState >= LoadState::Loaded;
	}
} static spt_imgui_feat;

IMPL_HOOK_THISCALL(SptImGuiFeature, void, CShaderDeviceDx8__Present, void*)
{
	std::scoped_lock lk{CSourcePauseTool::unloadMutex, imguiMutex};

	if (loadState >= LoadState::FrameSignalCalledAtLeastOnce && !recreateDeviceObjects)
	{
		SPT_VPROF_BUDGET(__FUNCTION__, VPROF_BUDGETGROUP_IMGUI);

		/*
		* I'm not a dx9 expert! This logic may be extremely borked, but it seems to work. My
		* understanding is that when the app window changes size or becomes maximized, the dx9
		* device needs to be reset. CShaderDeviceDx8::Present() calls:
		* 1) IDirect3DDevice9::EndScene()
		* 2) CShaderDeviceDx8::CheckDeviceLost(bool)  (this calls IDirect3DDevice9::TestCooperativeLevel)
		* 3) IDirect3DDevice9::Present()
		* 
		* This is perfect since we can check the cooperative level ourselves and that will tell us
		* if the game is about to reset the device. See:
		* https://github.com/ocornut/imgui/blob/521f84a3a9f21aa93d12a49823d589b560f899a5/examples/example_win32_directx9/main.cpp#L105
		* In the ImGui example, they call IDirect3DDevice9::Reset() before calling
		* ImGui_ImplDX9_CreateDeviceObjects(). Doing it the other way around seems to work just as
		* well most of the time, although occasionally it still breaks and I don't know why :(.
		* Maybe there's some race condition that I'm unaware of?
		*/
		HRESULT hr = dx9Device->TestCooperativeLevel();
		switch (hr)
		{
		case D3DERR_DEVICELOST:
		case D3DERR_DEVICENOTRESET:
			recreateDeviceObjects = true;
			loadState = LoadState::Loaded; // wait until framesignal is called again
			break;
		case D3D_OK:
			if (doWindowCallbacks)
			{
				ImGui::Render();
				ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
				if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
				{
					ImGui::UpdatePlatformWindows();
					ImGui::RenderPlatformWindowsDefault();
				}
			}
			break;
		case D3DERR_DRIVERINTERNALERROR:
		default:
			AssertMsg1(0, "Bad result from IDirect3DDevice9::TestCooperativeLevel(): %ld", hr);
			break;
		}
	}
	ORIG_CShaderDeviceDx8__Present(thisptr);
}

IMPL_HOOK_STDCALL(SptImGuiFeature, HCURSOR, SetCursor, HCURSOR hCursor)
{
	// imgui wants to capture mouse -> let imgui set cursor, otherwise let the game do it
	if (!inImGuiUpdateSection ^ ImGui::GetIO().WantCaptureMouse)
		return ORIG_SetCursor(hCursor);
	return NULL;
}

namespace SptImGuiGroup
{
	static inline bool initializingTabs = false;
	static inline const char* outOfPlaceCtorMsg = "spt: all tabs/sections should be initialized together!";

	Section::Section(const char* name, Tab* parent) : name{name}, parent{parent}
	{
		Assert(name);
		AssertMsg(parent, "spt: every section should be in a tab!");
		AssertMsg(initializingTabs, outOfPlaceCtorMsg);
		AssertMsg(parent->childTabs.empty(), "spt: cannot have child tabs & sections in a single tab!");
		if (!parent->childSections.empty())
			for (Tab* t = parent; t; t = t->parent)
				t->nLeafUserCallbacks++;
		parent->childSections.push_back(this);
	}

	bool Section::RegisterUserCallback(const SptImGuiSectionCallback& cb)
	{
		if (!spt_imgui_feat.Loaded())
			return false;
		Assert(cb);
		if (userCb)
		{
			AssertMsg(0, "spt: section already has callback!");
			return false;
		}
		userCb = cb;
		for (Tab* t = parent; t; t = t->parent)
			t->nRegisteredUserCallbacks++;
		return true;
	}

	Tab::Tab(const char* name, Tab* parent) : name{name}, parent{parent}
	{
		if (!parent)
		{
			if (this == &Root)
			{
				AssertMsg(!initializingTabs, outOfPlaceCtorMsg);
				initializingTabs = true;
				nLeafUserCallbacks = 1;
			}
			else if (this == &_DummyLast)
			{
				AssertMsg(initializingTabs, outOfPlaceCtorMsg);
				initializingTabs = false;
			}
			else
			{
				AssertMsg(0, "spt: every tab should have a parent!");
			}
		}
		else if (!initializingTabs)
		{
			AssertMsg(0, outOfPlaceCtorMsg);
		}
		else
		{
			Assert(name);
			AssertMsg(parent->childSections.empty(),
			          "spt: cannot have child tabs & sections in a single tab!");
			nLeafUserCallbacks = 1;
			if (!parent->childTabs.empty())
				for (Tab* t = parent; t; t = t->parent)
					t->nLeafUserCallbacks++;
			parent->childTabs.push_back(this);
		}
	}

	bool Tab::RegisterUserCallback(const SptImGuiTabCallback& cb)
	{
		if (!spt_imgui_feat.Loaded())
			return false;
		Assert(cb);
		if (userCb)
		{
			AssertMsg(0, "spt: tab already has callback!");
			return false;
		}
		else if (!childTabs.empty() || !childSections.empty())
		{
			AssertMsg(0, "spt: no user callback allowed (tab has child tabs or sections)!");
			return false;
		}
		userCb = cb;
		for (Tab* t = this; t; t = t->parent)
			t->nRegisteredUserCallbacks++;
		return true;
	}

	void Tab::ClearCallbacksRecursive()
	{
		userCb = nullptr;
		nRegisteredUserCallbacks = 0;
		for (Section* s : childSections)
			s->userCb = nullptr;
		for (Tab* t : childTabs)
			t->ClearCallbacksRecursive();
	}

	void Tab::Draw() const
	{
		if (userCb)
			userCb();
		else if (!childTabs.empty() || !childSections.empty())
			DrawChildrenRecursive();
	}

	/*
	* This is the implementation of the render callback of a tab which doesn't have a user defined callback.
	* - if there are child tabs, make a new tab bar and draw the child tabs recursively
	* - if there are child sections, draw each section and call the appropritate callback
	* 
	* If drawNonRegisteredCallbacks is set, then tabs & sections w/o user defined callbacks will be drawn
	* and an appropriate message will be displayed.
	*/
	void Tab::DrawChildrenRecursive() const
	{
		AssertMsg(childTabs.empty() || childSections.empty(),
		          "spt: child tabs or sections expected, not both!");

		if (!childTabs.empty())
		{
			ImGuiTabBarFlags tabBarFlags = ImGuiTabBarFlags_FittingPolicyScroll
			                               | ImGuiTabBarFlags_TabListPopupButton
			                               | ImGuiTabBarFlags_DrawSelectedOverline;
			if (!ImGui::BeginTabBar("[tab bar]", tabBarFlags))
				return;
			for (size_t i = 0; i < childTabs.size(); i++)
			{
				const Tab* child = childTabs[i];
				bool anyRegistered = child->nRegisteredUserCallbacks;
				bool allRegistered = child->nRegisteredUserCallbacks == child->nLeafUserCallbacks;
				if (!anyRegistered && !SptImGuiFeature::drawNonRegisteredCallbacks)
					continue;
				// Create new ID scope to allow multiple tabs with the same name.
				// Make sure to push the actual index of the tab item INCLUDING the
				// disabled tabs. This ensures that the current selected tab will not
				// spontaneously switch if a new tab is drawn/registered.
				ImGui::PushID(i);
				// Unfortunately this will also color the text in the tab list dropdown
				// of the tab bar if this is the first tab item. This might be a minor
				// bug in imgui but this isn't meant to be super pretty anyways.
				if (SptImGuiFeature::drawNonRegisteredCallbacks)
				{
					if (!anyRegistered)
						ImGui::PushStyleColor(ImGuiCol_Text, SPT_IMGUI_WARN_COLOR_RED);
					else if (!allRegistered)
						ImGui::PushStyleColor(ImGuiCol_Text, SPT_IMGUI_WARN_COLOR_YELLOW);
				}
				// don't use ImGuiTabItemFlags_NoPushId - that breaks if there's an item with the same name as the tab item
				if (ImGui::BeginTabItem(child->name))
				{
					if (!anyRegistered)
						ImGui::Text("This tab doesn't have an associated callback!");
					if (!allRegistered && SptImGuiFeature::drawNonRegisteredCallbacks)
						ImGui::PopStyleColor();
					// keep the tab bar at the top of the window
					if (ImGui::BeginChild("##tab"))
						child->Draw();
					ImGui::EndChild();
					ImGui::EndTabItem();
				}
				else if (!allRegistered && SptImGuiFeature::drawNonRegisteredCallbacks)
				{
					ImGui::PopStyleColor();
				}
				ImGui::PopID();
			}
			ImGui::EndTabBar();
		}
		else
		{
			for (size_t i = 0; i < childSections.size(); i++)
			{
				const Section* section = childSections[i];
				if (!section->userCb && !SptImGuiFeature::drawNonRegisteredCallbacks)
					continue;
				if (section->userCb)
				{
					ImGui::SeparatorText(section->name);
					// same logic here - push the index from within the entire section list
					ImGui::PushID(i);
					section->userCb();
					ImGui::PopID();
				}
				else
				{
					ImGui::PushStyleColor(ImGuiCol_Text, SPT_IMGUI_WARN_COLOR_RED);
					ImGui::PushStyleColor(ImGuiCol_Separator, SPT_IMGUI_WARN_COLOR_RED);
					ImGui::SeparatorText(section->name);
					ImGui::Text("This section doesn't have an associated callback!");
					ImGui::PopStyleColor(2);
				}
			}
		}
	}
} // namespace SptImGuiGroup

bool SptImGui::Loaded()
{
	return SptImGuiFeature::Loaded();
}

bool SptImGui::RegisterWindowCallback(const SptImGuiWindowCallback& cb)
{
	if (!Loaded())
		return false;
	Assert(cb);
	SptImGuiFeature::windowCallbacks.push_back(cb);
	return true;
}

void SptImGui::RegisterHudCvarCheckbox(ConVar& var)
{
	RegisterHudCvarCallback(var, [](ConVar& cv) { SptImGui::CvarCheckbox(cv, "##checkbox"); }, false);
}

void SptImGui::RegisterHudCvarCallback(ConVar& var, const SptImGuiHudTextCallback& cb, bool putInCollapsible)
{
	if (!Loaded())
		return;
	Assert(cb);
	auto emp = SptImGuiFeature::hudCvars.emplace(var, cb, putInCollapsible);
	AssertMsg(emp.second, "each hud cvar should not be registered more than once to imgui");
}

void SptImGui::BringFocusToMainWindow()
{
	SptImGuiFeature::forceMainWindowFocus = true;
}

ImGuiWindow* SptImGui::GetMainWindow()
{
	if (!Loaded())
		return nullptr;
	return ImGui::FindWindowByName(SptImGuiGroup::Root.name);
}

void SptImGui::ToggleFeature()
{
	if (!Loaded())
		return;
	bool anyActiveWindows = std::any_of(ImGui::GetCurrentContext()->Windows.begin(),
	                                    ImGui::GetCurrentContext()->Windows.end(),
	                                    [](const ImGuiWindow* wnd) { return wnd->Active && !wnd->Hidden; });
	if (SptImGuiFeature::doWindowCallbacks && anyActiveWindows)
	{
		SptImGuiFeature::doWindowCallbacks = false;
	}
	else
	{
		SptImGuiFeature::doWindowCallbacks = true;
		if (!SptImGuiFeature::showMainWindow)
			BringFocusToMainWindow();
	}
}
