#pragma once

#include "thirdparty/imgui/imgui.h"
#include "spt/cvars.hpp"
#include "spt/utils/ent_list.hpp"
#include "spt/utils/portal_utils.hpp"
#include "thirdparty/fonts/codicon/codepoints/IconsCodicons.h"

class SptImGuiFeature;
class SptImGui;
struct ImGuiWindow;

#define SPT_IMGUI_WARN_COLOR_YELLOW (ImVec4{.8f, .5f, .1f, 1})
#define SPT_IMGUI_WARN_COLOR_RED (ImVec4{.9f, .05f, .05f, 1})

/*
* This header is for registering features to display stuff with Dear ImGui. Some
* features are nice to have a GUI for, some might only be nice to use through
* the console, and some could be a hybrid mix of both. Usually, I think it would
* be ideal for most features to be entirely usable through the console, and
* optionally have a GUI for *most* of its functionality. This is to allow users
* to set up binds for their favorite features! If a feature has some parts of it
* available through the console AND the GUI, please let the user know somehow
* (e.g. if you can change the value of a cvar with a checkbox or a text field in
* ImGui, show the specific cvar that is being set in the GUI with at least a
* tooltip or something).
* 
* To register a feature to draw something with Dear ImGui, use one of the 'register'
* functions below. To simplify the look of the GUI, I think that most of the
* functionality of SPT's features should fit within a single main window. I've tried
* to roughly organize SPT's features into categories and subcategories corresponding
* to tabs and subtabs in what I'm calling the "main" window. UI design is one of my
* "top 10 things I probably don't want to do for a living", so there might be a
* better way of organizing everything. This approach is pretty simple and scalable.
* 
* Each feature can:
* - live in a single tab/subtab in the main window
* - live in a single section within a tab
* - live in a separate window
* - a combination of the above - features are allowed to live across several
*   tabs in the main window, but that requires the use of several callbacks
* 
* Some important things to keep in mind:
* 
* - Don't reinterpret cast from color32 to ImU32! Use the conversion functions
*   below or better yet, use ImVec4 directly.
* 
* - Please save yourself some trouble and read about the ImGui stack system:
*   https://github.com/ocornut/imgui/blob/master/docs/FAQ.md#q-about-the-id-stack-system
*   Go to DEV -> "Show ImGui example window", then Tools -> ID Stack Tool. This
*   is extremely helpful for determining why widgets don't react to input
*   events sometimes. All tabs/sections have their own ID scope.
*
* - Read about other debug tools: https://github.com/ocornut/imgui/wiki/Debug-Tools.
* 
* - The ImGui example window is your best friend for learning how to write your
*   own ImGui code. See: SPT -> DEV -> "Show ImGui example window". Look
*   through it to see if the thing you're trying to do is already in there,
*   then look for the code in imgui_demo.cpp.
* 
* - For displaying ConVars, I think a good approach is to let any value set
*   through the console take priority over any value set through the GUI.
*   Widgets like e.g. DragFloat() work nicely with this because they won't
*   override the value that you passed in unless the user starts dragging the
*   widget. For example if you want a listbox with 4 choices from a cvar, a
*   good approach would be to use var.GetInt() and clamp the value to [0, 3],
*   then only update the cvar if the user selects something in the widget.
* 
* - Don't rely on the imgui callbacks to update feature internals if you want
*   to check for something regularly, use e.g. SV_FrameSignal. The ImGui
*   callbacks are not guaranteed to be called every frame.
* 
* - With the current implementation of input events, ImGui will not see key &
*   mouse events when there is no game UI open.
*/

using SptImGuiTabCallback = std::function<void()>;
using SptImGuiSectionCallback = std::function<void()>;
using SptImGuiWindowCallback = std::function<void()>;
using SptImGuiHudTextCallback = std::function<void(ConVar& var)>;

