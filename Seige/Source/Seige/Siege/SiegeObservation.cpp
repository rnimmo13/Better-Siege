// Copyright Epic Games, Inc. All Rights Reserved.

#include "SiegeObservation.h"
#include "SiegeOperator.h"
#include "SiegeGameMode.h"
#include "SiegePlayerController.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace SiegeObservation
{
	int32 SpotEnemies(UWorld* World, uint8 ViewerTeam, const FVector& Eye, const FVector& Forward, float HalfAngleDegrees, float Range, float RevealDuration, AActor* IgnoreActor, FString& OutFirstName)
	{
		if (!World)
		{
			return 0;
		}

		const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(HalfAngleDegrees));
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SiegeSpot), true, IgnoreActor);

		int32 Count = 0;

		for (TActorIterator<ASiegeOperator> It(World); It; ++It)
		{
			ASiegeOperator* Op = *It;
			if (Op->IsDead() || Op->GetTeam() == ViewerTeam)
			{
				continue;
			}

			for (const FVector& Point : { Op->GetHeadLocation(), Op->GetChestLocation() })
			{
				const FVector ToPoint = Point - Eye;
				const float Dist = ToPoint.Size();

				if (Dist > Range || FVector::DotProduct(ToPoint / FMath::Max(Dist, 1.0f), Forward) < CosHalfAngle)
				{
					continue;
				}

				FHitResult Hit;
				const bool bBlocked = World->LineTraceSingleByChannel(Hit, Eye, Point, ECC_Visibility, Params) && Hit.GetActor() != Op;
				if (!bBlocked)
				{
					Op->RevealTo(ViewerTeam, RevealDuration);

					if (Count == 0)
					{
						OutFirstName = Op->GetDisplayName();
					}

					++Count;
					break;
				}
			}
		}

		return Count;
	}

	TArray<APawn*> GetTeamDevices(UWorld* World, uint8 Team)
	{
		TArray<APawn*> Result;

		for (TActorIterator<APawn> It(World); It; ++It)
		{
			if (ISiegeObserverDevice* Device = Cast<ISiegeObserverDevice>(*It))
			{
				if (Device->IsDeviceAlive() && Device->GetDeviceTeam() == Team)
				{
					Result.Add(*It);
				}
			}
		}

		Result.Sort([](const APawn& A, const APawn& B) { return A.GetName() < B.GetName(); });
		return Result;
	}

	static void LoadCommonActions(TObjectPtr<UInputAction>& Move, TObjectPtr<UInputAction>& Look, TObjectPtr<UInputAction>& MouseLook, TObjectPtr<UInputAction>& Jump, TObjectPtr<UInputAction>& Spot)
	{
		Move = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Move.IA_Move"));
		Look = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Look.IA_Look"));
		MouseLook = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_MouseLook.IA_MouseLook"));
		Jump = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Jump.IA_Jump"));
		Spot = LoadObject<UInputAction>(nullptr, TEXT("/Game/Variant_Shooter/Input/Actions/IA_Shoot.IA_Shoot"));
	}

	/** Binds the cycle and exit keys shared by every device to its owning player controller */
	template<typename DeviceType>
	static void BindDeviceKeys(DeviceType* Device, UEnhancedInputComponent* EIC, ASiegePlayerController* PC, void (DeviceType::*Prev)(), void (DeviceType::*Next)(), void (DeviceType::*Exit)())
	{
		EIC->BindAction(PC->GetSiegeAction(TEXT("LeanLeft")), ETriggerEvent::Started, Device, Prev);
		EIC->BindAction(PC->GetSiegeAction(TEXT("LeanRight")), ETriggerEvent::Started, Device, Next);
		EIC->BindAction(PC->GetSiegeAction(TEXT("Observe")), ETriggerEvent::Started, Device, Exit);
	}

	static void NotifyDestroyed(APawn* Device)
	{
		if (ASiegePlayerController* PC = Cast<ASiegePlayerController>(Device->GetController()))
		{
			PC->OnDeviceDestroyed(Device);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Drone

ASiegeDrone::ASiegeDrone()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessAI = EAutoPossessAI::Disabled;

	GetCapsuleComponent()->InitCapsuleSize(11.0f, 11.0f);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->MaxWalkSpeed = 420.0f;
	Move->JumpZVelocity = 320.0f;
	Move->BrakingDecelerationWalking = 1200.0f;
	Move->GroundFriction = 6.0f;
	Move->bOrientRotationToMovement = false;
	Move->MaxStepHeight = 8.0f;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(GetCapsuleComponent());
	Body->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Body->SetRelativeScale3D(FVector(0.24f, 0.2f, 0.1f));
	Body->SetRelativeLocation(FVector(0.0f, 0.0f, -4.0f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Body->SetStaticMesh(CubeMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BodyMat(TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_TopDark.MI_PrototypeGrid_TopDark"));
	if (BodyMat.Succeeded())
	{
		Body->SetMaterial(0, BodyMat.Object);
	}

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(12.0f, 0.0f, 4.0f));
	Camera->bUsePawnControlRotation = true;
	Camera->SetFieldOfView(100.0f);

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(GetCapsuleComponent());
	Light->SetRelativeLocation(FVector(-10.0f, 0.0f, 6.0f));
	Light->SetIntensity(250.0f);
	Light->SetAttenuationRadius(60.0f);
	Light->SetCastShadows(false);
	Light->SetLightColor(FLinearColor(1.0f, 0.2f, 0.1f));

	// dim headlight so tunnels and vents aren't pitch black
	Headlight = CreateDefaultSubobject<UPointLightComponent>(TEXT("Headlight"));
	Headlight->SetupAttachment(Camera);
	Headlight->SetRelativeLocation(FVector(40.0f, 0.0f, 0.0f));
	Headlight->SetIntensity(900.0f);
	Headlight->SetAttenuationRadius(320.0f);
	Headlight->SetCastShadows(false);
	Headlight->SetLightColor(FLinearColor(0.85f, 0.9f, 1.0f));
}

void ASiegeDrone::InitDrone(ASiegeOperator* InOwner, uint8 InTeam, const FString& InName)
{
	OwnerOperator = InOwner;
	Team = InTeam;
	DeviceName = InName;
}

void ASiegeDrone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bDestroyed)
	{
		return;
	}

	// parked or driven, a drone keeps an eye on what's in front of it
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextPassiveSpotTime)
	{
		NextPassiveSpotTime = Now + 2.5f;

		FString Name;
		SiegeObservation::SpotEnemies(GetWorld(), Team, Camera->GetComponentLocation(), Camera->GetForwardVector(), 30.0f, 2000.0f, 2.5f, this, Name);
	}
}

void ASiegeDrone::Spot()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (bDestroyed || Now - LastManualSpotTime < 1.0f)
	{
		return;
	}

	LastManualSpotTime = Now;

	FString Name;
	const int32 Count = SiegeObservation::SpotEnemies(GetWorld(), Team, Camera->GetComponentLocation(), Camera->GetForwardVector(), 10.0f, 3500.0f, 5.0f, this, Name);

	if (Count > 0)
	{
		if (ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>())
		{
			GM->PostTeamMessage(Team, FString::Printf(TEXT("%s spotted %s"), *DeviceName, *Name));
		}
	}
}

