/*
#include "actor.h"
#include "info.h"
#include "m_random.h"
#include "p_local.h"
#include "p_enemy.h"
#include "s_sound.h"
#include "statnums.h"
#include "a_specialspot.h"
#include "thingdef/thingdef.h"
#include "doomstat.h"
#include "g_level.h"
*/

static FRandom pr_brainscream ("BrainScream");
static FRandom pr_brainexplode ("BrainExplode");
static FRandom pr_spawnfly ("SpawnFly");

DEFINE_ACTION_FUNCTION(AActor, A_BrainAwake)
{
	PARAM_ACTION_PROLOGUE;
	// killough 3/26/98: only generates sound now
	S_Sound (self, CHAN_VOICE, "brain/sight", 1, ATTN_NONE);
	return 0;
}

DEFINE_ACTION_FUNCTION(AActor, A_BrainPain)
{
	PARAM_ACTION_PROLOGUE;
	S_Sound (self, CHAN_VOICE, "brain/pain", 1, ATTN_NONE);
	return 0;
}

static void BrainishExplosion (fixed_t x, fixed_t y, fixed_t z)
{
	AActor *boom = Spawn("Rocket", x, y, z, NO_REPLACE);
	if (boom != NULL)
	{
		boom->DeathSound = "misc/brainexplode";
		boom->vel.z = pr_brainscream() << 9;

		PClassActor *cls = PClass::FindActor("BossBrain");
		if (cls != NULL)
		{
			FState *state = cls->FindState(NAME_Brainexplode);
			if (state != NULL)
				boom->SetState (state);
		}
		boom->effects = 0;
		boom->Damage = NULL;	// disables collision detection which is not wanted here
		boom->tics -= pr_brainscream() & 7;
		if (boom->tics < 1)
			boom->tics = 1;
	}
}

DEFINE_ACTION_FUNCTION(AActor, A_BrainScream)
{
	PARAM_ACTION_PROLOGUE;
	fixed_t x;
		
	for (x = self->X() - 196*FRACUNIT; x < self->X() + 320*FRACUNIT; x += 8*FRACUNIT)
	{
		BrainishExplosion (x, self->Y() - 320*FRACUNIT,
			128 + (pr_brainscream() << (FRACBITS + 1)));
	}
	S_Sound (self, CHAN_VOICE, "brain/death", 1, ATTN_NONE);
	return 0;
}

DEFINE_ACTION_FUNCTION(AActor, A_BrainExplode)
{
	PARAM_ACTION_PROLOGUE;
	fixed_t x = self->X() + pr_brainexplode.Random2()*2048;
	fixed_t z = 128 + pr_brainexplode()*2*FRACUNIT;
	BrainishExplosion (x, self->Y(), z);
	return 0;
}

DEFINE_ACTION_FUNCTION(AActor, A_BrainDie)
{
	PARAM_ACTION_PROLOGUE;

	// [RH] If noexit, then don't end the level.
	// [BC] teamgame
	if ((deathmatch || teamgame || alwaysapplydmflags) && (dmflags & DF_NO_EXIT))
		return 0;

	// New dmflag: Kill all boss spawned monsters before ending the level.
	if (dmflags2 & DF2_KILLBOSSMONST)
	{
		int count;	// Repeat until we have no more boss-spawned monsters.
		do			// (e.g. Pain Elementals can spawn more to kill upon death.)
		{
			TThinkerIterator<AActor> it;
			AActor *mo;
			count = 0;
			while ((mo = it.Next()))
			{
				if (mo->health > 0 && mo->flags4 & MF4_BOSSSPAWNED)
				{
					P_DamageMobj(mo, self, self, mo->health, NAME_None, 
						DMG_NO_ARMOR|DMG_FORCED|DMG_THRUSTLESS|DMG_NO_FACTOR);
					count++;
				}
			}
		} while (count != 0);
	}

	G_ExitLevel (0, false);
	return 0;
}

