// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SiegeSound.generated.h"

class USoundAttenuation;

UENUM()
enum class ESiegeSound : uint8
{
	RifleShot,
	PistolShot,
	Footstep,
	Explosion,
	ThermalHiss,
	Crunch,
	Beep,
	Rope,
	Melee
};

/**
 *  Synthesized game sounds with Siege-style propagation.
 *
 *  Sounds don't go straight through walls: if the direct line to the listener is blocked, the sound is heard
 *  from the opening it reaches you through (found with a navigation path), quieter the longer that route is,
 *  plus a heavily muffled component through the wall itself. Blowing new holes changes the paths.
 *  Every sound is also reported to the game mode so bots can react to what they hear.
 */
UCLASS()
class SEIGE_API USiegeSoundSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> SpatialOnly;

	/** Generated 16-bit mono PCM per sound (several variants each) */
	TMap<ESiegeSound, TArray<TArray<int16>>> Bank;

	FRandomStream Random;

public:

	static constexpr int32 SampleRate = 22050;

	/** Plays a sound at a world location. SourceTeam lets bots ignore their own team's noise (255 = nobody) */
	static void Play(const UObject* WorldContext, ESiegeSound Sound, const FVector& Location, float Volume = 1.0f, const AActor* Source = nullptr, uint8 SourceTeam = 255);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

protected:

	void PlayInternal(ESiegeSound Sound, const FVector& Location, float Volume, const AActor* Source, uint8 SourceTeam);

	/** Spawns one voice of a sound. Volume is final (no engine distance attenuation) */
	void SpawnVoice(ESiegeSound Sound, const FVector& Location, float Volume, float LowPassHz, float Pitch);

	void BuildBank();

	static float GetRange(ESiegeSound Sound);
	static float GetLoudness(ESiegeSound Sound);
};
