// Copyright Epic Games, Inc. All Rights Reserved.

#include "Flight/EdenFlightPlayerController.h"

#include "Core/EdenLogCategories.h"
#include "Core/EdenSimulationClockSubsystem.h"
#include "EdenOs/EdenOsAdapterSubsystem.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Flight/EdenFlightMovementComponent.h"
#include "Flight/EdenFlightMovementModel.h"
#include "Flight/EdenSpacecraftPawn.h"
#include "HAL/PlatformMisc.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
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

namespace EdenFlightPlayerControllerEdenOs
{
	static const TCHAR* DefaultLiveBaseUrlEnvVar = TEXT("EDEN_OS_LIVE_E2E_BASE_URL");
	static const TCHAR* DefaultLiveBearerJwtEnvVar = TEXT("EDEN_OS_LIVE_E2E_BEARER_JWT");
}

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
	EnsureFlightInputAssetsLoaded();
	AddFlightInputMappingContext();
	ApplyFlightInputMode();
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
	AdvanceMouseStick(DeltaTime);

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
	EnsureFlightInputAssetsLoaded();
	BindFlightInputActions();
	// BP Event BeginPlay sometimes omits the parent call; map keys here too.
	AddFlightInputMappingContext();
}

void AEdenFlightPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	ClearFlightInputIntent();

	if (AEdenSpacecraftPawn* SpacecraftPawn = Cast<AEdenSpacecraftPawn>(InPawn))
	{
		SpacecraftPawn->ResetFlightState();
	}

	EnsureFlightInputAssetsLoaded();
	AddFlightInputMappingContext();
	ApplyFlightInputMode();
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
	MouseStickPitchYaw = FVector2D::ZeroVector;
	RollKeyInput = 0.0;
}

