// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SeigePlayerController.h"
#include "SiegePlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class ASiegeOperator;

/**
 *  Player controller for the tactical mode.
 *  Builds the extra tactical input actions at runtime, handles operator selection and spectating teammates after death.
 */
UCLASS()
class SEIGE_API ASiegePlayerController : public ASeigePlayerController
{
	GENERATED_BODY()

protected:

	/** Runtime mapping context holding the tactical bindings */
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> SiegeContext;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UInputAction>> SiegeActions;

	/** Pawn currently being spectated after death */
	TWeakObjectPtr<ASiegeOperator> SpectateTarget;

	FTimerHandle SpectateTimer;

	bool bShowScoreboard = false;

	/** This round's operator. The controller may be possessing a drone or camera instead */
	TWeakObjectPtr<ASiegeOperator> HomeOperator;

	/** Keeps the operator's body possessed while the player looks through a device (its anim blueprints need a controller) */
	UPROPERTY(Transient)
	TObjectPtr<class AAIController> BodyHolder;

	void ReleaseBodyHolder();

public:

	ASiegePlayerController();

	/** Returns a tactical input action by name (LeanLeft, LeanRight, Crouch, Sprint, Interact, Gadget, Secondary, Reload, Melee, Observe) */
	UInputAction* GetSiegeAction(FName ActionName);

	/** Called by the game mode when the possessed operator dies */
	void OnOperatorDied();

	/** Called by the game mode when a fresh operator is possessed for a new round */
	void OnRoundStarted();

	void SetHomeOperator(ASiegeOperator* Operator) { HomeOperator = Operator; }

	/** This round's operator, even while looking through a drone or camera */
	ASiegeOperator* GetOperator() const;

	/** Switches to the team's drones (attackers, deploying one if needed) or cameras (defenders) */
	void EnterObservation();

	/** Returns to the operator, or to spectating if the operator is dead */
	void ExitObservation();

	/** Moves to the previous (-1) or next (+1) team device */
	void CycleObservation(int32 Direction);

	/** Called by a device that is destroyed while being looked through */
	void OnDeviceDestroyed(APawn* Device);

	/** True while looking through a drone or camera */
	bool IsObserving() const;

	ASiegeOperator* GetSpectateTarget() const { return SpectateTarget.Get(); }
	bool IsScoreboardShown() const { return bShowScoreboard; }

protected:

	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	/** Creates the runtime input actions and mapping context if needed */
	void EnsureSiegeInput();

	void SelectOperator(int32 Index);
	void SelectOperator1() { SelectOperator(0); }
	void SelectOperator2() { SelectOperator(1); }
	void SelectOperator3() { SelectOperator(2); }
	void SelectOperator4() { SelectOperator(3); }

	void SpectateNext();
	void ShowScoreboard() { bShowScoreboard = true; }
	void HideScoreboard() { bShowScoreboard = false; }
};
