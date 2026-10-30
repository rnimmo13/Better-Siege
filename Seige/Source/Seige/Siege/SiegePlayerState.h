// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "SiegePlayerState.generated.h"

/**
 *  Per-player match data for both humans and bots
 */
UCLASS()
class SEIGE_API ASiegePlayerState : public APlayerState
{
	GENERATED_BODY()

public:

	/** 0 or 1. The human player is always on team 0 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Siege")
	uint8 Team = 0;

	/** Index into the game mode operator list chosen for this round, or INDEX_NONE */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Siege")
	int32 OperatorIndex = INDEX_NONE;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Siege")
	int32 Kills = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Siege")
	int32 Deaths = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Siege")
	int32 Plants = 0;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
