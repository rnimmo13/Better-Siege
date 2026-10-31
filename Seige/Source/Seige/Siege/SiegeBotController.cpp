// Copyright Epic Games, Inc. All Rights Reserved.

#include "SiegeBotController.h"
#include "Seige.h"
#include "SiegeOperator.h"
#include "SiegeGameMode.h"
#include "SiegeGameState.h"
#include "SiegeObjective.h"
#include "SiegeWall.h"
#include "SiegeGun.h"
#include "SiegeObservation.h"
#include "SiegeUserSettings.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ASiegeBotController::ASiegeBotController()
{
	bWantsPlayerState = true;
	PrimaryActorTick.bCanEverTick = true;
}

ASiegeOperator* ASiegeBotController::GetOperator() const
{
	return Cast<ASiegeOperator>(GetPawn());
}

ASiegeGameMode* ASiegeBotController::GetSiegeGameMode() const
{
	return GetWorld()->GetAuthGameMode<ASiegeGameMode>();
}

void ASiegeBotController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	Target.Reset();
	bFiring = false;
	bHasMoveGoal = false;
	PlannedRound = -1;
	LastEnemySeenTime = -100.0f;
	LastProgressTime = GetWorld()->GetTimeSeconds();
	ClearFocus(EAIFocusPriority::Gameplay);
}

void ASiegeBotController::UpdateControlRotation(float DeltaTime, bool bUpdatePawn)
{
	APawn* const MyPawn = GetPawn();
	if (!MyPawn)
	{
		return;
	}

	const FRotator Current = GetControlRotation();
	FRotator Desired = Current;

	const FVector FocalPoint = GetFocalPoint();
	if (FAISystem::IsValidLocation(FocalPoint))
	{
		Desired = (FocalPoint - MyPawn->GetPawnViewLocation()).Rotation();
	}
	else
	{
		Desired.Pitch = 0.0f;
	}

	// level out when just walking somewhere (but keep pitch for deliberate aim points like placing a charge)
	const bool bHasGameplayFocus = FAISystem::IsValidLocation(GetFocalPointForPriority(EAIFocusPriority::Gameplay));
	if (!bHasGameplayFocus && !Target.IsValid() && GetWorld()->GetTimeSeconds() - LastEnemySeenTime > 3.0f)
	{
		Desired.Pitch = 0.0f;
	}

	FRotator NewRotation = FMath::RInterpConstantTo(Current, Desired, DeltaTime, TurnRate);
	NewRotation.Roll = 0.0f;
	SetControlRotation(NewRotation);

	if (bUpdatePawn && !MyPawn->GetActorRotation().Equals(NewRotation, 1e-3f))
	{
		MyPawn->FaceRotation(NewRotation, DeltaTime);
	}
}

