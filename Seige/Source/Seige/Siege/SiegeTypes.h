// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SiegeTypes.generated.h"

class AShooterWeapon;

/** Phases of a single round */
UENUM(BlueprintType)
enum class ESiegePhase : uint8
{
	WaitingToStart,
	OperatorSelect,
	Preparation,
	Action,
	PostPlant,
	RoundOver,
	MatchOver
};

/** Unique and secondary gadget types */
UENUM(BlueprintType)
enum class ESiegeGadget : uint8
{
	None,
	BreachCharge,		// soft breach: opens ordinary walls, floors and barricades
	ThermalCharge,		// hard breach: opens reinforced walls
	PulseScanner,		// reveals nearby enemies through walls for a few seconds
	ShockBox,			// electrifies a wall panel, destroying charges placed on it
	DeployableShield,	// bulletproof waist-high cover
	AlarmSensor			// reveals attackers that walk past it
};

/** How a round ended */
UENUM(BlueprintType)
enum class ESiegeRoundEndReason : uint8
{
	None,
	AllDefendersEliminated,
	AllAttackersEliminated,
	TimeExpired,
	DeviceDetonated,
	DeviceDisabled
};

/** Static definition of a playable operator */
USTRUCT(BlueprintType)
struct FSiegeOperatorDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Operator")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Operator")
	FString Role;

	/** true for attackers, false for defenders */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Operator")
	bool bAttacker = true;

	/** 100, 110 or 125 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Operator")
	float MaxHealth = 110.0f;

	/** Base walk speed in cm/s. Lower health operators move faster */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Operator")
	float WalkSpeed = 380.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Operator")
	TSubclassOf<AShooterWeapon> PrimaryWeapon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Operator")
	TSubclassOf<AShooterWeapon> SecondaryWeapon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Operator")
	ESiegeGadget UniqueGadget = ESiegeGadget::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Operator")
	int32 UniqueGadgetCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Operator")
	ESiegeGadget SecondaryGadget = ESiegeGadget::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Operator")
	int32 SecondaryGadgetCount = 0;
};

/** A single line in the kill feed */
USTRUCT(BlueprintType)
struct FSiegeKillFeedEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="KillFeed")
	FString KillerName;

	UPROPERTY(BlueprintReadOnly, Category="KillFeed")
	FString VictimName;

	UPROPERTY(BlueprintReadOnly, Category="KillFeed")
	uint8 KillerTeam = 0;

	UPROPERTY(BlueprintReadOnly, Category="KillFeed")
	uint8 VictimTeam = 0;

	UPROPERTY(BlueprintReadOnly, Category="KillFeed")
	bool bHeadshot = false;

	UPROPERTY(BlueprintReadOnly, Category="KillFeed")
	float Time = 0.0f;
};

namespace SiegeTags
{
	/** Actor tag for indestructible structural geometry */
	static const FName Solid(TEXT("SiegeSolid"));
}

namespace SiegeGadgets
{
	inline FString GetName(ESiegeGadget Gadget)
	{
		switch (Gadget)
		{
		case ESiegeGadget::BreachCharge:		return TEXT("Breach Charge");
		case ESiegeGadget::ThermalCharge:		return TEXT("Thermal Charge");
		case ESiegeGadget::PulseScanner:		return TEXT("Pulse Scanner");
		case ESiegeGadget::ShockBox:			return TEXT("Shock Box");
		case ESiegeGadget::DeployableShield:	return TEXT("Deployable Shield");
		case ESiegeGadget::AlarmSensor:			return TEXT("Alarm Sensor");
		default:								return TEXT("None");
		}
	}
}
