// Copyright Epic Games, Inc. All Rights Reserved.

#include "SiegeOperator.h"
#include "SiegeGun.h"
#include "SiegeWall.h"
#include "SiegeGadgets.h"
#include "SiegeObjective.h"
#include "SiegeGameMode.h"
#include "SiegePlayerController.h"
#include "ShooterWeapon.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

ASiegeOperator::ASiegeOperator()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;
	GetCharacterMovement()->bCanWalkOffLedgesWhenCrouching = true;
	GetCharacterMovement()->SetCrouchedHalfHeight(60.0f);
	GetCharacterMovement()->JumpZVelocity = 0.0f;

	// operators never respawn mid-round
	RespawnTime = 0.0f;

	// the game mode assigns controllers explicitly
	AutoPossessAI = EAutoPossessAI::Disabled;
}

void ASiegeOperator::BeginPlay()
{
	Super::BeginPlay();

	FirstPersonMeshBaseLocation = GetFirstPersonMesh()->GetRelativeLocation();
	BaseFieldOfView = GetFirstPersonCameraComponent()->FieldOfView;
	BaseFirstPersonFieldOfView = GetFirstPersonCameraComponent()->FirstPersonFieldOfView;

	// bullets trace against the body mesh so we can detect headshots; the capsule lets them through
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	GetMesh()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}

void ASiegeOperator::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	ASiegePlayerController* PC = Cast<ASiegePlayerController>(GetController());
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);

	if (!PC || !EIC)
	{
		return;
	}

	EIC->BindAction(PC->GetSiegeAction(TEXT("LeanLeft")), ETriggerEvent::Started, this, &ASiegeOperator::OnLeanLeftPressed);
	EIC->BindAction(PC->GetSiegeAction(TEXT("LeanLeft")), ETriggerEvent::Completed, this, &ASiegeOperator::OnLeanLeftReleased);
	EIC->BindAction(PC->GetSiegeAction(TEXT("LeanRight")), ETriggerEvent::Started, this, &ASiegeOperator::OnLeanRightPressed);
	EIC->BindAction(PC->GetSiegeAction(TEXT("LeanRight")), ETriggerEvent::Completed, this, &ASiegeOperator::OnLeanRightReleased);
	EIC->BindAction(PC->GetSiegeAction(TEXT("Crouch")), ETriggerEvent::Started, this, &ASiegeOperator::ToggleCrouch);
	EIC->BindAction(PC->GetSiegeAction(TEXT("Sprint")), ETriggerEvent::Started, this, &ASiegeOperator::OnSprintPressed);
	EIC->BindAction(PC->GetSiegeAction(TEXT("Sprint")), ETriggerEvent::Completed, this, &ASiegeOperator::OnSprintReleased);
	EIC->BindAction(PC->GetSiegeAction(TEXT("Interact")), ETriggerEvent::Started, this, &ASiegeOperator::StartInteract);
	EIC->BindAction(PC->GetSiegeAction(TEXT("Interact")), ETriggerEvent::Completed, this, &ASiegeOperator::StopInteract);
	EIC->BindAction(PC->GetSiegeAction(TEXT("Gadget")), ETriggerEvent::Started, this, &ASiegeOperator::UseUniqueGadget);
	EIC->BindAction(PC->GetSiegeAction(TEXT("Secondary")), ETriggerEvent::Started, this, &ASiegeOperator::UseSecondaryGadget);
	EIC->BindAction(PC->GetSiegeAction(TEXT("Reload")), ETriggerEvent::Started, this, &ASiegeOperator::ReloadWeapon);
	EIC->BindAction(PC->GetSiegeAction(TEXT("Melee")), ETriggerEvent::Started, this, &ASiegeOperator::Melee);
	EIC->BindAction(PC->GetSiegeAction(TEXT("Observe")), ETriggerEvent::Started, this, &ASiegeOperator::OnObservePressed);
	EIC->BindAction(PC->GetSiegeAction(TEXT("Aim")), ETriggerEvent::Started, this, &ASiegeOperator::OnAimPressed);
	EIC->BindAction(PC->GetSiegeAction(TEXT("Aim")), ETriggerEvent::Completed, this, &ASiegeOperator::OnAimReleased);
	EIC->BindAction(PC->GetSiegeAction(TEXT("SwapWeapon")), ETriggerEvent::Started, this, &ASiegeOperator::OnSwapWeaponPressed);
}

