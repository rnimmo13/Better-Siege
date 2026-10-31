// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SiegeWall.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class ASiegeGadgetBase;

UENUM(BlueprintType)
enum class ESiegeWallKind : uint8
{
	/** Ordinary destructible wall panel. Can be reinforced by defenders */
	Soft,
	/** Thin wooden door or window barricade. Breaks quickly, cannot be reinforced */
	Barricade,
	/** Destructible floor or ceiling panel */
	Floor,
	/** Weak floor cover that can be smashed open to drop through, or reinforced by defenders */
	Hatch
};

/**
 *  A destructible surface built from a grid of small cells.
 *  Bullets chip individual cells, explosives open holes, reinforced panels only yield to hard breaching.
 *  The panel is centered on the actor: local X is thickness, Y is width and Z is height.
 */
UCLASS()
class SEIGE_API ASiegeWall : public AActor
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UInstancedStaticMeshComponent> Cells;

protected:

	UPROPERTY(EditAnywhere, Category="Wall")
	ESiegeWallKind Kind = ESiegeWallKind::Soft;

	UPROPERTY(EditAnywhere, Category="Wall", meta=(Units="cm"))
	float Width = 200.0f;

	UPROPERTY(EditAnywhere, Category="Wall", meta=(Units="cm"))
	float Height = 300.0f;

	UPROPERTY(EditAnywhere, Category="Wall", meta=(Units="cm"))
	float Thickness = 20.0f;

	UPROPERTY(EditAnywhere, Category="Wall", meta=(Units="cm"))
	float CellSize = 25.0f;

	/** Health of each individual cell against bullets and melee */
	UPROPERTY(EditAnywhere, Category="Wall")
	float CellHealth = 100.0f;

	/** If true, defenders may reinforce this panel */
	UPROPERTY(EditAnywhere, Category="Wall")
	bool bReinforceable = true;

	UPROPERTY(VisibleAnywhere, Category="Wall")
	bool bReinforced = false;

	UPROPERTY(EditAnywhere, Category="Materials")
	TObjectPtr<UMaterialInterface> SoftMaterial;

	UPROPERTY(EditAnywhere, Category="Materials")
	TObjectPtr<UMaterialInterface> ReinforcedMaterial;

	UPROPERTY(EditAnywhere, Category="Materials")
	TObjectPtr<UMaterialInterface> BarricadeMaterial;

	/** Remaining health for each cell, indexed [Row * NumCols + Col]. Zero means destroyed */
	TArray<float> CellHP;

	/** Maps ISM instance indices back to cell indices */
	TArray<int32> InstanceToCell;

	int32 NumCols = 0;
	int32 NumRows = 0;

	/** Cells removed at build time for drone holes; they don't count as damage */
	int32 PreCutCells = 0;

	/** Number of active shock boxes attached to this panel */
	int32 ElectrifiedCount = 0;

	/** Display name of the room this panel faces, for callouts */
	FString RoomLabel;

public:

	ASiegeWall();

	/** Sets up the panel dimensions and builds the cells. Call right after spawning */
	void InitWall(ESiegeWallKind InKind, float InWidth, float InHeight, float InThickness, bool bInReinforceable, const FString& InRoomLabel = FString());

	/** Handles bullet and melee damage. Point damage events chip the cell that was hit */
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Removes every cell within Radius of the world location. Reinforced panels ignore this unless bHardBreach */
	void ApplyExplosion(const FVector& WorldLocation, float Radius, bool bHardBreach);

	/** Destroys every cell on the panel */
	void DestroyAllCells();

	/**
	 *  Cuts a small floor-level drone hole at the world location if it lies on this panel.
	 *  Only drones fit through. Returns true if a hole was cut.
	 */
	bool CutDroneHoleAt(const FVector& WorldLocation, float HoleWidth = 50.0f, float HoleHeight = 50.0f);

	/** Removes every cell whose center lies in a rectangle (panel width x height axes) around the world location */
	bool CutRectAt(const FVector& WorldCenter, float SizeAlongWidth, float SizeAlongHeight);

	bool IsHatch() const { return Kind == ESiegeWallKind::Hatch; }

	/** True for floor and hatch panels */
	bool IsHorizontal() const { return Kind == ESiegeWallKind::Floor || Kind == ESiegeWallKind::Hatch; }

	/** Reinforces this panel. Returns false if it can't be reinforced */
	bool Reinforce();

	bool CanBeReinforced() const;
	bool IsReinforced() const { return bReinforced; }
	ESiegeWallKind GetKind() const { return Kind; }
	bool IsBarricade() const { return Kind == ESiegeWallKind::Barricade; }

	/** Fraction of cells still standing */
	float GetIntegrity() const;

	/** Damage multiplier applied to bullets that pass through this panel. Zero if bullets are stopped */
	float GetPenetrationFactor() const;

	void AddElectrifier() { ++ElectrifiedCount; }
	void RemoveElectrifier() { ElectrifiedCount = FMath::Max(0, ElectrifiedCount - 1); }
	bool IsElectrified() const { return ElectrifiedCount > 0; }

	const FString& GetRoomLabel() const { return RoomLabel; }

protected:

	void RebuildInstances();
	void UpdateMaterial();

	/** Returns the local-space center of a cell */
	FVector GetCellLocalCenter(int32 CellIndex) const;
};
