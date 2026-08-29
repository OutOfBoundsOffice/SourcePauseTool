#include "stdafx.hpp"
#include "..\feature.hpp"
#include "playerio.hpp"
#include "cmodel.h"
#include "SDK\hl_movedata.h"
#include "convar.hpp"
#include "interfaces.hpp"
#include "signals.hpp"

ConVar tas_log("tas_log", "0", 0, "If enabled, dumps a whole bunch of different stuff into the console.");

// Some logging stuff that can be turned on with tas_log 1
class TASLogging : public FeatureWrapper<TASLogging>
{
public:
protected:
	virtual bool ShouldLoadFeature() override;
	virtual void LoadFeature() override;

private:
	static void ProcessMovementPre(void* pPlayer, void* pMove);
	static void ProcessMovementPost(void* pPlayer, void* pMove);
};

static TASLogging spt_taslogging;

bool TASLogging::ShouldLoadFeature()
{
	return true;
}

void TASLogging::LoadFeature() 
{
	if (ProcessMovementPre_Signal.Works && ProcessMovementPost_Signal.Works)
	{
		ProcessMovementPre_Signal.Connect(ProcessMovementPre);
		ProcessMovementPost_Signal.Connect(ProcessMovementPost);
		InitConcommandBase(tas_log);
	}
}

void TASLogging::ProcessMovementPre(void* pPlayer, void* pMove)
{
	CHLMoveData* mv = reinterpret_cast<CHLMoveData*>(pMove);
	Vector v = spt_playerio.m_vecAbsOrigin.GetValue();
	if (tas_log.GetBool())
		DevMsg("[ProcessMovement PRE ] origin: %.8f %.8f %.8f; velocity: %.8f %.8f %.8f\n",
			v.x,
			v.y,
			v.z,
			mv->m_vecVelocity.x,
			mv->m_vecVelocity.y,
			mv->m_vecVelocity.z);
}

void TASLogging::ProcessMovementPost(void* pPlayer, void* pMove)
{
	CHLMoveData* mv = reinterpret_cast<CHLMoveData*>(pMove);
	Vector v = spt_playerio.m_vecAbsOrigin.GetValue();
	if (tas_log.GetBool())
		DevMsg("[ProcessMovement POST] origin: %.8f %.8f %.8f; velocity: %.8f %.8f %.8f\n",
			v.x,
			v.y,
			v.z,
			mv->m_vecVelocity.x,
			mv->m_vecVelocity.y,
			mv->m_vecVelocity.z);
}
