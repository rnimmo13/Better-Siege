// Copyright Epic Games, Inc. All Rights Reserved.

#include "SiegeHUD.h"
#include "SiegeGameState.h"
#include "SiegeGameMode.h"
#include "SiegePlayerState.h"
#include "SiegePlayerController.h"
#include "SiegeOperator.h"
#include "SiegeObjective.h"
#include "SiegeGun.h"
#include "SiegeObservation.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"

static const FLinearColor White(1.0f, 1.0f, 1.0f, 1.0f);
static const FLinearColor Dim(0.6f, 0.6f, 0.6f, 1.0f);
static const FLinearColor Warning(1.0f, 0.75f, 0.2f, 1.0f);
static const FLinearColor Danger(1.0f, 0.25f, 0.2f, 1.0f);
static const FLinearColor Panel(0.0f, 0.0f, 0.0f, 0.55f);

void ASiegeHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	SmallFont = GEngine->GetSmallFont();
	MediumFont = GEngine->GetMediumFont();
	LargeFont = GEngine->GetLargeFont();
	UIScale = FMath::Clamp(Canvas->ClipY / 1080.0f, 0.6f, 2.0f);

	ASiegeGameState* GS = GetWorld()->GetGameState<ASiegeGameState>();
	if (!GS)
	{
		return;
	}

	if (const ASiegePlayerState* PS = PlayerOwner ? PlayerOwner->GetPlayerState<ASiegePlayerState>() : nullptr)
	{
		MyTeam = PS->Team;
	}

	ASiegePlayerController* SiegePC = Cast<ASiegePlayerController>(PlayerOwner);
	ASiegeOperator* Op = SiegePC ? SiegePC->GetOperator() : (PlayerOwner ? Cast<ASiegeOperator>(PlayerOwner->GetPawn()) : nullptr);

	DrawTopBar(GS);
	DrawRosters(GS);
	DrawKillFeed(GS);
	DrawMessages(GS, Op);

	if (GS->Phase == ESiegePhase::OperatorSelect)
	{
		DrawOperatorSelect(GS);
	}
	else if (SiegePC && SiegePC->IsObserving())
	{
		DrawWorldMarkers(GS);
		DrawObservation(Op);
	}
	else if (Op)
	{
		if (Op->IsDead())
		{
			DrawDeathInfo(Op);
		}
		else
		{
			DrawWorldMarkers(GS);
			DrawCrosshair(Op);
			DrawInteraction(Op);
			DrawOperatorStatus(Op);
		}
	}

	DrawControlsHelp(GS);
	DrawRoundBanner(GS);

	const ASiegePlayerController* PC = Cast<ASiegePlayerController>(PlayerOwner);
	if ((PC && PC->IsScoreboardShown()) || GS->Phase == ESiegePhase::MatchOver)
	{
		DrawScoreboard(GS);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Helpers

void ASiegeHUD::Text(const FString& InText, float X, float Y, const FLinearColor& Color, UFont* Font, float Scale, float AlignX)
{
	if (InText.IsEmpty() || !Font)
	{
		return;
	}

	const float FinalScale = Scale * UIScale;

	float W = 0.0f, H = 0.0f;
	Canvas->StrLen(Font, InText, W, H);
	const float DrawX = X - W * FinalScale * AlignX;

	FCanvasTextItem Shadow(FVector2D(DrawX + 1.5f, Y + 1.5f), FText::FromString(InText), Font, FLinearColor(0, 0, 0, Color.A * 0.8f));
	Shadow.Scale = FVector2D(FinalScale, FinalScale);
	Canvas->DrawItem(Shadow);

	FCanvasTextItem Item(FVector2D(DrawX, Y), FText::FromString(InText), Font, Color);
	Item.Scale = FVector2D(FinalScale, FinalScale);
	Canvas->DrawItem(Item);
}

void ASiegeHUD::Box(float X, float Y, float W, float H, const FLinearColor& Color)
{
	FCanvasTileItem Tile(FVector2D(X, Y), FVector2D(W, H), Color);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

FLinearColor ASiegeHUD::TeamColor(uint8 Team) const
{
	return Team == MyTeam ? FLinearColor(0.3f, 0.65f, 1.0f, 1.0f) : FLinearColor(1.0f, 0.45f, 0.2f, 1.0f);
}

FString ASiegeHUD::FormatTime(float Seconds)
{
	const int32 Total = FMath::CeilToInt(FMath::Max(0.0f, Seconds));
	return FString::Printf(TEXT("%d:%02d"), Total / 60, Total % 60);
}

static FString PhaseName(ESiegePhase Phase)
{
	switch (Phase)
	{
	case ESiegePhase::OperatorSelect:	return TEXT("OPERATOR SELECT");
	case ESiegePhase::Preparation:		return TEXT("PREPARATION");
	case ESiegePhase::Action:			return TEXT("ACTION");
	case ESiegePhase::PostPlant:		return TEXT("DEVICE PLANTED");
	case ESiegePhase::RoundOver:		return TEXT("ROUND OVER");
	case ESiegePhase::MatchOver:		return TEXT("MATCH OVER");
	default:							return TEXT("WAITING");
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Sections

void ASiegeHUD::DrawTopBar(ASiegeGameState* GS)
{
	const float CX = Canvas->ClipX * 0.5f;
	const float Y = 14.0f * UIScale;
	const float W = 420.0f * UIScale;
	const float H = 74.0f * UIScale;

	Box(CX - W * 0.5f, Y, W, H, Panel);

	const uint8 EnemyTeam = 1 - MyTeam;
	const bool bWeAttack = GS->AttackingTeam == MyTeam;

	// scores
	Text(FString::FromInt(GS->GetScore(MyTeam)), CX - 150.0f * UIScale, Y + 8.0f * UIScale, TeamColor(MyTeam), LargeFont, 1.6f, 0.5f);
	Text(FString::FromInt(GS->GetScore(EnemyTeam)), CX + 150.0f * UIScale, Y + 8.0f * UIScale, TeamColor(EnemyTeam), LargeFont, 1.6f, 0.5f);
	Text(bWeAttack ? TEXT("ATTACK") : TEXT("DEFEND"), CX - 150.0f * UIScale, Y + 50.0f * UIScale, TeamColor(MyTeam), SmallFont, 1.0f, 0.5f);
	Text(bWeAttack ? TEXT("DEFEND") : TEXT("ATTACK"), CX + 150.0f * UIScale, Y + 50.0f * UIScale, TeamColor(EnemyTeam), SmallFont, 1.0f, 0.5f);

	// timer
	const bool bPlanted = GS->Phase == ESiegePhase::PostPlant;
	const float Remaining = GS->GetPhaseTimeRemaining();
	const FLinearColor TimerColor = bPlanted ? Danger : (Remaining < 30.0f && GS->Phase == ESiegePhase::Action ? Warning : White);

	Text(FormatTime(Remaining), CX, Y + 6.0f * UIScale, TimerColor, LargeFont, 1.5f, 0.5f);

	FString Sub = FString::Printf(TEXT("ROUND %d  -  %s"), GS->RoundNumber, *PhaseName(GS->Phase));
	if (bPlanted)
	{
		Sub = FString::Printf(TEXT("DEVICE AT SITE %s"), *GS->PlantedSite);
	}
	Text(Sub, CX, Y + 50.0f * UIScale, bPlanted ? Danger : Dim, SmallFont, 1.0f, 0.5f);
}

void ASiegeHUD::DrawRosters(ASiegeGameState* GS)
{
	const ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>();
	if (!GM)
	{
		return;
	}

	for (uint8 Team = 0; Team < 2; ++Team)
	{
		const bool bLeft = Team == MyTeam;
		float Y = 14.0f * UIScale;
		const float X = bLeft ? 16.0f * UIScale : Canvas->ClipX - 16.0f * UIScale;

		for (const ASiegeOperator* Op : GM->GetRoundOperators())
		{
			if (!IsValid(Op) || Op->GetTeam() != Team)
			{
				continue;
			}

			const bool bDead = Op->IsDead();

			// only reveal enemy operator picks once they're dead
			FString Line = Op->GetDisplayName();
			if (Team == MyTeam || bDead)
			{
				Line += FString::Printf(TEXT("  [%s]"), *Op->GetOperatorDef().Name);
			}
			if (bDead)
			{
				Line = TEXT("X  ") + Line;
			}

			FLinearColor Color = bDead ? FLinearColor(0.4f, 0.4f, 0.4f, 0.8f) : TeamColor(Team);
			Text(Line, X, Y, Color, SmallFont, 1.0f, bLeft ? 0.0f : 1.0f);

			// health bar for teammates
			if (Team == MyTeam && !bDead)
			{
				const float BarW = 90.0f * UIScale;
				Box(X, Y + 17.0f * UIScale, BarW, 3.0f * UIScale, FLinearColor(0, 0, 0, 0.6f));
				Box(X, Y + 17.0f * UIScale, BarW * Op->GetHealth() / FMath::Max(1.0f, Op->GetMaxHealth()), 3.0f * UIScale, Color);
			}

			Y += 24.0f * UIScale;
		}
	}
}

void ASiegeHUD::DrawOperatorStatus(ASiegeOperator* Op)
{
	const float Margin = 24.0f * UIScale;
	const float Bottom = Canvas->ClipY - Margin;

	// health, bottom left
	const FSiegeOperatorDef& Def = Op->GetOperatorDef();
	Text(FString::Printf(TEXT("%s  -  %s"), *Def.Name, *Def.Role), Margin, Bottom - 78.0f * UIScale, TeamColor(MyTeam), MediumFont, 1.0f);

	const float HealthPct = Op->GetHealth() / FMath::Max(1.0f, Op->GetMaxHealth());
	const float BarW = 280.0f * UIScale;
	const float BarH = 14.0f * UIScale;
	Box(Margin, Bottom - 44.0f * UIScale, BarW, BarH, Panel);
	Box(Margin, Bottom - 44.0f * UIScale, BarW * HealthPct, BarH, HealthPct < 0.35f ? Danger : White);
	Text(FString::Printf(TEXT("%d"), FMath::CeilToInt(Op->GetHealth())), Margin + BarW + 10.0f * UIScale, Bottom - 50.0f * UIScale, White, LargeFont, 0.9f);

	// stance
	FString Stance;
	if (Op->GetCurrentLean() < -0.5f) Stance += TEXT("LEAN L  ");
	if (Op->GetCurrentLean() > 0.5f) Stance += TEXT("LEAN R  ");
	if (Op->bIsCrouched) Stance += TEXT("CROUCHED  ");
	if (Op->IsSprinting()) Stance += TEXT("SPRINTING");
	Text(Stance, Margin, Bottom - 22.0f * UIScale, Dim, SmallFont);

	// ammo, bottom right
	const float Right = Canvas->ClipX - Margin;
	if (ASiegeGun* Gun = Op->GetCurrentGun())
	{
		Text(Gun->GetGunName(), Right, Bottom - 110.0f * UIScale, Dim, SmallFont, 1.0f, 1.0f);
		Text(FString::Printf(TEXT("%d / %d"), Gun->GetBulletCount(), Gun->GetReserveAmmo()), Right, Bottom - 92.0f * UIScale,
			Gun->GetBulletCount() == 0 ? Danger : White, LargeFont, 1.3f, 1.0f);

		if (Gun->IsReloading())
		{
			Text(TEXT("RELOADING"), Right, Bottom - 128.0f * UIScale, Warning, SmallFont, 1.0f, 1.0f);
		}
	}

	// gadgets
	FString Gadgets;
	if (Def.UniqueGadget != ESiegeGadget::None)
	{
		Gadgets += FString::Printf(TEXT("[G] %s x%d"), *SiegeGadgets::GetName(Def.UniqueGadget), Op->GetUniqueGadgetCount());
	}
	if (Def.SecondaryGadget != ESiegeGadget::None)
	{
		Gadgets += FString::Printf(TEXT("    [B] %s x%d"), *SiegeGadgets::GetName(Def.SecondaryGadget), Op->GetSecondaryGadgetCount());
	}
	if (Op->HasPlacedCharge())
	{
		Gadgets += TEXT("    CHARGE READY: press again to detonate");
	}
	Text(Gadgets, Right, Bottom - 44.0f * UIScale, White, SmallFont, 1.0f, 1.0f);

	if (!Def.bAttacker)
	{
		Text(FString::Printf(TEXT("Reinforcements: %d    [X] Cameras"), Op->GetReinforcementsLeft()), Right, Bottom - 22.0f * UIScale, Dim, SmallFont, 1.0f, 1.0f);
	}
	else
	{
		Text(FString::Printf(TEXT("[X] Drones: %d"), Op->GetDroneCount()), Right, Bottom - 22.0f * UIScale, Dim, SmallFont, 1.0f, 1.0f);
	}

	// damage flash
	const float SinceDamage = GetWorld()->GetTimeSeconds() - Op->GetLastDamagedTime();
	if (SinceDamage < 0.4f)
	{
		Box(0, 0, Canvas->ClipX, Canvas->ClipY, FLinearColor(0.8f, 0.0f, 0.0f, 0.25f * (1.0f - SinceDamage / 0.4f)));
	}
}

void ASiegeHUD::DrawCrosshair(ASiegeOperator* Op)
{
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;

	const float Gap = (3.0f + Op->GetSpreadMultiplier() * 4.0f) * UIScale;
	const float Len = 7.0f * UIScale;
	const float T = FMath::Max(1.0f, 2.0f * UIScale);
	const FLinearColor Color = Op->IsSprinting() ? FLinearColor(1, 1, 1, 0.25f) : FLinearColor(1, 1, 1, 0.9f);

	// aimed down sights: just a red dot; the cross lines fade out as you zoom in
	const float Aim = Op->GetAimAlpha();
	const float DotSize = FMath::Lerp(T, 4.0f * UIScale, Aim);
	const FLinearColor DotColor = FLinearColor::LerpUsingHSV(Color, FLinearColor(1.0f, 0.15f, 0.1f, 1.0f), Aim);
	Box(CX - DotSize * 0.5f, CY - DotSize * 0.5f, DotSize, DotSize, DotColor);

	FLinearColor LineColor = Color;
	LineColor.A *= 1.0f - Aim;
	Box(CX - Gap - Len, CY - T * 0.5f, Len, T, LineColor);
	Box(CX + Gap, CY - T * 0.5f, Len, T, LineColor);
	Box(CX - T * 0.5f, CY - Gap - Len, T, Len, LineColor);
	Box(CX - T * 0.5f, CY + Gap, T, Len, LineColor);

	// hit marker
	if (GetWorld()->GetTimeSeconds() - Op->GetLastHitConfirmTime() < 0.15f)
	{
		const float S = 9.0f * UIScale;
		const float O = 5.0f * UIScale;
		DrawLine(CX - O - S, CY - O - S, CX - O, CY - O, FLinearColor::White, T);
		DrawLine(CX + O + S, CY - O - S, CX + O, CY - O, FLinearColor::White, T);
		DrawLine(CX - O - S, CY + O + S, CX - O, CY + O, FLinearColor::White, T);
		DrawLine(CX + O + S, CY + O + S, CX + O, CY + O, FLinearColor::White, T);
	}
}

void ASiegeHUD::DrawInteraction(ASiegeOperator* Op)
{
	const float CX = Canvas->ClipX * 0.5f;
	const float Y = Canvas->ClipY * 0.5f + 70.0f * UIScale;

	if (Op->IsInteracting())
	{
		FString Label;
		switch (Op->GetCurrentInteraction())
		{
		case ESiegeInteraction::PlantDevice:	Label = TEXT("PLANTING DEVICE"); break;
		case ESiegeInteraction::DisableDevice:	Label = TEXT("DISABLING DEVICE"); break;
		case ESiegeInteraction::Reinforce:		Label = TEXT("REINFORCING"); break;
		default: break;
		}

		const float W = 260.0f * UIScale;
		const float H = 10.0f * UIScale;
		Text(Label, CX, Y, Warning, MediumFont, 1.0f, 0.5f);
		Box(CX - W * 0.5f, Y + 28.0f * UIScale, W, H, Panel);
		Box(CX - W * 0.5f, Y + 28.0f * UIScale, W * Op->GetInteractProgress01(), H, Warning);
		return;
	}

	Text(Op->GetInteractPrompt(), CX, Y, White, MediumFont, 1.0f, 0.5f);
}

void ASiegeHUD::DrawWorldMarkers(ASiegeGameState* GS)
{
	const ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>();
	if (!GM || !PlayerOwner)
	{
		return;
	}

	const FVector ViewLoc = PlayerOwner->PlayerCameraManager ? PlayerOwner->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;

	// enemies revealed by intel gadgets
	for (const ASiegeOperator* Op : GM->GetRoundOperators())
	{
		if (!IsValid(Op) || Op->IsDead() || Op->GetTeam() == MyTeam || !Op->IsRevealedTo(MyTeam))
		{
			continue;
		}

		const FVector Screen = Project(Op->GetActorLocation() + FVector(0, 0, 60.0f), true);
		if (Screen.Z > 0.0f)
		{
			const float S = 8.0f * UIScale;
			DrawLine(Screen.X, Screen.Y - S, Screen.X + S, Screen.Y, Danger, 2.0f);
			DrawLine(Screen.X + S, Screen.Y, Screen.X, Screen.Y + S, Danger, 2.0f);
			DrawLine(Screen.X, Screen.Y + S, Screen.X - S, Screen.Y, Danger, 2.0f);
			DrawLine(Screen.X - S, Screen.Y, Screen.X, Screen.Y - S, Danger, 2.0f);
			Text(FString::Printf(TEXT("%dm"), FMath::RoundToInt(FVector::Dist(ViewLoc, Op->GetActorLocation()) / 100.0f)), Screen.X, Screen.Y + S + 2.0f, Danger, SmallFont, 0.9f, 0.5f);
		}
	}

	// bomb sites before the plant, the device after
	if (const ASiegeDevice* Device = GM->GetActiveDevice())
	{
		const FVector Screen = Project(Device->GetActorLocation() + FVector(0, 0, 80.0f), true);
		if (Screen.Z > 0.0f)
		{
			Text(FString::Printf(TEXT("DEVICE %s"), *FormatTime(Device->GetTimeRemaining())), Screen.X, Screen.Y, Danger, MediumFont, 1.0f, 0.5f);
		}
	}
	else
	{
		for (const TWeakObjectPtr<ASiegeBombSite>& Site : GM->GetLayout().Sites)
		{
			if (!Site.IsValid())
			{
				continue;
			}

			const FVector Screen = Project(Site->GetActorLocation() + FVector(0, 0, 60.0f), true);
			if (Screen.Z > 0.0f)
			{
				const int32 Meters = FMath::RoundToInt(FVector::Dist(ViewLoc, Site->GetActorLocation()) / 100.0f);
				Text(FString::Printf(TEXT("%s  %dm"), *Site->SiteName, Meters), Screen.X, Screen.Y, FLinearColor(1.0f, 0.85f, 0.3f, 0.8f), SmallFont, 1.1f, 0.5f);
			}
		}
	}
}

void ASiegeHUD::DrawKillFeed(ASiegeGameState* GS)
{
	const float Now = GS->GetServerWorldTimeSeconds();
	const float X = Canvas->ClipX - 16.0f * UIScale;
	float Y = 150.0f * UIScale;

	for (int32 i = GS->KillFeed.Num() - 1; i >= 0; --i)
	{
		const FSiegeKillFeedEntry& Entry = GS->KillFeed[i];
		if (Now - Entry.Time > 8.0f)
		{
			continue;
		}

		const FString Line = FString::Printf(TEXT("%s  %s  %s"), *Entry.KillerName, Entry.bHeadshot ? TEXT("[HEADSHOT]") : TEXT(">"), *Entry.VictimName);
		Box(X - 330.0f * UIScale, Y - 2.0f, 330.0f * UIScale, 22.0f * UIScale, Panel);
		Text(Line, X - 8.0f * UIScale, Y, TeamColor(Entry.KillerTeam), SmallFont, 1.0f, 1.0f);
		Y += 26.0f * UIScale;
	}
}

void ASiegeHUD::DrawMessages(ASiegeGameState* GS, ASiegeOperator* Op)
{
	const float Now = GS->GetServerWorldTimeSeconds();
	float Y = Canvas->ClipY * 0.28f;
	const float CX = Canvas->ClipX * 0.5f;

	for (const FSiegeMessage& Message : GS->Messages)
	{
		if (Now - Message.Time > 5.0f)
		{
			continue;
		}

		if (Message.Team != 255 && Message.Team != MyTeam)
		{
			continue;
		}

		if (Message.OnlyFor.IsValid() && Message.OnlyFor.Get() != Op)
		{
			continue;
		}

		const float Alpha = FMath::Clamp(5.0f - (Now - Message.Time), 0.0f, 1.0f);
		Text(Message.Text, CX, Y, FLinearColor(1.0f, 1.0f, 1.0f, Alpha), MediumFont, 1.0f, 0.5f);
		Y += 26.0f * UIScale;
	}
}

void ASiegeHUD::DrawOperatorSelect(ASiegeGameState* GS)
{
	const ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>();
	const ASiegePlayerState* PS = PlayerOwner ? PlayerOwner->GetPlayerState<ASiegePlayerState>() : nullptr;
	if (!GM || !PS)
	{
		return;
	}

	const bool bAttacking = GS->AttackingTeam == MyTeam;
	const TArray<int32> Choices = GM->GetOperatorIndicesForSide(bAttacking);

	const float W = 760.0f * UIScale;
	const float H = (120.0f + Choices.Num() * 64.0f) * UIScale;
	const float X = (Canvas->ClipX - W) * 0.5f;
	const float Y = Canvas->ClipY * 0.36f;

	Box(X, Y, W, H, FLinearColor(0, 0, 0, 0.7f));
	Text(bAttacking ? TEXT("CHOOSE YOUR ATTACKER") : TEXT("CHOOSE YOUR DEFENDER"), X + W * 0.5f, Y + 14.0f * UIScale, TeamColor(MyTeam), LargeFont, 1.1f, 0.5f);
	Text(FString::Printf(TEXT("Press 1-%d   -   deploying in %s"), Choices.Num(), *FormatTime(GS->GetPhaseTimeRemaining())), X + W * 0.5f, Y + 56.0f * UIScale, Dim, SmallFont, 1.0f, 0.5f);

	float RowY = Y + 90.0f * UIScale;
	for (int32 i = 0; i < Choices.Num(); ++i)
	{
		const FSiegeOperatorDef& Def = GM->GetOperators()[Choices[i]];
		const bool bSelected = PS->OperatorIndex == Choices[i];

		if (bSelected)
		{
			Box(X + 10.0f * UIScale, RowY - 4.0f * UIScale, W - 20.0f * UIScale, 58.0f * UIScale, FLinearColor(0.3f, 0.65f, 1.0f, 0.25f));
		}

		Text(FString::Printf(TEXT("[%d]  %s  -  %s"), i + 1, *Def.Name, *Def.Role), X + 24.0f * UIScale, RowY, bSelected ? White : TeamColor(MyTeam), MediumFont, 1.0f);

		FString Details = FString::Printf(TEXT("Health %d   Speed %d   Gadget: %s x%d"), FMath::RoundToInt(Def.MaxHealth), FMath::RoundToInt(Def.WalkSpeed), *SiegeGadgets::GetName(Def.UniqueGadget), Def.UniqueGadgetCount);
		if (Def.SecondaryGadget != ESiegeGadget::None)
		{
			Details += FString::Printf(TEXT("   + %s x%d"), *SiegeGadgets::GetName(Def.SecondaryGadget), Def.SecondaryGadgetCount);
		}
		Text(Details, X + 64.0f * UIScale, RowY + 28.0f * UIScale, Dim, SmallFont, 1.0f);

		RowY += 64.0f * UIScale;
	}
}

void ASiegeHUD::DrawRoundBanner(ASiegeGameState* GS)
{
	if (GS->Phase != ESiegePhase::RoundOver && GS->Phase != ESiegePhase::MatchOver)
	{
		return;
	}

	const bool bWon = GS->LastRoundWinner == MyTeam;
	FString Title;
	FString Reason;

	if (GS->Phase == ESiegePhase::MatchOver)
	{
		Title = GS->GetScore(MyTeam) > GS->GetScore(1 - MyTeam) ? TEXT("VICTORY") : TEXT("DEFEAT");
		Reason = FString::Printf(TEXT("Final score %d - %d   -   new match in %s"), GS->GetScore(MyTeam), GS->GetScore(1 - MyTeam), *FormatTime(GS->GetPhaseTimeRemaining()));
	}
	else
	{
		Title = bWon ? TEXT("ROUND WON") : TEXT("ROUND LOST");

		switch (GS->LastRoundReason)
		{
		case ESiegeRoundEndReason::AllDefendersEliminated:	Reason = TEXT("All defenders eliminated"); break;
		case ESiegeRoundEndReason::AllAttackersEliminated:	Reason = TEXT("All attackers eliminated"); break;
		case ESiegeRoundEndReason::TimeExpired:				Reason = TEXT("Time expired - the device was never planted"); break;
		case ESiegeRoundEndReason::DeviceDetonated:			Reason = TEXT("The device detonated"); break;
		case ESiegeRoundEndReason::DeviceDisabled:			Reason = TEXT("The device was disabled"); break;
		default: break;
		}
	}

	const float CY = Canvas->ClipY * 0.4f;
	Box(0, CY - 10.0f * UIScale, Canvas->ClipX, 100.0f * UIScale, FLinearColor(0, 0, 0, 0.6f));
	Text(Title, Canvas->ClipX * 0.5f, CY, bWon || (GS->Phase == ESiegePhase::MatchOver && Title == TEXT("VICTORY")) ? TeamColor(MyTeam) : TeamColor(1 - MyTeam), LargeFont, 2.0f, 0.5f);
	Text(Reason, Canvas->ClipX * 0.5f, CY + 58.0f * UIScale, White, MediumFont, 1.0f, 0.5f);
}

void ASiegeHUD::DrawObservation(ASiegeOperator* Op)
{
	ISiegeObserverDevice* Device = PlayerOwner ? Cast<ISiegeObserverDevice>(PlayerOwner->GetPawn()) : nullptr;
	if (!Device)
	{
		return;
	}

	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;

	// darkened frame edges to sell the camera feed
	const float Edge = 40.0f * UIScale;
	Box(0, 0, Canvas->ClipX, Edge, FLinearColor(0, 0, 0, 0.35f));
	Box(0, Canvas->ClipY - Edge, Canvas->ClipX, Edge, FLinearColor(0, 0, 0, 0.35f));

	// targeting brackets
	const float S = 26.0f * UIScale;
	const float L = 10.0f * UIScale;
	const FLinearColor Color(1.0f, 1.0f, 1.0f, 0.8f);
	DrawLine(CX - S, CY - S, CX - S + L, CY - S, Color, 2.0f);
	DrawLine(CX - S, CY - S, CX - S, CY - S + L, Color, 2.0f);
	DrawLine(CX + S, CY - S, CX + S - L, CY - S, Color, 2.0f);
	DrawLine(CX + S, CY - S, CX + S, CY - S + L, Color, 2.0f);
	DrawLine(CX - S, CY + S, CX - S + L, CY + S, Color, 2.0f);
	DrawLine(CX - S, CY + S, CX - S, CY + S - L, Color, 2.0f);
	DrawLine(CX + S, CY + S, CX + S - L, CY + S, Color, 2.0f);
	DrawLine(CX + S, CY + S, CX + S, CY + S - L, Color, 2.0f);

	const bool bDrone = Cast<ASiegeDrone>(PlayerOwner->GetPawn()) != nullptr;
	const TArray<APawn*> Devices = SiegeObservation::GetTeamDevices(GetWorld(), Device->GetDeviceTeam());
	const int32 Index = Devices.Find(PlayerOwner->GetPawn());

	Text(FString::Printf(TEXT("%s   (%d / %d)"), *Device->GetDeviceName(), Index + 1, Devices.Num()), CX, Canvas->ClipY - 110.0f * UIScale, White, MediumFont, 1.1f, 0.5f);

	FString Help = bDrone ? TEXT("[WASD] drive  [Space] hop  [LMB] spot  [Q/E] switch  [X] back") : TEXT("[Mouse] look  [LMB] spot  [Q/E] switch camera  [X] back");
	if (Op && Op->IsDead())
	{
		Help += TEXT("   (you are eliminated)");
	}
	else if (Op && bDrone)
	{
		Help += FString::Printf(TEXT("   Drones left: %d"), Op->GetDroneCount());
	}

	Text(Help, CX, Canvas->ClipY - 80.0f * UIScale, Dim, SmallFont, 1.0f, 0.5f);
}

void ASiegeHUD::DrawDeathInfo(ASiegeOperator* Op)
{
	const float CX = Canvas->ClipX * 0.5f;
	const float Y = Canvas->ClipY * 0.72f;

	Text(FString::Printf(TEXT("Killed by %s"), *Op->GetKillerName()), CX, Y, Danger, MediumFont, 1.1f, 0.5f);

	const ASiegePlayerController* PC = Cast<ASiegePlayerController>(PlayerOwner);
	if (PC && PC->GetSpectateTarget())
	{
		Text(FString::Printf(TEXT("Spectating %s  -  [LMB] next teammate"), *PC->GetSpectateTarget()->GetDisplayName()), CX, Y + 30.0f * UIScale, White, SmallFont, 1.0f, 0.5f);
	}

	Text(TEXT("[X] watch drones / cameras and spot for your team"), CX, Y + 54.0f * UIScale, Dim, SmallFont, 1.0f, 0.5f);
}

void ASiegeHUD::DrawScoreboard(ASiegeGameState* GS)
{
	const float W = 620.0f * UIScale;
	const float X = (Canvas->ClipX - W) * 0.5f;
	float Y = Canvas->ClipY * 0.5f + 60.0f * UIScale;

	TArray<ASiegePlayerState*> States;
	for (APlayerState* PS : GS->PlayerArray)
	{
		if (ASiegePlayerState* SPS = Cast<ASiegePlayerState>(PS))
		{
			States.Add(SPS);
		}
	}

	States.Sort([this](const ASiegePlayerState& A, const ASiegePlayerState& B)
	{
		if (A.Team != B.Team)
		{
			return A.Team == MyTeam;
		}
		return A.Kills > B.Kills;
	});

	Box(X, Y, W, (40.0f + States.Num() * 24.0f) * UIScale, FLinearColor(0, 0, 0, 0.7f));
	Text(TEXT("PLAYER"), X + 16.0f * UIScale, Y + 8.0f * UIScale, Dim, SmallFont);
	Text(TEXT("K"), X + W - 160.0f * UIScale, Y + 8.0f * UIScale, Dim, SmallFont);
	Text(TEXT("D"), X + W - 110.0f * UIScale, Y + 8.0f * UIScale, Dim, SmallFont);
	Text(TEXT("PLANTS"), X + W - 70.0f * UIScale, Y + 8.0f * UIScale, Dim, SmallFont);
	Y += 34.0f * UIScale;

	for (const ASiegePlayerState* PS : States)
	{
		const FLinearColor Color = TeamColor(PS->Team);
		const bool bMe = PlayerOwner && PS == PlayerOwner->PlayerState;
		Text(PS->GetPlayerName() + (bMe ? TEXT("  (you)") : TEXT("")), X + 16.0f * UIScale, Y, Color, SmallFont);
		Text(FString::FromInt(PS->Kills), X + W - 160.0f * UIScale, Y, Color, SmallFont);
		Text(FString::FromInt(PS->Deaths), X + W - 110.0f * UIScale, Y, Color, SmallFont);
		Text(FString::FromInt(PS->Plants), X + W - 60.0f * UIScale, Y, Color, SmallFont);
		Y += 24.0f * UIScale;
	}
}

void ASiegeHUD::DrawControlsHelp(ASiegeGameState* GS)
{
	if (GS->Phase != ESiegePhase::Preparation && GS->Phase != ESiegePhase::OperatorSelect)
	{
		return;
	}

	const FString Help = TEXT("[RMB] aim  [Wheel] swap weapon  [X] drones/cameras  [WASD] move  [Shift] sprint  [Ctrl/C] crouch  [Q/E] lean  [Space] vault  [R] reload  [V] melee  [F] interact  [G] gadget  [B] secondary  [Tab] scores");
	Text(Help, Canvas->ClipX * 0.5f, Canvas->ClipY - 150.0f * UIScale, Dim, SmallFont, 1.0f, 0.5f);
}
