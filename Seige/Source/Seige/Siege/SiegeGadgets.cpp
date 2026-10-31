// Copyright Epic Games, Inc. All Rights Reserved.

#include "SiegeGadgets.h"
#include "SiegeSound.h"
#include "Seige.h"
#include "SiegeWall.h"
#include "SiegeOperator.h"
#include "SiegeGameMode.h"
#include "SiegeObservation.h"
#include "SiegeTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace SiegeExplosion
{
	/** Returns true if solid structural geometry blocks the line between two points */
	static bool IsBlockedBySolid(UWorld* World, const FVector& From, const FVector& To)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SiegeExplosion), false);

		// walk through non-solid blockers (destructible panels, pawns) until we reach the target or hit solid structure
		for (int32 Attempt = 0; Attempt < 6; ++Attempt)
		{
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params))
			{
				return false;
			}

			AActor* HitActor = Hit.GetActor();
			if (!HitActor)
			{
				return true;
			}

			if (HitActor->ActorHasTag(SiegeTags::Solid))
			{
				return true;
			}

			Params.AddIgnoredActor(HitActor);
		}

		return false;
	}

	void Explode(UWorld* World, const FVector& Location, float WallRadius, float DamageRadius, float MaxDamage, AController* Instigator, AActor* Causer, ASiegeWall* HardBreachWall)
	{
		if (!World)
		{
			return;
		}

		// open destructible surfaces
		if (IsValid(HardBreachWall))
		{
			HardBreachWall->DestroyAllCells();
		}

		for (TActorIterator<ASiegeWall> It(World); It; ++It)
		{
			ASiegeWall* Wall = *It;
			if (Wall != HardBreachWall && FVector::Dist(Wall->GetActorLocation(), Location) < WallRadius + 400.0f)
			{
				Wall->ApplyExplosion(Location, WallRadius, false);
			}
		}

		// damage operators
		for (TActorIterator<ASiegeOperator> It(World); It; ++It)
		{
			ASiegeOperator* Op = *It;
			if (Op->IsDead())
			{
				continue;
			}

			const FVector Target = Op->GetActorLocation();
			const float Dist = FVector::Dist(Target, Location);

			if (Dist < DamageRadius && !IsBlockedBySolid(World, Location, Target))
			{
				const float Damage = MaxDamage * FMath::Square(1.0f - Dist / DamageRadius);
				Op->TakeDamage(Damage, FDamageEvent(), Instigator, Causer);
			}
		}

		USiegeSoundSubsystem::Play(World, ESiegeSound::Explosion, Location, MaxDamage >= 300.0f ? 1.5f : 1.0f, Causer);

		// drones and cameras caught in the blast
		TArray<APawn*> Devices;
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			if (Cast<ISiegeObserverDevice>(*It) && FVector::Dist(It->GetActorLocation(), Location) < WallRadius + 100.0f)
			{
				Devices.Add(*It);
			}
		}

		for (APawn* Device : Devices)
		{
			if (IsValid(Device))
			{
				Device->TakeDamage(1.0f, FDamageEvent(), Instigator, Causer);
			}
		}

		// destroy gadgets caught in the blast
		TArray<ASiegeGadgetBase*> Caught;
		for (TActorIterator<ASiegeGadgetBase> It(World); It; ++It)
		{
			if (*It != Causer && FVector::Dist(It->GetActorLocation(), Location) < WallRadius + 50.0f)
			{
				Caught.Add(*It);
			}
		}

		for (ASiegeGadgetBase* Gadget : Caught)
		{
			if (IsValid(Gadget))
			{
				Gadget->OnCaughtInExplosion(Causer);
			}
		}
	}
}

ASiegeGadgetBase::ASiegeGadgetBase()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	Mesh->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CubeMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GadgetMat(TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_TopDark.MI_PrototypeGrid_TopDark"));
	if (GadgetMat.Succeeded())
	{
		Mesh->SetMaterial(0, GadgetMat.Object);
	}

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Mesh);
	Light->SetIntensity(800.0f);
	Light->SetAttenuationRadius(150.0f);
	Light->SetCastShadows(false);
}

void ASiegeGadgetBase::InitGadget(ASiegeOperator* InOwner, ASiegeWall* InWall)
{
	OwnerOperator = InOwner;
	OwnerTeam = InOwner ? InOwner->GetTeam() : 0;
	AttachedWall = InWall;

	// every deployed gadget gives itself away with a beep
	USiegeSoundSubsystem::Play(this, ESiegeSound::Beep, GetActorLocation(), 1.0f, this, OwnerTeam);
}

float ASiegeGadgetBase::TakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!bVulnerableToBullets || Damage <= 0.0f)
	{
		return 0.0f;
	}

	Health -= Damage;

	if (Health <= 0.0f)
	{
		if (ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>())
		{
			GM->PostTeamMessage(OwnerTeam, FString::Printf(TEXT("Your %s was destroyed"), *GadgetName));
		}

		Destroy();
	}

	return Damage;
}

void ASiegeGadgetBase::OnCaughtInExplosion(AActor* Causer)
{
	Destroy();
}

void ASiegeGadgetBase::SetLightColor(const FLinearColor& Color, float Intensity)
{
	Light->SetLightColor(Color);
	Light->SetIntensity(Intensity);
}

ASiegeBreachCharge::ASiegeBreachCharge()
{
	PrimaryActorTick.bCanEverTick = true;

	GadgetName = TEXT("breach charge");
	Mesh->SetRelativeScale3D(FVector(0.06f, 0.5f, 0.7f));
	Mesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetLightColor(FLinearColor(1.0f, 0.8f, 0.1f), 600.0f);
}

