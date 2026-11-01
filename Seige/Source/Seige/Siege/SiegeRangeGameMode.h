// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SiegeGameMode.h"
#include "SiegeRangeGameMode.generated.h"

class AAIController;
class ASiegePlayerController;

/** One line of the range's damage readout */
struct FSiegeRangeHit
{
	FString Victim;
	FString Zone;
	float Damage = 0.0f;
	float HealthLeft = 0.0f;

	/** Metres from the shooter, for checking damage falloff */
	float Distance = 0.0f;

	bool bHeadshot = false;
};

/**
 *  Firing range sandbox: no rounds, score or bots. The player can switch to any operator at any time,
 *  targets come back after they die, and the device can be planted and disabled without ending anything.
 *  Runs on the same phase rules as a live match by staying in the Action phase.
 */
UCLASS()
class SEIGE_API ASiegeRangeGameMode : public ASiegeGameMode
{
	GENERATED_BODY()

public:

	ASiegeRangeGameMode();

	virtual void Tick(float DeltaSeconds) override;

	virtual bool IsSandbox() const override { return true; }

	/** In the range the index covers the whole roster, attackers and defenders. Also restocks gadgets */
	virtual void RequestOperator(APlayerController* Player, int32 SideIndex) override;

	/** Enemy targets drop straight away and come back; downing and reviving is tried on the allies */
	virtual bool CanOperatorGoDown(const ASiegeOperator* Victim) const override;

	virtual void OnOperatorDamaged(ASiegeOperator* Victim, float Damage, FName HitBone, AController* DamageInstigator, bool bHeadshot) override;

	/** Rebuilds the walls and targets and restocks the player, who goes back to the start */
	void ResetRange();

	/** Newest first */
	const TArray<FSiegeRangeHit>& GetRecentHits() const { return RecentHits; }

protected:

	UPROPERTY(EditDefaultsOnly, Category="Siege|Range", meta=(Units="s"))
	float DummyRespawnTime = 4.0f;

	UPROPERTY(EditDefaultsOnly, Category="Siege|Range", meta=(Units="s"))
	float PlayerRespawnTime = 2.0f;

	/** Reinforcements carried by defenders, more than a match so walls can be tried freely */
	UPROPERTY(EditDefaultsOnly, Category="Siege|Range")
	int32 RangeReinforcements = 10;

	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	/** Nobody wins in the range; the dead are brought back by Tick */
	virtual void CheckEliminations() override {}

	/** Only the device can end a "round" here: report it and carry on */
	virtual void EndRound(uint8 Winner, ESiegeRoundEndReason Reason) override;

private:

	struct FRangeDummy
	{
		FSiegeDummySpot Spot;
		TWeakObjectPtr<ASiegeOperator> Operator;
		TWeakObjectPtr<AAIController> Controller;
		float RespawnAt = -1.0f;
		float StrafeDirection = 1.0f;
	};

	TArray<FRangeDummy> Dummies;
	TArray<FSiegeRangeHit> RecentHits;

	float PlayerRespawnAt = -1.0f;

	void StartRange();
	void EnterSandboxPhase();
	void SpawnPlayerOperator(ASiegePlayerController* PC, const FTransform& At);
	void SpawnDummy(FRangeDummy& Dummy);
	void RemoveOperator(ASiegeOperator* Op);
	void DestroyDummyControllers();
	void TickDummies(float Now);
	void TickPlayer(float Now);

	/** Puts the cameras on the defending side and makes the player's side the attackers or defenders */
	void ApplyPlayerSide(bool bAttacker);

	/** -RangeSelfTest on the command line: checks targets, damage, settings and the Esc menu, logs the results and quits */
	void RunSelfTest();
	void RunSelfTestStage2();
	void RunSelfTestAfterMenu();
	void RunSelfTestStage3();
	void SelfTestCheck(const TCHAR* Name, bool bPassed);

	int32 SelfTestFailures = 0;
	float SelfTestOldFov = 90.0f;
	float SelfTestOldVolume = 1.0f;
	bool bSelfTestDone = false;
};
