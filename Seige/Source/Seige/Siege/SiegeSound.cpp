// Copyright Epic Games, Inc. All Rights Reserved.

#include "SiegeSound.h"
#include "SiegeGameMode.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWaveProcedural.h"
#include "TimerManager.h"

namespace SiegeSynth
{
	using FBuffer = TArray<float>;

	static float LowPassCoefficient(float CutoffHz)
	{
		return 1.0f - FMath::Exp(-2.0f * PI * CutoffHz / USiegeSoundSubsystem::SampleRate);
	}

	static TArray<int16> ToPCM(const FBuffer& In)
	{
		float Peak = 0.001f;
		for (float S : In)
		{
			Peak = FMath::Max(Peak, FMath::Abs(S));
		}

		TArray<int16> Out;
		Out.SetNumUninitialized(In.Num());
		const float Scale = 0.85f * 32767.0f / Peak;
		for (int32 i = 0; i < In.Num(); ++i)
		{
			Out[i] = static_cast<int16>(FMath::Clamp(In[i] * Scale, -32767.0f, 32767.0f));
		}
		return Out;
	}

	/** Sharp noise crack with a low thump: gunfire */
	static TArray<int16> Shot(FRandomStream& R, float Length, float CrackCutoff, float CrackDecay, float ThumpHz, float ThumpDecay)
	{
		const int32 N = FMath::RoundToInt(Length * USiegeSoundSubsystem::SampleRate);
		FBuffer B;
		B.SetNumZeroed(N);

		const float A = LowPassCoefficient(CrackCutoff);
		float Filtered = 0.0f;

		for (int32 i = 0; i < N; ++i)
		{
			const float T = static_cast<float>(i) / USiegeSoundSubsystem::SampleRate;
			Filtered += A * (R.FRandRange(-1.0f, 1.0f) - Filtered);

			const float Crack = Filtered * (FMath::Exp(-T / CrackDecay) + 0.25f * FMath::Exp(-T / (CrackDecay * 5.0f)));
			const float Thump = FMath::Sin(2.0f * PI * ThumpHz * T) * FMath::Exp(-T / ThumpDecay) * 0.7f;
			B[i] = Crack * 1.6f + Thump;
		}
		return ToPCM(B);
	}

	/** Short soft-filtered tap: footsteps */
	static TArray<int16> Step(FRandomStream& R, float Cutoff)
	{
		const int32 N = FMath::RoundToInt(0.09f * USiegeSoundSubsystem::SampleRate);
		FBuffer B;
		B.SetNumZeroed(N);

		const float A = LowPassCoefficient(Cutoff);
		float Filtered = 0.0f;

		for (int32 i = 0; i < N; ++i)
		{
			const float T = static_cast<float>(i) / USiegeSoundSubsystem::SampleRate;
			Filtered += A * (R.FRandRange(-1.0f, 1.0f) - Filtered);
			const float Env = FMath::Min(1.0f, T / 0.002f) * FMath::Exp(-T / 0.018f);
			B[i] = Filtered * Env + FMath::Sin(2.0f * PI * 120.0f * T) * FMath::Exp(-T / 0.012f) * 0.15f;
		}
		return ToPCM(B);
	}

	/** Long rumbling boom: breach charges and the device */
	static TArray<int16> Boom(FRandomStream& R)
	{
		const int32 N = FMath::RoundToInt(1.6f * USiegeSoundSubsystem::SampleRate);
		FBuffer B;
		B.SetNumZeroed(N);

		const float A = LowPassCoefficient(700.0f);
		float Brown = 0.0f;
		float Filtered = 0.0f;

		for (int32 i = 0; i < N; ++i)
		{
			const float T = static_cast<float>(i) / USiegeSoundSubsystem::SampleRate;
			Brown = FMath::Clamp(Brown + R.FRandRange(-0.08f, 0.08f), -1.0f, 1.0f);
			Filtered += A * ((Brown + R.FRandRange(-0.3f, 0.3f)) - Filtered);

			const float Env = FMath::Min(1.0f, T / 0.004f) * FMath::Exp(-T / 0.38f);
			const float Rumble = FMath::Sin(2.0f * PI * 45.0f * T) * FMath::Exp(-T / 0.55f) * 0.6f;
			B[i] = Filtered * Env * 2.0f + Rumble;
		}
		return ToPCM(B);
	}