void ASiegeBotController::PlanRound(ASiegeGameMode* GM, ASiegeOperator* Op)
{
	const ASiegeGameState* GS = GM->GetSiegeGameState();
	PlannedRound = GS ? GS->RoundNumber : 0;

	const FSiegeLayoutResult& Layout = GM->GetLayout();

	// stable per-bot index from our place in the round's operator list
	const int32 Index = FMath::Max(0, GM->GetRoundOperators().Find(Op));

	bReachedEntry = false;
	bRoamer = false;

	BreachGadget = ESiegeGadget::None;
	BreachStep = EBreachStep::None;
	BreachWall.Reset();
	LastBreachTick = -1.0f;

	if (GM->IsAttackingTeam(Op->GetTeam()))
	{
		// one Torch and one Ram per round take the breacher job
		const ESiegeGadget Unique = Op->GetOperatorDef().UniqueGadget;
		if (GM->TryClaimBreachRole(Unique))
		{
			BreachGadget = Unique;
			BreachStep = EBreachStep::MoveToWall;
			UE_LOG(LogSeige, Log, TEXT("Breacher role: %s with %s"), *Op->GetDisplayName(), *SiegeGadgets::GetName(Unique));
		}

		if (Layout.EntryPoints.Num() > 0)
		{
			// prefer entries on the side of the target site
			const int32 Site = GM->GetBotAttackSite();
			TArray<FVector> Preferred;
			for (const FVector& Entry : Layout.EntryPoints)
			{
				if ((Site == 0 && Entry.X <= 100.0f) || (Site == 1 && Entry.X >= -100.0f))
				{
					Preferred.Add(Entry);
				}
			}

			const TArray<FVector>& Pool = Preferred.Num() > 0 ? Preferred : Layout.EntryPoints;
			EntryPoint = Pool[FMath::RandRange(0, Pool.Num() - 1)];
		}
	}
	else
	{
		// split defenders across both sites; the Sentry operator roams
		const TArray<FVector>& Posts = (Index % 2 == 0) ? Layout.DefenderPostsA : Layout.DefenderPostsB;
		Post = Posts.Num() > 0 ? Posts[(Index / 2) % Posts.Num()] : Op->GetActorLocation();

		bRoamer = Op->GetOperatorDef().UniqueGadget == ESiegeGadget::AlarmSensor && Layout.RoamPoints.Num() > 0;
		NextRoamTime = 0.0f;
	}
}

void ASiegeBotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	ASiegeOperator* Op = GetOperator();
	ASiegeGameMode* GM = GetSiegeGameMode();

	if (!Op || !GM || Op->IsDead())
	{
		if (bFiring && Op)
		{
			StopFire(Op);
		}
		return;
	}

	const ESiegePhase Phase = GM->GetPhase();
	const float Now = GetWorld()->GetTimeSeconds();

	const ASiegeGameState* GS = GM->GetSiegeGameState();
	if (GS && PlannedRound != GS->RoundNumber)
	{
		PlanRound(GM, Op);
	}

	// downed bots can only wait for help
	if (Op->IsDowned())
	{
		StopFire(Op);
		StopMovement();
		ClearFocus(EAIFocusPriority::Gameplay);
		Target.Reset();
		return;
	}

	if (Phase == ESiegePhase::RoundOver || Phase == ESiegePhase::MatchOver || Phase == ESiegePhase::OperatorSelect)
	{
		StopFire(Op);
		StopMovement();
		return;
	}

	if (Now >= NextThinkTime)
	{
		NextThinkTime = Now + 0.15f;

		AActor* Visible = GM->CanTeamAct(Op->GetTeam()) ? FindVisibleEnemy(Op) : nullptr;

		if (Visible)
		{
			if (Visible != Target.Get())
			{
				Target = Visible;
				TargetAcquiredTime = Now;
				ReactionDelay = FMath::FRandRange(MinReactionTime, MaxReactionTime) * GetDifficultyScale();
				bAimForHead = FMath::FRand() < HeadshotChance / FMath::Square(GetDifficultyScale());
				AimErrorDirection = FMath::VRand();
			}

			LastKnownEnemyLocation = Visible->GetActorLocation();
			LastEnemySeenTime = Now;

			const ASiegeOperator* VisibleOp = Cast<ASiegeOperator>(Visible);
			if (VisibleOp && !VisibleOp->IsDowned())
			{
				LastStandingEnemySeenTime = Now;
			}
		}
		else
		{
			Target.Reset();
		}

		UpdateObjective(GM, Op, Now);
	}

	UpdateCombat(Op, Now);
	HandleStuck(Op, Now);
}