void ASiegeOperator::OnObservePressed()
{
	// works while dead too: eliminated players keep watching drones and cameras
	if (ASiegePlayerController* PC = Cast<ASiegePlayerController>(GetController()))
	{
		PC->EnterObservation();
	}
}

void ASiegeOperator::ApplyOperatorDef(const FSiegeOperatorDef& Def, int32 Reinforcements)
{
	OperatorDef = Def;

	MaxHP = Def.MaxHealth;
	CurrentHP = MaxHP;
	OnDamaged.Broadcast(1.0f);

	UniqueGadgetCount = Def.UniqueGadgetCount;
	SecondaryGadgetCount = Def.SecondaryGadgetCount;
	ReinforcementsLeft = Reinforcements;
	DroneCount = Def.bAttacker ? 2 : 0;

	// the last weapon added becomes active, so add the sidearm first
	if (Def.SecondaryWeapon)
	{
		AddWeaponClass(Def.SecondaryWeapon);
	}

	if (Def.PrimaryWeapon)
	{
		AddWeaponClass(Def.PrimaryWeapon);
	}

	UpdateMovementSpeed();
}

void ASiegeOperator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (IsDead())
	{
		return;
	}

	UpdateLean(DeltaSeconds);
	UpdateAim(DeltaSeconds);
	UpdateMovementSpeed();
	UpdateInteraction(DeltaSeconds);
}

// ---------------------------------------------------------------------------------------------------------------------
// Movement

void ASiegeOperator::DoMove(float Right, float Forward)
{
	ForwardInput = Forward;

	// planting, disabling and reinforcing pin you in place
	if (IsInteracting())
	{
		return;
	}

	// attackers hold outside until the action phase starts
	if (const ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>())
	{
		if (!GM->CanTeamMove(GetTeam()))
		{
			return;
		}
	}

	Super::DoMove(Right, Forward);
}

void ASiegeOperator::DoJumpStart()
{
	if (IsDead() || IsInteracting())
	{
		return;
	}

	// there is no free jump: Space vaults over low obstacles in front of you
	const FVector Forward = GetActorForwardVector();
	const FVector Feet = GetActorLocation() - FVector(0.0f, 0.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());

	FCollisionQueryParams Params(SCENE_QUERY_STAT(SiegeVault), false, this);

	FHitResult LowHit;
	const FVector LowStart = Feet + FVector(0.0f, 0.0f, 45.0f);
	if (!GetWorld()->LineTraceSingleByChannel(LowHit, LowStart, LowStart + Forward * 110.0f, ECC_Visibility, Params))
	{
		return;
	}

	FHitResult HighHit;
	const FVector HighStart = Feet + FVector(0.0f, 0.0f, 135.0f);
	if (GetWorld()->LineTraceSingleByChannel(HighHit, HighStart, HighStart + Forward * 140.0f, ECC_Visibility, Params))
	{
		// obstacle is too tall to vault
		return;
	}

	if (bIsCrouched)
	{
		UnCrouch();
	}

	LaunchCharacter(Forward * 330.0f + FVector(0.0f, 0.0f, 480.0f), true, true);
}

void ASiegeOperator::DoJumpEnd()
{
	// unused: vaulting is instant
}

void ASiegeOperator::ToggleCrouch()
{
	if (IsDead())
	{
		return;
	}

	if (bIsCrouched)
	{
		UnCrouch();
	}
	else
	{
		SetSprinting(false);
		Crouch();
	}
}

void ASiegeOperator::SetSprinting(bool bSprint)
{
	bSprintHeld = bSprint;

	if (bSprint)
	{
		if (bIsCrouched)
		{
			UnCrouch();
		}

		bAimHeld = false;
		DoStopFiring();
	}
}

