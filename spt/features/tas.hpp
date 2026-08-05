#pragma once
#include "..\feature.hpp"

extern ConVar tas_strafe;
extern ConVar tas_strafe_type;
extern ConVar tas_strafe_dir;
extern ConVar tas_strafe_scale;
extern ConVar tas_strafe_yaw;
extern ConVar tas_strafe_buttons;
extern ConVar tas_strafe_vectorial;
extern ConVar tas_strafe_vectorial_increment;
extern ConVar tas_strafe_vectorial_offset;
extern ConVar tas_strafe_vectorial_snap;
extern ConVar tas_strafe_allow_jump_override;
extern ConVar tas_strafe_capped_limit;
extern ConVar tas_strafe_hull_is_line;
extern ConVar tas_strafe_use_tracing;
extern ConVar tas_force_airaccelerate;
extern ConVar tas_force_wishspeed_cap;
extern ConVar tas_reset_surface_friction;
extern ConVar tas_force_onground;
extern ConVar tas_strafe_version;
extern ConVar tas_strafe_afh_length;
extern ConVar tas_strafe_afh;
extern ConVar tas_strafe_lgagst;
extern ConVar tas_strafe_lgagst_minspeed;
extern ConVar tas_strafe_lgagst_fullmaxspeed;
extern ConVar tas_strafe_lgagst_min;
extern ConVar tas_strafe_lgagst_max;
extern ConVar tas_strafe_jumptype;
extern ConVar tas_strafe_autojb;
extern ConVar tas_script_printvars;
extern ConVar tas_script_savestates;
extern ConVar tas_script_onsuccess;
extern ConVar y_spt_hud_script_progress;

// these live in other cpp files
extern ConVar tas_anglespeed;
extern ConVar tas_pause;
extern ConVar tas_log;

extern ConVar spt_use_separate_rng;

// Enables TAS strafing and view related functionality
class TASFeature : public FeatureWrapper<TASFeature>
{
public:
	void Strafe();
	bool WriteTemplateScript(const char* filePath, const char* saveName, std::string& errMsg);
	bool tasAddressesWereFound = false;

protected:
	virtual bool ShouldLoadFeature() override;
	virtual void LoadFeature() override;
};

extern TASFeature spt_tas;
