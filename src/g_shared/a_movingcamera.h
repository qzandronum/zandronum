#ifndef __A_MOVINGCAMERA__
#define __A_MOVINGCAMERA__

#include "actor.h"
// [BB] New #includes.
#include "network.h"
#include "serializer.h"

class AInterpolationPoint : public AActor
{
	DECLARE_CLASS (AInterpolationPoint, AActor)
	HAS_OBJECT_POINTERS
public:
	void BeginPlay ();
	void HandleSpawnFlags ();
	void Tick () {}		// Nodes do no thinking
	AInterpolationPoint *ScanForLoop ();
	void FormChain ();

	
	void Serialize(FSerializer &arc);

	TObjPtr<AInterpolationPoint> Next;
};

class APathFollower : public AActor
{
	DECLARE_CLASS (APathFollower, AActor)
	HAS_OBJECT_POINTERS
public:
	void BeginPlay ();
	void PostBeginPlay ();
	void Tick ();
	void Activate (AActor *activator);
	void Deactivate (AActor *activator);
protected:
	double Splerp (double p1, double p2, double p3, double p4);
	double Lerp (double p1, double p2);
	virtual bool Interpolate ();
	virtual void NewNode ();

	
	void Serialize(FSerializer &arc);

	bool bActive, bJustStepped;
	TObjPtr<AInterpolationPoint> PrevNode, CurrNode;
	float Time;		// Runs from 0.0 to 1.0 between CurrNode and CurrNode->Next
	int HoldTime;

	bool bPostBeginPlayCalled;
	bool bActivateCalledBeforePostBeginPlay;

	// [EP] TODO: remove the 'l' mark from the name of the variables which aren't LONG anymore
	int lServerPrevNodeId, lServerCurrNodeId;
	float fServerTime;
public:
	bool IsActive () const;
	void SyncWithClient ( const ULONG ulClient );
	static void InitFromStream ( BYTESTREAM_s *pByteStream );
};

#endif