AActor* ASiegeBotController::FindVisibleEnemy(ASiegeOperator* Op) const
{
	ASiegeGameMode* GM = GetSiegeGameMode();
	if (!GM)
	{
		return nullptr;
	}

	const FVector Eye = Op->GetAimOrigin();
	const FVector Forward = Op->GetControlRotation().Vector();
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(SightHalfAngle));
	const bool bRecentlyHurt = GetWorld()->GetTimeSeconds() - Op->GetLastDamagedTime() < 2.0f;

	ASiegeOperator* Best = nullptr;
	float BestDist = SightRange;

	// downed enemies are only finished off when nobody standing is in sight
	ASiegeOperator* BestDowned = nullptr;
	float BestDownedDist = SightRange;

	for (ASiegeOperator* Enemy : GM->GetRoundOperators())
	{
		if (!IsValid(Enemy) || Enemy->IsDead() || Enemy->GetTeam() == Op->GetTeam())
		{
			continue;
		}

		const FVector ToEnemy = Enemy->GetActorLocation() - Eye;
		const float Dist = ToEnemy.Size();

		if (Dist > (Enemy->IsDowned() ? BestDownedDist : BestDist))
		{
			continue;
		}

		// enemies outside the view cone are only noticed up close, when hurt, or when revealed by intel
		const bool bInCone = FVector::DotProduct(ToEnemy.GetSafeNormal(), Forward) >= CosHalfAngle;
		if (!bInCone && Dist > 400.0f && !bRecentlyHurt && !Enemy->IsRevealedTo(Op->GetTeam()))
		{
			continue;
		}

		if (HasLineOfSight(Op, Enemy, Enemy->GetHeadLocation()) || HasLineOfSight(Op, Enemy, Enemy->GetChestLocation()))
		{
			if (Enemy->IsDowned())
			{
				BestDowned = Enemy;
				BestDownedDist = Dist;
			}
			else
			{
				Best = Enemy;
				BestDist = Dist;
			}
		}
	}

	if (Best)
	{
		return Best;
	}

	// only bother finishing a downed enemy once things have been quiet for a few seconds
	if (BestDowned && GetWorld()->GetTimeSeconds() - LastStandingEnemySeenTime > 4.0f)
	{
		return BestDowned;
	}

	// no operators in sight: shoot out enemy drones and cameras
	return FindVisibleDevice(Op);
}

AActor* ASiegeBotController::FindVisibleDevice(ASiegeOperator* Op) const
{
	const FVector Eye = Op->GetAimOrigin();
	const FVector Forward = Op->GetControlRotation().Vector();
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(SightHalfAngle));

	AActor* Best = nullptr;
	float BestDist = 1500.0f;

	for (APawn* Device : SiegeObservation::GetTeamDevices(GetWorld(), 1 - Op->GetTeam()))
	{
		const FVector ToDevice = Device->GetActorLocation() - Eye;
		const float Dist = ToDevice.Size();

		if (Dist < BestDist && FVector::DotProduct(ToDevice.GetSafeNormal(), Forward) >= CosHalfAngle && HasLineOfSight(Op, Device, Device->GetActorLocation()))
		{
			Best = Device;
			BestDist = Dist;
		}
	}

	return Best;
}

FVector ASiegeBotController::GetAimPoint(AActor* InTarget) const
{
	if (const ASiegeOperator* EnemyOp = Cast<ASiegeOperator>(InTarget))
	{
		return bAimForHead ? EnemyOp->GetHeadLocation() : EnemyOp->GetChestLocation();
	}

	return InTarget ? InTarget->GetActorLocation() : FVector::ZeroVector;
}

bool ASiegeBotController::HasLineOfSight(ASiegeOperator* Op, AActor* Enemy, const FVector& Point) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SiegeBotSight), true, Op);

	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Op->GetAimOrigin(), Point, ECC_Visibility, Params))
	{
		return Hit.GetActor() == Enemy;
	}

	return true;
}

