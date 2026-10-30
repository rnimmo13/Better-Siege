// Copyright Epic Games, Inc. All Rights Reserved.

#include "SiegePlayerState.h"
#include "Net/UnrealNetwork.h"

void ASiegePlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASiegePlayerState, Team);
	DOREPLIFETIME(ASiegePlayerState, OperatorIndex);
	DOREPLIFETIME(ASiegePlayerState, Kills);
	DOREPLIFETIME(ASiegePlayerState, Deaths);
	DOREPLIFETIME(ASiegePlayerState, Plants);
}
