// Copyright Epic Games, Inc. All Rights Reserved.

#include "SiegeRangeGameMode.h"
#include "Seige.h"
#include "SiegeGadgets.h"
#include "SiegeGameState.h"
#include "SiegeGun.h"
#include "SiegeObjective.h"
#include "SiegeObservation.h"
#include "SiegeOperator.h"
#include "SiegePlayerController.h"
#include "SiegePlayerState.h"
#include "SiegeUserSettings.h"
#include "AIController.h"
#include "AudioDevice.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Containers/Ticker.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace
{
	/** Long enough that the Action phase never runs out; nothing is timed off it in the range */
	constexpr float SandboxPhaseTime = 100000.0f;

	constexpr int32 MaxRecentHits = 6;
}

ASiegeRangeGameMode::ASiegeRangeGameMode()
{
	PrimaryActorTick.bCanEverTick = true;

	// short enough to try planting, disabling and the blast without a long wait
	DeviceCountdown = 20.0f;
}

void ASiegeRangeGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	if (ASiegePlayerState* PS = NewPlayer->GetPlayerState<ASiegePlayerState>())
	{
		PS->Team = 0;
		PS->SetPlayerName(TEXT("You"));
		PS->OperatorIndex = 0;
	}

	if (!bMatchStarted)
	{
		bMatchStarted = true;

		FTimerHandle StartTimer;
		GetWorldTimerManager().SetTimer(StartTimer, this, &ASiegeRangeGameMode::StartRange, 0.5f, false);
	}
}

void ASiegeRangeGameMode::EnterSandboxPhase()
{
	SetPhase(ESiegePhase::Action, SandboxPhaseTime);
}

void ASiegeRangeGameMode::ApplyPlayerSide(bool bAttacker)
{
	ASiegeGameState* GS = GetSiegeGameState();
	if (!GS)
	{
		return;
	}

	// the player is always team 0, so being a defender means team 1 attacks
	GS->AttackingTeam = bAttacker ? 0 : 1;

	for (const TWeakObjectPtr<ASiegeSecurityCamera>& Cam : Layout.Cameras)
	{
		if (Cam.IsValid())
		{
			Cam->SetTeam(1 - GS->AttackingTeam);
		}
	}
}

void ASiegeRangeGameMode::StartRange()
{
	ASiegeGameState* GS = GetSiegeGameState();
	if (!GS || Operators.Num() == 0 || !OperatorClass)
	{
		UE_LOG(LogSeige, Error, TEXT("Range: missing game state, roster or operator class"));
		return;
	}

	DestroyDummyControllers();
	Dummies.Reset();
	CleanupRound();

	GS->RoundNumber = 1;
	GS->PlantedSite.Reset();
	GS->KillFeed.Reset();
	PlayerRespawnAt = -1.0f;

	if (ASiegePlayerState* PS = GetWorld()->GetFirstPlayerController() ? GetWorld()->GetFirstPlayerController()->GetPlayerState<ASiegePlayerState>() : nullptr)
	{
		PS->Kills = 0;
		PS->Deaths = 0;
		PS->Plants = 0;
	}

	SiegeLayout::BuildRange(GetWorld(), Layout);
	EnterSandboxPhase();

	for (const FSiegeDummySpot& Spot : Layout.DummySpots)
	{
		FRangeDummy& Dummy = Dummies.AddDefaulted_GetRef();
		Dummy.Spot = Spot;
		SpawnDummy(Dummy);
	}

	if (ASiegePlayerController* PC = Cast<ASiegePlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		const ASiegePlayerState* PS = PC->GetPlayerState<ASiegePlayerState>();
		const int32 Index = PS && Operators.IsValidIndex(PS->OperatorIndex) ? PS->OperatorIndex : 0;

		ApplyPlayerSide(Operators[Index].bAttacker);

		if (Layout.AttackerSpawns.Num() > 0)
		{
			SpawnPlayerOperator(PC, Layout.AttackerSpawns[0]);
		}
	}

	UE_LOG(LogSeige, Log, TEXT("Range: built with %d targets"), Dummies.Num());

	if (!bSelfTestDone && FParse::Param(FCommandLine::Get(), TEXT("RangeSelfTest")))
	{
		FTimerHandle TestTimer;
		GetWorldTimerManager().SetTimer(TestTimer, this, &ASiegeRangeGameMode::RunSelfTest, 2.0f, false);
	}
}

