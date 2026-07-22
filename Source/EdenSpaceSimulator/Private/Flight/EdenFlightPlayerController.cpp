// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/EdenFlightPlayerController.h"

#include "Core/EdenLogCategories.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Flight/EdenFlightMovementModel.h"
#include "Flight/EdenSpacecraftPawn.h"
#include "InputAction.h"
#include "InputActionValue.h"

AEdenFlightPlayerController::AEdenFlightPlayerController()
{
	bShowMouseCursor = false;
}

void AEdenFlightPlayerController::BeginPlay()
{
	Super::BeginPlay();

	ClearFlightInputIntent();
	AddFlightInputMappingContext();
	LogMissingInputAssetState();
}

void AEdenFlightPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	AEdenSpacecraftPawn* SpacecraftPawn = GetPawn<AEdenSpacecraftPawn>();
	if (!SpacecraftPawn)
	{
		if (GetPawn() && !bLoggedUnexpectedPawnState)
		{
			UE_LOG(
				LogEdenFlight,
				Warning,
				TEXT("%s possesses '%s', not an AEdenSpacecraftPawn. Flight commands will not be forwarded."),
				*GetNameSafe(this),
				*GetNameSafe(GetPawn()));
			bLoggedUnexpectedPawnState = true;
		}
		return;
	}

	bLoggedUnexpectedPawnState = false;
	SpacecraftPawn->ApplyFlightInputCommand(GetCurrentFlightInputCommand(), DeltaTime);
}

void AEdenFlightPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	BindFlightInputActions();
}

void AEdenFlightPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	ClearFlightInputIntent();

	if (AEdenSpacecraftPawn* SpacecraftPawn = Cast<AEdenSpacecraftPawn>(InPawn))
	{
		SpacecraftPawn->ResetFlightState();
	}
}

void AEdenFlightPlayerController::OnUnPossess()
{
	ClearFlightInputIntent();
	Super::OnUnPossess();
}

void AEdenFlightPlayerController::Reset()
{
	ClearFlightInputIntent();

	if (AEdenSpacecraftPawn* SpacecraftPawn = GetPawn<AEdenSpacecraftPawn>())
	{
		SpacecraftPawn->ResetFlightState();
	}

	Super::Reset();
}

void AEdenFlightPlayerController::ClearFlightInputIntent()
{
	FlightInputIntent.Reset();
}

FEdenFlightInputCommand AEdenFlightPlayerController::GetCurrentFlightInputCommand() const
{
	return FEdenFlightMovementModel::SanitizeCommand(FlightInputIntent.CurrentCommand);
}

void AEdenFlightPlayerController::AddFlightInputMappingContext()
{
	if (!FlightInputMappingContext)
	{
		return;
	}

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (!LocalPlayer)
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s cannot add flight input mapping context without a local player."), *GetNameSafe(this));
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!InputSubsystem)
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s cannot find Enhanced Input local player subsystem."), *GetNameSafe(this));
		return;
	}

	InputSubsystem->AddMappingContext(FlightInputMappingContext, FlightInputMappingPriority);
}

void AEdenFlightPlayerController::BindFlightInputActions()
{
	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EnhancedInputComponent)
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s requires EnhancedInputComponent for flight controls."), *GetNameSafe(this));
		return;
	}

	if (FlightTranslateAction)
	{
		EnhancedInputComponent->BindAction(FlightTranslateAction, ETriggerEvent::Triggered, this, &AEdenFlightPlayerController::HandleTranslateInput);
		EnhancedInputComponent->BindAction(FlightTranslateAction, ETriggerEvent::Completed, this, &AEdenFlightPlayerController::HandleTranslateReleased);
		EnhancedInputComponent->BindAction(FlightTranslateAction, ETriggerEvent::Canceled, this, &AEdenFlightPlayerController::HandleTranslateReleased);
	}

	if (FlightRotateAction)
	{
		EnhancedInputComponent->BindAction(FlightRotateAction, ETriggerEvent::Triggered, this, &AEdenFlightPlayerController::HandleRotateInput);
		EnhancedInputComponent->BindAction(FlightRotateAction, ETriggerEvent::Completed, this, &AEdenFlightPlayerController::HandleRotateReleased);
		EnhancedInputComponent->BindAction(FlightRotateAction, ETriggerEvent::Canceled, this, &AEdenFlightPlayerController::HandleRotateReleased);
	}

	if (FlightStabilizeAction)
	{
		EnhancedInputComponent->BindAction(FlightStabilizeAction, ETriggerEvent::Started, this, &AEdenFlightPlayerController::HandleStabilizeStarted);
	}

	LogMissingInputAssetState();
}

void AEdenFlightPlayerController::SetTranslationIntent(FVector NewTranslationInput)
{
	FEdenFlightInputCommand Command = FlightInputIntent.CurrentCommand;
	Command.TranslationInput = NewTranslationInput;
	FlightInputIntent.CurrentCommand = FEdenFlightMovementModel::SanitizeCommand(Command);
}

void AEdenFlightPlayerController::SetRotationIntent(FVector NewRotationInput)
{
	FEdenFlightInputCommand Command = FlightInputIntent.CurrentCommand;
	Command.RotationInput = NewRotationInput;
	FlightInputIntent.CurrentCommand = FEdenFlightMovementModel::SanitizeCommand(Command);
}

void AEdenFlightPlayerController::HandleTranslateInput(const FInputActionValue& Value)
{
	SetTranslationIntent(Value.Get<FVector>());
}

void AEdenFlightPlayerController::HandleTranslateReleased(const FInputActionValue& Value)
{
	(void)Value;
	SetTranslationIntent(FVector::ZeroVector);
}

void AEdenFlightPlayerController::HandleRotateInput(const FInputActionValue& Value)
{
	SetRotationIntent(Value.Get<FVector>());
}

void AEdenFlightPlayerController::HandleRotateReleased(const FInputActionValue& Value)
{
	(void)Value;
	SetRotationIntent(FVector::ZeroVector);
}

void AEdenFlightPlayerController::HandleStabilizeStarted(const FInputActionValue& Value)
{
	(void)Value;

	FEdenFlightInputCommand Command = FlightInputIntent.CurrentCommand;
	Command.bStabilizationEnabled = !Command.bStabilizationEnabled;
	FlightInputIntent.CurrentCommand = FEdenFlightMovementModel::SanitizeCommand(Command);

	UE_LOG(
		LogEdenFlight,
		Verbose,
		TEXT("%s set flight stabilization to %s."),
		*GetNameSafe(this),
		FlightInputIntent.CurrentCommand.bStabilizationEnabled ? TEXT("enabled") : TEXT("disabled"));
}

void AEdenFlightPlayerController::LogMissingInputAssetState()
{
	if (bLoggedMissingInputAssetState)
	{
		return;
	}

	if (!FlightInputMappingContext || !FlightTranslateAction || !FlightRotateAction || !FlightStabilizeAction)
	{
		UE_LOG(
			LogEdenFlight,
			Warning,
			TEXT("%s is missing one or more flight input assets. Assign IMC_Flight, IA_FlightTranslate, IA_FlightRotate, and IA_FlightStabilize in BP_EdenFlightPlayerController."),
			*GetNameSafe(this));
		bLoggedMissingInputAssetState = true;
	}
}
