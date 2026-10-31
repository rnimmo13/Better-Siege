// Copyright Epic Games, Inc. All Rights Reserved.

#include "SiegeGun.h"
#include "SiegeSound.h"
#include "Seige.h"
#include "SiegeOperator.h"
#include "SiegeWall.h"
#include "SiegeGadgets.h"
#include "ShooterWeaponHolder.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "GameFramework/DamageType.h"

ASiegeGun::ASiegeGun()
{
	// the template auto-refills magazines; we track ammo ourselves
	MagazineSize = 30;
}

void ASiegeGun::BeginPlay()
{
	Super::BeginPlay();

	ReserveAmmo = MagazineSize * SpareMagazines;
}

void ASiegeGun::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	GetWorld()->GetTimerManager().ClearTimer(ReloadTimer);

	Super::EndPlay(EndPlayReason);
}

void ASiegeGun::Fire()
{
	ASiegeOperator* Op = Cast<ASiegeOperator>(GetOwner());

	if (!bIsFiring || bReloading || (Op && !Op->CanFireWeapon()))
	{
		return;
	}

	if (CurrentBullets <= 0)
	{
		Reload();
		return;
	}

	if (Op)
	{
		Op->OnWeaponFired();
	}

	Super::Fire();
}

void ASiegeGun::FireProjectile(const FVector& TargetLocation)
{
	ASiegeOperator* Op = Cast<ASiegeOperator>(GetOwner());

	const FVector Start = Op ? Op->GetAimOrigin() : GetActorLocation();
	FVector Direction = (TargetLocation - Start).GetSafeNormal();

	// apply spread
	const float Spread = SpreadDegrees * (Op ? Op->GetSpreadMultiplier() : 1.0f);
	Direction = FMath::VRandCone(Direction, FMath::DegreesToRadians(Spread));

	const FVector End = Start + Direction * Range;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(SiegeGunTrace), true);
	Params.AddIgnoredActor(GetOwner());
	Params.AddIgnoredActor(this);
	Params.bReturnPhysicalMaterial = false;

	AController* InstigatorController = PawnOwner ? PawnOwner->GetController() : nullptr;

	float DamageMultiplier = 1.0f;
	FVector TracerEnd = End;

	for (int32 Penetration = 0; Penetration <= MaxPenetrations; ++Penetration)
	{
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			break;
		}

		AActor* HitActor = Hit.GetActor();
		TracerEnd = Hit.ImpactPoint;

		if (!HitActor)
		{
			break;
		}

		const float HitDamage = GetDamageAtDistance(Hit.Distance) * DamageMultiplier;

		UE_LOG(LogSeige, Verbose, TEXT("Shot by %s hit %s / %s bone=%s dmg=%.1f"), *GetNameSafe(GetOwner()), *GetNameSafe(HitActor), *GetNameSafe(Hit.GetComponent()), *Hit.BoneName.ToString(), HitDamage);

		if (ASiegeWall* Wall = Cast<ASiegeWall>(HitActor))
		{
			UGameplayStatics::ApplyPointDamage(Wall, HitDamage, Direction, Hit, InstigatorController, this, UDamageType::StaticClass());

			const float Factor = Wall->GetPenetrationFactor();
			if (Factor <= 0.0f)
			{
				break;
			}

			// keep going through the panel with reduced damage
			DamageMultiplier *= Factor;
			Params.AddIgnoredActor(Wall);
			continue;
		}

		// operators, gadgets and anything else that can take damage stop the bullet
		UGameplayStatics::ApplyPointDamage(HitActor, HitDamage, Direction, Hit, InstigatorController, this, UDamageType::StaticClass());

		if (Op && Cast<ASiegeOperator>(HitActor))
		{
			Op->OnHitConfirmed(Cast<ASiegeOperator>(HitActor));
		}

		break;
	}

	if (bDrawTracers)
	{
		const FVector MuzzleLoc = GetFirstPersonMesh()->DoesSocketExist(MuzzleSocketName) ? GetFirstPersonMesh()->GetSocketLocation(MuzzleSocketName) : Start;
		DrawDebugLine(GetWorld(), MuzzleLoc, TracerEnd, FColor(255, 220, 120), false, 0.04f, 0, 0.6f);
		DrawDebugPoint(GetWorld(), TracerEnd, 4.0f, FColor(255, 160, 60), false, 0.5f);
	}

	USiegeSoundSubsystem::Play(this, MagazineSize <= 15 ? ESiegeSound::PistolShot : ESiegeSound::RifleShot, Start, 1.0f, GetOwner(), Op ? Op->GetTeam() : 255);

	// play the firing montage and recoil
	WeaponOwner->PlayFiringMontage(FiringMontage);
	WeaponOwner->AddWeaponRecoil(FiringRecoil);

	// consume ammo; reload automatically on an empty magazine
	--CurrentBullets;
	WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);

	if (CurrentBullets <= 0)
	{
		StopFiring();
		Reload();
	}
}

FVector ASiegeGun::GetSightWorldLocation() const
{
	const USkeletalMeshComponent* Mesh = GetFirstPersonMesh();
	if (!Mesh->DoesSocketExist(MuzzleSocketName))
	{
		return Mesh->GetComponentLocation();
	}

	return Mesh->GetSocketTransform(MuzzleSocketName).TransformPosition(SightOffsetFromMuzzle);
}

float ASiegeGun::GetDamageAtDistance(float Distance) const
{
	if (Distance <= FalloffStart)
	{
		return Damage;
	}

	const float Alpha = FMath::Clamp((Distance - FalloffStart) / FMath::Max(1.0f, FalloffEnd - FalloffStart), 0.0f, 1.0f);
	return Damage * FMath::Lerp(1.0f, FalloffMinMultiplier, Alpha);
}

void ASiegeGun::Reload()
{
	if (bReloading || ReserveAmmo <= 0 || CurrentBullets >= MagazineSize)
	{
		return;
	}

	bReloading = true;
	StopFiring();

	GetWorld()->GetTimerManager().SetTimer(ReloadTimer, this, &ASiegeGun::FinishReload, ReloadTime, false);
}

void ASiegeGun::CancelReload()
{
	bReloading = false;
	GetWorld()->GetTimerManager().ClearTimer(ReloadTimer);
}

void ASiegeGun::FinishReload()
{
	bReloading = false;

	const int32 Needed = MagazineSize - CurrentBullets;
	const int32 Taken = FMath::Min(Needed, ReserveAmmo);

	CurrentBullets += Taken;
	ReserveAmmo -= Taken;

	if (WeaponOwner)
	{
		WeaponOwner->UpdateWeaponHUD(CurrentBullets, MagazineSize);
	}
}