void ASiegeRangeGameMode::ResetRange()
{
	RecentHits.Reset();
	StartRange();
	PostGlobalMessage(TEXT("Range reset"));
}

void ASiegeRangeGameMode::SpawnPlayerOperator(ASiegePlayerController* PC, const FTransform& At)
{
	const ASiegePlayerState* PS = PC ? PC->GetPlayerState<ASiegePlayerState>() : nullptr;
	if (!PS || !Operators.IsValidIndex(PS->OperatorIndex))
	{
		return;
	}

	const FSiegeOperatorDef& Def = Operators[PS->OperatorIndex];

	ASiegeOperator* Op = GetWorld()->SpawnActorDeferred<ASiegeOperator>(OperatorClass, At, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Op)
	{
		return;
	}

	Op->AutoPossessAI = EAutoPossessAI::Disabled;
	Op->FinishSpawning(At);

	Op->SetTeam(PS->Team);
	Op->SetDisplayName(PS->GetPlayerName());

	// possess before giving weapons: the weapon anim blueprints read the controller
	Op->SetOwningController(PC);
	PC->Possess(Op);
	PC->SetControlRotation(At.Rotator());

	Op->ApplyOperatorDef(Def, Def.bAttacker ? 0 : RangeReinforcements);

	RoundOperators.Add(Op);

	PC->SetHomeOperator(Op);
	PC->OnRoundStarted();
}

void ASiegeRangeGameMode::SpawnDummy(FRangeDummy& Dummy)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AAIController* Controller = GetWorld()->SpawnActor<AAIController>(AAIController::StaticClass(), Params);
	if (!Controller)
	{
		return;
	}

	const FTransform& At = Dummy.Spot.Transform;

	ASiegeOperator* Op = GetWorld()->SpawnActorDeferred<ASiegeOperator>(OperatorClass, At, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Op)
	{
		Controller->Destroy();
		return;
	}

	Op->AutoPossessAI = EAutoPossessAI::Disabled;
	Op->FinishSpawning(At);

	Op->SetTeam(Dummy.Spot.Team);
	Op->SetDisplayName(Dummy.Spot.Name);
	Op->SetOwningController(Controller);
	Controller->Possess(Op);
	Controller->SetControlRotation(At.Rotator());

	// holding a gun keeps the normal stance; the idle controller never fires it
	FSiegeOperatorDef Def = Operators[0];
	Def.Name = Dummy.Spot.Name;
	Def.MaxHealth = Dummy.Spot.MaxHealth;
	Def.UniqueGadget = ESiegeGadget::None;
	Def.UniqueGadgetCount = 0;
	Def.SecondaryGadget = ESiegeGadget::None;
	Def.SecondaryGadgetCount = 0;
	Op->ApplyOperatorDef(Def, 0);

	RoundOperators.Add(Op);

	Dummy.Operator = Op;
	Dummy.Controller = Controller;
	Dummy.RespawnAt = -1.0f;
	Dummy.StrafeDirection = 1.0f;
}

