// Copyright 2017-2019, Institute for Artificial Intelligence - University of Bremen
// Author: Andrei Haidu (http://haidu.eu)

#include "WorldState/SLWorldStateWriterMongoC.h"
#include "Animation/SkeletalMeshActor.h"
#include "Conversions.h"
#include "SLEntitiesManager.h"

// Constr
FSLWorldStateWriterMongoC::FSLWorldStateWriterMongoC()
{
	bIsInit = false;
}

// Init constr
FSLWorldStateWriterMongoC::FSLWorldStateWriterMongoC(const FSLWorldStateWriterParams& InParams)
{
	bIsInit = false;
	Init(InParams);
}

// Destr
FSLWorldStateWriterMongoC::~FSLWorldStateWriterMongoC()
{
	Finish();
	
	// Disconnect and clean db connection
	Disconnect();
}

// Init
void FSLWorldStateWriterMongoC::Init(const FSLWorldStateWriterParams& InParams)
{
	if(!bIsInit)
	{
		if(!Connect(InParams.TaskId, InParams.EpisodeId, InParams.ServerIp, InParams.ServerPort, InParams.bOverwrite))
		{
			return;
		}
		LinDistSqMin = InParams.LinearDistanceSquared;
		AngDistMin = InParams.AngularDistance;
		bIsInit =  true;
	}
}

// Finish
void FSLWorldStateWriterMongoC::Finish()
{
	if (bIsInit)
	{
		CreateIndexes();
		bIsInit = false;
	}
}

// Write data to document
void FSLWorldStateWriterMongoC::Write(float Timestamp,
	TArray<TSLEntityPreviousPose<AActor>>& ActorEntities,
	TArray<TSLEntityPreviousPose<USceneComponent>>& ComponentEntities,
	TArray<TSLEntityPreviousPose<USLSkeletalDataComponent>>& SkeletalEntities,
	FSLGazeData& GazeData,
	bool bCheckAndRemoveInvalidEntities)
{
	// Avoid writing empty documents
	if (ActorEntities.Num() == 0 && ComponentEntities.Num() == 0 && SkeletalEntities.Num() == 0)
	{
		return;
	}

#if SL_WITH_LIBMONGO_C
	bson_t* ws_doc;
	bson_t entities_arr;
	bson_t sk_entities_arr;
	bson_error_t error;

	uint32_t arr_idx = 0;

	// Document to store the data
	ws_doc = bson_new();

	// Add timestamp
	BSON_APPEND_DOUBLE(ws_doc, "timestamp", Timestamp);

	// TODO Avoid writing empty documents by checking the indexes or sending a bool reference
	// Add entities to array
	BSON_APPEND_ARRAY_BEGIN(ws_doc, "entities", &entities_arr);
	AddActorEntities(ActorEntities, &entities_arr, arr_idx);
	AddComponentEntities(ComponentEntities, &entities_arr, arr_idx);
	bson_append_array_end(ws_doc, &entities_arr);

	
	// Avoid writing empty documents
	if(SkeletalEntities.Num() > 0)
	{
		// Add skel entities to array
		BSON_APPEND_ARRAY_BEGIN(ws_doc, "skel_entities", &sk_entities_arr);
		// Reset array index
		arr_idx = 0;
		AddSkeletalEntities(SkeletalEntities, &sk_entities_arr, arr_idx);
		bson_append_array_end(ws_doc, &sk_entities_arr);
	}

	if(GazeData.HasDataFast())
	{
		if(!PreviousGazeData.Equals(GazeData, 3.f))
		{
			AddGazeData(GazeData, ws_doc);
			PreviousGazeData = GazeData;
		}
	}



	if (!mongoc_collection_insert_one(collection, ws_doc, NULL, NULL, &error))
	{
		UE_LOG(LogTemp, Error, TEXT("%s::%d Err.: %s"),
			*FString(__func__), __LINE__, *FString(error.message));
		bson_destroy(ws_doc);
	}

	// Clean up
	bson_destroy(ws_doc);

#endif //SL_WITH_LIBMONGO_C
}

