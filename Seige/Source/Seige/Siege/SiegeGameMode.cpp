// Copyright Epic Games, Inc. All Rights Reserved.

#include "SiegeGameMode.h"
#include "Seige.h"
#include "SiegeGameState.h"
#include "SiegePlayerState.h"
#include "SiegePlayerController.h"
#include "SiegeBotController.h"
#include "SiegeHUD.h"
#include "SiegeOperator.h"
#include "SiegeObjective.h"
#include "SiegeGadgets.h"
#include "SiegeWall.h"
#include "SiegeObservation.h"
#include "AIController.h"
#include "ShooterWeapon.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"

ASiegeGameMode::ASiegeGameMode()
{
	PlayerControllerClass = ASiegePlayerController::StaticClass();
	PlayerStateClass = ASiegePlayerState::StaticClass();
	GameStateClass = ASiegeGameState::StaticClass();
	HUDClass = ASiegeHUD::StaticClass();
	BotControllerClass = ASiegeBotController::StaticClass();

	// operators are spawned by the round logic, not on login
	DefaultPawnClass = nullptr;

	BotNames = { TEXT("Kestrel"), TEXT("Mako"), TEXT("Vesper"), TEXT("Juno"), TEXT("Rook"), TEXT("Talon"), TEXT("Nyx"), TEXT("Bram"), TEXT("Echo") };
}

void ASiegeGameMode::BuildDefaultRoster()
{
	UClass* Rifle = LoadClass<AShooterWeapon>(nullptr, TEXT("/Game/Siege/Weapons/BP_SiegeRifle.BP_SiegeRifle_C"));
	UClass* SMG = LoadClass<AShooterWeapon>(nullptr, TEXT("/Game/Siege/Weapons/BP_SiegeSMG.BP_SiegeSMG_C"));
	UClass* Pistol = LoadClass<AShooterWeapon>(nullptr, TEXT("/Game/Siege/Weapons/BP_SiegePistol.BP_SiegePistol_C"));

	auto Make = [&](const TCHAR* OpName, const TCHAR* OpRole, bool bAttacker, float Health, float Speed, UClass* Primary, ESiegeGadget Unique, int32 UniqueCount, ESiegeGadget Secondary, int32 SecondaryCount)
	{
		FSiegeOperatorDef Def;
		Def.Name = OpName;
		Def.Role = OpRole;
		Def.bAttacker = bAttacker;
		Def.MaxHealth = Health;
		Def.WalkSpeed = Speed;
		Def.PrimaryWeapon = Primary;
		Def.SecondaryWeapon = Pistol;
		Def.UniqueGadget = Unique;
		Def.UniqueGadgetCount = UniqueCount;
		Def.SecondaryGadget = Secondary;
		Def.SecondaryGadgetCount = SecondaryCount;
		Operators.Add(Def);
	};

	// attackers
	Make(TEXT("Torch"), TEXT("Hard Breacher"), true, 110.0f, 400.0f, Rifle, ESiegeGadget::ThermalCharge, 1, ESiegeGadget::BreachCharge, 1);
	Make(TEXT("Ram"), TEXT("Soft Breacher"), true, 125.0f, 360.0f, SMG, ESiegeGadget::BreachCharge, 3, ESiegeGadget::None, 0);
	Make(TEXT("Scout"), TEXT("Intel"), true, 100.0f, 440.0f, Rifle, ESiegeGadget::PulseScanner, 3, ESiegeGadget::BreachCharge, 1);

	// defenders
	Make(TEXT("Static"), TEXT("Breach Denial"), false, 110.0f, 400.0f, SMG, ESiegeGadget::ShockBox, 2, ESiegeGadget::None, 0);
	Make(TEXT("Bulwark"), TEXT("Anchor"), false, 125.0f, 360.0f, Rifle, ESiegeGadget::DeployableShield, 2, ESiegeGadget::None, 0);
	Make(TEXT("Sentry"), TEXT("Roamer / Intel"), false, 100.0f, 440.0f, SMG, ESiegeGadget::AlarmSensor, 3, ESiegeGadget::None, 0);
}

void ASiegeGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (!OperatorClass)
	{
		OperatorClass = LoadClass<ASiegeOperator>(nullptr, TEXT("/Game/Siege/Blueprints/BP_SiegeOperator.BP_SiegeOperator_C"));
	}

	if (Operators.Num() == 0)
	{
		BuildDefaultRoster();
	}

	if (ASiegeGameState* GS = GetSiegeGameState())
	{
		GS->RoundsToWin = RoundsToWin;
	}
}

void ASiegeGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// the human player is always on team 0
	if (ASiegePlayerState* PS = NewPlayer->GetPlayerState<ASiegePlayerState>())
	{
		PS->Team = 0;
		PS->SetPlayerName(TEXT("You"));
	}

	if (!bMatchStarted)
	{
		bMatchStarted = true;

		FTimerHandle StartTimer;
		GetWorldTimerManager().SetTimer(StartTimer, this, &ASiegeGameMode::StartMatch, 1.0f, false);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Queries

ASiegeGameState* ASiegeGameMode::GetSiegeGameState() const
{
	return GetGameState<ASiegeGameState>();
}

ESiegePhase ASiegeGameMode::GetPhase() const
{
	const ASiegeGameState* GS = GetSiegeGameState();
	return GS ? GS->Phase : ESiegePhase::WaitingToStart;
}

uint8 ASiegeGameMode::GetAttackingTeam() const
{
	const ASiegeGameState* GS = GetSiegeGameState();
	return GS ? GS->AttackingTeam : 0;
}

bool ASiegeGameMode::IsAttackingTeam(uint8 Team) const
{
	return Team == GetAttackingTeam();
}

bool ASiegeGameMode::CanTeamMove(uint8 Team) const
{
	const ESiegePhase Phase = GetPhase();

	if (Phase == ESiegePhase::Preparation)
	{
		// attackers wait outside during preparation
		return !IsAttackingTeam(Team);
	}

	return Phase == ESiegePhase::Action || Phase == ESiegePhase::PostPlant || Phase == ESiegePhase::RoundOver;
}

bool ASiegeGameMode::CanTeamAct(uint8 Team) const
{
	const ESiegePhase Phase = GetPhase();

	if (Phase == ESiegePhase::Preparation)
	{
		return !IsAttackingTeam(Team);
	}

	return Phase == ESiegePhase::Action || Phase == ESiegePhase::PostPlant;
}

bool ASiegeGameMode::CanUseGadgets(uint8 Team) const
{
	return CanTeamAct(Team);
}

ASiegeBombSite* ASiegeGameMode::GetSiteAt(const FVector& Location) const
{
	for (const TWeakObjectPtr<ASiegeBombSite>& Site : Layout.Sites)
	{
		if (Site.IsValid() && Site->IsInside(Location))
		{
			return Site.Get();
		}
	}

	return nullptr;
}

ASiegeDevice* ASiegeGameMode::GetActiveDevice() const
{
	return (ActiveDevice && ActiveDevice->IsActive()) ? ActiveDevice.Get() : nullptr;
}

TArray<int32> ASiegeGameMode::GetOperatorIndicesForSide(bool bAttackers) const
{
	TArray<int32> Result;
	for (int32 i = 0; i < Operators.Num(); ++i)
	{
		if (Operators[i].bAttacker == bAttackers)
		{
			Result.Add(i);
		}
	}
	return Result;
}

FString ASiegeGameMode::GetDisplayNameFor(AController* Controller) const
{
	if (Controller && Controller->PlayerState)
	{
		return Controller->PlayerState->GetPlayerName();
	}

	return TEXT("Environment");
}

uint8 ASiegeGameMode::GetTeamOf(AController* Controller) const
{
	if (Controller)
	{
		if (const ASiegePlayerState* PS = Controller->GetPlayerState<ASiegePlayerState>())
		{
			return PS->Team;
		}
	}

	return 0;
}

TArray<AController*> ASiegeGameMode::GetParticipants() const
{
	TArray<AController*> Result;

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (It->IsValid())
		{
			Result.Add(It->Get());
		}
	}

	for (ASiegeBotController* Bot : Bots)
	{
		if (IsValid(Bot))
		{
			Result.Add(Bot);
		}
	}

	return Result;
}

int32 ASiegeGameMode::CountAlive(uint8 Team) const
{
	int32 Count = 0;
	for (const ASiegeOperator* Op : RoundOperators)
	{
		if (IsValid(Op) && !Op->IsDead() && !Op->IsDowned() && Op->GetTeam() == Team)
		{
			++Count;
		}
	}
	return Count;
}

// ---------------------------------------------------------------------------------------------------------------------
// Messages

void ASiegeGameMode::PostTeamMessage(uint8 Team, const FString& Text)
{
	if (ASiegeGameState* GS = GetSiegeGameState())
	{
		GS->AddMessage(Team, Text);
	}
}

void ASiegeGameMode::PostPlayerMessage(ASiegeOperator* Operator, const FString& Text)
{
	if (Operator && Cast<APlayerController>(Operator->GetOwningController()))
	{
		if (ASiegeGameState* GS = GetSiegeGameState())
		{
			GS->AddMessage(Operator->GetTeam(), Text, Operator);
		}
	}
}