/*
* This describes all tabs/sections in the main ImGui window. This approach of listing
* all the tabs in one place may seem a bit weird at first, but it makes it pretty
* easy to see the general UI layout at a glance (unlike spt_hud's approach of using a
* sort key). If you want to add your own section/tab, add it below! Do not make your
* own instances of the Section/Tab objects - the constructors here rely on static
* initialization order to determine the order of the tabs/subtabs/sections. The root
* tab should be first, and the dummy tab should be last. Try to order everything by
* most commonly used features first.
* 
* If a section/tab does not have a user defined callback, it will not be rendered!
* This applies recursively (if a tab has many child tabs/sections, and none of those
* children have callbacks, the parent tab won't be rendered). This is to prevent a
* bunch of empty sections/tabs from showing up when features fail to load. It also
* makes it really easy to see at a glance which features are supported in the current
* SPT/game version. In some cases, it can be easier to disable a section of the GUI
* instead of preventing it from being drawn if e.g. a cvar isn't initialized. See the
* BeginCmdGroup()/EndCmdGroup() functions below.
* 
* On the contrary, if you've registered a callback, it will always be rendered even
* if you don't draw anything. This means you should always draw something! If your
* feature is disabled because of game/version incompatibilities, don't register the
* callback. If your feature is disabled because of some other runtime condition
* (e.g. player has no portal gun or server is not active), draw the full UI of the
* feature within an ImGui::BeginDisabled block (and preferably let the user know why
* the feature is disabled in the UI).
* 
* Tab::RegisterUserCallback() & Section::RegisterUserCallback() return true if the
* internal feature is loaded. If the return value is false, the callback will *not*
* be called. If you want to display a separate window for your feature, use
* RegisterWindowCallback().
* 
* Each leaf section/tab should have at most a single callback. Section/tab
* callbacks are allowed to make their own windows, but keep in mind that those
* windows won't show when the tab is closed. It's recommended to register a
* separate window callback for separate windows.
*/
namespace SptImGuiGroup
{
	class Tab
	{
	private:
		friend class Section;
		friend class SptImGuiFeature;
		friend class SptImGui;
		const char* name;
		Tab* parent;
		SptImGuiTabCallback userCb;
		// These counts are recursive (including self). Leaf count is calculated in the constructor
		// during static initialization; the registered count is updated during runtime.
		int nLeafUserCallbacks;
		int nRegisteredUserCallbacks;
		std::vector<class Section*> childSections;
		std::vector<Tab*> childTabs;

	public:
		Tab() = delete;
		Tab(Tab&) = delete;
		Tab(const char* name, Tab* parent);
		// call from PreHook() or later!
		bool RegisterUserCallback(const SptImGuiTabCallback& cb);

	private:
		void Draw() const;
		void DrawChildrenRecursive() const;
		void ClearCallbacksRecursive();
	};

	// a section is a part of a tab used for simple features separated with ImGui::SeparatorText()
	class Section
	{
	private:
		friend class Tab;
		friend class SptImGuiFeature;
		friend class SptImGui;
		SptImGuiSectionCallback userCb;
		const char* name;
		Tab* parent;

	public:
		Section() = delete;
		Section(Section&) = delete;
		Section(const char* name, Tab* parent);
		// call from PreHook() or later!
		bool RegisterUserCallback(const SptImGuiSectionCallback& cb);
	};

	// dummy tab, should be first - has the callback for rendering the window
	inline Tab Root{"SPT", nullptr};

	// render overlay
	inline Tab Overlay{ICON_CI_MULTIPLE_WINDOWS " Overlay", &Root};

