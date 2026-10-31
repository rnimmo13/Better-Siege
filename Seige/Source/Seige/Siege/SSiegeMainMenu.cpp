// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSiegeMainMenu.h"
#include "SiegeUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SiegeMainMenu"

namespace SiegeMenuStyle
{
	const FLinearColor Accent(0.95f, 0.55f, 0.1f);
	const FLinearColor TextColor(0.92f, 0.93f, 0.95f);
	const FLinearColor DimText(0.55f, 0.58f, 0.63f);

	const FSlateBrush* White()
	{
		return FCoreStyle::Get().GetBrush("GenericWhiteBox");
	}

	FSlateFontInfo Font(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	/** Flat button: dark at rest, accent when hovered */
	const FButtonStyle& Button()
	{
		static FButtonStyle Style = []
		{
			FButtonStyle S;
			S.Normal = *White();
			S.Normal.TintColor = FLinearColor(0.025f, 0.028f, 0.035f, 1.0f);
			S.Hovered = *White();
			S.Hovered.TintColor = FLinearColor(0.95f, 0.55f, 0.1f, 1.0f);
			S.Pressed = *White();
			S.Pressed.TintColor = FLinearColor(0.65f, 0.36f, 0.06f, 1.0f);
			S.Disabled = *White();
			S.Disabled.TintColor = FLinearColor(0.05f, 0.05f, 0.05f, 0.5f);
			S.NormalPadding = FMargin(0.0f);
			S.PressedPadding = FMargin(0.0f);
			return S;
		}();

		return Style;
	}

	/** Tab button: hover only lifts the background so the accent label stays readable */
	const FButtonStyle& TabButton()
	{
		static FButtonStyle Style = []
		{
			FButtonStyle S = Button();
			S.Hovered.TintColor = FLinearColor(0.07f, 0.075f, 0.09f, 1.0f);
			S.Pressed.TintColor = FLinearColor(0.04f, 0.045f, 0.055f, 1.0f);
			return S;
		}();

		return Style;
	}

	const TArray<float>& FrameLimits()
	{
		static const TArray<float> Limits = { 0.0f, 30.0f, 60.0f, 120.0f, 144.0f, 165.0f, 240.0f };
		return Limits;
	}
}

using namespace SiegeMenuStyle;

USiegeUserSettings* SSiegeMainMenu::Settings()
{
	return USiegeUserSettings::Get();
}

void SSiegeMainMenu::Construct(const FArguments& InArgs)
{
	OnPlay = InArgs._OnPlay;
	OnQuit = InArgs._OnQuit;

	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
	if (Resolutions.Num() == 0)
	{
		Resolutions = { FIntPoint(1280, 720), FIntPoint(1600, 900), FIntPoint(1920, 1080), FIntPoint(2560, 1440), FIntPoint(3840, 2160) };
	}

	ChildSlot
	[
		SNew(SOverlay)

		// backdrop
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(White())
			.BorderBackgroundColor(FLinearColor(0.006f, 0.007f, 0.01f, 1.0f))
		]

		+ SOverlay::Slot()
		[
			SNew(SHorizontalBox)

			// left column: title and main buttons
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(480.0f)
				[
					SNew(SBorder)
					.BorderImage(White())
					.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f))
					.Padding(FMargin(64.0f, 0.0f, 48.0f, 0.0f))
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.FillHeight(1.0f)
						[
							SNew(SSpacer)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("Title", "BETTER\nSIEGE"))
							.Font(Font(54, true))
							.ColorAndOpacity(TextColor)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 6.0f, 0.0f, 48.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("Subtitle", "5v5 TACTICAL BREACH  |  SOLO VS BOTS"))
							.Font(Font(12, true))
							.ColorAndOpacity(Accent)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 10.0f)
						[
							MakeMenuButton(LOCTEXT("Play", "PLAY"), FSimpleDelegate::CreateLambda([this]() { OnPlay.ExecuteIfBound(); }), &PlayButton)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 10.0f)
						[
							MakeMenuButton(LOCTEXT("Settings", "SETTINGS"), FSimpleDelegate::CreateSP(this, &SSiegeMainMenu::ShowSettings))
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							MakeMenuButton(LOCTEXT("Quit", "QUIT"), FSimpleDelegate::CreateLambda([this]() { OnQuit.ExecuteIfBound(); }))
						]

						+ SVerticalBox::Slot()
						.FillHeight(1.0f)
						[
							SNew(SSpacer)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 32.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("Build", "Pre-alpha build"))
							.Font(Font(10))
							.ColorAndOpacity(DimText)
						]
					]
				]
			]

			// right area: settings panel
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(FMargin(60.0f, 80.0f))
			.HAlign(HAlign_Left)
			[
				SNew(SBox)
				.WidthOverride(760.0f)
				.Visibility_Lambda([this]() { return bSettingsOpen ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(SBorder)
					.BorderImage(White())
					.BorderBackgroundColor(FLinearColor(0.012f, 0.014f, 0.018f, 0.97f))
					.Padding(32.0f)
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 20.0f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SettingsTitle", "SETTINGS"))
							.Font(Font(26, true))
							.ColorAndOpacity(TextColor)
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 0.0f, 0.0f, 24.0f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)[ MakeTabButton(LOCTEXT("TabGameplay", "GAMEPLAY"), 0) ]
							+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)[ MakeTabButton(LOCTEXT("TabAudio", "AUDIO"), 1) ]
							+ SHorizontalBox::Slot().AutoWidth()[ MakeTabButton(LOCTEXT("TabVideo", "VIDEO"), 2) ]
						]

						+ SVerticalBox::Slot()
						.FillHeight(1.0f)
						[
							SAssignNew(TabSwitcher, SWidgetSwitcher)
							+ SWidgetSwitcher::Slot()[ BuildGameplayTab() ]
							+ SWidgetSwitcher::Slot()[ BuildAudioTab() ]
							+ SWidgetSwitcher::Slot()[ BuildVideoTab() ]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 24.0f, 0.0f, 0.0f)
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								MakeFooterButton(LOCTEXT("Defaults", "DEFAULTS"), FSimpleDelegate::CreateSP(this, &SSiegeMainMenu::ResetSettings))
							]

							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.VAlign(VAlign_Center)
							.Padding(16.0f, 0.0f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("Unsaved", "Unsaved changes"))
								.Font(Font(11))
								.ColorAndOpacity(Accent)
								.Visibility_Lambda([this]() { return bDirty ? EVisibility::Visible : EVisibility::Hidden; })
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(0.0f, 0.0f, 8.0f, 0.0f)
							[
								MakeFooterButton(LOCTEXT("Back", "BACK"), FSimpleDelegate::CreateSP(this, &SSiegeMainMenu::CloseSettings))
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								MakeFooterButton(LOCTEXT("Apply", "APPLY"), FSimpleDelegate::CreateSP(this, &SSiegeMainMenu::ApplySettings))
							]
						]
					]
				]
			]
		]
	];
}

