// Copyright Epic Games, Inc. All Rights Reserved.

#include "SiegeLayout.h"
#include "SiegeObjective.h"
#include "SiegeTypes.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"

namespace SiegeLayout
{
	static constexpr float WallHeight = 300.0f;
	static constexpr float WallThickness = 20.0f;
	static constexpr float MaxPanelWidth = 200.0f;
	static constexpr float UpperFloorZ = 320.0f;

	struct FBuilder
	{
		UWorld* World = nullptr;
		FSiegeLayoutResult* Out = nullptr;
		UStaticMesh* Cube = nullptr;
		UMaterialInterface* SolidMaterial = nullptr;
		UMaterialInterface* GroundMaterial = nullptr;

		/** Spawns an indestructible axis-aligned (optionally rotated) block */
		AActor* Block(const FVector& Center, const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator, UMaterialInterface* Material = nullptr)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Center, Rotation, Params);
			if (!Actor)
			{
				return nullptr;
			}

			UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent();
			MeshComp->SetMobility(EComponentMobility::Movable);
			MeshComp->SetStaticMesh(Cube);
			MeshComp->SetMaterial(0, Material ? Material : SolidMaterial);
			MeshComp->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Actor->SetActorScale3D(Size / 100.0f);
			Actor->Tags.Add(SiegeTags::Solid);

