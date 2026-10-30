// Copyright Epic Games, Inc. All Rights Reserved.

#include "SiegeGameState.h"
#include "Seige.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

float ASiegeGameState::GetPhaseTimeRemaining() const
{
	return FMath::Max(0.0f, PhaseEndTime - GetServerWorldTimeSeconds());
}

void ASiegeGameState::AddMessage(uint8 Team, const FString& Text, APawn* OnlyFor)
{
	// several gadgets can report the same thing at once; drop repeats
	for (const FSiegeMessage& Existing : Messages)
	{
		if (Existing.Team == Team && Existing.Text == Text && GetServerWorldTimeSeconds() - Existing.Time < 2.0f)
		{
			return;
		}
	}

	UE_LOG(LogSeige, Log, TEXT("Message [team %d]: %s"), Team, *Text);

	FSiegeMessage Message;
	Message.Team = Team;
	Message.Text = Text;
	Message.OnlyFor = OnlyFor;
	Message.Time = GetServerWorldTimeSeconds();

	Messages.Add(Message);

	// keep the list short
	if (Messages.Num() > 12)
	{
		Messages.RemoveAt(0, Messages.Num() - 12);
	}
}

void ASiegeGameState::AddKill(const FSiegeKillFeedEntry& Entry)
{
	KillFeed.Add(Entry);

	if (KillFeed.Num() > 8)
	{
		KillFeed.RemoveAt(0, KillFeed.Num() - 8);
	}
}

void ASiegeGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASiegeGameState, Phase);
	DOREPLIFETIME(ASiegeGameState, PhaseEndTime);
	DOREPLIFETIME(ASiegeGameState, RoundNumber);
	DOREPLIFETIME(ASiegeGameState, ScoreTeam0);
	DOREPLIFETIME(ASiegeGameState, ScoreTeam1);
	DOREPLIFETIME(ASiegeGameState, AttackingTeam);
	DOREPLIFETIME(ASiegeGameState, RoundsToWin);
	DOREPLIFETIME(ASiegeGameState, LastRoundWinner);
	DOREPLIFETIME(ASiegeGameState, LastRoundReason);
	DOREPLIFETIME(ASiegeGameState, PlantedSite);
}