void ASiegeRangeGameMode::RemoveOperator(ASiegeOperator* Op)
{
	if (!IsValid(Op))
	{
		return;
	}

	RoundOperators.Remove(Op);

	// a soft charge nobody can detonate any more would sit on the wall until the next reset. Thermals burn through
	// on their own, and other gadgets stay on purpose, so a shock box placed as a defender can be tested against
	// charges placed as an attacker
	TArray<ASiegeBreachCharge*> Charges;
	for (TActorIterator<ASiegeBreachCharge> It(GetWorld()); It; ++It)
	{
		if (It->GetOwnerOperator() == Op && !It->IsHardBreach())
		{
			Charges.Add(*It);
		}
	}
	for (ASiegeBreachCharge* Charge : Charges)
	{
		Charge->Destroy();
	}

	// each operator brings fresh drones, so the old ones would otherwise pile up
	TArray<ASiegeDrone*> Drones;
	for (TActorIterator<ASiegeDrone> It(GetWorld()); It; ++It)
	{
		if (It->GetOwnerOperator() == Op)
		{
			Drones.Add(*It);
		}
	}
	for (ASiegeDrone* Drone : Drones)
	{
		Drone->Destroy();
	}

	if (AController* C = Op->GetController())
	{
		C->UnPossess();
	}

	Op->Destroy();
}

void ASiegeRangeGameMode::DestroyDummyControllers()
{
	for (const FRangeDummy& Dummy : Dummies)
	{
		if (Dummy.Controller.IsValid())
		{
			Dummy.Controller->Destroy();
		}
	}
}

void ASiegeRangeGameMode::RequestOperator(APlayerController* Player, int32 SideIndex)
{
	ASiegePlayerController* PC = Cast<ASiegePlayerController>(Player);
	ASiegePlayerState* PS = PC ? PC->GetPlayerState<ASiegePlayerState>() : nullptr;
	if (!PS || !Operators.IsValidIndex(SideIndex) || PC->IsPauseMenuOpen())
	{
		return;
	}

	// swap in place, so a gadget can be tried on the wall in front of you
	ASiegeOperator* Old = PC->GetOperator();
	FTransform At = Layout.AttackerSpawns.Num() > 0 ? Layout.AttackerSpawns[0] : FTransform::Identity;
	if (IsValid(Old) && !Old->IsDead())
	{
		// through a drone or camera the control rotation is the device's, not the body's
		const float Yaw = PC->IsObserving() ? Old->GetActorRotation().Yaw : PC->GetControlRotation().Yaw;

		// the new operator stands up, so a crouched or downed body's center is too low for it
		const float StandUp = Old->GetDefaultHalfHeight() - Old->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

		At = FTransform(FRotator(0.0f, Yaw, 0.0f), Old->GetActorLocation() + FVector(0.0f, 0.0f, StandUp));
	}

	RemoveOperator(Old);

	PS->OperatorIndex = SideIndex;
	PlayerRespawnAt = -1.0f;

	ApplyPlayerSide(Operators[SideIndex].bAttacker);
	SpawnPlayerOperator(PC, At);

	PostGlobalMessage(FString::Printf(TEXT("%s - %s"), *Operators[SideIndex].Name, Operators[SideIndex].bAttacker ? TEXT("attacking") : TEXT("defending")));
}

bool ASiegeRangeGameMode::CanOperatorGoDown(const ASiegeOperator* Victim) const
{
	return Victim && Victim->GetTeam() == 0 && Super::CanOperatorGoDown(Victim);
}

void ASiegeRangeGameMode::OnOperatorDamaged(ASiegeOperator* Victim, float Damage, FName HitBone, AController* DamageInstigator, bool bHeadshot)
{
	const APlayerController* Shooter = Cast<APlayerController>(DamageInstigator);
	if (!Victim || !Shooter)
	{
		return;
	}

	const FString Bone = HitBone.ToString().ToLower();

	FSiegeRangeHit Hit;
	Hit.Victim = Victim->GetDisplayName();
	Hit.Damage = Damage;
	Hit.HealthLeft = FMath::Max(0.0f, Victim->GetHealth() - Damage);
	Hit.bHeadshot = bHeadshot;

	if (HitBone.IsNone())
	{
		Hit.Zone = TEXT("melee/blast");
	}
	else if (bHeadshot)
	{
		Hit.Zone = TEXT("head");
	}
	else if (Bone.Contains(TEXT("thigh")) || Bone.Contains(TEXT("calf")) || Bone.Contains(TEXT("foot")))
	{
		Hit.Zone = TEXT("leg");
	}
	else
	{
		Hit.Zone = TEXT("body");
	}

	if (const ASiegePlayerController* PC = Cast<ASiegePlayerController>(Shooter); PC && PC->GetOperator())
	{
		Hit.Distance = FVector::Dist(PC->GetOperator()->GetActorLocation(), Victim->GetActorLocation()) / 100.0f;
	}

	RecentHits.Insert(Hit, 0);
	if (RecentHits.Num() > MaxRecentHits)
	{
		RecentHits.SetNum(MaxRecentHits);
	}
}

