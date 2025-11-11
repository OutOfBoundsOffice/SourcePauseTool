#include <stdafx.hpp>
#include <inttypes.h>
#include <array>

#include "imgui_interface.hpp"
#include "thirdparty/imgui/imgui_internal.h"
#include "spt/utils/portal_utils.hpp"
#include "thirdparty/fonts/codicon/codepoints/IconsCodicons.h"

// for float/integer text inputs have a constant width
#define SPT_IMGUI_NUMBER_INPUT_N_CHARS 25
#define SPT_SET_NUMBER_INPUT_ITEM_WIDTH(maxChars) ImGui::SetNextItemWidth(ImGui::GetFontSize() * (maxChars) * 0.5f)

// A stack based storage for a single ImGui context for data of arbitrary length.
// When popped, the pointer to the data remains valid until Push/Pop is called again.
class
{
	std::vector<char> userStorage;
	std::vector<size_t> userStorageOffsets;
	bool lastUserStorageWasPop = false;

	void InvalidatePoppedData()
	{
		if (lastUserStorageWasPop)
		{
			userStorage.resize(userStorageOffsets.back());
			userStorageOffsets.pop_back();
			lastUserStorageWasPop = false;
		}
	}

public:
	void Push(const void* data, size_t len)
	{
		InvalidatePoppedData();
		userStorageOffsets.push_back(userStorage.size());
		userStorage.resize(userStorage.size() + len);
		memcpy(userStorage.data() + userStorageOffsets.back(), data, len);
	}

	void* Pop(size_t* len)
	{
		InvalidatePoppedData();
		Assert(!userStorage.empty() && !userStorageOffsets.empty());
		if (len)
			*len = userStorageOffsets.back();
		lastUserStorageWasPop = true;
		return userStorage.data() + userStorageOffsets.back();
	}

} static g_SptImGuiUserStorage;

bool SptImGui::CmdButton(const char* label, ConCommand& cmd)
{
	BeginCmdGroup(cmd);
	bool ret = ImGui::Button(label);
	ImGui::SameLine();
	ImGui::TextDisabled("(%s)", WrangleLegacyCommandName(cmd.GetName(), true, nullptr));
	ImGui::SameLine();
	HelpMarker("%s", cmd.GetHelpText());
	EndCmdGroup();
	return ret;
}

void SptImGui::CvarValue(const ConVar& c, CvarValueFlags flags)
{
	BeginCmdGroup(c);
	const char* v = c.GetString();
	const char* surround = "";
	if ((flags & CVF_ALWAYS_QUOTE) || !*v || strchr(v, ' '))
		surround = "\"";
	const char* name = flags & CVF_NO_WRANGLE ? c.GetName() : WrangleLegacyCommandName(c.GetName(), true, nullptr);
	ImGui::TextDisabled("(%s %s%s%s)", name, surround, v, surround);
	ImGui::SameLine();
	HelpMarker("%s", c.GetHelpText());
	EndCmdGroup();
}

bool SptImGui::CvarCheckbox(ConVar& c, const char* label)
{
	BeginCmdGroup(c);
	bool oldVal = c.GetBool();
	bool newVal = oldVal;
	ImGui::Checkbox(label, &newVal);
	if (oldVal != newVal)
		c.SetValue(newVal); // only set value if it was changed to not spam cvar callbacks
	ImGui::SameLine();
	CvarValue(c);
	EndCmdGroup();
	return newVal;
}