	// drawing visualizations
	inline Tab Draw{ICON_CI_SYMBOL_COLOR " Drawing", &Root};
	inline Tab Draw_Collides{"Collision", &Draw};
	inline Tab Draw_Collides_World{"World collides", &Draw_Collides};
	inline Tab Draw_Collides_Ents{"Entity collides", &Draw_Collides};
	inline Tab Draw_Collides_PortalEnv{"Portal environment collision", &Draw_Collides};
	inline Tab Draw_MapOverlay{"Map overlay", &Draw};
	inline Tab Draw_PpPlacement{"Portal placement", &Draw};
	inline Section Draw_PpPlacement_Gun{"Gun portal placement", &Draw_PpPlacement};
	inline Section Draw_PpPlacement_Grid{"Portal placement grid", &Draw_PpPlacement};
	inline Tab Draw_VagTrace{"VAG trace", &Draw};
	inline Tab Draw_Lines{"Draw lines", &Draw};
	inline Tab Draw_Misc{"Misc.", &Draw};
	inline Section Draw_Misc_OobEnts{"OOB entities", &Draw_Misc};
	inline Section Draw_Misc_Seams{"Seamshots", &Draw_Misc};
	inline Section Draw_Misc_LeafVis{"Leaf vis", &Draw_Misc};

	// player trace
	inline Tab PlayerTrace{ICON_CI_ISSUE_REOPENED " Player trace", &Root};
	inline Tab PlayerTrace_Player{ICON_CI_PERSON " Player data", &PlayerTrace};
	inline Tab PlayerTrace_Entities{ICON_CI_PACKAGE " Active entities", &PlayerTrace};
	inline Tab PlayerTrace_Portals{ICON_CI_CIRCLE_LARGE " Active portals", &PlayerTrace};

	// quality of life and/or purely visual stuff
	inline Tab QoL{ICON_CI_SMILEY " QoL", &Root};
	inline Section QoL_Demo{"Demo utils", &QoL};
	inline Section QoL_MultiInstance{"Multiple game instances", &QoL};
	inline Section QoL_Noclip{"Noclip", &QoL}; // QoL instead of in cheats cuz noclip is cheating anyways :)
	inline Section QoL_NoSleep{"No sleep", &QoL};
	inline Section QoL_Visual{"Visual fixes", &QoL};
	inline Section QoL_ConNotify{"Console notify", &QoL};
	inline Section QoL_FastLoads{"Fast loads", &QoL};
	inline Section QoL_Timer{"Timer", &QoL};

	// cheats - stuff that changes gameplay or cannot be done via normal means
	inline Tab Cheats{ICON_CI_SHIELD " Cheats", &Root};
	inline Tab Cheats_PortalSpecific{"Portal specific", &Cheats};
	inline Section Cheats_PortalSpecific_SetSg{"Set SG", &Cheats_PortalSpecific};
	inline Section Cheats_PortalSpecific_VagCrash{"Prevent VAG crash", &Cheats_PortalSpecific};
	inline Section Cheats_PortalSpecific_VagSearch{"VAG search", &Cheats_PortalSpecific};
	inline Tab Cheats_Misc{"Misc.", &Cheats};
	inline Section Cheats_Misc_Jumping{"Jumping", &Cheats_Misc};
	inline Section Cheats_Misc_HL2AirControl{"HL2 air control", &Cheats_Misc};
	inline Section Cheats_Misc_ISG{"ISG", &Cheats_Misc};
	inline Section Cheats_Misc_SnapshotOverflow{"Snapshot overflow fix", &Cheats_Misc};
	inline Section Cheats_Misc_PlayerShadow{"Player shadow", &Cheats_Misc};
	inline Section Cheats_Misc_Pause{"Pause on load", &Cheats_Misc};
	inline Section Cheats_Misc_FreeOob{"Free OOB", &Cheats_Misc};
	inline Section Cheats_Misc_PlayerCollisionGroup{"Player collision group", &Cheats_Misc};
	inline Section Cheats_Misc_MaxSpeed{"Max speed", &Cheats_Misc};
	inline Section Cheats_Misc_Tickrate{"Tickrate", &Cheats_Misc};

