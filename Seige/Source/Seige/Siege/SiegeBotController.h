// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "SiegeBotController.generated.h"

class ASiegeOperator;
class ASiegeGameMode;

/**
 *  Simple tactical bot.
 *  Attackers gather at an entry point, push the chosen site and plant; defenders hold posts, roam and retake the device.
 *  Combat uses line-of-sight checks, a human-like reaction delay and aim error that tightens over time.
 */
UCLASS()
class SEIGE_API ASiegeBotController : public AAIController
{
	GENERATED_BODY()

protected:

	/** Distance at which enemies can be spotted */
	UPROPERTY(EditAnywhere, Category="Bot", meta=(Units="cm"))
	float SightRange = 4500.0f;

	/** Half-angle of the bot's field of view */
	UPROPERTY(EditAnywhere, Category="Bot", meta=(Units="Degrees"))
	float SightHalfAngle = 70.0f;

	UPROPERTY(EditAnywhere, Category="Bot", meta=(Units="s"))
	float MinReactionTime = 0.45f;

	UPROPERTY(EditAnywhere, Category="Bot", meta=(Units="s"))
	float MaxReactionTime = 0.8f;

	/** Chance to aim for the head when acquiring a target */
	UPROPERTY(EditAnywhere, Category="Bot", meta=(ClampMin=0, ClampMax=1))
	float HeadshotChance = 0.15f;

	/** Aim error radius when first acquiring a target */
	UPROPERTY(EditAnywhere, Category="Bot", meta=(Units="cm"))
	float InitialAimError = 90.0f;

	/** Aim error radius after settling on a target */
	UPROPERTY(EditAnywhere, Category="Bot", meta=(Units="cm"))
	float SettledAimError = 14.0f;

	/** Seconds to settle from initial to settled aim error */
	UPROPERTY(EditAnywhere, Category="Bot", meta=(Units="s"))
	float AimSettleTime = 1.6f;

	TWeakObjectPtr<ASiegeOperator> Target;
	float TargetAcquiredTime = 0.0f;
	float ReactionDelay = 0.5f;
	bool bAimForHead = false;
	FVector AimErrorDirection = FVector::ZeroVector;

	FVector LastKnownEnemyLocation = FVector::ZeroVector;
	float LastEnemySeenTime = -100.0f;

	bool bFiring = false;
	float BurstEndTime = 0.0f;
	float NextBurstTime = 0.0f;

	float NextThinkTime = 0.0f;

	/** Current navigation goal */
	FVector MoveGoal = FVector::ZeroVector;
	bool bHasMoveGoal = false;
	float LastRepathTime = -100.0f;

	FVector LastProgressLocation = FVector::ZeroVector;
	float LastProgressTime = 0.0f;
	float LastBashTime = -100.0f;

	/** Round-specific plan */
	int32 PlannedRound = -1;
	FVector Post = FVector::ZeroVector;
	FVector EntryPoint = FVector::ZeroVector;
	bool bReachedEntry = false;
	bool bRoamer = false;
	float NextRoamTime = 0.0f;
	float NextScanTime = 0.0f;

public:

	ASiegeBotController();

	virtual void Tick(float DeltaSeconds) override;

	/** Keeps aim pitch toward focal points (the engine zeroes it unless focusing a pawn) and limits turn rate */
	virtual void UpdateControlRotation(float DeltaTime, bool bUpdatePawn = true) override;

protected:

	/** Max aim turn speed */
	UPROPERTY(EditAnywhere, Category="Bot", meta=(Units="DegreesPerSecond"))
	float TurnRate = 420.0f;

	virtual void OnPossess(APawn* InPawn) override;

	ASiegeOperator* GetOperator() const;
	ASiegeGameMode* GetSiegeGameMode() const;

	/** Builds this round's plan (post, entry point, roaming) */
	void PlanRound(ASiegeGameMode* GM, ASiegeOperator* Op);

	/** Returns the closest enemy currently in line of sight */
	ASiegeOperator* FindVisibleEnemy(ASiegeOperator* Op) const;

	bool HasLineOfSight(ASiegeOperator* Op, ASiegeOperator* Enemy) const;

	void UpdateCombat(ASiegeOperator* Op, float Now);
	void UpdateObjective(ASiegeGameMode* GM, ASiegeOperator* Op, float Now);

	void StartFire(ASiegeOperator* Op);
	void StopFire(ASiegeOperator* Op);

	/** Moves to a location, re-pathing only when the goal changes noticeably */
	void MoveTowards(const FVector& Goal, float Acceptance = 60.0f);

	/** Breaks barricades blocking the way and recovers from getting stuck */
	void HandleStuck(ASiegeOperator* Op, float Now);
};
