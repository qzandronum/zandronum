/*
#include "actor.h"
#include "gi.h"
#include "m_random.h"
#include "s_sound.h"
#include "d_player.h"
#include "a_action.h"
#include "p_local.h"
#include "p_pspr.h"
#include "gstrings.h"
#include "a_hexenglobal.h"
#include "a_weaponpiece.h"
#include "thingdef/thingdef.h"
*/

static FRandom pr_quietusdrop ("QuietusDrop");
static FRandom pr_fswordflame ("FSwordFlame");

//==========================================================================

//============================================================================
//
// A_DropQuietusPieces
//
//============================================================================

DEFINE_ACTION_FUNCTION_PARAMS(AActor, A_DropWeaponPieces)
{
	PARAM_ACTION_PROLOGUE;
	PARAM_CLASS(p1, AActor);
	PARAM_CLASS(p2, AActor);
	PARAM_CLASS(p3, AActor);

	for (int i = 0, j = 0; i < 3; ++i)
	{
		PClassActor *cls = j == 0 ?  p1 : j == 1 ? p2 : p3;
		if (cls)
		{
			AActor *piece = Spawn (cls, self->_f_Pos(), ALLOW_REPLACE);
			if (piece != NULL)
			{
				piece->Vel = self->Vel + DAngle(i*120.).ToVector(1);
				piece->flags |= MF_DROPPED;
				j = (j == 0) ? (pr_quietusdrop() & 1) + 1 : 3-j;
			}
		}
	}
	return 0;
}



// Fighter Sword Missile ----------------------------------------------------

class AFSwordMissile : public AActor
{
	DECLARE_CLASS (AFSwordMissile, AActor)
public:
	int DoSpecialDamage(AActor *victim, int damage, FName damagetype);
};

IMPLEMENT_CLASS (AFSwordMissile)

int AFSwordMissile::DoSpecialDamage(AActor *victim, int damage, FName damagetype)
{
	if (victim->player)
	{
		damage -= damage >> 2;
	}
	return damage;
}

//============================================================================
//
// A_FSwordAttack
//
//============================================================================

DEFINE_ACTION_FUNCTION(AActor, A_FSwordAttack)
{
	PARAM_ACTION_PROLOGUE;

	player_t *player;

	if (NULL == (player = self->player))
	{
		return 0;
	}
	AWeapon *weapon = self->player->ReadyWeapon;
	if (weapon != NULL)
	{
		if (!weapon->DepleteAmmo (weapon->bAltFire))
			return 0;
	}

	// [BC] Weapons are handled by the server.
	if ( NETWORK_InClientMode() )
	{
		S_Sound (self, CHAN_WEAPON, "FighterSwordFire", 1, ATTN_NORM);
		return 0;
	}

	P_SpawnPlayerMissile (self, 0, 0, -10*FRACUNIT, RUNTIME_CLASS(AFSwordMissile), self->Angles.Yaw + (45./4));
	P_SpawnPlayerMissile (self, 0, 0,  -5*FRACUNIT, RUNTIME_CLASS(AFSwordMissile), self->Angles.Yaw + (45./8));
	P_SpawnPlayerMissile (self, 0, 0,   0,		   RUNTIME_CLASS(AFSwordMissile), self->Angles.Yaw);
	P_SpawnPlayerMissile (self, 0, 0,   5*FRACUNIT, RUNTIME_CLASS(AFSwordMissile), self->Angles.Yaw - (45./8));
	P_SpawnPlayerMissile (self, 0, 0,  10*FRACUNIT, RUNTIME_CLASS(AFSwordMissile), self->Angles.Yaw - (45./4));

	// [BC] Apply spread.
	if ( player->cheats2 & CF2_SPREAD )
	{
		P_SpawnPlayerMissile (self, 0, 0, -10*FRACUNIT, RUNTIME_CLASS(AFSwordMissile), self->Angles.Yaw + (45./4) + 15);
		P_SpawnPlayerMissile (self, 0, 0,  -5*FRACUNIT, RUNTIME_CLASS(AFSwordMissile), self->Angles.Yaw + (45./8) + 15);
		P_SpawnPlayerMissile (self, 0, 0,   0,		   RUNTIME_CLASS(AFSwordMissile), self->Angles.Yaw + 15);
		P_SpawnPlayerMissile (self, 0, 0,   5*FRACUNIT, RUNTIME_CLASS(AFSwordMissile), self->Angles.Yaw - (45./8) + 15);
		P_SpawnPlayerMissile (self, 0, 0,  10*FRACUNIT, RUNTIME_CLASS(AFSwordMissile), self->Angles.Yaw - (45./4) + 15);

		P_SpawnPlayerMissile (self, 0, 0, -10*FRACUNIT, RUNTIME_CLASS(AFSwordMissile), self->Angles.Yaw + (45./4) - 15);
		P_SpawnPlayerMissile (self, 0, 0,  -5*FRACUNIT, RUNTIME_CLASS(AFSwordMissile), self->Angles.Yaw + (45./8) - 15);
		P_SpawnPlayerMissile (self, 0, 0,   0,		   RUNTIME_CLASS(AFSwordMissile), self->Angles.Yaw - 15);
		P_SpawnPlayerMissile (self, 0, 0,   5*FRACUNIT, RUNTIME_CLASS(AFSwordMissile), self->Angles.Yaw - (45./8) - 15);
		P_SpawnPlayerMissile (self, 0, 0,  10*FRACUNIT, RUNTIME_CLASS(AFSwordMissile), self->Angles.Yaw - (45./4) - 15);
	}

	S_Sound (self, CHAN_WEAPON, "FighterSwordFire", 1, ATTN_NORM);

	// [BB] If we're the server, tell the clients to play the sound.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_WeaponSound( ULONG( player - players ), "FighterSwordFire", ULONG( player - players ), SVCF_SKIPTHISCLIENT );

	return 0;
}