void ASiegeOperator::SetAiming(bool bAim)
{
	bAimHeld = bAim && !IsDead();

	// aiming and sprinting are mutually exclusive; the most recent input wins
	if (bAimHeld)
	{
		bSprintHeld = false;
	}
}

void ASiegeOperator::UpdateAim(float DeltaSeconds)
{
	const float Target = (bAimHeld && !IsSprinting() && !IsInteracting()) ? 1.0f : 0.0f;
	AimAlpha = FMath::FInterpTo(AimAlpha, Target, DeltaSeconds, 12.0f);

	UCameraComponent* Camera = GetFirstPersonCameraComponent();
	Camera->SetFieldOfView(FMath::Lerp(BaseFieldOfView, BaseFieldOfView * AimFOVMultiplier, AimAlpha));
	Camera->FirstPersonFieldOfView = FMath::Lerp(BaseFirstPersonFieldOfView, BaseFirstPersonFieldOfView * 0.85f, AimAlpha);
}

void ASiegeOperator::DoAim(float Yaw, float Pitch)
{
	// slower look while zoomed in
	const float Scale = FMath::Lerp(1.0f, AimLookSensitivity, AimAlpha);
	Super::DoAim(Yaw * Scale, Pitch * Scale);
}

void ASiegeOperator::AddWeaponRecoil(float Recoil)
{
	Super::AddWeaponRecoil(Recoil * FMath::Lerp(1.0f, AimRecoilMultiplier, AimAlpha));
}

bool ASiegeOperator::IsSprinting() const
{
	return bSprintHeld && ForwardInput > 0.1f && !bIsCrouched && !IsInteracting() && GetVelocity().Size2D() > 10.0f;
}

void ASiegeOperator::UpdateMovementSpeed()
{
	const float Walk = OperatorDef.WalkSpeed;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	const float AimedWalk = Walk * FMath::Lerp(1.0f, AimSpeedMultiplier, AimAlpha);
	Move->MaxWalkSpeed = (bSprintHeld && ForwardInput > 0.1f) ? Walk * SprintMultiplier : AimedWalk;
	Move->MaxWalkSpeedCrouched = Walk * 0.5f;
}

void ASiegeOperator::SetLean(float Direction)
{
	LeanInput = FMath::Clamp(Direction, -1.0f, 1.0f);
}

void ASiegeOperator::RefreshLean()
{
	// leaning is a toggle-free hold; holding both keys cancels out
	SetLean((bLeanRightHeld ? 1.0f : 0.0f) - (bLeanLeftHeld ? 1.0f : 0.0f));
}

