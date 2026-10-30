// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "SiegeTypes.h"
#include "SiegeGameState.generated.h"

/** A short HUD message for one team (or everyone) */
USTRUCT(BlueprintType)
struct FSiegeMessage
{
	GENERATED_BODY()

	/** Team that should see it. 255 = everyone */
	UPROPERTY(BlueprintReadOnly, Category="Siege")
	uint8 Team = 255;

	/** If set, only this pawn's controller sees the message */
	UPROPERTY(BlueprintReadOnly, Category="Siege")
	TWeakObjectPtr<APawn> OnlyFor;

	UPROPERTY(BlueprintReadOnly, Category="Siege")
	FString Text;

	UPROPERTY(BlueprintReadOnly, Category="Siege")
	float Time = 0.0f;
};

/**
 *  Round and match state shared with the HUD
 */
UCLASS()
class SEIGE_API ASiegeGameState : public AGameStateBase
{
	GENERATED_BODY()

public:

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Siege")
	ESiegePhase Phase = ESiegePhase::WaitingToStart;

	/** Server world time at which the current phase ends */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Siege")
	float PhaseEndTime = 0.0f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Siege")
	int32 RoundNumber = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Siege")
	int32 ScoreTeam0 = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Siege")
	int32 ScoreTeam1 = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Siege")
	uint8 AttackingTeam = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Siege")
	int32 RoundsToWin = 4;

	/** Winner of the last round, 255 if none yet */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Siege")
	uint8 LastRoundWinner = 255;

	UPROPERTY(Replicated, BlueprintReadOnly, Category="Siege")
	ESiegeRoundEndReason LastRoundReason = ESiegeRoundEndReason::None;

	/** Name of the site the device was planted on, empty if not planted */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Siege")
	FString PlantedSite;

	UPROPERTY(BlueprintReadOnly, Category="Siege")
	TArray<FSiegeKillFeedEntry> KillFeed;

	UPROPERTY(BlueprintReadOnly, Category="Siege")
	TArray<FSiegeMessage> Messages;

	/** Seconds left in the current phase */
	float GetPhaseTimeRemaining() const;

	int32 GetScore(uint8 Team) const { return Team == 0 ? ScoreTeam0 : ScoreTeam1; }

	void AddMessage(uint8 Team, const FString& Text, APawn* OnlyFor = nullptr);

	void AddKill(const FSiegeKillFeedEntry& Entry);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
