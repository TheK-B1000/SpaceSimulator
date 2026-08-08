// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/EdenFlightPlayerController.h"

#include "Core/EdenLogCategories.h"
#include "Core/EdenSimulationClockSubsystem.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Flight/EdenFlightMovementModel.h"
#include "Flight/EdenSpacecraftPawn.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Missions/EdenMissionDefinitionDataAsset.h"
#include "Missions/EdenMissionSubsystem.h"
#include "Operations/EdenAlertSubsystem.h"
#include "Operations/EdenOperatorControlComponent.h"
#include "Operations/EdenOperatorHudWidget.h"
#include "Telemetry/EdenAfterActionReviewWidget.h"
#include "Telemetry/EdenTelemetrySubsystem.h"
#include "Systems/EdenFuelSystemComponent.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalSystemComponent.h"
#include "UObject/SoftObjectPath.h"

namespace EdenFlightPlayerControllerMission
{
	static const TCHAR* DefaultSolarEventAssetPath = TEXT("/Game/Eden/Data/Missions/DA_SolarEventEmergency.DA_SolarEventEmergency");

	UEdenMissionDefinitionDataAsset* ResolveDefaultMissionAsset(AEdenFlightPlayerController* Controller)
	{
		if (!Controller)
		{
			return nullptr;
		}

		if (UEdenMissionDefinitionDataAsset* Loaded = Controller->DefaultMissionDefinitionAsset.LoadSynchronous())
		{
			return Loaded;
		}

		const FSoftObjectPath FallbackPath(DefaultSolarEventAssetPath);
		return Cast<UEdenMissionDefinitionDataAsset>(FallbackPath.TryLoad());
	}
}