	// spt_hud and friends
	inline Tab Hud{ICON_CI_SYMBOL_KEY " HUD", &Root};
	inline Tab Hud_TextHud{"Text HUD", &Hud}; // use the RegisterHudCvarXXX functions below to add cvars here
	inline Tab Hud_IHud{"Input HUD", &Hud};
	inline Tab Hud_JHud{"Jump HUD", &Hud};
	inline Tab Hud_StrafeHud{"Strafe HUD", &Hud};

	// imgui settings
	inline Tab Settings{ICON_CI_GEAR " Settings", &Root};

	// development/debugging features
	inline Tab Dev{ICON_CI_BUG " DEV", &Root};
	inline Section Dev_GameDetection{"Game detection", &Dev};
	inline Section Dev_Mesh{"Meshes", &Dev};
	inline Section Dev_ImGui{"ImGui developer settings", &Dev};

	// should be last - for development to make sure that no one else creates Tabs & Sections :p
	inline Tab _DummyLast{nullptr, nullptr};

} // namespace SptImGuiGroup

class SptImGui
{
public:
	// true if the imgui feature works and is loaded
	static bool Loaded();
	/*
	* Use this if you want a separate window for your feature, it returns true if the internal
	* feature is loaded.
	* 
	* The logic for window callbacks should look something like this:
	* if (ImGui::Begin(...)) {
	*   // drawing logic
	* }
	* ImGui::End();
	* 
	* Window callbacks are called in the order that the register functions were called in.
	* Since this (probably) depends on feature initialization order it should not be relied on
	* (at least until feature dependencies are implemented). The main window has no special
	* priority over other windows.
	* 
	* If your feature can be used from the console and you want support for many games/versions,
	* avoid relying on the ImGui callback to update the feature internals. Instead, use e.g.
	* SV_FrameSignal since that is more thoroughly tested and supported for more game versions.
	*/
	static bool RegisterWindowCallback(const SptImGuiWindowCallback& cb);

	/*
	* HUD cvars should all be listed in a single tab. For simple on/off cvars, use a checkbox.
	* For anything more complicated create a draw callback - if it's more than 1 line tall, put
	* it in a collapsible. You can (and should) reuse this callback for drawing the cvar logic
	* within the dedicated tab/section for your feature.
	* 
	* Your logic should be:
	* 
	* if (AddHudCallback(..., var))
	*   RegisterHudCvarXXX(var, ...);
	* 
	* This ensures that the cvar will only show up in the HUD tab of the cvar was initialized.
	*/
	static void RegisterHudCvarCheckbox(ConVar& var);
	static void RegisterHudCvarCallback(ConVar& var, const SptImGuiHudTextCallback& cb, bool putInCollapsible);

	/*
	* Opens the root window & calls ImGui::SetNextWindowFocus(). There are no utility functions for
	* bringing focus to a specific tab/section because the logic for that is actually very
	* complicated. If you have a (non-niche) use case for this, let me know and I will consider
	* implementing it. See: TODO GITHUB ISSUE
	*/
	static void BringFocusToMainWindow();

	static ImGuiWindow* GetMainWindow();
	static void ToggleFeature();

	// SPT-related widgets

	enum CvarValueFlags
	{
		CVF_NONE = 0,
		CVF_ALWAYS_QUOTE = 1 << 0, // always quote cvar value (default will only quote if the value has spaces)
		CVF_NO_WRANGLE = 1 << 1,   // don't wrangle the name (use for non-spt cvars)
		CVF_NO_COPY_BUTTON = 1 << 2,
		CVF_NO_RESET_BUTTON = 1 << 3,
	};

