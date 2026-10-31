// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SWidgetSwitcher;
class SButton;
class USiegeUserSettings;

/**
 *  Front end menu: Play, Settings and Quit on the left, with a tabbed settings panel (Gameplay, Audio, Video).
 *  Settings are edited on the USiegeUserSettings object and saved when Apply is pressed; Back discards unapplied changes.
 */
class SSiegeMainMenu : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SSiegeMainMenu) {}
		SLATE_EVENT(FSimpleDelegate, OnPlay)
		SLATE_EVENT(FSimpleDelegate, OnQuit)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** Puts keyboard focus on the Play button */
	void FocusDefault();

private:

	FSimpleDelegate OnPlay;
	FSimpleDelegate OnQuit;

	bool bSettingsOpen = false;
	TSharedPtr<SWidgetSwitcher> TabSwitcher;
	TSharedPtr<SButton> PlayButton;

	TArray<FIntPoint> Resolutions;
	bool bDirty = false;

	static USiegeUserSettings* Settings();

	void ShowSettings();
	void CloseSettings();
	void ApplySettings();
	void ResetSettings();

	TSharedRef<SWidget> MakeMenuButton(const FText& Label, FSimpleDelegate OnClicked, TSharedPtr<SButton>* OutButton = nullptr);
	TSharedRef<SWidget> MakeTabButton(const FText& Label, int32 TabIndex);
	TSharedRef<SWidget> MakeFooterButton(const FText& Label, FSimpleDelegate OnClicked);

	TSharedRef<SWidget> MakeSliderRow(const FText& Label, float Min, float Max, TFunction<float()> Getter, TFunction<void(float)> Setter, TFunction<FText(float)> Format);
	TSharedRef<SWidget> MakeCycleRow(const FText& Label, TFunction<FText()> Getter, TFunction<void(int32)> Step);
	TSharedRef<SWidget> MakeCheckRow(const FText& Label, TFunction<bool()> Getter, TFunction<void(bool)> Setter);
	TSharedRef<SWidget> MakeRow(const FText& Label, TSharedRef<SWidget> Control);

	TSharedRef<SWidget> BuildGameplayTab();
	TSharedRef<SWidget> BuildAudioTab();
	TSharedRef<SWidget> BuildVideoTab();
};
