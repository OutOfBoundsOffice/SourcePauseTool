//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//

#if !defined( IGAMEMOVEMENT_H )
#define IGAMEMOVEMENT_H

#ifdef _WIN32
#pragma once
#endif

#if defined( OE )
#include "vector.h"
#else
#include "mathlib/vector.h"
#endif

#include "utils/game_detection.hpp"
#include "interface.h"
#include "imovehelper.h"
#include "const.h"

//-----------------------------------------------------------------------------
// Name of the class implementing the game movement.
//-----------------------------------------------------------------------------

#define INTERFACENAME_GAMEMOVEMENT	"GameMovement001"

//-----------------------------------------------------------------------------
// Forward declarations.
//-----------------------------------------------------------------------------

class IMoveHelper;

//-----------------------------------------------------------------------------
// Purpose: Encapsulated input parameters to player movement.
//-----------------------------------------------------------------------------

class CMoveData
{
public:
	bool			m_bFirstRunOfFunctions : 1;
	bool			m_bGameCodeMovedPlayer : 1;

	EntityHandle_t	m_nPlayerHandle;	// edict index on server, client entity handle on client

	int				m_nImpulseCommand;	// Impulse command issued.
	QAngle			m_vecViewAngles;	// Command view angles (local space)
	QAngle			m_vecAbsViewAngles;	// Command view angles (world space)
	int				m_nButtons;			// Attack buttons.
	int				m_nOldButtons;		// From host_client->oldbuttons;
	float			m_flForwardMove;
#if BMS
	int UNKNOWN001;
#endif
	float			m_flSideMove;
	float			m_flUpMove;
	
	float			m_flMaxSpeed;
	float			m_flClientMaxSpeed;

	// Variables from the player edict (sv_player) or entvars on the client.
	// These are copied in here before calling and copied out after calling.
	Vector			m_vecVelocity;		// edict::velocity		// Current movement direction.
	QAngle			m_vecAngles;		// edict::angles
	QAngle			m_vecOldAngles;
	
// Output only
	float			m_outStepHeight;	// how much you climbed this move
	Vector			m_outWishVel;		// This is where you tried 
	Vector			m_outJumpVel;		// This is your jump velocity

	// Movement constraints	(radius 0 means no constraint)
	Vector			m_vecConstraintCenter;
	float			m_flConstraintRadius;
	float			m_flConstraintWidth;
	float			m_flConstraintSpeedFactor;

	void			SetAbsOrigin( const Vector &vec );
	const Vector	&GetAbsOrigin() const;

private:
	Vector			m_vecAbsOrigin;		// edict::origin
};

inline const Vector &CMoveData::GetAbsOrigin() const
{
#ifdef OE
	return reinterpret_cast<const Vector&>(m_outStepHeight);
#elif SSDK2007
	if (utils::GetBuildNumber() <= 4104) {
		return reinterpret_cast<const Vector&>(m_outStepHeight);
	}
	else {
		return m_vecAbsOrigin;
	}
#else
	return m_vecAbsOrigin;
#endif

}

#if !defined( CLIENT_DLL ) && defined( _DEBUG )
// We only ever want this code path on the server side in a debug build
//  and you have to uncomment the code below and rebuild to have the test operate.
//#define PLAYER_GETTING_STUCK_TESTING

#endif

#if !defined( PLAYER_GETTING_STUCK_TESTING )

// This is implemented with a more exhaustive test in gamemovement.cpp.  We check if the origin being requested is
//  inside solid, which it never should be
inline void CMoveData::SetAbsOrigin( const Vector &vec )
{
	m_vecAbsOrigin = vec;
}

#endif


#endif // IGAMEMOVEMENT_H