void ASiegeGameMode::PostGlobalMessage(const FString& Text)
{
	if (ASiegeGameState* GS = GetSiegeGameState())
	{
		GS->AddMessage(255, Text);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Match flow

void ASiegeGameMode::SetPhase(ESiegePhase NewPhase, float Duration)
{
	if (ASiegeGameState* GS = GetSiegeGameState())
	{
		GS->Phase = NewPhase;
		UE_LOG(LogSeige, Log, TEXT("Phase -> %d (round %d, attackers team %d, %.0fs)"), static_cast<int32>(NewPhase), GS->RoundNumber, GS->AttackingTeam, Duration);
		GS->PhaseEndTime = GS->GetServerWorldTimeSeconds() + Duration;
	}
}

void ASiegeGameMode::StartMatch()
{
	// create the bots once
	if (Bots.Num() == 0)
	{
		const int32 NumBots = TeamSize * 2 - 1;

		for (int32 i = 0; i < NumBots; ++i)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			ASiegeBotController* Bot = GetWorld()->SpawnActor<ASiegeBotController>(BotControllerClass, Params);
			if (!Bot)
			{
				continue;
			}

			// first TeamSize-1 bots join the player, the rest form the enemy team
			const uint8 Team = i < TeamSize - 1 ? 0 : 1;

			if (ASiegePlayerState* PS = Bot->GetPlayerState<ASiegePlayerState>())
			{
				PS->Team = Team;
				PS->SetPlayerName(BotNames.IsValidIndex(i) ? BotNames[i] : FString::Printf(TEXT("Bot %d"), i + 1));
			}

			Bots.Add(Bot);
		}
	}

	if (ASiegeGameState* GS = GetSiegeGameState())
	{
		GS->ScoreTeam0 = 0;
		GS->ScoreTeam1 = 0;
		GS->RoundNumber = 0;
		GS->KillFeed.Reset();
	}

	for (AController* C : GetParticipants())
	{
		if (ASiegePlayerState* PS = C->GetPlayerState<ASiegePlayerState>())
		{
			PS->Kills = 0;
			PS->Deaths = 0;
			PS->Plants = 0;
			PS->OperatorIndex = INDEX_NONE;
		}
	}

	StartNewRound();
}

void ASiegeGameMode::StartNewRound()
{
	ASiegeGameState* GS = GetSiegeGameState();
	if (!GS)
	{
		return;
	}

	CleanupRound();

	const uint8 PreviousAttackers = GS->AttackingTeam;

	GS->RoundNumber++;
	GS->AttackingTeam = (((GS->RoundNumber - 1) / FMath::Max(1, RoundsPerHalf)) % 2 == 0) ? 0 : 1;
	GS->LastRoundWinner = 255;
	GS->LastRoundReason = ESiegeRoundEndReason::None;
	GS->PlantedSite.Reset();

	// forget operator picks when sides swap
	if (GS->RoundNumber == 1 || PreviousAttackers != GS->AttackingTeam)
	{
		for (AController* C : GetParticipants())
		{
			if (ASiegePlayerState* PS = C->GetPlayerState<ASiegePlayerState>())
			{
				PS->OperatorIndex = INDEX_NONE;
			}
		}
	}

	SiegeLayout::BuildEmbassy(GetWorld(), Layout);
	BotAttackSite = FMath::RandRange(0, FMath::Max(0, Layout.Sites.Num() - 1));
	ClaimedBreachRoles.Reset();

	// cameras belong to whoever defends this round
	for (const TWeakObjectPtr<ASiegeSecurityCamera>& Cam : Layout.Cameras)
	{
		if (Cam.IsValid())
		{
			Cam->SetTeam(1 - GS->AttackingTeam);
		}
	}

	// overview camera while picking operators
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			PC->SetInitialLocationAndRotation(FVector(0.0f, -3400.0f, 1700.0f), FRotator(-28.0f, 90.0f, 0.0f));
			PC->SetViewTarget(PC);
		}
	}

	SetPhase(ESiegePhase::OperatorSelect, OperatorSelectTime);
	GetWorldTimerManager().SetTimer(PhaseTimer, this, &ASiegeGameMode::BeginPreparation, OperatorSelectTime, false);

	PostGlobalMessage(FString::Printf(TEXT("Round %d: your team is %s"), GS->RoundNumber, IsAttackingTeam(0) ? TEXT("ATTACKING") : TEXT("DEFENDING")));
}

