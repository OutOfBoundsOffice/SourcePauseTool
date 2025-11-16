#include "stdafx.hpp"
#ifndef OE

#include "..\feature.hpp"
#include "..\sptlib-wrapper.hpp"
#include "playerio.hpp"
#include "signals.hpp"
#include "create_collide.hpp"
#include "interfaces.hpp"

#include "vphysics_interface.h"

ConVar spt_qc_cmd("spt_qc_cmd",
                  "",
                  FCVAR_CHEAT,
                  "Execute command when quickclip occurs. "
                  "This is detected by the difference between "
                  "the quake position and the shadow position "
                  "rising above some threshold "
                  "and then teleporting to the same position\n");

ConVar spt_qc_threshold(
    "spt_qc_threshold",
    "32",
    FCVAR_CHEAT,
    "Threshold for quickclip detection, the distance between the quake hitbox and havok hitbox must exceed this before the teleport.\n");

ConVar spt_qc_maxdist_to_prev(
    "spt_qc_maxdist_to_prev",
    "200",
    FCVAR_CHEAT | FCVAR_HIDDEN,
    "Max distance to previous shadow position. This is to prevent loading a save with a havok hitbox differential causing the qc command to trigger.\n");

ConVar spt_qc_teleport_eps("spt_qc_teleport_eps",
                           "1",
                           FCVAR_CHEAT | FCVAR_HIDDEN,
                           "Quake hitbox needs to be within this many units to be considered teleported.\n");

// Execute command on quickclip
class QcCmdFeature : public FeatureWrapper<QcCmdFeature>
{
public:
	Vector previousShadowPosition;
	float previousShadowDiff;

protected:
	virtual bool ShouldLoadFeature() override;

	virtual void InitHooks() override;

	virtual void LoadFeature() override;

	virtual void UnloadFeature() override;
};

static QcCmdFeature qc_cmd;

static void Tick(bool simulating)
{
	if (!simulating || spt_qc_cmd.GetString()[0] == '\0')
	{
		return;
	}

	Vector playerPos = spt_playerio.m_vecAbsOrigin.GetValue();
	Vector havokPos = playerPos;

	IPhysicsObject* havokObj = spt_collideToMesh.GetPhysObj(utils::spt_serverEntList.GetPlayer());
	if (havokObj)
		havokObj->GetPosition(&havokPos, nullptr);

	float diffToCurrent = playerPos.DistTo(havokPos);
	float diffToPrev = playerPos.DistTo(qc_cmd.previousShadowPosition);

	if (qc_cmd.previousShadowDiff > spt_qc_threshold.GetFloat() && diffToCurrent < spt_qc_teleport_eps.GetFloat()
	    && diffToPrev < spt_qc_maxdist_to_prev.GetFloat())
	{
		EngineConCmd(spt_qc_cmd.GetString());
		spt_qc_cmd.SetValue("");
	}

	qc_cmd.previousShadowDiff = diffToCurrent;
	qc_cmd.previousShadowPosition = havokPos;
}

bool QcCmdFeature::ShouldLoadFeature()
{
	// Can't run commands without EngineConCmd
	return interfaces::engine_client != nullptr;
}

void QcCmdFeature::InitHooks() {}

void QcCmdFeature::LoadFeature()
{
	previousShadowDiff = 0.0f;
	previousShadowPosition.Zero();
	InitConcommandBase(spt_qc_cmd);
	InitConcommandBase(spt_qc_threshold);
	InitConcommandBase(spt_qc_maxdist_to_prev);
	TickSignal.Connect(Tick);
}

void QcCmdFeature::UnloadFeature() {}

#endif
