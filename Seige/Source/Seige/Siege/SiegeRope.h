// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SiegeRope.generated.h"

class UStaticMeshComponent;

/**
 *  A rappel point on an exterior wall. Attackers standing at its foot can attach and climb the facade.
 *  The actor sits on the wall face at ground level; its forward vector points away from the building.
 */
UCLASS()
class SEIGE_API ASiegeRopeAnchor : public AActor
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> RopeMesh;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> HookMesh;

public:

	/** Height of the top of the rope above the anchor's base */
	UPROPERTY(EditAnywhere, Category="Rope", meta=(Units="cm"))
	float Height = 640.0f;

	/** How far left or right along the wall an attached operator can move */
	UPROPERTY(EditAnywhere, Category="Rope", meta=(Units="cm"))
	float HalfWidth = 170.0f;

	ASiegeRopeAnchor();

	/** Sets the rope height and refreshes the visuals */
	void InitRope(float InHeight);

	/** Returns true if an operator whose feet are at this location can attach. Outputs the starting rope position */
	bool CanAttach(const FVector& FeetLocation, float& OutAlong, float& OutHeight) const;

	/** World location of an operator's capsule center hanging at a rope position */
	FVector GetHangLocation(float Along, float HeightOnRope, float CapsuleHalfHeight) const;

	FVector GetOutward() const { return GetActorForwardVector(); }
	FVector GetAlongDirection() const { return GetActorRightVector(); }
};