// Connect to the database
bool FSLWorldStateWriterMongoC::Connect(const FString& DBName, const FString& CollectionName, const FString& ServerIp, uint16 ServerPort, bool bOverwrite)
{
#if SL_WITH_LIBMONGO_C
	// Required to initialize libmongoc's internals	
	mongoc_init();

	// Stores any error that might appear during the connection
	bson_error_t error;

	// Safely create a MongoDB URI object from the given string
	FString Uri = TEXT("mongodb://") + ServerIp + TEXT(":") + FString::FromInt(ServerPort);
	uri = mongoc_uri_new_with_error(TCHAR_TO_UTF8(*Uri), &error);
	if (!uri)
	{
		UE_LOG(LogTemp, Error, TEXT("%s::%d Err.:%s; [Uri=%s]"),
			*FString(__func__), __LINE__, *FString(error.message), *Uri);
		return false;
	}

	// Create a new client instance
	client = mongoc_client_new_from_uri(uri);
	if (!client)
	{
		return false;
	}

	// Register the application name so we can track it in the profile logs on the server
	mongoc_client_set_appname(client, TCHAR_TO_UTF8(*("SL_" + CollectionName)));

	// Get a handle on the database "db_name" and collection "coll_name"
	database = mongoc_client_get_database(client, TCHAR_TO_UTF8(*DBName));

	// Abort if we connect to an existing collection
	if (mongoc_database_has_collection(database, TCHAR_TO_UTF8(*CollectionName), &error))
	{
		if(bOverwrite)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s::%d World state collection %s already exists, will be removed and overwritten.."),
				*FString(__func__), __LINE__, *CollectionName);
			if(!mongoc_collection_drop (mongoc_database_get_collection(database, TCHAR_TO_UTF8(*CollectionName)), &error))
			{
				UE_LOG(LogTemp, Error, TEXT("%s::%d Could not drop collection, err.:%s;"),
					*FString(__func__), __LINE__, *FString(error.message));
				return false;
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("%s::%d World state collection %s already exists and should not be overwritten, skipping metadata logging.."),
				*FString(__func__), __LINE__, *CollectionName);
			return false;
		}
	}
	else 
	{
		UE_LOG(LogTemp, Warning, TEXT("%s::%d Collection %s does not exist, creating a new one.."),
			*FString(__func__), __LINE__, *CollectionName);
	}
	
	collection = mongoc_client_get_collection(client, TCHAR_TO_UTF8(*DBName), TCHAR_TO_UTF8(*CollectionName));

	// Check server. Ping the "admin" database
	bson_t* server_ping_cmd;
	server_ping_cmd = BCON_NEW("ping", BCON_INT32(1));
	if (!mongoc_client_command_simple(client, "admin", server_ping_cmd, NULL, NULL, &error))
	{
		UE_LOG(LogTemp, Error, TEXT("%s::%d Check server err.: %s"),
			*FString(__func__), __LINE__, *FString(error.message));
		bson_destroy(server_ping_cmd);
		return false;
	}

	bson_destroy(server_ping_cmd);
	return true;
#else
	return false;
#endif //SL_WITH_LIBMONGO_C
}

// Disconnect and clean db connection
void FSLWorldStateWriterMongoC::Disconnect()
{
#if SL_WITH_LIBMONGO_C
	// Release handles and clean up mongoc
	if(uri)
	{
		mongoc_uri_destroy(uri);
	}
	if(client)
	{
		mongoc_client_destroy(client);
	}
	if(database)
	{
		mongoc_database_destroy(database);
	}
	if(collection)
	{
		mongoc_collection_destroy(collection);
	}
	mongoc_cleanup();
#endif //SL_WITH_LIBMONGO_C
}

