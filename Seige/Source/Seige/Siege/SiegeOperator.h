// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterCharacter.h"
#include "SiegeTypes.h"
#include "SiegeOperator.generated.h"

class ASiegeGun;
class ASiegeWall;
class ASiegeBreachCharge;
class ASiegeBombSite;
class UInputAction;

/** What holding the interact key is currently doing */
UENUM(BlueprintType)
enum class ESiegeInteraction : uint8
{
	None,
	PlantDevice,
	DisableDevice,
	Reinforce
};

/**
 *  A playable operator used by both the player and AI bots.
 *  Adds tactical movement (lean, crouch, sprint, vault), operator health pools with lethal headshots,
 *  objective interaction, wall reinforcement, gadgets and melee on top of the template shooter character.
 */
UCLASS(abstract)
class SEIGE_API ASiegeOperator : public AShooterCharacter
{
	GENERATED_BODY()

protected:

	/** Definition this operator was spawned with */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Siege")
	FSiegeOperatorDef OperatorDef;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Siege")
	int32 UniqueGadgetCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Siege")
	int32 SecondaryGadgetCount = 0;

	/** Reinforcements this operator can still place (defenders only) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Siege")
	int32 ReinforcementsLeft = 0;

	/** Recon drones this operator can still deploy (attackers only) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Siege")
	int32 DroneCount = 0;

	TWeakObjectPtr<AController> OwningController;

	UPROPERTY(EditAnywhere, Category="Siege|Movement", meta=(Units="cm"))
	float LeanDistance = 32.0f;

	UPROPERTY(EditAnywhere, Category="Siege|Movement", meta=(Units="Degrees"))
	float LeanRoll = 12.0f;

	UPROPERTY(EditAnywhere, Category="Siege|Movement")
	float SprintMultiplier = 1.55f;

	UPROPERTY(EditAnywhere, Category="Siege|Interaction", meta=(Units="s"))
	float PlantTime = 5.0f;

	UPROPERTY(EditAnywhere, Category="Siege|Interaction", meta=(Units="s"))
	float DisableTime = 7.0f;

	UPROPERTY(EditAnywhere, Category="Siege|Interaction", meta=(Units="s"))
	float ReinforceTime = 1.5f;

	UPROPERTY(EditAnywhere, Category="Siege|Interaction", meta=(Units="cm"))
	float InteractRange = 200.0f;

	UPROPERTY(EditAnywhere, Category="Siege|Melee")
	float MeleeDamage = 55.0f;

	UPROPERTY(EditAnywhere, Category="Siege|Melee", meta=(Units="s"))
	float MeleeCooldown = 0.8f;

	/** -1 left, 0 none, 1 right */
	float LeanInput = 0.0f;
	float CurrentLean = 0.0f;
	bool bLeanLeftHeld = false;
	bool bLeanRightHeld = false;

	FVector FirstPersonMeshBaseLocation = FVector::ZeroVector;

	bool bSprintHeld = false;
	float ForwardInput = 0.0f;

	bool bInteractHeld = false;
	ESiegeInteraction CurrentInteraction = ESiegeInteraction::None;
	float InteractProgress = 0.0f;
	TWeakObjectPtr<ASiegeWall> InteractWall;

	float LastMeleeTime = -100.0f;
	float LastHitConfirmTime = -100.0f;
	float LastDamagedTime = -100.0f;
	float LastShotTime = -100.0f;

	/** Seconds of world time until which this operator is revealed to each team */
	float RevealedUntil[2] = { -1.0f, -1.0f };

	/** Soft breach charges placed and waiting for detonation */
	TArray<TWeakObjectPtr<ASiegeBreachCharge>> PlacedCharges;

	/** Name of whoever killed this operator */
	FString KillerName;

	/** Display name of the controlling player or bot */
	FString DisplayName;

public:

	ASiegeOperator();

	virtual void Tick(float DeltaSeconds) override;

	/** Applies an operator definition: health, speed, weapons and gadgets */
	void ApplyOperatorDef(const FSiegeOperatorDef& Def, int32 Reinforcements);

	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	// ~Begin input handlers (also driven by bots)
	virtual void DoMove(float Right, float Forward) override;
	virtual void DoJumpStart() override;
	virtual void DoJumpEnd() override;

