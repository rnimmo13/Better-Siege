// Copyright Epic Games, Inc. All Rights Reserved.

#include "SiegeOperator.h"
#include "SiegeUserSettings.h"
#include "SiegeSound.h"
#include "SiegeRope.h"
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
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

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

	// character art: the mannequin meshes stay as invisible animation/collision rigs and these follow their pose
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> OperatorMesh(TEXT("/Game/QuantumCharacter/Mesh/SKM_QuantumCharacter.SKM_QuantumCharacter"));
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> OperatorMeshNoHead(TEXT("/Game/QuantumCharacter/Mesh/SKM_QuantumCharacter_NoHead.SKM_QuantumCharacter_NoHead"));

	BodyMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body Mesh"));
	BodyMesh->SetupAttachment(GetMesh());
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetOwnerNoSee(true);
	BodyMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;
	BodyMesh->bUseBoundsFromLeaderPoseComponent = true;
	if (OperatorMesh.Succeeded())
	{
		BodyMesh->SetSkeletalMeshAsset(OperatorMesh.Object);
	}

	FirstPersonBodyMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Body Mesh"));
	FirstPersonBodyMesh->SetupAttachment(GetFirstPersonMesh());
	FirstPersonBodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonBodyMesh->SetOnlyOwnerSee(true);
	FirstPersonBodyMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonBodyMesh->CastShadow = false;
	FirstPersonBodyMesh->bUseBoundsFromLeaderPoseComponent = true;
	if (OperatorMeshNoHead.Succeeded())
	{
		FirstPersonBodyMesh->SetSkeletalMeshAsset(OperatorMeshNoHead.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShirtMat(TEXT("/Game/Siege/Materials/M_Team_Shirt.M_Team_Shirt"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> VestMat(TEXT("/Game/Siege/Materials/M_Team_Vest.M_Team_Vest"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CapMat(TEXT("/Game/Siege/Materials/M_Team_Cap.M_Team_Cap"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HiddenMat(TEXT("/Game/Siege/Materials/M_Invisible.M_Invisible"));
	TeamShirtMaterial = ShirtMat.Object;
	TeamVestMaterial = VestMat.Object;
	TeamCapMaterial = CapMat.Object;
	InvisibleMaterial = HiddenMat.Object;
}

void ASiegeOperator::ApplyTeamLook()
{
	// team 0 wears blue gear, team 1 red
	const bool bBlue = GetTeam() == 0;
	const FLinearColor GearTint = bBlue ? FLinearColor(0.25f, 0.55f, 1.8f) : FLinearColor(1.8f, 0.35f, 0.22f);
	const FLinearColor ShirtTint = bBlue ? FLinearColor(0.8f, 1.0f, 1.4f) : FLinearColor(1.6f, 0.6f, 0.5f);

	auto Tinted = [this](UMaterialInterface* Base, const FLinearColor& Tint) -> UMaterialInterface*
	{
		if (!Base)
		{
			return nullptr;
		}

		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
		MID->SetVectorParameterValue(TEXT("TeamTint"), Tint);
		return MID;
	};

	UMaterialInterface* Shirt = Tinted(TeamShirtMaterial, ShirtTint);
	UMaterialInterface* Vest = Tinted(TeamVestMaterial, GearTint);
	UMaterialInterface* Cap = Tinted(TeamCapMaterial, GearTint);

	for (USkeletalMeshComponent* Body : { BodyMesh.Get(), FirstPersonBodyMesh.Get() })
	{
		if (!Body)
		{
			continue;
		}

		auto SetSlot = [Body](FName Slot, UMaterialInterface* Material)
		{
			const int32 Index = Body->GetMaterialIndex(Slot);
			if (Material && Index != INDEX_NONE)
			{
				Body->SetMaterial(Index, Material);
			}
		};

		SetSlot(TEXT("M_Shirt_RolledUp"), Shirt);
		SetSlot(TEXT("M_Bulletproof_Light"), Vest);
		SetSlot(TEXT("M_Cap"), Cap);

		// the model has a pistol modelled in its holster; the real sidearm is the weapon actor
		SetSlot(TEXT("M_Holster_Hard"), InvisibleMaterial);
		SetSlot(TEXT("M_Pistol"), InvisibleMaterial);
	}
}

void ASiegeOperator::BeginPlay()
{
	Super::BeginPlay();

	FirstPersonMeshBaseLocation = GetFirstPersonMesh()->GetRelativeLocation();
	CameraBaseRelativeLocation = GetFirstPersonCameraComponent()->GetRelativeLocation();
	BaseFieldOfView = GetFirstPersonCameraComponent()->FieldOfView;
	if (const USiegeUserSettings* Settings = USiegeUserSettings::Get())
	{
		BaseFieldOfView = FMath::Clamp(Settings->FieldOfView, 60.0f, 120.0f);
		GetFirstPersonCameraComponent()->SetFieldOfView(BaseFieldOfView);
	}
	BaseFirstPersonFieldOfView = GetFirstPersonCameraComponent()->FirstPersonFieldOfView;

	// bullets trace against the body mesh so we can detect headshots; the capsule lets them through
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	GetMesh()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	// swap the visible art: the mannequins keep animating (and colliding) while hidden, the operator meshes copy their pose
	if (BodyMesh && BodyMesh->GetSkeletalMeshAsset())
	{
		BodyMesh->SetLeaderPoseComponent(GetMesh());
		GetMesh()->SetVisibility(false);
	}

	if (FirstPersonBodyMesh && FirstPersonBodyMesh->GetSkeletalMeshAsset())
	{
		GetFirstPersonMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		FirstPersonBodyMesh->SetLeaderPoseComponent(GetFirstPersonMesh());
		GetFirstPersonMesh()->SetVisibility(false);
	}
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
	EIC->BindAction(PC->GetSiegeAction(TEXT("Rappel")), ETriggerEvent::Started, this, &ASiegeOperator::OnRappelPressed);
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

	ApplyTeamLook();

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

	if (bDowned)
	{
		// bleed out unless a teammate gets here first
		CurrentHP -= DownedBleedRate * DeltaSeconds;
		OnDamaged.Broadcast(FMath::Max(0.0f, CurrentHP / MaxHP));

		if (CurrentHP <= 0.0f)
		{
			CurrentHP = 0.0f;
			FinishDeath(DownedBy.Get(), false);
			return;
		}

		if (!bIsCrouched)
		{
			Crouch();
		}

		UpdateLean(DeltaSeconds);
		UpdateAim(DeltaSeconds);
		UpdateMovementSpeed();
		UpdateFootsteps(DeltaSeconds);
		return;
	}

	if (bRappelling)
	{
		UpdateRappel(DeltaSeconds);
	}

	UpdateLean(DeltaSeconds);
	UpdateAim(DeltaSeconds);
	UpdateMovementSpeed();
	UpdateFootsteps(DeltaSeconds);
	UpdateInteraction(DeltaSeconds);
}

// ---------------------------------------------------------------------------------------------------------------------
// Movement

void ASiegeOperator::DoMove(float Right, float Forward)
{
	ForwardInput = Forward;
	RightInput = Right;

	// on a rope, the move keys climb and slide instead (see UpdateRappel)
	if (bRappelling)
	{
		return;
	}

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
	if (IsDead() || bDowned || IsInteracting())
	{
		return;
	}

	// on a rope, Space crashes through the window in front of you
	if (bRappelling)
	{
		TryRappelEntry();
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
	if (IsDead() || bDowned)
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
	bSprintHeld = bSprint && !bDowned && !bRappelling;

	if (bSprintHeld)
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
	bAimHeld = bAim && !IsDead() && !bDowned;

	// aiming and sprinting are mutually exclusive; the most recent input wins
	if (bAimHeld)
	{
		bSprintHeld = false;
	}
}

void ASiegeOperator::ReleaseHeldInputs()
{
	DoStopFiring();
	StopInteract();
	SetAiming(false);
	SetSprinting(false);
	bLeanLeftHeld = false;
	bLeanRightHeld = false;
	RefreshLean();
}

void ASiegeOperator::UpdateAim(float DeltaSeconds)
{
	// read every frame so a change in the pause menu applies as soon as the game resumes
	if (IsPlayerControlled())
	{
		if (const USiegeUserSettings* Settings = USiegeUserSettings::Get())
		{
			BaseFieldOfView = FMath::Clamp(Settings->FieldOfView, 60.0f, 120.0f);
		}
	}

	const float Target = (bAimHeld && !IsSprinting() && !IsInteracting()) ? 1.0f : 0.0f;
	AimAlpha = FMath::FInterpTo(AimAlpha, Target, DeltaSeconds, 12.0f);

	UCameraComponent* Camera = GetFirstPersonCameraComponent();
	Camera->SetFieldOfView(FMath::Lerp(BaseFieldOfView, BaseFieldOfView * AimFOVMultiplier, AimAlpha));
	Camera->FirstPersonFieldOfView = FMath::Lerp(BaseFirstPersonFieldOfView, BaseFirstPersonFieldOfView * 0.85f, AimAlpha);
}

void ASiegeOperator::UpdateFootsteps(float DeltaSeconds)
{
	const float Speed = GetVelocity().Size2D();
	if (!GetCharacterMovement()->IsMovingOnGround() || Speed < 40.0f)
	{
		FootstepDistance = 0.0f;
		return;
	}

	FootstepDistance += Speed * DeltaSeconds;

	// sprinting is loud, crouch-walking and crawling are nearly silent
	const bool bSprinting = IsSprinting();
	const float Stride = bDowned ? 90.0f : (bSprinting ? 170.0f : (bIsCrouched ? 110.0f : 140.0f));

	if (FootstepDistance >= Stride)
	{
		FootstepDistance = 0.0f;

		const float Volume = bDowned ? 0.15f : (bSprinting ? 1.0f : (bIsCrouched ? 0.2f : (AimAlpha > 0.5f ? 0.35f : 0.55f)));
		const FVector Feet = GetActorLocation() - FVector(0.0f, 0.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		USiegeSoundSubsystem::Play(this, ESiegeSound::Footstep, Feet, Volume, this, GetTeam());
	}
}

void ASiegeOperator::DoAim(float Yaw, float Pitch)
{
	// player sensitivity and invert, then slower look while zoomed in
	float AimSens = AimLookSensitivity;
	FVector2D Look(Yaw, Pitch);

	if (IsPlayerControlled())
	{
		Look = USiegeUserSettings::ScaleLook(Look);

		if (const USiegeUserSettings* Settings = USiegeUserSettings::Get())
		{
			AimSens = Settings->AimSensitivity;
		}
	}

	const float Scale = FMath::Lerp(1.0f, AimSens, AimAlpha);
	Super::DoAim(Look.X * Scale, Look.Y * Scale);
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
	Move->MaxWalkSpeedCrouched = bDowned ? CrawlSpeed : Walk * 0.5f;
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
	const FVector LeanOffset = GetActorRightVector() * (CurrentLean * LeanDistance) + FVector(0.0f, 0.0f, -FMath::Abs(CurrentLean) * 6.0f);

	// true aim down sights: slide the arms and gun so the weapon's own sight sits on the camera's view axis.
	// the camera is a child of the arms rig, so it gets the opposite offset and stays where it is
	UCameraComponent* Camera = GetFirstPersonCameraComponent();
	FVector AdsOffset = FVector::ZeroVector;

	const ASiegeGun* Gun = GetCurrentGun();
	if (Gun && Cast<APlayerController>(GetController()) && !bDowned)
	{
		const FVector CamRight = Camera->GetRightVector();
		const FVector CamUp = Camera->GetUpVector();
		const FVector ToSight = Gun->GetSightWorldLocation() - Camera->GetComponentLocation();

		// where the sight would be without our current correction
		const FVector2D RawSight(FVector::DotProduct(ToSight, CamRight) - AppliedAdsOffset.X, FVector::DotProduct(ToSight, CamUp) - AppliedAdsOffset.Y);
		AppliedAdsOffset = -RawSight * AimAlpha;

		AdsOffset = CamRight * AppliedAdsOffset.X + CamUp * AppliedAdsOffset.Y;
	}
	else
	{
		AppliedAdsOffset = FVector2D::ZeroVector;
	}

	const FVector RigOffsetLocal = GetMesh()->GetComponentTransform().InverseTransformVectorNoScale(LeanOffset + AdsOffset);
	GetFirstPersonMesh()->SetRelativeLocation(FirstPersonMeshBaseLocation + RigOffsetLocal);

	if (const USceneComponent* CameraParent = Camera->GetAttachParent())
	{
		const FTransform ParentTransform = CameraParent->GetSocketTransform(Camera->GetAttachSocketName());
		Camera->SetRelativeLocation(CameraBaseRelativeLocation + ParentTransform.InverseTransformVectorNoScale(-AdsOffset));
	}

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
	if (IsDead() || bDowned || IsSprinting() || IsInteracting())
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
	FName HitBone = NAME_None;

	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const FPointDamageEvent& PointEvent = static_cast<const FPointDamageEvent&>(DamageEvent);
		HitBone = PointEvent.HitInfo.BoneName;
		const FString Bone = HitBone.ToString().ToLower();

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

	LastDamagedTime = GetWorld()->GetTimeSeconds();

	// body shots that would kill leave you down but not out, as long as a teammate could still revive you.
	// headshots, explosions and hits on someone already down kill outright
	ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>();
	const bool bWouldDie = CurrentHP - Damage <= 0.0f;

	if (GM)
	{
		GM->OnOperatorDamaged(this, Damage, HitBone, EventInstigator, bHeadshot);
	}

	if (bWouldDie && !bDowned && !bHeadshot && Cast<ASiegeGun>(DamageCauser) && GM && GM->CanOperatorGoDown(this))
	{
		EnterDowned(EventInstigator);
		return Damage;
	}

	CurrentHP -= Damage;
	OnDamaged.Broadcast(FMath::Max(0.0f, CurrentHP / MaxHP));

	if (CurrentHP <= 0.0f)
	{
		CurrentHP = 0.0f;
		FinishDeath(EventInstigator, bHeadshot);
	}

	return Damage;
}

void ASiegeOperator::FinishDeath(AController* Killer, bool bHeadshot)
{
	ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>();

	if (GM)
	{
		KillerName = GM->GetDisplayNameFor(Killer);
	}

	bDowned = false;
	Die();

	if (GM)
	{
		GM->OnOperatorKilled(this, Killer, bHeadshot);
	}
}

void ASiegeOperator::EnterDowned(AController* Downer)
{
	if (bRappelling)
	{
		StopRappel(false);
	}

	bDowned = true;
	DownedBy = Downer;
	CurrentHP = DownedHealth;

	StopInteract();
	DoStopFiring();
	bAimHeld = false;
	bSprintHeld = false;
	SetLean(0.0f);

	// can't fight while down
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->DeactivateWeapon();
	}

	Crouch();
	OnDamaged.Broadcast(FMath::Max(0.0f, CurrentHP / MaxHP));

	if (ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>())
	{
		GM->OnOperatorDowned(this, Downer);
	}
}

void ASiegeOperator::Revive(ASiegeOperator* Reviver)
{
	if (!bDowned || IsDead())
	{
		return;
	}

	bDowned = false;
	DownedBy.Reset();
	CurrentHP = ReviveHealth;
	OnDamaged.Broadcast(FMath::Max(0.0f, CurrentHP / MaxHP));

	UnCrouch();

	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->ActivateWeapon(PlayerTag);
	}

	if (ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>())
	{
		GM->PostTeamMessage(GetTeam(), FString::Printf(TEXT("%s revived %s"), Reviver ? *Reviver->GetDisplayName() : TEXT("Someone"), *DisplayName));
	}
}

ASiegeOperator* ASiegeOperator::FindDownedTeammate(float Range) const
{
	ASiegeOperator* Best = nullptr;
	float BestDist = Range;

	for (TActorIterator<ASiegeOperator> It(GetWorld()); It; ++It)
	{
		ASiegeOperator* Other = *It;
		if (Other != this && Other->GetTeam() == GetTeam() && Other->IsDowned() && !Other->IsDead())
		{
			const float Dist = FVector::Dist(Other->GetActorLocation(), GetActorLocation());
			if (Dist < BestDist)
			{
				Best = Other;
				BestDist = Dist;
			}
		}
	}

	return Best;
}

void ASiegeOperator::Die()
{
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->DeactivateWeapon();
	}

	StopInteract();
	bRappelling = false;

	Tags.Add(DeathTag);

	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// ragdoll the body; it stays until the next round
	GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->SetOwnerNoSee(false);
	BodyMesh->SetOwnerNoSee(false);
	FirstPersonBodyMesh->SetHiddenInGame(true);
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
	if (!GM || IsDead() || bDowned)
	{
		return ESiegeInteraction::None;
	}

	const ESiegePhase Phase = GM->GetPhase();
	const bool bAttacker = GM->IsAttackingTeam(GetTeam());

	// disabling the device beats everything; reviving beats everything else
	const ASiegeDevice* ActiveDevice = GM->GetActiveDevice();
	const bool bCanDisable = !bAttacker && ActiveDevice && FVector::Dist(ActiveDevice->GetActorLocation(), GetActorLocation()) < InteractRange;

	if (!bCanDisable && FindDownedTeammate(150.0f))
	{
		return ESiegeInteraction::Revive;
	}

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
	case ESiegeInteraction::Revive:
	{
		const ASiegeOperator* Downed = FindDownedTeammate(150.0f);
		return FString::Printf(TEXT("Hold [F] to revive %s"), Downed ? *Downed->GetDisplayName() : TEXT("teammate"));
	}
	default:								return FString();
	}
}

void ASiegeOperator::StartInteract()
{
	if (IsDead() || bDowned)
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
	ReviveTarget.Reset();
}

float ASiegeOperator::GetInteractProgress01() const
{
	float Duration = 1.0f;

	switch (CurrentInteraction)
	{
	case ESiegeInteraction::PlantDevice:	Duration = PlantTime; break;
	case ESiegeInteraction::DisableDevice:	Duration = DisableTime; break;
	case ESiegeInteraction::Reinforce:		Duration = ReinforceTime; break;
	case ESiegeInteraction::Revive:			Duration = ReviveTime; break;
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
	ASiegeOperator* Downed = Available == ESiegeInteraction::Revive ? FindDownedTeammate(150.0f) : nullptr;

	// restart if what we're doing changed (walked off site, looked at a different wall...)
	if (Available != CurrentInteraction
		|| (Available == ESiegeInteraction::Reinforce && Wall != InteractWall.Get())
		|| (Available == ESiegeInteraction::Revive && Downed != ReviveTarget.Get()))
	{
		CurrentInteraction = Available;
		InteractWall = Wall;
		ReviveTarget = Downed;
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
	ASiegeOperator* RevivedTeammate = ReviveTarget.Get();

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

	case ESiegeInteraction::Revive:
		if (RevivedTeammate)
		{
			RevivedTeammate->Revive(this);
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
	if (IsDead() || bDowned || !GM || Gadget == ESiegeGadget::None)
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
	if (IsDead() || bDowned || IsInteracting() || Now - LastMeleeTime < MeleeCooldown)
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

	USiegeSoundSubsystem::Play(this, ESiegeSound::Melee, Hit.ImpactPoint, 0.8f, this, GetTeam());

	if (ASiegeWall* Wall = Cast<ASiegeWall>(HitActor))
	{
		// melee smashes a chunk out of barricades and a small hole in soft walls
		Wall->ApplyExplosion(Hit.ImpactPoint, (Wall->IsBarricade() || Wall->IsHatch()) ? 55.0f : 20.0f, false);
		USiegeSoundSubsystem::Play(this, ESiegeSound::Crunch, Hit.ImpactPoint, 1.0f, this, GetTeam());
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

// ---------------------------------------------------------------------------------------------------------------------
// Rappelling

ASiegeRopeAnchor* ASiegeOperator::FindAttachableRope(float& OutAlong, float& OutHeight) const
{
	const ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>();
	if (!GM || IsDead() || bDowned || !GM->IsAttackingTeam(GetTeam()) || !GM->CanTeamMove(GetTeam()))
	{
		return nullptr;
	}

	const FVector Feet = GetActorLocation() - FVector(0.0f, 0.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());

	for (TActorIterator<ASiegeRopeAnchor> It(GetWorld()); It; ++It)
	{
		if (It->CanAttach(Feet, OutAlong, OutHeight))
		{
			return *It;
		}
	}

	return nullptr;
}

void ASiegeOperator::ToggleRappel()
{
	if (bRappelling)
	{
		StopRappel();
		return;
	}

	float Along = 0.0f;
	float Height = 0.0f;
	if (ASiegeRopeAnchor* Anchor = FindAttachableRope(Along, Height))
	{
		StartRappel(Anchor, Along, Height);
	}
}

void ASiegeOperator::StartRappel(ASiegeRopeAnchor* Anchor, float Along, float Height)
{
	if (!Anchor)
	{
		return;
	}

	StopInteract();

	if (bIsCrouched)
	{
		UnCrouch();
	}

	bSprintHeld = false;
	bRappelling = true;
	RappelAnchor = Anchor;
	RappelAlong = Along;
	RappelHeight = Height;

	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->SetMovementMode(MOVE_Flying);

	// turn to face the wall
	if (AController* C = GetController())
	{
		C->SetControlRotation(FRotator(0.0f, (-Anchor->GetOutward()).Rotation().Yaw, 0.0f));
	}

	USiegeSoundSubsystem::Play(this, ESiegeSound::Rope, GetActorLocation(), 1.0f, this, GetTeam());
}

void ASiegeOperator::StopRappel(bool bPlaySound)
{
	if (!bRappelling)
	{
		return;
	}

	bRappelling = false;
	RappelAnchor.Reset();
	GetCharacterMovement()->SetMovementMode(MOVE_Falling);

	if (bPlaySound)
	{
		USiegeSoundSubsystem::Play(this, ESiegeSound::Rope, GetActorLocation(), 0.7f, this, GetTeam());
	}
}

void ASiegeOperator::UpdateRappel(float DeltaSeconds)
{
	ASiegeRopeAnchor* Anchor = RappelAnchor.Get();
	if (!Anchor)
	{
		StopRappel(false);
		return;
	}

	// W/S climb, A/D slide along the facade
	RappelHeight = FMath::Clamp(RappelHeight + ForwardInput * RappelClimbSpeed * DeltaSeconds, 0.0f, Anchor->Height - 200.0f);
	RappelAlong = FMath::Clamp(RappelAlong + RightInput * RappelSideSpeed * DeltaSeconds, -Anchor->HalfWidth, Anchor->HalfWidth);

	// climbing down to the ground lets go
	if (RappelHeight <= 0.0f && ForwardInput < -0.1f)
	{
		StopRappel();
		return;
	}

	const FVector Target = Anchor->GetHangLocation(RappelAlong, RappelHeight, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	SetActorLocation(Target, true);
	GetCharacterMovement()->Velocity = FVector::ZeroVector;

	// move inputs were consumed; they're re-sent every frame while held
	ForwardInput = 0.0f;
	RightInput = 0.0f;
}

bool ASiegeOperator::TryRappelEntry()
{
	ASiegeRopeAnchor* Anchor = RappelAnchor.Get();
	if (!Anchor)
	{
		return false;
	}

	FHitResult Hit;
	ASiegeWall* Window = TraceForWall(160.0f, Hit);

	if (!Window || !Window->IsBarricade())
	{
		if (ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>())
		{
			GM->PostPlayerMessage(this, TEXT("Line up with a window barricade to crash through"));
		}
		return false;
	}

	// smash the barricade and swing in, crouched to fit through the frame
	Window->DestroyAllCells();
	USiegeSoundSubsystem::Play(this, ESiegeSound::Crunch, Hit.ImpactPoint, 1.4f, this, GetTeam());

	const FVector Inside = Window->GetActorLocation() - Anchor->GetOutward() * 90.0f;

	StopRappel(false);
	Crouch();

	FVector Landing = Inside;
	Landing.Z = Hit.ImpactPoint.Z;
	SetActorLocation(Landing, false, nullptr, ETeleportType::TeleportPhysics);
	return true;
}

FString ASiegeOperator::GetRappelPrompt() const
{
	if (bRappelling)
	{
		return TEXT("[W/S] climb   [A/D] slide   [Space] crash through window   [T] let go");
	}

	float Along = 0.0f;
	float Height = 0.0f;
	return FindAttachableRope(Along, Height) ? TEXT("Press [T] to rappel") : FString();
}