// Create indexes from the logged data, usually called after logging
bool FSLWorldStateWriterMongoC::CreateIndexes() const
{
	if (!bIsInit)
	{
		return false;
	}
#if SL_WITH_LIBMONGO_C

	bson_t* index_command;
	bson_error_t error;
	
	bson_t index;
	bson_init(&index);
	BSON_APPEND_INT32(&index, "timestamp", 1);
	char* index_name = mongoc_collection_keys_to_index_string(&index);

	bson_t index2;
	bson_init(&index2);
	BSON_APPEND_INT32(&index2, "entities.id", 1);
	char* index_name2 = mongoc_collection_keys_to_index_string(&index2);

	bson_t index3;
	bson_init(&index3);
	BSON_APPEND_INT32(&index3, "skel_entities.id", 1);
	char* index_name3 = mongoc_collection_keys_to_index_string(&index3);

	bson_t index4;
	bson_init(&index4);
	BSON_APPEND_INT32(&index4, "skel_entities.bones.name", 1);
	char* index_name4 = mongoc_collection_keys_to_index_string(&index4);

	bson_t index5;
	bson_init(&index5);
	BSON_APPEND_INT32(&index5, "gaze.entity_id", 1);
	char* index_name5 = mongoc_collection_keys_to_index_string(&index5);


	index_command = BCON_NEW("createIndexes",
			BCON_UTF8(mongoc_collection_get_name(collection)),
			"indexes",
			"[",
				"{",
					"key",
					BCON_DOCUMENT(&index),
					"name",
					BCON_UTF8(index_name),
					//"unique",
					//BCON_BOOL(false),
				"}",
				"{",
					"key",
					BCON_DOCUMENT(&index2),
					"name",
					BCON_UTF8(index_name2),
					//"unique",
					//BCON_BOOL(false),
				"}",
				"{",
					"key",
					BCON_DOCUMENT(&index3),
					"name",
					BCON_UTF8(index_name3),
					//"unique",
					//BCON_BOOL(false),
				"}",
				"{",
					"key",
					BCON_DOCUMENT(&index4),
					"name",
					BCON_UTF8(index_name4),
					//"unique",
					//BCON_BOOL(false),
				"}",
				"{",
					"key",
					BCON_DOCUMENT(&index5),
					"name",
					BCON_UTF8(index_name5),
					//"unique",
					//BCON_BOOL(false),
				"}",
			"]");

	if (!mongoc_collection_write_command_with_opts(collection, index_command, NULL/*opts*/, NULL/*reply*/, &error))
	{
		UE_LOG(LogTemp, Error, TEXT("%s::%d Create indexes err.: %s"),
			*FString(__func__), __LINE__, *FString(error.message));
		bson_destroy(index_command);
		bson_free(index_name);
		return false;
	}

	// Clean up
	bson_destroy(index_command);
	bson_free(index_name);
	return true;
#else
	return false;
#endif //SL_WITH_LIBMONGO_C
}

#if SL_WITH_LIBMONGO_C
// Add non skeletal actors to array
void FSLWorldStateWriterMongoC::AddActorEntities(TArray<TSLEntityPreviousPose<AActor>>& ActorEntities,
	bson_t* out_doc, uint32_t& idx) const
{
	bson_t arr_obj;
	char idx_str[16];
	const char *idx_key;

	// Iterate items
	for (auto Itr(ActorEntities.CreateIterator()); Itr; ++Itr)
	{
		// Check if pointer is valid
		if (Itr->Obj.IsValid(/*false, true*/))
		{
			// Check if the entity moved more than the threshold since the last logging
			const FVector CurrLoc = Itr->Obj->GetActorLocation();
			const FQuat CurrQuat = Itr->Obj->GetActorQuat();

			if (FVector::DistSquared(CurrLoc, Itr->PrevLoc) > LinDistSqMin ||
				CurrQuat.AngularDistance(Itr->PrevQuat))
			{
				// Update prev state
				Itr->PrevLoc = CurrLoc;
				Itr->PrevQuat = CurrQuat;

				bson_uint32_to_string(idx, &idx_key, idx_str, sizeof idx_str);
				BSON_APPEND_DOCUMENT_BEGIN(out_doc, idx_key, &arr_obj);

				BSON_APPEND_UTF8(&arr_obj, "id", TCHAR_TO_UTF8(*Itr->Entity.Id));
				AddPoseChild(CurrLoc, CurrQuat, &arr_obj);

				bson_append_document_end(out_doc, &arr_obj);
				idx++;
			}
		}
		else
		{
			Itr.RemoveCurrent();
			FSLEntitiesManager::GetInstance()->RemoveEntity(Itr->Obj.Get());
		}
	}
}

