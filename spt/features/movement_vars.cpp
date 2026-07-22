#include "stdafx.hpp"
#include "..\feature.hpp"
#include "cmodel.h"
#include "SDK\hl_movedata.h"
#include "convar.hpp"
#include "interfaces.hpp"
#include "signals.hpp"
#include "visualizations\imgui\imgui_interface.hpp"

static void Change_Maxspeed(CON_COMMAND_CALLBACK_ARGS);

static ConVar* _cl_forwardspeed = nullptr;
static ConVar* _cl_sidespeed = nullptr;
static ConVar* _cl_backspeed = nullptr;
static ConVar* _sv_maxspeed_cvar = nullptr;

ConVar spt_player_maxspeed(
    "spt_player_maxspeed",
    "0",
    FCVAR_DEMO,
    "If nonzero, set player maxspeed to this value. When changed also changes cl_forwardspeed/cl_sidespeed/cl_backspeed/sv_maxspeed to match this value.",
    Change_Maxspeed);

static void Change_Maxspeed(CON_COMMAND_CALLBACK_ARGS)
{
#define SET_DEFAULT(cvar) cvar->SetValue(cvar->GetFloat())
	float maxspeed = spt_player_maxspeed.GetFloat();
	if (maxspeed == 0.0f)
	{
		SET_DEFAULT(_cl_forwardspeed);
		SET_DEFAULT(_cl_sidespeed);
		SET_DEFAULT(_cl_backspeed);
		SET_DEFAULT(_sv_maxspeed_cvar);
	}
	else
	{
		_cl_forwardspeed->SetValue(maxspeed);
		_cl_sidespeed->SetValue(maxspeed);
		_cl_backspeed->SetValue(maxspeed);
		_sv_maxspeed_cvar->SetValue(maxspeed);
	}
}

// Some logging stuff that can be turned on with tas_log 1
class VarChanger : public FeatureWrapper<VarChanger>
{
public:
protected:
	virtual bool ShouldLoadFeature() override;
	virtual void LoadFeature() override;

private:
	static void ProcessMovementPre(void* pPlayer, void* pMove);
};

static VarChanger spt_vars;

bool VarChanger::ShouldLoadFeature()
{
	return true;
}

void VarChanger::LoadFeature()
{
	if (g_pCVar)
	{
		_cl_forwardspeed = g_pCVar->FindVar("cl_forwardspeed");
		_cl_sidespeed = g_pCVar->FindVar("cl_sidespeed");
		_cl_backspeed = g_pCVar->FindVar("cl_backspeed");
		_sv_maxspeed_cvar = g_pCVar->FindVar("sv_maxspeed");
	}

	if (ProcessMovementPre_Signal.Works && _cl_forwardspeed && _cl_sidespeed && _cl_backspeed && _sv_maxspeed_cvar)
	{
		ProcessMovementPre_Signal.Connect(ProcessMovementPre);
		InitConcommandBase(spt_player_maxspeed);
		SptImGuiGroup::Cheats_Misc_MaxSpeed.RegisterUserCallback(
		    []()
		    { SptImGui::CvarDouble(spt_player_maxspeed, "override max speed value", "enter float value"); });
	}
}

void VarChanger::ProcessMovementPre(void* pPlayer, void* pMove)
{
	CHLMoveData* mv = reinterpret_cast<CHLMoveData*>(pMove);
	float maxspeed = spt_player_maxspeed.GetFloat();
	if (maxspeed != 0)
	{
		mv->m_flClientMaxSpeed = mv->m_flMaxSpeed = maxspeed;
	}
}