void ASiegeRangeGameMode::EndRound(uint8 Winner, ESiegeRoundEndReason Reason)
{
	ASiegeGameState* GS = GetSiegeGameState();
	if (!GS)
	{
		return;
	}

	if (Reason == ESiegeRoundEndReason::DeviceDetonated)
	{
		PostGlobalMessage(TEXT("Device detonated"));
	}

	if (ActiveDevice && ActiveDevice->IsActive())
	{
		ActiveDevice->Disable();
	}

	GS->PlantedSite.Reset();
	EnterSandboxPhase();

	// the game mode still uses the device as the source of the blast right after this returns
	TWeakObjectPtr<ASiegeDevice> OldDevice = ActiveDevice.Get();
	FTimerHandle CleanupTimer;
	GetWorldTimerManager().SetTimer(CleanupTimer, FTimerDelegate::CreateLambda([OldDevice]()
	{
		if (OldDevice.IsValid())
		{
			OldDevice->Destroy();
		}
	}), 3.0f, false);
}

void ASiegeRangeGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float Now = GetWorld()->GetTimeSeconds();
	TickDummies(Now);
	TickPlayer(Now);
}

void ASiegeRangeGameMode::TickDummies(float Now)
{
	for (FRangeDummy& Dummy : Dummies)
	{
		ASiegeOperator* Op = Dummy.Operator.Get();

		if (!Op || Op->IsDead())
		{
			if (Dummy.RespawnAt < 0.0f)
			{
				Dummy.RespawnAt = Now + DummyRespawnTime;
			}
			else if (Now >= Dummy.RespawnAt)
			{
				RemoveOperator(Op);

				if (Dummy.Controller.IsValid())
				{
					Dummy.Controller->Destroy();
				}

				SpawnDummy(Dummy);
			}
			continue;
		}

		if (Dummy.Spot.StrafeDistance > 0.0f && !Op->IsDowned())
		{
			const FVector Right = Op->GetActorRightVector();
			const float Offset = FVector::DotProduct(Op->GetActorLocation() - Dummy.Spot.Transform.GetLocation(), Right);

			if (Offset > Dummy.Spot.StrafeDistance)
			{
				Dummy.StrafeDirection = -1.0f;
			}
			else if (Offset < -Dummy.Spot.StrafeDistance)
			{
				Dummy.StrafeDirection = 1.0f;
			}

			Op->DoMove(Dummy.StrafeDirection, 0.0f);
		}
	}
}

void ASiegeRangeGameMode::TickPlayer(float Now)
{
	ASiegePlayerController* PC = Cast<ASiegePlayerController>(GetWorld()->GetFirstPlayerController());
	ASiegeOperator* Op = PC ? PC->GetOperator() : nullptr;

	if (!Op || !Op->IsDead() || Layout.AttackerSpawns.Num() == 0)
	{
		return;
	}

	if (PlayerRespawnAt < 0.0f)
	{
		PlayerRespawnAt = Now + PlayerRespawnTime;
	}
	else if (Now >= PlayerRespawnAt)
	{
		PlayerRespawnAt = -1.0f;
		RemoveOperator(Op);
		SpawnPlayerOperator(PC, Layout.AttackerSpawns[0]);
	}
}