// Add non skeletal components to array
void FSLWorldStateWriterMongoC::AddComponentEntities(TArray<TSLEntityPreviousPose<USceneComponent>>& ComponentEntities,
	bson_t* out_doc, uint32_t& idx) const
{
	bson_t arr_obj;
	char idx_str[16];
	const char *idx_key;

	// Iterate items
	for (auto Itr(ComponentEntities.CreateIterator()); Itr; ++Itr)
	{
		// Check if pointer is valid
		if (Itr->Obj.IsValid(/*false, true*/))
		{
			// Check if the entity moved more than the threshold since the last logging
			const FVector CurrLoc = Itr->Obj->GetComponentLocation();
			const FQuat CurrQuat = Itr->Obj->GetComponentQuat();

			if (FVector::DistSquared(CurrLoc, Itr->PrevLoc) > LinDistSqMin ||
				CurrQuat.AngularDistance(Itr->PrevQuat))
			{
				// Update prev state
				Itr->PrevLoc = CurrLoc;
				Itr->PrevQuat = CurrQuat;

				bson_uint32_to_string(idx, &idx_key, idx_str, sizeof idx_str);
				BSON_APPEND_DOCUMENT_BEGIN(out_doc, idx_key, &arr_obj);

				BSON_APPEND_UTF8(&arr_obj, "id", TCHAR_TO_UTF8(*Itr->Entity.Id));
				AddPoseChild(CurrLoc, CurrQuat, &arr_obj);

				uint32_t arr_jdx = 10;


				bson_append_document_end(out_doc, &arr_obj);
				idx++;
			}
		}
		else
		{
			Itr.RemoveCurrent();
			FSLEntitiesManager::GetInstance()->RemoveEntity(Itr->Obj.Get());
		}
	}
}

// Add skeletal actors to array
void FSLWorldStateWriterMongoC::AddSkeletalEntities(TArray<TSLEntityPreviousPose<USLSkeletalDataComponent>>& SkeletalEntities,
	bson_t* out_doc, uint32_t& idx) const
{
	bson_t arr_obj;
	char idx_str[16];
	const char *idx_key;

	// Iterate items
	for (auto Itr(SkeletalEntities.CreateIterator()); Itr; ++Itr)
	{
		// Check if the entity moved more than the threshold since the last logging
		const FVector CurrLoc = Itr->Obj->GetComponentLocation();
		const FQuat CurrQuat = Itr->Obj->GetComponentQuat();

		// Check if pointer is valid
		if (Itr->Obj.IsValid(/*false, true*/))
		{
			if (FVector::DistSquared(CurrLoc, Itr->PrevLoc) > LinDistSqMin ||
				CurrQuat.AngularDistance(Itr->PrevQuat))
			{
				// Update prev state
				Itr->PrevLoc = CurrLoc;
				Itr->PrevQuat = CurrQuat;

				bson_uint32_to_string(idx, &idx_key, idx_str, sizeof idx_str);
				BSON_APPEND_DOCUMENT_BEGIN(out_doc, idx_key, &arr_obj);

				BSON_APPEND_UTF8(&arr_obj, "id", TCHAR_TO_UTF8(*Itr->Entity.Id));
				AddPoseChild(CurrLoc, CurrQuat, &arr_obj);

				// Add bones
				if (Itr->Obj->SkeletalMeshParent)
				{
					AddSkeletalBones(Itr->Obj->SkeletalMeshParent, &arr_obj);
				}

				bson_append_document_end(out_doc, &arr_obj);
				idx++;
			}
		}
		else
		{
			Itr.RemoveCurrent();
			FSLEntitiesManager::GetInstance()->RemoveEntity(Itr->Obj.Get());
		}
	}
}