void ASiegeGameMode::RequestOperator(APlayerController* Player, int32 SideIndex)
{
	if (GetPhase() != ESiegePhase::OperatorSelect || !Player)
	{
		return;
	}

	ASiegePlayerState* PS = Player->GetPlayerState<ASiegePlayerState>();
	if (!PS)
	{
		return;
	}

	const TArray<int32> Choices = GetOperatorIndicesForSide(IsAttackingTeam(PS->Team));
	if (Choices.IsValidIndex(SideIndex))
	{
		PS->OperatorIndex = Choices[SideIndex];
	}
}

void ASiegeGameMode::AssignBotOperators()
{
	for (uint8 Team = 0; Team < 2; ++Team)
	{
		const TArray<int32> Choices = GetOperatorIndicesForSide(IsAttackingTeam(Team));
		if (Choices.Num() == 0)
		{
			continue;
		}

		TArray<int32> Taken;

		// humans pick first; give them a random operator if they didn't choose
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			ASiegePlayerState* PS = It->Get() ? It->Get()->GetPlayerState<ASiegePlayerState>() : nullptr;
			if (PS && PS->Team == Team)
			{
				if (!Choices.Contains(PS->OperatorIndex))
				{
					PS->OperatorIndex = Choices[FMath::RandRange(0, Choices.Num() - 1)];
				}
				Taken.Add(PS->OperatorIndex);
			}
		}

		// bots take unused operators first, then duplicates
		for (ASiegeBotController* Bot : Bots)
		{
			ASiegePlayerState* PS = Bot ? Bot->GetPlayerState<ASiegePlayerState>() : nullptr;
			if (!PS || PS->Team != Team)
			{
				continue;
			}

			TArray<int32> Free = Choices.FilterByPredicate([&Taken](int32 Index) { return !Taken.Contains(Index); });
			const TArray<int32>& Pool = Free.Num() > 0 ? Free : Choices;

			PS->OperatorIndex = Pool[FMath::RandRange(0, Pool.Num() - 1)];
			Taken.Add(PS->OperatorIndex);
		}
	}
}

void ASiegeGameMode::SpawnOperators()
{
	if (!OperatorClass)
	{
		PostGlobalMessage(TEXT("ERROR: BP_SiegeOperator not found at /Game/Siege/Blueprints"));
		return;
	}

	int32 SpawnIndex[2] = { 0, 0 };

	for (AController* C : GetParticipants())
	{
		ASiegePlayerState* PS = C->GetPlayerState<ASiegePlayerState>();
		if (!PS || !Operators.IsValidIndex(PS->OperatorIndex))
		{
			continue;
		}

		const uint8 Team = PS->Team;
		const bool bAttacker = IsAttackingTeam(Team);
		const TArray<FTransform>& Spawns = bAttacker ? Layout.AttackerSpawns : Layout.DefenderSpawns;

		if (Spawns.Num() == 0)
		{
			continue;
		}

		const FTransform SpawnTransform = Spawns[SpawnIndex[Team]++ % Spawns.Num()];

		ASiegeOperator* Op = GetWorld()->SpawnActorDeferred<ASiegeOperator>(OperatorClass, SpawnTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!Op)
		{
			continue;
		}

		Op->AutoPossessAI = EAutoPossessAI::Disabled;
		Op->FinishSpawning(SpawnTransform);

		Op->SetTeam(Team);
		Op->SetDisplayName(PS->GetPlayerName());

		// possess before giving weapons: the weapon anim blueprints read the controller
		Op->SetOwningController(C);
		C->Possess(Op);
		C->SetControlRotation(SpawnTransform.Rotator());

		Op->ApplyOperatorDef(Operators[PS->OperatorIndex], bAttacker ? 0 : ReinforcementsPerDefender);

		RoundOperators.Add(Op);

		if (ASiegePlayerController* PC = Cast<ASiegePlayerController>(C))
		{
			PC->SetHomeOperator(Op);
			PC->OnRoundStarted();
		}
	}
}

void ASiegeGameMode::BeginPreparation()
{
	AssignBotOperators();
	SpawnOperators();

	const float Duration = IsAttackingTeam(0) ? AttackerPreparationTime : PreparationTime;
	SetPhase(ESiegePhase::Preparation, Duration);

	GetWorldTimerManager().SetTimer(PhaseTimer, this, &ASiegeGameMode::BeginAction, Duration, false);
	GetWorldTimerManager().SetTimer(BotPrepTimer, this, &ASiegeGameMode::RunBotPreparation, 2.0f, true, 1.5f);

	LaunchBotDrones();

	// attacking players start the round driving a drone
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ASiegePlayerController* PC = Cast<ASiegePlayerController>(It->Get());
		if (PC && PC->GetOperator() && IsAttackingTeam(PC->GetOperator()->GetTeam()))
		{
			PC->EnterObservation();
		}
	}

	if (IsAttackingTeam(0))
	{
		PostGlobalMessage(TEXT("Preparation: scout with your drone. [LMB] spot  [Q/E] switch  [X] back to operator"));
	}
	else
	{
		PostGlobalMessage(TEXT("Preparation: reinforce walls [F] and deploy gadgets [G]"));
	}
}