float ASiegeDrone::TakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (bDestroyed || Damage <= 0.0f)
	{
		return 0.0f;
	}

	bDestroyed = true;

	if (ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>())
	{
		GM->PostTeamMessage(Team, FString::Printf(TEXT("%s was destroyed"), *DeviceName));
	}

	SiegeObservation::NotifyDestroyed(this);
	Destroy();
	return Damage;
}

void ASiegeDrone::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	ASiegePlayerController* PC = Cast<ASiegePlayerController>(GetController());
	if (!EIC || !PC)
	{
		return;
	}

	SiegeObservation::LoadCommonActions(MoveAction, LookAction, MouseLookAction, JumpAction, SpotAction);

	EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ASiegeDrone::OnMove);
	EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &ASiegeDrone::OnLook);
	EIC->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ASiegeDrone::OnLook);
	EIC->BindAction(JumpAction, ETriggerEvent::Started, this, &ASiegeDrone::OnJump);
	EIC->BindAction(SpotAction, ETriggerEvent::Started, this, &ASiegeDrone::Spot);

	SiegeObservation::BindDeviceKeys(this, EIC, PC, &ASiegeDrone::OnCyclePrev, &ASiegeDrone::OnCycleNext, &ASiegeDrone::OnExit);
}

void ASiegeDrone::OnMove(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddMovementInput(GetActorForwardVector(), Axis.Y);
	AddMovementInput(GetActorRightVector(), Axis.X);
}

void ASiegeDrone::OnLook(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
}

void ASiegeDrone::OnJump()
{
	Jump();
}

void ASiegeDrone::OnCyclePrev()
{
	if (ASiegePlayerController* PC = Cast<ASiegePlayerController>(GetController()))
	{
		PC->CycleObservation(-1);
	}
}

void ASiegeDrone::OnCycleNext()
{
	if (ASiegePlayerController* PC = Cast<ASiegePlayerController>(GetController()))
	{
		PC->CycleObservation(1);
	}
}

