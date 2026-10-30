// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "UObject/Interface.h"
#include "SiegeObservation.generated.h"

class UCameraComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UBoxComponent;
class UInputAction;
class ASiegeOperator;

UINTERFACE(MinimalAPI)
class USiegeObserverDevice : public UInterface
{
	GENERATED_BODY()
};

/**
 *  Common interface for observation devices (drones and cameras).
 *  Players possess a device to look through it; enemies can shoot it out.
 */
class SEIGE_API ISiegeObserverDevice
{
	GENERATED_BODY()

public:

	virtual uint8 GetDeviceTeam() const = 0;
	virtual FString GetDeviceName() const = 0;
	virtual bool IsDeviceAlive() const = 0;
};

namespace SiegeObservation
{
	/**
	 *  Reveals enemy operators in a view cone with line of sight to the viewing team.
	 *  Returns the number of operators spotted and the name of the first one.
	 */
	SEIGE_API int32 SpotEnemies(UWorld* World, uint8 ViewerTeam, const FVector& Eye, const FVector& Forward, float HalfAngleDegrees, float Range, float RevealDuration, AActor* IgnoreActor, FString& OutFirstName);

	/** Returns every observation device in the world belonging to a team, in a stable order */
	SEIGE_API TArray<APawn*> GetTeamDevices(UWorld* World, uint8 Team);
}

/**
 *  Small wheeled recon drone. Attackers drive it during preparation to scout the building.
 *  It fits through small holes, can hop, and dies to a single bullet.
 *  Parked drones keep passively spotting enemies in front of them.
 */
UCLASS()
class SEIGE_API ASiegeDrone : public ACharacter, public ISiegeObserverDevice
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UPointLightComponent> Headlight;

protected:

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MouseLookAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SpotAction;

	uint8 Team = 0;

	TWeakObjectPtr<ASiegeOperator> OwnerOperator;

	FString DeviceName = TEXT("Drone");

	bool bDestroyed = false;

	float LastManualSpotTime = -100.0f;
	float NextPassiveSpotTime = 0.0f;

public:

	ASiegeDrone();

	void InitDrone(ASiegeOperator* InOwner, uint8 InTeam, const FString& InName);

	virtual void Tick(float DeltaSeconds) override;

	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	// ~Begin ISiegeObserverDevice
	virtual uint8 GetDeviceTeam() const override { return Team; }
	virtual FString GetDeviceName() const override { return DeviceName; }
	virtual bool IsDeviceAlive() const override { return !bDestroyed; }
	// ~End ISiegeObserverDevice

	ASiegeOperator* GetOwnerOperator() const { return OwnerOperator.Get(); }

	/** Manually spot enemies near the center of the view */
	void Spot();

protected:

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	void OnMove(const struct FInputActionValue& Value);
	void OnLook(const struct FInputActionValue& Value);
	void OnJump();
	void OnCyclePrev();
	void OnCycleNext();
	void OnExit();
};

/**
 *  Wall-mounted security camera defenders can look through. Destroyed by a single bullet.
 *  Unpossessed cameras passively spot attackers in their field of view.
 */
UCLASS()
class SEIGE_API ASiegeSecurityCamera : public APawn, public ISiegeObserverDevice
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UBoxComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UPointLightComponent> Light;

protected:

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MouseLookAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SpotAction;

	uint8 Team = 1;

	FString DeviceName = TEXT("Camera");

	bool bDestroyed = false;

	/** Facing the camera was mounted with; look is clamped around it */
	FRotator BaseRotation = FRotator::ZeroRotator;

	float LastManualSpotTime = -100.0f;
	float NextPassiveSpotTime = 0.0f;

public:

	ASiegeSecurityCamera();

	void InitCamera(const FString& InName, uint8 InTeam);
	void SetTeam(uint8 InTeam) { Team = InTeam; }

	virtual void Tick(float DeltaSeconds) override;

	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	virtual void PossessedBy(AController* NewController) override;

	// ~Begin ISiegeObserverDevice
	virtual uint8 GetDeviceTeam() const override { return Team; }
	virtual FString GetDeviceName() const override { return DeviceName; }
	virtual bool IsDeviceAlive() const override { return !bDestroyed; }
	// ~End ISiegeObserverDevice

	void Spot();

protected:

	virtual void BeginPlay() override;

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	void OnLook(const struct FInputActionValue& Value);
	void OnCyclePrev();
	void OnCycleNext();
	void OnExit();
};
