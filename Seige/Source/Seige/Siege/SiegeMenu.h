// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "SiegeMenu.generated.h"

class SSiegeMainMenu;
class SWidget;

/**
 *  Game mode for the front end map. No pawn, just the menu controller.
 */
UCLASS()
class SEIGE_API ASiegeMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:

	ASiegeMenuGameMode();
};

/**
 *  Shows the main menu on the viewport and handles its Play and Quit buttons.
 */
UCLASS()
class SEIGE_API ASiegeMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:

	/** Map opened by the Play button */
	UPROPERTY(EditAnywhere, Category="Menu")
	FString PlayMap = TEXT("/Game/Siege/Maps/Lvl_Embassy");

	/** Map opened by the Firing Range button */
	UPROPERTY(EditAnywhere, Category="Menu")
	FString RangeMap = TEXT("/Game/Siege/Maps/Lvl_Range");

protected:

	TSharedPtr<SSiegeMainMenu> Menu;
	TSharedPtr<SWidget> MenuContainer;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void StartMatch();
	void StartRange();
	void QuitGame();
};
