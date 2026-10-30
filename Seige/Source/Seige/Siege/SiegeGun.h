// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShooterWeapon.h"
#include "SiegeGun.generated.h"

/**
 *  Hitscan firearm. Bullets chip and pass through soft surfaces with reduced damage,
 *  damage falls off with distance and a clean headshot is lethal.
 *  Adds limited reserve ammo and timed reloads on top of the template weapon.
 */
UCLASS(abstract)
class SEIGE_API ASiegeGun : public AShooterWeapon
{
	GENERATED_BODY()

protected:

	/** Display name for the HUD */
	UPROPERTY(EditAnywhere, Category="Siege|Gun")
	FString GunName = TEXT("Rifle");

	/** Body shot damage at close range */
	UPROPERTY(EditAnywhere, Category="Siege|Gun", meta=(ClampMin=0))
	float Damage = 40.0f;

	/** If true, a headshot kills regardless of remaining health */
	UPROPERTY(EditAnywhere, Category="Siege|Gun")
	bool bHeadshotLethal = true;

	/** Distance at which damage starts to fall off */
	UPROPERTY(EditAnywhere, Category="Siege|Gun", meta=(Units="cm"))
	float FalloffStart = 2500.0f;

	/** Distance at which damage reaches its minimum */
	UPROPERTY(EditAnywhere, Category="Siege|Gun", meta=(Units="cm"))
	float FalloffEnd = 4500.0f;

	/** Damage multiplier at and beyond FalloffEnd */
	UPROPERTY(EditAnywhere, Category="Siege|Gun", meta=(ClampMin=0, ClampMax=1))
	float FalloffMinMultiplier = 0.6f;

	UPROPERTY(EditAnywhere, Category="Siege|Gun", meta=(Units="cm"))
	float Range = 10000.0f;

	/** Base bullet spread cone half-angle */
	UPROPERTY(EditAnywhere, Category="Siege|Gun", meta=(Units="Degrees"))
	float SpreadDegrees = 0.6f;

	/** Number of spare magazines carried */
	UPROPERTY(EditAnywhere, Category="Siege|Gun", meta=(ClampMin=0))
	int32 SpareMagazines = 4;

	UPROPERTY(EditAnywhere, Category="Siege|Gun", meta=(Units="s"))
	float ReloadTime = 2.4f;

	/** Max number of soft surfaces a bullet can pass through */
	UPROPERTY(EditAnywhere, Category="Siege|Gun")
	int32 MaxPenetrations = 2;

	/** If true, draw short-lived tracer lines for fired bullets */
	UPROPERTY(EditAnywhere, Category="Siege|Gun")
	bool bDrawTracers = true;

	int32 ReserveAmmo = 0;

	bool bReloading = false;

	FTimerHandle ReloadTimer;

public:

	ASiegeGun();

	/** Starts a reload if possible */
	void Reload();

	/** Cancels an in-progress reload */
	void CancelReload();

	bool IsReloading() const { return bReloading; }
	int32 GetReserveAmmo() const { return ReserveAmmo; }
	const FString& GetGunName() const { return GunName; }
	bool IsFullAuto() const { return bFullAuto; }

protected:

	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

	virtual void Fire() override;
	virtual void FireProjectile(const FVector& TargetLocation) override;

	void FinishReload();

	float GetDamageAtDistance(float Distance) const;
};
