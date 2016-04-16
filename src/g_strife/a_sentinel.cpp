/*
#include "actor.h"
#include "p_enemy.h"
#include "a_action.h"
#include "p_local.h"
#include "m_random.h"
#include "thingdef/thingdef.h"
*/

static FRandom pr_sentinelrefire ("SentinelRefire");

DEFINE_ACTION_FUNCTION(AActor, A_SentinelBob)
{
	PARAM_ACTION_PROLOGUE;

	fixed_t minz, maxz;

	// [CW] This is handled by the server.
	if ( NETWORK_InClientMode() )
		return 0;

	if (self->flags & MF_INFLOAT)
	{
		self->vel.z = 0;

		// [CW] Moving the actor is server side.
		if ( NETWORK_GetState( ) == NETSTATE_SERVER )
			SERVERCOMMANDS_MoveThing( self, CM_VELZ );

		return 0;
	}
	if (self->threshold != 0)
		return 0;

	maxz =  self->ceilingz - self->height - 16*FRACUNIT;
	minz = self->floorz + 96*FRACUNIT;
	if (minz > maxz)
	{
		minz = maxz;
	}
	if (minz < self->Z())
	{
		self->vel.z -= FRACUNIT;
	}
	else
	{
		self->vel.z += FRACUNIT;
	}
	self->reactiontime = (minz >= self->Z()) ? 4 : 0;

	// [CW] Moving the actor is server side.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_MoveThingExact( self, CM_VELZ );

	return 0;
}

DEFINE_ACTION_FUNCTION(AActor, A_SentinelAttack)
{
	PARAM_ACTION_PROLOGUE;

	AActor *missile = NULL, *trail;


	// [BB] Without a target the P_SpawnMissileZAimed call will crash.
	if (!self->target)
	{
		return 0;
	}

	// [CW] If we aren't a client, spawn the missile.
	if ( NETWORK_InClientMode() == false )
		missile = P_SpawnMissileZAimed (self, self->Z() + 32*FRACUNIT, self->target, PClass::FindActor("SentinelFX2"), true); // [BB] Inform clients

	if (missile != NULL && (missile->vel.x | missile->vel.y) != 0)
	{
		for (int i = 8; i > 1; --i)
		{
			trail = Spawn("SentinelFX1",
				self->Vec3Angle(missile->radius*i, missile->angle, (missile->vel.z / 4 * i)), ALLOW_REPLACE);
			if (trail != NULL)
			{
				trail->target = self;
				trail->vel.x = missile->vel.x;
				trail->vel.y = missile->vel.y;
				trail->vel.z = missile->vel.z;
				P_CheckMissileSpawn (trail, self->radius);
			}
		}
		missile->AddZ(missile->vel.z >> 2);
	}
	return 0;
}

DEFINE_ACTION_FUNCTION(AActor, A_SentinelRefire)
{
	PARAM_ACTION_PROLOGUE;

	A_FaceTarget (self);

	// [CW] Clients may not do this.
	if ( NETWORK_InClientMode() )
		return 0;

	if (pr_sentinelrefire() >= 30)
	{
		if (self->target == NULL ||
			self->target->health <= 0 ||
			!P_CheckSight (self, self->target, SF_SEEPASTBLOCKEVERYTHING|SF_SEEPASTSHOOTABLELINES) ||
			P_HitFriend(self) ||
			(self->MissileState == NULL && !self->CheckMeleeRange()) ||
			pr_sentinelrefire() < 40)
		{
			// [CW] Tell clients to set the frame.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_SetThingFrame( self, self->SeeState );

			self->SetState (self->SeeState);
		}
	}
	return 0;
}