void ASiegeOperator::UpdateLean(float DeltaSeconds)
{
	const float TargetLean = IsSprinting() ? 0.0f : LeanInput;
	CurrentLean = FMath::FInterpTo(CurrentLean, TargetLean, DeltaSeconds, 10.0f);

	// shift the first person rig sideways so the camera peeks around cover
	const FVector WorldOffset = GetActorRightVector() * (CurrentLean * LeanDistance) + FVector(0.0f, 0.0f, -FMath::Abs(CurrentLean) * 6.0f);
	const FVector LocalOffset = GetMesh()->GetComponentTransform().InverseTransformVectorNoScale(WorldOffset);
	GetFirstPersonMesh()->SetRelativeLocation(FirstPersonMeshBaseLocation + LocalOffset);

	// tilt the view for local players
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		FRotator ControlRot = PC->GetControlRotation();
		ControlRot.Roll = CurrentLean * LeanRoll;
		PC->SetControlRotation(ControlRot);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Combat

bool ASiegeOperator::CanFireWeapon() const
{
	if (IsDead() || IsSprinting() || IsInteracting())
	{
		return false;
	}

	if (const ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>())
	{
		return GM->CanTeamAct(GetTeam());
	}

	return true;
}

void ASiegeOperator::OnWeaponFired()
{
	LastShotTime = GetWorld()->GetTimeSeconds();
}

void ASiegeOperator::OnHitConfirmed(ASiegeOperator* Victim)
{
	LastHitConfirmTime = GetWorld()->GetTimeSeconds();
}

FVector ASiegeOperator::GetAimOrigin() const
{
	return GetFirstPersonCameraComponent()->GetComponentLocation();
}

FVector ASiegeOperator::GetPawnViewLocation() const
{
	return GetAimOrigin();
}

float ASiegeOperator::GetSpreadMultiplier() const
{
	const float Speed = GetVelocity().Size2D();
	float Multiplier = 1.0f + FMath::Clamp(Speed / 400.0f, 0.0f, 1.5f) * 2.0f;

	// aiming down sights tightens the cone a lot
	Multiplier *= FMath::Lerp(1.0f, AimSpreadMultiplier, AimAlpha);

	if (bIsCrouched)
	{
		Multiplier *= 0.7f;
	}

	if (GetCharacterMovement()->IsFalling())
	{
		Multiplier *= 3.0f;
	}

	return Multiplier;
}

FVector ASiegeOperator::GetHeadLocation() const
{
	return GetMesh()->DoesSocketExist(TEXT("head")) ? GetMesh()->GetSocketLocation(TEXT("head")) : GetActorLocation() + FVector(0.0f, 0.0f, 70.0f);
}

FVector ASiegeOperator::GetChestLocation() const
{
	return GetMesh()->DoesSocketExist(TEXT("spine_04")) ? GetMesh()->GetSocketLocation(TEXT("spine_04")) : GetActorLocation() + FVector(0.0f, 0.0f, 35.0f);
}

ASiegeGun* ASiegeOperator::GetCurrentGun() const
{
	return Cast<ASiegeGun>(CurrentWeapon.Get());
}

void ASiegeOperator::ReloadWeapon()
{
	if (ASiegeGun* Gun = GetCurrentGun())
	{
		if (!IsDead())
		{
			Gun->Reload();
		}
	}
}

float ASiegeOperator::TakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (IsDead() || Damage <= 0.0f)
	{
		return 0.0f;
	}

	bool bHeadshot = false;

	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const FPointDamageEvent& PointEvent = static_cast<const FPointDamageEvent&>(DamageEvent);
		const FString Bone = PointEvent.HitInfo.BoneName.ToString().ToLower();

		bHeadshot = Bone.Contains(TEXT("head"));

		const ASiegeGun* Gun = Cast<ASiegeGun>(DamageCauser);
		if (bHeadshot && Gun)
		{
			// a clean headshot from a firearm is lethal regardless of health pool
			Damage = CurrentHP + 1.0f;
		}
		else if (Bone.Contains(TEXT("thigh")) || Bone.Contains(TEXT("calf")) || Bone.Contains(TEXT("foot")))
		{
			Damage *= 0.75f;
		}
	}

	CurrentHP -= Damage;
	LastDamagedTime = GetWorld()->GetTimeSeconds();

	OnDamaged.Broadcast(FMath::Max(0.0f, CurrentHP / MaxHP));

	if (CurrentHP <= 0.0f)
	{
		CurrentHP = 0.0f;

		if (ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>())
		{
			KillerName = GM->GetDisplayNameFor(EventInstigator);
		}

		Die();

		if (ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>())
		{
			GM->OnOperatorKilled(this, EventInstigator, bHeadshot);
		}
	}

	return Damage;
}

void ASiegeOperator::Die()
{
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->DeactivateWeapon();
	}

	StopInteract();

	Tags.Add(DeathTag);

	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// ragdoll the body; it stays until the next round
	GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->SetOwnerNoSee(false);
	GetFirstPersonMesh()->SetHiddenInGame(true);

	OnBulletCountUpdated.Broadcast(0, 0);

	BP_OnDeath();
}

// ---------------------------------------------------------------------------------------------------------------------
// Interaction