void ASiegeGameMode::BeginAction()
{
	GetWorldTimerManager().ClearTimer(BotPrepTimer);

	// parked drones stay where they are and keep watching
	for (AAIController* DroneAI : DroneControllers)
	{
		if (IsValid(DroneAI))
		{
			DroneAI->StopMovement();
		}
	}

	SetPhase(ESiegePhase::Action, ActionTime);
	GetWorldTimerManager().SetTimer(PhaseTimer, this, &ASiegeGameMode::OnActionTimeExpired, ActionTime, false);

	PostGlobalMessage(TEXT("Action phase: go!"));
}

void ASiegeGameMode::OnActionTimeExpired()
{
	if (GetPhase() == ESiegePhase::Action)
	{
		EndRound(1 - GetAttackingTeam(), ESiegeRoundEndReason::TimeExpired);
	}
}

void ASiegeGameMode::CompletePlant(ASiegeOperator* Planter)
{
	if (GetPhase() != ESiegePhase::Action || GetActiveDevice() || !Planter)
	{
		return;
	}

	ASiegeBombSite* Site = GetSiteAt(Planter->GetActorLocation());
	if (!Site)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FVector Feet = Planter->GetActorLocation() + Planter->GetActorForwardVector() * 40.0f - FVector(0.0f, 0.0f, 70.0f);
	ActiveDevice = GetWorld()->SpawnActor<ASiegeDevice>(ASiegeDevice::StaticClass(), Feet, FRotator::ZeroRotator, Params);

	if (!ActiveDevice)
	{
		return;
	}

	ActiveDevice->Arm(DeviceCountdown, Site->SiteName);
	UE_LOG(LogSeige, Log, TEXT("Device planted at site %s by %s"), *Site->SiteName, *Planter->GetDisplayName());

	GetWorldTimerManager().ClearTimer(PhaseTimer);
	SetPhase(ESiegePhase::PostPlant, DeviceCountdown);

	if (ASiegeGameState* GS = GetSiegeGameState())
	{
		GS->PlantedSite = Site->SiteName;
	}

	if (ASiegePlayerState* PS = Planter->GetPlayerState<ASiegePlayerState>())
	{
		PS->Plants++;
	}

	PostGlobalMessage(FString::Printf(TEXT("%s planted the device at site %s"), *Planter->GetDisplayName(), *Site->SiteName));
}

void ASiegeGameMode::CompleteDisable(ASiegeOperator* Defuser)
{
	ASiegeDevice* Device = GetActiveDevice();
	if (!Device || GetPhase() != ESiegePhase::PostPlant)
	{
		return;
	}

	Device->Disable();

	PostGlobalMessage(FString::Printf(TEXT("%s disabled the device"), Defuser ? *Defuser->GetDisplayName() : TEXT("A defender")));
	EndRound(1 - GetAttackingTeam(), ESiegeRoundEndReason::DeviceDisabled);
}

void ASiegeGameMode::OnDeviceDetonated(ASiegeDevice* Device)
{
	if (GetPhase() != ESiegePhase::PostPlant)
	{
		return;
	}

	const FVector Location = Device ? Device->GetActorLocation() : FVector::ZeroVector;

	// end the round first so the blast can't also trigger an elimination result
	EndRound(GetAttackingTeam(), ESiegeRoundEndReason::DeviceDetonated);

	SiegeExplosion::Explode(GetWorld(), Location + FVector(0, 0, 60.0f), 250.0f, 900.0f, 400.0f, nullptr, Device);
}

void ASiegeGameMode::OnOperatorKilled(ASiegeOperator* Victim, AController* Killer, bool bHeadshot)
{
	if (!Victim)
	{
		return;
	}

	ASiegeGameState* GS = GetSiegeGameState();
	AController* VictimController = Victim->GetOwningController();

	if (GS)
	{
		FSiegeKillFeedEntry Entry;
		Entry.KillerName = GetDisplayNameFor(Killer);
		Entry.VictimName = Victim->GetDisplayName();
		Entry.KillerTeam = GetTeamOf(Killer);
		Entry.VictimTeam = Victim->GetTeam();
		Entry.bHeadshot = bHeadshot;
		Entry.Time = GS->GetServerWorldTimeSeconds();
		GS->AddKill(Entry);
		UE_LOG(LogSeige, Log, TEXT("Kill: %s -> %s%s"), *Entry.KillerName, *Entry.VictimName, bHeadshot ? TEXT(" (headshot)") : TEXT(""));
	}

	if (ASiegePlayerState* VictimPS = VictimController ? VictimController->GetPlayerState<ASiegePlayerState>() : nullptr)
	{
		VictimPS->Deaths++;
	}

	if (ASiegePlayerState* KillerPS = Killer ? Killer->GetPlayerState<ASiegePlayerState>() : nullptr)
	{
		// team kills don't count
		if (Killer != VictimController && KillerPS->Team != Victim->GetTeam())
		{
			KillerPS->Kills++;
		}
	}

	if (ASiegePlayerController* PC = Cast<ASiegePlayerController>(VictimController))
	{
		PC->OnOperatorDied();
	}

	CheckEliminations();
}

