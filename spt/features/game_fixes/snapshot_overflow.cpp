#include "stdafx.hpp"
#include "..\feature.hpp"
#include "..\visualizations\imgui\imgui_interface.hpp"

ConVar spt_prevent_snapshot_overflow("spt_prevent_snapshot_overflow",
                                     "0",
                                     FCVAR_DONTRECORD,
                                     "If enabled, tries to prevent the reliable snapshot overflow error.");

namespace patterns
{
	/*
	* This pattern starts at the 'push -1' instruction for the bf_write constructor (nMaxBits = -1).
	* The instructions MUST match the following:
	* PUSH -1                (2 bytes)
	* PUSH int               (5 bytes) <- pushes size of scratch space, we overwrite this
	* LEA reg1,[reg2 + off]  (6 bytes) <-
	* PUSH reg1              (2 bytes) <^ these two instructions push the scratch buffer, we overwrite this
	* 
	* If the instructions are different, the code below must be changed.
	*/
	PATTERNS(MiddleOfCBaseClient__SendSnapshot,
	         "3420-portal1",
	         "6A FF 68 ?? ?? ?? ?? 8D 8E ?? ?? ?? ?? 51 68 ?? ?? ?? ?? 8D 4C ?? 20 E8 ?? ?? ?? ?? 8B 16",
	         "7122284-hl2",
	         "6A FF 68 ?? ?? ?? ?? 8D 85 ?? ?? ?? ?? 50 68 ?? ?? ?? ?? 8D 4D ?? E8 ?? ?? ?? ?? 8B 5D 08");
} // namespace patterns

/*
* This feature is meant to prevent the "reliable snapshot overflow" error that happens (more) on older versions.
* The message is spelled "snaphsot" on older versions. It happens because the scratch buffer used by the game for
* network updates is too small, so we just™ increase it. This is somewhat annoying because the actual buffer is a
* fixed size buffer in a class instance. Luckily it's only used in this one part of the code, so we'll modify the
* instructions to push our own larger buffer instead.
*/
class SnapshotOverflow : public FeatureWrapper<SnapshotOverflow>
{
protected:
	virtual void InitHooks() override
	{
		FIND_PATTERN(engine, MiddleOfCBaseClient__SendSnapshot);
	};

	virtual void LoadFeature() override
	{
		if (!ORIG_MiddleOfCBaseClient__SendSnapshot)
			return;

		memcpy(origBytes, ORIG_MiddleOfCBaseClient__SendSnapshot, sizeof origBytes);
		memcpy(newBytes, ORIG_MiddleOfCBaseClient__SendSnapshot, sizeof newBytes);

		int origScratchSize = *reinterpret_cast<int*>(newBytes + 3);
		if (origScratchSize < 0 || origScratchSize > 0x20000)
		{
			// I've never seen this error happen on newer game versions where the buffer is much larger...
			ORIG_MiddleOfCBaseClient__SendSnapshot = nullptr;
			return;
		}

		// arbitrarily choose a scratch space 4x larger than the one used by the game,
		int scratchSize = origScratchSize * 4;
		scratchBytes.reset(new byte[scratchSize]);

		// push scratch size
		*reinterpret_cast<int*>(newBytes + 3) = scratchSize;
		// push
		newBytes[7] = 0x68;
		// address to new scratch buffer
		*reinterpret_cast<byte**>(newBytes + 8) = scratchBytes.get();
		// pad up to the next push with nops
		std::fill(newBytes + 12, newBytes + sizeof newBytes, 0x90);

		InitConcommandBase(spt_prevent_snapshot_overflow);

		spt_prevent_snapshot_overflow.InstallChangeCallback(
		    [](
#ifdef OE
		        ConVar* var, char const* pOldString
#else
		        IConVar* var, const char* pOldValue, float flOldValue
#endif
		    )
		    {
			    extern SnapshotOverflow spt_snapShotOverflow;
			    spt_snapShotOverflow.SetOverwrite(((ConVar*)var)->GetBool());
		    });

		SptImGuiGroup::Cheats_Misc_SnapshotOverflow.RegisterUserCallback(
		    []() { SptImGui::CvarCheckbox(spt_prevent_snapshot_overflow, "##checkbox"); });
	};

	virtual void UnloadFeature() override
	{
		if (!ORIG_MiddleOfCBaseClient__SendSnapshot)
			return;
		SetOverwrite(0);
		ORIG_MiddleOfCBaseClient__SendSnapshot = nullptr;
	};

public:
	void SetOverwrite(bool overwrite)
	{
		Assert(ORIG_MiddleOfCBaseClient__SendSnapshot);
		if (!ORIG_MiddleOfCBaseClient__SendSnapshot)
			return;
		if (overwrite == overwritten)
			return;
		overwritten = overwrite;
		MemUtils::ReplaceBytes(ORIG_MiddleOfCBaseClient__SendSnapshot,
		                       overwrite ? sizeof(newBytes) : sizeof(origBytes),
		                       overwrite ? newBytes : origBytes);
	}

private:
	void* ORIG_MiddleOfCBaseClient__SendSnapshot = nullptr;
	byte newBytes[14];
	byte origBytes[sizeof newBytes];
	bool overwritten = false;
	std::unique_ptr<byte[]> scratchBytes;
};

SnapshotOverflow spt_snapShotOverflow;