ESiegeInteraction ASiegeOperator::FindAvailableInteraction(ASiegeWall*& OutWall) const
{
	OutWall = nullptr;

	const ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>();
	if (!GM || IsDead())
	{
		return ESiegeInteraction::None;
	}

	const ESiegePhase Phase = GM->GetPhase();
	const bool bAttacker = GM->IsAttackingTeam(GetTeam());

	if (bAttacker)
	{
		if (Phase == ESiegePhase::Action && GM->GetSiteAt(GetActorLocation()) && !GM->GetActiveDevice())
		{
			return ESiegeInteraction::PlantDevice;
		}
	}
	else
	{
		const ASiegeDevice* Device = GM->GetActiveDevice();
		if (Device && FVector::Dist(Device->GetActorLocation(), GetActorLocation()) < InteractRange)
		{
			return ESiegeInteraction::DisableDevice;
		}

		if ((Phase == ESiegePhase::Preparation || Phase == ESiegePhase::Action) && ReinforcementsLeft > 0)
		{
			FHitResult Hit;
			ASiegeWall* Wall = TraceForWall(InteractRange, Hit);
			if (Wall && Wall->CanBeReinforced())
			{
				OutWall = Wall;
				return ESiegeInteraction::Reinforce;
			}
		}
	}

	return ESiegeInteraction::None;
}

FString ASiegeOperator::GetInteractPrompt() const
{
	ASiegeWall* Wall = nullptr;

	switch (FindAvailableInteraction(Wall))
	{
	case ESiegeInteraction::PlantDevice:	return TEXT("Hold [F] to plant the device");
	case ESiegeInteraction::DisableDevice:	return TEXT("Hold [F] to disable the device");
	case ESiegeInteraction::Reinforce:		return FString::Printf(TEXT("Hold [F] to reinforce wall (%d left)"), ReinforcementsLeft);
	default:								return FString();
	}
}

void ASiegeOperator::StartInteract()
{
	if (IsDead())
	{
		return;
	}

	bInteractHeld = true;
	CurrentInteraction = ESiegeInteraction::None;
	InteractProgress = 0.0f;
}

void ASiegeOperator::StopInteract()
{
	bInteractHeld = false;
	CurrentInteraction = ESiegeInteraction::None;
	InteractProgress = 0.0f;
	InteractWall.Reset();
}

float ASiegeOperator::GetInteractProgress01() const
{
	float Duration = 1.0f;

	switch (CurrentInteraction)
	{
	case ESiegeInteraction::PlantDevice:	Duration = PlantTime; break;
	case ESiegeInteraction::DisableDevice:	Duration = DisableTime; break;
	case ESiegeInteraction::Reinforce:		Duration = ReinforceTime; break;
	default:								return 0.0f;
	}

	return FMath::Clamp(InteractProgress / Duration, 0.0f, 1.0f);
}

void ASiegeOperator::UpdateInteraction(float DeltaSeconds)
{
	if (!bInteractHeld)
	{
		return;
	}

	ASiegeWall* Wall = nullptr;
	const ESiegeInteraction Available = FindAvailableInteraction(Wall);

	// restart if what we're doing changed (walked off site, looked at a different wall...)
	if (Available != CurrentInteraction || (Available == ESiegeInteraction::Reinforce && Wall != InteractWall.Get()))
	{
		CurrentInteraction = Available;
		InteractWall = Wall;
		InteractProgress = 0.0f;
	}

	if (CurrentInteraction == ESiegeInteraction::None)
	{
		return;
	}

	if (CurrentInteraction != ESiegeInteraction::None)
	{
		DoStopFiring();
		GetCharacterMovement()->StopMovementImmediately();
	}

	InteractProgress += DeltaSeconds;

	if (GetInteractProgress01() < 1.0f)
	{
		return;
	}

	ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>();
	const ESiegeInteraction Completed = CurrentInteraction;

	StopInteract();

	if (!GM)
	{
		return;
	}

	switch (Completed)
	{
	case ESiegeInteraction::PlantDevice:
		GM->CompletePlant(this);
		break;

	case ESiegeInteraction::DisableDevice:
		GM->CompleteDisable(this);
		break;

	case ESiegeInteraction::Reinforce:
		if (Wall && Wall->Reinforce())
		{
			--ReinforcementsLeft;
		}
		break;

	default:
		break;
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Gadgets and melee

ASiegeWall* ASiegeOperator::TraceForWall(float Distance, FHitResult& OutHit) const
{
	const UCameraComponent* Camera = GetFirstPersonCameraComponent();
	const FVector Start = Camera->GetComponentLocation();
	const FVector End = Start + Camera->GetForwardVector() * Distance;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(SiegeWallTrace), false, this);

	if (GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, Params))
	{
		return Cast<ASiegeWall>(OutHit.GetActor());
	}

	return nullptr;
}

