// Copyright 2017-2019, Institute for Artificial Intelligence - University of Bremen
// Author: Andrei Haidu (http://haidu.eu)

#pragma once

#include "CoreMinimal.h"
#include "SLStructs.generated.h"

/************************************************************************/
/*                       STRUCTS                                        */
/************************************************************************/
/**
* Structure holding the semantic data of an entity
*/
struct FSLEntity
{
	// UObject of entity
	UObject* Obj = nullptr;

	// Semantic id of entity
	FString Id;

	// Semantic class of entity
	FString Class;

	// Visual mask of the entity
	FString VisualMask;

	// Default constructor
	FSLEntity() = default;

	// Init constructor
	FSLEntity(UObject* InObj, const FString& InId, const FString& InClass) :
		Obj(InObj), Id(InId), Class(InClass) {};

	// Init ctor with visual mask
	FSLEntity(UObject* InObj, const FString& InId, const FString& InClass, const FString& InVisualMask) :
		Obj(InObj), Id(InId), Class(InClass), VisualMask(InVisualMask) {};
	
	// Set data
	void Set(UObject* InObj, const FString& InId, const FString& InClass)
	{
		Obj = InObj;
		Id = InId;
		Class = InClass;
	}

	// Set data with visual mask
	void Set(UObject* InObj, const FString& InId, const FString& InClass, const FString& InVisualMask)
	{
		Obj = InObj;
		Id = InId;
		Class = InClass;
		VisualMask = InVisualMask;
	}

	// Clear
	void Clear()
	{
		Obj = nullptr;
		Id = "";
		Class = "";
		VisualMask = "";
	}

	// True if the unique id, the semantic id and the semantic class is not empty
	bool IsSet() const { return Obj != nullptr && !Id.IsEmpty() && !Class.IsEmpty(); }

	// Check if visual is set
	bool HasVisualMask() const { return !VisualMask.IsEmpty(); }
	
	// Get result as string
	FString ToString() const
	{
		return FString::Printf(TEXT("UniqueID:%ld Id:%s Class:%s"), Obj->GetUniqueID(), *Id, *Class);
	}

	// Compares only the pointers of the UObject (should work in all cases)
	FORCEINLINE bool EqualsFast(const FSLEntity& Other) const
	{
		if(Obj == nullptr || Other.Obj == nullptr)
		{
			return false;
		}
		return Obj == Other.Obj;
	}

	// Compares the UObject pointers inlcuding the Id and Class
	FORCEINLINE bool Equals(const FSLEntity& Other) const
	{
		return Obj == Other.Obj && Id.Equals(Other.Id) && Class.Equals(Other.Class);
	}
};

/**
* Structure holding the semantic data of two entities
*/
struct FSLEntityPair
{
	// First entity
	FSLEntity Entity1;

	// Second entity
	FSLEntity Entity2;

	// Default constructor
	FSLEntityPair() {};

	// Init constructor
	FSLEntityPair(const FSLEntity& InEntity1, const FSLEntity& InEntity2) : Entity1(InEntity1), Entity2(InEntity2) {};

	// True if the unique id, the semantic id and the semantic class is not empty
	bool IsSet() const { return Entity1.IsSet() && Entity2.IsSet(); }

	// Get result as string
	FString ToString() const
	{
		return FString::Printf(TEXT("%s %s"), *Entity1.ToString(), *Entity2.ToString());
	}
};


/**
* Templated data structure of entities with semantic and previous transform information
*/
// TODO remove the Obj pointer, and use the one from entity, add a IsValid test to make sure the Obj can have a location
template <typename T>
struct TSLEntityPreviousPose
{
	// Pointer to the actor/component/skeletal actor, the object should have a transform
	TWeakObjectPtr<T> Obj;

	// The semantically annotated entity
	FSLEntity Entity;

	// Its previous location
	FVector PrevLoc;

	// Its previous rotation
	FQuat PrevQuat;

	// Default constructor
	TSLEntityPreviousPose() {};

	// Init constructor
	TSLEntityPreviousPose(TWeakObjectPtr<T> InObj,
		const FSLEntity& InEntity,
		FVector InPrevLoc = FVector(BIG_NUMBER),
		FQuat InPrevQuat = FQuat::Identity) :
		Obj(InObj),
		Entity(InEntity),
		PrevLoc(InPrevLoc),
		PrevQuat(InPrevQuat)
	{};

	// Check if the entity is valid and has a transform
	bool IsSet() const { return Entity.IsSet() && (Cast<USceneComponent>(Entity.Obj) || Cast<AActor>(Entity.Obj)); }
};

/**
 * Structure containing information about the semantic overlap event
 */
USTRUCT()
struct FSLContactResult
{
	GENERATED_BODY()

	// Self
	FSLEntity Self;

	// Other 
	FSLEntity Other;

	// The mesh (static or skeletal) of the other overlapping component
	TWeakObjectPtr<UMeshComponent> SelfMeshComponent;

	// The mesh (static or skeletal) of the other overlapping component
	TWeakObjectPtr<UMeshComponent> OtherMeshComponent;

	// Timestamp in seconds of the event triggering
	float Time;

	// Flag showing if Other is also of type Semantic Overlap Area
	bool bIsOtherASemanticOverlapArea;

	// Default ctor
	FSLContactResult() {};

	// Init constructor
	FSLContactResult(const FSLEntity& InSelf, const FSLEntity& InOther, float InTime,
		bool bIsSemanticOverlapArea) :
		Self(InSelf),
		Other(InOther),
		Time(InTime),
		bIsOtherASemanticOverlapArea(bIsSemanticOverlapArea)
	{};

	// Init constructor with mesh component (static/skeletal)
	FSLContactResult(const FSLEntity& InSelf, const FSLEntity& InOther, float InTime,
		bool bIsSemanticOverlapArea, UMeshComponent* InSelfMeshComponent, UMeshComponent* InOtherMeshComponent) :
		Self(InSelf),
		Other(InOther),
		SelfMeshComponent(InSelfMeshComponent),
		OtherMeshComponent(InOtherMeshComponent),
		Time(InTime),
		bIsOtherASemanticOverlapArea(bIsSemanticOverlapArea)
	{};

	// Get result as string
	FString ToString() const
	{
		return FString::Printf(TEXT("Self:[%s] Other:[%s] Time:%f bIsOtherASemanticOverlapArea:%s StaticMeshActor:%s StaticMeshComponent:%s"),
			*Self.ToString(), *Other.ToString(), Time,
			bIsOtherASemanticOverlapArea == true ? TEXT("True") : TEXT("False"),
			OtherMeshComponent.IsValid() ? *OtherMeshComponent->GetName() : TEXT("None"));
	}
};

/************************************************************************/
/*                       DELEGATES                                      */
/************************************************************************/
/** Delegate to notify that a contact begins between two semantically annotated objects */
DECLARE_MULTICAST_DELEGATE_OneParam(FSLBeginContactSignature, const FSLContactResult&);

/** Delegate to notify that a contact ended between two semantically annotated objects */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FSLEndContactSignature, const FSLEntity& /*Self*/, const FSLEntity& /*Other*/, float /*Time*/);