// Add gaze data to document
void FSLWorldStateWriterMongoC::AddGazeData(const FSLGazeData& GazeData, bson_t* out_doc) const
{
	const FVector ROSTargetLoc = FConversions::UToROS(GazeData.Target);
	const FVector ROSOrigLoc = FConversions::UToROS(GazeData.Origin);

	// When nesting objects, parent needs to be init to a base state !!! 
	bson_t gaze_obj = BSON_INITIALIZER; 
	bson_t target_loc;
	bson_t origin_loc;
	
	BSON_APPEND_UTF8(&gaze_obj, "entity_id", TCHAR_TO_UTF8(*GazeData.Entity.Id));
	
	BSON_APPEND_DOCUMENT_BEGIN(&gaze_obj, "target", &target_loc);
	BSON_APPEND_DOUBLE(&target_loc, "x", ROSTargetLoc.X);
	BSON_APPEND_DOUBLE(&target_loc, "y", ROSTargetLoc.Y);
	BSON_APPEND_DOUBLE(&target_loc, "z", ROSTargetLoc.Z);
	bson_append_document_end(&gaze_obj, &target_loc);

	BSON_APPEND_DOCUMENT_BEGIN(&gaze_obj, "origin", &origin_loc);
	BSON_APPEND_DOUBLE(&origin_loc, "x", ROSOrigLoc.X);
	BSON_APPEND_DOUBLE(&origin_loc, "y", ROSOrigLoc.Y);
	BSON_APPEND_DOUBLE(&origin_loc, "z", ROSOrigLoc.Z);
	bson_append_document_end(&gaze_obj, &origin_loc);
	
	BSON_APPEND_DOCUMENT(out_doc, "gaze", &gaze_obj);
}

// Add skeletal bones to array
void FSLWorldStateWriterMongoC::AddSkeletalBones(USkeletalMeshComponent* SkelComp, bson_t* out_doc) const
{
	bson_t bones_arr;
	bson_t arr_obj;
	char idx_str[16];
	const char *idx_key;
	uint32_t arr_idx = 0;

	// Add entities to array
	BSON_APPEND_ARRAY_BEGIN(out_doc, "bones", &bones_arr);

	TArray<FName> BoneNames;
	SkelComp->GetBoneNames(BoneNames);
	for (const auto& BoneName : BoneNames)
	{
		const FVector CurrLoc = SkelComp->GetBoneLocation(BoneName);
		const FQuat CurrQuat = SkelComp->GetBoneQuaternion(BoneName);

		bson_uint32_to_string(arr_idx, &idx_key, idx_str, sizeof idx_str);
		BSON_APPEND_DOCUMENT_BEGIN(&bones_arr, idx_key, &arr_obj);

		BSON_APPEND_UTF8(&arr_obj, "name", TCHAR_TO_UTF8(*BoneName.ToString()));
		AddPoseChild(CurrLoc, CurrQuat, &arr_obj);

		bson_append_document_end(&bones_arr, &arr_obj);
		arr_idx++;
	}

	bson_append_array_end(out_doc, &bones_arr);
}

// Add pose to document
void FSLWorldStateWriterMongoC::AddPoseChild(const FVector& InLoc, const FQuat& InQuat, bson_t* out_doc) const
{
	// Switch to right handed ROS transformation
	const FVector ROSLoc = FConversions::UToROS(InLoc);
	const FQuat ROSQuat = FConversions::UToROS(InQuat);

	bson_t child_obj_loc;
	bson_t child_obj_rot;
	
	BSON_APPEND_DOCUMENT_BEGIN(out_doc, "loc", &child_obj_loc);
	BSON_APPEND_DOUBLE(&child_obj_loc, "x", ROSLoc.X);
	BSON_APPEND_DOUBLE(&child_obj_loc, "y", ROSLoc.Y);
	BSON_APPEND_DOUBLE(&child_obj_loc, "z", ROSLoc.Z);
	bson_append_document_end(out_doc, &child_obj_loc);

	BSON_APPEND_DOCUMENT_BEGIN(out_doc, "rot", &child_obj_rot);
	BSON_APPEND_DOUBLE(&child_obj_rot, "x", ROSQuat.X);
	BSON_APPEND_DOUBLE(&child_obj_rot, "y", ROSQuat.Y);
	BSON_APPEND_DOUBLE(&child_obj_rot, "z", ROSQuat.Z);
	BSON_APPEND_DOUBLE(&child_obj_rot, "w", ROSQuat.W);
	bson_append_document_end(out_doc, &child_obj_rot);
}
#endif //SL_WITH_LIBMONGO_C