void ASiegeBotController::UpdateCombat(ASiegeOperator* Op, float Now)
{
	AActor* Enemy = Target.Get();
	const ASiegeOperator* EnemyOperator = Cast<ASiegeOperator>(Enemy);
	const ISiegeObserverDevice* EnemyDevice = Cast<ISiegeObserverDevice>(Enemy);

	// stop shooting someone the moment they go down; finishing them is a separate, later decision
	if (EnemyOperator && EnemyOperator->IsDowned() && Now - LastStandingEnemySeenTime < 4.0f)
	{
		Target.Reset();
		Enemy = nullptr;
	}

	if (!Enemy || (EnemyOperator && EnemyOperator->IsDead()) || (EnemyDevice && !EnemyDevice->IsDeviceAlive()))
	{
		StopFire(Op);

		// keep watching where we last saw someone for a few seconds (unless lining up a breach charge)
		if (BreachStep == EBreachStep::Aim)
		{
			// the breach routine owns the aim point
		}
		else if (Now - LastEnemySeenTime < 3.0f)
		{
			SetFocalPoint(LastKnownEnemyLocation + FVector(0, 0, 60.0f), EAIFocusPriority::Gameplay);
		}
		else if (Now - LastHeardTime < 2.5f)
		{
			// turn toward what we just heard
			SetFocalPoint(LastHeardLocation + FVector(0, 0, 60.0f), EAIFocusPriority::Gameplay);
		}
		else
		{
			ClearFocus(EAIFocusPriority::Gameplay);
		}

		// top up the magazine between fights
		if (ASiegeGun* Gun = Op->GetCurrentGun())
		{
			if (!Gun->IsReloading() && Gun->GetBulletCount() < Gun->GetMagazineSize() * 0.4f)
			{
				Op->ReloadWeapon();
			}
		}
		return;
	}

	// aim error shrinks as the bot settles onto the target
	const float Settle = FMath::Clamp((Now - TargetAcquiredTime) / AimSettleTime, 0.0f, 1.0f);
	const float ErrorRadius = FMath::Lerp(InitialAimError, SettledAimError, Settle) * GetDifficultyScale();
	// devices are small, so aim error is scaled down for them
	const float ErrorScale = EnemyOperator ? 1.0f : 0.25f;
	const FVector AimPoint = GetAimPoint(Enemy) + AimErrorDirection * ErrorRadius * ErrorScale;

	SetFocalPoint(AimPoint, EAIFocusPriority::Gameplay);

	// wait out the reaction time before pulling the trigger
	if (Now - TargetAcquiredTime < ReactionDelay)
	{
		return;
	}

	// only shoot when roughly facing the target
	const FVector ToAim = (AimPoint - Op->GetAimOrigin()).GetSafeNormal();
	const bool bFacing = FVector::DotProduct(ToAim, GetControlRotation().Vector()) > 0.97f;

	ASiegeGun* Gun = Op->GetCurrentGun();

	if (!bFacing || (Gun && Gun->IsReloading()))
	{
		StopFire(Op);
		return;
	}

	// fire in bursts; semi-auto weapons re-press the trigger each burst
	if (bFiring)
	{
		if (Now >= BurstEndTime)
		{
			StopFire(Op);
			NextBurstTime = Now + FMath::FRandRange(0.15f, 0.4f);
			AimErrorDirection = FMath::VRand();
		}
	}
	else if (Now >= NextBurstTime)
	{
		const bool bAuto = Gun && Gun->IsFullAuto();
		BurstEndTime = Now + (bAuto ? FMath::FRandRange(0.25f, 0.6f) : 0.1f);
		StartFire(Op);
	}
}

void ASiegeBotController::StartFire(ASiegeOperator* Op)
{
	if (!bFiring)
	{
		bFiring = true;
		Op->DoStartFiring();
	}
}

void ASiegeBotController::StopFire(ASiegeOperator* Op)
{
	if (bFiring)
	{
		bFiring = false;
		Op->DoStopFiring();
	}
}

void ASiegeBotController::MoveTowards(const FVector& Goal, float Acceptance)
{
	const float Now = GetWorld()->GetTimeSeconds();
	const bool bIdle = GetMoveStatus() == EPathFollowingStatus::Idle;

	if (bHasMoveGoal && FVector::Dist(Goal, MoveGoal) < 100.0f && !bIdle)
	{
		return;
	}

	// don't spam path requests when the goal is unreachable
	if (bHasMoveGoal && FVector::Dist(Goal, MoveGoal) < 100.0f && Now - LastRepathTime < 1.0f)
	{
		return;
	}

	MoveGoal = Goal;
	bHasMoveGoal = true;
	LastRepathTime = Now;

	MoveToLocation(Goal, Acceptance, true, true, true, false, nullptr, true);
}