void ASiegeGameMode::CheckEliminations()
{
	const ESiegePhase Phase = GetPhase();
	if (Phase != ESiegePhase::Preparation && Phase != ESiegePhase::Action && Phase != ESiegePhase::PostPlant)
	{
		return;
	}

	const uint8 Attackers = GetAttackingTeam();
	const uint8 Defenders = 1 - Attackers;

	if (CountAlive(Defenders) == 0)
	{
		EndRound(Attackers, ESiegeRoundEndReason::AllDefendersEliminated);
	}
	else if (CountAlive(Attackers) == 0 && !GetActiveDevice())
	{
		// with the device ticking, defenders still have to disable it
		EndRound(Defenders, ESiegeRoundEndReason::AllAttackersEliminated);
	}
}

void ASiegeGameMode::EndRound(uint8 Winner, ESiegeRoundEndReason Reason)
{
	ASiegeGameState* GS = GetSiegeGameState();
	if (!GS || GS->Phase == ESiegePhase::RoundOver || GS->Phase == ESiegePhase::MatchOver)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(PhaseTimer);
	GetWorldTimerManager().ClearTimer(BotPrepTimer);

	if (Winner == 0)
	{
		GS->ScoreTeam0++;
	}
	else
	{
		GS->ScoreTeam1++;
	}

	GS->LastRoundWinner = Winner;
	GS->LastRoundReason = Reason;
	UE_LOG(LogSeige, Log, TEXT("Round %d over: team %d wins (reason %d). Score %d - %d"), GS->RoundNumber, Winner, static_cast<int32>(Reason), GS->ScoreTeam0, GS->ScoreTeam1);

	for (ASiegeOperator* Op : RoundOperators)
	{
		if (IsValid(Op))
		{
			Op->DoStopFiring();
			Op->StopInteract();
		}
	}

	// stop the device ticking
	if (ActiveDevice && ActiveDevice->IsActive() && Reason != ESiegeRoundEndReason::DeviceDetonated)
	{
		ActiveDevice->Disable();
	}

	SetPhase(ESiegePhase::RoundOver, RoundOverTime);
	GetWorldTimerManager().SetTimer(PhaseTimer, this, &ASiegeGameMode::OnRoundOverFinished, RoundOverTime, false);
}

void ASiegeGameMode::OnRoundOverFinished()
{
	ASiegeGameState* GS = GetSiegeGameState();
	if (!GS)
	{
		return;
	}

	if (GS->ScoreTeam0 >= RoundsToWin || GS->ScoreTeam1 >= RoundsToWin)
	{
		SetPhase(ESiegePhase::MatchOver, MatchOverTime);
		GetWorldTimerManager().SetTimer(PhaseTimer, this, &ASiegeGameMode::StartMatch, MatchOverTime, false);
		return;
	}

	StartNewRound();
}

void ASiegeGameMode::CleanupRound()
{
	for (ASiegeOperator* Op : RoundOperators)
	{
		if (IsValid(Op))
		{
			if (AController* C = Op->GetController())
			{
				C->UnPossess();
			}
			Op->Destroy();
		}
	}
	RoundOperators.Reset();

	TArray<ASiegeGadgetBase*> Gadgets;
	for (TActorIterator<ASiegeGadgetBase> It(GetWorld()); It; ++It)
	{
		Gadgets.Add(*It);
	}
	for (ASiegeGadgetBase* Gadget : Gadgets)
	{
		Gadget->Destroy();
	}

	if (ActiveDevice)
	{
		ActiveDevice->Destroy();
		ActiveDevice = nullptr;
	}

	for (AAIController* DroneAI : DroneControllers)
	{
		if (IsValid(DroneAI))
		{
			DroneAI->Destroy();
		}
	}
	DroneControllers.Reset();

	TArray<ASiegeDrone*> Drones;
	for (TActorIterator<ASiegeDrone> It(GetWorld()); It; ++It)
	{
		Drones.Add(*It);
	}
	for (ASiegeDrone* Drone : Drones)
	{
		Drone->Destroy();
	}

	SiegeLayout::Clear(Layout);
}