			Out->Actors.Add(Actor);
			return Actor;
		}

		ASiegeWall* Panel(const FVector& Center, const FRotator& Rotation, ESiegeWallKind Kind, float Width, float Height, float Thickness, bool bReinforceable, const FString& Label, bool bSiteWall)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			ASiegeWall* Wall = World->SpawnActor<ASiegeWall>(ASiegeWall::StaticClass(), Center, Rotation, Params);
			if (!Wall)
			{
				return nullptr;
			}

			Wall->InitWall(Kind, Width, Height, Thickness, bReinforceable, Label);
			Out->Actors.Add(Wall);

			if (bSiteWall && bReinforceable)
			{
				Out->SiteWalls.Add(Wall);
			}

			return Wall;
		}

		/**
		 *  Builds a straight wall run between two floor points, leaving gaps for doors and windows.
		 *  Soft runs are split into destructible panels; hard runs are solid blocks.
		 */
		void WallRun(const FVector2D& A, const FVector2D& B, float BaseZ, float Height, bool bSoft, bool bReinforceable, TArray<FGap> Gaps, const FString& Label, bool bSiteWall = false)
		{
			const FVector2D Delta = B - A;
			const float Length = Delta.Size();
			const FVector2D Dir = Delta / Length;
			const bool bAlongX = FMath::Abs(Dir.X) > FMath::Abs(Dir.Y);

			// panel local Y must follow the run direction
			const FRotator Rotation(0.0f, bAlongX ? 90.0f : 0.0f, 0.0f);

			auto PointAt = [&](float Dist, float Z) { const FVector2D P = A + Dir * Dist; return FVector(P.X, P.Y, Z); };

			auto Piece = [&](float From, float To, float Bottom, float Top, bool bAllowReinforce, ESiegeWallKind Kind = ESiegeWallKind::Soft)
			{
				const float SegLength = To - From;
				const float SegHeight = Top - Bottom;
				if (SegLength < 5.0f || SegHeight < 5.0f)
				{
					return;
				}

				if (!bSoft && Kind != ESiegeWallKind::Barricade)
				{
					const FVector Center = PointAt((From + To) * 0.5f, BaseZ + (Bottom + Top) * 0.5f);
					const FVector Size = bAlongX ? FVector(SegLength, WallThickness, SegHeight) : FVector(WallThickness, SegLength, SegHeight);
					Block(Center, Size);
					return;
				}

				const float Thickness = Kind == ESiegeWallKind::Barricade ? 6.0f : WallThickness;
				const int32 Count = Kind == ESiegeWallKind::Barricade ? 1 : FMath::CeilToInt(SegLength / MaxPanelWidth);
				const float PanelWidth = SegLength / Count;

				for (int32 i = 0; i < Count; ++i)
				{
					const float PanelCenter = From + PanelWidth * (i + 0.5f);
					const FVector Center = PointAt(PanelCenter, BaseZ + (Bottom + Top) * 0.5f);
					Panel(Center, Rotation, Kind, PanelWidth, SegHeight, Thickness, bAllowReinforce, Label, bSiteWall);
				}
			};

			Gaps.Sort([](const FGap& L, const FGap& R) { return L.Center < R.Center; });

			float Cursor = 0.0f;
			for (const FGap& Gap : Gaps)
			{
				const float GapStart = Gap.Center - Gap.Width * 0.5f;
				const float GapEnd = Gap.Center + Gap.Width * 0.5f;

				// full height wall up to the gap
				Piece(Cursor, GapStart, 0.0f, Height, bReinforceable);

				// sill below windows and lintel above
				Piece(GapStart, GapEnd, 0.0f, Gap.Bottom, false);
				Piece(GapStart, GapEnd, Gap.Top, Height, false);

				if (Gap.Fill == EGapFill::Barricade)
				{
					Piece(GapStart, GapEnd, Gap.Bottom, Gap.Top, false, ESiegeWallKind::Barricade);
				}

				Cursor = GapEnd;
			}

			Piece(Cursor, Length, 0.0f, Height, bReinforceable);
		}

		/** Destructible floor made of a grid of horizontal panels, top surface at TopZ */
		void Floor(float MinX, float MaxX, float MinY, float MaxY, float TopZ, int32 CountX, int32 CountY, const FString& Label)
		{
			const float SizeX = (MaxX - MinX) / CountX;
			const float SizeY = (MaxY - MinY) / CountY;
			const float CenterZ = TopZ - WallThickness * 0.5f;

			// pitch 90 maps panel thickness (local X) to world Z, width (local Y) to world Y and height (local Z) to world X
			const FRotator Rotation(90.0f, 0.0f, 0.0f);

			for (int32 X = 0; X < CountX; ++X)
			{
				for (int32 Y = 0; Y < CountY; ++Y)
				{
					const FVector Center(MinX + SizeX * (X + 0.5f), MinY + SizeY * (Y + 0.5f), CenterZ);
					Panel(Center, Rotation, ESiegeWallKind::Floor, SizeY, SizeX, WallThickness, false, Label, false);
				}
			}
		}

		ASiegeBombSite* Site(const FString& Name, const FVector& Center, const FVector& HalfExtent)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			ASiegeBombSite* SiteActor = World->SpawnActor<ASiegeBombSite>(ASiegeBombSite::StaticClass(), Center, FRotator::ZeroRotator, Params);
			if (SiteActor)
			{
				SiteActor->SiteName = Name;
				SiteActor->SetExtent(HalfExtent);
				Out->Actors.Add(SiteActor);
				Out->Sites.Add(SiteActor);
			}
			return SiteActor;
		}
	};

	static FGap Door(float Center, float Width = 120.0f, EGapFill Fill = EGapFill::Open)
	{
		FGap Gap;
		Gap.Center = Center;
		Gap.Width = Width;
		Gap.Bottom = 0.0f;
		Gap.Top = 220.0f;
		Gap.Fill = Fill;
		return Gap;
	}

	static FGap Window(float Center, float Width = 120.0f)
	{
		FGap Gap;
		Gap.Center = Center;
		Gap.Width = Width;
		Gap.Bottom = 100.0f;
		Gap.Top = 210.0f;
		Gap.Fill = EGapFill::Barricade;
		return Gap;
	}

	void BuildEmbassy(UWorld* World, FSiegeLayoutResult& Out)
	{
		FBuilder B;
		B.World = World;
		B.Out = &Out;
		B.Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		B.SolidMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray_02.MI_PrototypeGrid_Gray_02"));
		B.GroundMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_TopDark.MI_PrototypeGrid_TopDark"));

		if (!B.Cube)
		{
			return;
		}

		const float H = WallHeight;

		// ground
		B.Block(FVector(0.0f, 0.0f, -25.0f), FVector(12000.0f, 12000.0f, 50.0f), FRotator::ZeroRotator, B.GroundMaterial);

		// corner pillars
		for (const FVector2D& Corner : { FVector2D(-1500, -1000), FVector2D(1500, -1000), FVector2D(-1500, 1000), FVector2D(1500, 1000) })
		{
			B.Block(FVector(Corner.X, Corner.Y, H * 0.5f), FVector(40.0f, 40.0f, H));
		}

		// ----- exterior walls -----
		// distances along each run are measured from its first point

		// south facade: main entrance to the lobby, windows into kitchen and storage
		B.WallRun(FVector2D(-1500, -1000), FVector2D(1500, -1000), 0.0f, H, true, false,
			{ Window(500.0f), Door(1500.0f, 160.0f, EGapFill::Barricade), Window(2500.0f) }, TEXT("South Facade"));

		// north facade: site A office, lounge, site B server room
		B.WallRun(FVector2D(-1500, 1000), FVector2D(-500, 1000), 0.0f, H, true, true, {}, TEXT("A Office"), true);
		B.WallRun(FVector2D(-500, 1000), FVector2D(500, 1000), 0.0f, H, true, false, { Door(500.0f, 120.0f, EGapFill::Barricade) }, TEXT("Lounge"));
		B.WallRun(FVector2D(500, 1000), FVector2D(1500, 1000), 0.0f, H, true, true, { Window(500.0f) }, TEXT("B Server"), true);

		// west facade: kitchen, hallway door, site A
		B.WallRun(FVector2D(-1500, -1000), FVector2D(-1500, -150), 0.0f, H, true, false, { Window(425.0f) }, TEXT("Kitchen"));
		B.WallRun(FVector2D(-1500, -150), FVector2D(-1500, 150), 0.0f, H, true, false, { Door(150.0f, 140.0f, EGapFill::Barricade) }, TEXT("West Hall"));
		B.WallRun(FVector2D(-1500, 150), FVector2D(-1500, 1000), 0.0f, H, true, true, {}, TEXT("A Office"), true);

		// east facade: storage, hallway door, solid concrete along site B
		B.WallRun(FVector2D(1500, -1000), FVector2D(1500, -150), 0.0f, H, true, false, {}, TEXT("Storage"));
		B.WallRun(FVector2D(1500, -150), FVector2D(1500, 150), 0.0f, H, true, false, { Door(150.0f, 140.0f, EGapFill::Barricade) }, TEXT("East Hall"));
		B.WallRun(FVector2D(1500, 150), FVector2D(1500, 1000), 0.0f, H, false, false, {}, TEXT("B Server"));

		// ----- interior walls -----

		// hallway north side, doors into site A, lounge and site B
		B.WallRun(FVector2D(-1500, 150), FVector2D(-500, 150), 0.0f, H, true, true, { Door(500.0f) }, TEXT("A Office"), true);
		B.WallRun(FVector2D(-500, 150), FVector2D(500, 150), 0.0f, H, true, false, { Door(500.0f, 160.0f) }, TEXT("Lounge"));
		B.WallRun(FVector2D(500, 150), FVector2D(1500, 150), 0.0f, H, true, true, { Door(500.0f) }, TEXT("B Server"), true);

		// hallway south side, wide opening into the lobby
		B.WallRun(FVector2D(-1500, -150), FVector2D(1500, -150), 0.0f, H, true, false, { Door(500.0f), Door(1500.0f, 300.0f), Door(2500.0f) }, TEXT("Hallway"));

		// north room dividers (sites connect to the lounge)
		B.WallRun(FVector2D(-500, 150), FVector2D(-500, 1000), 0.0f, H, true, true, { Door(450.0f) }, TEXT("A Office"), true);
		B.WallRun(FVector2D(500, 150), FVector2D(500, 1000), 0.0f, H, true, true, { Door(450.0f) }, TEXT("B Server"), true);

		// south room dividers
		B.WallRun(FVector2D(-500, -1000), FVector2D(-500, -150), 0.0f, H, true, false, { Door(425.0f) }, TEXT("Kitchen"));
		B.WallRun(FVector2D(500, -1000), FVector2D(500, -150), 0.0f, H, false, false, {}, TEXT("Storage"));

		// ----- upstairs office above site A -----

		B.Floor(-1500.0f, -500.0f, 150.0f, 1000.0f, UpperFloorZ, 4, 4, TEXT("A Office Ceiling"));

		const float UZ = UpperFloorZ;
		B.WallRun(FVector2D(-1500, 150), FVector2D(-500, 150), UZ, H, false, false, {}, TEXT("Upstairs"));
		B.WallRun(FVector2D(-1500, 1000), FVector2D(-500, 1000), UZ, H, true, false, { Window(500.0f) }, TEXT("Upstairs"));
		B.WallRun(FVector2D(-1500, 150), FVector2D(-1500, 1000), UZ, H, false, false, { Door(550.0f, 140.0f) }, TEXT("Upstairs"));
		B.WallRun(FVector2D(-500, 150), FVector2D(-500, 1000), UZ, H, true, false, { Window(425.0f, 160.0f) }, TEXT("Upstairs"));

		// roof
		B.Block(FVector(-1000.0f, 575.0f, UZ + H + 10.0f), FVector(1040.0f, 890.0f, 20.0f));

		// exterior ramp up to the upstairs door (top at x=-1500, z=320)
		{
			const float RampLength = 900.0f;
			const float Rise = UZ;
			const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Rise, RampLength));
			const float Slant = FMath::Sqrt(RampLength * RampLength + Rise * Rise);
			const FVector Center(-1560.0f - RampLength * 0.5f, 700.0f, Rise * 0.5f - 10.0f);
			B.Block(Center, FVector(Slant, 180.0f, 20.0f), FRotator(Angle, 0.0f, 0.0f));

			// landing bridging the ramp top to the doorway
			B.Block(FVector(-1530.0f, 700.0f, UZ - 10.0f), FVector(80.0f, 180.0f, 20.0f));
		}

		// ----- outside cover for attackers -----
		B.Block(FVector(-700.0f, -1700.0f, 75.0f), FVector(420.0f, 180.0f, 150.0f));
		B.Block(FVector(800.0f, -1850.0f, 75.0f), FVector(180.0f, 420.0f, 150.0f));
		B.Block(FVector(-2200.0f, 0.0f, 60.0f), FVector(200.0f, 400.0f, 120.0f));
		B.Block(FVector(2300.0f, 400.0f, 75.0f), FVector(420.0f, 180.0f, 150.0f));
		B.Block(FVector(0.0f, 1800.0f, 60.0f), FVector(500.0f, 160.0f, 120.0f));

		// ----- bomb sites -----
		B.Site(TEXT("A"), FVector(-1000.0f, 575.0f, 150.0f), FVector(470.0f, 400.0f, 150.0f));
		B.Site(TEXT("B"), FVector(1000.0f, 575.0f, 150.0f), FVector(470.0f, 400.0f, 150.0f));

		// ----- spawns and AI points -----
		const FRotator FaceNorth(0.0f, 90.0f, 0.0f);
		for (int32 i = 0; i < 5; ++i)
		{
			Out.AttackerSpawns.Add(FTransform(FaceNorth, FVector(-600.0f + i * 300.0f, -2400.0f, 100.0f)));
		}

		const FRotator FaceSouth(0.0f, -90.0f, 0.0f);
		Out.DefenderSpawns.Add(FTransform(FaceSouth, FVector(-1100.0f, 700.0f, 100.0f)));
		Out.DefenderSpawns.Add(FTransform(FaceSouth, FVector(-800.0f, 450.0f, 100.0f)));
		Out.DefenderSpawns.Add(FTransform(FaceSouth, FVector(0.0f, 600.0f, 100.0f)));
		Out.DefenderSpawns.Add(FTransform(FaceSouth, FVector(800.0f, 450.0f, 100.0f)));
		Out.DefenderSpawns.Add(FTransform(FaceSouth, FVector(1100.0f, 700.0f, 100.0f)));

		Out.DefenderPostsA = { FVector(-1350.0f, 880.0f, 100.0f), FVector(-650.0f, 880.0f, 100.0f), FVector(-1350.0f, 300.0f, 100.0f), FVector(-650.0f, 320.0f, 100.0f), FVector(-200.0f, 850.0f, 100.0f) };
		Out.DefenderPostsB = { FVector(1350.0f, 880.0f, 100.0f), FVector(650.0f, 880.0f, 100.0f), FVector(1350.0f, 300.0f, 100.0f), FVector(650.0f, 320.0f, 100.0f), FVector(200.0f, 850.0f, 100.0f) };

		Out.RoamPoints = { FVector(-1200.0f, 0.0f, 100.0f), FVector(1200.0f, 0.0f, 100.0f), FVector(-1000.0f, -600.0f, 100.0f), FVector(1000.0f, -600.0f, 100.0f), FVector(0.0f, -600.0f, 100.0f), FVector(-1000.0f, 575.0f, 420.0f) };

		Out.EntryPoints = { FVector(0.0f, -1300.0f, 100.0f), FVector(-1900.0f, 0.0f, 100.0f), FVector(1900.0f, 0.0f, 100.0f), FVector(-1000.0f, -1300.0f, 100.0f), FVector(1000.0f, -1300.0f, 100.0f) };
	}

	void Clear(FSiegeLayoutResult& Layout)
	{
		for (const TWeakObjectPtr<AActor>& Actor : Layout.Actors)
		{
			if (Actor.IsValid())
			{
				Actor->Destroy();
			}
		}

		Layout = FSiegeLayoutResult();
	}
}