void ASiegeRangeGameMode::SelfTestCheck(const TCHAR* Name, bool bPassed)
{
	UE_LOG(LogSeige, Display, TEXT("RangeSelfTest %s: %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"), Name);
	SelfTestFailures += bPassed ? 0 : 1;
}

void ASiegeRangeGameMode::RunSelfTest()
{
	// screenshots land in Saved/Screenshots when running with a real renderer
	FScreenshotRequest::RequestScreenshot(true);

	ASiegePlayerController* PC = Cast<ASiegePlayerController>(GetWorld()->GetFirstPlayerController());
	ASiegeOperator* Player = PC ? PC->GetOperator() : nullptr;

	SelfTestCheck(TEXT("player has an operator"), IsValid(Player));
	SelfTestCheck(TEXT("every target spawned"), Dummies.Num() > 0 && !Dummies.ContainsByPredicate([](const FRangeDummy& D) { return !D.Operator.IsValid(); }));
	SelfTestCheck(TEXT("bomb site and walls built"), Layout.Sites.Num() == 1 && Layout.SiteWalls.Num() > 0);
	SelfTestCheck(TEXT("phase allows shooting"), GetPhase() == ESiegePhase::Action && CanTeamAct(0));

	// the two nearest standing enemies: one for a body shot, one for a headshot
	TArray<ASiegeOperator*> Targets;
	for (const FRangeDummy& Dummy : Dummies)
	{
		if (Dummy.Spot.Team == 1 && Dummy.Spot.StrafeDistance <= 0.0f && Dummy.Operator.IsValid())
		{
			Targets.Add(Dummy.Operator.Get());
		}
	}

	ASiegeGun* Gun = Player ? Player->GetCurrentGun() : nullptr;
	SelfTestCheck(TEXT("player holds a gun"), Gun != nullptr);

	if (Targets.Num() >= 2 && Gun)
	{
		FHitResult Hit;
		Hit.BoneName = TEXT("spine_03");
		const float Before = Targets[0]->GetHealth();
		Targets[0]->TakeDamage(30.0f, FPointDamageEvent(30.0f, Hit, FVector::ForwardVector, nullptr), PC, Gun);
		SelfTestCheck(TEXT("body shot removes health"), FMath::IsNearlyEqual(Targets[0]->GetHealth(), Before - 30.0f));
		SelfTestCheck(TEXT("hit shows in the readout"), RecentHits.Num() == 1 && RecentHits[0].Zone == TEXT("body"));

		Hit.BoneName = TEXT("head");
		Targets[1]->TakeDamage(30.0f, FPointDamageEvent(30.0f, Hit, FVector::ForwardVector, nullptr), PC, Gun);
		SelfTestCheck(TEXT("headshot kills"), Targets[1]->IsDead());
		SelfTestCheck(TEXT("a kill does not end the phase"), GetPhase() == ESiegePhase::Action);
	}

	// settings, on the live objects the game reads; nothing here is saved
	if (USiegeUserSettings* Settings = USiegeUserSettings::Get())
	{
		const float OldSens = Settings->MouseSensitivity;
		const bool bOldInvert = Settings->bInvertY;

		Settings->MouseSensitivity = 2.0f;
		Settings->bInvertY = true;
		const FVector2D Look = USiegeUserSettings::ScaleLook(FVector2D(1.0f, 1.0f));
		SelfTestCheck(TEXT("mouse sensitivity scales look"), FMath::IsNearlyEqual(static_cast<float>(Look.X), 2.0f));
		SelfTestCheck(TEXT("invert flips vertical look"), FMath::IsNearlyEqual(static_cast<float>(Look.Y), -2.0f));

		Settings->MouseSensitivity = OldSens;
		Settings->bInvertY = bOldInvert;

		// the audio thread picks the volume up on its next update, so stage 2 reads it back
		SelfTestOldVolume = Settings->MasterVolume;
		Settings->MasterVolume = 0.25f;
		Settings->ApplyAudio();

		SelfTestOldFov = Settings->FieldOfView;
		Settings->FieldOfView = 105.0f;
	}

	FTimerHandle Next;
	GetWorldTimerManager().SetTimer(Next, this, &ASiegeRangeGameMode::RunSelfTestStage2, 0.5f, false);
}