void AEdenFlightPlayerController::EdenCaptureScreenshot(float DelaySeconds, int32 QuitAfter)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FString Filename = FPaths::Combine(
		FPaths::ScreenShotDir(),
		FString::Printf(TEXT("EdenCapture_%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
	const bool bQuit = QuitAfter != 0;
	TWeakObjectPtr<AEdenFlightPlayerController> WeakThis(this);
	World->GetTimerManager().SetTimer(
		ScreenshotTimerHandle,
		FTimerDelegate::CreateLambda([WeakThis, Filename, bQuit]()
		{
			AEdenFlightPlayerController* Controller = WeakThis.Get();
			if (!Controller)
			{
				return;
			}
			FScreenshotRequest::RequestScreenshot(Filename, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
			UE_LOG(LogEdenFlight, Display, TEXT("EdenCaptureScreenshot requested %s"), *Filename);
			if (bQuit)
			{
				// Give the renderer a few frames to resolve and write the capture before quitting.
				Controller->GetWorldTimerManager().SetTimer(
					Controller->ScreenshotTimerHandle,
					FTimerDelegate::CreateWeakLambda(Controller, [Controller]() { Controller->ConsoleCommand(TEXT("quit")); }),
					2.0f,
					false);
			}
		}),
		FMath::Max(0.1f, DelaySeconds),
		false);
	UE_LOG(LogEdenFlight, Display, TEXT("EdenCaptureScreenshot scheduled in %.1fs -> %s%s"), FMath::Max(0.1f, DelaySeconds), *Filename, bQuit ? TEXT(" (then quit)") : TEXT(""));
}

void AEdenFlightPlayerController::EdenCenterMouseStick()
{
	MouseStickPitchYaw = FVector2D::ZeroVector;
	ApplyComposedRotationIntent();
}

void AEdenFlightPlayerController::AddRotationInputSample(const FVector& RotationAxes)
{
	if (!RotationAxes.ContainsNaN())
	{
		MouseStickPitchYaw = FEdenFlightPresentationModel::AccumulateMouseStick(
			MouseStickPitchYaw,
			FVector2D(RotationAxes.X, RotationAxes.Y),
			MouseStickSettings);
		RollKeyInput = FMath::Clamp(RotationAxes.Z, -1.0, 1.0);
	}
	ApplyComposedRotationIntent();
}

void AEdenFlightPlayerController::ReleaseRotationInput()
{
	RollKeyInput = 0.0;
	ApplyComposedRotationIntent();
}

void AEdenFlightPlayerController::AdvanceMouseStick(float DeltaSeconds)
{
	MouseStickPitchYaw = FEdenFlightPresentationModel::CenterMouseStick(MouseStickPitchYaw, MouseStickSettings, DeltaSeconds);
	ApplyComposedRotationIntent();
}

void AEdenFlightPlayerController::ApplyComposedRotationIntent()
{
	const FVector2D Shaped = FEdenFlightPresentationModel::ApplyStickDeadZone(MouseStickPitchYaw, MouseStickSettings);
	SetRotationIntent(FVector(Shaped.X, Shaped.Y, RollKeyInput));
}

FEdenFlightInputCommand AEdenFlightPlayerController::GetCurrentFlightInputCommand() const
{
	return FEdenFlightMovementModel::SanitizeCommand(FlightInputIntent.CurrentCommand);
}

void AEdenFlightPlayerController::EnsureFlightInputAssetsLoaded()
{
	if (!FlightInputMappingContext)
	{
		FlightInputMappingContext = LoadObject<UInputMappingContext>(
			nullptr, TEXT("/Game/Eden/Input/IMC_Flight.IMC_Flight"));
	}
	if (!FlightTranslateAction)
	{
		FlightTranslateAction =
			LoadObject<UInputAction>(nullptr, TEXT("/Game/Eden/Input/IA_FlightTranslate.IA_FlightTranslate"));
	}
	if (!FlightRotateAction)
	{
		FlightRotateAction =
			LoadObject<UInputAction>(nullptr, TEXT("/Game/Eden/Input/IA_FlightRotate.IA_FlightRotate"));
	}
	if (!FlightStabilizeAction)
	{
		FlightStabilizeAction =
			LoadObject<UInputAction>(nullptr, TEXT("/Game/Eden/Input/IA_FlightStabilize.IA_FlightStabilize"));
	}
}

void AEdenFlightPlayerController::ApplyFlightInputMode()
{
	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	bShowMouseCursor = false;
	SetIgnoreLookInput(false);
	SetIgnoreMoveInput(false);
}

void AEdenFlightPlayerController::AddFlightInputMappingContext()
{
	EnsureFlightInputAssetsLoaded();
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

	if (!InputSubsystem->HasMappingContext(FlightInputMappingContext))
	{
		InputSubsystem->AddMappingContext(FlightInputMappingContext, FlightInputMappingPriority);
		UE_LOG(LogEdenFlight, Log, TEXT("%s added IMC_Flight (priority %d)."), *GetNameSafe(this), FlightInputMappingPriority);
	}
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
	AddRotationInputSample(Value.Get<FVector>());
}

void AEdenFlightPlayerController::HandleRotateReleased(const FInputActionValue& Value)
{
	(void)Value;
	ReleaseRotationInput();
}

void AEdenFlightPlayerController::HandleStabilizeStarted(const FInputActionValue& Value)
{
	(void)Value;
	EdenToggleFlightAssist();
}

void AEdenFlightPlayerController::EdenToggleFlightAssist()
{

	FEdenFlightInputCommand Command = FlightInputIntent.CurrentCommand;
	Command.bStabilizationEnabled = !Command.bStabilizationEnabled;
	FlightInputIntent.CurrentCommand = FEdenFlightMovementModel::SanitizeCommand(Command);

	UE_LOG(
		LogEdenFlight,
		Verbose,
		TEXT("%s set flight stabilization to %s."),
		*GetNameSafe(this),
		FlightInputIntent.CurrentCommand.bStabilizationEnabled ? TEXT("enabled") : TEXT("disabled"));

	const AEdenSpacecraftPawn* SpacecraftPawn = GetPawn<AEdenSpacecraftPawn>();
	const UEdenFlightMovementComponent* Flight = SpacecraftPawn ? SpacecraftPawn->GetFlightMovementComponent() : nullptr;
	if (!Flight)
	{
		ReportControlFeedback(TEXT("[X] FLIGHT ASSIST: no spacecraft possessed"), false);
		return;
	}

	if (!FlightInputIntent.CurrentCommand.bStabilizationEnabled)
	{
		ReportControlFeedback(TEXT("[X] FLIGHT ASSIST OFF  -  momentum is conserved, no automatic braking"), true);
	}
	else if (Flight->IsStabilizationAssistAvailable())
	{
		ReportControlFeedback(TEXT("[X] FLIGHT ASSIST ON  -  thrusters hold commanded velocity and rate"), true);
	}
	else
	{
		ReportControlFeedback(TEXT("[X] FLIGHT ASSIST REQUESTED  -  UNAVAILABLE: load shed has removed assist power (press L)"), false);
	}
}

void AEdenFlightPlayerController::HandleThermalModeStarted(const FInputActionValue& Value)
{
	(void)Value;
	EdenCycleThermalMode();
}

void AEdenFlightPlayerController::EdenCycleThermalMode()
{
	if (AEdenSpacecraftPawn* SpacecraftPawn = GetPawn<AEdenSpacecraftPawn>())
	{
		if (UEdenOperatorControlComponent* Operator = SpacecraftPawn->GetOperatorControlComponent())
		{
			if (!Operator->CycleThermalControlMode())
			{
				ReportControlFeedback(FString::Printf(TEXT("[T] THERMAL MODE unchanged  -  %s"), *DescribeOperatorRejection(Operator)), false);
				return;
			}
			const FEdenOperatorStateSnapshot State = Operator->GetOperatorStateSnapshot();
			ReportControlFeedback(
				FString::Printf(
					TEXT("[T] THERMAL MODE: %s  -  cooling %+.2f C/s, power %+.2f kW"),
					*UEnum::GetDisplayValueAsText(State.ThermalMode).ToString(),
					State.OperatorDissipationDegreesCelsiusPerSecond,
					State.OperatorDemandKilowatts),
				true);
			return;
		}
	}
	ReportControlFeedback(TEXT("[T] THERMAL MODE: no spacecraft operator control"), false);
}

void AEdenFlightPlayerController::HandleLoadShedStarted(const FInputActionValue& Value)
{
	(void)Value;
	EdenToggleLoadShed();
}

void AEdenFlightPlayerController::EdenToggleLoadShed()
{
	if (AEdenSpacecraftPawn* SpacecraftPawn = GetPawn<AEdenSpacecraftPawn>())
	{
		if (UEdenOperatorControlComponent* Operator = SpacecraftPawn->GetOperatorControlComponent())
		{
			if (!Operator->ToggleLoadShedMode())
			{
				ReportControlFeedback(FString::Printf(TEXT("[L] LOAD SHED unchanged  -  %s"), *DescribeOperatorRejection(Operator)), false);
				return;
			}
			const FEdenOperatorStateSnapshot State = Operator->GetOperatorStateSnapshot();
			const bool bShed = State.LoadShedMode == EEdenLoadShedMode::Shed;
			ReportControlFeedback(
				bShed
					? FString::Printf(
						  TEXT("[L] LOAD SHED ON  -  power %+.2f kW, cooling %+.2f C/s, flight assist UNAVAILABLE"),
						  State.OperatorDemandKilowatts,
						  State.OperatorDissipationDegreesCelsiusPerSecond)
					: TEXT("[L] LOAD SHED OFF  -  normal loads restored, flight assist available"),
				true);
			return;
		}
	}
	ReportControlFeedback(TEXT("[L] LOAD SHED: no spacecraft operator control"), false);
}

void AEdenFlightPlayerController::HandlePropulsionPriorityStarted(const FInputActionValue& Value)
{
	(void)Value;
	EdenTogglePropulsionPriority();
}

void AEdenFlightPlayerController::EdenTogglePropulsionPriority()
{
	if (AEdenSpacecraftPawn* SpacecraftPawn = GetPawn<AEdenSpacecraftPawn>())
	{
		if (UEdenOperatorControlComponent* Operator = SpacecraftPawn->GetOperatorControlComponent())
		{
			if (!Operator->TogglePropulsionPriorityMode())
			{
				ReportControlFeedback(FString::Printf(TEXT("[P] PROPULSION PRIORITY unchanged  -  %s"), *DescribeOperatorRejection(Operator)), false);
				return;
			}
			const FEdenOperatorStateSnapshot State = Operator->GetOperatorStateSnapshot();
			const UEdenFlightMovementComponent* Flight = SpacecraftPawn->GetFlightMovementComponent();
			ReportControlFeedback(
				FString::Printf(
					TEXT("[P] PROPULSION PRIORITY: %s  -  operator authority %.0f%%, deliverable %.0f%%"),
					*UEnum::GetDisplayValueAsText(State.PropulsionPriority).ToString(),
					State.ThrustAuthority * 100.0f,
					Flight ? Flight->GetEffectiveThrustAuthority() * 100.0f : 0.0f),
				true);
			return;
		}
	}
	ReportControlFeedback(TEXT("[P] PROPULSION PRIORITY: no spacecraft operator control"), false);
}

void AEdenFlightPlayerController::ReportControlFeedback(const FString& Message, bool bSucceeded)
{
	UE_LOG(LogEdenFlight, Log, TEXT("%s control feedback: %s"), *GetNameSafe(this), *Message);
	LastControlFeedback = Message;
	bLastControlFeedbackSucceeded = bSucceeded;
	EnsureOperatorHudCreated();
	if (OperatorHudWidget)
	{
		const UWorld* World = GetWorld();
		OperatorHudWidget->ShowControlFeedback(FText::FromString(Message), bSucceeded, World ? World->GetRealTimeSeconds() : 0.0);
	}
	RefreshOperatorHudSnapshot();
}

FString AEdenFlightPlayerController::DescribeOperatorRejection(const UEdenOperatorControlComponent* Operator) const
{
	if (!Operator)
	{
		return TEXT("no operator control component");
	}
	if (!Operator->IsOperatorControlEnabled())
	{
		return TEXT("operator control is disabled (missing or invalid DA_EdenOperatorControlConfig)");
	}

	const AEdenSpacecraftPawn* SpacecraftPawn = GetPawn<AEdenSpacecraftPawn>();
	const UEdenPowerSystemComponent* Power = SpacecraftPawn ? SpacecraftPawn->GetPowerSystemComponent() : nullptr;
	const UEdenThermalSystemComponent* Thermal = SpacecraftPawn ? SpacecraftPawn->GetThermalSystemComponent() : nullptr;
	if (!Power || !Power->IsPowerSimulationEnabled())
	{
		return TEXT("power system is not simulating, so it cannot accept the change");
	}
	if (!Thermal || !Thermal->IsThermalSimulationEnabled())
	{
		return TEXT("thermal system is not simulating, so it cannot accept the change");
	}
	return TEXT("the change was rejected by a spacecraft system (see Output Log)");
}

void AEdenFlightPlayerController::EdenHudDiagnostics()
{
	EnsureOperatorHudCreated();
	if (!OperatorHudWidgetClass)
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s EdenHudDiagnostics: OperatorHudWidgetClass is not assigned."), *GetNameSafe(this));
		return;
	}
	if (!OperatorHudWidget)
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s EdenHudDiagnostics: HUD widget was not created."), *GetNameSafe(this));
		return;
	}
	UE_LOG(LogEdenFlight, Log, TEXT("%s EdenHudDiagnostics: %s"), *GetNameSafe(this), *OperatorHudWidget->DescribeLayoutForDiagnostics());
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

	FEdenOsAcceptedAdvisory AcceptedAdvisory;
	bool bHasMission = false;
	bool bHasEdenAdapter = false;
	FEdenOsConnectionSnapshot EdenConnection;
	if (const UWorld* World = GetWorld())
	{
		if (const UEdenMissionSubsystem* Mission = World->GetSubsystem<UEdenMissionSubsystem>())
		{
			MissionSnapshot = Mission->GetMissionStateSnapshot();
			bHasMission = true;
		}
		if (const UEdenAlertSubsystem* AlertsSubsystem = World->GetSubsystem<UEdenAlertSubsystem>())
		{
			Alerts = AlertsSubsystem->GetActiveAlerts();
		}
		if (const UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>())
		{
			SimTimeSeconds = Clock->GetElapsedSimulationTimeSeconds();
		}
		if (const UEdenOsAdapterSubsystem* Adapter = World->GetSubsystem<UEdenOsAdapterSubsystem>())
		{
			AcceptedAdvisory = Adapter->GetLatestAcceptedAdvisory();
			EdenConnection = Adapter->GetConnectionSnapshot();
			bHasEdenAdapter = true;
		}
	}

	bool bHasFuel = false;
	bool bHasPower = false;
	bool bHasThermal = false;
	bool bHasOperator = false;
	bool bHasFlight = false;
	float FuelCapacityKilograms = 0.0f;
	FEdenFlightDynamicsSnapshot FlightSnapshot;

	if (const AEdenSpacecraftPawn* SpacecraftPawn = GetPawn<AEdenSpacecraftPawn>())
	{
		if (const UEdenFuelSystemComponent* Fuel = SpacecraftPawn->GetFuelSystemComponent())
		{
			FuelSnapshot = Fuel->GetFuelStateSnapshot();
			bHasFuel = Fuel->IsFuelSimulationEnabled();
			FuelCapacityKilograms = Fuel->GetActiveFuelConfig().CapacityKilograms;
		}
		if (const UEdenPowerSystemComponent* Power = SpacecraftPawn->GetPowerSystemComponent())
		{
			PowerSnapshot = Power->GetPowerStateSnapshot();
			bHasPower = Power->IsPowerSimulationEnabled();
		}
		if (const UEdenThermalSystemComponent* Thermal = SpacecraftPawn->GetThermalSystemComponent())
		{
			ThermalSnapshot = Thermal->GetThermalStateSnapshot();
			bHasThermal = Thermal->IsThermalSimulationEnabled();
		}
		if (const UEdenOperatorControlComponent* Operator = SpacecraftPawn->GetOperatorControlComponent())
		{
			OperatorSnapshot = Operator->GetOperatorStateSnapshot();
			bHasOperator = Operator->IsOperatorControlEnabled();
		}
		if (const UEdenFlightMovementComponent* Flight = SpacecraftPawn->GetFlightMovementComponent())
		{
			FlightSnapshot = Flight->GetFlightDynamicsSnapshot();
			// The pilot's assist request lives in the controller until the next flight step consumes it.
			FlightSnapshot.bFlightAssistRequested = FlightInputIntent.CurrentCommand.bStabilizationEnabled;
			FlightSnapshot.bFlightAssistActive = FlightSnapshot.bFlightAssistRequested && Flight->IsStabilizationAssistAvailable();
			bHasFlight = true;
		}
	}

	FEdenOperatorHudSnapshot Snapshot = FEdenOperatorHudModel::Assemble(
		MissionSnapshot,
		FuelSnapshot,
		PowerSnapshot,
		ThermalSnapshot,
		OperatorSnapshot,
		Alerts,
		SimTimeSeconds,
		AcceptedAdvisory);
	Snapshot.bHasMission = bHasMission;
	Snapshot.bHasFuel = bHasFuel;
	Snapshot.bHasPower = bHasPower;
	Snapshot.bHasThermal = bHasThermal;
	Snapshot.bHasOperator = bHasOperator;
	Snapshot.bHasFlight = bHasFlight;
	Snapshot.Flight = FlightSnapshot;
	Snapshot.FuelCapacityKilograms = FuelCapacityKilograms;
	Snapshot.PilotRotationCommand = GetCurrentFlightInputCommand().RotationInput;
	Snapshot.bHasEdenAdapter = bHasEdenAdapter;
	Snapshot.EdenConnection = EdenConnection;
	return Snapshot;
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

void AEdenFlightPlayerController::EdenSetFuelKilograms(float Kilograms)
{
	const AEdenSpacecraftPawn* SpacecraftPawn = GetPawn<AEdenSpacecraftPawn>();
	UEdenFuelSystemComponent* Fuel = SpacecraftPawn ? SpacecraftPawn->GetFuelSystemComponent() : nullptr;
	if (!Fuel)
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s EdenSetFuelKilograms: no possessed spacecraft fuel system."), *GetNameSafe(this));
		return;
	}

	Fuel->SetFuelQuantityKilograms(Kilograms);
	UE_LOG(
		LogEdenFlight,
		Log,
		TEXT("%s EdenSetFuelKilograms: stored propellant now %.3f kg."),
		*GetNameSafe(this),
		Fuel->GetPropellantMassKilograms());
}

void AEdenFlightPlayerController::EdenSetBatteryFraction(float Fraction)
{
	const AEdenSpacecraftPawn* SpacecraftPawn = GetPawn<AEdenSpacecraftPawn>();
	UEdenPowerSystemComponent* Power = SpacecraftPawn ? SpacecraftPawn->GetPowerSystemComponent() : nullptr;
	if (!Power || !Power->IsPowerSimulationEnabled())
	{
		UE_LOG(LogEdenFlight, Warning, TEXT("%s EdenSetBatteryFraction: no possessed spacecraft power system."), *GetNameSafe(this));
		return;
	}

	const float SafeFraction = FMath::IsFinite(Fraction) ? FMath::Clamp(Fraction, 0.0f, 1.0f) : 0.0f;
	Power->SetBatteryChargeKilowattHours(Power->GetActivePowerConfig().BatteryCapacityKilowattHours * SafeFraction);
	UE_LOG(
		LogEdenFlight,
		Log,
		TEXT("%s EdenSetBatteryFraction: battery %.1f%%, power state %s."),
		*GetNameSafe(this),
		SafeFraction * 100.0f,
		*UEnum::GetValueAsString(Power->GetPowerStateSnapshot().PowerState));
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

void AEdenFlightPlayerController::EnableEdenOs(const FString& BaseUrl)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UEdenOsAdapterSubsystem* Adapter = World->GetSubsystem<UEdenOsAdapterSubsystem>();
	if (!Adapter)
	{
		UE_LOG(LogEdenOs, Warning, TEXT("%s EnableEdenOs: UEdenOsAdapterSubsystem not found."), *GetNameSafe(this));
		return;
	}

	FString ResolvedBaseUrl = BaseUrl.TrimStartAndEnd();
	if (ResolvedBaseUrl.IsEmpty())
	{
		ResolvedBaseUrl =
			FPlatformMisc::GetEnvironmentVariable(EdenFlightPlayerControllerEdenOs::DefaultLiveBaseUrlEnvVar)
				.TrimStartAndEnd();
	}

	const bool bAccepted = Adapter->EnableRuntimeConnection(ResolvedBaseUrl);
	const FEdenOsConnectionSnapshot Snapshot = Adapter->GetConnectionSnapshot();
	const FEdenOsValidationResult Validation = Adapter->GetLastValidationResult();
	if (!bAccepted)
	{
		UE_LOG(
			LogEdenOs,
			Warning,
			TEXT("%s EnableEdenOs rejected: %s (BaseUrl='%s')."),
			*GetNameSafe(this),
			*Validation.GetFirstErrorOrEmpty(),
			*ResolvedBaseUrl);
		return;
	}

	UE_LOG(
		LogEdenOs,
		Log,
		TEXT("%s EnableEdenOs ready: BaseUrl='%s' HasBearerJwt=%s State=%d. Inject JWT next if needed, then StartMission."),
		*GetNameSafe(this),
		*ResolvedBaseUrl,
		Snapshot.bHasBearerJwt ? TEXT("true") : TEXT("false"),
		static_cast<int32>(Snapshot.ConnectionState));
}

void AEdenFlightPlayerController::SetEdenOsBearerFromEnv(const FString& VariableName)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UEdenOsAdapterSubsystem* Adapter = World->GetSubsystem<UEdenOsAdapterSubsystem>();
	if (!Adapter)
	{
		UE_LOG(LogEdenOs, Warning, TEXT("%s SetEdenOsBearerFromEnv: UEdenOsAdapterSubsystem not found."), *GetNameSafe(this));
		return;
	}

	FString ResolvedVariableName = VariableName.TrimStartAndEnd();
	if (ResolvedVariableName.IsEmpty())
	{
		ResolvedVariableName = EdenFlightPlayerControllerEdenOs::DefaultLiveBearerJwtEnvVar;
	}

	if (!Adapter->LoadRuntimeBearerJwtFromEnvironment(ResolvedVariableName))
	{
		UE_LOG(
			LogEdenOs,
			Warning,
			TEXT("%s SetEdenOsBearerFromEnv: environment variable '%s' is missing or empty."),
			*GetNameSafe(this),
			*ResolvedVariableName);
		return;
	}

	const FEdenOsConnectionSnapshot Snapshot = Adapter->GetConnectionSnapshot();
	UE_LOG(
		LogEdenOs,
		Log,
		TEXT("%s SetEdenOsBearerFromEnv loaded token from '%s' (value not logged). HasBearerJwt=%s Enabled=%s."),
		*GetNameSafe(this),
		*ResolvedVariableName,
		Snapshot.bHasBearerJwt ? TEXT("true") : TEXT("false"),
		Snapshot.bEnabled ? TEXT("true") : TEXT("false"));
}

void AEdenFlightPlayerController::EnsureAfterActionReviewCreated()
{
	if (AfterActionReviewWidget || !AfterActionReviewWidgetClass)
	{
		return;
	}

	AfterActionReviewWidget = CreateWidget<UEdenAfterActionReviewWidget>(this, AfterActionReviewWidgetClass);
}

UEdenOperatorHudWidget* AEdenFlightPlayerController::GetOperatorHudWidget() const
{
	return OperatorHudWidget;
}

FString AEdenFlightPlayerController::GetLastControlFeedback() const
{
	return LastControlFeedback;
}

bool AEdenFlightPlayerController::WasLastControlFeedbackSuccessful() const
{
	return bLastControlFeedbackSucceeded;
}

void AEdenFlightPlayerController::RefreshOperatorHudNow()
{
	RefreshOperatorHudSnapshot();
}