bool ASiegeOperator::HasPlacedCharge() const
{
	for (const TWeakObjectPtr<ASiegeBreachCharge>& Charge : PlacedCharges)
	{
		if (Charge.IsValid() && !Charge->IsHardBreach())
		{
			return true;
		}
	}

	return false;
}

void ASiegeOperator::OnChargeGone(ASiegeBreachCharge* Charge)
{
	PlacedCharges.RemoveAll([Charge](const TWeakObjectPtr<ASiegeBreachCharge>& Ptr) { return !Ptr.IsValid() || Ptr.Get() == Charge; });
}

void ASiegeOperator::UseUniqueGadget()
{
	UseGadget(OperatorDef.UniqueGadget, UniqueGadgetCount);
}

void ASiegeOperator::UseSecondaryGadget()
{
	UseGadget(OperatorDef.SecondaryGadget, SecondaryGadgetCount);
}

void ASiegeOperator::UseGadget(ESiegeGadget Gadget, int32& Count)
{
	ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>();
	if (IsDead() || !GM || Gadget == ESiegeGadget::None)
	{
		return;
	}

	// pressing the key again with a soft charge planted detonates it
	if (Gadget == ESiegeGadget::BreachCharge && HasPlacedCharge())
	{
		TArray<TWeakObjectPtr<ASiegeBreachCharge>> Charges = PlacedCharges;
		for (const TWeakObjectPtr<ASiegeBreachCharge>& Charge : Charges)
		{
			if (Charge.IsValid() && !Charge->IsHardBreach())
			{
				Charge->Detonate();
			}
		}
		return;
	}

	if (Count <= 0 || !GM->CanUseGadgets(GetTeam()))
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	switch (Gadget)
	{
	case ESiegeGadget::BreachCharge:
	case ESiegeGadget::ThermalCharge:
	{
		FHitResult Hit;
		ASiegeWall* Wall = TraceForWall(180.0f, Hit);

		if (!Wall || (Gadget == ESiegeGadget::ThermalCharge && Wall->IsBarricade()))
		{
			GM->PostPlayerMessage(this, TEXT("Aim at a destructible wall or floor to place the charge"));
			return;
		}

		const FVector Location = Hit.ImpactPoint + Hit.ImpactNormal * 4.0f;
		ASiegeBreachCharge* Charge = GetWorld()->SpawnActor<ASiegeBreachCharge>(ASiegeBreachCharge::StaticClass(), Location, Hit.ImpactNormal.Rotation(), SpawnParams);

		if (Charge)
		{
			--Count;
			Charge->SetHardBreach(Gadget == ESiegeGadget::ThermalCharge);
			PlacedCharges.Add(Charge);
			Charge->InitGadget(this, Wall);

			if (Gadget == ESiegeGadget::BreachCharge && IsValid(Charge))
			{
				GM->PostPlayerMessage(this, TEXT("Charge placed. Press again to detonate"));
			}
		}
		break;
	}

	case ESiegeGadget::ShockBox:
	{
		FHitResult Hit;
		ASiegeWall* Wall = TraceForWall(180.0f, Hit);

		if (!Wall || Wall->IsBarricade())
		{
			GM->PostPlayerMessage(this, TEXT("Aim at a wall panel to attach the shock box"));
			return;
		}

		const FVector Location = Hit.ImpactPoint + Hit.ImpactNormal * 8.0f;
		if (ASiegeShockBox* Box = GetWorld()->SpawnActor<ASiegeShockBox>(ASiegeShockBox::StaticClass(), Location, Hit.ImpactNormal.Rotation(), SpawnParams))
		{
			--Count;
			Box->InitGadget(this, Wall);
		}
		break;
	}

	case ESiegeGadget::DeployableShield:
	case ESiegeGadget::AlarmSensor:
	{
		const bool bShield = Gadget == ESiegeGadget::DeployableShield;
		const FVector Ahead = GetActorLocation() + GetActorForwardVector() * (bShield ? 90.0f : 60.0f);

		FHitResult Ground;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SiegeGadgetGround), false, this);
		if (!GetWorld()->LineTraceSingleByChannel(Ground, Ahead + FVector(0, 0, 50.0f), Ahead - FVector(0, 0, 300.0f), ECC_Visibility, Params))
		{
			return;
		}

		const FRotator Facing(0.0f, GetActorRotation().Yaw, 0.0f);

		if (bShield)
		{
			const FVector Location = Ground.ImpactPoint + FVector(0, 0, 57.5f);
			if (ASiegeShield* Shield = GetWorld()->SpawnActor<ASiegeShield>(ASiegeShield::StaticClass(), Location, Facing, SpawnParams))
			{
				--Count;
				Shield->InitGadget(this, nullptr);
			}
		}
		else
		{
			const FVector Location = Ground.ImpactPoint + FVector(0, 0, 4.0f);
			if (ASiegeAlarmSensor* Sensor = GetWorld()->SpawnActor<ASiegeAlarmSensor>(ASiegeAlarmSensor::StaticClass(), Location, Facing, SpawnParams))
			{
				--Count;
				Sensor->InitGadget(this, nullptr);
			}
		}
		break;
	}

	case ESiegeGadget::PulseScanner:
	{
		int32 Found = 0;
		for (TActorIterator<ASiegeOperator> It(GetWorld()); It; ++It)
		{
			if (!It->IsDead() && It->GetTeam() != GetTeam() && FVector::Dist(It->GetActorLocation(), GetActorLocation()) < 1500.0f)
			{
				It->RevealTo(GetTeam(), 4.0f);
				++Found;
			}
		}

		--Count;
		GM->PostTeamMessage(GetTeam(), FString::Printf(TEXT("%s pulse scan: %d enemies detected"), *DisplayName, Found));
		break;
	}

	default:
		break;
	}
}