	void SetLean(float Direction);
	void ToggleCrouch();
	void SetSprinting(bool bSprint);
	void StartInteract();
	void StopInteract();
	void UseUniqueGadget();
	void UseSecondaryGadget();
	void ReloadWeapon();
	void Melee();
	// ~End input handlers

	/** Called by the gun before a shot. Returns false to block firing */
	bool CanFireWeapon() const;

	/** Called by the gun when a shot goes off */
	void OnWeaponFired();

	/** Called by the gun when a bullet hits another operator */
	void OnHitConfirmed(ASiegeOperator* Victim);

	/** Called by a breach charge when it detonates or is destroyed */
	void OnChargeGone(ASiegeBreachCharge* Charge);

	/** Reveals this operator's position to the given team for a duration */
	void RevealTo(uint8 ViewingTeam, float Duration);

	bool IsRevealedTo(uint8 ViewingTeam) const;

	/** World-space origin for weapon traces (the camera) */
	FVector GetAimOrigin() const;

	/** AI controllers aim from the view location, so make it match where bullets come from */
	virtual FVector GetPawnViewLocation() const override;

	/** Spread multiplier based on movement and stance */
	float GetSpreadMultiplier() const;

	/** Location bots aim at for a headshot */
	FVector GetHeadLocation() const;

	/** Location bots aim at for a body shot */
	FVector GetChestLocation() const;

	uint8 GetTeam() const { return TeamByte; }
	float GetHealth() const { return FMath::Max(0.0f, CurrentHP); }
	float GetMaxHealth() const { return MaxHP; }
	const FSiegeOperatorDef& GetOperatorDef() const { return OperatorDef; }
	int32 GetUniqueGadgetCount() const { return UniqueGadgetCount; }
	int32 GetSecondaryGadgetCount() const { return SecondaryGadgetCount; }
	int32 GetReinforcementsLeft() const { return ReinforcementsLeft; }
	bool HasPlacedCharge() const;
	ASiegeGun* GetCurrentGun() const;
	float GetCurrentLean() const { return CurrentLean; }
	bool IsSprinting() const;
	bool IsInteracting() const { return bInteractHeld && CurrentInteraction != ESiegeInteraction::None; }
	ESiegeInteraction GetCurrentInteraction() const { return CurrentInteraction; }
	float GetInteractProgress01() const;
	float GetLastHitConfirmTime() const { return LastHitConfirmTime; }
	float GetLastDamagedTime() const { return LastDamagedTime; }
	float GetLastShotTime() const { return LastShotTime; }
	const FString& GetKillerName() const { return KillerName; }

	int32 GetDroneCount() const { return DroneCount; }
	void ConsumeDrone() { DroneCount = FMath::Max(0, DroneCount - 1); }

	/** The player or bot controller this operator belongs to, even while it is looking through a drone */
	AController* GetOwningController() const { return OwningController.Get(); }
	void SetOwningController(AController* InController) { OwningController = InController; }

	void ConsumeReinforcement() { ReinforcementsLeft = FMath::Max(0, ReinforcementsLeft - 1); }
	void ConsumeUniqueGadget() { UniqueGadgetCount = FMath::Max(0, UniqueGadgetCount - 1); }

	void SetDisplayName(const FString& InName) { DisplayName = InName; }
	const FString& GetDisplayName() const { return DisplayName; }

	/** What holding interact would do right now, or None */
	ESiegeInteraction FindAvailableInteraction(ASiegeWall*& OutWall) const;

	/** Human readable prompt for the HUD */
	FString GetInteractPrompt() const;

protected:

	virtual void BeginPlay() override;

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	virtual void Die() override;

	void UseGadget(ESiegeGadget Gadget, int32& Count);

	/** Traces from the camera and returns the first destructible panel within range */
	ASiegeWall* TraceForWall(float Distance, FHitResult& OutHit) const;

	void UpdateMovementSpeed();
	void UpdateLean(float DeltaSeconds);
	void UpdateInteraction(float DeltaSeconds);

	void OnLeanLeftPressed() { bLeanLeftHeld = true; RefreshLean(); }
	void OnLeanLeftReleased() { bLeanLeftHeld = false; RefreshLean(); }
	void OnLeanRightPressed() { bLeanRightHeld = true; RefreshLean(); }
	void OnLeanRightReleased() { bLeanRightHeld = false; RefreshLean(); }
	void RefreshLean();
	void OnObservePressed();
	void OnSprintPressed() { SetSprinting(true); }
	void OnSprintReleased() { SetSprinting(false); }
};
