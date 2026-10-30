// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SiegeTypes.h"
#include "SiegeLayout.h"
#include "SiegeGameMode.generated.h"

class ASiegeOperator;
class ASiegeBombSite;
class ASiegeDevice;
class ASiegeGameState;
class ASiegeBotController;
class AController;

/**
 *  Tactical 5v5 bomb mode against AI bots.
 *  Runs operator select, preparation, action and post-plant phases, applies the round win rules,
 *  swaps sides at halftime and ends the match when a team reaches RoundsToWin.
 */
UCLASS()
class SEIGE_API ASiegeGameMode : public AGameModeBase
{
	GENERATED_BODY()

protected:

	/** Operator character class used for players and bots */
	UPROPERTY(EditDefaultsOnly, Category="Siege")
	TSubclassOf<ASiegeOperator> OperatorClass;

	/** All playable operators, attackers and defenders */
	UPROPERTY(EditDefaultsOnly, Category="Siege")
	TArray<FSiegeOperatorDef> Operators;

	UPROPERTY(EditDefaultsOnly, Category="Siege|Rules")
	int32 TeamSize = 5;

	UPROPERTY(EditDefaultsOnly, Category="Siege|Rules")
	int32 RoundsToWin = 4;

	/** Sides swap after this many rounds */
	UPROPERTY(EditDefaultsOnly, Category="Siege|Rules")
	int32 RoundsPerHalf = 3;

	UPROPERTY(EditDefaultsOnly, Category="Siege|Rules", meta=(Units="s"))
	float OperatorSelectTime = 12.0f;

	/** Preparation length when the player defends (time to reinforce and set up) */
	UPROPERTY(EditDefaultsOnly, Category="Siege|Rules", meta=(Units="s"))
	float PreparationTime = 45.0f;

	/** Preparation length when the player attacks (spent driving recon drones) */
	UPROPERTY(EditDefaultsOnly, Category="Siege|Rules", meta=(Units="s"))
	float AttackerPreparationTime = 45.0f;

	UPROPERTY(EditDefaultsOnly, Category="Siege|Rules", meta=(Units="s"))
	float ActionTime = 180.0f;

	UPROPERTY(EditDefaultsOnly, Category="Siege|Rules", meta=(Units="s"))
	float DeviceCountdown = 45.0f;

	UPROPERTY(EditDefaultsOnly, Category="Siege|Rules", meta=(Units="s"))
	float RoundOverTime = 7.0f;

	UPROPERTY(EditDefaultsOnly, Category="Siege|Rules", meta=(Units="s"))
	float MatchOverTime = 20.0f;

	/** Reinforcements each defender carries */
	UPROPERTY(EditDefaultsOnly, Category="Siege|Rules")
	int32 ReinforcementsPerDefender = 2;

	UPROPERTY(EditDefaultsOnly, Category="Siege|Bots")
	TArray<FString> BotNames;

	UPROPERTY(EditDefaultsOnly, Category="Siege|Bots")
	TSubclassOf<ASiegeBotController> BotControllerClass;

	/** The built map for the current round */
	FSiegeLayoutResult Layout;

	UPROPERTY()
	TArray<TObjectPtr<ASiegeBotController>> Bots;

	UPROPERTY()
	TArray<TObjectPtr<ASiegeOperator>> RoundOperators;

	UPROPERTY()
	TObjectPtr<ASiegeDevice> ActiveDevice;

	/** AI controllers driving attacker bots' recon drones */
	UPROPERTY()
	TArray<TObjectPtr<class AAIController>> DroneControllers;

	FTimerHandle PhaseTimer;
	FTimerHandle BotPrepTimer;

	bool bMatchStarted = false;

	/** Site index (0 = A, 1 = B) attacker bots push this round */
	int32 BotAttackSite = 0;

public:

	int32 GetBotAttackSite() const { return BotAttackSite; }

	ASiegeGameMode();

	// ~Begin queries
	ESiegePhase GetPhase() const;
	bool IsAttackingTeam(uint8 Team) const;
	uint8 GetAttackingTeam() const;

	/** True while operators of this team may move */
	bool CanTeamMove(uint8 Team) const;

	/** True while operators of this team may shoot */
	bool CanTeamAct(uint8 Team) const;

	/** True while operators of this team may deploy gadgets */
	bool CanUseGadgets(uint8 Team) const;

	ASiegeBombSite* GetSiteAt(const FVector& Location) const;
	ASiegeDevice* GetActiveDevice() const;
	const FSiegeLayoutResult& GetLayout() const { return Layout; }
	const TArray<FSiegeOperatorDef>& GetOperators() const { return Operators; }

	/** Operators belonging to one side, with their indices into the full list */
	TArray<int32> GetOperatorIndicesForSide(bool bAttackers) const;

	/** Every operator spawned this round */
	const TArray<TObjectPtr<ASiegeOperator>>& GetRoundOperators() const { return RoundOperators; }

	FString GetDisplayNameFor(AController* Controller) const;

	ASiegeGameState* GetSiegeGameState() const;
	// ~End queries

	// ~Begin events
	/** Spawns a recon drone at the operator's feet. Returns null if it has none left */
	class ASiegeDrone* DeployDrone(ASiegeOperator* Operator);

	void RequestOperator(APlayerController* Player, int32 SideIndex);
	void CompletePlant(ASiegeOperator* Planter);
	void CompleteDisable(ASiegeOperator* Defuser);
	void OnDeviceDetonated(ASiegeDevice* Device);
	void OnOperatorKilled(ASiegeOperator* Victim, AController* Killer, bool bHeadshot);

	void PostTeamMessage(uint8 Team, const FString& Text);
	void PostPlayerMessage(ASiegeOperator* Operator, const FString& Text);
	void PostGlobalMessage(const FString& Text);
	// ~End events

protected:

	virtual void BeginPlay() override;

	/** Players don't get a default pawn; operators are spawned each round */
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

	void StartMatch();
	void StartNewRound();
	void BeginPreparation();
	void BeginAction();
	void OnActionTimeExpired();
	void EndRound(uint8 Winner, ESiegeRoundEndReason Reason);
	void OnRoundOverFinished();

	void SetPhase(ESiegePhase NewPhase, float Duration);

	/** Destroys this round's operators, gadgets, device and map */
	void CleanupRound();

	/** Spawns and possesses an operator for every player and bot */
	void SpawnOperators();

	/** Picks operators for bots, avoiding duplicates on each side */
	void AssignBotOperators();

	/** Checks the elimination win conditions */
	void CheckEliminations();

	int32 CountAlive(uint8 Team) const;

	/** Defender bots reinforce walls and deploy gadgets during preparation */
	void RunBotPreparation();

	/** Attacker bots send recon drones toward the target site */
	void LaunchBotDrones();

	/** All controllers taking part in the match (player first, then bots) */
	TArray<AController*> GetParticipants() const;

	uint8 GetTeamOf(AController* Controller) const;

	/** Fills Operators with the default roster */
	void BuildDefaultRoster();
};
