// Copyright 2017-2019, Institute for Artificial Intelligence - University of Bremen
// Author: Andrei Haidu (http://haidu.eu)

#pragma once

#include "ISLEvent.h"
#include "SLStructs.h"

/**
* Slide event class
*/
class FSLSlideEvent : public ISLEvent
{
public:
	// Default constructor
	FSLSlideEvent() = default;

	// Constructor with initialization
	FSLSlideEvent(const FString& InId, const float InStart, const float InEnd, const uint64 InPairId,
		const FSLEntity& InManipulator, const FSLEntity& InItem);

	// Constructor initialization without end time
	FSLSlideEvent(const FString& InId, const float InStart, const uint64 InPairId,
		const FSLEntity& InManipulator, const FSLEntity& InItem);

	// Pair id of the event (combination of two unique runtime ids)
	uint64 PairId;

	// Who is Slideing the object
	FSLEntity Manipulator;

	// The object Slideed
	FSLEntity Item;

	/* Begin IEvent interface */
	// Create an owl representation of the event
	virtual FSLOwlNode ToOwlNode() const override;

	// Add the owl representation of the event to the owl document
	virtual void AddToOwlDoc(FSLOwlDoc* OutDoc) override;

	// Get event context data as string
	virtual FString Context() const override;

	// Get the tooltip data
	virtual FString Tooltip() const override;

	// Get the data as string
	virtual FString ToString() const override;
	/* End IEvent interface */
};