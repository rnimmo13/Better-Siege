// Copyright Epic Games, Inc. All Rights Reserved.

#include "SiegeRope.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ASiegeRopeAnchor::ASiegeRopeAnchor()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RopeMat(TEXT("/Game/LevelPrototyping/Materials/MI_DefaultColorway.MI_DefaultColorway"));

	RopeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Rope"));
	RopeMesh->SetupAttachment(RootComponent);
	RopeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RopeMesh->SetCanEverAffectNavigation(false);
	if (Cylinder.Succeeded())
	{
		RopeMesh->SetStaticMesh(Cylinder.Object);
	}
	if (RopeMat.Succeeded())
	{
		RopeMesh->SetMaterial(0, RopeMat.Object);
	}

	HookMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Hook"));
	HookMesh->SetupAttachment(RootComponent);
	HookMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HookMesh->SetCanEverAffectNavigation(false);
	if (Cube.Succeeded())
	{
		HookMesh->SetStaticMesh(Cube.Object);
	}

	InitRope(Height);
}

void ASiegeRopeAnchor::InitRope(float InHeight)
{
	Height = InHeight;

	// a thin rope hanging just off the wall from the roof edge, with a hook block on top
	RopeMesh->SetRelativeLocation(FVector(12.0f, 0.0f, Height * 0.5f));
	RopeMesh->SetRelativeScale3D(FVector(0.04f, 0.04f, Height / 100.0f));

	HookMesh->SetRelativeLocation(FVector(8.0f, 0.0f, Height + 5.0f));
	HookMesh->SetRelativeScale3D(FVector(0.3f, 0.2f, 0.1f));
}

bool ASiegeRopeAnchor::CanAttach(const FVector& FeetLocation, float& OutAlong, float& OutHeight) const
{
	const FVector Local = FeetLocation - GetActorLocation();

	const float Along = FVector::DotProduct(Local, GetAlongDirection());
	const float Out = FVector::DotProduct(Local, GetOutward());
	const float Up = Local.Z;

	if (FMath::Abs(Along) > HalfWidth + 30.0f || Out < 10.0f || Out > 200.0f || Up < -60.0f || Up > Height - 150.0f)
	{
		return false;
	}

	OutAlong = FMath::Clamp(Along, -HalfWidth, HalfWidth);
	OutHeight = FMath::Max(0.0f, Up);
	return true;
}

FVector ASiegeRopeAnchor::GetHangLocation(float Along, float HeightOnRope, float CapsuleHalfHeight) const
{
	return GetActorLocation() + GetAlongDirection() * Along + FVector(0.0f, 0.0f, HeightOnRope + CapsuleHalfHeight) + GetOutward() * 45.0f;
}
