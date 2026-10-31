// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "SiegeUserSettings.generated.h"

/** How sharp the AI operators are */
UENUM()
enum class ESiegeBotDifficulty : uint8
{
	Recruit,
	Regular,
	Veteran
};

/**
 *  Player settings for the tactical mode, saved to GameUserSettings.ini alongside the engine's video settings.
 *  Set as the engine's GameUserSettingsClassName so GEngine->GetGameUserSettings() returns this.
 */
UCLASS(config=GameUserSettings, configdonotcheckdefaults)
class SEIGE_API USiegeUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:

	/** Multiplier applied to all look input */
	UPROPERTY(config)
	float MouseSensitivity = 1.0f;

	/** Look speed multiplier while aiming down sights */
	UPROPERTY(config)
	float AimSensitivity = 0.6f;

	UPROPERTY(config)
	bool bInvertY = false;

	/** Horizontal field of view of the operator camera */
	UPROPERTY(config)
	float FieldOfView = 90.0f;

	UPROPERTY(config)
	float MasterVolume = 1.0f;

	UPROPERTY(config)
	ESiegeBotDifficulty BotDifficulty = ESiegeBotDifficulty::Regular;

	/** Returns the engine's settings object, or nullptr if the config class isn't set up */
	static USiegeUserSettings* Get();

	virtual void SetToDefaults() override;
	virtual void ApplyNonResolutionSettings() override;

	/** Applies mouse sensitivity and invert Y to a look input axis */
	static FVector2D ScaleLook(const FVector2D& Axis);

	/** Pushes the master volume to the running audio device */
	void ApplyAudio() const;
};
