#include "stdafx.hpp"
#include "..\generic.hpp"
#include "..\sptlib-wrapper.hpp"
#include "..\cvars.hpp"
#include "signals.hpp"
#include "dbg.h"
#include "interfaces.hpp"
#include <sstream>
#include "thirdparty/x86.h"

static void DemoJumpJitterFixCvarCallback(CON_COMMAND_CALLBACK_ARGS);

ConVar y_spt_demo_jump_jitter_fix("y_spt_demo_jump_jitter_fix",
								  "0",
								  0,
								  "Disables all extra movement on flashlight.",
								  DemoJumpJitterFixCvarCallback);

namespace patterns
{
	PATTERNS(
		m_iv_vecViewOffset__TargetString,
		"1",
		"43 5F 42 61 73 65 50 6C 61 79 65 72 3A 3A 6D 5F 69 76 5F 76 65 63 56 69 65 77 4F 66 66 73 65 74");
	PATTERNS(
		Client__StringReferences, 
		"1", 
		"68", 
		"2",
		"C7"
	);
} // namespace patterns


class DemoJumpJitterFixFeature : public FeatureWrapper<DemoJumpJitterFixFeature>
{
public:
	void Toggle(bool enabled);

protected:
	virtual bool ShouldLoadFeature() override;
	virtual void LoadFeature() override;
	virtual void InitHooks() override;
	virtual void UnloadFeature() override;

private:
	uintptr_t ORIG_m_iv_vecViewOffset__TargetString = 0;
	std::vector<patterns::MatchedPattern> MATCHES_Client__StringReferences;

	DECL_BYTE_REPLACE(InterpTypePush, 1, 0x12);
};
static DemoJumpJitterFixFeature spt_demojumpjitterfix;

bool DemoJumpJitterFixFeature::ShouldLoadFeature()
{
	return interfaces::engine_client != nullptr;
}

void DemoJumpJitterFixFeature::InitHooks()
{
	FIND_PATTERN(client, m_iv_vecViewOffset__TargetString);
	FIND_PATTERN_ALL(client, Client__StringReferences);
}

void DemoJumpJitterFixFeature::LoadFeature()
{
	if (!ORIG_m_iv_vecViewOffset__TargetString || MATCHES_Client__StringReferences.empty())
		return;

	GET_MODULE(client);

	/*
	* we're looking the following call:
	* AddVar( &m_vecViewOffset, &m_iv_vecViewOffset, LATCH_SIMULATION_VAR );
	* and changing LATCH_SIMULATION_VAR to LATCH_SIMULATION_VAR | INTERPOLATE_LINEAR_ONLY ((1 << 1) | (1 << 4))
	* 
	* m_iv_vecViewOffset is an IInterpolatedVar which requires a debug name on most versions
	* so we'll hinge our search for that call from its string reference.
	*/
	for (auto match : MATCHES_Client__StringReferences)
	{
		auto strRefPtr = match.ptr;
		uintptr_t refLocation = *(uintptr_t*)(strRefPtr + ((*(uint8_t*)strRefPtr == 0x68) ? 1 : 3));
		if (refLocation != (uintptr_t)ORIG_m_iv_vecViewOffset__TargetString)
			continue;

		uint8_t* addr = (uint8_t*)strRefPtr;
		for (int lenLeft = 0x200; lenLeft > 0;)
		{
			int len = x86_len(addr);
			addr += len;
			lenLeft -= len;

			// outside of function now
			if (addr[0] == X86_INT || addr[0] == X86_NOP)
				goto end;

			int offset = 1;
			bool found =
				// search for push 02 (6A 02)
				(addr[0] == X86_PUSHI8 && addr[1] == 2)
				// search for mov [reg + off],02 (c7 ?? ?? ?? 02 00 00 00)
			    || (addr[0] == X86_MOVMIW && (offset = 4, (*(uint32_t*)(addr + 4)) == 2));
			if (found)
			{
				DevMsg("Found C_BasePlayer::m_iv_vecViewOffset AddVar() type argument push at %p\n", addr);
				INIT_BYTE_REPLACE(InterpTypePush, (uintptr_t)(addr + offset));
				InitConcommandBase(y_spt_demo_jump_jitter_fix);
				return;
			}
		}
	}

end:;
	DevWarning("Couldn't find C_BasePlayer::m_iv_vecViewOffset AddVar() type argument push!\n");
}

void DemoJumpJitterFixFeature::Toggle(bool enabled)
{
	if (!PTR_InterpTypePush)
	{
		Warning("The demo jitter jump fix isn't functional at the moment.\n");
	}

	if (enabled)
	{
		DO_BYTE_REPLACE(InterpTypePush);
	}
	else
	{
		UNDO_BYTE_REPLACE(InterpTypePush);
	}

	Msg("Please restart the current map or reload a save or replay the demo for the change to take effect.\n");
}

void DemoJumpJitterFixCvarCallback(CON_COMMAND_CALLBACK_ARGS)
{
	spt_demojumpjitterfix.Toggle(((ConVar*)var)->GetBool());
}
	
void DemoJumpJitterFixFeature::UnloadFeature() 
{
	DESTROY_BYTE_REPLACE(InterpTypePush);
}