void ASiegeDrone::OnExit()
{
	if (ASiegePlayerController* PC = Cast<ASiegePlayerController>(GetController()))
	{
		PC->ExitObservation();
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Security camera

ASiegeSecurityCamera::ASiegeSecurityCamera()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessAI = EAutoPossessAI::Disabled;

	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	RootComponent = Collision;
	Collision->SetBoxExtent(FVector(10.0f, 8.0f, 8.0f));
	Collision->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	Collision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Collision->SetCanEverAffectNavigation(false);

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Collision);
	Body->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Body->SetRelativeScale3D(FVector(0.2f, 0.16f, 0.16f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Body->SetStaticMesh(CubeMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BodyMat(TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_TopDark.MI_PrototypeGrid_TopDark"));
	if (BodyMat.Succeeded())
	{
		Body->SetMaterial(0, BodyMat.Object);
	}

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Collision);
	Camera->SetRelativeLocation(FVector(14.0f, 0.0f, 0.0f));
	Camera->bUsePawnControlRotation = true;
	Camera->SetFieldOfView(95.0f);

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Collision);
	Light->SetRelativeLocation(FVector(12.0f, 0.0f, 8.0f));
	Light->SetIntensity(300.0f);
	Light->SetAttenuationRadius(60.0f);
	Light->SetCastShadows(false);
	Light->SetLightColor(FLinearColor(1.0f, 0.0f, 0.0f));
}

void ASiegeSecurityCamera::BeginPlay()
{
	Super::BeginPlay();

	BaseRotation = GetActorRotation();
}

void ASiegeSecurityCamera::InitCamera(const FString& InName, uint8 InTeam)
{
	DeviceName = InName;
	Team = InTeam;
}

void ASiegeSecurityCamera::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// start looking where the camera points
	if (NewController)
	{
		NewController->SetControlRotation(BaseRotation);
	}
}

void ASiegeSecurityCamera::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bDestroyed)
	{
		return;
	}

	// clamp look around the mounting direction
	if (AController* C = GetController())
	{
		FRotator Rot = C->GetControlRotation();
		const float DeltaYaw = FMath::Clamp(FRotator::NormalizeAxis(Rot.Yaw - BaseRotation.Yaw), -75.0f, 75.0f);
		const float Pitch = FMath::Clamp(FRotator::NormalizeAxis(Rot.Pitch), -65.0f, 15.0f);
		C->SetControlRotation(FRotator(Pitch, BaseRotation.Yaw + DeltaYaw, 0.0f));
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextPassiveSpotTime)
	{
		NextPassiveSpotTime = Now + 2.5f;

		FString Name;
		SiegeObservation::SpotEnemies(GetWorld(), Team, Camera->GetComponentLocation(), Camera->GetForwardVector(), 35.0f, 2500.0f, 2.5f, this, Name);
	}
}

void ASiegeSecurityCamera::Spot()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (bDestroyed || Now - LastManualSpotTime < 1.0f)
	{
		return;
	}

	LastManualSpotTime = Now;

	FString Name;
	const int32 Count = SiegeObservation::SpotEnemies(GetWorld(), Team, Camera->GetComponentLocation(), Camera->GetForwardVector(), 10.0f, 4000.0f, 5.0f, this, Name);

	if (Count > 0)
	{
		if (ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>())
		{
			GM->PostTeamMessage(Team, FString::Printf(TEXT("%s spotted %s"), *DeviceName, *Name));
		}
	}
}

float ASiegeSecurityCamera::TakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (bDestroyed || Damage <= 0.0f)
	{
		return 0.0f;
	}

	bDestroyed = true;

	if (ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>())
	{
		GM->PostTeamMessage(Team, FString::Printf(TEXT("%s was destroyed"), *DeviceName));
	}

	SiegeObservation::NotifyDestroyed(this);
	Destroy();
	return Damage;
}

void ASiegeSecurityCamera::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	ASiegePlayerController* PC = Cast<ASiegePlayerController>(GetController());
	if (!EIC || !PC)
	{
		return;
	}

	TObjectPtr<UInputAction> Unused;
	TObjectPtr<UInputAction> UnusedJump;
	SiegeObservation::LoadCommonActions(Unused, LookAction, MouseLookAction, UnusedJump, SpotAction);

	EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &ASiegeSecurityCamera::OnLook);
	EIC->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ASiegeSecurityCamera::OnLook);
	EIC->BindAction(SpotAction, ETriggerEvent::Started, this, &ASiegeSecurityCamera::Spot);

	SiegeObservation::BindDeviceKeys(this, EIC, PC, &ASiegeSecurityCamera::OnCyclePrev, &ASiegeSecurityCamera::OnCycleNext, &ASiegeSecurityCamera::OnExit);
}

void ASiegeSecurityCamera::OnLook(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
}

void ASiegeSecurityCamera::OnCyclePrev()
{
	if (ASiegePlayerController* PC = Cast<ASiegePlayerController>(GetController()))
	{
		PC->CycleObservation(-1);
	}
}

void ASiegeSecurityCamera::OnCycleNext()
{
	if (ASiegePlayerController* PC = Cast<ASiegePlayerController>(GetController()))
	{
		PC->CycleObservation(1);
	}
}

void ASiegeSecurityCamera::OnExit()
{
	if (ASiegePlayerController* PC = Cast<ASiegePlayerController>(GetController()))
	{
		PC->ExitObservation();
	}
}