void SSiegeMainMenu::FocusDefault()
{
	if (PlayButton.IsValid())
	{
		FSlateApplication::Get().SetAllUserFocus(PlayButton, EFocusCause::SetDirectly);
	}
}

FReply SSiegeMainMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (bSettingsOpen && (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::Gamepad_FaceButton_Right))
	{
		CloseSettings();
		return FReply::Handled();
	}

	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SSiegeMainMenu::ShowSettings()
{
	bSettingsOpen = true;
}

void SSiegeMainMenu::CloseSettings()
{
	// throw away anything that wasn't applied
	if (bDirty)
	{
		if (USiegeUserSettings* S = Settings())
		{
			S->LoadSettings(true);
			S->ApplyAudio();
		}

		bDirty = false;
	}

	bSettingsOpen = false;
	FocusDefault();
}

void SSiegeMainMenu::ApplySettings()
{
	if (USiegeUserSettings* S = Settings())
	{
		// applies resolution, window mode, scalability and audio, then saves to GameUserSettings.ini
		S->ApplySettings(false);
	}

	bDirty = false;
}

void SSiegeMainMenu::ResetSettings()
{
	if (USiegeUserSettings* S = Settings())
	{
		S->SetToDefaults();
		S->ApplyAudio();
		bDirty = true;
	}
}

