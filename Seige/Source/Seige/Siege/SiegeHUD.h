// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SiegeHUD.generated.h"

class ASiegeGameState;
class ASiegeOperator;
class UFont;

/**
 *  Canvas HUD for the tactical mode: round timer and score, team rosters, health and ammo,
 *  gadgets, interaction prompts, kill feed, intel markers and the operator select screen.
 */
UCLASS()
class SEIGE_API ASiegeHUD : public AHUD
{
	GENERATED_BODY()

public:

	virtual void DrawHUD() override;

protected:

	UFont* SmallFont = nullptr;
	UFont* MediumFont = nullptr;
	UFont* LargeFont = nullptr;

	float UIScale = 1.0f;

	/** Team of the local player */
	uint8 MyTeam = 0;

	void DrawTopBar(ASiegeGameState* GS);
	void DrawRosters(ASiegeGameState* GS);
	void DrawOperatorStatus(ASiegeOperator* Op);
	void DrawCrosshair(ASiegeOperator* Op);
	void DrawInteraction(ASiegeOperator* Op);
	void DrawWorldMarkers(ASiegeGameState* GS);
	void DrawKillFeed(ASiegeGameState* GS);
	void DrawMessages(ASiegeGameState* GS, ASiegeOperator* Op);
	void DrawOperatorSelect(ASiegeGameState* GS);
	void DrawRoundBanner(ASiegeGameState* GS);
	void DrawDeathInfo(ASiegeOperator* Op);
	void DrawDowned(ASiegeOperator* Op);
	void DrawObservation(ASiegeOperator* Op);
	void DrawScoreboard(ASiegeGameState* GS);
	void DrawControlsHelp(ASiegeGameState* GS);

	/** Draws text with a drop shadow. Alignment: 0 left, 0.5 center, 1 right */
	void Text(const FString& InText, float X, float Y, const FLinearColor& Color, UFont* Font, float Scale = 1.0f, float AlignX = 0.0f);

	void Box(float X, float Y, float W, float H, const FLinearColor& Color);

	FLinearColor TeamColor(uint8 Team) const;

	static FString FormatTime(float Seconds);
};
