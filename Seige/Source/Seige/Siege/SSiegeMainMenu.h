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
 *  With bPauseMenu it becomes the in-game Esc menu: a see-through backdrop, Resume and Main Menu buttons, and settings
 *  that are kept when the panel closes. Window mode and resolution still wait for Apply so the window doesn't jump.
 */
class SSiegeMainMenu : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SSiegeMainMenu)
		: _bPauseMenu(false)
	{}
		SLATE_ARGUMENT(bool, bPauseMenu)
		SLATE_ARGUMENT(FText, SecondaryLabel)
		SLATE_EVENT(FSimpleDelegate, OnPlay)
		SLATE_EVENT(FSimpleDelegate, OnSecondary)
		SLATE_EVENT(FSimpleDelegate, OnQuit)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	/** Puts keyboard focus on the Play button */
	void FocusDefault();

	/** Applies and saves pending changes, except window mode and resolution. Pause menu only */
	void CommitSettings();

private:

	FSimpleDelegate OnPlay;
	FSimpleDelegate OnSecondary;
	FSimpleDelegate OnQuit;

	bool bPauseMenu = false;

	/** What the window is really using, so the pause menu can keep unapplied changes out of the saved settings */
	FIntPoint AppliedResolution = FIntPoint::ZeroValue;
	EWindowMode::Type AppliedWindowMode = EWindowMode::Windowed;

	/** Records the current resolution and window mode as applied */
	void RememberAppliedVideo();

	/** True when resolution or window mode was changed but not applied */
	bool HasUnappliedVideo() const;

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