void ASiegeBreachCharge::SetHardBreach(bool bHard)
{
	bHardBreach = bHard;

	if (bHardBreach)
	{
		Mesh->SetRelativeScale3D(FVector(0.08f, 0.9f, 1.4f));
		SetLightColor(FLinearColor(1.0f, 0.35f, 0.05f), 1500.0f);
	}
}

void ASiegeBreachCharge::InitGadget(ASiegeOperator* InOwner, ASiegeWall* InWall)
{
	Super::InitGadget(InOwner, InWall);

	if (InWall && InWall->IsElectrified())
	{
		Fizzle(TEXT("Charge destroyed: the wall is electrified"));
		return;
	}

	if (bHardBreach)
	{
		USiegeSoundSubsystem::Play(this, ESiegeSound::ThermalHiss, GetActorLocation(), 1.0f, this, OwnerTeam);
	}
}

void ASiegeBreachCharge::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bExploded || IsActorBeingDestroyed())
	{
		return;
	}

	// a panel can be electrified after the charge was placed
	if (AttachedWall.IsValid() && AttachedWall->IsElectrified())
	{
		Fizzle(TEXT("Charge destroyed: the wall is electrified"));
		return;
	}

	if (bHardBreach)
	{
		BurnElapsed += DeltaSeconds;

		// flicker while burning
		Light->SetIntensity(800.0f + FMath::FRand() * 2500.0f);

		if (BurnElapsed >= BurnTime)
		{
			Detonate();
		}
	}
}

void ASiegeBreachCharge::Detonate()
{
	if (bExploded)
	{
		return;
	}

	bExploded = true;

	UE_LOG(LogSeige, Log, TEXT("Breach: %s detonated a %s charge on %s (reinforced: %d)"), OwnerOperator.IsValid() ? *OwnerOperator->GetDisplayName() : TEXT("?"),
		bHardBreach ? TEXT("thermal") : TEXT("breach"), AttachedWall.IsValid() ? *AttachedWall->GetRoomLabel() : TEXT("floor/unknown"), AttachedWall.IsValid() ? AttachedWall->IsReinforced() : 0);

	AController* InstigatorController = OwnerOperator.IsValid() ? OwnerOperator->GetOwningController() : nullptr;

	// a little in front of the panel so the blast reaches both sides
	const FVector BlastLocation = GetActorLocation();

	if (bHardBreach)
	{
		SiegeExplosion::Explode(GetWorld(), BlastLocation, 60.0f, 250.0f, 40.0f, InstigatorController, this, AttachedWall.Get());
	}
	else
	{
		SiegeExplosion::Explode(GetWorld(), BlastLocation, 110.0f, 350.0f, 90.0f, InstigatorController, this);
	}

	if (OwnerOperator.IsValid())
	{
		OwnerOperator->OnChargeGone(this);
	}

	Destroy();
}

void ASiegeBreachCharge::Fizzle(const FString& Reason)
{
	bExploded = true;

	if (ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>())
	{
		GM->PostTeamMessage(OwnerTeam, Reason);
	}

	if (OwnerOperator.IsValid())
	{
		OwnerOperator->OnChargeGone(this);
	}

	Destroy();
}

ASiegeShockBox::ASiegeShockBox()
{
	GadgetName = TEXT("shock box");
	Mesh->SetRelativeScale3D(FVector(0.15f, 0.25f, 0.25f));
	Health = 20.0f;
	SetLightColor(FLinearColor(0.2f, 0.6f, 1.0f), 1500.0f);
}

void ASiegeShockBox::InitGadget(ASiegeOperator* InOwner, ASiegeWall* InWall)
{
	Super::InitGadget(InOwner, InWall);

	if (InWall)
	{
		InWall->AddElectrifier();
	}
}

void ASiegeShockBox::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AttachedWall.IsValid())
	{
		AttachedWall->RemoveElectrifier();
	}

	Super::EndPlay(EndPlayReason);
}

ASiegeShield::ASiegeShield()
{
	GadgetName = TEXT("shield");
	Mesh->SetRelativeScale3D(FVector(0.12f, 1.1f, 1.15f));
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Mesh->SetCanEverAffectNavigation(true);
	bVulnerableToBullets = false;
	SetLightColor(FLinearColor(0.3f, 1.0f, 0.4f), 200.0f);
}

ASiegeAlarmSensor::ASiegeAlarmSensor()
{
	PrimaryActorTick.bCanEverTick = true;

	GadgetName = TEXT("alarm sensor");
	Mesh->SetRelativeScale3D(FVector(0.2f, 0.2f, 0.08f));
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	Health = 1.0f;
	SetLightColor(FLinearColor(1.0f, 0.1f, 0.6f), 300.0f);
}

void ASiegeAlarmSensor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextCheckTime || Now - LastTriggerTime < Cooldown)
	{
		return;
	}

	NextCheckTime = Now + 0.25f;

	bool bTriggered = false;

	for (TActorIterator<ASiegeOperator> It(GetWorld()); It; ++It)
	{
		ASiegeOperator* Op = *It;
		if (!Op->IsDead() && Op->GetTeam() != OwnerTeam && FVector::Dist(Op->GetActorLocation(), GetActorLocation()) < Radius)
		{
			Op->RevealTo(OwnerTeam, RevealDuration);
			bTriggered = true;
		}
	}

	if (bTriggered)
	{
		LastTriggerTime = Now;
		SetLightColor(FLinearColor(1.0f, 0.0f, 0.0f), 3000.0f);

		if (ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>())
		{
			GM->PostTeamMessage(OwnerTeam, TEXT("Alarm sensor triggered"));
		}
	}
	else if (Now - LastTriggerTime > RevealDuration)
	{
		SetLightColor(FLinearColor(1.0f, 0.1f, 0.6f), 300.0f);
	}
}
