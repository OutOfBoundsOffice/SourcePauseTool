#pragma once
#include "..\feature.hpp"

// Various demo features
class DemoStuff : public FeatureWrapper<DemoStuff>
{
public:
	void Demo_StopRecording();
	int Demo_GetPlaybackTick() const;
	int Demo_GetTotalTicks() const;
	bool Demo_IsPlayingBack() const;
	bool Demo_IsPlaybackPaused() const;
	bool Demo_IsAutoRecordingAvailable() const;
	bool Demo_IsDemoPlayerAvailable() const;
	void StartAutorecord();
	void StopAutorecord();

	DECL_HOOK_CDECL(void, Stop);

	void** pDemoplayer = nullptr;

protected:
	virtual bool ShouldLoadFeature() override;
	virtual void InitHooks() override;
	virtual void PreHook() override;
	virtual void LoadFeature() override;
	virtual void UnloadFeature() override;

private:
	int GetPlaybackTick_Offset = 0;
	int GetTotalTicks_Offset = 0;
	int IsPlayingBack_Offset = 0;
	int IsPlaybackPaused_Offset = 0;
	int currentAutoRecordDemoNumber = 1;
	int m_nDemoNumber_Offset = 0;
	int m_bRecording_Offset = 0;
	bool isAutoRecordingDemo = false;

	DECL_HOOK_THISCALL(void, StopRecording, void*);
	DECL_HOOK_THISCALL(bool, CDemoPlayer__StartPlayback, void*, const char* filename, bool as_time_demo);
	DECL_HOOK_THISCALL(const char*, CDemoFile__ReadConsoleCommand, void*);
	uintptr_t ORIG_Record = 0;

	void OnFrame();
	void OnSignonStateSignal(void* thisptr, int state);
};

extern DemoStuff spt_demostuff;
