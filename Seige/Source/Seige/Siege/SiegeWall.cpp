// Copyright Epic Games, Inc. All Rights Reserved.

#include "SiegeWall.h"
#include "SiegeSound.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/DamageEvents.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ASiegeWall::ASiegeWall()
{
	PrimaryActorTick.bCanEverTick = false;

	Cells = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Cells"));
	RootComponent = Cells;

	Cells->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Cells->SetMobility(EComponentMobility::Movable);
	Cells->SetCanEverAffectNavigation(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		Cells->SetStaticMesh(CubeMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SoftMat(TEXT("/Game/Siege/Materials/Env/MI_Env_Plaster.MI_Env_Plaster"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ReinforcedMat(TEXT("/Game/Siege/Materials/Env/MI_Env_Reinforced.MI_Env_Reinforced"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BarricadeMat(TEXT("/Game/Siege/Materials/Env/MI_Env_Barricade.MI_Env_Barricade"));

	SoftMaterial = SoftMat.Object;
	ReinforcedMaterial = ReinforcedMat.Object;
	BarricadeMaterial = BarricadeMat.Object;
}

void ASiegeWall::InitWall(ESiegeWallKind InKind, float InWidth, float InHeight, float InThickness, bool bInReinforceable, const FString& InRoomLabel)
{
	Kind = InKind;
	Width = InWidth;
	Height = InHeight;
	Thickness = InThickness;
	RoomLabel = InRoomLabel;
	bReinforceable = bInReinforceable && Kind != ESiegeWallKind::Barricade;
	bReinforced = false;

	if (Kind == ESiegeWallKind::Barricade)
	{
		// barricades are flimsy and don't block navigation so bots will path through and bash them
		// sturdy enough to survive stray shots at drones slipping underneath, still quick to melee or shoot open
		CellSize = 40.0f;
		CellHealth = 60.0f;
		Cells->SetCanEverAffectNavigation(false);
	}
	else if (Kind == ESiegeWallKind::Hatch)
	{
		// a couple of melee hits or a short burst opens it
		CellSize = 25.0f;
		CellHealth = 40.0f;
		bReinforceable = bInReinforceable;
	}

	NumCols = FMath::Max(1, FMath::RoundToInt(Width / CellSize));
	NumRows = FMath::Max(1, FMath::RoundToInt(Height / CellSize));

	CellHP.Init(CellHealth, NumCols * NumRows);

	UpdateMaterial();
	RebuildInstances();
}

FVector ASiegeWall::GetCellLocalCenter(int32 CellIndex) const
{
	const int32 Row = CellIndex / NumCols;
	const int32 Col = CellIndex % NumCols;

	const float CellW = Width / NumCols;
	const float CellH = Height / NumRows;

	return FVector(0.0f, -Width * 0.5f + CellW * (Col + 0.5f), -Height * 0.5f + CellH * (Row + 0.5f));
}

void ASiegeWall::RebuildInstances()
{
	Cells->ClearInstances();
	InstanceToCell.Reset();

	const float CellW = Width / NumCols;
	const float CellH = Height / NumRows;

	// reinforced panels are visibly thicker
	const float VisualThickness = bReinforced ? Thickness * 1.6f : Thickness;
	const FVector Scale(VisualThickness / 100.0f, CellW / 100.0f, CellH / 100.0f);

	TArray<FTransform> Transforms;
	Transforms.Reserve(CellHP.Num());

	for (int32 i = 0; i < CellHP.Num(); ++i)
	{
		if (CellHP[i] > 0.0f)
		{
			Transforms.Add(FTransform(FRotator::ZeroRotator, GetCellLocalCenter(i), Scale));
			InstanceToCell.Add(i);
		}
	}

	if (Transforms.Num() > 0)
	{
		Cells->AddInstances(Transforms, false, false);
	}
}

void ASiegeWall::UpdateMaterial()
{
	UMaterialInterface* Mat = SoftMaterial;

	if (bReinforced)
	{
		Mat = ReinforcedMaterial;
	}
	else if (Kind == ESiegeWallKind::Barricade)
	{
		Mat = BarricadeMaterial;
	}

	if (Mat)
	{
		Cells->SetMaterial(0, Mat);
	}
}

float ASiegeWall::TakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// reinforced panels shrug off bullets and melee
	if (bReinforced || Damage <= 0.0f)
	{
		return 0.0f;
	}

	int32 CellIndex = INDEX_NONE;
	FVector HitLocation = GetActorLocation();

	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const FPointDamageEvent& PointEvent = static_cast<const FPointDamageEvent&>(DamageEvent);
		HitLocation = PointEvent.HitInfo.ImpactPoint;

		if (PointEvent.HitInfo.GetComponent() == Cells && InstanceToCell.IsValidIndex(PointEvent.HitInfo.Item))
		{
			CellIndex = InstanceToCell[PointEvent.HitInfo.Item];
		}
	}

	// fall back to the closest living cell to the hit location
	if (CellIndex == INDEX_NONE)
	{
		const FVector Local = GetActorTransform().InverseTransformPosition(HitLocation);
		float BestDist = TNumericLimits<float>::Max();

		for (int32 i = 0; i < CellHP.Num(); ++i)
		{
			if (CellHP[i] > 0.0f)
			{
				const float Dist = FVector::DistSquared(Local, GetCellLocalCenter(i));
				if (Dist < BestDist)
				{
					BestDist = Dist;
					CellIndex = i;
				}
			}
		}
	}

	if (CellHP.IsValidIndex(CellIndex) && CellHP[CellIndex] > 0.0f)
	{
		CellHP[CellIndex] -= Damage;

		if (CellHP[CellIndex] <= 0.0f)
		{
			CellHP[CellIndex] = 0.0f;
			RebuildInstances();

			// splintering when a piece breaks off, throttled so full-auto fire does not stack dozens of sounds
			const float Now = GetWorld()->GetTimeSeconds();
			if (Now - LastBreakSoundTime > 0.08f)
			{
				LastBreakSoundTime = Now;
				USiegeSoundSubsystem::Play(this, ESiegeSound::Crunch, HitLocation, IsBarricade() ? 0.8f : 0.5f, this);
			}
		}
	}

	return Damage;
}

void ASiegeWall::ApplyExplosion(const FVector& WorldLocation, float Radius, bool bHardBreach)
{
	if (bReinforced && !bHardBreach)
	{
		return;
	}

	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	const FVector2D LocalPlane(Local.Y, Local.Z);

	// ignore explosions that are far away from the panel's plane
	if (FMath::Abs(Local.X) > Radius)
	{
		return;
	}

	bool bChanged = false;

	for (int32 i = 0; i < CellHP.Num(); ++i)
	{
		if (CellHP[i] > 0.0f)
		{
			const FVector Center = GetCellLocalCenter(i);
			if (FVector2D::Distance(FVector2D(Center.Y, Center.Z), LocalPlane) <= Radius)
			{
				CellHP[i] = 0.0f;
				bChanged = true;
			}
		}
	}

	if (bChanged)
	{
		RebuildInstances();
	}
}

void ASiegeWall::DestroyAllCells()
{
	for (float& HP : CellHP)
	{
		HP = 0.0f;
	}

	RebuildInstances();
}

bool ASiegeWall::CutDroneHoleAt(const FVector& WorldLocation, float HoleWidth, float HoleHeight)
{
	if (Kind == ESiegeWallKind::Floor)
	{
		return false;
	}

	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	const float Bottom = -Height * 0.5f;

	// must be on this panel, near its base
	if (FMath::Abs(Local.X) > Thickness * 2.0f || FMath::Abs(Local.Y) > Width * 0.5f || Local.Z < Bottom - 30.0f || Local.Z > Bottom + HoleHeight + 30.0f)
	{
		return false;
	}

	int32 Cut = 0;
	for (int32 i = 0; i < CellHP.Num(); ++i)
	{
		const FVector Center = GetCellLocalCenter(i);
		if (CellHP[i] > 0.0f && FMath::Abs(Center.Y - Local.Y) < HoleWidth * 0.5f && Center.Z < Bottom + HoleHeight)
		{
			CellHP[i] = 0.0f;
			++Cut;
		}
	}

	if (Cut > 0)
	{
		PreCutCells += Cut;
		RebuildInstances();
	}

	return Cut > 0;
}

bool ASiegeWall::CutRectAt(const FVector& WorldCenter, float SizeAlongWidth, float SizeAlongHeight)
{
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldCenter);

	if (FMath::Abs(Local.X) > Thickness * 2.0f)
	{
		return false;
	}

	int32 Cut = 0;
	for (int32 i = 0; i < CellHP.Num(); ++i)
	{
		const FVector Center = GetCellLocalCenter(i);
		if (CellHP[i] > 0.0f && FMath::Abs(Center.Y - Local.Y) < SizeAlongWidth * 0.5f && FMath::Abs(Center.Z - Local.Z) < SizeAlongHeight * 0.5f)
		{
			CellHP[i] = 0.0f;
			++Cut;
		}
	}

	if (Cut > 0)
	{
		PreCutCells += Cut;
		RebuildInstances();
	}

	return Cut > 0;
}

bool ASiegeWall::CanBeReinforced() const
{
	// can't reinforce a panel that already has holes in it
	return bReinforceable && !bReinforced && GetIntegrity() > 0.95f;
}

bool ASiegeWall::Reinforce()
{
	if (!CanBeReinforced())
	{
		return false;
	}

	bReinforced = true;
	UpdateMaterial();
	RebuildInstances();
	return true;
}

float ASiegeWall::GetIntegrity() const
{
	if (CellHP.Num() == 0)
	{
		return 0.0f;
	}

	int32 Alive = 0;
	for (float HP : CellHP)
	{
		if (HP > 0.0f)
		{
			++Alive;
		}
	}

	return static_cast<float>(Alive) / FMath::Max(1, CellHP.Num() - PreCutCells);
}

float ASiegeWall::GetPenetrationFactor() const
{
	if (bReinforced)
	{
		return 0.0f;
	}

	return (Kind == ESiegeWallKind::Barricade || Kind == ESiegeWallKind::Hatch) ? 0.9f : 0.6f;
}

void ASiegeWall::SetSurfaceMaterial(UMaterialInterface* Material)
{
	if (Material && Kind != ESiegeWallKind::Barricade)
	{
		SoftMaterial = Material;
		UpdateMaterial();
	}
}