// ---------------------------------------------------------------------------------------------------------------------
// Bot preparation

void ASiegeGameMode::RunBotPreparation()
{
	if (GetPhase() != ESiegePhase::Preparation)
	{
		return;
	}

	const uint8 Defenders = 1 - GetAttackingTeam();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (ASiegeOperator* Op : RoundOperators)
	{
		if (!IsValid(Op) || Op->IsDead() || Op->GetTeam() != Defenders || !Cast<ASiegeBotController>(Op->GetController()))
		{
			continue;
		}

		Params.Owner = Op;

		// reinforce one site wall per tick
		if (Op->GetReinforcementsLeft() > 0)
		{
			TArray<ASiegeWall*> Candidates;
			for (const TWeakObjectPtr<ASiegeWall>& Wall : Layout.SiteWalls)
			{
				if (Wall.IsValid() && Wall->CanBeReinforced())
				{
					Candidates.Add(Wall.Get());
				}
			}

			if (Candidates.Num() > 0 && Candidates[FMath::RandRange(0, Candidates.Num() - 1)]->Reinforce())
			{
				Op->ConsumeReinforcement();
			}
			continue;
		}

		if (Op->GetUniqueGadgetCount() <= 0)
		{
			continue;
		}

		switch (Op->GetOperatorDef().UniqueGadget)
		{
		case ESiegeGadget::ShockBox:
		{
			// electrify a reinforced site wall that isn't already protected, on the site side
			TArray<ASiegeWall*> Candidates;
			for (const TWeakObjectPtr<ASiegeWall>& Wall : Layout.SiteWalls)
			{
				if (Wall.IsValid() && Wall->IsReinforced() && !Wall->IsElectrified())
				{
					Candidates.Add(Wall.Get());
				}
			}

			if (Candidates.Num() == 0)
			{
				break;
			}

			ASiegeWall* Wall = Candidates[FMath::RandRange(0, Candidates.Num() - 1)];
			ASiegeBombSite* Nearest = nullptr;
			float BestDist = TNumericLimits<float>::Max();
			for (const TWeakObjectPtr<ASiegeBombSite>& Site : Layout.Sites)
			{
				if (Site.IsValid())
				{
					const float Dist = FVector::Dist(Site->GetActorLocation(), Wall->GetActorLocation());
					if (Dist < BestDist)
					{
						BestDist = Dist;
						Nearest = Site.Get();
					}
				}
			}

			FVector Normal = Wall->GetActorForwardVector();
			if (Nearest && FVector::DotProduct((Nearest->GetActorLocation() - Wall->GetActorLocation()).GetSafeNormal2D(), Normal) < 0.0f)
			{
				Normal = -Normal;
			}

			const FVector Location = Wall->GetActorLocation() + Normal * 25.0f - FVector(0, 0, 40.0f);
			if (ASiegeShockBox* Box = GetWorld()->SpawnActor<ASiegeShockBox>(ASiegeShockBox::StaticClass(), Location, Normal.Rotation(), Params))
			{
				Box->InitGadget(Op, Wall);
				Op->ConsumeUniqueGadget();
			}
			break;
		}

		case ESiegeGadget::DeployableShield:
		{
			const FVector Location = Op->GetActorLocation() + Op->GetActorForwardVector() * 90.0f;

			// don't drop the shield on top of a teammate
			bool bBlocked = false;
			for (const ASiegeOperator* Other : RoundOperators)
			{
				if (IsValid(Other) && Other != Op && FVector::Dist2D(Other->GetActorLocation(), Location) < 130.0f)
				{
					bBlocked = true;
					break;
				}
			}

			FHitResult Ground;
			if (!bBlocked && GetWorld()->LineTraceSingleByChannel(Ground, Location, Location - FVector(0, 0, 300.0f), ECC_Visibility))
			{
				const FRotator Facing(0.0f, Op->GetActorRotation().Yaw, 0.0f);
				if (ASiegeShield* Shield = GetWorld()->SpawnActor<ASiegeShield>(ASiegeShield::StaticClass(), Ground.ImpactPoint + FVector(0, 0, 57.5f), Facing, Params))
				{
					Shield->InitGadget(Op, nullptr);
					Op->ConsumeUniqueGadget();
				}
			}
			break;
		}

		case ESiegeGadget::AlarmSensor:
		{
			// cover the doorways attackers are likely to use
			static const FVector DoorSpots[] = { FVector(0.0f, -850.0f, 4.0f), FVector(-1350.0f, 0.0f, 4.0f), FVector(1350.0f, 0.0f, 4.0f), FVector(0.0f, 900.0f, 4.0f) };
			const FVector Location = DoorSpots[FMath::RandRange(0, UE_ARRAY_COUNT(DoorSpots) - 1)];

			if (ASiegeAlarmSensor* Sensor = GetWorld()->SpawnActor<ASiegeAlarmSensor>(ASiegeAlarmSensor::StaticClass(), Location, FRotator::ZeroRotator, Params))
			{
				Sensor->InitGadget(Op, nullptr);
				Op->ConsumeUniqueGadget();
			}
			break;
		}

		default:
			break;
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Drones

ASiegeDrone* ASiegeGameMode::DeployDrone(ASiegeOperator* Operator)
{
	if (!Operator || Operator->IsDead() || Operator->GetDroneCount() <= 0)
	{
		return nullptr;
	}

	const FVector Forward = Operator->GetActorForwardVector();
	const FVector Location = Operator->GetActorLocation() + Forward * 70.0f - FVector(0.0f, 0.0f, 80.0f);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	ASiegeDrone* Drone = GetWorld()->SpawnActor<ASiegeDrone>(ASiegeDrone::StaticClass(), Location, FRotator(0.0f, Operator->GetActorRotation().Yaw, 0.0f), Params);
	if (!Drone)
	{
		return nullptr;
	}

	const int32 Number = 3 - Operator->GetDroneCount();
	Operator->ConsumeDrone();
	Drone->InitDrone(Operator, Operator->GetTeam(), FString::Printf(TEXT("Drone %s-%d"), *Operator->GetDisplayName(), Number));

	return Drone;
}

void ASiegeGameMode::LaunchBotDrones()
{
	if (!Layout.Sites.IsValidIndex(BotAttackSite) || !Layout.Sites[BotAttackSite].IsValid())
	{
		return;
	}

	ASiegeBombSite* Site = Layout.Sites[BotAttackSite].Get();
	const uint8 Attackers = GetAttackingTeam();

	for (ASiegeOperator* Op : RoundOperators)
	{
		if (!IsValid(Op) || Op->GetTeam() != Attackers || !Cast<ASiegeBotController>(Op->GetController()))
		{
			continue;
		}

		ASiegeDrone* Drone = DeployDrone(Op);
		if (!Drone)
		{
			continue;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AAIController* DroneAI = GetWorld()->SpawnActor<AAIController>(AAIController::StaticClass(), Params);
		if (!DroneAI)
		{
			continue;
		}

		DroneAI->Possess(Drone);
		DroneAI->MoveToLocation(Site->GetRandomPoint(), 120.0f, true, true, true, false, nullptr, true);
		DroneControllers.Add(DroneAI);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Downed and breach roles

bool ASiegeGameMode::CanOperatorGoDown(const ASiegeOperator* Victim) const
{
	if (!Victim)
	{
		return false;
	}

	for (const ASiegeOperator* Op : RoundOperators)
	{
		if (IsValid(Op) && Op != Victim && Op->GetTeam() == Victim->GetTeam() && !Op->IsDead() && !Op->IsDowned())
		{
			return true;
		}
	}

	return false;
}

void ASiegeGameMode::OnOperatorDowned(ASiegeOperator* Victim, AController* Downer)
{
	if (!Victim)
	{
		return;
	}

	if (ASiegeGameState* GS = GetSiegeGameState())
	{
		FSiegeKillFeedEntry Entry;
		Entry.KillerName = GetDisplayNameFor(Downer);
		Entry.VictimName = Victim->GetDisplayName();
		Entry.KillerTeam = GetTeamOf(Downer);
		Entry.VictimTeam = Victim->GetTeam();
		Entry.bDowned = true;
		Entry.Time = GS->GetServerWorldTimeSeconds();
		GS->AddKill(Entry);

		UE_LOG(LogSeige, Log, TEXT("Downed: %s -> %s"), *Entry.KillerName, *Entry.VictimName);
	}

	PostTeamMessage(Victim->GetTeam(), FString::Printf(TEXT("%s is down! Revive them [F]"), *Victim->GetDisplayName()));

	CheckEliminations();
}

bool ASiegeGameMode::TryClaimBreachRole(ESiegeGadget Gadget)
{
	if (Gadget != ESiegeGadget::ThermalCharge && Gadget != ESiegeGadget::BreachCharge)
	{
		return false;
	}

	const uint8 Key = static_cast<uint8>(Gadget);
	if (ClaimedBreachRoles.Contains(Key))
	{
		return false;
	}

	ClaimedBreachRoles.Add(Key);
	return true;
}
