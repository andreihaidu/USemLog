// Copyright 2017-2019, Institute for Artificial Intelligence - University of Bremen
// Author: Andrei Haidu (http://haidu.eu)

#pragma once

#include "Components/MeshComponent.h"
#include "Components/ShapeComponent.h"
#include "SLStructs.h"
#include "TimerManager.h"
#include "SLContactShapeInterface.generated.h"

/**
 * Structure holding the OverlapEnd event data,
 * cached for a small period of time in case it should be concatenated with the follow-up event
 */
struct FSLOverlapEndEvent
{
	// Default ctor
	FSLOverlapEndEvent() = default;

	// Init ctor
	FSLOverlapEndEvent(UPrimitiveComponent* InOtherComp, const FSLEntity& InOtherItem, float InTime) :
		OtherComp(InOtherComp), OtherItem(InOtherItem), Time(InTime) {};

	// Overlap component
	UPrimitiveComponent* OtherComp;
	
	// Other item of the overlap end
	FSLEntity OtherItem;

	// Time
	float Time;
};


/** Notiy the begin/end of a supported by event */
DECLARE_MULTICAST_DELEGATE_FourParams(FSLBeginSupportedBySignature, const FSLEntity& /*Supported*/, const FSLEntity& /*Supporting*/, float /*Time*/, const uint64 /*PairId*/);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FSLEndSupportedBySignature, const uint64 /*PairId1*/, const uint64 /*PairId2*/, float /*Time*/);

/**
 *  Unreal style interface for the contact shapes 
 */
UINTERFACE(Blueprintable)
class USLContactShapeInterface : public UInterface
{
	GENERATED_BODY()
};

class ISLContactShapeInterface
{
	GENERATED_BODY()

public:
	// Initialize trigger area for runtime, check if outer is valid and semantically annotated
	virtual void Init(bool bLogSupportedByEvents = true) = 0;

	// Start publishing overlap events, trigger currently overlapping objects
	virtual void Start() = 0;

	// Stop publishing overlap events
	void Finish(bool bForced = false);

	// Get init state
	bool IsInit() const { return bIsInit; };

	// Get started state
	bool IsStarted() const { return bIsStarted; };

	// Get finished state
	bool IsFinished() const { return bIsFinished; };

	// True if parent is supported by a surface
	bool IsSupportedBySomething() const { return IsSupportedByPariIds.Num() != 0; };

	// Get the last supported by something time
	float GetLastSupportedByEndTime() const { return PrevSupportedByEndTime; }

	// Get the world
	UWorld* GetWorldFromShape() const { return World; };
	
#if WITH_EDITOR
	// Update bounds visual (red/green -- parent is not/is semantically annotated)
	// it is public so it can be accessed from the editor panel for updates
	virtual void UpdateVisualColor() = 0;
#endif // WITH_EDITOR

protected:
#if WITH_EDITOR
	// Load and apply cached parameters from tags
	virtual bool LoadShapeBounds() = 0;

	// Calculate and apply trigger area size
	virtual bool CalcShapeBounds() = 0;

	// Save current parameters to tags
	virtual bool StoreShapeBounds() = 0;
#endif // WITH_EDITOR

	// Init interface
	bool InitInterface(UShapeComponent* InShapeComponent, UWorld* InWorld);

	// Publish currently overlapping components
	void TriggerInitialOverlaps();

	// Start checking for supported by events
	void StartSupportedByUpdateCheck();

	// Check for supported by events
	void SupportedByUpdateCheckBegin();

	// Check if Other is a supported by candidate
	bool CheckAndRemoveIfJustCandidate(UObject* InOther);

	// Event called when something starts to overlaps this component
	UFUNCTION()
	virtual void OnOverlapBegin(UPrimitiveComponent* OverlappedComp,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	// Event called when something stops overlapping this component 
	UFUNCTION()
	virtual void OnOverlapEnd(UPrimitiveComponent* OverlappedComp,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	// Delayed call of sending the finished event to check for possible concatenation of jittering events of the same type
	void DelayedOverlapEndEventCallback();

	// Broadcast delayed overlaps, if curr time < 0, it guarantees a publish
	bool PublishDelayedOverlapEndEvent(const FSLOverlapEndEvent& Ev, float CurrTime = -1.f);

	// Skip publishing overlap event if it can be concatenated with the current event start
	bool SkipOverlapEndEventBroadcast(const FSLEntity& InItem, float StartTime);
	
public:
	// Event called when a semantic overlap begins / ends
	FSLBeginContactSignature OnBeginSLContact;
	FSLEndContactSignature OnEndSLContact;

	// Called when a supported by event begins / ends
	FSLBeginSupportedBySignature OnBeginSLSupportedBy;
	FSLEndSupportedBySignature OnEndSLSupportedBy;
	
protected:
	// True if initialized
	bool bIsInit;

	// True if started
	bool bIsStarted;

	// True if finished
	bool bIsFinished;

	// Array of events id of objects currently supporting this item, used for checking if this object is supported by any suface(s)
	TArray<uint64> IsSupportedByPariIds;

	// Last supported by time
	float PrevSupportedByEndTime;

	// Pointer to the world
	UWorld* World;

	// Pointer to the given shape component
	UShapeComponent* ShapeComponent;

	// Pointer to the outer (owner) mesh component 
	UMeshComponent* OwnerMeshComp;

	// Semantic data of the owner
	FSLEntity SemanticOwner;

	// Include supported by events
	bool bLogSupportedByEvents;

	// SupportedBy contact candidates
	TArray<FSLContactResult> SBCandidates;
	
	// Supported by event update timer handle
	FTimerHandle SBTimerHandle;

	// Allow binding against non-UObject functions
	FTimerDelegate SBTimerDelegate;

	// Send finished events with a delay to check for possible concatenation of equal and consecutive events with small time gaps in between
	FTimerHandle DelayTimerHandle;

	// Can only bind the timer handle to UObjects or FTimerDelegates
	FTimerDelegate DelayTimerDelegate;

	// Array of recently ended overlaps
	TArray<FSLOverlapEndEvent> RecentlyEndedOverlapEvents;

	/* Constants */
	constexpr static const char* TagTypeName = "SemLogColl";
	constexpr static float SBUpdateRate = 0.11f;
	constexpr static float SBMaxVertSpeed = 0.5f;
	constexpr static float MaxOverlapEventTimeGap = 0.12f;
};