DEFINE_ACTION_FUNCTION_PARAMS(AActor, A_BrainSpit)
{
	PARAM_ACTION_PROLOGUE;
	PARAM_CLASS_OPT(spawntype, AActor) { spawntype = NULL; }

	DSpotState *state = DSpotState::GetSpotState();
	AActor *targ;
	AActor *spit;
	bool isdefault = false;

	// [BC] Brain spitting is server-side.
	if ( NETWORK_InClientMode() )
	{
		return 0;
	}

	// shoot a cube at current target
	targ = state->GetNextInList(PClass::FindClass("BossTarget"), G_SkillProperty(SKILLP_EasyBossBrain));

	if (targ != NULL)
	{
		if (spawntype == NULL) 
		{
			spawntype = PClass::FindActor("SpawnShot");
			isdefault = true;
		}

		// spawn brain missile
		spit = P_SpawnMissile (self, targ, spawntype);

		if (spit != NULL)
		{
			// Boss cubes should move freely to their destination so it's
			// probably best to disable all collision detection for them.
			if (spit->flags & MF_NOCLIP) spit->flags5 |= MF5_NOINTERACTION;
	
			spit->target = targ;
			spit->master = self;
			// [RH] Do this correctly for any trajectory. Doom would divide by 0
			// if the target had the same y coordinate as the spitter.
			if ((spit->vel.x | spit->vel.y) == 0)
			{
				spit->special2 = 0;
			}
			else if (abs(spit->vel.y) > abs(spit->vel.x))
			{
				spit->special2 = (targ->Y() - self->Y()) / spit->vel.y;
			}
			else
			{
				spit->special2 = (targ->X() - self->X()) / spit->vel.x;
			}
			// [GZ] Calculates when the projectile will have reached destination
			spit->special2 += level.maptime;
			spit->flags6 |= MF6_BOSSCUBE;

			// [BC] If we're the server, tell clients to spawn the actor.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_SpawnMissile( spit );
		}

		if (!isdefault)
		{
			S_Sound(self, CHAN_WEAPON, self->AttackSound, 1, ATTN_NONE);

			// [BC] If we're the server, tell clients create the sound.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_SoundPoint( self->Pos(), CHAN_WEAPON, self->AttackSound, 1, ATTN_NONE );
		}
		else
		{
			// compatibility fallback
			S_Sound (self, CHAN_WEAPON, "brain/spit", 1, ATTN_NONE);

			// [BC] If we're the server, tell clients create the sound.
			if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				SERVERCOMMANDS_SoundPoint( self->Pos(), CHAN_WEAPON, "brain/spit", 1, ATTN_NONE );
		}
	}
	return 0;
}