void ASiegeOperator::Melee()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (IsDead() || IsInteracting() || Now - LastMeleeTime < MeleeCooldown)
	{
		return;
	}

	LastMeleeTime = Now;

	const UCameraComponent* Camera = GetFirstPersonCameraComponent();
	const FVector Start = Camera->GetComponentLocation();
	const FVector End = Start + Camera->GetForwardVector() * 150.0f;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(SiegeMelee), false, this);
	FHitResult Hit;

	if (!GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(12.0f), Params))
	{
		return;
	}

	AActor* HitActor = Hit.GetActor();

	if (ASiegeWall* Wall = Cast<ASiegeWall>(HitActor))
	{
		// melee smashes a chunk out of barricades and a small hole in soft walls
		Wall->ApplyExplosion(Hit.ImpactPoint, Wall->IsBarricade() ? 55.0f : 20.0f, false);
	}
	else if (HitActor)
	{
		UGameplayStatics::ApplyDamage(HitActor, MeleeDamage, GetController(), this, UDamageType::StaticClass());
	}
}

void ASiegeOperator::RevealTo(uint8 ViewingTeam, float Duration)
{
	if (ViewingTeam < 2)
	{
		RevealedUntil[ViewingTeam] = FMath::Max(RevealedUntil[ViewingTeam], GetWorld()->GetTimeSeconds() + Duration);
	}
}

bool ASiegeOperator::IsRevealedTo(uint8 ViewingTeam) const
{
	return ViewingTeam < 2 && RevealedUntil[ViewingTeam] > GetWorld()->GetTimeSeconds();
}
