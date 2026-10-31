// Copyright Epic Games, Inc. All Rights Reserved.

#include "SiegeLayout.h"
#include "SiegeObjective.h"
#include "SiegeObservation.h"
#include "SiegeRope.h"
#include "SiegeTypes.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace SiegeLayout
{
	static constexpr float WallHeight = 300.0f;
	static constexpr float WallThickness = 20.0f;
	static constexpr float MaxPanelWidth = 200.0f;
	static constexpr float UpperFloorZ = 320.0f;

	/** Top of the basement floor; the ground floor slab above it is 20cm thick with its top at 0 */
	static constexpr float BasementZ = -340.0f;

	/** Basement walls run from the basement floor up to the underside of the ground floor */
	static constexpr float BasementWallHeight = -WallThickness - BasementZ;

	/** Surface materials, all instances of the world-aligned M_Env */
	struct FSurfaces
	{
		UMaterialInterface* Brick = nullptr;
		UMaterialInterface* Plaster = nullptr;
		UMaterialInterface* Wood = nullptr;
		UMaterialInterface* Tile = nullptr;
		UMaterialInterface* Concrete = nullptr;
		UMaterialInterface* Carpet = nullptr;
		UMaterialInterface* Pavers = nullptr;
		UMaterialInterface* Metal = nullptr;
		UMaterialInterface* Light = nullptr;
		UMaterialInterface* Barricade = nullptr;

		void Load()
		{
			auto Get = [](const TCHAR* Name)
			{
				return LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Siege/Materials/Env/MI_Env_%s.MI_Env_%s"), Name, Name));
			};

			Brick = Get(TEXT("Brick"));
			Plaster = Get(TEXT("Plaster"));
			Wood = Get(TEXT("Wood"));
			Tile = Get(TEXT("Tile"));
			Concrete = Get(TEXT("Concrete"));
			Carpet = Get(TEXT("Carpet"));
			Pavers = Get(TEXT("Pavers"));
			Metal = Get(TEXT("Metal"));
			Light = Get(TEXT("Light"));
			Barricade = Get(TEXT("Barricade"));
		}
	};

	struct FBuilder
	{
		UWorld* World = nullptr;
		FSiegeLayoutResult* Out = nullptr;
		UStaticMesh* Cube = nullptr;
		UStaticMesh* Cylinder = nullptr;
		UStaticMesh* Sphere = nullptr;
		FSurfaces S;

		/** Material used by the next WallRun's panels and solid pieces */
		UMaterialInterface* WallMat = nullptr;

		/** Painted ceiling color shown on the underside of floors */
		const FLinearColor PlasterCeiling = FLinearColor(0.82f, 0.81f, 0.78f, 1.0f);
		const FLinearColor ConcreteCeiling = FLinearColor(0.36f, 0.36f, 0.35f, 1.0f);

		/** A recolored copy of a surface. Ceiling alpha 0 keeps the surface's own look on downward faces */
		UMaterialInterface* Tinted(UMaterialInterface* Base, const FLinearColor& Tint, const FLinearColor* Ceiling = nullptr)
		{
			if (!Base)
			{
				return nullptr;
			}

			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, World);
			MID->SetVectorParameterValue(TEXT("Tint"), Tint);
			if (Ceiling)
			{
				MID->SetVectorParameterValue(TEXT("CeilingColor"), *Ceiling);
			}
			return MID;
		}

		AStaticMeshActor* SpawnMesh(UStaticMesh* Mesh, const FVector& Center, const FVector& Size, const FRotator& Rotation, UMaterialInterface* Material, bool bCollide)
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
			MeshComp->SetStaticMesh(Mesh);
			MeshComp->SetMaterial(0, Material ? Material : S.Concrete);

			if (bCollide)
			{
				MeshComp->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			}
			else
			{
				MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				MeshComp->SetCanEverAffectNavigation(false);
			}

			Actor->SetActorScale3D(Size / 100.0f);
			Out->Actors.Add(Actor);
			return Actor;
		}

		/** Spawns an indestructible axis-aligned (optionally rotated) structural block */
		AActor* Block(const FVector& Center, const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator, UMaterialInterface* Material = nullptr)
		{
			AStaticMeshActor* Actor = SpawnMesh(Cube, Center, Size, Rotation, Material ? Material : WallMat, true);
			if (Actor)
			{
				Actor->Tags.Add(SiegeTags::Solid);
			}
			return Actor;
		}

		/** Furniture and dressing: solid to move around and take cover behind, but not a structural blast shield */
		AStaticMeshActor* Prop(const FVector& Center, const FVector& Size, UMaterialInterface* Material, bool bCollide = true, const FRotator& Rotation = FRotator::ZeroRotator, UStaticMesh* Mesh = nullptr)
		{
			return SpawnMesh(Mesh ? Mesh : Cube, Center, Size, Rotation, Material, bCollide);
		}

		/** Paper-thin visual layer with no collision, for floor finishes and signage */
		void Decal(const FVector& Center, const FVector& Size, UMaterialInterface* Material)
		{
			if (AStaticMeshActor* Actor = Prop(Center, Size, Material, false))
			{
				Actor->GetStaticMeshComponent()->SetCastShadow(false);
			}
		}

		/** Ceiling light with a glowing fixture panel just above it */
		void CeilingLight(const FVector& Location, float Radius, float Intensity = 900.0f, const FLinearColor& Color = FLinearColor(1.0f, 0.9f, 0.78f))
		{
			APointLight* Light = World->SpawnActorDeferred<APointLight>(APointLight::StaticClass(), FTransform(Location), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Light)
			{
				return;
			}

			UPointLightComponent* Comp = Light->PointLightComponent;
			Comp->SetMobility(EComponentMobility::Movable);
			Comp->SetIntensityUnits(ELightUnits::Candelas);
			Comp->SetIntensity(Intensity);
			Comp->SetAttenuationRadius(Radius);
			Comp->SetLightColor(Color);
			Comp->SetSourceRadius(25.0f);
			Comp->SetCastShadows(true);
			Light->FinishSpawning(FTransform(Location));
			Out->Actors.Add(Light);

			if (AStaticMeshActor* Fixture = Prop(Location + FVector(0.0f, 0.0f, 16.0f), FVector(110.0f, 36.0f, 4.0f), S.Light, false))
			{
				Fixture->GetStaticMeshComponent()->SetCastShadow(false);
			}
		}

		ASiegeWall* Panel(const FVector& Center, const FRotator& Rotation, ESiegeWallKind Kind, float Width, float Height, float Thickness, bool bReinforceable, const FString& Label, bool bSiteWall, UMaterialInterface* Material)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			ASiegeWall* Wall = World->SpawnActor<ASiegeWall>(ASiegeWall::StaticClass(), Center, Rotation, Params);
			if (!Wall)
			{
				return nullptr;
			}

			Wall->InitWall(Kind, Width, Height, Thickness, bReinforceable, Label);
			Wall->SetSurfaceMaterial(Material);
			Out->Actors.Add(Wall);

			if (bSiteWall && bReinforceable)
			{
				Out->SiteWalls.Add(Wall);
			}

			return Wall;
		}

		/**
		 *  Builds a straight wall run between two floor points, leaving gaps for doors and windows.
		 *  Soft runs are split into destructible panels; hard runs are solid blocks. Both use WallMat.
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
					Panel(Center, Rotation, Kind, PanelWidth, SegHeight, Thickness, bAllowReinforce, Label, bSiteWall, WallMat);
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
					// door barricades leave a gap at the bottom drones can slip under
					const float BarricadeBottom = Gap.Bottom > 0.0f ? Gap.Bottom : 25.0f;
					Piece(GapStart, GapEnd, BarricadeBottom, Gap.Top, false, ESiegeWallKind::Barricade);
				}

				Cursor = GapEnd;
			}

			Piece(Cursor, Length, 0.0f, Height, bReinforceable);
		}

		/** Destructible floor made of a grid of horizontal panels, top surface at TopZ */
		void Floor(float MinX, float MaxX, float MinY, float MaxY, float TopZ, int32 CountX, int32 CountY, const FString& Label, UMaterialInterface* Material)
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
					Panel(Center, Rotation, ESiegeWallKind::Floor, SizeY, SizeX, WallThickness, false, Label, false, Material);
				}
			}

			// indestructible joists under the panel seams: a blown-open floor leaves narrow shooting lanes
			for (int32 X = 1; X < CountX; ++X)
			{
				const float BeamX = MinX + SizeX * X;
				Block(FVector(BeamX, (MinY + MaxY) * 0.5f, TopZ - WallThickness - 12.0f), FVector(20.0f, MaxY - MinY, 24.0f), FRotator::ZeroRotator, S.Wood);
			}
		}

		/** Replaces a square of floor with a weak hatch cover centered on X/Y */
		void Hatch(float X, float Y, float TopZ, const FString& Label, bool bSiteHatch)
		{
			const float Size = 100.0f;
			const FVector Center(X, Y, TopZ - WallThickness * 0.5f);

			for (const TWeakObjectPtr<AActor>& Actor : Out->Actors)
			{
				ASiegeWall* Wall = Cast<ASiegeWall>(Actor.Get());
				if (Wall && Wall->GetKind() == ESiegeWallKind::Floor && FMath::Abs(Wall->GetActorLocation().Z - Center.Z) < 5.0f && FVector::Dist2D(Wall->GetActorLocation(), Center) < 260.0f)
				{
					Wall->CutRectAt(Center, Size, Size);
				}
			}

			Panel(Center, FRotator(90.0f, 0.0f, 0.0f), ESiegeWallKind::Hatch, Size, Size, 12.0f, true, Label, bSiteHatch, S.Metal);
		}

		/** Cuts a drone hole in whichever destructible panel stands at this floor point */
		void DroneHole(float X, float Y, float BaseZ)
		{
			const FVector Point(X, Y, BaseZ + 25.0f);

			for (const TWeakObjectPtr<AActor>& Actor : Out->Actors)
			{
				if (ASiegeWall* Wall = Cast<ASiegeWall>(Actor.Get()))
				{
					if (Wall->CutDroneHoleAt(Point))
					{
						return;
					}
				}
			}
		}

		/**
		 *  Low concrete duct only a drone fits through, running straight between two floor points.
		 *  Interior is 50cm wide and 45cm tall; both ends are open.
		 */
		void DroneTunnel(const FVector2D& From, const FVector2D& To, const FString& Label)
		{
			const FVector2D Delta = To - From;
			const float Length = Delta.Size();
			const bool bAlongX = FMath::Abs(Delta.X) > FMath::Abs(Delta.Y);
			const FVector2D Mid = (From + To) * 0.5f;

			const float InnerWidth = 50.0f;
			const float InnerHeight = 45.0f;
			const float Side = 15.0f;
			const float Roof = 10.0f;

			const FVector SideSize = bAlongX ? FVector(Length, Side, InnerHeight) : FVector(Side, Length, InnerHeight);
			const FVector Offset = bAlongX ? FVector(0.0f, (InnerWidth + Side) * 0.5f, 0.0f) : FVector((InnerWidth + Side) * 0.5f, 0.0f, 0.0f);
			const FVector Center(Mid.X, Mid.Y, InnerHeight * 0.5f);

			Block(Center + Offset, SideSize, FRotator::ZeroRotator, S.Concrete);
			Block(Center - Offset, SideSize, FRotator::ZeroRotator, S.Concrete);

			const float OuterWidth = InnerWidth + Side * 2.0f;
			const FVector RoofSize = bAlongX ? FVector(Length, OuterWidth, Roof) : FVector(OuterWidth, Length, Roof);
			Block(FVector(Mid.X, Mid.Y, InnerHeight + Roof * 0.5f), RoofSize, FRotator::ZeroRotator, S.Concrete);
		}

		/** Straight stair or ramp slab rising from (FromX, FromZ) to (ToX, ToZ) along X, centered on Y */
		void SlopeX(float FromX, float FromZ, float ToX, float ToZ, float Y, float Width, UMaterialInterface* Material)
		{
			const float Run = ToX - FromX;
			const float Rise = ToZ - FromZ;
			const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Rise, FMath::Abs(Run)));
			const float Slant = FMath::Sqrt(Run * Run + Rise * Rise);
			const float Yaw = Run >= 0.0f ? 0.0f : 180.0f;
			Block(FVector((FromX + ToX) * 0.5f, Y, (FromZ + ToZ) * 0.5f - 10.0f), FVector(Slant, Width, 20.0f), FRotator(Angle, Yaw, 0.0f), Material);
		}

		/** Same as SlopeX along the Y axis */
		void SlopeY(float FromY, float FromZ, float ToY, float ToZ, float X, float Width, UMaterialInterface* Material)
		{
			const float Run = ToY - FromY;
			const float Rise = ToZ - FromZ;
			const float Angle = FMath::RadiansToDegrees(FMath::Atan2(Rise, FMath::Abs(Run)));
			const float Slant = FMath::Sqrt(Run * Run + Rise * Rise);
			const float Yaw = Run >= 0.0f ? 90.0f : -90.0f;
			Block(FVector(X, (FromY + ToY) * 0.5f, (FromZ + ToZ) * 0.5f - 10.0f), FVector(Slant, Width, 20.0f), FRotator(Angle, Yaw, 0.0f), Material);
		}

		/** Desk-height surface: body plus a contrasting top */
		void Counter(const FVector& Base, const FVector& Size, UMaterialInterface* Body, UMaterialInterface* Top)
		{
			Prop(Base + FVector(0.0f, 0.0f, (Size.Z - 4.0f) * 0.5f), FVector(Size.X, Size.Y, Size.Z - 4.0f), Body);
			Prop(Base + FVector(0.0f, 0.0f, Size.Z - 2.0f), FVector(Size.X + 4.0f, Size.Y + 4.0f, 4.0f), Top);
		}

		/** Office desk with a monitor; FacingY is +1 or -1 for which side the monitor faces */
		void Desk(const FVector& Base, bool bAlongX, float Facing)
		{
			const FVector Size = bAlongX ? FVector(160.0f, 80.0f, 76.0f) : FVector(80.0f, 160.0f, 76.0f);
			Counter(Base, Size, Tinted(S.Wood, FLinearColor(0.7f, 0.6f, 0.5f)), S.Wood);

			const FVector Back = bAlongX ? FVector(0.0f, -20.0f * Facing, 0.0f) : FVector(-20.0f * Facing, 0.0f, 0.0f);
			const FVector ScreenSize = bAlongX ? FVector(55.0f, 6.0f, 34.0f) : FVector(6.0f, 55.0f, 34.0f);
			Prop(Base + Back + FVector(0.0f, 0.0f, 76.0f + 22.0f), ScreenSize, Tinted(S.Metal, FLinearColor(0.08f, 0.08f, 0.09f)), false);
		}

		/** Tall shelving unit with rows of colored stock on it */
		void Shelf(const FVector& Base, const FVector& Size, UMaterialInterface* Frame, const TArray<UMaterialInterface*>& Stock)
		{
			Prop(Base + FVector(0.0f, 0.0f, Size.Z * 0.5f), Size, Frame);

			if (Stock.Num() == 0)
			{
				return;
			}

			// stock pokes slightly out of the frame on both long faces so it reads as filled shelves
			const bool bAlongX = Size.X > Size.Y;
			const int32 Rows = FMath::Max(1, FMath::FloorToInt(Size.Z / 45.0f) - 1);
			for (int32 Row = 0; Row < Rows; ++Row)
			{
				const FVector RowSize = bAlongX ? FVector(Size.X - 12.0f, Size.Y + 4.0f, 28.0f) : FVector(Size.X + 4.0f, Size.Y - 12.0f, 28.0f);
				Prop(Base + FVector(0.0f, 0.0f, 30.0f + Row * 45.0f), RowSize, Stock[Row % Stock.Num()], false);
			}
		}

		/** Tree: trunk with collision and a leafy canopy without */
		void Tree(const FVector2D& At, float Height, UMaterialInterface* Bark, UMaterialInterface* Leaves)
		{
			Prop(FVector(At.X, At.Y, Height * 0.4f), FVector(40.0f, 40.0f, Height * 0.8f), Bark, true, FRotator::ZeroRotator, Cylinder);
			Prop(FVector(At.X, At.Y, Height * 0.85f), FVector(Height * 0.6f, Height * 0.6f, Height * 0.5f), Leaves, false, FRotator::ZeroRotator, Sphere);
			Prop(FVector(At.X + Height * 0.12f, At.Y - Height * 0.08f, Height * 0.7f), FVector(Height * 0.42f, Height * 0.42f, Height * 0.36f), Leaves, false, FRotator::ZeroRotator, Sphere);
		}

		ASiegeRopeAnchor* Rope(const FVector& BaseOnWall, float OutwardYaw, float Height)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			ASiegeRopeAnchor* Anchor = World->SpawnActor<ASiegeRopeAnchor>(ASiegeRopeAnchor::StaticClass(), BaseOnWall, FRotator(0.0f, OutwardYaw, 0.0f), Params);
			if (Anchor)
			{
				Anchor->InitRope(Height);
				Out->Actors.Add(Anchor);
			}
			return Anchor;
		}

		ASiegeSecurityCamera* SecurityCamera(const FString& Name, const FVector& Location, float Yaw)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			// tilt slightly down like a ceiling-corner camera
			ASiegeSecurityCamera* Cam = World->SpawnActor<ASiegeSecurityCamera>(ASiegeSecurityCamera::StaticClass(), Location, FRotator(-20.0f, Yaw, 0.0f), Params);
			if (Cam)
			{
				Cam->InitCamera(Name, 1);
				Out->Actors.Add(Cam);
				Out->Cameras.Add(Cam);
			}
			return Cam;
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

	/** Drone-sized opening at the base of a solid wall */
	static FGap DroneGap(float Center)
	{
		FGap Gap;
		Gap.Center = Center;
		Gap.Width = 50.0f;
		Gap.Bottom = 0.0f;
		Gap.Top = 45.0f;
		Gap.Fill = EGapFill::Open;
		return Gap;
	}

	/** Floor-to-head-height window big enough to rappel through when crouched */
	static FGap TallWindow(float Center, float Width = 140.0f)
	{
		FGap Gap;
		Gap.Center = Center;
		Gap.Width = Width;
		Gap.Bottom = 20.0f;
		Gap.Top = 230.0f;
		Gap.Fill = EGapFill::Barricade;
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

	FString GetSiteSetName(int32 SiteSet)
	{
		return SiteSet == 1 ? TEXT("Basement: A Vault / B Generator") : TEXT("Ground floor: A Office / B Server");
	}

	void BuildEmbassy(UWorld* World, FSiegeLayoutResult& Out, int32 SiteSet)
	{
		FBuilder B;
		B.World = World;
		B.Out = &Out;
		B.Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		B.Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		B.Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		B.S.Load();

		if (!B.Cube || !B.Cylinder || !B.Sphere)
		{
			return;
		}

		const FSurfaces& S = B.S;
		const float H = WallHeight;
		const float UZ = UpperFloorZ;
		const float BZ = BasementZ;
		const float BH = BasementWallHeight;

		// which pair of rooms holds the bomb sites this round
		const bool bGroundSites = SiteSet != 1;
		const bool bBasementSites = SiteSet == 1;

		// room colors; walls shared by two rooms take the color of the room named in the run label
		const FLinearColor ConcreteCeil = B.ConcreteCeiling;
		const FLinearColor PlasterCeil = B.PlasterCeiling;
		UMaterialInterface* PaintHall = B.Tinted(S.Plaster, FLinearColor(0.93f, 0.89f, 0.8f));
		UMaterialInterface* PaintOffice = B.Tinted(S.Plaster, FLinearColor(0.68f, 0.76f, 0.88f));
		UMaterialInterface* PaintLounge = B.Tinted(S.Plaster, FLinearColor(0.95f, 0.82f, 0.64f));
		UMaterialInterface* PaintServer = B.Tinted(S.Plaster, FLinearColor(0.74f, 0.84f, 0.8f));
		UMaterialInterface* PaintKitchen = B.Tinted(S.Plaster, FLinearColor(0.96f, 0.94f, 0.78f));
		UMaterialInterface* PaintLibrary = B.Tinted(S.Plaster, FLinearColor(0.62f, 0.74f, 0.6f));
		UMaterialInterface* BasementBlock = B.Tinted(S.Concrete, FLinearColor(0.95f, 0.95f, 0.9f));
		UMaterialInterface* RailMetal = B.Tinted(S.Metal, FLinearColor(0.2f, 0.2f, 0.22f));
		UMaterialInterface* DarkWood = B.Tinted(S.Wood, FLinearColor(0.55f, 0.45f, 0.4f));
		UMaterialInterface* Leaves = B.Tinted(S.Carpet, FLinearColor(0.35f, 0.75f, 0.25f));
		UMaterialInterface* Bark = B.Tinted(S.Wood, FLinearColor(0.45f, 0.35f, 0.3f));
		UMaterialInterface* Fabric = B.Tinted(S.Carpet, FLinearColor(1.5f, 0.9f, 0.6f));
		UMaterialInterface* Asphalt = B.Tinted(S.Concrete, FLinearColor(0.35f, 0.35f, 0.36f));

		// ===== ground =====
		// the yard is paved; the building footprint south of y=150 sits over the basement, and the cellar steps cut a trench outside
		B.WallMat = S.Pavers;
		B.Block(FVector(0.0f, 3070.0f, -25.0f), FVector(12000.0f, 5860.0f, 50.0f));					// north of the basement: y 140..6000
		B.Block(FVector(0.0f, -3850.0f, -25.0f), FVector(12000.0f, 4300.0f, 50.0f));				// south of the cellar trench: y -6000..-1700
		B.Block(FVector(-2855.0f, -1355.0f, -25.0f), FVector(6290.0f, 690.0f, 50.0f));				// trench west side: x -6000..290
		B.Block(FVector(3235.0f, -1355.0f, -25.0f), FVector(5530.0f, 690.0f, 50.0f));				// trench east side: x 470..6000
		B.Block(FVector(-3755.0f, -435.0f, -25.0f), FVector(4490.0f, 1150.0f, 50.0f));				// west of the basement: x -6000..-1510, y -1000..140
		B.Block(FVector(3755.0f, -435.0f, -25.0f), FVector(4490.0f, 1150.0f, 50.0f));				// east of the basement
		B.Decal(FVector(0.0f, -3100.0f, 1.0f), FVector(12000.0f, 500.0f, 2.0f), Asphalt);			// street behind the attacker spawn

		// corner pillars
		for (const FVector2D& Corner : { FVector2D(-1500, -1000), FVector2D(1500, -1000), FVector2D(-1500, 1000), FVector2D(1500, 1000) })
		{
			B.Block(FVector(Corner.X, Corner.Y, H * 0.5f), FVector(40.0f, 40.0f, H), FRotator::ZeroRotator, S.Concrete);
		}

		// ===== basement shell =====
		B.WallMat = S.Concrete;
		B.Block(FVector(0.0f, -425.0f, BZ - 20.0f), FVector(3020.0f, 1170.0f, 40.0f));

		// perimeter is poured concrete; the cellar door opens onto the outside steps
		B.WallRun(FVector2D(-1500, -1000), FVector2D(1500, -1000), BZ, -BZ, false, false, { Door(1880.0f, 140.0f, EGapFill::Barricade) }, TEXT("Cellar"));
		B.WallRun(FVector2D(-1500, 150), FVector2D(1500, 150), BZ, -BZ, false, false, {}, TEXT("Basement Corridor"));
		B.WallRun(FVector2D(-1500, -1000), FVector2D(-1500, 150), BZ, -BZ, false, false, {}, TEXT("Vault"));
		B.WallRun(FVector2D(1500, -1000), FVector2D(1500, 150), BZ, -BZ, false, false, {}, TEXT("Generator Room"));

		// interior block walls: vault, cellar and generator room off a corridor under the main hallway
		B.WallMat = BasementBlock;
		B.WallRun(FVector2D(-1500, -150), FVector2D(-500, -150), BZ, BH, true, true, { Door(500.0f) }, TEXT("Vault"), bBasementSites);
		B.WallRun(FVector2D(-500, -150), FVector2D(500, -150), BZ, BH, true, false, { Door(500.0f, 160.0f) }, TEXT("Cellar"));
		B.WallRun(FVector2D(500, -150), FVector2D(1500, -150), BZ, BH, true, true, { Door(500.0f) }, TEXT("Generator Room"), bBasementSites);
		B.WallRun(FVector2D(-500, -1000), FVector2D(-500, -150), BZ, BH, true, true, { Door(425.0f) }, TEXT("Vault"), bBasementSites);
		B.WallRun(FVector2D(500, -1000), FVector2D(500, -150), BZ, BH, true, true, { Door(425.0f) }, TEXT("Generator Room"), bBasementSites);

		// ground floor over the basement: destructible floors in the south rooms, a solid hallway slab with the stairwell cut out
		B.Floor(-1500.0f, -500.0f, -1000.0f, -150.0f, 0.0f, 4, 3, TEXT("Kitchen Floor"), B.Tinted(S.Tile, FLinearColor(0.95f, 0.9f, 0.82f), &ConcreteCeil));
		B.Floor(-500.0f, 500.0f, -1000.0f, -150.0f, 0.0f, 4, 3, TEXT("Lobby Floor"), B.Tinted(S.Tile, FLinearColor::White, &ConcreteCeil));
		B.Floor(500.0f, 1500.0f, -1000.0f, -150.0f, 0.0f, 4, 3, TEXT("Storage Floor"), S.Concrete);
		B.Hatch(-800.0f, -700.0f, 0.0f, TEXT("Vault Hatch"), bBasementSites);
		B.Hatch(800.0f, -450.0f, 0.0f, TEXT("Generator Hatch"), bBasementSites);

		{
			UMaterialInterface* HallFloor = B.Tinted(S.Wood, FLinearColor::White, &ConcreteCeil);
			B.Block(FVector(0.0f, -67.5f, -10.0f), FVector(3000.0f, 165.0f, 20.0f), FRotator::ZeroRotator, HallFloor);	// y -150..15
			B.Block(FVector(-620.0f, 82.5f, -10.0f), FVector(1760.0f, 135.0f, 20.0f), FRotator::ZeroRotator, HallFloor);	// x -1500..260
			B.Block(FVector(1200.0f, 82.5f, -10.0f), FVector(600.0f, 135.0f, 20.0f), FRotator::ZeroRotator, HallFloor);	// x 900..1500

			// stairs from the hallway down to the basement corridor, with a rail around the stairwell
			B.SlopeX(260.0f, 0.0f, 900.0f, BZ, 77.5f, 125.0f, S.Wood);
			B.Block(FVector(580.0f, 15.0f, 50.0f), FVector(640.0f, 6.0f, 100.0f), FRotator::ZeroRotator, RailMetal);
			B.Block(FVector(900.0f, 82.5f, 50.0f), FVector(6.0f, 135.0f, 100.0f), FRotator::ZeroRotator, RailMetal);
		}

		// outside steps down to the cellar door: a ramp in a walled trench east of the main entrance
		B.SlopeY(-1700.0f, 0.0f, -1010.0f, BZ, 380.0f, 160.0f, S.Concrete);
		B.Block(FVector(290.0f, -1355.0f, BZ * 0.5f), FVector(20.0f, 690.0f, -BZ), FRotator::ZeroRotator, S.Concrete);
		B.Block(FVector(470.0f, -1355.0f, BZ * 0.5f), FVector(20.0f, 690.0f, -BZ), FRotator::ZeroRotator, S.Concrete);
		B.Block(FVector(380.0f, -1355.0f, BZ - 20.0f), FVector(200.0f, 690.0f, 40.0f), FRotator::ZeroRotator, S.Concrete);
		B.Block(FVector(290.0f, -1355.0f, 50.0f), FVector(6.0f, 690.0f, 100.0f), FRotator::ZeroRotator, RailMetal);
		B.Block(FVector(470.0f, -1355.0f, 50.0f), FVector(6.0f, 690.0f, 100.0f), FRotator::ZeroRotator, RailMetal);

		// ===== exterior walls =====
		// distances along each run are measured from its first point
		B.WallMat = S.Brick;

		// south facade: main entrance to the lobby, windows into kitchen and storage
		B.WallRun(FVector2D(-1500, -1000), FVector2D(1500, -1000), 0.0f, H, true, false,
			{ Window(500.0f), Door(1500.0f, 160.0f, EGapFill::Barricade), Window(2500.0f) }, TEXT("South Facade"));

		// north facade: site A office, lounge, site B server room
		B.WallRun(FVector2D(-1500, 1000), FVector2D(-500, 1000), 0.0f, H, true, true, {}, TEXT("A Office"), bGroundSites);
		B.WallRun(FVector2D(-500, 1000), FVector2D(500, 1000), 0.0f, H, true, false, { Door(500.0f, 120.0f, EGapFill::Barricade) }, TEXT("Lounge"));
		B.WallRun(FVector2D(500, 1000), FVector2D(1500, 1000), 0.0f, H, true, true, { Window(500.0f) }, TEXT("B Server"), bGroundSites);

		// west facade: kitchen, hallway door, site A
		B.WallRun(FVector2D(-1500, -1000), FVector2D(-1500, -150), 0.0f, H, true, false, { Window(425.0f) }, TEXT("Kitchen"));
		B.WallRun(FVector2D(-1500, -150), FVector2D(-1500, 150), 0.0f, H, true, false, { Door(150.0f, 140.0f, EGapFill::Barricade) }, TEXT("West Hall"));
		B.WallRun(FVector2D(-1500, 150), FVector2D(-1500, 1000), 0.0f, H, true, true, {}, TEXT("A Office"), bGroundSites);

		// east facade: storage, hallway door, solid concrete along site B
		B.WallRun(FVector2D(1500, -1000), FVector2D(1500, -150), 0.0f, H, true, false, {}, TEXT("Storage"));
		B.WallRun(FVector2D(1500, -150), FVector2D(1500, 150), 0.0f, H, true, false, { Door(150.0f, 140.0f, EGapFill::Barricade) }, TEXT("East Hall"));
		B.WallRun(FVector2D(1500, 150), FVector2D(1500, 1000), 0.0f, H, false, false, { DroneGap(550.0f) }, TEXT("B Server"));

		// ===== interior walls =====

		// hallway north side, doors into site A, lounge and site B
		B.WallMat = PaintOffice;
		B.WallRun(FVector2D(-1500, 150), FVector2D(-500, 150), 0.0f, H, true, true, { Door(500.0f) }, TEXT("A Office"), bGroundSites);
		B.WallMat = PaintLounge;
		B.WallRun(FVector2D(-500, 150), FVector2D(500, 150), 0.0f, H, true, false, { Door(500.0f, 160.0f) }, TEXT("Lounge"));
		B.WallMat = PaintServer;
		B.WallRun(FVector2D(500, 150), FVector2D(1500, 150), 0.0f, H, true, true, { Door(500.0f) }, TEXT("B Server"), bGroundSites);

		// hallway south side, wide opening into the lobby
		B.WallMat = PaintHall;
		B.WallRun(FVector2D(-1500, -150), FVector2D(1500, -150), 0.0f, H, true, false, { Door(500.0f), Door(1500.0f, 300.0f), Door(2500.0f) }, TEXT("Hallway"));

		// north room dividers (sites connect to the lounge)
		B.WallMat = PaintOffice;
		B.WallRun(FVector2D(-500, 150), FVector2D(-500, 1000), 0.0f, H, true, true, { Door(450.0f) }, TEXT("A Office"), bGroundSites);
		B.WallMat = PaintServer;
		B.WallRun(FVector2D(500, 150), FVector2D(500, 1000), 0.0f, H, true, true, { Door(450.0f) }, TEXT("B Server"), bGroundSites);

		// south room dividers
		B.WallMat = PaintKitchen;
		B.WallRun(FVector2D(-500, -1000), FVector2D(-500, -150), 0.0f, H, true, false, { Door(425.0f) }, TEXT("Kitchen"));
		B.WallMat = S.Concrete;
		B.WallRun(FVector2D(500, -1000), FVector2D(500, -150), 0.0f, H, false, false, {}, TEXT("Storage"));

		// floor finishes in the north wing, which stands on solid ground
		B.Decal(FVector(-1000.0f, 575.0f, 0.5f), FVector(980.0f, 830.0f, 1.0f), B.Tinted(S.Carpet, FLinearColor(0.75f, 0.85f, 1.15f)));
		B.Decal(FVector(0.0f, 575.0f, 0.5f), FVector(980.0f, 830.0f, 1.0f), S.Wood);
		B.Decal(FVector(1000.0f, 575.0f, 0.5f), FVector(980.0f, 830.0f, 1.0f), B.Tinted(S.Tile, FLinearColor(0.6f, 0.62f, 0.65f)));

		// ===== second floor over the north wing (above site A, the lounge and site B) =====

		// destructible floors with structural beams left underneath when they're blown open
		B.Floor(-1500.0f, -500.0f, 150.0f, 1000.0f, UZ, 4, 4, TEXT("A Office Ceiling"), B.Tinted(S.Carpet, FLinearColor(0.7f, 0.5f, 0.45f), &PlasterCeil));
		B.Floor(-500.0f, 500.0f, 150.0f, 1000.0f, UZ, 4, 4, TEXT("Lounge Ceiling"), S.Wood);
		B.Floor(500.0f, 1500.0f, 150.0f, 1000.0f, UZ, 4, 4, TEXT("B Server Ceiling"), B.Tinted(S.Tile, FLinearColor(0.7f, 0.7f, 0.7f), &PlasterCeil));

		// hatches: smash open to drop down, or reinforce to keep closed
		B.Hatch(-1125.0f, 400.0f, UZ, TEXT("A Hatch"), bGroundSites);
		B.Hatch(1125.0f, 800.0f, UZ, TEXT("B Hatch"), bGroundSites);
		B.Hatch(125.0f, 500.0f, UZ, TEXT("Lounge Hatch"), false);

		// upstairs rooms: Upstairs Office (above A), Library (above the lounge), Archive (above B)
		B.WallMat = PaintHall;
		B.WallRun(FVector2D(-1500, 150), FVector2D(1500, 150), UZ, H, true, false, { Window(500.0f), Door(1270.0f, 120.0f), Window(2500.0f) }, TEXT("Upstairs Hall Side"));
		B.WallMat = S.Brick;
		B.WallRun(FVector2D(-1500, 1000), FVector2D(1500, 1000), UZ, H, true, false, { TallWindow(500.0f), TallWindow(1500.0f), TallWindow(2500.0f) }, TEXT("Upstairs North"));
		B.WallRun(FVector2D(-1500, 150), FVector2D(-1500, 1000), UZ, H, false, false, { Door(550.0f, 140.0f) }, TEXT("Upstairs Office"));
		B.WallRun(FVector2D(1500, 150), FVector2D(1500, 1000), UZ, H, false, false, {}, TEXT("Archive"));
		B.WallMat = PaintLibrary;
		B.WallRun(FVector2D(-500, 150), FVector2D(-500, 1000), UZ, H, true, false, { Door(550.0f) }, TEXT("Library"));
		B.WallRun(FVector2D(500, 150), FVector2D(500, 1000), UZ, H, true, false, { Door(550.0f) }, TEXT("Library"));

		// roofs: high over the north wing and the hallway stairwell, low over the south rooms
		B.Block(FVector(0.0f, 430.0f, UZ + H + 10.0f), FVector(3040.0f, 1180.0f, 20.0f), FRotator::ZeroRotator, S.Concrete);
		B.Block(FVector(0.0f, -580.0f, H + 10.0f), FVector(3040.0f, 860.0f, 20.0f), FRotator::ZeroRotator, S.Concrete);

		// brick closing in the tall hallway above the south rooms' roof
		B.Block(FVector(0.0f, -150.0f, H + (UZ + H - H) * 0.5f), FVector(3040.0f, 20.0f, UZ), FRotator::ZeroRotator, S.Brick);
		B.Block(FVector(-1500.0f, 0.0f, H + UZ * 0.5f), FVector(20.0f, 300.0f, UZ), FRotator::ZeroRotator, S.Brick);
		B.Block(FVector(1500.0f, 0.0f, H + UZ * 0.5f), FVector(20.0f, 300.0f, UZ), FRotator::ZeroRotator, S.Brick);

		// interior staircase in the hallway up to the Library door (top at x=-300, z=320)
		{
			const float StairY = 77.5f;
			B.SlopeX(-940.0f, 0.0f, -300.0f, UZ, StairY, 125.0f, S.Wood);

			// landing in front of the Library door
			B.Block(FVector(-230.0f, StairY, UZ - 10.0f), FVector(140.0f, 125.0f, 20.0f), FRotator::ZeroRotator, S.Wood);
		}

		// exterior ramp up to the upstairs door (top at x=-1500, z=320)
		B.SlopeX(-2460.0f, 0.0f, -1560.0f, UZ, 700.0f, 180.0f, S.Concrete);
		B.Block(FVector(-1530.0f, 700.0f, UZ - 10.0f), FVector(80.0f, 180.0f, 20.0f), FRotator::ZeroRotator, S.Concrete);

		// ===== outside cover for attackers =====
		B.WallMat = S.Concrete;
		B.Block(FVector(-700.0f, -1700.0f, 75.0f), FVector(420.0f, 180.0f, 150.0f));
		B.Block(FVector(800.0f, -1850.0f, 75.0f), FVector(180.0f, 420.0f, 150.0f));
		B.Block(FVector(-2200.0f, 0.0f, 60.0f), FVector(200.0f, 400.0f, 120.0f));
		B.Block(FVector(2300.0f, 400.0f, 75.0f), FVector(420.0f, 180.0f, 150.0f));
		B.Block(FVector(0.0f, 1800.0f, 60.0f), FVector(500.0f, 160.0f, 120.0f));

		// ===== furniture and dressing =====
		{
			UMaterialInterface* Steel = S.Metal;
			UMaterialInterface* DarkSteel = B.Tinted(S.Metal, FLinearColor(0.25f, 0.27f, 0.3f));
			UMaterialInterface* Brass = B.Tinted(S.Metal, FLinearColor(1.0f, 0.75f, 0.35f));
			UMaterialInterface* Marble = B.Tinted(S.Tile, FLinearColor(0.9f, 0.9f, 0.92f));
			UMaterialInterface* Crate = B.Tinted(S.Wood, FLinearColor(1.3f, 1.1f, 0.85f));
			UMaterialInterface* Books1 = B.Tinted(S.Carpet, FLinearColor(1.6f, 0.4f, 0.35f));
			UMaterialInterface* Books2 = B.Tinted(S.Carpet, FLinearColor(0.4f, 0.6f, 1.4f));
			UMaterialInterface* Books3 = B.Tinted(S.Carpet, FLinearColor(1.4f, 1.2f, 0.5f));
			UMaterialInterface* Files = B.Tinted(S.Plaster, FLinearColor(0.9f, 0.85f, 0.7f));
			UMaterialInterface* ServerGlow = B.Tinted(S.Light, FLinearColor(0.2f, 1.0f, 0.5f));
			UMaterialInterface* Pot = B.Tinted(S.Concrete, FLinearColor(0.8f, 0.55f, 0.4f));
			const TArray<UMaterialInterface*> BookRows = { Books1, Books2, Books3, Books2 };

			// kitchen: counters along the west and south walls, an island and a fridge (the vault hatch and both drone holes stay clear)
			B.Counter(FVector(-1460.0f, -360.0f, 0.0f), FVector(60.0f, 240.0f, 92.0f), DarkWood, Marble);
			B.Counter(FVector(-1010.0f, -960.0f, 0.0f), FVector(500.0f, 60.0f, 92.0f), DarkWood, Marble);
			B.Counter(FVector(-1000.0f, -480.0f, 0.0f), FVector(200.0f, 90.0f, 92.0f), DarkWood, Marble);
			B.Prop(FVector(-560.0f, -950.0f, 100.0f), FVector(60.0f, 70.0f, 200.0f), Steel);

			// lobby: reception desk facing the entrance, benches and plants
			B.Counter(FVector(0.0f, -430.0f, 0.0f), FVector(320.0f, 80.0f, 110.0f), DarkWood, Marble);
			B.Prop(FVector(-380.0f, -850.0f, 25.0f), FVector(60.0f, 200.0f, 50.0f), S.Wood);
			B.Prop(FVector(380.0f, -850.0f, 25.0f), FVector(60.0f, 200.0f, 50.0f), S.Wood);
			for (float PlantX : { -440.0f, 440.0f })
			{
				B.Prop(FVector(PlantX, -205.0f, 35.0f), FVector(50.0f, 50.0f, 70.0f), Pot, true, FRotator::ZeroRotator, B.Cylinder);
				B.Prop(FVector(PlantX, -205.0f, 105.0f), FVector(80.0f, 80.0f, 90.0f), Leaves, false, FRotator::ZeroRotator, B.Sphere);
			}

			// storage: racking along the east wall and stacked crates (the generator hatch at 800,-450 stays clear)
			B.Shelf(FVector(1460.0f, -600.0f, 0.0f), FVector(60.0f, 700.0f, 220.0f), DarkSteel, { Crate, Files });
			B.Prop(FVector(680.0f, -860.0f, 50.0f), FVector(100.0f, 100.0f, 100.0f), Crate);
			B.Prop(FVector(680.0f, -860.0f, 140.0f), FVector(80.0f, 80.0f, 80.0f), Crate);
			B.Prop(FVector(800.0f, -880.0f, 40.0f), FVector(80.0f, 80.0f, 80.0f), Crate);
			B.Prop(FVector(1200.0f, -380.0f, 50.0f), FVector(100.0f, 100.0f, 100.0f), Crate);

			// A office: two desks, filing cabinets along the west wall
			B.Desk(FVector(-1250.0f, 800.0f, 0.0f), true, 1.0f);
			B.Desk(FVector(-750.0f, 800.0f, 0.0f), true, 1.0f);
			B.Prop(FVector(-1465.0f, 420.0f, 65.0f), FVector(45.0f, 120.0f, 130.0f), DarkSteel);
			B.Prop(FVector(-1465.0f, 560.0f, 65.0f), FVector(45.0f, 120.0f, 130.0f), DarkSteel);

			// lounge: sofas facing a coffee table
			for (float SofaX : { -300.0f, 300.0f })
			{
				B.Prop(FVector(SofaX, 850.0f, 22.0f), FVector(200.0f, 80.0f, 44.0f), Fabric);
				B.Prop(FVector(SofaX, 885.0f, 60.0f), FVector(200.0f, 18.0f, 80.0f), Fabric);
			}
			B.Prop(FVector(0.0f, 620.0f, 20.0f), FVector(120.0f, 70.0f, 40.0f), DarkWood);

			// B server room: two rows of racks with status lights
			for (float RackX : { 750.0f, 1250.0f })
			{
				B.Prop(FVector(RackX, 650.0f, 100.0f), FVector(60.0f, 350.0f, 200.0f), DarkSteel);
				B.Decal(FVector(RackX - 31.0f, 650.0f, 150.0f), FVector(2.0f, 320.0f, 6.0f), ServerGlow);
				B.Decal(FVector(RackX + 31.0f, 650.0f, 120.0f), FVector(2.0f, 320.0f, 6.0f), ServerGlow);
			}

			// upstairs office, library shelves and archive (the hatches stay clear)
			B.Desk(FVector(-1000.0f, 650.0f, UZ), true, 1.0f);
			B.Prop(FVector(-1465.0f, 900.0f, UZ + 65.0f), FVector(45.0f, 120.0f, 130.0f), DarkSteel);
			B.Shelf(FVector(-300.0f, 750.0f, UZ), FVector(40.0f, 300.0f, 200.0f), DarkWood, BookRows);
			B.Shelf(FVector(320.0f, 750.0f, UZ), FVector(40.0f, 300.0f, 200.0f), DarkWood, BookRows);
			B.Prop(FVector(-60.0f, 820.0f, UZ + 38.0f), FVector(160.0f, 90.0f, 76.0f), S.Wood);
			B.Shelf(FVector(850.0f, 450.0f, UZ), FVector(300.0f, 50.0f, 200.0f), DarkSteel, { Files, Crate });
			B.Shelf(FVector(850.0f, 900.0f, UZ), FVector(300.0f, 50.0f, 200.0f), DarkSteel, { Files, Crate });

			// vault: wall of brass deposit boxes, the round vault door and a strongbox table in the middle
			B.Prop(FVector(-1000.0f, -975.0f, BZ + 120.0f), FVector(900.0f, 30.0f, 220.0f), Brass);
			B.Prop(FVector(-1480.0f, -575.0f, BZ + 140.0f), FVector(240.0f, 240.0f, 24.0f), DarkSteel, true, FRotator(90.0f, 0.0f, 0.0f), B.Cylinder);
			B.Prop(FVector(-1000.0f, -600.0f, BZ + 45.0f), FVector(160.0f, 80.0f, 90.0f), Steel);
			B.Prop(FVector(-1300.0f, -300.0f, BZ + 60.0f), FVector(90.0f, 90.0f, 120.0f), DarkSteel);

			// cellar: wine rack, barrels and crates (the outside door at x=380 stays clear)
			B.Shelf(FVector(-250.0f, -970.0f, BZ), FVector(400.0f, 40.0f, 220.0f), DarkWood, { Books1, DarkWood });
			for (const FVector2D& Barrel : { FVector2D(-380.0f, -420.0f), FVector2D(-300.0f, -460.0f), FVector2D(-380.0f, -510.0f) })
			{
				B.Prop(FVector(Barrel.X, Barrel.Y, BZ + 45.0f), FVector(70.0f, 70.0f, 90.0f), DarkWood, true, FRotator::ZeroRotator, B.Cylinder);
			}
			B.Prop(FVector(250.0f, -480.0f, BZ + 50.0f), FVector(100.0f, 100.0f, 100.0f), Crate);
			B.Prop(FVector(150.0f, -420.0f, BZ + 40.0f), FVector(80.0f, 80.0f, 80.0f), Crate);

			// generator room: generator block with exhaust, boiler tank, breaker panels
			B.Prop(FVector(1050.0f, -700.0f, BZ + 90.0f), FVector(320.0f, 160.0f, 180.0f), DarkSteel);
			B.Prop(FVector(1150.0f, -700.0f, BZ + 240.0f), FVector(30.0f, 30.0f, 120.0f), Steel, false, FRotator::ZeroRotator, B.Cylinder);
			B.Prop(FVector(700.0f, -860.0f, BZ + 110.0f), FVector(120.0f, 120.0f, 220.0f), Steel, true, FRotator::ZeroRotator, B.Cylinder);
			B.Prop(FVector(1485.0f, -380.0f, BZ + 150.0f), FVector(20.0f, 150.0f, 180.0f), DarkSteel);

			// pipes along the basement corridor ceiling
			B.Prop(FVector(-620.0f, 125.0f, -70.0f), FVector(18.0f, 18.0f, 1760.0f), Steel, false, FRotator(90.0f, 0.0f, 0.0f), B.Cylinder);
			B.Prop(FVector(-620.0f, 100.0f, -60.0f), FVector(12.0f, 12.0f, 1760.0f), Brass, false, FRotator(90.0f, 0.0f, 0.0f), B.Cylinder);

			// yard: flagpole and planters by the entrance, trees around the grounds
			B.Prop(FVector(-260.0f, -1300.0f, 400.0f), FVector(12.0f, 12.0f, 800.0f), Steel, true, FRotator::ZeroRotator, B.Cylinder);
			B.Decal(FVector(-195.0f, -1300.0f, 740.0f), FVector(120.0f, 3.0f, 26.0f), B.Tinted(S.Plaster, FLinearColor(0.1f, 0.45f, 0.25f)));
			B.Decal(FVector(-195.0f, -1300.0f, 714.0f), FVector(120.0f, 3.0f, 26.0f), S.Plaster);
			B.Decal(FVector(-195.0f, -1300.0f, 688.0f), FVector(120.0f, 3.0f, 26.0f), B.Tinted(S.Plaster, FLinearColor(0.1f, 0.45f, 0.25f)));
			for (const FVector2D& Planter : { FVector2D(-400.0f, -1080.0f), FVector2D(650.0f, -1080.0f) })
			{
				B.Prop(FVector(Planter.X, Planter.Y, 30.0f), FVector(220.0f, 60.0f, 60.0f), S.Concrete);
				B.Prop(FVector(Planter.X, Planter.Y, 70.0f), FVector(200.0f, 50.0f, 40.0f), Leaves, false);
			}
			for (const FVector2D& TreeAt : { FVector2D(-2600.0f, -2000.0f), FVector2D(2600.0f, -1800.0f), FVector2D(-2800.0f, 1500.0f), FVector2D(2700.0f, 2000.0f), FVector2D(-900.0f, 2600.0f), FVector2D(1200.0f, 2800.0f) })
			{
				B.Tree(TreeAt, 700.0f, Bark, Leaves);
			}
		}

		// ===== lights =====
		// fixtures sit between the ceiling joists; the hatches stay dark
		B.CeilingLight(FVector(-1000.0f, -575.0f, 270.0f), 750.0f);
		B.CeilingLight(FVector(0.0f, -575.0f, 270.0f), 750.0f);
		B.CeilingLight(FVector(1000.0f, -575.0f, 270.0f), 750.0f, 700.0f, FLinearColor(0.85f, 0.92f, 1.0f));
		B.CeilingLight(FVector(-700.0f, 0.0f, 590.0f), 900.0f);
		B.CeilingLight(FVector(700.0f, 0.0f, 590.0f), 900.0f);
		B.CeilingLight(FVector(-875.0f, 575.0f, 270.0f), 750.0f, 900.0f, FLinearColor(0.9f, 0.95f, 1.0f));
		B.CeilingLight(FVector(-125.0f, 700.0f, 270.0f), 750.0f);
		B.CeilingLight(FVector(875.0f, 575.0f, 270.0f), 750.0f, 700.0f, FLinearColor(0.8f, 0.95f, 1.0f));
		B.CeilingLight(FVector(-1000.0f, 575.0f, 590.0f), 750.0f);
		B.CeilingLight(FVector(0.0f, 575.0f, 590.0f), 750.0f);
		B.CeilingLight(FVector(1000.0f, 575.0f, 590.0f), 750.0f);
		B.CeilingLight(FVector(-875.0f, -575.0f, -65.0f), 750.0f, 800.0f);
		B.CeilingLight(FVector(-125.0f, -575.0f, -65.0f), 750.0f, 650.0f);
		B.CeilingLight(FVector(875.0f, -575.0f, -65.0f), 750.0f, 800.0f, FLinearColor(1.0f, 0.75f, 0.5f));
		B.CeilingLight(FVector(-900.0f, 0.0f, -45.0f), 800.0f, 650.0f);
		B.CeilingLight(FVector(1200.0f, 0.0f, -45.0f), 800.0f, 650.0f);

		// ===== bomb sites =====
		if (bBasementSites)
		{
			B.Site(TEXT("A Vault"), FVector(-1000.0f, -575.0f, BZ + 150.0f), FVector(470.0f, 400.0f, 150.0f));
			B.Site(TEXT("B Generator"), FVector(1000.0f, -575.0f, BZ + 150.0f), FVector(470.0f, 400.0f, 150.0f));
		}
		else
		{
			B.Site(TEXT("A Office"), FVector(-1000.0f, 575.0f, 150.0f), FVector(470.0f, 400.0f, 150.0f));
			B.Site(TEXT("B Server"), FVector(1000.0f, 575.0f, 150.0f), FVector(470.0f, 400.0f, 150.0f));
		}

		// ===== spawns and AI points =====
		const FRotator FaceNorth(0.0f, 90.0f, 0.0f);
		for (int32 i = 0; i < 5; ++i)
		{
			Out.AttackerSpawns.Add(FTransform(FaceNorth, FVector(-600.0f + i * 300.0f, -2400.0f, 100.0f)));
		}

		const FRotator FaceSouth(0.0f, -90.0f, 0.0f);
		const float BF = BZ + 100.0f;
		if (bBasementSites)
		{
			const FRotator FaceCorridor(0.0f, 90.0f, 0.0f);
			Out.DefenderSpawns.Add(FTransform(FaceCorridor, FVector(-1100.0f, -700.0f, BF)));
			Out.DefenderSpawns.Add(FTransform(FaceCorridor, FVector(-800.0f, -350.0f, BF)));
			Out.DefenderSpawns.Add(FTransform(FaceCorridor, FVector(0.0f, -650.0f, BF)));
			Out.DefenderSpawns.Add(FTransform(FaceCorridor, FVector(800.0f, -350.0f, BF)));
			Out.DefenderSpawns.Add(FTransform(FaceCorridor, FVector(1250.0f, -350.0f, BF)));

			Out.DefenderPostsA = { FVector(-1350.0f, -880.0f, BF), FVector(-650.0f, -880.0f, BF), FVector(-1350.0f, -300.0f, BF), FVector(-650.0f, -300.0f, BF), FVector(-200.0f, -850.0f, BF), FVector(-1150.0f, -800.0f, 100.0f) };
			Out.DefenderPostsB = { FVector(1350.0f, -880.0f, BF), FVector(650.0f, -880.0f, BF), FVector(1350.0f, -250.0f, BF), FVector(650.0f, -300.0f, BF), FVector(200.0f, -850.0f, BF), FVector(1100.0f, -700.0f, 100.0f) };
		}
		else
		{
			Out.DefenderSpawns.Add(FTransform(FaceSouth, FVector(-1100.0f, 700.0f, 100.0f)));
			Out.DefenderSpawns.Add(FTransform(FaceSouth, FVector(-800.0f, 450.0f, 100.0f)));
			Out.DefenderSpawns.Add(FTransform(FaceSouth, FVector(0.0f, 600.0f, 100.0f)));
			Out.DefenderSpawns.Add(FTransform(FaceSouth, FVector(800.0f, 450.0f, 100.0f)));
			Out.DefenderSpawns.Add(FTransform(FaceSouth, FVector(1100.0f, 700.0f, 100.0f)));

			Out.DefenderPostsA = { FVector(-1350.0f, 880.0f, 100.0f), FVector(-650.0f, 880.0f, 100.0f), FVector(-1350.0f, 300.0f, 100.0f), FVector(-650.0f, 320.0f, 100.0f), FVector(-200.0f, 850.0f, 100.0f), FVector(-1000.0f, 750.0f, 420.0f) };
			Out.DefenderPostsB = { FVector(1350.0f, 880.0f, 100.0f), FVector(650.0f, 880.0f, 100.0f), FVector(1350.0f, 300.0f, 100.0f), FVector(650.0f, 320.0f, 100.0f), FVector(200.0f, 850.0f, 100.0f), FVector(1000.0f, 400.0f, 420.0f) };
		}

		Out.RoamPoints = { FVector(-1200.0f, 0.0f, 100.0f), FVector(1200.0f, 0.0f, 100.0f), FVector(-1000.0f, -600.0f, 100.0f), FVector(1000.0f, -650.0f, 100.0f), FVector(250.0f, -650.0f, 100.0f), FVector(-1000.0f, 575.0f, 420.0f), FVector(0.0f, 575.0f, 420.0f), FVector(1000.0f, 575.0f, 420.0f),
			FVector(-1000.0f, 0.0f, BF), FVector(1250.0f, 0.0f, BF), FVector(0.0f, -650.0f, BF) };

		Out.EntryPoints = { FVector(0.0f, -1300.0f, 100.0f), FVector(-1900.0f, 0.0f, 100.0f), FVector(1900.0f, 0.0f, 100.0f), FVector(-1000.0f, -1300.0f, 100.0f), FVector(1000.0f, -1300.0f, 100.0f), FVector(380.0f, -1850.0f, 100.0f) };

		// ===== drone routes =====
		// small floor-level holes only drones fit through
		for (const FVector2D& Hole : {
			FVector2D(-700.0f, -150.0f), FVector2D(700.0f, -150.0f),		// kitchen / storage <-> hallway
			FVector2D(-700.0f, 150.0f), FVector2D(1300.0f, 150.0f),		// hallway <-> sites
			FVector2D(-500.0f, 300.0f), FVector2D(500.0f, 850.0f),			// sites <-> lounge
			FVector2D(-200.0f, -1000.0f), FVector2D(-1300.0f, -1000.0f),	// south facade
			FVector2D(-1500.0f, 850.0f), FVector2D(1300.0f, 1000.0f) })	// site exteriors
		{
			B.DroneHole(Hole.X, Hole.Y, 0.0f);
		}

		// basement: corridor into the vault and the generator room
		B.DroneHole(-700.0f, -150.0f, BZ);
		B.DroneHole(700.0f, -150.0f, BZ);

		// hidden drone-only tunnels into the building
		B.DroneTunnel(FVector2D(2250.0f, 700.0f), FVector2D(1515.0f, 700.0f), TEXT("Storm Drain"));		// east yard -> site B through the concrete wall
		B.DroneTunnel(FVector2D(320.0f, 1760.0f), FVector2D(320.0f, 1015.0f), TEXT("Lounge Vent"));		// behind the north cover -> lounge
		B.DroneTunnel(FVector2D(-2050.0f, -700.0f), FVector2D(-1515.0f, -700.0f), TEXT("Kitchen Duct"));	// west yard -> kitchen
		B.DroneHole(320.0f, 1000.0f, 0.0f);
		B.DroneHole(-1500.0f, -700.0f, 0.0f);

		// ===== rappel points on the north facade, under the tall upstairs windows =====
		for (float RopeX : { -1000.0f, 0.0f, 1000.0f })
		{
			B.Rope(FVector(RopeX, 1012.0f, 0.0f), 90.0f, UZ + H + 20.0f);
		}

		// ===== defender security cameras =====
		B.SecurityCamera(TEXT("Cam: West Hall"), FVector(-1470.0f, 0.0f, 270.0f), 0.0f);
		B.SecurityCamera(TEXT("Cam: East Hall"), FVector(1470.0f, 0.0f, 270.0f), 180.0f);
		B.SecurityCamera(TEXT("Cam: Lobby"), FVector(-470.0f, -970.0f, 270.0f), 45.0f);
		B.SecurityCamera(TEXT("Cam: Lounge"), FVector(470.0f, 970.0f, 270.0f), -135.0f);
		B.SecurityCamera(TEXT("Cam: Front Yard"), FVector(1000.0f, -1035.0f, 280.0f), -90.0f);
		B.SecurityCamera(TEXT("Cam: Upstairs"), FVector(-530.0f, 970.0f, 590.0f), -135.0f);
		B.SecurityCamera(TEXT("Cam: Basement"), FVector(-1470.0f, 0.0f, BZ + 270.0f), 0.0f);
		B.SecurityCamera(TEXT("Cam: Cellar"), FVector(470.0f, -170.0f, BZ + 270.0f), -135.0f);
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
