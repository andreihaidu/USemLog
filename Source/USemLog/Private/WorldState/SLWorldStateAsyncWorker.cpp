// Copyright 2017-2019, Institute for Artificial Intelligence - University of Bremen
// Author: Andrei Haidu (http://haidu.eu)

#include "WorldState/SLWorldStateAsyncWorker.h"
#include "SLEntitiesManager.h"
#include "WorldState/SLWorldStateWriterJson.h"
#include "WorldState/SLWorldStateWriterBson.h"
#include "WorldState/SLWorldStateWriterMongoC.h"
#include "WorldState/SLWorldStateWriterMongoCxx.h"
#include "Tags.h"
#include "Animation/SkeletalMeshActor.h"

// Constructor
FSLWorldStateAsyncWorker::FSLWorldStateAsyncWorker()
{
	// Flags
	bIsInit = false;
	bIsStarted = false;
	bIsFinished = false;
}

// Destructor
FSLWorldStateAsyncWorker::~FSLWorldStateAsyncWorker()
{
	Finish(true);
}

// Init writer, load items from sl mapping singleton
void FSLWorldStateAsyncWorker::Init(UWorld* InWorld,
	ESLWorldStateWriterType InWriterType,
	const FSLWorldStateWriterParams& InParams)
{
	if(!bIsInit)
	{
		// Pointer to the world
		World = InWorld;
		if(!World)
		{
			return;
		}
		
		// Cache the writer type
		WriterType = InWriterType;

		// Create the writer object
		switch(WriterType)
		{
		case ESLWorldStateWriterType::Json:
			Writer = MakeShareable(new FSLWorldStateWriterJson(InParams));
			break;
		case ESLWorldStateWriterType::Bson:
			Writer = MakeShareable(new FSLWorldStateWriterBson(InParams));
			break;
		case ESLWorldStateWriterType::MongoC:
			Writer = MakeShareable(new FSLWorldStateWriterMongoC(InParams));
			break;
		case ESLWorldStateWriterType::MongoCxx:
			Writer = MakeShareable(new FSLWorldStateWriterMongoCxx(InParams));
			break;
		default:
			Writer = MakeShareable(new FSLWorldStateWriterJson(InParams));
			break;
		}

		// Writer could not be created
		if (!Writer.IsValid() || !Writer->IsInit())
		{
			return;
		}

		// Make sure the semantic items are initialized
		FSLEntitiesManager::GetInstance()->Init(World);

		// Iterate all annotated entities, ignore skeletal ones
		TArray<FSLEntity> SemanticEntities;
		FSLEntitiesManager::GetInstance()->GetSemanticDataArray(SemanticEntities);
		for (const auto& SemEntity : SemanticEntities)
		{
			// Take into account only objects with transform data (AActor, USceneComponents)
			if (AActor* ObjAsActor = Cast<AActor>(SemEntity.Obj))
			{
				// Continue if it is not a skeletal mesh actor
				if (!Cast<ASkeletalMeshActor>(ObjAsActor))
				{
					ActorEntitites.Emplace(TSLEntityPreviousPose<AActor>(ObjAsActor, SemEntity));
				}
			}
			else if (USceneComponent* ObjAsSceneComp = Cast<USceneComponent>(SemEntity.Obj))
			{
				// Continue if it is not a skeletal mesh component 
				if (!Cast<USkeletalMeshComponent>(ObjAsSceneComp))
				{
					ComponentEntities.Emplace(TSLEntityPreviousPose<USceneComponent>(ObjAsSceneComp, SemEntity));
				}
			}
		}

		// Get the skeletal data info
		TArray<USLSkeletalDataComponent*> SemanticSkeletalData;
		FSLEntitiesManager::GetInstance()->GetSemanticSkeletalDataArray(SemanticSkeletalData);
		for (const auto& SemSkelData : SemanticSkeletalData)
		{
			SkeletalEntities.Emplace(TSLEntityPreviousPose<USLSkeletalDataComponent>(
				SemSkelData, SemSkelData->OwnerSemanticData));
		}

		// Init the gaze handler
		GazeDataHandler.Init();

		// Can start working
		bIsInit = true;
	}
}

// Prepare worker for starting to log
void FSLWorldStateAsyncWorker::Start()
{
	if(!bIsStarted && bIsInit)
	{
		// Start the gaze handler
		GazeDataHandler.Start(World);

		bIsStarted = true;
	}
}

// Finish up worker
void FSLWorldStateAsyncWorker::Finish(bool bForced)
{
	if (!bIsFinished && (bIsStarted || bIsInit))
	{
		if (!bForced)
		{
			// Check if mongo writer
			if (Writer.IsValid())
			{
				if (WriterType == ESLWorldStateWriterType::MongoCxx)
				{
					// We cannot cast dynamically if it is not an UObject
					TSharedPtr<FSLWorldStateWriterMongoCxx> AsMongoCxxWriter = StaticCastSharedPtr<FSLWorldStateWriterMongoCxx>(Writer);
					// Finish writer (create database indexes for example)
					AsMongoCxxWriter->Finish();
				}
				else if (WriterType == ESLWorldStateWriterType::MongoC)
				{
					// We cannot cast dynamically if it is not an UObject
					TSharedPtr<FSLWorldStateWriterMongoC> AsMongoCWriter = StaticCastSharedPtr<FSLWorldStateWriterMongoC>(Writer);
					// Finish writer (create database indexes for example)
					AsMongoCWriter->Finish();
				}
			}

			GazeDataHandler.Finish();
		}
		
		bIsInit = false;
		bIsStarted = false;
		bIsFinished = true;
	}
}

// Remove all items that are semantically marked as static
void FSLWorldStateAsyncWorker::RemoveStaticItems()
{
	// Non-skeletal actors
	for (auto Itr(ActorEntitites.CreateIterator()); Itr; ++Itr)
	{
		if (FTags::HasKeyValuePair(Itr->Obj.Get(), "SemLog", "Mobility", "Static"))
		{
			Itr.RemoveCurrent();
		}
	}
	ActorEntitites.Shrink();

	// Non-skeletal scene components
	for (auto Itr(ComponentEntities.CreateIterator()); Itr; ++Itr)
	{
		if (FTags::HasKeyValuePair(Itr->Obj.Get(), "SemLog", "Mobility", "Static"))
		{
			Itr.RemoveCurrent();
		}
	}
	ComponentEntities.Shrink();

	// Skeletal components are probably always movable, so we just skip that step
}

// Async work done here
void FSLWorldStateAsyncWorker::DoWork()
{
	FSLGazeData GazeData;
	GazeDataHandler.GetData(GazeData);
	Writer->Write(World->GetTimeSeconds(), ActorEntitites, ComponentEntities, SkeletalEntities, GazeData);
}

// Needed by the engine API
FORCEINLINE TStatId FSLWorldStateAsyncWorker::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FSLWorldStateAsyncWorker, STATGROUP_ThreadPoolAsyncTasks);
}
