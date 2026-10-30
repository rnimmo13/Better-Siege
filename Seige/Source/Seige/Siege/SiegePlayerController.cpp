// Copyright Epic Games, Inc. All Rights Reserved.

#include "SiegePlayerController.h"
#include "SiegeOperator.h"
#include "SiegeGameMode.h"
#include "SiegePlayerState.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ASiegePlayerController::ASiegePlayerController()
{
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> DefaultIMC(TEXT("/Game/Input/IMC_Default.IMC_Default"));
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> MouseLookIMC(TEXT("/Game/Input/IMC_MouseLook.IMC_MouseLook"));
	static ConstructorHelpers::FObjectFinder<UInputMappingContext> WeaponsIMC(TEXT("/Game/Variant_Shooter/Input/IMC_Weapons.IMC_Weapons"));

	if (DefaultIMC.Succeeded())
	{
		DefaultMappingContexts.Add(DefaultIMC.Object);
	}

	if (WeaponsIMC.Succeeded())
	{
		DefaultMappingContexts.Add(WeaponsIMC.Object);
	}

	if (MouseLookIMC.Succeeded())
	{
		MobileExcludedMappingContexts.Add(MouseLookIMC.Object);
	}
}

void ASiegePlayerController::EnsureSiegeInput()
{
	if (SiegeContext)
	{
		return;
	}

	SiegeContext = NewObject<UInputMappingContext>(this, TEXT("IMC_SiegeRuntime"));

	auto MakeAction = [this](FName Name, std::initializer_list<FKey> Keys)
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = EInputActionValueType::Boolean;
		SiegeActions.Add(Name, Action);

		for (const FKey& Key : Keys)
		{
			SiegeContext->MapKey(Action, Key);
		}
	};

	MakeAction(TEXT("LeanLeft"), { EKeys::Q });
	MakeAction(TEXT("LeanRight"), { EKeys::E });
	MakeAction(TEXT("Crouch"), { EKeys::LeftControl, EKeys::C });
	MakeAction(TEXT("Sprint"), { EKeys::LeftShift });
	MakeAction(TEXT("Interact"), { EKeys::F });
	MakeAction(TEXT("Gadget"), { EKeys::G });
	MakeAction(TEXT("Secondary"), { EKeys::B });
	MakeAction(TEXT("Reload"), { EKeys::R });
	MakeAction(TEXT("Melee"), { EKeys::V });
	MakeAction(TEXT("Select1"), { EKeys::One });
	MakeAction(TEXT("Select2"), { EKeys::Two });
	MakeAction(TEXT("Select3"), { EKeys::Three });
	MakeAction(TEXT("Select4"), { EKeys::Four });
	MakeAction(TEXT("SpectateNext"), { EKeys::LeftMouseButton, EKeys::SpaceBar });
	MakeAction(TEXT("Scoreboard"), { EKeys::Tab });

	// spectating shares LMB and Space with Fire and Vault; let those keys reach the lower priority contexts too
	SiegeActions[TEXT("SpectateNext")]->bConsumeInput = false;
}

UInputAction* ASiegePlayerController::GetSiegeAction(FName ActionName)
{
	EnsureSiegeInput();

	const TObjectPtr<UInputAction>* Found = SiegeActions.Find(ActionName);
	return Found ? Found->Get() : nullptr;
}

void ASiegePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!IsLocalPlayerController())
	{
		return;
	}

	EnsureSiegeInput();

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(SiegeContext, 1);
	}

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EIC->BindAction(GetSiegeAction(TEXT("Select1")), ETriggerEvent::Started, this, &ASiegePlayerController::SelectOperator1);
		EIC->BindAction(GetSiegeAction(TEXT("Select2")), ETriggerEvent::Started, this, &ASiegePlayerController::SelectOperator2);
		EIC->BindAction(GetSiegeAction(TEXT("Select3")), ETriggerEvent::Started, this, &ASiegePlayerController::SelectOperator3);
		EIC->BindAction(GetSiegeAction(TEXT("Select4")), ETriggerEvent::Started, this, &ASiegePlayerController::SelectOperator4);
		EIC->BindAction(GetSiegeAction(TEXT("SpectateNext")), ETriggerEvent::Started, this, &ASiegePlayerController::SpectateNext);
		EIC->BindAction(GetSiegeAction(TEXT("Scoreboard")), ETriggerEvent::Started, this, &ASiegePlayerController::ShowScoreboard);
		EIC->BindAction(GetSiegeAction(TEXT("Scoreboard")), ETriggerEvent::Completed, this, &ASiegePlayerController::HideScoreboard);
	}
}

void ASiegePlayerController::SelectOperator(int32 Index)
{
	if (ASiegeGameMode* GM = GetWorld()->GetAuthGameMode<ASiegeGameMode>())
	{
		GM->RequestOperator(this, Index);
	}
}

void ASiegePlayerController::OnOperatorDied()
{
	// give the player a moment to see what happened before switching to a teammate's view
	GetWorldTimerManager().SetTimer(SpectateTimer, this, &ASiegePlayerController::SpectateNext, 2.5f, false);
}

void ASiegePlayerController::OnRoundStarted()
{
	GetWorldTimerManager().ClearTimer(SpectateTimer);
	SpectateTarget.Reset();

	if (GetPawn())
	{
		SetViewTarget(GetPawn());
	}
}

void ASiegePlayerController::SpectateNext()
{
	ASiegeOperator* MyOperator = Cast<ASiegeOperator>(GetPawn());

	// only spectate while our own operator is dead
	if (!MyOperator || !MyOperator->IsDead())
	{
		return;
	}

	const uint8 MyTeam = MyOperator->GetTeam();

	TArray<ASiegeOperator*> Candidates;
	for (TActorIterator<ASiegeOperator> It(GetWorld()); It; ++It)
	{
		if (*It != MyOperator && !It->IsDead() && It->GetTeam() == MyTeam)
		{
			Candidates.Add(*It);
		}
	}

	if (Candidates.Num() == 0)
	{
		return;
	}

	int32 Index = Candidates.Find(SpectateTarget.Get());
	Index = (Index + 1) % Candidates.Num();

	SpectateTarget = Candidates[Index];
	SetViewTargetWithBlend(SpectateTarget.Get(), 0.3f);
}
