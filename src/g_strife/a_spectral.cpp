/*
#include "actor.h"
#include "m_random.h"
#include "a_action.h"
#include "p_local.h"
#include "s_sound.h"
#include "m_random.h"
#include "a_strifeglobal.h"
#include "thingdef/thingdef.h"
*/

class ASpectralMonster : public AActor
{
	DECLARE_CLASS (ASpectralMonster, AActor)
public:
	void Touch (AActor *toucher);
};

IMPLEMENT_CLASS (ASpectralMonster)

void ASpectralMonster::Touch (AActor *toucher)
{
	P_DamageMobj (toucher, this, this, 5, NAME_Melee);
}


DEFINE_ACTION_FUNCTION(AActor, A_SpectralLightningTail)
{
	PARAM_ACTION_PROLOGUE;

	AActor *foo = Spawn("SpectralLightningHTail", self->Vec3Offset(-self->_f_velx(), -self->_f_vely(), 0), ALLOW_REPLACE);

	foo->Angles.Yaw = self->Angles.Yaw;
	foo->FriendPlayer = self->FriendPlayer;
	return 0;
}

DEFINE_ACTION_FUNCTION(AActor, A_SpectralBigBallLightning)
{
	PARAM_ACTION_PROLOGUE;

	PClassActor *cls = PClass::FindActor("SpectralLightningH3");
	if (cls)
	{
		self->Angles.Yaw += 90.;
		P_SpawnSubMissile (self, cls, self->target);
		self->Angles.Yaw += 180.;
		P_SpawnSubMissile (self, cls, self->target);
		self->Angles.Yaw -= 270.;
		P_SpawnSubMissile (self, cls, self->target);
	}
	return 0;
}

static FRandom pr_zap5 ("Zap5");

DEFINE_ACTION_FUNCTION(AActor, A_SpectralLightning)
{
	PARAM_ACTION_PROLOGUE;

	AActor *flash;

	// [CW] Clients may not do this.
	if ( NETWORK_InClientMode() )
		return 0;

	if (self->threshold != 0)
		--self->threshold;

	self->Vel.X += pr_zap5.Random2(3);
	self->Vel.Y += pr_zap5.Random2(3);

	// [CW] Tell clients to spawn the actor.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_MoveThingExact( self, CM_VELX|CM_VELY );

	fixedvec2 pos = self->Vec2Offset(
		pr_zap5.Random2(3) * FRACUNIT * 50,
		pr_zap5.Random2(3) * FRACUNIT * 50);

	flash = Spawn (self->threshold > 25 ? PClass::FindActor(NAME_SpectralLightningV2) :
		PClass::FindActor(NAME_SpectralLightningV1), pos.x, pos.y, ONCEILINGZ, ALLOW_REPLACE);

	flash->target = self->target;
	flash->Vel.Z = -18*FRACUNIT;
	flash->FriendPlayer = self->FriendPlayer;

	flash = Spawn(NAME_SpectralLightningV2, self->_f_X(), self->_f_Y(), ONCEILINGZ, ALLOW_REPLACE);

	// [CW] Tell clients to spawn the actor.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_SpawnMissile( flash );


	flash->target = self->target;
	flash->Vel.Z = -18 * FRACUNIT;
	flash->FriendPlayer = self->FriendPlayer;
	return 0;

	// [CW] Tell clients to spawn the missile.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_SpawnMissile( flash );
}
// In Strife, this number is stored in the data segment, but it doesn't seem to be
// altered anywhere.
#define TRACEANGLE (19.6875)

DEFINE_ACTION_FUNCTION(AActor, A_Tracer2)
{
	PARAM_ACTION_PROLOGUE;

	AActor *dest;
	fixed_t dist;
	fixed_t slope;

	// [BC] Server takes care of movement.
	if ( NETWORK_InClientMode() )
	{
		return 0;
	}

	dest = self->tracer;

	if (!dest || dest->health <= 0 || self->Speed == 0 || !self->CanSeek(dest))
		return 0;

	DAngle exact = self->AngleTo(dest);
	DAngle diff = deltaangle(self->Angles.Yaw, exact);

	if (diff < 0)
	{
		self->Angles.Yaw -= TRACEANGLE;
		if (deltaangle(self->Angles.Yaw, exact) > 0)
			self->Angles.Yaw = exact;
	}
	else if (diff > 0)
	{
		self->Angles.Yaw += TRACEANGLE;
		if (deltaangle(self->Angles.Yaw, exact) < 0.)
			self->Angles.Yaw = exact;
	}

	self->VelFromAngle();

	if (!(self->flags3 & (MF3_FLOORHUGGER|MF3_CEILINGHUGGER)))
	{
		// change slope
		dist = self->AproxDistance (dest) / self->_f_speed();

		if (dist < 1)
		{
			dist = 1;
		}
		if (dest->_f_height() >= 56*FRACUNIT)
		{
			slope = (dest->_f_Z()+40*FRACUNIT - self->_f_Z()) / dist;
		}
		else
		{
			slope = (dest->_f_Z() + self->_f_height()*2/3 - self->_f_Z()) / dist;
		}
		if (slope < self->_f_velz())
		{
			self->Vel.Z -= 1 / 8.;
		}
		else
		{
			self->Vel.Z += 1 / 8.;
		}
	}

	// [BC] Update the thing's position, angle and velocity.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_MoveThingExact( self, CM_X|CM_Y|CM_Z|CM_ANGLE|CM_VELX|CM_VELY|CM_VELZ );

	return 0;
}