void ASiegeBotController::UpdateObjective(ASiegeGameMode* GM, ASiegeOperator* Op, float Now)
{
	const ESiegePhase Phase = GM->GetPhase();
	const bool bAttacker = GM->IsAttackingTeam(Op->GetTeam());
	const bool bEngaged = Target.IsValid();
	const FSiegeLayoutResult& Layout = GM->GetLayout();

	if (!GM->CanTeamMove(Op->GetTeam()))
	{
		StopMovement();
		return;
	}

	// hold still and fight when an enemy is in sight
	if (bEngaged)
	{
		Op->StopInteract();
		StopMovement();
		bHasMoveGoal = false;
		return;
	}

	ASiegeDevice* Device = GM->GetActiveDevice();

	// pick up downed teammates, unless we're a defender who needs to get to the device
	if (!(Device && !bAttacker) && UpdateRevive(Op, Now))
	{
		return;
	}

	if (bAttacker)
	{
		if (Device)
		{
			// guard the planted device from nearby cover
			if (FVector::Dist(Op->GetActorLocation(), Device->GetActorLocation()) > 500.0f)
			{
				MoveTowards(Device->GetActorLocation(), 300.0f);
			}
			else
			{
				StopMovement();
			}
			return;
		}

		const int32 SiteIndex = GM->GetBotAttackSite();
		if (!Layout.Sites.IsValidIndex(SiteIndex) || !Layout.Sites[SiteIndex].IsValid())
		{
			return;
		}

		ASiegeBombSite* Site = Layout.Sites[SiteIndex].Get();

		// stage at the entry point first
		if (!bReachedEntry)
		{
			if (FVector::Dist2D(Op->GetActorLocation(), EntryPoint) < 250.0f)
			{
				bReachedEntry = true;
			}
			else
			{
				MoveTowards(EntryPoint, 150.0f);
				return;
			}
		}

		// breachers open a site wall before the push
		if (UpdateBreach(GM, Op, Now))
		{
			return;
		}

		// intel operators scan as they close in
		if (Op->GetOperatorDef().UniqueGadget == ESiegeGadget::PulseScanner && Op->GetUniqueGadgetCount() > 0 && Now >= NextScanTime
			&& FVector::Dist(Op->GetActorLocation(), Site->GetActorLocation()) < 1800.0f)
		{
			Op->UseUniqueGadget();
			NextScanTime = Now + 12.0f;
		}

		if (Site->IsInside(Op->GetActorLocation()))
		{
			// plant once the area has been quiet for a moment
			if (Phase == ESiegePhase::Action && Now - LastEnemySeenTime > 1.5f)
			{
				StopMovement();
				if (!Op->IsInteracting())
				{
					Op->StartInteract();
				}
			}
			return;
		}

		if (!bHasMoveGoal || !Site->IsInside(MoveGoal))
		{
			MoveTowards(Site->GetRandomPoint(), 80.0f);
		}
		else
		{
			MoveTowards(MoveGoal, 80.0f);
		}
		return;
	}

	// defenders
	if (Device)
	{
		if (FVector::Dist(Op->GetActorLocation(), Device->GetActorLocation()) < 150.0f)
		{
			StopMovement();
			if (!Op->IsInteracting())
			{
				Op->StartInteract();
			}
		}
		else
		{
			MoveTowards(Device->GetActorLocation(), 80.0f);
		}
		return;
	}

	if (bRoamer && Phase != ESiegePhase::Preparation)
	{
		if (Now >= NextRoamTime || !bHasMoveGoal)
		{
			NextRoamTime = Now + FMath::FRandRange(10.0f, 18.0f);
			MoveTowards(Layout.RoamPoints[FMath::RandRange(0, Layout.RoamPoints.Num() - 1)], 120.0f);
		}
		return;
	}

	if (FVector::Dist2D(Op->GetActorLocation(), Post) > 120.0f)
	{
		MoveTowards(Post, 80.0f);
	}
}