static void SpawnFly(AActor *self, PClassActor *spawntype, FSoundID sound)
{
	AActor *newmobj;
	AActor *fog = NULL;
	AActor *eye = self->master; // The eye is the spawnshot's master, not the target!
	AActor *targ = self->target; // Unlike other projectiles, the target is the intended destination.
	int r;
		
	// [BC] Brain spitting is server-side.
	if ( NETWORK_InClientMode() )
	{
		return;
	}

	// [GZ] Should be more viable than a countdown...
	if (self->special2 != 0)
	{
		if (self->special2 > level.maptime)
			return;		// still flying
	}
	else
	{
		if (self->reactiontime == 0 || --self->reactiontime != 0)
			return;		// still flying
	}
	
	if (spawntype != NULL)
	{
		fog = Spawn (spawntype, targ->Pos(), ALLOW_REPLACE);
		if (fog != NULL) S_Sound (fog, CHAN_BODY, sound, 1, ATTN_NORM);
	}

	// [BC] If we're the server, spawn the fire, and tell clients to play the sound.
	if (( NETWORK_GetState( ) == NETSTATE_SERVER ) && ( fog ))
	{
		SERVERCOMMANDS_SpawnThing( fog );
		SERVERCOMMANDS_SoundPoint( fog->Pos(), CHAN_BODY, S_GetName( sound ), 1, ATTN_NORM );
	}

	FName SpawnName;

	DDropItem *di;   // di will be our drop item list iterator
	DDropItem *drop; // while drop stays as the reference point.
	int n = 0;

	// First see if this cube has its own actor list
	drop = self->GetDropItems();

	// If not, then default back to its master's list
	if (drop == NULL && eye != NULL)
		drop = eye->GetDropItems();

	if (drop != NULL)
	{
		for (di = drop; di != NULL; di = di->Next)
		{
			if (di->Name != NAME_None)
			{
				if (di->Amount < 0)
				{
					di->Amount = 1; // default value is -1, we need a positive value.
				}
				n += di->Amount; // this is how we can weight the list.
			}
		}
		di = drop;
		n = pr_spawnfly(n);
		while (n >= 0)
		{
			if (di->Name != NAME_None)
			{
				n -= di->Amount; // logically, none of the -1 values have survived by now.
			}
			if ((di->Next != NULL) && (n >= 0))
			{
				di = di->Next;
			}
			else
			{
				n = -1;
			}
		}
		SpawnName = di->Name;
	}
	if (SpawnName == NAME_None)
	{
		// Randomly select monster to spawn.
		r = pr_spawnfly ();

		// Probability distribution (kind of :),
		// decreasing likelihood.
			 if (r < 50)  SpawnName = "DoomImp";
		else if (r < 90)  SpawnName = "Demon";
		else if (r < 120) SpawnName = "Spectre";
		else if (r < 130) SpawnName = "PainElemental";
		else if (r < 160) SpawnName = "Cacodemon";
		else if (r < 162) SpawnName = "Archvile";
		else if (r < 172) SpawnName = "Revenant";
		else if (r < 192) SpawnName = "Arachnotron";
		else if (r < 222) SpawnName = "Fatso";
		else if (r < 246) SpawnName = "HellKnight";
		else			  SpawnName = "BaronOfHell";
	}
	spawntype = PClass::FindActor(SpawnName);
	if (spawntype != NULL)
	{
		newmobj = Spawn (spawntype, targ->Pos(), ALLOW_REPLACE);
		if (newmobj != NULL)
		{
			// Make the new monster hate what the boss eye hates
			if (eye != NULL)
			{
				newmobj->CopyFriendliness (eye, false);
			}
			// Make it act as if it was around when the player first made noise
			// (if the player has made noise).
			newmobj->LastHeard = newmobj->Sector->SoundTarget;

			if (newmobj->SeeState != NULL && P_LookForPlayers (newmobj, true, NULL))
			{
				newmobj->SetState (newmobj->SeeState);
			}
			if (!(newmobj->ObjectFlags & OF_EuthanizeMe))
			{
				// telefrag anything in this spot
				P_TeleportMove (newmobj, newmobj->Pos(), true);

				// [BC] If we're the server, tell clients to spawn the new monster.
				if ( NETWORK_GetState( ) == NETSTATE_SERVER )
				{
					SERVERCOMMANDS_SpawnThing( newmobj );
					if ( newmobj->state == newmobj->SeeState )
						SERVERCOMMANDS_SetThingState( newmobj, STATE_SEE );
				}
			}
			newmobj->flags4 |= MF4_BOSSSPAWNED;
		}
	}

	// [BC] Tell clients to destroy the cube.
	if ( NETWORK_GetState( ) == NETSTATE_SERVER )
		SERVERCOMMANDS_DestroyThing( self );

	// remove self (i.e., cube).
	self->Destroy ();
}

DEFINE_ACTION_FUNCTION_PARAMS(AActor, A_SpawnFly)
{
	PARAM_ACTION_PROLOGUE;
	PARAM_CLASS_OPT	(spawntype, AActor)	{ spawntype = NULL; }

	FSoundID sound;

	if (spawntype != NULL) 
	{
		sound = GetDefaultByType(spawntype)->SeeSound;
	}
	else
	{
		spawntype = PClass::FindActor("SpawnFire");
		sound = "brain/spawn";
	}
	SpawnFly(self, spawntype, sound);
	return 0;
}

// travelling cube sound
DEFINE_ACTION_FUNCTION(AActor, A_SpawnSound)
{
	PARAM_ACTION_PROLOGUE;
	S_Sound (self, CHAN_BODY, "brain/cube", 1, ATTN_IDLE);
	SpawnFly(self, PClass::FindActor("SpawnFire"), "brain/spawn");
	return 0;
}