	// ImGui::SmallButton, but even smaller!
	static bool SmallIconButton(const char* label);
	// a button for a ConCommand with no args, does NOT invoke the command (because of OE compat)
	static bool CmdButton(const char* label, ConCommand& cmd);
	// display cvar name & value
	static void CvarValue(ConVar& c, CvarValueFlags flags = CVF_NONE);
	// a checkbox for a boolean cvar, returns value of cvar
	static bool CvarCheckbox(ConVar& c, const char* label, CvarValueFlags flags = CVF_NO_RESET_BUTTON);
	// a combo/dropdown box for a cvar with multiple integer options (does not use clipper - not optimized for huge lists), returns value of cvar
	static int CvarCombo(ConVar& c, const char* label, const char* const* opts, size_t nOpts);
	// a textbox for an integer in base 10 or 16, returns true if the value was modified
	static bool InputTextInteger(const char* label, const char* hint, long long& val, int radix);
	// a textbox for a cvar which can take on any long long value, returns the value
	static long long CvarInputTextInteger(ConVar& c, const char* label, const char* hint, int radix = 10);
	// wrapper of ImGui::InputDouble, returns true if the value was changed
	static bool InputDouble(const char* label, double* f);
	// wrapper of SptImGui::InputDouble
	static double CvarDouble(ConVar& c, const char* label, const char* hint);
	// either a slider or a draggable for a cvar, depending on the value of isSlider
	static long long CvarDraggableInt(ConVar& c,
	                                  const char* label,
	                                  const char* format = nullptr,
	                                  bool isSlider = false);
	static float CvarDraggableFloat(ConVar& c,
	                                const char* label,
	                                float speed, // only applicable if isSlider is false
	                                const char* format = nullptr,
	                                bool isSlider = false);
	/*
	* - uses DragScalarN & puts everything in a CmdGroup scope using the first cvar
	* - does not provide help text
	* - sets mins/maxs from the first cvar (if present)
	*/
	static void CvarsDragScalar(
	    ConVar* const* cvars,
	    void* data, // isFloat=true: floats, isFloat=false: long longs; will be filled by this function
	    int n,
	    bool isFloat,
	    const char* label,
	    float speed = 1.f,
	    const char* format = nullptr);
	// same as CvarsDragScalar, but uses ImGui::SliderScalarN instead of ImGui::DragScalarN, appropriate when the cvars have mins & maxs
	static void CvarsSliderScalar(ConVar* const* cvars,
	                              void* data,
	                              int n,
	                              bool isFloat,
	                              const char* label,
	                              const char* format = nullptr);
	// same as internal imgui help marker - a tooltip with extra info
	static void HelpMarker(const char* fmt, ...);
	// a help marker that says "Help text for <cvar_name>:\n\n<help_text>"
	static void CmdHelpMarkerWithName(const ConCommandBase& c);
	// add a border around an item - this is just a table with a single row/column
	static bool BeginBordered(const ImVec2& outer_size = {0.0f, 0.0f}, float inner_width = 0.0f);
	static void EndBordered();
	/*
	* A scope for cvar/cmd widgets:
	* - puts the whole diget into a group
	* - disables the group if the cvar/cmd is not registered (and returns false)
	* - creates a new ID scope
	* - does *not* check if the cvar is enabled
	* All of the above CvarXXX/CmdXXX widgets do this internally. The text HUD tab does not do this
	* this for you - instead you should check the return of AddHudCallback (refer to the comments
	* for the RegisterHudCvarXXX functions above).
	*/
	static bool BeginCmdGroup(const ConCommandBase& cmdBase);
	static void EndCmdGroup();

