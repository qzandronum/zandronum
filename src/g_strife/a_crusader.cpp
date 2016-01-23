/*
#include "actor.h"
#include "m_random.h"
#include "a_action.h"
#include "p_local.h"
#include "p_enemy.h"
#include "s_sound.h"
#include "a_strifeglobal.h"
#include "thingdef/thingdef.h"
*/

static bool CrusaderCheckRange (AActor *self)
{
	if (self->reactiontime == 0 && P_CheckSight (self, self->target))
	{
		return self->AproxDistance (self->target) < 264*FRACUNIT;
	}
	return false;
}

DEFINE_ACTION_FUNCTION(AActor, A_CrusaderChoose)
{
	// [BC]
	AActor	*pMissile;

	// [BC] This is handled server-side.
	if ( NETWORK_InClientMode() )
	{
		return;
	}

	if (self->target == NULL)
		return;

	if (CrusaderCheckRange (self))
	{
		A_FaceTarget (self);
		self->angle -= ANGLE_180/16;
		pMissile = P_SpawnMissileZAimed (self, self->Z() + 40*FRACUNIT, self->target, PClass::FindClass("FastFlameMissile"));

		// [BC] Tell clients to spawn the missile.
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			SERVERCOMMANDS_SpawnMissile( pMissile );
	}
	else
	{
		if (P_CheckMissileRange (self))
		{
			A_FaceTarget (self);
			pMissile = P_SpawnMissileZAimed (self, self->Z() + 56*FRACUNIT, self->target, PClass::FindClass("CrusaderMissile"));

			// [BC] Tell clients to spawn the missile.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_SpawnMissile( pMissile );

			self->angle -= ANGLE_45/32;
			pMissile =P_SpawnMissileZAimed (self, self->Z() + 40*FRACUNIT, self->target, PClass::FindClass("CrusaderMissile"));

			// [BC] Tell clients to spawn the missile.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_SpawnMissile( pMissile );

			self->angle += ANGLE_45/16;
			pMissile = P_SpawnMissileZAimed (self, self->Z() + 40*FRACUNIT, self->target, PClass::FindClass("CrusaderMissile"));

			// [BC] Tell clients to spawn the missile.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_SpawnMissile( pMissile );

			self->angle -= ANGLE_45/16;
			self->reactiontime += 15;
		}

		// [BC] Set the thing's state.
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			SERVERCOMMANDS_SetThingState( self, STATE_SEE );

		self->SetState (self->SeeState);
	}
}

DEFINE_ACTION_FUNCTION(AActor, A_CrusaderSweepLeft)
{
	// [BC] This is handled server-side.
	if ( NETWORK_InClientMode() )
	{
		return;
	}

	self->angle += ANGLE_90/16;
	AActor *misl = P_SpawnMissileZAimed (self, self->Z() + 48*FRACUNIT, self->target, PClass::FindClass("FastFlameMissile"));
	if (misl != NULL)
	{
		misl->velz += FRACUNIT;

		// [BC] Tell clients to spawn the missile.
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			SERVERCOMMANDS_SpawnMissile( misl );
	}
}

DEFINE_ACTION_FUNCTION(AActor, A_CrusaderSweepRight)
{
	// [BC] This is handled server-side.
	if ( NETWORK_InClientMode() )
	{
		return;
	}

	self->angle -= ANGLE_90/16;
	AActor *misl = P_SpawnMissileZAimed (self, self->Z() + 48*FRACUNIT, self->target, PClass::FindClass("FastFlameMissile"));
	if (misl != NULL)
	{
		misl->velz += FRACUNIT;

		// [BC] Tell clients to spawn the missile.
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			SERVERCOMMANDS_SpawnMissile( misl );
	}
}

DEFINE_ACTION_FUNCTION(AActor, A_CrusaderRefire)
{
	// [Dusk] The client should not execute this.
	if( NETWORK_InClientMode() )
		return;

	if (self->target == NULL ||
		self->target->health <= 0 ||
		!P_CheckSight (self, self->target))
	{
		// [BC] Set the thing's state.
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			SERVERCOMMANDS_SetThingState( self, STATE_SEE );

		self->SetState (self->SeeState);
	}
}

DEFINE_ACTION_FUNCTION(AActor, A_CrusaderDeath)
{
	// [BC] This is handled server-side.
	if ( NETWORK_InClientMode() )
	{
		return;
	}

	if (CheckBossDeath (self))
	{
		EV_DoFloor (DFloor::floorLowerToLowest, NULL, 667, FRACUNIT, 0, -1, 0, false);
	}
}