AEdenFlightPlayerController::AEdenFlightPlayerController()
{
	bShowMouseCursor = false;
	DefaultMissionDefinitionAsset = TSoftObjectPtr<UEdenMissionDefinitionDataAsset>(
		FSoftObjectPath(EdenFlightPlayerControllerMission::DefaultSolarEventAssetPath));
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

	OperatorHudRefreshAccumulatorSeconds += DeltaTime;
	const float RefreshIntervalSeconds = OperatorHudRefreshHz > KINDA_SMALL_NUMBER ? (1.0f / OperatorHudRefreshHz) : 0.1f;
	if (OperatorHudRefreshAccumulatorSeconds >= RefreshIntervalSeconds)
	{
		OperatorHudRefreshAccumulatorSeconds = 0.0f;
		RefreshOperatorHudSnapshot();
	}
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

	EnsureOperatorHudCreated();
	RefreshOperatorHudSnapshot();
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

	if (ThermalModeAction)
	{
		EnhancedInputComponent->BindAction(ThermalModeAction, ETriggerEvent::Started, this, &AEdenFlightPlayerController::HandleThermalModeStarted);
	}

	if (LoadShedAction)
	{
		EnhancedInputComponent->BindAction(LoadShedAction, ETriggerEvent::Started, this, &AEdenFlightPlayerController::HandleLoadShedStarted);
	}

	if (PropulsionPriorityAction)
	{
		EnhancedInputComponent->BindAction(
			PropulsionPriorityAction,
			ETriggerEvent::Started,
			this,
			&AEdenFlightPlayerController::HandlePropulsionPriorityStarted);
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

void AEdenFlightPlayerController::HandleThermalModeStarted(const FInputActionValue& Value)
{
	(void)Value;
	if (AEdenSpacecraftPawn* SpacecraftPawn = GetPawn<AEdenSpacecraftPawn>())
	{
		if (UEdenOperatorControlComponent* Operator = SpacecraftPawn->GetOperatorControlComponent())
		{
			Operator->CycleThermalControlMode();
		}
	}
}

void AEdenFlightPlayerController::HandleLoadShedStarted(const FInputActionValue& Value)
{
	(void)Value;
	if (AEdenSpacecraftPawn* SpacecraftPawn = GetPawn<AEdenSpacecraftPawn>())
	{
		if (UEdenOperatorControlComponent* Operator = SpacecraftPawn->GetOperatorControlComponent())
		{
			Operator->ToggleLoadShedMode();
		}
	}
}

void AEdenFlightPlayerController::HandlePropulsionPriorityStarted(const FInputActionValue& Value)
{
	(void)Value;
	if (AEdenSpacecraftPawn* SpacecraftPawn = GetPawn<AEdenSpacecraftPawn>())
	{
		if (UEdenOperatorControlComponent* Operator = SpacecraftPawn->GetOperatorControlComponent())
		{
			Operator->TogglePropulsionPriorityMode();
		}
	}
}

FEdenOperatorHudSnapshot AEdenFlightPlayerController::GetOperatorHudSnapshot() const
{
	return CachedOperatorHudSnapshot;
}

void AEdenFlightPlayerController::EnsureOperatorHudCreated()
{
	if (OperatorHudWidget || !OperatorHudWidgetClass)
	{
		return;
	}

	OperatorHudWidget = CreateWidget<UEdenOperatorHudWidget>(this, OperatorHudWidgetClass);
	if (OperatorHudWidget)
	{
		OperatorHudWidget->AddToViewport(10);
	}
}

void AEdenFlightPlayerController::RefreshOperatorHudSnapshot()
{
	CachedOperatorHudSnapshot = AssembleOperatorHudSnapshot();
	EnsureOperatorHudCreated();
	if (OperatorHudWidget)
	{
		OperatorHudWidget->ApplyHudSnapshot(CachedOperatorHudSnapshot);
	}
}

FEdenOperatorHudSnapshot AEdenFlightPlayerController::AssembleOperatorHudSnapshot() const
{
	FEdenMissionStateSnapshot MissionSnapshot;
	FEdenFuelStateSnapshot FuelSnapshot;
	FEdenPowerStateSnapshot PowerSnapshot;
	FEdenThermalStateSnapshot ThermalSnapshot;
	FEdenOperatorStateSnapshot OperatorSnapshot;
	TArray<FEdenAlert> Alerts;
	float SimTimeSeconds = 0.0f;

	if (const UWorld* World = GetWorld())
	{
		if (const UEdenMissionSubsystem* Mission = World->GetSubsystem<UEdenMissionSubsystem>())
		{
			MissionSnapshot = Mission->GetMissionStateSnapshot();
		}
		if (const UEdenAlertSubsystem* AlertsSubsystem = World->GetSubsystem<UEdenAlertSubsystem>())
		{
			Alerts = AlertsSubsystem->GetActiveAlerts();
		}
		if (const UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>())
		{
			SimTimeSeconds = Clock->GetElapsedSimulationTimeSeconds();
		}
	}

	if (const AEdenSpacecraftPawn* SpacecraftPawn = GetPawn<AEdenSpacecraftPawn>())
	{
		if (const UEdenFuelSystemComponent* Fuel = SpacecraftPawn->GetFuelSystemComponent())
		{
			FuelSnapshot = Fuel->GetFuelStateSnapshot();
		}
		if (const UEdenPowerSystemComponent* Power = SpacecraftPawn->GetPowerSystemComponent())
		{
			PowerSnapshot = Power->GetPowerStateSnapshot();
		}
		if (const UEdenThermalSystemComponent* Thermal = SpacecraftPawn->GetThermalSystemComponent())
		{
			ThermalSnapshot = Thermal->GetThermalStateSnapshot();
		}
		if (const UEdenOperatorControlComponent* Operator = SpacecraftPawn->GetOperatorControlComponent())
		{
			OperatorSnapshot = Operator->GetOperatorStateSnapshot();
		}
	}

	return FEdenOperatorHudModel::Assemble(
		MissionSnapshot,
		FuelSnapshot,
		PowerSnapshot,
		ThermalSnapshot,
		OperatorSnapshot,
		Alerts,
		SimTimeSeconds);
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

void AEdenFlightPlayerController::StartMission()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UEdenMissionSubsystem* MissionSubsystem = World->GetSubsystem<UEdenMissionSubsystem>();
	if (!MissionSubsystem)
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s StartMission: UEdenMissionSubsystem not found in world."), *GetNameSafe(this));
		return;
	}

	if (MissionSubsystem->GetMissionState() == EEdenMissionState::Inactive)
	{
		UEdenMissionDefinitionDataAsset* MissionAsset = EdenFlightPlayerControllerMission::ResolveDefaultMissionAsset(this);
		if (!MissionAsset)
		{
			UE_LOG(
				LogEdenFlight,
				Warning,
				TEXT("%s StartMission: default mission Data Asset is unavailable. Assign DefaultMissionDefinitionAsset or create DA_SolarEventEmergency."),
				*GetNameSafe(this));
			return;
		}

		if (!MissionSubsystem->LoadMissionFromDefinitionAsset(MissionAsset))
		{
			return;
		}
	}

	if (MissionSubsystem->GetMissionState() == EEdenMissionState::Ready)
	{
		MissionSubsystem->StartMission();
	}
}

void AEdenFlightPlayerController::RestartMission()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UEdenMissionSubsystem* MissionSubsystem = World->GetSubsystem<UEdenMissionSubsystem>();
	if (!MissionSubsystem)
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s RestartMission: UEdenMissionSubsystem not found in world."), *GetNameSafe(this));
		return;
	}

	if (MissionSubsystem->GetMissionState() == EEdenMissionState::Running)
	{
		MissionSubsystem->AbortMission();
	}

	MissionSubsystem->ResetMission();

	UEdenMissionDefinitionDataAsset* MissionAsset = EdenFlightPlayerControllerMission::ResolveDefaultMissionAsset(this);
	if (!MissionAsset)
	{
		UE_LOG(
			LogEdenFlight,
			Warning,
			TEXT("%s RestartMission: default mission Data Asset is unavailable."),
			*GetNameSafe(this));
		return;
	}

	if (!MissionSubsystem->LoadMissionFromDefinitionAsset(MissionAsset))
	{
		return;
	}

	MissionSubsystem->StartMission();
}

