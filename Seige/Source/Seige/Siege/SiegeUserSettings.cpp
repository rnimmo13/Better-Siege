// Copyright Epic Games, Inc. All Rights Reserved.

#include "SiegeUserSettings.h"
#include "Engine/Engine.h"
#include "AudioDevice.h"

USiegeUserSettings* USiegeUserSettings::Get()
{
	return GEngine ? Cast<USiegeUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

void USiegeUserSettings::SetToDefaults()
{
	Super::SetToDefaults();

	MouseSensitivity = 1.0f;
	AimSensitivity = 0.6f;
	bInvertY = false;
	FieldOfView = 90.0f;
	MasterVolume = 1.0f;
	BotDifficulty = ESiegeBotDifficulty::Regular;
}

void USiegeUserSettings::ApplyNonResolutionSettings()
{
	Super::ApplyNonResolutionSettings();

	ApplyAudio();
}

void USiegeUserSettings::ApplyAudio() const
{
	if (GEngine)
	{
		if (FAudioDeviceHandle Device = GEngine->GetMainAudioDevice())
		{
			Device->SetTransientPrimaryVolume(FMath::Clamp(MasterVolume, 0.0f, 1.0f));
		}
	}
}

FVector2D USiegeUserSettings::ScaleLook(const FVector2D& Axis)
{
	const USiegeUserSettings* Settings = Get();
	if (!Settings)
	{
		return Axis;
	}

	return FVector2D(Axis.X * Settings->MouseSensitivity, Axis.Y * Settings->MouseSensitivity * (Settings->bInvertY ? -1.0f : 1.0f));
}
