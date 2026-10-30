// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SiegeGadgets.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class ASiegeWall;
class ASiegeOperator;

namespace SiegeExplosion
{
	/**
	 *  Detonates an explosive: opens destructible panels within WallRadius, damages operators within
	 *  DamageRadius (only solid structure blocks the blast) and destroys gadgets caught in it.
	 *  If HardBreachWall is set, that panel is destroyed completely even if reinforced.
	 */
	SEIGE_API void Explode(UWorld* World, const FVector& Location, float WallRadius, float DamageRadius, float MaxDamage, AController* Instigator, AActor* Causer, ASiegeWall* HardBreachWall = nullptr);
}

/**
 *  Base class for deployable gadgets. Gadgets belong to a team and can be destroyed by damage
 */
UCLASS(abstract)
class SEIGE_API ASiegeGadgetBase : public AActor
{
	GENERATED_BODY()

protected:

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY(EditAnywhere, Category="Gadget")
	float Health = 1.0f;

	/** If false, bullets and melee can't destroy this gadget, only explosives */
	UPROPERTY(EditAnywhere, Category="Gadget")
	bool bVulnerableToBullets = true;

	uint8 OwnerTeam = 0;

	TWeakObjectPtr<ASiegeOperator> OwnerOperator;

	/** Panel this gadget is stuck to, if any */
	TWeakObjectPtr<ASiegeWall> AttachedWall;

	/** Display name for messages */
	FString GadgetName = TEXT("gadget");

public:

	ASiegeGadgetBase();

	/** Called right after spawning */
	virtual void InitGadget(ASiegeOperator* InOwner, ASiegeWall* InWall);

	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Called by explosions */
	virtual void OnCaughtInExplosion(AActor* Causer);

	uint8 GetOwnerTeam() const { return OwnerTeam; }
	ASiegeOperator* GetOwnerOperator() const { return OwnerOperator.Get(); }
	ASiegeWall* GetAttachedWall() const { return AttachedWall.Get(); }

protected:

	void SetLightColor(const FLinearColor& Color, float Intensity);
};

/**
 *  Breaching explosive. Soft charges are detonated by their owner and open ordinary surfaces.
 *  Thermal (hard) charges burn for a few seconds and then open a whole panel, even a reinforced one.
 *  Either kind is destroyed instantly if placed on an electrified panel.
 */
UCLASS()
class SEIGE_API ASiegeBreachCharge : public ASiegeGadgetBase
{
	GENERATED_BODY()

protected:

	bool bHardBreach = false;

	/** Time a thermal charge takes from placement to detonation */
	float BurnTime = 4.0f;

	float BurnElapsed = 0.0f;

	bool bExploded = false;

public:

	ASiegeBreachCharge();

	void SetHardBreach(bool bHard);

	virtual void InitGadget(ASiegeOperator* InOwner, ASiegeWall* InWall) override;

	virtual void Tick(float DeltaSeconds) override;

	/** Detonates the charge */
	void Detonate();

	bool IsHardBreach() const { return bHardBreach; }

	/** Returns 0-1 burn progress for thermal charges */
	float GetBurnProgress() const { return BurnTime > 0.0f ? BurnElapsed / BurnTime : 1.0f; }

protected:

	/** Destroys the charge without detonating it */
	void Fizzle(const FString& Reason);
};

/**
 *  Electrifies the panel it is attached to. Charges placed on that panel are destroyed
 */
UCLASS()
class SEIGE_API ASiegeShockBox : public ASiegeGadgetBase
{
	GENERATED_BODY()

public:

	ASiegeShockBox();

	virtual void InitGadget(ASiegeOperator* InOwner, ASiegeWall* InWall) override;

protected:

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};

/**
 *  Waist-high bulletproof cover. Only explosives destroy it
 */
UCLASS()
class SEIGE_API ASiegeShield : public ASiegeGadgetBase
{
	GENERATED_BODY()

public:

	ASiegeShield();
};

/**
 *  Floor sensor that reveals attackers walking near it to the defending team
 */
UCLASS()
class SEIGE_API ASiegeAlarmSensor : public ASiegeGadgetBase
{
	GENERATED_BODY()

protected:

	float Radius = 350.0f;

	float RevealDuration = 3.0f;

	float Cooldown = 4.0f;

	float LastTriggerTime = -100.0f;

	float NextCheckTime = 0.0f;

public:

	ASiegeAlarmSensor();

	virtual void Tick(float DeltaSeconds) override;
};