void ASiegeRangeGameMode::RunSelfTestStage2()
{
	ASiegePlayerController* PC = Cast<ASiegePlayerController>(GetWorld()->GetFirstPlayerController());
	const ASiegeOperator* Player = PC ? PC->GetOperator() : nullptr;

	if (Player)
	{
		SelfTestCheck(TEXT("field of view changes live"), FMath::IsNearlyEqual(Player->GetFirstPersonCameraComponent()->FieldOfView, 105.0f, 0.5f));
	}

	if (FAudioDeviceHandle Device = GEngine->GetMainAudioDevice())
	{
		// the device also folds in the app-wide multiplier, which drops when the window isn't focused
		SelfTestCheck(TEXT("master volume reaches the audio device"), FMath::IsNearlyEqual(Device->GetPrimaryVolume(), 0.25f * FApp::GetVolumeMultiplier(), 0.01f));
	}
	else
	{
		UE_LOG(LogSeige, Display, TEXT("RangeSelfTest SKIP: no audio device in this run"));
	}

	if (USiegeUserSettings* Settings = USiegeUserSettings::Get())
	{
		Settings->FieldOfView = SelfTestOldFov;
		Settings->MasterVolume = SelfTestOldVolume;
		Settings->ApplyAudio();
	}

	if (!PC)
	{
		return;
	}

	PC->TogglePauseMenu();
	SelfTestCheck(TEXT("Esc menu opens and pauses"), PC->IsPauseMenuOpen() && UGameplayStatics::IsGamePaused(this));
	FScreenshotRequest::RequestScreenshot(true);

	// world timers don't run while paused, so stay on the menu for a moment with the core ticker
	TWeakObjectPtr<ASiegeRangeGameMode> WeakThis = this;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float)
	{
		if (ASiegeRangeGameMode* Self = WeakThis.Get())
		{
			Self->RunSelfTestAfterMenu();
		}
		return false;
	}), 1.5f);
}

void ASiegeRangeGameMode::RunSelfTestAfterMenu()
{
	ASiegePlayerController* PC = Cast<ASiegePlayerController>(GetWorld()->GetFirstPlayerController());
	if (!PC)
	{
		return;
	}

	PC->TogglePauseMenu();
	SelfTestCheck(TEXT("Esc menu closes and resumes"), !PC->IsPauseMenuOpen() && !UGameplayStatics::IsGamePaused(this));

	// index 3 is the first defender in the default roster
	RequestOperator(PC, 3);
	const ASiegeOperator* Swapped = PC->GetOperator();
	SelfTestCheck(TEXT("operator switch changes operator and side"), Swapped && Swapped->GetOperatorDef().Name == Operators[3].Name && !IsAttackingTeam(0));

	FTimerHandle Next;
	GetWorldTimerManager().SetTimer(Next, this, &ASiegeRangeGameMode::RunSelfTestStage3, DummyRespawnTime + 1.0f, false);
}

void ASiegeRangeGameMode::RunSelfTestStage3()
{
	SelfTestCheck(TEXT("dead target respawns"), !Dummies.ContainsByPredicate([](const FRangeDummy& D) { return !D.Operator.IsValid() || D.Operator->IsDead(); }));

	const int32 NumDummies = Dummies.Num();
	bSelfTestDone = true;
	ResetRange();
	SelfTestCheck(TEXT("reset rebuilds the range"), Dummies.Num() == NumDummies && Layout.Sites.Num() == 1);

	UE_LOG(LogSeige, Display, TEXT("RangeSelfTest finished: %d failure(s)"), SelfTestFailures);
	UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
}
