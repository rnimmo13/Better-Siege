// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SiegeWall.h"

class AActor;
class ASiegeBombSite;
class UWorld;

/** A stationary target in the firing range */
struct FSiegeDummySpot
{
	FTransform Transform;

	/** 0 stands with the player and can be revived, 1 is the enemy */
	uint8 Team = 1;

	float MaxHealth = 100.0f;

	/** How far it walks to either side of its spot. 0 stands still */
	float StrafeDistance = 0.0f;

	FString Name;
};

/** Everything the game mode needs to know about a built map */
struct FSiegeLayoutResult
{
	/** Every spawned actor, destroyed when the round resets */
	TArray<TWeakObjectPtr<AActor>> Actors;

	TArray<TWeakObjectPtr<ASiegeBombSite>> Sites;

	TArray<FTransform> AttackerSpawns;
	TArray<FTransform> DefenderSpawns;

	/** Panels that border a bomb site; good reinforcement candidates */
	TArray<TWeakObjectPtr<ASiegeWall>> SiteWalls;

	/** Positions defender bots hold, indexed by site (0 = A, 1 = B) */
	TArray<FVector> DefenderPostsA;
	TArray<FVector> DefenderPostsB;

	/** Points roaming defenders wander between */
	TArray<FVector> RoamPoints;

	/** Points outside the building attackers approach from before pushing a site */
	TArray<FVector> EntryPoints;

	/** Defender security cameras placed around the map */
	TArray<TWeakObjectPtr<class ASiegeSecurityCamera>> Cameras;

	/** Targets to spawn, firing range only */
	TArray<FSiegeDummySpot> DummySpots;
};

/**
 *  Builds the "Embassy" map at runtime: a brick building with a ground floor, a second floor over the north wing
 *  and a basement under the south half, soft destructible walls and floors, barricaded doors and windows, furniture
 *  and lights. Two bomb site pairs (ground floor or basement) are chosen per round. Rebuilt every round so destruction resets.
 */
namespace SiegeLayout
{
	enum class EGapFill : uint8
	{
		Open,
		Barricade
	};

	struct FGap
	{
		/** Distance along the wall run to the gap center */
		float Center = 0.0f;
		float Width = 120.0f;
		/** Height of the gap bottom above the run's base (0 for doors) */
		float Bottom = 0.0f;
		/** Height of the gap top above the run's base */
		float Top = 220.0f;
		EGapFill Fill = EGapFill::Open;
	};

	/** Number of bomb site pairs the Embassy can be played on */
	static constexpr int32 NumSiteSets = 2;

	/** Builds the map with the bomb sites of the given set: 0 = ground floor (A Office, B Server), 1 = basement (A Vault, B Generator) */
	SEIGE_API void BuildEmbassy(UWorld* World, FSiegeLayoutResult& Out, int32 SiteSet = 0);

	/** Builds the firing range: shooting lanes, a wall test row, a two-storey breach house and a movement course */
	SEIGE_API void BuildRange(UWorld* World, FSiegeLayoutResult& Out);

	/** Readable name of a site set for messages */
	SEIGE_API FString GetSiteSetName(int32 SiteSet);

	SEIGE_API void Clear(FSiegeLayoutResult& Layout);
}