TSharedRef<SWidget> SSiegeMainMenu::MakeMenuButton(const FText& Label, FSimpleDelegate OnClicked, TSharedPtr<SButton>* OutButton)
{
	TSharedRef<SButton> NewButton = SNew(SButton)
		.ButtonStyle(&Button())
		.ContentPadding(FMargin(24.0f, 14.0f))
		.OnClicked_Lambda([OnClicked]() { OnClicked.ExecuteIfBound(); return FReply::Handled(); })
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(Font(20, true))
			.ColorAndOpacity(TextColor)
		];

	if (OutButton)
	{
		*OutButton = NewButton;
	}

	return SNew(SBox).WidthOverride(320.0f)[ NewButton ];
}

TSharedRef<SWidget> SSiegeMainMenu::MakeTabButton(const FText& Label, int32 TabIndex)
{
	return SNew(SButton)
		.ButtonStyle(&TabButton())
		.ContentPadding(FMargin(22.0f, 10.0f))
		.ButtonColorAndOpacity_Lambda([this, TabIndex]()
		{
			const bool bActive = TabSwitcher.IsValid() && TabSwitcher->GetActiveWidgetIndex() == TabIndex;
			return bActive ? FLinearColor(1.6f, 1.6f, 1.6f, 1.0f) : FLinearColor::White;
		})
		.OnClicked_Lambda([this, TabIndex]()
		{
			if (TabSwitcher.IsValid())
			{
				TabSwitcher->SetActiveWidgetIndex(TabIndex);
			}
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(Font(13, true))
			.ColorAndOpacity_Lambda([this, TabIndex]()
			{
				const bool bActive = TabSwitcher.IsValid() && TabSwitcher->GetActiveWidgetIndex() == TabIndex;
				return FSlateColor(bActive ? Accent : TextColor);
			})
		];
}

TSharedRef<SWidget> SSiegeMainMenu::MakeFooterButton(const FText& Label, FSimpleDelegate OnClicked)
{
	return SNew(SButton)
		.ButtonStyle(&Button())
		.ContentPadding(FMargin(22.0f, 10.0f))
		.OnClicked_Lambda([OnClicked]() { OnClicked.ExecuteIfBound(); return FReply::Handled(); })
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(Font(13, true))
			.ColorAndOpacity(TextColor)
		];
}

TSharedRef<SWidget> SSiegeMainMenu::MakeRow(const FText& Label, TSharedRef<SWidget> Control)
{
	return SNew(SBox)
		.HeightOverride(46.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(0.42f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(Font(14))
				.ColorAndOpacity(TextColor)
			]

			+ SHorizontalBox::Slot()
			.FillWidth(0.58f)
			.VAlign(VAlign_Center)
			[
				Control
			]
		];
}

TSharedRef<SWidget> SSiegeMainMenu::MakeSliderRow(const FText& Label, float Min, float Max, TFunction<float()> Getter, TFunction<void(float)> Setter, TFunction<FText(float)> Format)
{
	return MakeRow(Label,
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SSlider)
			.MinValue(Min)
			.MaxValue(Max)
			.SliderBarColor(FLinearColor(0.25f, 0.27f, 0.3f))
			.SliderHandleColor(Accent)
			.Value_Lambda([Getter]() { return Getter(); })
			.OnValueChanged_Lambda([this, Setter](float NewValue)
			{
				Setter(NewValue);
				bDirty = true;
			})
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(14.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(64.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([Getter, Format]() { return Format(Getter()); })
				.Font(Font(14, true))
				.ColorAndOpacity(TextColor)
				.Justification(ETextJustify::Right)
			]
		]);
}