//============================================================================
//
// A_FSwordFlames
//
//============================================================================

DEFINE_ACTION_FUNCTION(AActor, A_FSwordFlames)
{
	PARAM_ACTION_PROLOGUE;

	int i;

	for (i = 1+(pr_fswordflame()&3); i; i--)
	{
		fixed_t xo = ((pr_fswordflame() - 128) << 12);
		fixed_t yo = ((pr_fswordflame() - 128) << 12);
		fixed_t zo = ((pr_fswordflame() - 128) << 11);
		Spawn ("FSwordFlame", self->Vec3Offset(xo, yo, zo), ALLOW_REPLACE);
	}
	return 0;
}

//============================================================================
//
// A_FighterAttack
//
//============================================================================

DEFINE_ACTION_FUNCTION(AActor, A_FighterAttack)
{
	PARAM_ACTION_PROLOGUE;

	// [Dusk] Zedek's attack is handled by the server
	if ( NETWORK_InClientMode() ) return 0;

	if (!self->target) return 0;

	angle_t angle = self->_f_angle();

	// [Dusk] Using a for-loop to avoid a copy/paste nightmare here.
	/*
	P_SpawnMissileAngle (self, RUNTIME_CLASS(AFSwordMissile), angle+ANG45/4, 0);
	P_SpawnMissileAngle (self, RUNTIME_CLASS(AFSwordMissile), angle+ANG45/8, 0);
	P_SpawnMissileAngle (self, RUNTIME_CLASS(AFSwordMissile), angle,         0);
	P_SpawnMissileAngle (self, RUNTIME_CLASS(AFSwordMissile), angle-ANG45/8, 0);
	P_SpawnMissileAngle (self, RUNTIME_CLASS(AFSwordMissile), angle-ANG45/4, 0);
	*/

	for (int i = -2; i <= 2; i++) {
		P_SpawnMissileAngle (self, RUNTIME_CLASS(AFSwordMissile), angle + (i*ANG45)/8, 0, true); // [BB] Inform clients
	}

	S_Sound (self, CHAN_WEAPON, "FighterSwordFire", 1, ATTN_NORM, true);	// [TP] Inform the clients.

	return 0;
}