void ASiegeBotController::HandleStuck(ASiegeOperator* Op, float Now)
{
	if (!bHasMoveGoal || GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		LastProgressLocation = Op->GetActorLocation();
		LastProgressTime = Now;
		return;
	}

	if (FVector::Dist2D(Op->GetActorLocation(), LastProgressLocation) > 60.0f)
	{
		LastProgressLocation = Op->GetActorLocation();
		LastProgressTime = Now;
		return;
	}

	if (Now - LastProgressTime < 0.7f)
	{
		return;
	}

	// bash through barricades in the direction we're trying to go
	if (Now - LastBashTime > 0.8f)
	{
		LastBashTime = Now;

		FVector Dir = (MoveGoal - Op->GetActorLocation()).GetSafeNormal2D();
		if (const UPathFollowingComponent* PFC = GetPathFollowingComponent())
		{
			const FVector PathDir = PFC->GetCurrentDirection();
			if (!PathDir.IsNearlyZero())
			{
				Dir = PathDir.GetSafeNormal2D();
			}
		}

		// kick out whatever is left of a barricade just ahead, including pieces at head height
		const FVector Ahead = Op->GetActorLocation() + Dir * 60.0f;

		for (TActorIterator<ASiegeWall> It(GetWorld()); It; ++It)
		{
			ASiegeWall* Wall = *It;
			if (Wall->IsBarricade() && Wall->GetIntegrity() > 0.0f && FVector::Dist2D(Wall->GetActorLocation(), Ahead) < 160.0f)
			{
				Wall->DestroyAllCells();
			}
		}
	}

	// truly stuck: re-path
	if (Now - LastProgressTime > 3.0f)
	{
		LastProgressTime = Now;
		bHasMoveGoal = false;
		StopMovement();
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Reviving

bool ASiegeBotController::UpdateRevive(ASiegeOperator* Op, float Now)
{
	// keep going if we're already mid-revive
	if (Op->IsInteracting() && Op->GetCurrentInteraction() == ESiegeInteraction::Revive)
	{
		StopMovement();
		return true;
	}

	// don't walk into a fight to do it
	if (Now - LastEnemySeenTime < 1.5f)
	{
		return false;
	}

	ASiegeOperator* Downed = Op->FindDownedTeammate(1500.0f);
	if (!Downed)
	{
		return false;
	}

	if (FVector::Dist(Op->GetActorLocation(), Downed->GetActorLocation()) < 120.0f)
	{
		StopMovement();
		Op->StartInteract();
	}
	else
	{
		MoveTowards(Downed->GetActorLocation(), 60.0f);
	}

	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Breaching

bool ASiegeBotController::PickBreachWall(ASiegeGameMode* GM, ASiegeOperator* Op)
{
	const FSiegeLayoutResult& Layout = GM->GetLayout();
	const int32 SiteIndex = GM->GetBotAttackSite();
	if (!Layout.Sites.IsValidIndex(SiteIndex) || !Layout.Sites[SiteIndex].IsValid())
	{
		return false;
	}

	const ASiegeBombSite* Site = Layout.Sites[SiteIndex].Get();
	const FVector SiteCenter = Site->GetActorLocation();
	const bool bHard = BreachGadget == ESiegeGadget::ThermalCharge;

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());

	ASiegeWall* BestWall = nullptr;
	FVector BestStand = FVector::ZeroVector;
	FVector BestOutward = FVector::ZeroVector;
	float BestScore = TNumericLimits<float>::Max();

	for (const TWeakObjectPtr<ASiegeWall>& WallPtr : Layout.SiteWalls)
	{
		ASiegeWall* Wall = WallPtr.Get();
		if (!Wall || Wall->IsHorizontal() || Wall->IsBarricade() || Wall->IsElectrified() || Wall->GetIntegrity() < 0.9f)
		{
			continue;
		}

		// soft charges can't open reinforced walls
		if (!bHard && Wall->IsReinforced())
		{
			continue;
		}

		if (FVector::Dist2D(Wall->GetActorLocation(), SiteCenter) > 900.0f)
		{
			continue;
		}

		// stand on the side of the panel facing away from the site
		const FVector Normal = Wall->GetActorForwardVector().GetSafeNormal2D();
		const FVector ToSite = (SiteCenter - Wall->GetActorLocation()).GetSafeNormal2D();
		const FVector Outward = FVector::DotProduct(Normal, ToSite) > 0.0f ? -Normal : Normal;

		// stand on the site's floor (it may be a level below or above the breacher)
		FVector Stand = Wall->GetActorLocation() + Outward * 110.0f;
		Stand.Z = SiteCenter.Z - 50.0f;

		if (Site->IsInside(Stand))
		{
			continue;
		}

		if (NavSys)
		{
			FNavLocation Projected;
			if (!NavSys->ProjectPointToNavigation(Stand, Projected, FVector(40.0f, 40.0f, 150.0f)))
			{
				continue;
			}
			Stand = Projected.Location;
		}

		// thermal charges are wasted on soft walls, so strongly prefer reinforced ones
		const float Score = FVector::Dist(Op->GetActorLocation(), Stand) - ((bHard && Wall->IsReinforced()) ? 1500.0f : 0.0f);
		if (Score < BestScore)
		{
			BestScore = Score;
			BestWall = Wall;
			BestStand = Stand;
			BestOutward = Outward;
		}
	}

	if (!BestWall)
	{
		return false;
	}

	BreachWall = BestWall;
	BreachStand = BestStand;
	BreachRetreat = BestStand + BestOutward * 300.0f;

	if (NavSys)
	{
		FNavLocation Projected;
		if (NavSys->ProjectPointToNavigation(BreachRetreat, Projected, FVector(150.0f, 150.0f, 150.0f)))
		{
			BreachRetreat = Projected.Location;
		}
	}

	return true;
}

bool ASiegeBotController::UpdateBreach(ASiegeGameMode* GM, ASiegeOperator* Op, float Now)
{
	if (BreachGadget == ESiegeGadget::None || BreachStep == EBreachStep::None || BreachStep == EBreachStep::Done)
	{
		return false;
	}

	if (GM->GetPhase() != ESiegePhase::Action || (Op->GetUniqueGadgetCount() <= 0 && BreachStep != EBreachStep::Retreat))
	{
		BreachStep = EBreachStep::Done;
		return false;
	}

	if (!BreachWall.IsValid())
	{
		const bool bPicked = PickBreachWall(GM, Op);
		UE_LOG(LogSeige, Log, TEXT("Breacher %s (%s): %s"), *Op->GetDisplayName(), *SiegeGadgets::GetName(BreachGadget),
			bPicked ? *FString::Printf(TEXT("targeting %s wall (reinforced %d)"), *BreachWall->GetRoomLabel(), BreachWall->IsReinforced()) : TEXT("no usable wall"));

		if (!bPicked)
		{
			BreachStep = EBreachStep::Done;
			return false;
		}

		BreachStep = EBreachStep::MoveToWall;
		BreachStepTime = Now;
	}

	// time spent fighting or reviving elsewhere does not count against the breach
	if (LastBreachTick > 0.0f && Now - LastBreachTick > 0.4f)
	{
		BreachStepTime += Now - LastBreachTick;
	}
	LastBreachTick = Now;

	// give up if a step takes too long (blocked path...)
	if (Now - BreachStepTime > (BreachStep == EBreachStep::MoveToWall ? 40.0f : 10.0f) && BreachStep != EBreachStep::Done)
	{
		UE_LOG(LogSeige, Log, TEXT("Breacher %s gave up at step %d"), *Op->GetDisplayName(), static_cast<int32>(BreachStep));
		BreachStep = EBreachStep::Done;
		ClearFocus(EAIFocusPriority::Gameplay);
		return false;
	}

	ASiegeWall* Wall = BreachWall.Get();

	switch (BreachStep)
	{
	case EBreachStep::MoveToWall:
	{
		// someone already opened it
		if (Wall->GetIntegrity() < 0.5f)
		{
			BreachStep = EBreachStep::Done;
			return false;
		}

		if (FVector::Dist2D(Op->GetActorLocation(), BreachStand) > 70.0f || FMath::Abs(Op->GetActorLocation().Z - BreachStand.Z) > 150.0f)
		{
			MoveTowards(BreachStand, 30.0f);
			return true;
		}

		StopMovement();
		BreachStep = EBreachStep::Aim;
		BreachStepTime = Now;
		return true;
	}

	case EBreachStep::Aim:
	{
		FVector AimPoint = Wall->GetActorLocation();
		AimPoint.Z = Op->GetAimOrigin().Z - 15.0f;
		SetFocalPoint(AimPoint, EAIFocusPriority::Gameplay);

		const FVector ToAim = (AimPoint - Op->GetAimOrigin()).GetSafeNormal();
		const bool bFacing = FVector::DotProduct(ToAim, GetControlRotation().Vector()) > 0.97f;

		if (bFacing && Now - BreachStepTime > 0.5f)
		{
			const int32 Before = Op->GetUniqueGadgetCount();
			Op->UseUniqueGadget();

			if (Op->GetUniqueGadgetCount() < Before)
			{
				BreachStep = EBreachStep::Retreat;
				BreachStepTime = Now;
				ClearFocus(EAIFocusPriority::Gameplay);
			}
			else if (Now - BreachStepTime > 3.0f)
			{
				BreachStep = EBreachStep::Done;
				ClearFocus(EAIFocusPriority::Gameplay);
			}
		}
		return true;
	}

	case EBreachStep::Retreat:
	{
		MoveTowards(BreachRetreat, 40.0f);

		if (BreachGadget == ESiegeGadget::BreachCharge)
		{
			// back off, then blow it
			if (FVector::Dist2D(Op->GetActorLocation(), BreachRetreat) < 100.0f || Now - BreachStepTime > 2.5f)
			{
				if (Op->HasPlacedCharge())
				{
					Op->UseUniqueGadget();
				}
				BreachStep = EBreachStep::Done;
			}
		}
		else if (Now - BreachStepTime > 5.0f)
		{
			// thermal charges burn through on their own
			BreachStep = EBreachStep::Done;
		}
		return true;
	}

	default:
		return false;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Hearing

void ASiegeBotController::OnHeardNoise(const FVector& Location, float Loudness, uint8 SourceTeam)
{
	const ASiegeOperator* Op = GetOperator();
	if (!Op || Op->IsDead() || Op->IsDowned() || SourceTeam == Op->GetTeam())
	{
		return;
	}

	// louder sounds carry further; a gunshot is heard across the map, footsteps only nearby
	const float HearingRange = 2500.0f * Loudness;
	if (FVector::Dist(Op->GetActorLocation(), Location) > HearingRange)
	{
		return;
	}

	LastHeardLocation = Location;
	LastHeardTime = GetWorld()->GetTimeSeconds();
}

float ASiegeBotController::GetDifficultyScale()
{
	const USiegeUserSettings* Settings = USiegeUserSettings::Get();

	switch (Settings ? Settings->BotDifficulty : ESiegeBotDifficulty::Regular)
	{
	case ESiegeBotDifficulty::Recruit:	return 1.5f;
	case ESiegeBotDifficulty::Veteran:	return 0.7f;
	default:							return 1.0f;
	}
}