TSharedRef<SWidget> SSiegeMainMenu::MakeCycleRow(const FText& Label, TFunction<FText()> Getter, TFunction<void(int32)> Step)
{
	auto Arrow = [this, Step](const FText& Glyph, int32 Direction)
	{
		return SNew(SButton)
			.ButtonStyle(&Button())
			.ContentPadding(FMargin(14.0f, 4.0f))
			.OnClicked_Lambda([this, Step, Direction]()
			{
				Step(Direction);
				bDirty = true;
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(Glyph)
				.Font(Font(14, true))
				.ColorAndOpacity(TextColor)
			];
	};

	return MakeRow(Label,
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			Arrow(FText::FromString(TEXT("<")), -1)
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		[
			SNew(STextBlock)
			.Text_Lambda([Getter]() { return Getter(); })
			.Font(Font(14, true))
			.ColorAndOpacity(TextColor)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			Arrow(FText::FromString(TEXT(">")), 1)
		]);
}

TSharedRef<SWidget> SSiegeMainMenu::MakeCheckRow(const FText& Label, TFunction<bool()> Getter, TFunction<void(bool)> Setter)
{
	return MakeRow(Label,
		SNew(SCheckBox)
		.IsChecked_Lambda([Getter]() { return Getter() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([this, Setter](ECheckBoxState NewState)
		{
			Setter(NewState == ECheckBoxState::Checked);
			bDirty = true;
		}));
}

TSharedRef<SWidget> SSiegeMainMenu::BuildGameplayTab()
{
	auto Multiplier = [](float Value) { return FText::FromString(FString::Printf(TEXT("%.2f"), Value)); };

	return SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeSliderRow(LOCTEXT("MouseSens", "Mouse sensitivity"), 0.1f, 3.0f,
				[]() { const USiegeUserSettings* S = Settings(); return S ? S->MouseSensitivity : 1.0f; },
				[](float V) { if (USiegeUserSettings* S = Settings()) { S->MouseSensitivity = FMath::RoundToFloat(V * 20.0f) / 20.0f; } },
				Multiplier)
		]

		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeSliderRow(LOCTEXT("AimSens", "Aim down sights sensitivity"), 0.2f, 1.0f,
				[]() { const USiegeUserSettings* S = Settings(); return S ? S->AimSensitivity : 0.6f; },
				[](float V) { if (USiegeUserSettings* S = Settings()) { S->AimSensitivity = FMath::RoundToFloat(V * 20.0f) / 20.0f; } },
				Multiplier)
		]

		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeCheckRow(LOCTEXT("InvertY", "Invert vertical look"),
				[]() { const USiegeUserSettings* S = Settings(); return S && S->bInvertY; },
				[](bool b) { if (USiegeUserSettings* S = Settings()) { S->bInvertY = b; } })
		]

		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeSliderRow(LOCTEXT("FOV", "Field of view"), 70.0f, 110.0f,
				[]() { const USiegeUserSettings* S = Settings(); return S ? S->FieldOfView : 90.0f; },
				[](float V) { if (USiegeUserSettings* S = Settings()) { S->FieldOfView = FMath::RoundToFloat(V); } },
				[](float V) { return FText::AsNumber(FMath::RoundToInt(V)); })
		]

		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeCycleRow(LOCTEXT("BotDifficulty", "Bot difficulty"),
				[]()
				{
					const USiegeUserSettings* S = Settings();
					switch (S ? S->BotDifficulty : ESiegeBotDifficulty::Regular)
					{
					case ESiegeBotDifficulty::Recruit:	return LOCTEXT("Recruit", "Recruit");
					case ESiegeBotDifficulty::Veteran:	return LOCTEXT("Veteran", "Veteran");
					default:							return LOCTEXT("Regular", "Regular");
					}
				},
				[](int32 Direction)
				{
					if (USiegeUserSettings* S = Settings())
					{
						const int32 Next = (static_cast<int32>(S->BotDifficulty) + Direction + 3) % 3;
						S->BotDifficulty = static_cast<ESiegeBotDifficulty>(Next);
					}
				})
		];
}

TSharedRef<SWidget> SSiegeMainMenu::BuildAudioTab()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeSliderRow(LOCTEXT("MasterVolume", "Master volume"), 0.0f, 1.0f,
				[]() { const USiegeUserSettings* S = Settings(); return S ? S->MasterVolume : 1.0f; },
				[](float V)
				{
					if (USiegeUserSettings* S = Settings())
					{
						// previewed right away so the level can be judged
						S->MasterVolume = FMath::RoundToFloat(V * 100.0f) / 100.0f;
						S->ApplyAudio();
					}
				},
				[](float V) { return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(V * 100.0f))); })
		];
}