	/** Hissing fizz: thermal charge burning */
	static TArray<int16> Hiss(FRandomStream& R)
	{
		const int32 N = FMath::RoundToInt(1.2f * USiegeSoundSubsystem::SampleRate);
		FBuffer B;
		B.SetNumZeroed(N);

		const float A = LowPassCoefficient(1500.0f);
		float Low = 0.0f;

		for (int32 i = 0; i < N; ++i)
		{
			const float T = static_cast<float>(i) / USiegeSoundSubsystem::SampleRate;
			const float X = R.FRandRange(-1.0f, 1.0f);
			Low += A * (X - Low);

			// high-passed noise with crackle
			const float Crackle = (R.FRand() < 0.004f) ? R.FRandRange(-1.0f, 1.0f) * 2.0f : 0.0f;
			const float Env = FMath::Min(1.0f, T / 0.05f) * FMath::Clamp((1.2f - T) / 0.3f, 0.0f, 1.0f);
			B[i] = ((X - Low) + Crackle) * Env;
		}
		return ToPCM(B);
	}

	/** Splintering debris: barricades and walls breaking */
	static TArray<int16> Crunch(FRandomStream& R)
	{
		const int32 N = FMath::RoundToInt(0.2f * USiegeSoundSubsystem::SampleRate);
		FBuffer B;
		B.SetNumZeroed(N);

		const float A = LowPassCoefficient(2800.0f);
		float Filtered = 0.0f;

		for (int32 i = 0; i < N; ++i)
		{
			const float T = static_cast<float>(i) / USiegeSoundSubsystem::SampleRate;
			const float Impulse = (R.FRand() < 0.03f) ? R.FRandRange(-1.0f, 1.0f) * 3.0f : 0.0f;
			Filtered += A * ((R.FRandRange(-0.4f, 0.4f) + Impulse) - Filtered);
			B[i] = Filtered * FMath::Exp(-T / 0.06f);
		}
		return ToPCM(B);
	}

	/** Electronic beep: gadgets and the device */
	static TArray<int16> Beep()
	{
		const int32 N = FMath::RoundToInt(0.12f * USiegeSoundSubsystem::SampleRate);
		FBuffer B;
		B.SetNumZeroed(N);

		for (int32 i = 0; i < N; ++i)
		{
			const float T = static_cast<float>(i) / USiegeSoundSubsystem::SampleRate;
			const float Env = FMath::Min(1.0f, T / 0.005f) * FMath::Clamp((0.12f - T) / 0.02f, 0.0f, 1.0f);
			B[i] = FMath::Sin(2.0f * PI * 1900.0f * T) * Env;
		}
		return ToPCM(B);
	}

	/** Rope zip: rappelling */
	static TArray<int16> Rope(FRandomStream& R)
	{
		const int32 N = FMath::RoundToInt(0.5f * USiegeSoundSubsystem::SampleRate);
		FBuffer B;
		B.SetNumZeroed(N);

		float Filtered = 0.0f;
		for (int32 i = 0; i < N; ++i)
		{
			const float T = static_cast<float>(i) / USiegeSoundSubsystem::SampleRate;
			const float A = LowPassCoefficient(FMath::Lerp(700.0f, 3200.0f, T / 0.5f));
			Filtered += A * (R.FRandRange(-1.0f, 1.0f) - Filtered);
			B[i] = Filtered * FMath::Sin(PI * T / 0.5f);
		}
		return ToPCM(B);
	}

	/** Dull impact: melee */
	static TArray<int16> Thud(FRandomStream& R)
	{
		const int32 N = FMath::RoundToInt(0.14f * USiegeSoundSubsystem::SampleRate);
		FBuffer B;
		B.SetNumZeroed(N);

		const float A = LowPassCoefficient(500.0f);
		float Filtered = 0.0f;
		for (int32 i = 0; i < N; ++i)
		{
			const float T = static_cast<float>(i) / USiegeSoundSubsystem::SampleRate;
			Filtered += A * (R.FRandRange(-1.0f, 1.0f) - Filtered);
			B[i] = (Filtered * 2.0f + FMath::Sin(2.0f * PI * 90.0f * T)) * FMath::Exp(-T / 0.03f);
		}
		return ToPCM(B);
	}
}

void USiegeSoundSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Random.Initialize(0x5133);

	// we compute volume from the propagation path ourselves; the engine only pans the sound
	SpatialOnly = NewObject<USoundAttenuation>(this);
	SpatialOnly->Attenuation.bAttenuate = false;
	SpatialOnly->Attenuation.bSpatialize = true;

	BuildBank();
}

void USiegeSoundSubsystem::BuildBank()
{
	FRandomStream R(1337);

	Bank.Add(ESiegeSound::RifleShot, { SiegeSynth::Shot(R, 0.35f, 3500.0f, 0.045f, 70.0f, 0.06f), SiegeSynth::Shot(R, 0.35f, 3200.0f, 0.05f, 65.0f, 0.065f) });
	Bank.Add(ESiegeSound::PistolShot, { SiegeSynth::Shot(R, 0.3f, 4500.0f, 0.035f, 110.0f, 0.04f), SiegeSynth::Shot(R, 0.3f, 4200.0f, 0.038f, 105.0f, 0.045f) });
	Bank.Add(ESiegeSound::Footstep, { SiegeSynth::Step(R, 700.0f), SiegeSynth::Step(R, 900.0f), SiegeSynth::Step(R, 1100.0f), SiegeSynth::Step(R, 800.0f) });
	Bank.Add(ESiegeSound::Explosion, { SiegeSynth::Boom(R) });
	Bank.Add(ESiegeSound::ThermalHiss, { SiegeSynth::Hiss(R) });
	Bank.Add(ESiegeSound::Crunch, { SiegeSynth::Crunch(R), SiegeSynth::Crunch(R), SiegeSynth::Crunch(R) });
	Bank.Add(ESiegeSound::Beep, { SiegeSynth::Beep() });
	Bank.Add(ESiegeSound::Rope, { SiegeSynth::Rope(R) });
	Bank.Add(ESiegeSound::Melee, { SiegeSynth::Thud(R), SiegeSynth::Thud(R) });
}

float USiegeSoundSubsystem::GetRange(ESiegeSound Sound)
{
	switch (Sound)
	{
	case ESiegeSound::RifleShot:	return 6500.0f;
	case ESiegeSound::PistolShot:	return 5500.0f;
	case ESiegeSound::Footstep:		return 1500.0f;
	case ESiegeSound::Explosion:	return 9000.0f;
	case ESiegeSound::ThermalHiss:	return 2500.0f;
	case ESiegeSound::Crunch:		return 2500.0f;
	case ESiegeSound::Beep:			return 1500.0f;
	case ESiegeSound::Rope:			return 2000.0f;
	case ESiegeSound::Melee:		return 1500.0f;
	default:						return 2000.0f;
	}
}

float USiegeSoundSubsystem::GetLoudness(ESiegeSound Sound)
{
	switch (Sound)
	{
	case ESiegeSound::RifleShot:
	case ESiegeSound::PistolShot:	return 1.0f;
	case ESiegeSound::Explosion:	return 1.5f;
	case ESiegeSound::Footstep:		return 0.25f;
	case ESiegeSound::Beep:			return 0.15f;
	default:						return 0.5f;
	}
}

void USiegeSoundSubsystem::Play(const UObject* WorldContext, ESiegeSound Sound, const FVector& Location, float Volume, const AActor* Source, uint8 SourceTeam)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	if (USiegeSoundSubsystem* Subsystem = World->GetSubsystem<USiegeSoundSubsystem>())
	{
		Subsystem->PlayInternal(Sound, Location, Volume, Source, SourceTeam);
	}
}