	struct AutocompletePersistData
	{
		/*
		* Values are readonly & most are for internal use. Apparently there is a plan to eventually
		* overhaul ImGui::InputText which may change how this works in the future. As a botched
		* hack, if you want to set the textInput yourself - instead of overwriting it, overwrite
		* one of the autocomplete entries and set the overwriteIdx to that entry. This will handle
		* recomputing the autocomplete entries, etc.
		*/
		char textInput[COMMAND_COMPLETION_ITEM_LENGTH]{""}; // public
		char autocomplete[COMMAND_COMPLETION_MAXITEMS][COMMAND_COMPLETION_ITEM_LENGTH];
		int nAutocomplete = 0;
		int autocompleteSelectIdx = 0;
		int overwriteIdx = -1;
		bool recomputeAutocomplete = true;
		bool enterPressed = false; // public
		bool modified = false;     // public
	};
	/*
	* A text input field with autocomplete for a specific ConCommand. To use, create a static
	* AutocompletePersistData and pass it in here every time you call this widget. 7.25 display
	* items is what BeginListBox() uses and is a reasonable default.
	* 
	* Textbox entries are fed to the autocomplete func as if entered from the console e.g. if the
	* textbox has "xyz", the autocomplete func will get "spt_draw_map_overlay xyz". This *should*
	* work for commands with multiple args (not tested). SPT's current implementations of
	* autocomplete functions are meant to be compatible with OE and therefore have limits of 64
	* entries & 64 characters per entry. Since I don't expect anyone to write their own
	* autocomplete functions for con commands, this widget is meant to be compatible with those
	* callbacks and therefore also has those limits.
	* 
	* If you want a more general-purpose autocomplete widget without these limits, let me know
	* and I can add another function without those restrictions.
	*/
	static void TextInputAutocomplete(const char* inputTextLabel,
	                                  const char* popupId,
	                                  AutocompletePersistData& persist,
	                                  FnCommandCompletionCallback autocompleteFn,
	                                  const char* cmdName,
	                                  float maxDisplayItems = 7.25f);

#ifdef SPT_PORTAL_UTILS

	struct PortalSelectionPersist
	{
		int selectedPortalIdx = -1; // [in/out] public
		bool enableHighPrecision = false;
		bool showIndexSelectorTooltip = false;          // [in] public
		bool userSelectedPortalByIndexLastCall = false; // [out] public
	};

	// Creates a table of portals for the user to select from. create a static
	// PortalSelectionPersist and pass it in here every time you call this widget.
	static const utils::PortalInfo* PortalSelectionWidget(PortalSelectionPersist& persist, int getPortalFlags);

	// For cvars which accept "blue"/"orange"/"auto" etc. Use the same flags as for getPortal.
	static const utils::PortalInfo* PortalSelectionWidgetCvar(ConVar& c,
	                                                          PortalSelectionPersist& persist,
	                                                          int getPortalFlags);

#endif

	/*
	* Useful for short timed error messages, e.g. user pressed a button but something failed
	* internally. To use, create a static instance of this and always call Show(). Call
	* StartShowing(text) to display a tooltip, or set the text first then call StartShowing().
	*/
	struct TimedToolTip
	{
		const char* text = nullptr;
		double lastStartDisplayTime = -666;

		bool Show(const ImVec4& color, double duration) const
		{
			if (text && ImGui::GetTime() - lastStartDisplayTime < duration)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, color);
				ImGui::SetTooltip("%s", text);
				ImGui::PopStyleColor();
				return true;
			}
			return false;
		}

		// newText must remain a valid pointer while showing
		void StartShowing(const char* newText)
		{
			text = newText;
			StartShowing();
		}

		void StartShowing()
		{
			lastStartDisplayTime = ImGui::GetTime();
		}

		void StopShowing()
		{
			text = nullptr;
			lastStartDisplayTime = -666;
		}
	};
};

inline ConCommand spt_gui{
    "spt_gui",
    SptImGui::ToggleFeature,
    "Toggle the SPT GUI",
    FCVAR_DONTRECORD,
};

// Use these instead of reinterpret casts! I've enabled IMGUI_USE_BGRA_PACKED_COLOR because that
// should in theory be better for the dx9 backend, but that means color32 != ImU32.

inline color32 ImU32ToColor32(ImU32 col)
{
	return {
	    (byte)(col >> IM_COL32_R_SHIFT),
	    (byte)(col >> IM_COL32_G_SHIFT),
	    (byte)(col >> IM_COL32_B_SHIFT),
	    (byte)(col >> IM_COL32_A_SHIFT),
	};
}

inline ImU32 Color32ToImU32(color32 col)
{
	return IM_COL32(col.r, col.g, col.b, col.a);
}