TSharedRef<SWidget> SSiegeMainMenu::BuildVideoTab()
{
	return SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeCycleRow(LOCTEXT("WindowMode", "Window mode"),
				[]()
				{
					const USiegeUserSettings* S = Settings();
					switch (S ? S->GetFullscreenMode() : EWindowMode::Windowed)
					{
					case EWindowMode::Fullscreen:			return LOCTEXT("Fullscreen", "Fullscreen");
					case EWindowMode::WindowedFullscreen:	return LOCTEXT("Borderless", "Borderless");
					default:								return LOCTEXT("Windowed", "Windowed");
					}
				},
				[](int32 Direction)
				{
					if (USiegeUserSettings* S = Settings())
					{
						const int32 Next = (static_cast<int32>(S->GetFullscreenMode()) + Direction + EWindowMode::NumWindowModes) % EWindowMode::NumWindowModes;
						S->SetFullscreenMode(EWindowMode::ConvertIntToWindowMode(Next));
					}
				})
		]

		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeCycleRow(LOCTEXT("Resolution", "Resolution"),
				[]()
				{
					const USiegeUserSettings* S = Settings();
					const FIntPoint Res = S ? S->GetScreenResolution() : FIntPoint::ZeroValue;
					return FText::FromString(FString::Printf(TEXT("%d x %d"), Res.X, Res.Y));
				},
				[this](int32 Direction)
				{
					USiegeUserSettings* S = Settings();
					if (!S || Resolutions.Num() == 0)
					{
						return;
					}

					int32 Index = Resolutions.Find(S->GetScreenResolution());
					Index = Index == INDEX_NONE ? 0 : (Index + Direction + Resolutions.Num()) % Resolutions.Num();
					S->SetScreenResolution(Resolutions[Index]);
				})
		]

		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeCycleRow(LOCTEXT("Quality", "Graphics quality"),
				[]()
				{
					const USiegeUserSettings* S = Settings();
					switch (S ? S->GetOverallScalabilityLevel() : -1)
					{
					case 0:		return LOCTEXT("Low", "Low");
					case 1:		return LOCTEXT("Medium", "Medium");
					case 2:		return LOCTEXT("High", "High");
					case 3:		return LOCTEXT("Epic", "Epic");
					case 4:		return LOCTEXT("Cinematic", "Cinematic");
					default:	return LOCTEXT("Custom", "Custom");
					}
				},
				[](int32 Direction)
				{
					if (USiegeUserSettings* S = Settings())
					{
						const int32 Current = S->GetOverallScalabilityLevel();
						const int32 Next = Current < 0 ? 3 : (Current + Direction + 5) % 5;
						S->SetOverallScalabilityLevel(Next);
					}
				})
		]

		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeCheckRow(LOCTEXT("VSync", "VSync"),
				[]() { const USiegeUserSettings* S = Settings(); return S && S->IsVSyncEnabled(); },
				[](bool b) { if (USiegeUserSettings* S = Settings()) { S->SetVSyncEnabled(b); } })
		]

		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeCycleRow(LOCTEXT("FrameLimit", "Frame rate limit"),
				[]()
				{
					const USiegeUserSettings* S = Settings();
					const float Limit = S ? S->GetFrameRateLimit() : 0.0f;
					return Limit <= 0.0f ? LOCTEXT("Unlimited", "Unlimited") : FText::AsNumber(FMath::RoundToInt(Limit));
				},
				[](int32 Direction)
				{
					if (USiegeUserSettings* S = Settings())
					{
						const TArray<float>& Limits = FrameLimits();
						int32 Index = Limits.IndexOfByPredicate([S](float L) { return FMath::IsNearlyEqual(L, S->GetFrameRateLimit(), 0.5f); });
						Index = Index == INDEX_NONE ? 0 : (Index + Direction + Limits.Num()) % Limits.Num();
						S->SetFrameRateLimit(Limits[Index]);
					}
				})
		];
}

#undef LOCTEXT_NAMESPACE