void USiegeSoundSubsystem::PlayInternal(ESiegeSound Sound, const FVector& Location, float Volume, const AActor* Source, uint8 SourceTeam)
{
	UWorld* World = GetWorld();

	// bots hear everything, audio or not
	if (ASiegeGameMode* GM = World->GetAuthGameMode<ASiegeGameMode>())
	{
		GM->ReportNoise(Location, GetLoudness(Sound) * Volume, SourceTeam);
	}

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	FVector Listener, Front, Right;
	PC->GetAudioListenerPosition(Listener, Front, Right);

	const float Range = GetRange(Sound) * FMath::Clamp(Volume, 0.25f, 1.5f);
	const float Distance = FVector::Dist(Listener, Location);
	if (Distance > Range)
	{
		return;
	}

	auto Falloff = [Range](float D) { return FMath::Square(FMath::Clamp(1.0f - D / Range, 0.0f, 1.0f)); };
	const float Pitch = Random.FRandRange(0.94f, 1.06f);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(SiegeSoundOcclusion), false, Source);
	Params.AddIgnoredActor(PC->GetPawn());
	Params.AddIgnoredActor(PC->GetViewTarget());

	const bool bBlocked = World->LineTraceTestByChannel(Location, Listener, ECC_Visibility, Params);
	if (!bBlocked)
	{
		SpawnVoice(Sound, Location, Volume * Falloff(Distance), 0.0f, Pitch);
		return;
	}

	// muffled straight through whatever is in the way
	SpawnVoice(Sound, Location, Volume * Falloff(Distance) * 0.3f, 700.0f, Pitch);

	// and the real route: through doors, holes and stairwells
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	UNavigationPath* Path = Nav ? Nav->FindPathToLocationSynchronously(World, Location, Listener) : nullptr;

	if (!Path || !Path->IsValid() || Path->PathPoints.Num() < 2)
	{
		return;
	}

	const float PathLength = Path->GetPathLength();
	if (PathLength >= Range)
	{
		return;
	}

	// the sound appears to come from the furthest point along the route the listener can still see
	FVector Portal = Path->PathPoints.Last() + FVector(0.0f, 0.0f, 80.0f);
	for (int32 i = Path->PathPoints.Num() - 1; i >= 0; --i)
	{
		const FVector Point = Path->PathPoints[i] + FVector(0.0f, 0.0f, 80.0f);
		if (World->LineTraceTestByChannel(Point, Listener, ECC_Visibility, Params))
		{
			break;
		}
		Portal = Point;
	}

	SpawnVoice(Sound, Portal, Volume * Falloff(PathLength), 6000.0f, Pitch);
}

void USiegeSoundSubsystem::SpawnVoice(ESiegeSound Sound, const FVector& Location, float Volume, float LowPassHz, float Pitch)
{
	const TArray<TArray<int16>>* Variants = Bank.Find(Sound);
	if (!Variants || Variants->Num() == 0 || Volume < 0.01f)
	{
		return;
	}

	const TArray<int16>& PCM = (*Variants)[Random.RandRange(0, Variants->Num() - 1)];
	const float Duration = static_cast<float>(PCM.Num()) / SampleRate;

	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
	Wave->SetSampleRate(SampleRate);
	Wave->NumChannels = 1;
	Wave->Duration = Duration;
	Wave->SoundGroup = SOUNDGROUP_Effects;
	Wave->bLooping = false;
	Wave->QueueAudio(reinterpret_cast<const uint8*>(PCM.GetData()), PCM.Num() * sizeof(int16));

	UAudioComponent* Audio = UGameplayStatics::SpawnSoundAtLocation(this, Wave, Location, FRotator::ZeroRotator, FMath::Min(Volume, 1.5f), Pitch, 0.0f, SpatialOnly, nullptr, true);
	if (!Audio)
	{
		return;
	}

	if (LowPassHz > 0.0f)
	{
		Audio->SetLowPassFilterEnabled(true);
		Audio->SetLowPassFilterFrequency(LowPassHz);
	}

	// procedural waves keep playing silence once their buffer runs dry; stop them when the sound is over
	FTimerHandle StopHandle;
	GetWorld()->GetTimerManager().SetTimer(StopHandle, FTimerDelegate::CreateWeakLambda(Audio, [Audio]() { Audio->Stop(); }), Duration / Pitch + 0.05f, false);
}