int SptImGui::CvarCombo(ConVar& c, const char* label, const char* const* opts, size_t nOpts)
{
	Assert(nOpts >= 1);
	BeginCmdGroup(c);
	int val = clamp(c.GetInt(), 0, (int)nOpts - 1);

	if (ImGui::BeginCombo(label, opts[val], ImGuiComboFlags_WidthFitPreview))
	{
		for (int i = 0; i < (int)nOpts; i++)
		{
			const bool is_selected = (val == i);
			if (ImGui::Selectable(opts[i], is_selected))
			{
				val = i;
				// set value even if it wasn't "changed" to apply clamp (e.g. was 4 but nOpts is 3 and value was set to 2)
				c.SetValue(i);
			}
			if (is_selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	CvarValue(c);
	EndCmdGroup();
	return val;
}

bool SptImGui::InputTextInteger(const char* label, const char* hint, long long& val, int radix)
{
	Assert(radix == 10 || radix == 16);
	char buf[SPT_IMGUI_NUMBER_INPUT_N_CHARS];
	const char* fmtSpecifier = radix == 10 ? "%" PRId64 : "%" PRIx64;
	snprintf(buf, sizeof buf, fmtSpecifier, val);
	SPT_SET_NUMBER_INPUT_ITEM_WIDTH(sizeof buf);
	ImGuiInputTextFlags flags =
	    ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_AutoSelectAll;
	flags |= radix == 10 ? ImGuiInputTextFlags_CharsDecimal : ImGuiInputTextFlags_CharsHexadecimal;
	bool ret = ImGui::InputTextWithHint(label, hint, buf, sizeof buf, flags);
	if (ret)
		_snscanf_s(buf, sizeof buf, fmtSpecifier, &val);
	return ret;
}

long long SptImGui::CvarInputTextInteger(ConVar& c, const char* label, const char* hint, int radix)
{
	// don't use c.GetInt() since that's cast from a float
	Assert(radix == 10 || radix == 16);
	BeginCmdGroup(c);
	long long val = strtoll(c.GetString(), nullptr, radix);
	if (InputTextInteger(label, hint, val, radix))
	{
		char buf[24];
		snprintf(buf, sizeof buf, radix == 10 ? "%" PRId64 : "%" PRIx64, val);
		c.SetValue(buf);
	}
	ImGui::SameLine();
	CmdHelpMarkerWithName(c);
	EndCmdGroup();
	return val;
}

static void DraggableCvarsScalarInternal(ConVar* const* cvars,
                                         void* data,
                                         int n,
                                         bool isFloat,
                                         const char* label,
                                         float speed,
                                         const char* format,
                                         bool isSlider,
                                         ImGuiSliderFlags flags)
{
	Assert(cvars && data && n > 0);

	for (int i = 0; i < n; i++)
	{
		// cvars do string -> float -> int, so always use strtoll
		if (isFloat)
			((float*)data)[i] = cvars[i]->GetFloat();
		else
			((long long*)data)[i] = strtoll(cvars[i]->GetString(), nullptr, 10);
	}

	// use the mins/maxs from the first cvar if they exist
	float fMin, fMax;
	long long llMin, llMax;
	void *pMin = nullptr, *pMax = nullptr;
	if (cvars[0]->GetMin(fMin))
	{
		if (isFloat)
		{
			pMin = &fMin;
		}
		else
		{
			llMin = static_cast<long long>(fMin);
			pMin = &llMin;
		}
	}
	if (cvars[0]->GetMax(fMax))
	{
		if (isFloat)
		{
			pMax = &fMax;
		}
		else
		{
			llMax = static_cast<long long>(fMax);
			pMax = &llMax;
		}
	}

	SptImGui::BeginCmdGroup(*cvars[0]);
	ImGuiDataType dataType = isFloat ? ImGuiDataType_Float : ImGuiDataType_S64;
	bool ret = isSlider ? ImGui::SliderScalarN(label, dataType, data, n, pMin, pMax, format, flags)
	                    : ImGui::DragScalarN(label, dataType, data, n, speed, pMin, pMax, format, flags);
	if (ret)
	{
		for (int i = 0; i < n; i++)
		{
			if (isFloat)
				cvars[i]->SetValue(((float*)data)[i]);
			else
				cvars[i]->SetValue((int)(((long long*)data)[i]));
		}
	}
	SptImGui::EndCmdGroup();
}

long long SptImGui::CvarDraggableInt(ConVar& c, const char* label, const char* format, bool isSlider)
{
	[[maybe_unused]] float dummy;
	Assert(!isSlider || (c.GetMin(dummy) && c.GetMax(dummy)));
	BeginCmdGroup(c);
	SPT_SET_NUMBER_INPUT_ITEM_WIDTH(SPT_IMGUI_NUMBER_INPUT_N_CHARS);
	ConVar* pc = &c;
	long long val;
	// clang-format off
	DraggableCvarsScalarInternal(
		&pc, &val, 1, false, label, 1.f, format, isSlider,
		isSlider ? ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_NoRoundToFormat
	);
	// clang-format on
	ImGui::SameLine();
	if (*label)
		CmdHelpMarkerWithName(c);
	else
		CvarValue(c);
	EndCmdGroup();
	return val;
}

float SptImGui::CvarDraggableFloat(ConVar& c, const char* label, float speed, const char* format, bool isSlider)
{
	[[maybe_unused]] float dummy;
	Assert(!isSlider || (c.GetMin(dummy) && c.GetMax(dummy)));
	BeginCmdGroup(c);
	SPT_SET_NUMBER_INPUT_ITEM_WIDTH(SPT_IMGUI_NUMBER_INPUT_N_CHARS);
	ConVar* pc = &c;
	float val;
	// clang-format off
	DraggableCvarsScalarInternal(
		&pc, &val, 1, true, label, speed, format, isSlider,
		isSlider ? ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_NoRoundToFormat
	);
	// clang-format on
	ImGui::SameLine();
	if (*label)
		CmdHelpMarkerWithName(c);
	else
		CvarValue(c);
	EndCmdGroup();
	return val;
}

bool SptImGui::InputDouble(const char* label, double* f)
{
	SPT_SET_NUMBER_INPUT_ITEM_WIDTH(SPT_IMGUI_NUMBER_INPUT_N_CHARS);
	return ImGui::InputDouble(label, f, 0.0, 0.0, "%g");
}

double SptImGui::CvarDouble(ConVar& c, const char* label, const char* hint)
{
	BeginCmdGroup(c);
	double val = atof(c.GetString());
	if (SptImGui::InputDouble(label, &val))
	{
		char buf[SPT_IMGUI_NUMBER_INPUT_N_CHARS];
		sprintf_s(buf, "%f", val);
		c.SetValue(buf);
	}
	ImGui::SameLine();
	CmdHelpMarkerWithName(c);
	EndCmdGroup();
	return val;
}

void SptImGui::CvarsDragScalar(ConVar* const* cvars,
                               void* data,
                               int n,
                               bool isFloat,
                               const char* label,
                               float speed,
                               const char* format)
{
	// clang-format off
	DraggableCvarsScalarInternal(cvars, data, n, isFloat, label, speed, format, false, ImGuiSliderFlags_NoRoundToFormat);
	// clang-format on
}

void SptImGui::CvarsSliderScalar(ConVar* const* cvars,
                                 void* data,
                                 int n,
                                 bool isFloat,
                                 const char* label,
                                 const char* format)
{
	// clang-format off
	DraggableCvarsScalarInternal(cvars, data, n, isFloat, label, -1, format, true, ImGuiSliderFlags_NoRoundToFormat | ImGuiSliderFlags_AlwaysClamp);
	// clang-format on
}

void SptImGui::HelpMarker(const char* fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	ImGui::TextDisabled(ICON_CI_QUESTION);
	if (ImGui::BeginItemTooltip())
	{
		ImGui::PushTextWrapPos(-1.f);
		ImGui::TextV(fmt, va);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
	va_end(va);
}

void SptImGui::CmdHelpMarkerWithName(const ConCommandBase& c)
{
	HelpMarker("Help text for %s:\n\n%s", WrangleLegacyCommandName(c.GetName(), true, nullptr), c.GetHelpText());
}

bool SptImGui::BeginBordered(const ImVec2& outer_size, float inner_width)
{
	if (ImGui::BeginTable("##table_border",
	                      1,
	                      ImGuiTableFlags_BordersOuter | ImGuiTableFlags_NoHostExtendX,
	                      outer_size,
	                      inner_width))
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		return true;
	}
	return false;
}

void SptImGui::EndBordered()
{
	ImGui::EndTable();
}

void SptImGui::BeginCmdGroup(const ConCommandBase& cmdBase)
{
	ImGui::BeginGroup();
	ImGui::BeginDisabled(!cmdBase.IsRegistered());
	auto adr = &cmdBase; // assume the command has a constant address
	g_SptImGuiUserStorage.Push(&adr, sizeof adr);
}

void SptImGui::EndCmdGroup()
{
	const ConCommandBase* cmdBase = *static_cast<const ConCommandBase**>(g_SptImGuiUserStorage.Pop(nullptr));
	ImGui::EndDisabled();
	ImGui::EndGroup();
	if (!cmdBase->IsRegistered())
	{
		ImGui::SetItemTooltip("%s \"%s\" was not initialized",
		                      cmdBase->IsCommand() ? "ConCommand" : "ConVar",
		                      WrangleLegacyCommandName(cmdBase->GetName(), true, nullptr));
	}
}

void SptImGui::TextInputAutocomplete(const char* inputTextLabel,
                                     const char* popupId,
                                     AutocompletePersistData& persist,
                                     FnCommandCompletionCallback autocompleteFn,
                                     const char* cmdName,
                                     float maxDisplayItems)
{
	/*
	* Unfortunately ImGui does not (currently) have a InputText with autocomplete, so I tried to
	* make one myself. The feature is discussed here: https://github.com/ocornut/imgui/issues/718.
	* Whenever the text box is focused, a new window is created which lists the autocomplete options.
	* Instead of using BeginListBox() directly, I set the window size and use clipper instead. In
	* theory, that means that this can be expanded in the future to support much more than the default
	* 64 items (that source uses) and it should still be efficient (tm).
	* 
	* There are a few nuances to the input handling that I haven't figured out how to account for
	* gracefully. The tricky bit is that I want to allow both keyboard TAB autocomplete and selecting
	* the autocomplete entry with the mouse. Keyboard events by themselves are simple enough:
	* 
	* - each edit to the text field recomputes the autocomplete entries
	* - pressing TAB fills the text field with the selected entry (and recomputes autocomplete)
	* - UP/DOWN arrows change the selected item and force the scroll bar to move
	* 
	* Using the mouse on its own is also pretty simple. The only mildly annoying thing I haven't
	* figured out is when you interact with the popup window with the mouse, that loses focus on the
	* text box and you need to click on it again to start typing. If we select an autocomplete entry
	* with the mouse, then we can use SetActiveID() to refocus the text box, but if we start using
	* the scroll bar all hope is lost. Using SetActiveID() on the text box in that case will prevent
	* the scroll bar from getting used.
	*/

	struct AutoCompleteUserData
	{
		AutocompletePersistData& persist;
		FnCommandCompletionCallback autocompleteFn;
		const char* cmdName;
		bool forceScroll;
	} udata{
	    .persist = persist,
	    .autocompleteFn = autocompleteFn,
	    .cmdName = cmdName,
	    .forceScroll = false,
	};

	if (persist.recomputeAutocomplete)
	{
		persist.nAutocomplete = udata.autocompleteFn(persist.textInput, persist.autocomplete);
		persist.recomputeAutocomplete = false;
	}

	ImGuiInputTextFlags textFlags = ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CallbackHistory
	                                | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackEdit
	                                | ImGuiInputTextFlags_EnterReturnsTrue;
	// The only way I've been able to overwrite the buffer (e.g. mouse pressed on autocomplete entry)
	// is within the callback; setting the textInput directly isn't currently supported by ImGui ;}.
	if (persist.overwriteIdx >= 0)
		textFlags |= ImGuiInputTextFlags_CallbackAlways;
	persist.modified = false;

	persist.enterPressed = ImGui::InputText(
	    inputTextLabel,
	    persist.textInput,
	    sizeof persist.textInput,
	    textFlags,
	    [](ImGuiInputTextCallbackData* data)
	    {
		    AutoCompleteUserData& ud = *(AutoCompleteUserData*)data->UserData;
		    AutocompletePersistData& pd = ud.persist;
		    switch (data->EventFlag)
		    {
		    case ImGuiInputTextFlags_CallbackAlways:
			    if (pd.overwriteIdx < 0)
				    break;
			    [[fallthrough]];
		    case ImGuiInputTextFlags_CallbackCompletion:
			    strncpy(data->Buf,
			            pd.autocomplete[pd.overwriteIdx >= 0 ? pd.overwriteIdx : pd.autocompleteSelectIdx],
			            data->BufSize);
			    pd.overwriteIdx = -1;
			    data->BufTextLen = data->CursorPos = strnlen(data->Buf, data->BufSize);
			    data->SelectionStart = data->SelectionEnd;
			    data->BufDirty = true;
			    [[fallthrough]];
		    case ImGuiInputTextFlags_CallbackEdit:
		    {
			    /*
				* Feed data to the completion callback as if it was called from the console - the
				* cmd name is added before the textbox input and dropped from the autocomplete
				* results. If we change this to a more generic (non ConCommand) based function,
				* feed the textbox input directly to the autocomplete func instead of prefixing it.
				*/
			    static char tmpBuf[SPT_MAX_CVAR_NAME_LEN + COMMAND_COMPLETION_ITEM_LENGTH + 2];
			    snprintf(tmpBuf, sizeof tmpBuf, "%s %s", ud.cmdName, data->Buf);
			    tmpBuf[sizeof(tmpBuf) - 1] = '\0';
			    pd.nAutocomplete = ud.autocompleteFn(tmpBuf, pd.autocomplete);
			    size_t nTrim = strlen(ud.cmdName) + 1;
			    for (int i = 0; i < pd.nAutocomplete; i++)
				    memmove(pd.autocomplete[i], pd.autocomplete[i] + nTrim, data->BufSize - nTrim);
			    pd.autocompleteSelectIdx = 0;
			    pd.modified = true;
			    break;
		    }
		    case ImGuiInputTextFlags_CallbackHistory:
			    // The history flag was meant for getting previously entered values,
			    // we're using it to notify us when up/down is pressed.
			    pd.autocompleteSelectIdx += data->EventKey == ImGuiKey_UpArrow
			                                    ? -1
			                                    : (data->EventKey == ImGuiKey_DownArrow ? 1 : 0);
			    ud.forceScroll = true;
			    break;
		    }
		    if (pd.nAutocomplete != 0)
			    pd.autocompleteSelectIdx = (pd.autocompleteSelectIdx + pd.nAutocomplete) % pd.nAutocomplete;
		    return 0;
	    },
	    &udata);

	ImGuiWindow* textBoxWnd = ImGui::GetCurrentWindow();
	ImGuiID textBoxId = ImGui::GetID(inputTextLabel);
	ImGuiWindow* navWindow = ImGui::GetCurrentContext()->NavWindow;

	bool popupIsNav = navWindow && !strcmp(popupId, navWindow->Name);

	// determine if we should display the "popup"

	// no focused window or focused window is not root/popup window
	if (!navWindow || (navWindow != textBoxWnd && !popupIsNav))
		return;
	// nothing to autocomplete
	if (persist.nAutocomplete == 0)
		return;
	/*
	* - if the textbox is the active element display the popup
	* - if the popup window is the nav window - great, keep it alive
	* - the third condition is to handle the one frame transition between those two states:
	*   (the textbox is not active AND the popup is not the nav window)
	*/
	if (!(popupIsNav || ImGui::IsItemActive() || ImGui::IsItemDeactivated()))
		return;
	// 1 entry to autocomplete but it's the same as what user typed in the text box
	if (persist.nAutocomplete == 1 && !strcmp(persist.autocomplete[0], persist.textInput))
		return;

	ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y));
	float nDisplayItems, framePadding;
	if (persist.nAutocomplete <= maxDisplayItems)
	{
		nDisplayItems = persist.nAutocomplete;
		framePadding = 0;
	}
	else
	{
		nDisplayItems = maxDisplayItems;
		framePadding = ImGui::GetStyle().FramePadding.y * 2.0f;
	}
	ImGui::SetNextWindowSize({
	    ImGui::GetItemRectSize().x,
	    ImGui::GetTextLineHeightWithSpacing() * nDisplayItems + framePadding,
	});
	auto& style = ImGui::GetStyle();
	// align clipper entries with the text in the textbox
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {style.FramePadding.x, style.ItemSpacing.y * .5f});
	ImGuiWindowFlags wndFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
	                            | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings
	                            | ImGuiWindowFlags_NoFocusOnAppearing;

	if (ImGui::Begin(popupId, nullptr, wndFlags))
	{
		ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

		ImGuiListClipper clipper;
		clipper.Begin(persist.nAutocomplete);
		clipper.IncludeItemByIndex(persist.autocompleteSelectIdx); // to allow ScrollToItem()
		while (clipper.Step())
		{
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
			{
				ImGui::PushID(i);
				if (ImGui::Selectable(persist.autocomplete[i], persist.autocompleteSelectIdx == i))
				{
					persist.overwriteIdx = i;
					ImGui::SetActiveID(textBoxId, textBoxWnd);
				}
				if (udata.forceScroll && persist.autocompleteSelectIdx == i)
					ImGui::ScrollToItem();
				ImGui::PopID();
			}
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();
}

#ifdef SPT_PORTAL_UTILS

const utils::PortalInfo* SptImGui::PortalSelectionWidget(PortalSelectionPersist& persist, int getPortalFlags)
{
	ImGui::BeginGroup();
	ImGui::Checkbox("Show high precision position/angles##portal_select", &persist.enableHighPrecision);

	const utils::PortalInfo* selectedPortal = nullptr;
	persist.userSelectedPortalByIndexLastCall = false;

	bool serverEnabled = utils::spt_serverEntList.Valid();
	auto& portalList = utils::GetPortalList();
	if (portalList.empty())
	{
		ImGui::TextDisabled("No portals...");
	}
	else
	{
		ImGuiTabBarFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
		                              | ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_NoKeepColumnsVisible;
		if (ImGui::BeginTable("##portal_select", 11, tableFlags))
		{
			ImGui::TableSetupColumn("index", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("linked", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("color", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("state", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("x", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("y", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("z", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("pitch", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("yaw", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("roll", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("linkage", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableHeadersRow();

			const char* posAngFmt = persist.enableHighPrecision ? "%.9g" : "%.2f";

			char buf[16];
			for (auto& portal : portalList)
			{
				if (portal.handle.GetEntryIndex() == persist.selectedPortalIdx)
					selectedPortal = &portal;

				ImGui::TableNextRow(ImGuiTableRowFlags_None);

				// handle
				ImGui::TableSetColumnIndex(0);
				snprintf(buf, sizeof buf, "%d", portal.handle.GetEntryIndex());

				bool portalClosedSelectionDisabled =
				    !portal.isOpen && (getPortalFlags & GPF_ONLY_OPEN_PORTALS);
				ImGuiSelectableFlags selectFlags =
				    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;
				if (portalClosedSelectionDisabled)
					selectFlags |= ImGuiSelectableFlags_Disabled;

				if (ImGui::Selectable(buf,
				                      portal.handle.GetEntryIndex() == persist.selectedPortalIdx,
				                      selectFlags))
				{
					selectedPortal = &portal;
					persist.selectedPortalIdx = portal.handle.GetEntryIndex();
					persist.userSelectedPortalByIndexLastCall = true;
				}

				if (portalClosedSelectionDisabled)
				{
					ImGui::SetItemTooltip("Can't select closed portals");
				}
				else if (persist.showIndexSelectorTooltip) // hack
				{
					ImGui::SetItemTooltip(
					    "Clicking here will change the portal selection to index mode");
				}

				// linked handle
				ImGui::TableSetColumnIndex(1);
				if (portal.linkedHandle.IsValid())
					ImGui::Text("%d", portal.linkedHandle.GetEntryIndex());
				else
					ImGui::TextDisabled("NONE");

				// color
				ImGui::TableSetColumnIndex(2);
				ImGui::TextColored(portal.isOrange ? ImVec4{1.f, .63f, .13f, 1.f}
				                                   : ImVec4{.25f, .63f, 1.f, 1.f},
				                   portal.isOrange ? "orange" : "blue");

				// state
				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(portal.isOpen ? "open"
				                                     : (portal.isActivated ? "closed" : "inactive"));

				// pos & ang
				for (int i = 0; i < 3; i++)
				{
					ImGui::TableSetColumnIndex(4 + i);
					ImGui::Text(posAngFmt, portal.pos[i]);
					ImGui::TableSetColumnIndex(7 + i);
					ImGui::Text(posAngFmt, portal.ang[i]);
				}

				// linkage ID
				ImGui::TableSetColumnIndex(10);
				if (serverEnabled)
				{
					ImGui::Text("%d", portal.linkageId);
				}
				else
				{
					ImGui::TextDisabled("N/A");
					ImGui::SetTooltip("Linkage ID is only available when the server is active");
				}
			}
			ImGui::EndTable();
		}
	}
	if (!selectedPortal && persist.selectedPortalIdx >= 0 && persist.selectedPortalIdx < MAX_EDICTS)
		persist.selectedPortalIdx = -1;
	ImGui::EndGroup();
	return selectedPortal;
}

const utils::PortalInfo* SptImGui::PortalSelectionWidgetCvar(ConVar& c,
                                                             PortalSelectionPersist& persist,
                                                             int getPortalFlags)
{
	BeginCmdGroup(c);

	const char* oldCvarVal = c.GetString();

	size_t nOpts = 0;
	std::array<const char*, 5> opts;
	if (getPortalFlags & GPF_ALLOW_OVERLAY)
		opts[nOpts++] = "overlay";
	if (getPortalFlags & GPF_ALLOW_AUTO)
		opts[nOpts++] = "auto";
	if (getPortalFlags & GPF_ALLOW_PLAYER_ENV)
		opts[nOpts++] = "env";
	opts[nOpts++] = "blue";
	opts[nOpts++] = "orange";

	char previewBuf[32];
	const char* start = c.GetString();
	char* end;
	long idx = strtol(start, &end, 10);
	if (start == end)
	{
		persist.showIndexSelectorTooltip = true;
	}
	else
	{
		persist.showIndexSelectorTooltip = false;
		snprintf(previewBuf, sizeof previewBuf, "index: %" PRId32, idx);
	}

	if (ImGui::BeginCombo("Portal type",
	                      persist.showIndexSelectorTooltip ? oldCvarVal : previewBuf,
	                      ImGuiComboFlags_WidthFitPreview))
	{
		for (size_t i = 0; i < nOpts; i++)
		{
			const bool is_selected = !stricmp(opts[i], oldCvarVal);
			if (ImGui::Selectable(opts[i], is_selected))
				c.SetValue(opts[i]);
			if (is_selected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	CvarValue(c);

	const utils::PortalInfo* selectedPortal = getPortal(c.GetString(), getPortalFlags);
	if (selectedPortal)
		persist.selectedPortalIdx = selectedPortal->handle.GetEntryIndex();
	else
		persist.selectedPortalIdx = -1;

	selectedPortal = PortalSelectionWidget(persist, getPortalFlags);
	if (persist.userSelectedPortalByIndexLastCall)
	{
		Assert(selectedPortal);
		c.SetValue(selectedPortal->handle.GetEntryIndex());
	}

	EndCmdGroup();
	return selectedPortal;
}

#endif
