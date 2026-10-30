// Copyright Epic Games, Inc. All Rights Reserved.

#include "SiegeObjective.h"
#include "SiegeGameMode.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ASiegeBombSite::ASiegeBombSite()
{
	PrimaryActorTick.bCanEverTick = false;

	Area = CreateDefaultSubobject<UBoxComponent>(TEXT("Area"));
	RootComponent = Area;
	Area->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Area->SetCanEverAffectNavigation(false);
	Area->SetHiddenInGame(true);

	FloorMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Floor Marker"));
	FloorMarker->SetupAttachment(Area);
	FloorMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FloorMarker->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		FloorMarker->SetStaticMesh(CubeMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MarkerMat(TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Materials/MI_GlowNT.MI_GlowNT"));
	if (MarkerMat.Succeeded())
	{
		FloorMarker->SetMaterial(0, MarkerMat.Object);
	}
}

void ASiegeBombSite::SetExtent(const FVector& HalfExtent)
{
	Area->SetBoxExtent(HalfExtent);

	// place a thin plate on the floor, slightly inset from the zone edges
	FloorMarker->SetRelativeLocation(FVector(0.0f, 0.0f, -HalfExtent.Z + 1.0f));
	FloorMarker->SetRelativeScale3D(FVector(HalfExtent.X * 2.0f / 100.0f * 0.9f, HalfExtent.Y * 2.0f / 100.0f * 0.9f, 0.01f));
}

FVector ASiegeBombSite::GetExtent() const
{
	return Area->GetScaledBoxExtent();
}

bool ASiegeBombSite::IsInside(const FVector& Location) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(Location);
	const FVector Extent = Area->GetUnscaledBoxExtent();

	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

FVector ASiegeBombSite::GetRandomPoint() const
{
	const FVector Extent = Area->GetUnscaledBoxExtent() * 0.7f;
	const FVector Local(FMath::FRandRange(-Extent.X, Extent.X), FMath::FRandRange(-Extent.Y, Extent.Y), -Area->GetUnscaledBoxExtent().Z + 100.0f);
	return GetActorTransform().TransformPosition(Local);
}

ASiegeDevice::ASiegeDevice()
{
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetRelativeScale3D(FVector(0.35f, 0.35f, 0.5f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		Mesh->SetStaticMesh(CylinderMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowMat(TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Materials/MI_GlowNT.MI_GlowNT"));
	if (GlowMat.Succeeded())
	{
		Mesh->SetMaterial(0, GlowMat.Object);
	}

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Mesh);
	Light->SetRelativeLocation(FVector(0.0f, 0.0f, 80.0f));
	Light->SetLightColor(FLinearColor(1.0f, 0.15f, 0.05f));
	Light->SetIntensity(4000.0f);
	Light->SetAttenuationRadius(600.0f);
}

void ASiegeDevice::Arm(float Countdown, const FString& InSiteName)
{
	TimeRemaining = Countdown;
	SiteName = InSiteName;
	bDisabled = false;
	bDetonated = false;
}

void ASiegeDevice::Disable()
{
	bDisabled = true;
	Light->SetLightColor(FLinearColor(0.1f, 0.4f, 1.0f));
	Light->SetIntensity(1500.0f);
}

void ASiegeDevice::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsActive())
	{
		return;
	}

	TimeRemaining -= DeltaSeconds;

	// pulse the light faster as the countdown runs out
	const float PulseRate = FMath::GetMappedRangeValueClamped(FVector2D(45.0f, 0.0f), FVector2D(1.0f, 6.0f), TimeRemaining);
	const float Pulse = 0.5f + 0.5f * FMath::Sin(GetWorld()->GetTimeSeconds() * PulseRate * PI * 2.0f);
	Light->SetIntensity(1000.0f + Pulse * 6000.0f);

	if (TimeRemaining <= 0.0f)
	{
		TimeRemaining = 0.0f;
		bDetonated = true;

		if (ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>())
		{
			GM->OnDeviceDetonated(this);
		}
	}
}
