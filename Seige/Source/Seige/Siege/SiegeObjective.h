// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SiegeObjective.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UPointLightComponent;

/**
 *  A bomb site the defenders protect. Attackers may plant the device anywhere inside the box
 */
UCLASS()
class SEIGE_API ASiegeBombSite : public AActor
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UBoxComponent> Area;

	/** Thin colored plate on the floor so players can see the plant zone */
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> FloorMarker;

public:

	UPROPERTY(EditAnywhere, Category="Site")
	FString SiteName = TEXT("A");

	ASiegeBombSite();

	/** Sets the plant zone extent (half size, cm) */
	void SetExtent(const FVector& HalfExtent);

	/** Returns true if the location is inside the plant zone */
	bool IsInside(const FVector& Location) const;

	/** Returns a random point inside the plant zone at floor height */
	FVector GetRandomPoint() const;

	FVector GetExtent() const;
};

/**
 *  The device attackers plant. Detonates when its countdown expires unless defenders disable it
 */
UCLASS()
class SEIGE_API ASiegeDevice : public AActor
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UPointLightComponent> Light;

protected:

	float TimeRemaining = 45.0f;

	bool bDisabled = false;

	bool bDetonated = false;

	float BeepTimer = 0.0f;

	/** Name of the site this device was planted on */
	FString SiteName;

public:

	ASiegeDevice();

	virtual void Tick(float DeltaSeconds) override;

	void Arm(float Countdown, const FString& InSiteName);

	void Disable();

	float GetTimeRemaining() const { return TimeRemaining; }
	bool IsActive() const { return !bDisabled && !bDetonated; }
	const FString& GetSiteName() const { return SiteName; }
};
