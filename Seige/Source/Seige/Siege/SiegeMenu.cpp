// Copyright Epic Games, Inc. All Rights Reserved.

#include "SiegeMenu.h"
#include "Seige.h"
#include "SSiegeMainMenu.h"
#include "SiegeUserSettings.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Widgets/SWeakWidget.h"

ASiegeMenuGameMode::ASiegeMenuGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = ASiegeMenuPlayerController::StaticClass();
}

void ASiegeMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalPlayerController())
	{
		return;
	}

	if (USiegeUserSettings* Settings = USiegeUserSettings::Get())
	{
		Settings->ApplyAudio();
	}

	UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	if (!Viewport)
	{
		return;
	}

	Menu = SNew(SSiegeMainMenu)
		.OnPlay(FSimpleDelegate::CreateUObject(this, &ASiegeMenuPlayerController::StartMatch))
		.OnQuit(FSimpleDelegate::CreateUObject(this, &ASiegeMenuPlayerController::QuitGame));

	MenuContainer = SNew(SWeakWidget).PossiblyNullContent(Menu);
	Viewport->AddViewportWidgetContent(MenuContainer.ToSharedRef(), 10);

	bShowMouseCursor = true;

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(Menu);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);

	Menu->FocusDefault();
}

void ASiegeMenuPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (MenuContainer.IsValid())
	{
		if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
		{
			Viewport->RemoveViewportWidgetContent(MenuContainer.ToSharedRef());
		}
	}

	MenuContainer.Reset();
	Menu.Reset();

	Super::EndPlay(EndPlayReason);
}

void ASiegeMenuPlayerController::StartMatch()
{
	UE_LOG(LogSeige, Log, TEXT("Menu: starting match on %s"), *PlayMap);
	UGameplayStatics::OpenLevel(this, FName(*PlayMap));
}

void ASiegeMenuPlayerController::QuitGame()
{
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}