void AEdenFlightPlayerController::AbortMission()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UEdenMissionSubsystem* MissionSubsystem = World->GetSubsystem<UEdenMissionSubsystem>();
	if (MissionSubsystem)
	{
		MissionSubsystem->AbortMission();
	}
}

void AEdenFlightPlayerController::ExportTelemetry()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UEdenTelemetrySubsystem* Telemetry = World->GetSubsystem<UEdenTelemetrySubsystem>();
	if (!Telemetry)
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s ExportTelemetry: UEdenTelemetrySubsystem not found."), *GetNameSafe(this));
		return;
	}

	const FString Path = Telemetry->WriteSessionJsonV1ToDisk();
	if (Path.IsEmpty())
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s ExportTelemetry failed."), *GetNameSafe(this));
		return;
	}

	UE_LOG(LogEdenFlight, Log, TEXT("%s ExportTelemetry wrote '%s'."), *GetNameSafe(this), *Path);
}

void AEdenFlightPlayerController::ShowAfterAction()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UEdenTelemetrySubsystem* Telemetry = World->GetSubsystem<UEdenTelemetrySubsystem>();
	if (!Telemetry)
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s ShowAfterAction: UEdenTelemetrySubsystem not found."), *GetNameSafe(this));
		return;
	}

	EnsureAfterActionReviewCreated();
	if (!AfterActionReviewWidget)
	{
		UE_LOG(
			LogEdenFlight,
			Warning,
			TEXT("%s ShowAfterAction: AfterActionReviewWidgetClass is unset. Assign WBP_EdenAfterActionReview."),
			*GetNameSafe(this));
		return;
	}

	const FEdenAfterActionResult Result = Telemetry->BuildAfterActionResult();
	AfterActionReviewWidget->ApplyAfterActionResult(Result);
	if (!AfterActionReviewWidget->IsInViewport())
	{
		AfterActionReviewWidget->AddToViewport(20);
	}
	AfterActionReviewWidget->SetVisibility(ESlateVisibility::Visible);
}

void AEdenFlightPlayerController::EnsureAfterActionReviewCreated()
{
	if (AfterActionReviewWidget || !AfterActionReviewWidgetClass)
	{
		return;
	}

	AfterActionReviewWidget = CreateWidget<UEdenAfterActionReviewWidget>(this, AfterActionReviewWidgetClass);
}
