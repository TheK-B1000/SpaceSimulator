// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/EdenLogCategories.h"
#include "Core/EdenSimulationClockSubsystem.h"
#include "Flight/EdenFlightMovementComponent.h"
#include "Flight/EdenFlightPlayerController.h"
#include "Flight/EdenFlightTypes.h"
#include "Flight/EdenPropulsionDemandSource.h"
#include "Flight/EdenSpacecraftPawn.h"
#include "Missions/EdenMissionDefinitionDataAsset.h"
#include "Missions/EdenMissionSubsystem.h"
#include "Operations/EdenOperatorControlComponent.h"
#include "Operations/EdenOperatorHudWidget.h"
#include "Blueprint/UserWidget.h"
#include "Systems/EdenFuelConfigDataAsset.h"
#include "Systems/EdenFuelSystemComponent.h"
#include "Systems/EdenPowerConfigDataAsset.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalConfigDataAsset.h"
#include "Systems/EdenThermalSystemComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "Flight/EdenFlightPresentationTypes.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Telemetry/EdenAfterActionReviewWidget.h"
#include "Telemetry/EdenAfterActionModel.h"
#include "Telemetry/EdenTelemetrySubsystem.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "JsonObjectConverter.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenRuntimeCompositionTests
{
constexpr double Tolerance = 0.001;
const TCHAR* PawnBlueprintClassPath = TEXT("/Game/Eden/Blueprints/BP_EdenSpacecraftPawn.BP_EdenSpacecraftPawn_C");
const TCHAR* FuelConfigPath = TEXT("/Game/Eden/Data/Systems/DA_EdenFuelConfig.DA_EdenFuelConfig");
const TCHAR* PowerConfigPath = TEXT("/Game/Eden/Data/Systems/DA_EdenPowerConfig.DA_EdenPowerConfig");
const TCHAR* ThermalConfigPath = TEXT("/Game/Eden/Data/Systems/DA_EdenThermalConfig.DA_EdenThermalConfig");
const TCHAR* SolarEventMissionPath = TEXT("/Game/Eden/Data/Missions/DA_SolarEventEmergency.DA_SolarEventEmergency");

struct FScopedRuntimeWorld
{
	FScopedRuntimeWorld()
	{
		const FName WorldName = MakeUniqueObjectName(
			nullptr,
			UWorld::StaticClass(),
			TEXT("EdenRuntimeCompositionTestWorld"),
			EUniqueObjectNameOptions::GloballyUnique);

		WorldContext = &GEngine->CreateNewWorldContext(EWorldType::Game);
		World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage());
		check(World);
		World->AddToRoot();
		WorldContext->SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
	}

	~FScopedRuntimeWorld()
	{
		if (!World)
		{
			return;
		}

		if (World->AreActorsInitialized())
		{
			for (AActor* Actor : FActorRange(World))
			{
				if (Actor)
				{
					Actor->RouteEndPlay(EEndPlayReason::LevelTransition);
				}
			}
		}

		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		World->RemoveFromRoot();
		GEngine->DestroyWorldContext(World);
		World = nullptr;
		WorldContext = nullptr;
	}

	UWorld* Get() const
	{
		return World;
	}

private:
	UWorld* World = nullptr;
	FWorldContext* WorldContext = nullptr;
};

template <typename TAsset>
TAsset* LoadAsset(const TCHAR* Path)
{
	return Cast<TAsset>(StaticLoadObject(TAsset::StaticClass(), nullptr, Path));
}

bool TestFloatNearlyEqual(FAutomationTestBase& Test, const TCHAR* What, float Actual, float Expected)
{
	return Test.TestTrue(
		FString::Printf(TEXT("%s. Actual=%f Expected=%f"), What, Actual, Expected),
		FMath::IsNearlyEqual(Actual, Expected, Tolerance));
}

template <typename TComponent>
int32 CountComponents(const AActor* Actor)
{
	TInlineComponentArray<TComponent*> Components;
	Actor->GetComponents(Components);
	return Components.Num();
}

int32 CountPropulsionDemandSources(const AActor* Actor)
{
	TInlineComponentArray<UActorComponent*> Components;
	Actor->GetComponents(Components);

	int32 Count = 0;
	for (const UActorComponent* Component : Components)
	{
		if (Cast<IEdenPropulsionDemandSource>(Component))
		{
			++Count;
		}
	}

	return Count;
}

void EnsureBeginPlay(AActor* Actor)
{
	if (Actor && !Actor->HasActorBegunPlay())
	{
		Actor->DispatchBeginPlay();
	}
}

bool SeedControllerInputIntent(AEdenFlightPlayerController* Controller, const FEdenFlightInputCommand& Command)
{
	if (!Controller)
	{
		return false;
	}

	FStructProperty* IntentProperty = FindFProperty<FStructProperty>(
		Controller->GetClass(),
		TEXT("FlightInputIntent"));
	if (!IntentProperty)
	{
		return false;
	}

	FEdenFlightInputIntent* Intent = IntentProperty->ContainerPtrToValuePtr<FEdenFlightInputIntent>(Controller);
	if (!Intent)
	{
		return false;
	}

	Intent->CurrentCommand = Command;
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenRuntimeCompositionBlueprintPawnUsesRuntimeSystemsTest,
	"Eden.Integration.Runtime.BlueprintPawnUsesRuntimeSystems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenRuntimeCompositionBlueprintPawnUsesRuntimeSystemsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UClass* PawnClass = StaticLoadClass(
		AEdenSpacecraftPawn::StaticClass(),
		nullptr,
		EdenRuntimeCompositionTests::PawnBlueprintClassPath);
	TestNotNull(TEXT("BP_EdenSpacecraftPawn class loads"), PawnClass);
	if (!PawnClass)
	{
		return false;
	}

	const UEdenFuelConfigDataAsset* FuelConfigAsset =
		EdenRuntimeCompositionTests::LoadAsset<UEdenFuelConfigDataAsset>(EdenRuntimeCompositionTests::FuelConfigPath);
	const UEdenPowerConfigDataAsset* PowerConfigAsset =
		EdenRuntimeCompositionTests::LoadAsset<UEdenPowerConfigDataAsset>(EdenRuntimeCompositionTests::PowerConfigPath);
	const UEdenThermalConfigDataAsset* ThermalConfigAsset =
		EdenRuntimeCompositionTests::LoadAsset<UEdenThermalConfigDataAsset>(EdenRuntimeCompositionTests::ThermalConfigPath);
	TestNotNull(TEXT("Fuel config asset loads"), FuelConfigAsset);
	TestNotNull(TEXT("Power config asset loads"), PowerConfigAsset);
	TestNotNull(TEXT("Thermal config asset loads"), ThermalConfigAsset);
	if (!FuelConfigAsset || !PowerConfigAsset || !ThermalConfigAsset)
	{
		return false;
	}

	EdenRuntimeCompositionTests::FScopedRuntimeWorld RuntimeWorld;
	UWorld* World = RuntimeWorld.Get();
	TestNotNull(TEXT("Runtime game world exists"), World);
	TestTrue(TEXT("Runtime world type is Game"), World && World->WorldType == EWorldType::Game);

	UEdenSimulationClockSubsystem* Clock = World ? World->GetSubsystem<UEdenSimulationClockSubsystem>() : nullptr;
	TestNotNull(TEXT("Game world receives Eden simulation clock subsystem"), Clock);
	TestTrue(TEXT("Clock supports Game worlds"), Clock && Clock->DoesSupportWorldType(World->WorldType));
	if (!World || !Clock)
	{
		return false;
	}
	TestTrue(TEXT("Runtime test widens catch-up cap without changing fixed-step semantics"), Clock->SetMaxCatchUpSteps(200));

	AEdenSpacecraftPawn* Pawn = World->SpawnActor<AEdenSpacecraftPawn>(PawnClass, FTransform::Identity);
	TestNotNull(TEXT("Blueprint spacecraft pawn spawns in runtime world"), Pawn);
	if (!Pawn)
	{
		return false;
	}

	World->BeginPlay();
	EdenRuntimeCompositionTests::EnsureBeginPlay(Pawn);

	UEdenFlightMovementComponent* Flight = Pawn->GetFlightMovementComponent();
	UEdenFuelSystemComponent* Fuel = Pawn->GetFuelSystemComponent();
	UEdenPowerSystemComponent* Power = Pawn->GetPowerSystemComponent();
	UEdenThermalSystemComponent* Thermal = Pawn->GetThermalSystemComponent();
	UEdenOperatorControlComponent* Operator = Pawn->GetOperatorControlComponent();

	TestNotNull(TEXT("Flight movement component exists"), Flight);
	TestNotNull(TEXT("Fuel component exists"), Fuel);
	TestNotNull(TEXT("Power component exists"), Power);
	TestNotNull(TEXT("Thermal component exists"), Thermal);
	TestNotNull(TEXT("Operator control component exists"), Operator);
	if (!Flight || !Fuel || !Power || !Thermal || !Operator)
	{
		return false;
	}

	TestEqual(TEXT("Exactly one flight movement component exists"), EdenRuntimeCompositionTests::CountComponents<UEdenFlightMovementComponent>(Pawn), 1);
	TestEqual(TEXT("Exactly one fuel component exists"), EdenRuntimeCompositionTests::CountComponents<UEdenFuelSystemComponent>(Pawn), 1);
	TestEqual(TEXT("Exactly one power component exists"), EdenRuntimeCompositionTests::CountComponents<UEdenPowerSystemComponent>(Pawn), 1);
	TestEqual(TEXT("Exactly one thermal component exists"), EdenRuntimeCompositionTests::CountComponents<UEdenThermalSystemComponent>(Pawn), 1);
	TestEqual(TEXT("Exactly one operator control component exists"), EdenRuntimeCompositionTests::CountComponents<UEdenOperatorControlComponent>(Pawn), 1);
	TestEqual(TEXT("Exactly one propulsion demand source exists"), EdenRuntimeCompositionTests::CountPropulsionDemandSources(Pawn), 1);

	TestTrue(TEXT("Fuel runtime config asset was applied"), Fuel->IsFuelSimulationEnabled());
	TestTrue(TEXT("Power runtime config asset was applied"), Power->IsPowerSimulationEnabled());
	TestTrue(TEXT("Thermal runtime config asset was applied"), Thermal->IsThermalSimulationEnabled());
	EdenRuntimeCompositionTests::TestFloatNearlyEqual(
		*this,
		TEXT("Fuel active capacity matches Data Asset"),
		Fuel->GetActiveFuelConfig().CapacityKilograms,
		FuelConfigAsset->FuelConfig.CapacityKilograms);
	EdenRuntimeCompositionTests::TestFloatNearlyEqual(
		*this,
		TEXT("Power active capacity matches Data Asset"),
		Power->GetActivePowerConfig().BatteryCapacityKilowattHours,
		PowerConfigAsset->PowerConfig.BatteryCapacityKilowattHours);
	EdenRuntimeCompositionTests::TestFloatNearlyEqual(
		*this,
		TEXT("Thermal active ambient matches Data Asset"),
		Thermal->GetActiveThermalConfig().AmbientTemperatureCelsius,
		ThermalConfigAsset->ThermalConfig.AmbientTemperatureCelsius);

	TestTrue(TEXT("Fuel, power, and thermal registered with clock"), Clock->GetSubscriberCount() >= 3);

	const float InitialElapsedSeconds = Clock->GetElapsedSimulationTimeSeconds();
	const float InitialFuelKilograms = Fuel->GetFuelStateSnapshot().FuelQuantityKilograms;
	const float InitialTemperatureCelsius = Thermal->GetThermalStateSnapshot().TemperatureCelsius;

	FEdenFlightInputCommand ThrustCommand;
	ThrustCommand.TranslationInput = FVector(1.0f, 0.0f, 0.0f);
	Pawn->ApplyFlightInputCommand(ThrustCommand, 0.1f);
	TestTrue(TEXT("Flight movement exposes nonzero propulsion demand"), Flight->GetPropulsionDemandNormalized() > 0.0f);
	TestEqual(TEXT("0.1 s of world time runs twelve 120 Hz flight steps"), Flight->GetFlightDynamicsSnapshot().FlightStepCount, static_cast<int64>(12));
	TestTrue(TEXT("Thrust accelerated the spacecraft"), Flight->GetSpeedMetersPerSecond() > 0.0f);

	Clock->Tick(1.0f);
	TestTrue(TEXT("Simulation time advances"), Clock->GetElapsedSimulationTimeSeconds() > InitialElapsedSeconds);
	TestTrue(TEXT("Sustained runtime propulsion demand reduces fuel"), Fuel->GetFuelStateSnapshot().FuelQuantityKilograms < InitialFuelKilograms);
	// Baseline A2 (ExecPlan 0011): a healthy idle ship in Nominal is thermally stable...
	TestTrue(
		TEXT("Idle Nominal ship holds its temperature (A2 baseline)"),
		FMath::IsNearlyEqual(Thermal->GetThermalStateSnapshot().TemperatureCelsius, InitialTemperatureCelsius, 0.01f));
	// ...and the thermal model still advances: with active cooling Off, passive radiation alone loses to heating.
	TestTrue(TEXT("Thermal mode Off is accepted"), Operator->SetThermalControlMode(EEdenThermalControlMode::Off));
	Clock->Tick(1.0f);
	TestTrue(TEXT("Thermal model advances in runtime wiring (Off heats up)"), Thermal->GetThermalStateSnapshot().TemperatureCelsius > InitialTemperatureCelsius);
	TestTrue(TEXT("Thermal mode back to Nominal"), Operator->SetThermalControlMode(EEdenThermalControlMode::Nominal));

	// Newtonian flight (ExecPlan 0008): with Flight Assist off, released controls fire no thrusters and the ship drifts.
	const float FuelAfterThrustKilograms = Fuel->GetFuelStateSnapshot().FuelQuantityKilograms;
	const float SpeedBeforeCoastMetersPerSecond = Flight->GetSpeedMetersPerSecond();
	FEdenFlightInputCommand CoastCommand;
	CoastCommand.bStabilizationEnabled = false;
	Pawn->ApplyFlightInputCommand(CoastCommand, 0.1f);
	TestEqual(TEXT("Assist-off zero command clears propulsion demand"), Flight->GetPropulsionDemandNormalized(), 0.0f);
	EdenRuntimeCompositionTests::TestFloatNearlyEqual(
		*this,
		TEXT("Assist-off zero command conserves momentum"),
		Flight->GetSpeedMetersPerSecond(),
		SpeedBeforeCoastMetersPerSecond);

	Clock->Tick(1.0f);
	EdenRuntimeCompositionTests::TestFloatNearlyEqual(
		*this,
		TEXT("Zero propulsion demand stops propulsion fuel consumption"),
		Fuel->GetFuelStateSnapshot().FuelQuantityKilograms,
		FuelAfterThrustKilograms);

	// With Flight Assist on, released controls brake on real thrusters, which bills propellant.
	FEdenFlightInputCommand AssistedStopCommand;
	AssistedStopCommand.bStabilizationEnabled = true;
	Pawn->ApplyFlightInputCommand(AssistedStopCommand, 0.05f);
	TestTrue(TEXT("Assist braking reports propulsion demand"), Flight->GetPropulsionDemandNormalized() > 0.0f);
	TestTrue(TEXT("Assist braking slows the ship"), Flight->GetSpeedMetersPerSecond() < SpeedBeforeCoastMetersPerSecond);

	Power->SetBatteryChargeKilowattHours(Power->GetActivePowerConfig().BatteryCapacityKilowattHours * 0.5f);
	const float BatteryBeforeAdvanceKilowattHours = Power->GetPowerStateSnapshot().BatteryChargeKilowattHours;
	Clock->Tick(10.0f);
	TestTrue(
		TEXT("Power model advances through runtime clock"),
		!FMath::IsNearlyEqual(
			Power->GetPowerStateSnapshot().BatteryChargeKilowattHours,
			BatteryBeforeAdvanceKilowattHours,
			EdenRuntimeCompositionTests::Tolerance));

	TestTrue(TEXT("Fuel reset restores configured initial value"), Fuel->ResetFuelState());
	TestTrue(TEXT("Power reset restores configured initial value"), Power->ResetPowerState());
	TestTrue(TEXT("Thermal reset restores configured initial value"), Thermal->ResetThermalState());
	Clock->ResetSimulationClock();

	EdenRuntimeCompositionTests::TestFloatNearlyEqual(
		*this,
		TEXT("Clock reset clears elapsed time"),
		Clock->GetElapsedSimulationTimeSeconds(),
		0.0f);
	EdenRuntimeCompositionTests::TestFloatNearlyEqual(
		*this,
		TEXT("Fuel reset matches configured initial quantity"),
		Fuel->GetFuelStateSnapshot().FuelQuantityKilograms,
		FuelConfigAsset->FuelConfig.CapacityKilograms * FuelConfigAsset->FuelConfig.InitialFuelFraction);
	EdenRuntimeCompositionTests::TestFloatNearlyEqual(
		*this,
		TEXT("Power reset matches configured initial charge"),
		Power->GetPowerStateSnapshot().BatteryChargeKilowattHours,
		PowerConfigAsset->PowerConfig.BatteryCapacityKilowattHours * PowerConfigAsset->PowerConfig.InitialChargeFraction);
	EdenRuntimeCompositionTests::TestFloatNearlyEqual(
		*this,
		TEXT("Thermal reset matches configured initial temperature"),
		Thermal->GetThermalStateSnapshot().TemperatureCelsius,
		ThermalConfigAsset->ThermalConfig.InitialTemperatureCelsius);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenRuntimeCompositionFlightResetClearsPossessedRuntimeStateTest,
	"Eden.Integration.Runtime.FlightResetClearsPossessedRuntimeState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenRuntimeCompositionFlightResetClearsPossessedRuntimeStateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UClass* PawnClass = StaticLoadClass(
		AEdenSpacecraftPawn::StaticClass(),
		nullptr,
		EdenRuntimeCompositionTests::PawnBlueprintClassPath);
	TestNotNull(TEXT("BP_EdenSpacecraftPawn class loads"), PawnClass);
	if (!PawnClass)
	{
		return false;
	}

	EdenRuntimeCompositionTests::FScopedRuntimeWorld RuntimeWorld;
	UWorld* World = RuntimeWorld.Get();
	TestNotNull(TEXT("Runtime game world exists"), World);
	if (!World)
	{
		return false;
	}

	AEdenSpacecraftPawn* Pawn = World->SpawnActor<AEdenSpacecraftPawn>(PawnClass, FTransform::Identity);
	AEdenFlightPlayerController* Controller =
		World->SpawnActor<AEdenFlightPlayerController>(AEdenFlightPlayerController::StaticClass(), FTransform::Identity);
	TestNotNull(TEXT("Runtime pawn spawns"), Pawn);
	TestNotNull(TEXT("Runtime controller spawns"), Controller);
	if (!Pawn || !Controller)
	{
		return false;
	}

	World->BeginPlay();
	EdenRuntimeCompositionTests::EnsureBeginPlay(Pawn);
	EdenRuntimeCompositionTests::EnsureBeginPlay(Controller);

	UEdenFlightMovementComponent* Flight = Pawn->GetFlightMovementComponent();
	TestNotNull(TEXT("Pawn has flight movement"), Flight);
	if (!Flight)
	{
		return false;
	}

	FEdenFlightInputCommand SeededCommand;
	SeededCommand.TranslationInput = FVector(1.0f, 0.5f, 0.0f);
	SeededCommand.RotationInput = FVector(0.5f, 0.0f, 1.0f);
	SeededCommand.bStabilizationEnabled = false;
	TestTrue(TEXT("Test seeds controller input intent through reflected property"), EdenRuntimeCompositionTests::SeedControllerInputIntent(Controller, SeededCommand));

	Flight->Velocity = FVector(100.0f, 50.0f, 25.0f);
	Flight->SetAngularVelocityBodyRadiansPerSecond(FVector(0.1f, 0.2f, 0.3f));

	Controller->Possess(Pawn);

	const FEdenFlightInputCommand CurrentCommand = Controller->GetCurrentFlightInputCommand();
	TestEqual(TEXT("Controller OnPossess clears translation intent"), CurrentCommand.TranslationInput, FVector::ZeroVector);
	TestEqual(TEXT("Controller OnPossess clears rotation intent"), CurrentCommand.RotationInput, FVector::ZeroVector);
	TestTrue(TEXT("Controller OnPossess restores stabilization default"), CurrentCommand.bStabilizationEnabled);
	TestEqual(TEXT("Pawn reset during OnPossess clears linear velocity"), Flight->Velocity, FVector::ZeroVector);
	TestEqual(TEXT("Pawn reset during OnPossess clears angular velocity"), Flight->GetAngularVelocityBodyRadiansPerSecond(), FVector::ZeroVector);
	TestEqual(TEXT("Pawn reset during OnPossess clears propulsion demand"), Flight->GetPropulsionDemandNormalized(), 0.0f);

	TestTrue(TEXT("Test re-seeds controller input intent after possession"), EdenRuntimeCompositionTests::SeedControllerInputIntent(Controller, SeededCommand));
	Flight->Velocity = FVector(10.0f, 20.0f, 30.0f);
	Flight->SetAngularVelocityBodyRadiansPerSecond(FVector(0.5f, 0.6f, 0.7f));

	Controller->ClearFlightInputIntent();
	Pawn->ResetFlightState();

	const FEdenFlightInputCommand ClearedCommand = Controller->GetCurrentFlightInputCommand();
	TestEqual(TEXT("Explicit controller reset clears translation intent"), ClearedCommand.TranslationInput, FVector::ZeroVector);
	TestEqual(TEXT("Explicit controller reset clears rotation intent"), ClearedCommand.RotationInput, FVector::ZeroVector);
	TestTrue(TEXT("Explicit controller reset restores stabilization default"), ClearedCommand.bStabilizationEnabled);
	TestEqual(TEXT("Explicit pawn reset clears linear velocity"), Flight->Velocity, FVector::ZeroVector);
	TestEqual(TEXT("Explicit pawn reset clears angular velocity"), Flight->GetAngularVelocityBodyRadiansPerSecond(), FVector::ZeroVector);
	TestEqual(TEXT("Explicit pawn reset clears propulsion demand"), Flight->GetPropulsionDemandNormalized(), 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenRuntimeMissionSubsystemAndSolarEventAssetAccessibleTest,
	"Eden.Integration.Runtime.MissionSubsystemAndSolarEventAssetAccessible",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenRuntimeMissionSubsystemAndSolarEventAssetAccessibleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenRuntimeCompositionTests::FScopedRuntimeWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("World exists"), World);
	if (!World)
	{
		return false;
	}

	UEdenSimulationClockSubsystem* Clock = World->GetSubsystem<UEdenSimulationClockSubsystem>();
	UEdenMissionSubsystem* Mission = World->GetSubsystem<UEdenMissionSubsystem>();
	TestNotNull(TEXT("Simulation clock subsystem exists"), Clock);
	TestNotNull(TEXT("Mission subsystem exists"), Mission);

	UEdenMissionDefinitionDataAsset* SolarEvent = Cast<UEdenMissionDefinitionDataAsset>(
		FSoftObjectPath(EdenRuntimeCompositionTests::SolarEventMissionPath).TryLoad());
	TestNotNull(TEXT("DA_SolarEventEmergency loads"), SolarEvent);
	if (!SolarEvent || !Mission)
	{
		return false;
	}

	TestEqual(TEXT("Single mission subsystem instance"), Mission, World->GetSubsystem<UEdenMissionSubsystem>());
	TestTrue(TEXT("Mission loads Solar Event asset"), Mission->LoadMissionFromDefinitionAsset(SolarEvent));
	TestEqual(TEXT("Active mission id is SolarCrisis"), Mission->GetActiveMissionId(), FName("SolarCrisis"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenRuntimeCompositionOperatorControlConstrainsPhysicalFlightTest,
	"Eden.Integration.Flight.OperatorControlConstrainsPhysicalFlight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenRuntimeCompositionOperatorControlConstrainsPhysicalFlightTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UClass* PawnClass = StaticLoadClass(
		AEdenSpacecraftPawn::StaticClass(),
		nullptr,
		EdenRuntimeCompositionTests::PawnBlueprintClassPath);
	TestNotNull(TEXT("BP_EdenSpacecraftPawn class loads"), PawnClass);
	EdenRuntimeCompositionTests::FScopedRuntimeWorld RuntimeWorld;
	UWorld* World = RuntimeWorld.Get();
	UEdenSimulationClockSubsystem* Clock = World ? World->GetSubsystem<UEdenSimulationClockSubsystem>() : nullptr;
	if (!PawnClass || !World || !Clock)
	{
		return false;
	}
	Clock->SetMaxCatchUpSteps(200);

	AEdenSpacecraftPawn* Pawn = World->SpawnActor<AEdenSpacecraftPawn>(PawnClass, FTransform::Identity);
	TestNotNull(TEXT("Blueprint spacecraft spawns"), Pawn);
	if (!Pawn)
	{
		return false;
	}
	World->BeginPlay();
	EdenRuntimeCompositionTests::EnsureBeginPlay(Pawn);

	UEdenFlightMovementComponent* Flight = Pawn->GetFlightMovementComponent();
	UEdenOperatorControlComponent* Operator = Pawn->GetOperatorControlComponent();
	UEdenFuelSystemComponent* Fuel = Pawn->GetFuelSystemComponent();
	TestTrue(TEXT("Operator control is enabled from its Data Asset"), Operator && Operator->IsOperatorControlEnabled());
	if (!Flight || !Operator || !Fuel || !Operator->IsOperatorControlEnabled())
	{
		return false;
	}

	FEdenFlightInputCommand AssistRequested;
	AssistRequested.bStabilizationEnabled = true;
	auto Advance = [&](const FEdenFlightInputCommand& Command, int32 Updates)
	{
		for (int32 Update = 0; Update < Updates; ++Update)
		{
			Pawn->ApplyFlightInputCommand(Command, 0.1f);
		}
	};

	// Load shed: assist unavailable, so the requested assist cannot fire thrusters and the ship drifts and spins on.
	const FVector Drift(1000.0, 0.0, 0.0);
	const FVector Spin(0.0, 0.0, 0.3);
	Flight->Velocity = Drift;
	Flight->SetAngularVelocityBodyRadiansPerSecond(Spin);
	TestTrue(TEXT("Operator sheds load"), Operator->SetLoadShedMode(EEdenLoadShedMode::Shed));
	TestFalse(TEXT("Load shed removes flight assist"), Flight->IsStabilizationAssistAvailable());

	const float FuelBeforeShedKilograms = Fuel->GetFuelStateSnapshot().FuelQuantityKilograms;
	Advance(AssistRequested, 10);
	const FEdenFlightDynamicsSnapshot Shed = Flight->GetFlightDynamicsSnapshot();
	TestTrue(TEXT("Snapshot shows assist requested"), Shed.bFlightAssistRequested);
	TestFalse(TEXT("Snapshot shows assist inactive under load shed"), Shed.bFlightAssistActive);
	TestTrue(TEXT("Shed ship drifts at constant velocity"), Flight->Velocity.Equals(Drift, 1.0e-9));
	TestTrue(TEXT("Shed ship keeps spinning (principal-axis spin, no torque)"), Flight->GetAngularVelocityBodyRadiansPerSecond() == Spin);
	TestEqual(TEXT("Shed ship fires no thrusters"), Shed.DeliveredThrusterOutputNewtons, 0.0f);
	Clock->Tick(1.0f);
	EdenRuntimeCompositionTests::TestFloatNearlyEqual(
		*this,
		TEXT("Drifting under load shed burns no propellant"),
		Fuel->GetFuelStateSnapshot().FuelQuantityKilograms,
		FuelBeforeShedKilograms);

	// Restore power: assist brakes translation and rotation on real thrusters and pays for it.
	TestTrue(TEXT("Operator restores normal load"), Operator->SetLoadShedMode(EEdenLoadShedMode::Normal));
	TestTrue(TEXT("Normal load restores flight assist"), Flight->IsStabilizationAssistAvailable());
	// Checkpoint D: flight draws propellant during its own steps (not on the 10 Hz fuel tick).
	const double FuelBeforeBrakingKilograms = Fuel->GetPropellantMassKilograms();
	Advance(AssistRequested, 1);
	TestTrue(TEXT("Assist braking reports propulsion demand"), Flight->GetPropulsionDemandNormalized() > 0.0f);
	TestTrue(TEXT("Assist slows the drift"), Flight->GetSpeedMetersPerSecond() < 10.0f);
	TestTrue(TEXT("Assist slows the spin"), Flight->GetAngularVelocityBodyRadiansPerSecond().Z < Spin.Z);
	TestTrue(TEXT("Assist braking burns propellant"), Fuel->GetPropellantMassKilograms() < FuelBeforeBrakingKilograms);

	// Reduced propulsion priority: the operator limit scales physical thrust and torque.
	TestTrue(TEXT("Operator reduces propulsion priority"), Operator->SetPropulsionPriorityMode(EEdenPropulsionPriorityMode::Reduced));
	const double Authority = Operator->GetOperatorStateSnapshot().ThrustAuthority;
	TestTrue(TEXT("Reduced priority lowers thrust authority"), Authority < 1.0);
	TestEqual(TEXT("Movement reports the operator authority"), static_cast<double>(Flight->GetThrustAuthority()), Authority);

	const FEdenFlightPropulsionSettings Propulsion;
	const FEdenFlightAttitudeSettings Attitude;
	const FEdenFlightDynamicsSnapshot Reduced = Flight->GetFlightDynamicsSnapshot();
	TestTrue(
		TEXT("Available forward thrust is scaled by authority"),
		FMath::IsNearlyEqual(Reduced.AvailablePositiveThrustNewtons.X, Propulsion.ForwardThrustNewtons * Authority, 1.0e-3));
	TestTrue(
		TEXT("Available yaw torque is scaled by authority"),
		FMath::IsNearlyEqual(Reduced.AvailableTorqueNewtonMeters.Z, Attitude.YawTorqueNewtonMeters * Authority, 1.0e-3));

	Flight->StopMovementImmediately();
	FEdenFlightInputCommand BurnAndYaw;
	BurnAndYaw.TranslationInput = FVector(1.0, 0.0, 0.0);
	BurnAndYaw.RotationInput = FVector(0.0, 1.0, 0.0);
	BurnAndYaw.bStabilizationEnabled = false;
	// Checkpoint D: the Blueprint pawn carries propellant, so mass and inertia include it (a ~1% effect).
	const double MassBeforeBurn = Flight->GetMassKilograms();
	const double InertiaScale = MassBeforeBurn / Propulsion.DryMassKilograms;
	Advance(BurnAndYaw, 1);
	const double ExpectedYawRate =
		Attitude.YawTorqueNewtonMeters * Authority / (Attitude.PrincipalMomentsOfInertiaKilogramMetersSquared.Z * InertiaScale) * 0.1;
	TestTrue(
		FString::Printf(TEXT("Reduced yaw torque gives w = (A tau / I(m)) t (%.9f vs %.9f)"), Flight->GetAngularVelocityBodyRadiansPerSecond().Z, ExpectedYawRate),
		FMath::IsNearlyEqual(Flight->GetAngularVelocityBodyRadiansPerSecond().Z, ExpectedYawRate, ExpectedYawRate * 1.0e-4));
	const double ExpectedSpeed = Propulsion.ForwardThrustNewtons * Authority / MassBeforeBurn * 0.1;
	TestTrue(
		FString::Printf(TEXT("Reduced thrust gives v = (A F / m) t (%.6f vs %.6f)"), Flight->GetSpeedMetersPerSecond(), ExpectedSpeed),
		FMath::IsNearlyEqual(Flight->GetSpeedMetersPerSecond(), ExpectedSpeed, ExpectedSpeed * 1.0e-4));

	return true;
}

namespace EdenRuntimeCompositionTests
{
struct FFlightPropellantFixture
{
	FScopedRuntimeWorld RuntimeWorld;
	AEdenSpacecraftPawn* Pawn = nullptr;
	UEdenFlightMovementComponent* Flight = nullptr;
	UEdenFuelSystemComponent* Fuel = nullptr;
	UEdenPowerSystemComponent* Power = nullptr;
	UEdenSimulationClockSubsystem* Clock = nullptr;

	bool Setup(FAutomationTestBase& Test)
	{
		UClass* PawnClass = StaticLoadClass(AEdenSpacecraftPawn::StaticClass(), nullptr, PawnBlueprintClassPath);
		UWorld* World = RuntimeWorld.Get();
		Clock = World ? World->GetSubsystem<UEdenSimulationClockSubsystem>() : nullptr;
		if (!Test.TestNotNull(TEXT("BP_EdenSpacecraftPawn class loads"), PawnClass) || !World || !Clock)
		{
			return false;
		}
		Clock->SetMaxCatchUpSteps(200);
		Pawn = World->SpawnActor<AEdenSpacecraftPawn>(PawnClass, FTransform::Identity);
		if (!Test.TestNotNull(TEXT("Blueprint spacecraft spawns"), Pawn))
		{
			return false;
		}
		World->BeginPlay();
		EnsureBeginPlay(Pawn);
		Flight = Pawn->GetFlightMovementComponent();
		Fuel = Pawn->GetFuelSystemComponent();
		Power = Pawn->GetPowerSystemComponent();
		return Flight && Fuel && Power && Fuel->IsFuelSimulationEnabled() && Power->IsPowerSimulationEnabled();
	}

	void Advance(const FEdenFlightInputCommand& Command, int32 Updates)
	{
		for (int32 Update = 0; Update < Updates; ++Update)
		{
			Pawn->ApplyFlightInputCommand(Command, 0.1f);
		}
	}

	static FEdenFlightInputCommand FullBurn()
	{
		FEdenFlightInputCommand Burn;
		Burn.TranslationInput = FVector(1.0, 0.0, 0.0);
		Burn.bStabilizationEnabled = false;
		return Burn;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenRuntimeCompositionFuelSystemIsSinglePropellantOwnerTest,
	"Eden.Integration.Flight.FuelSystemIsSinglePropellantOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenRuntimeCompositionFuelSystemIsSinglePropellantOwnerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenRuntimeCompositionTests::FFlightPropellantFixture Fixture;
	if (!TestTrue(TEXT("Blueprint pawn with fuel and power runs"), Fixture.Setup(*this)))
	{
		return false;
	}

	const FEdenFlightPropulsionSettings Propulsion;
	TestTrue(TEXT("Flight meters propellant directly"), Fixture.Flight->MetersPropellantDirectly());
	const double InitialPropellant = Fixture.Fuel->GetPropellantMassKilograms();
	TestTrue(TEXT("The Blueprint pawn carries propellant"), InitialPropellant > 1.0);
	TestTrue(
		TEXT("Total mass is dry mass plus stored propellant"),
		FMath::IsNearlyEqual(static_cast<double>(Fixture.Flight->GetMassKilograms()), Propulsion.DryMassKilograms + InitialPropellant, 1.0e-3));

	Fixture.Advance(EdenRuntimeCompositionTests::FFlightPropellantFixture::FullBurn(), 10);
	const double MassFlow = FEdenFlightMovementModel::CalculatePropellantMassFlowKilogramsPerSecond(Propulsion.ForwardThrustNewtons, Propulsion);
	const double Consumed = InitialPropellant - Fixture.Fuel->GetPropellantMassKilograms();
	TestTrue(
		FString::Printf(TEXT("One second of full burn draws mdot x 1 s from the fuel system (%.9f vs %.9f kg)"), Consumed, MassFlow),
		FMath::IsNearlyEqual(Consumed, MassFlow, MassFlow * 1.0e-9));
	TestTrue(
		TEXT("Flight reports the propellant flow it drew"),
		FMath::IsNearlyEqual(static_cast<double>(Fixture.Flight->GetFlightDynamicsSnapshot().PropellantFlowKilogramsPerSecond), MassFlow, MassFlow * 1.0e-5));
	TestTrue(
		TEXT("The fuel snapshot follows the ledger"),
		FMath::IsNearlyEqual(
			static_cast<double>(Fixture.Fuel->GetFuelStateSnapshot().FuelQuantityKilograms),
			Fixture.Fuel->GetPropellantMassKilograms(),
			1.0e-4));

	// The fuel clock step must not bill demand x rate on top of what flight already drew.
	const double BeforeClockTick = Fixture.Fuel->GetPropellantMassKilograms();
	TestTrue(TEXT("Demand is still nonzero from the last burn step"), Fixture.Flight->GetPropulsionDemandNormalized() > 0.0f);
	Fixture.Clock->Tick(1.0f);
	TestEqual(TEXT("A 10 Hz fuel tick does not subtract the same propellant again"), Fixture.Fuel->GetPropellantMassKilograms(), BeforeClockTick);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenRuntimeCompositionEmptyTankStopsPropulsionTest,
	"Eden.Integration.Flight.EmptyTankStopsPropulsion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenRuntimeCompositionEmptyTankStopsPropulsionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenRuntimeCompositionTests::FFlightPropellantFixture Fixture;
	if (!TestTrue(TEXT("Blueprint pawn with fuel and power runs"), Fixture.Setup(*this)))
	{
		return false;
	}

	// 0.05 kg lasts ~0.05 s at 1 kg/s: the 0.1 s burn runs dry mid-update, then the ship coasts.
	const FEdenFlightPropulsionSettings Propulsion;
	TestTrue(TEXT("Fuel can be set to a few grams"), Fixture.Fuel->SetFuelQuantityKilograms(0.05f));
	const double Loaded = Fixture.Fuel->GetPropellantMassKilograms();
	Fixture.Advance(EdenRuntimeCompositionTests::FFlightPropellantFixture::FullBurn(), 1);

	const double MassFlow = FEdenFlightMovementModel::CalculatePropellantMassFlowKilogramsPerSecond(Propulsion.ForwardThrustNewtons, Propulsion);
	const double BurnSeconds = Loaded / MassFlow;
	const double ExpectedSpeed = Propulsion.ForwardThrustNewtons * BurnSeconds / (Propulsion.DryMassKilograms + 0.5 * Loaded);
	TestEqual(TEXT("Tank is exactly empty"), Fixture.Fuel->GetPropellantMassKilograms(), 0.0);
	TestTrue(TEXT("Tank reports Depleted"), Fixture.Fuel->GetFuelStateSnapshot().FuelState == EEdenFuelState::Depleted);
	TestTrue(
		FString::Printf(TEXT("Speed comes only from the propellant that existed (%.6f vs %.6f m/s)"), Fixture.Flight->GetSpeedMetersPerSecond(), ExpectedSpeed),
		FMath::IsNearlyEqual(static_cast<double>(Fixture.Flight->GetSpeedMetersPerSecond()), ExpectedSpeed, ExpectedSpeed * 1.0e-5));

	const FEdenFlightDynamicsSnapshot Empty = Fixture.Flight->GetFlightDynamicsSnapshot();
	TestTrue(TEXT("Snapshot is propellant-limited"), Empty.bPropellantLimited);
	TestTrue(TEXT("Empty tank: no thrust available"), Empty.AvailablePositiveThrustNewtons.IsZero());
	TestTrue(TEXT("Empty tank: no torque available"), Empty.AvailableTorqueNewtonMeters.IsZero());
	TestTrue(TEXT("Mass is back to dry mass"), FMath::IsNearlyEqual(Empty.MassKilograms, Propulsion.DryMassKilograms, 1.0e-3f));

	const FVector VelocityAfterDepletion = Fixture.Flight->Velocity;
	Fixture.Advance(EdenRuntimeCompositionTests::FFlightPropellantFixture::FullBurn(), 3);
	TestTrue(TEXT("Burning on an empty tank does not accelerate"), Fixture.Flight->Velocity.Equals(VelocityAfterDepletion, 1.0e-9));
	TestEqual(TEXT("Burning on an empty tank reads zero demand"), Fixture.Flight->GetPropulsionDemandNormalized(), 0.0f);
	TestEqual(TEXT("Propellant never goes negative"), Fixture.Fuel->GetPropellantMassKilograms(), 0.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenRuntimeCompositionPowerLimitsDeliveredPropulsionTest,
	"Eden.Integration.Flight.PowerLimitsDeliveredPropulsion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenRuntimeCompositionPowerLimitsDeliveredPropulsionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenRuntimeCompositionTests::FFlightPropellantFixture Fixture;
	if (!TestTrue(TEXT("Blueprint pawn with fuel and power runs"), Fixture.Setup(*this)))
	{
		return false;
	}

	const FEdenFlightPropulsionSettings Propulsion;
	const FEdenPowerConfig PowerConfig = Fixture.Power->GetActivePowerConfig();
	Fixture.Power->SetBatteryChargeKilowattHours(PowerConfig.BatteryCapacityKilowattHours * PowerConfig.CriticalThresholdFraction * 0.5f);
	TestTrue(TEXT("Battery is Critical"), Fixture.Power->GetPowerStateSnapshot().PowerState == EEdenPowerState::Critical);
	TestTrue(
		TEXT("Critical power limits thrust to CriticalPowerThrustFraction"),
		FMath::IsNearlyEqual(Fixture.Flight->GetPowerThrustFraction(), Propulsion.CriticalPowerThrustFraction));

	const double MassBefore = Fixture.Flight->GetMassKilograms();
	const double PropellantBefore = Fixture.Fuel->GetPropellantMassKilograms();
	Fixture.Advance(EdenRuntimeCompositionTests::FFlightPropellantFixture::FullBurn(), 1);

	// In double, as the model computes it (a float product would round 0.4f x 100 kN to exactly 40 kN).
	const double Delivered =
		static_cast<double>(Propulsion.ForwardThrustNewtons) * static_cast<double>(Propulsion.CriticalPowerThrustFraction);
	const double ExpectedSpeed = Delivered * 0.1 / MassBefore;
	const double ExpectedConsumed = FEdenFlightMovementModel::CalculatePropellantMassFlowKilogramsPerSecond(Delivered, Propulsion) * 0.1;
	const FEdenFlightDynamicsSnapshot Limited = Fixture.Flight->GetFlightDynamicsSnapshot();
	TestTrue(TEXT("Requested is the full 100 kN"), FMath::IsNearlyEqual(Limited.RequestedThrusterOutputNewtons, Propulsion.ForwardThrustNewtons, 1.0f));
	TestTrue(TEXT("Delivered is the power-limited 40 kN"), FMath::IsNearlyEqual(Limited.DeliveredThrusterOutputNewtons, static_cast<float>(Delivered), 1.0f));
	TestTrue(TEXT("Snapshot is power-limited"), Limited.bPowerLimited);
	TestTrue(
		FString::Printf(TEXT("Acceleration uses delivered thrust (%.6f vs %.6f m/s)"), Fixture.Flight->GetSpeedMetersPerSecond(), ExpectedSpeed),
		FMath::IsNearlyEqual(static_cast<double>(Fixture.Flight->GetSpeedMetersPerSecond()), ExpectedSpeed, ExpectedSpeed * 1.0e-4));
	TestTrue(
		FString::Printf(TEXT("Propellant use follows delivered thrust (%.9f vs %.9f kg)"), PropellantBefore - Fixture.Fuel->GetPropellantMassKilograms(), ExpectedConsumed),
		FMath::IsNearlyEqual(PropellantBefore - Fixture.Fuel->GetPropellantMassKilograms(), ExpectedConsumed, ExpectedConsumed * 1.0e-9));

	Fixture.Power->SetBatteryChargeKilowattHours(0.0f);
	TestTrue(TEXT("Battery is Depleted"), Fixture.Power->GetPowerStateSnapshot().PowerState == EEdenPowerState::Depleted);
	const FVector VelocityUnpowered = Fixture.Flight->Velocity;
	const double PropellantUnpowered = Fixture.Fuel->GetPropellantMassKilograms();
	Fixture.Advance(EdenRuntimeCompositionTests::FFlightPropellantFixture::FullBurn(), 2);
	TestTrue(TEXT("Unpowered thrusters deliver nothing"), Fixture.Flight->Velocity.Equals(VelocityUnpowered, 1.0e-9));
	TestEqual(TEXT("Unpowered thrusters burn no propellant"), Fixture.Fuel->GetPropellantMassKilograms(), PropellantUnpowered);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenRuntimeCompositionResourceTransitionsStayConsistentTest,
	"Eden.Integration.Flight.ResourceTransitionsStayConsistent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenRuntimeCompositionResourceTransitionsStayConsistentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenRuntimeCompositionTests::FFlightPropellantFixture Fixture;
	if (!TestTrue(TEXT("Blueprint pawn with fuel and power runs"), Fixture.Setup(*this)))
	{
		return false;
	}

	const FEdenFlightPropulsionSettings Propulsion;
	const FEdenFlightInputCommand Burn = EdenRuntimeCompositionTests::FFlightPropellantFixture::FullBurn();
	const double FullFlow = FEdenFlightMovementModel::CalculatePropellantMassFlowKilogramsPerSecond(Propulsion.ForwardThrustNewtons, Propulsion);
	auto Propellant = [&Fixture]() { return Fixture.Fuel->GetPropellantMassKilograms(); };

	// 1. Interleaved 10 Hz resource ticks and 120 Hz flight for 5 s: propellant is billed once, by flight.
	double Before = Propellant();
	for (int32 Update = 0; Update < 50; ++Update)
	{
		Fixture.Advance(Burn, 1);
		Fixture.Clock->Tick(0.1f);
	}
	TestTrue(
		FString::Printf(TEXT("Interleaved resource ticks: 5 s burn draws exactly mdot x 5 s (%.9f vs %.9f kg)"), Before - Propellant(), FullFlow * 5.0),
		FMath::IsNearlyEqual(Before - Propellant(), FullFlow * 5.0, FullFlow * 5.0 * 1.0e-9));

	// 2. Pause: no flight steps, no draws, no motion; resume continues without catch-up.
	Before = Propellant();
	const FVector VelocityBeforePause = Fixture.Flight->Velocity;
	Fixture.Clock->PauseSimulation();
	Fixture.Advance(Burn, 5);
	Fixture.Clock->Tick(0.5f);
	TestEqual(TEXT("Paused: no propellant drawn"), Propellant(), Before);
	TestTrue(TEXT("Paused: velocity unchanged"), Fixture.Flight->Velocity.Equals(VelocityBeforePause, 0.0));
	Fixture.Clock->ResumeSimulation();
	Fixture.Advance(Burn, 1);
	TestTrue(
		TEXT("Resumed: one update draws one update of propellant"),
		FMath::IsNearlyEqual(Before - Propellant(), FullFlow * 0.1, FullFlow * 0.1 * 1.0e-9));

	// 3. Power tiers in sequence, including recovery: delivered thrust and propellant follow each tier.
	const FEdenPowerConfig PowerConfig = Fixture.Power->GetActivePowerConfig();
	struct FTier
	{
		const TCHAR* Name;
		float ChargeFraction;
		EEdenPowerState ExpectedState;
		double ExpectedFraction;
	};
	const FTier Tiers[] = {
		{TEXT("Normal"), 1.0f, EEdenPowerState::Normal, 1.0},
		{TEXT("Critical"), PowerConfig.CriticalThresholdFraction * 0.5f, EEdenPowerState::Critical, Propulsion.CriticalPowerThrustFraction},
		{TEXT("Depleted"), 0.0f, EEdenPowerState::Depleted, Propulsion.DepletedPowerThrustFraction},
		{TEXT("Recharged"), 1.0f, EEdenPowerState::Normal, 1.0},
	};
	for (const FTier& Tier : Tiers)
	{
		Fixture.Power->SetBatteryChargeKilowattHours(PowerConfig.BatteryCapacityKilowattHours * Tier.ChargeFraction);
		TestTrue(FString::Printf(TEXT("%s: power state"), Tier.Name), Fixture.Power->GetPowerStateSnapshot().PowerState == Tier.ExpectedState);
		Before = Propellant();
		Fixture.Advance(Burn, 1);
		const double ExpectedFlow = FullFlow * Tier.ExpectedFraction;
		TestTrue(
			FString::Printf(TEXT("%s: delivered output is %.0f%% of request"), Tier.Name, Tier.ExpectedFraction * 100.0),
			FMath::IsNearlyEqual(
				static_cast<double>(Fixture.Flight->GetFlightDynamicsSnapshot().DeliveredThrusterOutputNewtons),
				Propulsion.ForwardThrustNewtons * Tier.ExpectedFraction,
				1.0));
		TestTrue(
			FString::Printf(TEXT("%s: propellant follows delivered thrust"), Tier.Name),
			FMath::IsNearlyEqual(Before - Propellant(), ExpectedFlow * 0.1, FMath::Max(ExpectedFlow * 0.1 * 1.0e-9, 1.0e-15)));
	}

	// 3b. Natural recovery: from a dead battery, the real power simulation (baseline load only) must bring
	// propulsion back by itself; Depleted is not a permanent loss of control.
	Fixture.Power->SetBatteryChargeKilowattHours(0.0f);
	TestEqual(TEXT("Dead battery: no propulsion"), Fixture.Flight->GetPowerThrustFraction(), 0.0f);
	double SecondsToRecover = -1.0;
	for (int32 Tick = 0; Tick < 6000 && SecondsToRecover < 0.0; ++Tick)
	{
		Fixture.Clock->Tick(0.1f);
		if (Fixture.Flight->GetPowerThrustFraction() > 0.0f)
		{
			SecondsToRecover = (Tick + 1) * 0.1;
		}
	}
	const double NetKilowatts = Fixture.Power->GetPowerStateSnapshot().NetPowerKilowatts;
	const double KilowattHoursToLeaveCritical = PowerConfig.BatteryCapacityKilowattHours * PowerConfig.CriticalThresholdFraction;
	AddInfo(FString::Printf(
		TEXT("Dead battery recovery: propulsion back after %.1f s at the Critical tier (generation %.2f kW, demand %.2f kW, net %.2f kW). ")
		TEXT("Battery %.2f kWh, Critical below %.0f%% (%.3f kWh): full thrust returns after ~%.1f min at baseline load."),
		SecondsToRecover,
		Fixture.Power->GetPowerStateSnapshot().GenerationKilowatts,
		Fixture.Power->GetPowerStateSnapshot().TotalDemandKilowatts,
		NetKilowatts,
		PowerConfig.BatteryCapacityKilowattHours,
		PowerConfig.CriticalThresholdFraction * 100.0f,
		KilowattHoursToLeaveCritical,
		NetKilowatts > 0.0 ? KilowattHoursToLeaveCritical / NetKilowatts * 60.0 : -1.0));
	TestTrue(TEXT("Dead battery recovers propulsion within 10 minutes of simulated time"), SecondsToRecover > 0.0);
	Fixture.Power->SetBatteryChargeKilowattHours(PowerConfig.BatteryCapacityKilowattHours);

	// 4. Load shed during Critical power: assist removed and thrust limited at the same time.
	UEdenOperatorControlComponent* Operator = Fixture.Pawn->GetOperatorControlComponent();
	TestTrue(TEXT("Operator control enabled"), Operator && Operator->IsOperatorControlEnabled());
	if (Operator)
	{
		Fixture.Power->SetBatteryChargeKilowattHours(PowerConfig.BatteryCapacityKilowattHours * PowerConfig.CriticalThresholdFraction * 0.5f);
		Operator->SetLoadShedMode(EEdenLoadShedMode::Shed);
		FEdenFlightInputCommand AssistedHold;
		AssistedHold.bStabilizationEnabled = true;
		const FVector DriftVelocity = Fixture.Flight->Velocity;
		Before = Propellant();
		Fixture.Advance(AssistedHold, 3);
		TestFalse(TEXT("Shed + Critical: assist inactive"), Fixture.Flight->GetFlightDynamicsSnapshot().bFlightAssistActive);
		TestTrue(TEXT("Shed + Critical: ship drifts"), Fixture.Flight->Velocity.Equals(DriftVelocity, 1.0e-9));
		TestEqual(TEXT("Shed + Critical: no propellant drawn"), Propellant(), Before);
		Operator->SetLoadShedMode(EEdenLoadShedMode::Normal);
		Fixture.Power->SetBatteryChargeKilowattHours(PowerConfig.BatteryCapacityKilowattHours);
	}

	// 5. Mission reset leaves the fuel ledger alone; a fuel reset restores it and flight follows immediately.
	UEdenMissionSubsystem* Mission = Fixture.Pawn->GetWorld()->GetSubsystem<UEdenMissionSubsystem>();
	UEdenMissionDefinitionDataAsset* SolarEvent =
		Cast<UEdenMissionDefinitionDataAsset>(FSoftObjectPath(EdenRuntimeCompositionTests::SolarEventMissionPath).TryLoad());
	if (TestNotNull(TEXT("Mission subsystem"), Mission) && TestNotNull(TEXT("Solar Event asset"), SolarEvent))
	{
		Mission->SetMissionResourceTargets(Fixture.Pawn->GetThermalSystemComponent(), Fixture.Power, Fixture.Fuel);
		Mission->LoadMissionFromDefinitionAsset(SolarEvent);
		Mission->StartMission();
		Before = Propellant();
		Mission->AbortMission();
		Mission->ResetMission();
		TestEqual(TEXT("Mission abort/reset does not touch stored propellant"), Propellant(), Before);
	}
	TestTrue(TEXT("Fuel reset succeeds"), Fixture.Fuel->ResetFuelState());
	const double Restored = Propellant();
	TestTrue(
		TEXT("Flight mass follows the reset ledger immediately"),
		FMath::IsNearlyEqual(static_cast<double>(Fixture.Flight->GetMassKilograms()), Propulsion.DryMassKilograms + Restored, 1.0e-3));
	Fixture.Advance(Burn, 1);
	TestTrue(
		TEXT("After fuel reset, draws come from the restored ledger"),
		FMath::IsNearlyEqual(Restored - Propellant(), FullFlow * 0.1, FullFlow * 0.1 * 1.0e-9));

	return true;
}

namespace EdenRuntimeCompositionTests
{
const TCHAR* HudWidgetClassPath = TEXT("/Game/Eden/UI/WBP_EdenOperatorHud.WBP_EdenOperatorHud_C");

AEdenFlightPlayerController* PossessWithController(FFlightPropellantFixture& Fixture)
{
	AEdenFlightPlayerController* Controller = Fixture.Pawn->GetWorld()->SpawnActor<AEdenFlightPlayerController>(
		AEdenFlightPlayerController::StaticClass(),
		FTransform::Identity);
	if (Controller)
	{
		EnsureBeginPlay(Controller);
		Controller->Possess(Fixture.Pawn);
	}
	return Controller;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenHudPanelsExistInFirstSlateBuildTest,
	"Eden.Integration.Hud.PanelsExistInFirstSlateBuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenHudPanelsExistInFirstSlateBuildTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenRuntimeCompositionTests::FScopedRuntimeWorld RuntimeWorld;
	UWorld* World = RuntimeWorld.Get();
	UClass* HudClass = StaticLoadClass(UEdenOperatorHudWidget::StaticClass(), nullptr, EdenRuntimeCompositionTests::HudWidgetClassPath);
	if (!TestNotNull(TEXT("WBP_EdenOperatorHud loads"), HudClass) || !World)
	{
		return false;
	}

	// Root cause: WBP_EdenOperatorHud has no designer tree. The panels must already be in the tree when
	// Slate builds the widget the first time; otherwise the HUD exists but renders nothing.
	UEdenOperatorHudWidget* Hud = CreateWidget<UEdenOperatorHudWidget>(World, HudClass);
	if (!TestNotNull(TEXT("HUD widget is created"), Hud))
	{
		return false;
	}
	TestEqual(TEXT("No Slate widget before the first build"), Hud->GetBuiltPanelCount(), 0);
	// Hold the Slate widget: UWidget keeps only a weak reference, as a viewport or parent would hold it in game.
	const TSharedRef<SWidget> SlateHolder = Hud->TakeWidget();
	TestEqual(TEXT("All five panels exist in the very first Slate build"), Hud->GetBuiltPanelCount(), 5);
	const FString Layout = Hud->DescribeLayoutForDiagnostics();
	AddInfo(FString::Printf(TEXT("HUD layout after first build: %s"), *Layout));
	TestTrue(TEXT("Root is the HUD canvas"), Layout.Contains(TEXT("rootIsHudCanvas=true")));
	TestTrue(TEXT("Slate widget is built"), Layout.Contains(TEXT("slateBuilt=true")));

	// Feedback banner shows immediately and expires on its own.
	const double Now = World->GetRealTimeSeconds();
	Hud->ShowControlFeedback(FText::FromString(TEXT("[L] LOAD SHED ON")), true, Now);
	TestEqual(TEXT("Feedback is visible immediately"), Hud->GetControlFeedbackText().ToString(), FString(TEXT("[L] LOAD SHED ON")));
	Hud->ShowControlFeedback(FText::FromString(TEXT("stale")), true, Now - 60.0);
	Hud->ApplyHudSnapshot(FEdenOperatorHudSnapshot());
	TestTrue(TEXT("Expired feedback hides on the next refresh"), Hud->GetControlFeedbackText().IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenHudFormattersDistinguishUnavailableFromZeroTest,
	"Eden.Unit.Hud.FormattersDistinguishUnavailableFromZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenHudFormattersDistinguishUnavailableFromZeroTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// No sources at all: every panel says so; nothing reads as a real zero.
	const FEdenOperatorHudSnapshot Empty;
	TestTrue(TEXT("Flight unavailable"), UEdenOperatorHudWidget::FormatFlightPanel(Empty).StartsWith(TEXT("--")));
	TestTrue(TEXT("Propellant unavailable"), UEdenOperatorHudWidget::FormatSystemsPanel(Empty).Contains(TEXT("PROPELLANT  --")));
	TestTrue(TEXT("Battery unavailable"), UEdenOperatorHudWidget::FormatSystemsPanel(Empty).Contains(TEXT("BATTERY     --")));
	TestTrue(TEXT("Thrust unavailable"), UEdenOperatorHudWidget::FormatSystemsPanel(Empty).Contains(TEXT("THRUST      --")));
	TestTrue(TEXT("Operator unavailable"), UEdenOperatorHudWidget::FormatOperatorPanel(Empty).Contains(TEXT("operator control disabled")));
	TestTrue(TEXT("Mission unavailable"), UEdenOperatorHudWidget::FormatMissionPanel(Empty).StartsWith(TEXT("--")));
	TestTrue(TEXT("EDEN unavailable"), UEdenOperatorHudWidget::FormatEdenPanel(Empty).StartsWith(TEXT("--")));

	// Real values, including genuine zeros, are shown as values.
	FEdenOperatorHudSnapshot Live;
	Live.bHasFlight = true;
	Live.bHasFuel = true;
	Live.bHasPower = true;
	Live.bHasThermal = true;
	Live.bHasOperator = true;
	Live.bHasMission = true;
	Live.bHasEdenAdapter = true;
	Live.Flight.VelocityWorldMetersPerSecond = FVector(125.4, 0.0, 0.0);
	Live.Flight.AngularVelocityBodyRadiansPerSecond = FVector(0.0, 0.0, FMath::DegreesToRadians(4.2));
	Live.Flight.RequestedThrusterOutputNewtons = 100000.0f;
	Live.Flight.DeliveredThrusterOutputNewtons = 40000.0f;
	Live.Flight.PowerThrustFraction = 0.4f;
	Live.Flight.bPowerLimited = true;
	Live.Flight.bPropellantMetered = true;
	Live.Flight.PropellantFlowKilogramsPerSecond = 0.4f;
	Live.Flight.bFlightAssistRequested = true;
	Live.Flight.bFlightAssistActive = true;
	Live.Fuel.FuelQuantityKilograms = 0.0f;
	Live.Fuel.FuelFraction = 0.0f;
	Live.Fuel.FuelState = EEdenFuelState::Depleted;
	Live.FuelCapacityKilograms = 100.0f;

	const FString Flight = UEdenOperatorHudWidget::FormatFlightPanel(Live);
	const FString Systems = UEdenOperatorHudWidget::FormatSystemsPanel(Live);
	TestTrue(TEXT("Speed is shown"), Flight.Contains(TEXT("125.40 m/s")));
	TestTrue(TEXT("Yaw rate is shown in the pilot convention"), Flight.Contains(TEXT("yaw   +4.2")));
	TestTrue(TEXT("Assist active is shown"), Flight.Contains(TEXT("ASSIST    ON")));
	TestTrue(TEXT("Delivered vs requested thrust"), Systems.Contains(TEXT("delivered 40.0 kN / requested 100.0 kN")));
	TestTrue(TEXT("Power limit is flagged"), Systems.Contains(TEXT("POWER-LIMITED")));
	TestTrue(TEXT("An empty tank is a real zero, not unavailable"), Systems.Contains(TEXT("PROPELLANT  0.00 / 100 kg (0%)  DEPLETED")));
	TestTrue(TEXT("Flow is shown"), Systems.Contains(TEXT("0.400 kg/s")));
	TestTrue(TEXT("Inactive mission says how to start one"), UEdenOperatorHudWidget::FormatMissionPanel(Live).Contains(TEXT("StartMission")));
	TestTrue(TEXT("EDEN offline is shown truthfully"), UEdenOperatorHudWidget::FormatEdenPanel(Live).Contains(TEXT("OFFLINE")));
	TestTrue(TEXT("No advisory is invented"), UEdenOperatorHudWidget::FormatEdenPanel(Live).Contains(TEXT("none received")));
	TestTrue(TEXT("Command execution off is shown"), UEdenOperatorHudWidget::FormatEdenPanel(Live).Contains(TEXT("command execution OFF")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenHudControlsShowConfirmedStateTest,
	"Eden.Integration.Hud.ControlsShowConfirmedState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenHudControlsShowConfirmedStateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenRuntimeCompositionTests::FFlightPropellantFixture Fixture;
	if (!TestTrue(TEXT("Blueprint pawn runs"), Fixture.Setup(*this)))
	{
		return false;
	}
	AEdenFlightPlayerController* Controller = EdenRuntimeCompositionTests::PossessWithController(Fixture);
	UEdenOperatorControlComponent* Operator = Fixture.Pawn->GetOperatorControlComponent();
	if (!TestNotNull(TEXT("Controller possesses the pawn"), Controller) || !Operator)
	{
		return false;
	}

	// L: confirmed shed state, and the gameplay effect actually happened.
	Controller->EdenToggleLoadShed();
	TestTrue(TEXT("L sheds load in the operator component"), Operator->GetOperatorIntent().LoadShedMode == EEdenLoadShedMode::Shed);
	TestTrue(TEXT("L feedback confirms LOAD SHED ON"), Controller->GetLastControlFeedback().Contains(TEXT("LOAD SHED ON")));
	TestTrue(TEXT("L feedback is a success"), Controller->WasLastControlFeedbackSuccessful());
	TestFalse(TEXT("Shed removes assist in flight"), Fixture.Flight->IsStabilizationAssistAvailable());

	// X: off, then on-but-unavailable (with the real reason).
	Controller->EdenToggleFlightAssist();
	TestTrue(TEXT("X confirms FLIGHT ASSIST OFF"), Controller->GetLastControlFeedback().Contains(TEXT("FLIGHT ASSIST OFF")));
	Controller->EdenToggleFlightAssist();
	TestTrue(TEXT("X while shed explains why assist is unavailable"), Controller->GetLastControlFeedback().Contains(TEXT("UNAVAILABLE: load shed")));
	TestFalse(TEXT("Unavailable assist is not reported as success"), Controller->WasLastControlFeedbackSuccessful());

	Controller->EdenToggleLoadShed();
	TestTrue(TEXT("L confirms LOAD SHED OFF"), Controller->GetLastControlFeedback().Contains(TEXT("LOAD SHED OFF")));

	// T: the reported mode is the operator component's resulting mode.
	Controller->EdenCycleThermalMode();
	const FString ThermalName = UEnum::GetDisplayValueAsText(Operator->GetOperatorIntent().ThermalMode).ToString();
	TestTrue(
		FString::Printf(TEXT("T feedback names the resulting mode (%s)"), *ThermalName),
		Controller->GetLastControlFeedback().Contains(FString::Printf(TEXT("THERMAL MODE: %s"), *ThermalName)));

	// P: authority in the feedback is the authority the flight model now has.
	Controller->EdenTogglePropulsionPriority();
	TestTrue(TEXT("P reduces propulsion priority"), Operator->GetOperatorIntent().PropulsionPriority == EEdenPropulsionPriorityMode::Reduced);
	TestTrue(
		TEXT("P feedback reports the real operator authority"),
		Controller->GetLastControlFeedback().Contains(FString::Printf(TEXT("operator authority %.0f%%"), Fixture.Flight->GetThrustAuthority() * 100.0f)));

	// The HUD snapshot was refreshed immediately with the confirmed state.
	TestTrue(
		TEXT("HUD snapshot refreshed immediately after P"),
		Controller->GetOperatorHudSnapshot().Operator.PropulsionPriority == EEdenPropulsionPriorityMode::Reduced);

	// A ship whose systems are not configured (bare C++ pawn: no power/thermal Data Assets) reports the actual reason.
	EdenRuntimeCompositionTests::FScopedRuntimeWorld BareWorld;
	AEdenSpacecraftPawn* BarePawn = BareWorld.Get()->SpawnActor<AEdenSpacecraftPawn>(AEdenSpacecraftPawn::StaticClass(), FTransform::Identity);
	AEdenFlightPlayerController* BareController =
		BareWorld.Get()->SpawnActor<AEdenFlightPlayerController>(AEdenFlightPlayerController::StaticClass(), FTransform::Identity);
	BareWorld.Get()->BeginPlay();
	EdenRuntimeCompositionTests::EnsureBeginPlay(BarePawn);
	EdenRuntimeCompositionTests::EnsureBeginPlay(BareController);
	BareController->Possess(BarePawn);
	BareController->EdenToggleLoadShed();
	TestFalse(TEXT("Unconfigured systems: not reported as success"), BareController->WasLastControlFeedbackSuccessful());
	TestTrue(
		FString::Printf(TEXT("Unconfigured systems: the real reason is shown (got: %s)"), *BareController->GetLastControlFeedback()),
		BareController->GetLastControlFeedback().Contains(TEXT("power system is not simulating")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenHudSnapshotTracksResourceChangesTest,
	"Eden.Integration.Hud.SnapshotTracksResourceChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenHudSnapshotTracksResourceChangesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenRuntimeCompositionTests::FFlightPropellantFixture Fixture;
	if (!TestTrue(TEXT("Blueprint pawn runs"), Fixture.Setup(*this)))
	{
		return false;
	}
	AEdenFlightPlayerController* Controller = EdenRuntimeCompositionTests::PossessWithController(Fixture);
	if (!TestNotNull(TEXT("Controller possesses the pawn"), Controller))
	{
		return false;
	}

	Fixture.Advance(EdenRuntimeCompositionTests::FFlightPropellantFixture::FullBurn(), 10);
	Controller->RefreshOperatorHudNow();
	FEdenOperatorHudSnapshot Hud = Controller->GetOperatorHudSnapshot();
	TestTrue(TEXT("HUD has flight data"), Hud.bHasFlight && Hud.bHasFuel && Hud.bHasPower && Hud.bHasThermal && Hud.bHasOperator);
	TestTrue(
		TEXT("HUD speed is the movement component's speed"),
		FMath::IsNearlyEqual(Hud.Flight.SpeedMetersPerSecond, Fixture.Flight->GetSpeedMetersPerSecond()));
	TestTrue(
		TEXT("HUD propellant is the fuel system's propellant"),
		FMath::IsNearlyEqual(Hud.Fuel.FuelQuantityKilograms, static_cast<float>(Fixture.Fuel->GetPropellantMassKilograms()), 1.0e-3f));
	TestTrue(TEXT("HUD shows delivered thrust during the burn"), Hud.Flight.DeliveredThrusterOutputNewtons > 0.0f);

	// Fuel exhaustion.
	Controller->EdenSetFuelKilograms(0.0f);
	Fixture.Advance(EdenRuntimeCompositionTests::FFlightPropellantFixture::FullBurn(), 1);
	Controller->RefreshOperatorHudNow();
	const FString EmptyTank = UEdenOperatorHudWidget::FormatSystemsPanel(Controller->GetOperatorHudSnapshot());
	TestTrue(TEXT("Empty tank: HUD flags NO PROPELLANT"), EmptyTank.Contains(TEXT("NO PROPELLANT")));
	TestTrue(TEXT("Empty tank: HUD shows zero delivered with a real request"), EmptyTank.Contains(TEXT("delivered 0.0 kN / requested 100.0 kN")));

	// Power tier.
	Controller->EdenSetFuelKilograms(100.0f);
	const FEdenPowerConfig PowerConfig = Fixture.Power->GetActivePowerConfig();
	Controller->EdenSetBatteryFraction(PowerConfig.CriticalThresholdFraction * 0.5f);
	Fixture.Advance(EdenRuntimeCompositionTests::FFlightPropellantFixture::FullBurn(), 1);
	Controller->RefreshOperatorHudNow();
	const FString Critical = UEdenOperatorHudWidget::FormatSystemsPanel(Controller->GetOperatorHudSnapshot());
	TestTrue(TEXT("Critical power: HUD shows CRITICAL"), Critical.Contains(TEXT("CRITICAL")));
	TestTrue(TEXT("Critical power: HUD flags POWER-LIMITED"), Critical.Contains(TEXT("POWER-LIMITED")));

	// Mission start and reset.
	UEdenMissionSubsystem* Mission = Fixture.Pawn->GetWorld()->GetSubsystem<UEdenMissionSubsystem>();
	UEdenMissionDefinitionDataAsset* SolarEvent =
		Cast<UEdenMissionDefinitionDataAsset>(FSoftObjectPath(EdenRuntimeCompositionTests::SolarEventMissionPath).TryLoad());
	if (Mission && SolarEvent)
	{
		Mission->SetMissionResourceTargets(Fixture.Pawn->GetThermalSystemComponent(), Fixture.Power, Fixture.Fuel);
		Mission->LoadMissionFromDefinitionAsset(SolarEvent);
		Mission->StartMission();
		Controller->RefreshOperatorHudNow();
		const FString Running = UEdenOperatorHudWidget::FormatMissionPanel(Controller->GetOperatorHudSnapshot());
		TestTrue(TEXT("HUD shows the running mission"), Running.Contains(TEXT("SolarCrisis")) && Running.Contains(TEXT("RUNNING")));
		Mission->AbortMission();
		Mission->ResetMission();
		Controller->RefreshOperatorHudNow();
		TestTrue(
			TEXT("HUD shows no active mission after reset"),
			UEdenOperatorHudWidget::FormatMissionPanel(Controller->GetOperatorHudSnapshot()).Contains(TEXT("NO ACTIVE MISSION")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenHudNeverAltersFlightPhysicsTest,
	"Eden.Integration.Hud.NeverAltersFlightPhysics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenHudNeverAltersFlightPhysicsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenFlightInputCommand Manoeuvre;
	Manoeuvre.TranslationInput = FVector(1.0, 0.3, 0.0);
	Manoeuvre.RotationInput = FVector(0.2, 1.0, -0.4);
	Manoeuvre.bStabilizationEnabled = true;

	// Same ship, same commands; one has a controller refreshing the HUD every update, one has none.
	EdenRuntimeCompositionTests::FFlightPropellantFixture WithHud;
	EdenRuntimeCompositionTests::FFlightPropellantFixture WithoutHud;
	if (!WithHud.Setup(*this) || !WithoutHud.Setup(*this))
	{
		return false;
	}
	AEdenFlightPlayerController* Controller = EdenRuntimeCompositionTests::PossessWithController(WithHud);
	if (!TestNotNull(TEXT("Controller possesses the HUD ship"), Controller))
	{
		return false;
	}

	for (int32 Update = 0; Update < 30; ++Update)
	{
		WithHud.Advance(Manoeuvre, 1);
		Controller->RefreshOperatorHudNow();
		WithoutHud.Advance(Manoeuvre, 1);
	}

	TestTrue(TEXT("Velocity identical with and without HUD"), WithHud.Flight->Velocity == WithoutHud.Flight->Velocity);
	TestTrue(TEXT("Orientation identical with and without HUD"), WithHud.Pawn->GetActorQuat() == WithoutHud.Pawn->GetActorQuat());
	TestTrue(
		TEXT("Angular velocity identical with and without HUD"),
		WithHud.Flight->GetAngularVelocityBodyRadiansPerSecond() == WithoutHud.Flight->GetAngularVelocityBodyRadiansPerSecond());
	TestEqual(TEXT("Propellant identical with and without HUD"), WithHud.Fuel->GetPropellantMassKilograms(), WithoutHud.Fuel->GetPropellantMassKilograms());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenRuntimeCompositionOperatorModifiersDeferredUntilResourcesReadyTest,
	"Eden.Integration.Operations.OperatorModifiersDeferredUntilResourcesReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenRuntimeCompositionOperatorModifiersDeferredUntilResourcesReadyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenRuntimeCompositionTests::FScopedRuntimeWorld RuntimeWorld;
	UWorld* World = RuntimeWorld.Get();
	if (!World)
	{
		return false;
	}

	FEdenOperatorControlConfig OperatorConfig;
	OperatorConfig.LoadShedDemandReductionKilowatts = 3.0f;
	FEdenPowerConfig PowerConfig;
	FEdenThermalConfig ThermalConfig;

	auto MakeShip = [World]()
	{
		AActor* Ship = World->SpawnActor<AActor>();
		USceneComponent* Root = NewObject<USceneComponent>(Ship, TEXT("Root"));
		Ship->SetRootComponent(Root);
		Root->RegisterComponent();
		for (UClass* ComponentClass : {
				 UEdenPowerSystemComponent::StaticClass(),
				 UEdenThermalSystemComponent::StaticClass(),
				 UEdenFlightMovementComponent::StaticClass(),
				 UEdenOperatorControlComponent::StaticClass()})
		{
			UActorComponent* Component = NewObject<UActorComponent>(Ship, ComponentClass);
			Component->RegisterComponent();
		}
		return Ship;
	};

	// 1. Startup ordering: power and thermal have not begun play when the operator applies its intent.
	AActor* Ship = MakeShip();
	UEdenPowerSystemComponent* Power = Ship->FindComponentByClass<UEdenPowerSystemComponent>();
	UEdenThermalSystemComponent* Thermal = Ship->FindComponentByClass<UEdenThermalSystemComponent>();
	UEdenOperatorControlComponent* Operator = Ship->FindComponentByClass<UEdenOperatorControlComponent>();
	TestTrue(TEXT("Operator initializes while resources are not ready (no failure)"), Operator->InitializeOperatorControl(OperatorConfig));
	TestTrue(TEXT("Modifiers are pending, not dropped"), Operator->IsIntentApplyPending());
	TestTrue(TEXT("Load shed is accepted while pending"), Operator->SetLoadShedMode(EEdenLoadShedMode::Shed));
	TestTrue(TEXT("Still pending until resources are ready"), Operator->IsIntentApplyPending());

	TestTrue(TEXT("Power becomes ready"), Power->InitializePowerSimulation(PowerConfig));
	TestTrue(TEXT("Thermal becomes ready"), Thermal->InitializeThermalSimulation(ThermalConfig));
	TestTrue(TEXT("Deferred modifiers apply once resources are ready"), Operator->ApplyPendingIntent());
	TestFalse(TEXT("Nothing pending after apply"), Operator->IsIntentApplyPending());
	TestTrue(
		TEXT("Power received the deferred load-shed reduction"),
		FMath::IsNearlyEqual(
			Power->GetPowerStateSnapshot().OperatorDemandKilowatts,
			OperatorConfig.NominalCoolingDemandKilowatts - OperatorConfig.LoadShedDemandReductionKilowatts));

	// 2. Genuine failure: power began play but stayed disabled (no config). Not pending; reported as failure.
	AActor* BrokenShip = MakeShip();
	UEdenPowerSystemComponent* BrokenPower = BrokenShip->FindComponentByClass<UEdenPowerSystemComponent>();
	UEdenThermalSystemComponent* BrokenThermal = BrokenShip->FindComponentByClass<UEdenThermalSystemComponent>();
	UEdenOperatorControlComponent* BrokenOperator = BrokenShip->FindComponentByClass<UEdenOperatorControlComponent>();
	EdenRuntimeCompositionTests::EnsureBeginPlay(BrokenShip);
	TestTrue(TEXT("Broken power has begun play"), BrokenPower->HasBegunPlay());
	TestFalse(TEXT("Broken power is disabled"), BrokenPower->IsPowerSimulationEnabled());
	TestTrue(TEXT("Thermal is ready on the broken ship"), BrokenThermal->InitializeThermalSimulation(ThermalConfig));
	TestFalse(TEXT("A genuinely disabled resource still fails operator apply"), BrokenOperator->InitializeOperatorControl(OperatorConfig));
	TestFalse(TEXT("A genuine failure is never marked pending"), BrokenOperator->IsIntentApplyPending());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenRuntimeCompositionDeveloperResourceCommandsTest,
	"Eden.Integration.Flight.DeveloperResourceCommands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenRuntimeCompositionDeveloperResourceCommandsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenRuntimeCompositionTests::FFlightPropellantFixture Fixture;
	if (!TestTrue(TEXT("Blueprint pawn with fuel and power runs"), Fixture.Setup(*this)))
	{
		return false;
	}

	AEdenFlightPlayerController* Controller = Fixture.Pawn->GetWorld()->SpawnActor<AEdenFlightPlayerController>(
		AEdenFlightPlayerController::StaticClass(),
		FTransform::Identity);
	if (!TestNotNull(TEXT("Controller spawns"), Controller))
	{
		return false;
	}
	EdenRuntimeCompositionTests::EnsureBeginPlay(Controller);
	Controller->Possess(Fixture.Pawn);

	// The PIE checklist relies on these to reach empty-tank and power-tier states quickly.
	Controller->EdenSetFuelKilograms(2.5f);
	TestTrue(TEXT("EdenSetFuelKilograms sets the fuel ledger"), FMath::IsNearlyEqual(Fixture.Fuel->GetPropellantMassKilograms(), 2.5, 1.0e-6));
	Controller->EdenSetFuelKilograms(0.0f);
	TestEqual(TEXT("EdenSetFuelKilograms 0 empties the tank"), Fixture.Fuel->GetPropellantMassKilograms(), 0.0);

	const FEdenPowerConfig PowerConfig = Fixture.Power->GetActivePowerConfig();
	Controller->EdenSetBatteryFraction(PowerConfig.CriticalThresholdFraction * 0.5f);
	TestTrue(TEXT("EdenSetBatteryFraction reaches Critical"), Fixture.Power->GetPowerStateSnapshot().PowerState == EEdenPowerState::Critical);
	Controller->EdenSetBatteryFraction(0.0f);
	TestTrue(TEXT("EdenSetBatteryFraction 0 reaches Depleted"), Fixture.Power->GetPowerStateSnapshot().PowerState == EEdenPowerState::Depleted);
	Controller->EdenSetBatteryFraction(1.0f);
	TestTrue(TEXT("EdenSetBatteryFraction 1 restores Normal"), Fixture.Power->GetPowerStateSnapshot().PowerState == EEdenPowerState::Normal);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenRuntimeCompositionSolarCrisisWithNewtonianPropulsionTest,
	"Eden.Scenario.Flight.SolarCrisisWithNewtonianPropulsion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenRuntimeCompositionSolarCrisisWithNewtonianPropulsionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	struct FProfile
	{
		const TCHAR* Name;
		bool bShedDuringImpact;
		EEdenThermalControlMode ImpactThermalMode;
	};
	const FProfile Profiles[] = {
		{TEXT("Passive operator"), false, EEdenThermalControlMode::Nominal},
		{TEXT("Shed during impact"), true, EEdenThermalControlMode::Nominal},
		{TEXT("Shed + thermal boost"), true, EEdenThermalControlMode::Boost},
	};

	int32 SucceededProfiles = 0;
	for (const FProfile& Profile : Profiles)
	{
		EdenRuntimeCompositionTests::FFlightPropellantFixture Fixture;
		if (!TestTrue(FString::Printf(TEXT("%s: pawn runs"), Profile.Name), Fixture.Setup(*this)))
		{
			return false;
		}

		UEdenOperatorControlComponent* Operator = Fixture.Pawn->GetOperatorControlComponent();
		UEdenMissionSubsystem* Mission = Fixture.Pawn->GetWorld()->GetSubsystem<UEdenMissionSubsystem>();
		UEdenMissionDefinitionDataAsset* SolarEvent =
			Cast<UEdenMissionDefinitionDataAsset>(FSoftObjectPath(EdenRuntimeCompositionTests::SolarEventMissionPath).TryLoad());
		if (!Operator || !Mission || !SolarEvent)
		{
			return false;
		}

		Mission->SetMissionResourceTargets(Fixture.Pawn->GetThermalSystemComponent(), Fixture.Power, Fixture.Fuel);
		TestTrue(FString::Printf(TEXT("%s: mission loads"), Profile.Name), Mission->LoadMissionFromDefinitionAsset(SolarEvent));
		TestTrue(FString::Printf(TEXT("%s: mission starts"), Profile.Name), Mission->StartMission());

		// A pilot who keeps manoeuvring: 1 s burn + yaw every 5 s, assisted hold otherwise.
		FEdenFlightInputCommand Manoeuvre;
		Manoeuvre.TranslationInput = FVector(1.0, 0.0, 0.0);
		Manoeuvre.RotationInput = FVector(0.0, 1.0, 0.0);
		Manoeuvre.bStabilizationEnabled = true;
		FEdenFlightInputCommand Hold;
		Hold.bStabilizationEnabled = true;

		bool bActionsApplied = false;
		bool bRestoredAfterImpact = false;
		double MinPowerFraction = 1.0;
		double SecondsWithoutPropulsion = 0.0;
		double LastSecondWithoutPropulsion = -1.0;
		double SecondsPowerLimited = 0.0;
		const double InitialPropellant = Fixture.Fuel->GetPropellantMassKilograms();
		EEdenMissionState TerminalState = EEdenMissionState::Running;
		constexpr int32 Updates = 900; // 90 s: mission plus 30+ s of aftermath
		for (int32 Update = 0; Update < Updates; ++Update)
		{
			const double Seconds = Update * 0.1;
			const EEdenMissionPhase Phase = Mission->GetMissionPhase();
			if (!bActionsApplied && Phase == EEdenMissionPhase::Impact)
			{
				if (Profile.bShedDuringImpact)
				{
					Operator->SetLoadShedMode(EEdenLoadShedMode::Shed);
				}
				Operator->SetThermalControlMode(Profile.ImpactThermalMode);
				bActionsApplied = true;
			}
			if (bActionsApplied && !bRestoredAfterImpact && Phase != EEdenMissionPhase::Impact)
			{
				Operator->SetLoadShedMode(EEdenLoadShedMode::Normal);
				Operator->SetThermalControlMode(EEdenThermalControlMode::Nominal);
				bRestoredAfterImpact = true;
			}

			const bool bManoeuvring = FMath::Fmod(Seconds, 5.0) < 1.0;
			Fixture.Advance(bManoeuvring ? Manoeuvre : Hold, 1);
			Fixture.Clock->Tick(0.1f);

			const double PowerFraction = Fixture.Flight->GetPowerThrustFraction();
			MinPowerFraction = FMath::Min(MinPowerFraction, PowerFraction);
			SecondsPowerLimited += PowerFraction < 1.0 ? 0.1 : 0.0;
			if (PowerFraction <= 0.0)
			{
				SecondsWithoutPropulsion += 0.1;
				LastSecondWithoutPropulsion = Seconds;
			}

			const EEdenMissionState State = Mission->GetMissionState();
			if (TerminalState == EEdenMissionState::Running && (State == EEdenMissionState::Succeeded || State == EEdenMissionState::Failed))
			{
				TerminalState = State;
			}
		}

		const double FinalPowerFraction = Fixture.Flight->GetPowerThrustFraction();
		AddInfo(FString::Printf(
			TEXT("%s: mission %s; min power fraction %.2f; %.1f s power-limited; %.1f s with no propulsion (last at %.1f s); final power fraction %.2f (%s); propellant used %.2f kg"),
			Profile.Name,
			*UEnum::GetValueAsString(Mission->GetMissionState()),
			MinPowerFraction,
			SecondsPowerLimited,
			SecondsWithoutPropulsion,
			LastSecondWithoutPropulsion,
			FinalPowerFraction,
			*UEnum::GetValueAsString(Fixture.Power->GetPowerStateSnapshot().PowerState),
			InitialPropellant - Fixture.Fuel->GetPropellantMassKilograms()));

		TestTrue(
			FString::Printf(TEXT("%s: mission reaches a terminal state"), Profile.Name),
			TerminalState == EEdenMissionState::Succeeded || TerminalState == EEdenMissionState::Failed);
		TestTrue(
			FString::Printf(TEXT("%s: propulsion is available again after the event (no permanent loss of control)"), Profile.Name),
			FinalPowerFraction > 0.0);
		TestTrue(FString::Printf(TEXT("%s: propellant never negative"), Profile.Name), Fixture.Fuel->GetPropellantMassKilograms() >= 0.0);
		SucceededProfiles += TerminalState == EEdenMissionState::Succeeded ? 1 : 0;
	}

	TestTrue(TEXT("Solar Crisis remains completable with Newtonian propulsion"), SucceededProfiles > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenRuntimeCompositionSpacecraftBlueprintUsesValidFlightModelTest,
	"Eden.Integration.Flight.SpacecraftBlueprintUsesValidFlightModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenRuntimeCompositionSpacecraftBlueprintUsesValidFlightModelTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UClass* PawnClass = StaticLoadClass(
		AEdenSpacecraftPawn::StaticClass(),
		nullptr,
		EdenRuntimeCompositionTests::PawnBlueprintClassPath);
	TestNotNull(TEXT("BP_EdenSpacecraftPawn class loads"), PawnClass);
	const AEdenSpacecraftPawn* DefaultPawn = PawnClass ? PawnClass->GetDefaultObject<AEdenSpacecraftPawn>() : nullptr;
	const UEdenFlightMovementComponent* Flight = DefaultPawn ? DefaultPawn->GetFlightMovementComponent() : nullptr;
	TestNotNull(TEXT("Blueprint keeps the C++ flight movement component"), Flight);
	if (!Flight)
	{
		return false;
	}

	TestTrue(TEXT("Blueprint flight component is exactly the C++ class"), Flight->GetClass() == UEdenFlightMovementComponent::StaticClass());
	TestTrue(TEXT("Blueprint flight moves the C++ sphere root"), Flight->UpdatedComponent.Get() == static_cast<const USceneComponent*>(DefaultPawn->GetRequiredCollisionRoot()));

	const FEdenFlightStepSettings DefaultStep;
	const FEdenFlightPropulsionSettings DefaultPropulsion;
	const FEdenFlightAttitudeSettings DefaultAttitude;
	TestTrue(TEXT("Blueprint step settings are valid"), FEdenFlightMovementModel::ValidateStepSettings(Flight->StepSettings));
	TestTrue(
		TEXT("Blueprint propulsion settings are valid"),
		FEdenFlightMovementModel::ValidatePropulsionSettings(Flight->PropulsionSettings, Flight->StepSettings.GetFixedStepSeconds()));

	// The Blueprint must not silently override the C++ flight model. A deliberate per-ship tune must update these expectations.
	TestEqual(TEXT("Step rate not overridden"), Flight->StepSettings.FixedStepHz, DefaultStep.FixedStepHz);
	TestEqual(TEXT("Max steps per update not overridden"), Flight->StepSettings.MaxStepsPerUpdate, DefaultStep.MaxStepsPerUpdate);
	TestEqual(TEXT("Dry mass not overridden"), Flight->PropulsionSettings.DryMassKilograms, DefaultPropulsion.DryMassKilograms);
	TestEqual(TEXT("Forward thrust not overridden"), Flight->PropulsionSettings.ForwardThrustNewtons, DefaultPropulsion.ForwardThrustNewtons);
	TestEqual(TEXT("Reverse thrust not overridden"), Flight->PropulsionSettings.ReverseThrustNewtons, DefaultPropulsion.ReverseThrustNewtons);
	TestEqual(TEXT("Lateral thrust not overridden"), Flight->PropulsionSettings.LateralThrustNewtons, DefaultPropulsion.LateralThrustNewtons);
	TestEqual(TEXT("Vertical thrust not overridden"), Flight->PropulsionSettings.VerticalThrustNewtons, DefaultPropulsion.VerticalThrustNewtons);
	TestEqual(TEXT("Specific impulse not overridden"), Flight->PropulsionSettings.SpecificImpulseSeconds, DefaultPropulsion.SpecificImpulseSeconds);
	TestEqual(
		TEXT("Critical power fraction not overridden"),
		Flight->PropulsionSettings.CriticalPowerThrustFraction,
		DefaultPropulsion.CriticalPowerThrustFraction);
	TestEqual(
		TEXT("Depleted power fraction not overridden"),
		Flight->PropulsionSettings.DepletedPowerThrustFraction,
		DefaultPropulsion.DepletedPowerThrustFraction);

	// Decision 2 (no rebalance yet): the Isp placeholder must reproduce the Data Asset's legacy rate at full forward thrust.
	const UEdenFuelConfigDataAsset* FuelConfigAsset =
		EdenRuntimeCompositionTests::LoadAsset<UEdenFuelConfigDataAsset>(EdenRuntimeCompositionTests::FuelConfigPath);
	TestNotNull(TEXT("DA_EdenFuelConfig loads"), FuelConfigAsset);
	if (FuelConfigAsset)
	{
		const double LegacyRate = FuelConfigAsset->FuelConfig.ConsumptionRateKilogramsPerSecond;
		const double IspRate =
			FEdenFlightMovementModel::CalculatePropellantMassFlowKilogramsPerSecond(Flight->PropulsionSettings.ForwardThrustNewtons, Flight->PropulsionSettings);
		AddInfo(FString::Printf(
			TEXT("DA_EdenFuelConfig: capacity %.3f kg, legacy rate %.6f kg/s; Isp %.3f s gives %.6f kg/s at %.0f N"),
			FuelConfigAsset->FuelConfig.CapacityKilograms,
			LegacyRate,
			Flight->PropulsionSettings.SpecificImpulseSeconds,
			IspRate,
			Flight->PropulsionSettings.ForwardThrustNewtons));
		TestTrue(
			FString::Printf(TEXT("Isp reproduces the legacy full-thrust rate (%.6f vs %.6f kg/s)"), IspRate, LegacyRate),
			FMath::IsNearlyEqual(IspRate, LegacyRate, LegacyRate * 1.0e-5));
	}
	TestEqual(
		TEXT("Assist speed not overridden"),
		Flight->PropulsionSettings.AssistMaxSpeedMetersPerSecond,
		DefaultPropulsion.AssistMaxSpeedMetersPerSecond);
	TestEqual(
		TEXT("Assist response not overridden"),
		Flight->PropulsionSettings.AssistResponseTimeSeconds,
		DefaultPropulsion.AssistResponseTimeSeconds);
	TestTrue(
		TEXT("Blueprint attitude settings are valid"),
		FEdenFlightMovementModel::ValidateAttitudeSettings(Flight->AttitudeSettings, Flight->StepSettings.GetFixedStepSeconds()));
	TestTrue(
		TEXT("Inertia tensor not overridden"),
		Flight->AttitudeSettings.PrincipalMomentsOfInertiaKilogramMetersSquared == DefaultAttitude.PrincipalMomentsOfInertiaKilogramMetersSquared);
	TestEqual(TEXT("Roll torque not overridden"), Flight->AttitudeSettings.RollTorqueNewtonMeters, DefaultAttitude.RollTorqueNewtonMeters);
	TestEqual(TEXT("Pitch torque not overridden"), Flight->AttitudeSettings.PitchTorqueNewtonMeters, DefaultAttitude.PitchTorqueNewtonMeters);
	TestEqual(TEXT("Yaw torque not overridden"), Flight->AttitudeSettings.YawTorqueNewtonMeters, DefaultAttitude.YawTorqueNewtonMeters);
	TestEqual(
		TEXT("Assist angular rate not overridden"),
		Flight->AttitudeSettings.AssistMaxAngularRateDegreesPerSecond,
		DefaultAttitude.AssistMaxAngularRateDegreesPerSecond);
	TestEqual(
		TEXT("Assist angular response not overridden"),
		Flight->AttitudeSettings.AssistAngularResponseTimeSeconds,
		DefaultAttitude.AssistAngularResponseTimeSeconds);
	TestEqual(
		TEXT("Orientation hold response not overridden"),
		Flight->AttitudeSettings.OrientationHoldResponseTimeSeconds,
		DefaultAttitude.OrientationHoldResponseTimeSeconds);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenRuntimeCompositionFlightFixedStepFollowsSimulationPauseTest,
	"Eden.Integration.Flight.FixedStepFollowsSimulationPause",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenRuntimeCompositionFlightFixedStepFollowsSimulationPauseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenRuntimeCompositionTests::FScopedRuntimeWorld RuntimeWorld;
	UWorld* World = RuntimeWorld.Get();
	UEdenSimulationClockSubsystem* Clock = World ? World->GetSubsystem<UEdenSimulationClockSubsystem>() : nullptr;
	TestNotNull(TEXT("Simulation clock exists"), Clock);
	if (!World || !Clock)
	{
		return false;
	}

	AEdenSpacecraftPawn* Pawn = World->SpawnActor<AEdenSpacecraftPawn>(AEdenSpacecraftPawn::StaticClass(), FTransform::Identity);
	TestNotNull(TEXT("Spacecraft spawns"), Pawn);
	if (!Pawn)
	{
		return false;
	}

	World->BeginPlay();
	EdenRuntimeCompositionTests::EnsureBeginPlay(Pawn);
	UEdenFlightMovementComponent* Flight = Pawn->GetFlightMovementComponent();
	TestNotNull(TEXT("Flight component exists"), Flight);
	if (!Flight)
	{
		return false;
	}

	FEdenFlightInputCommand Burn;
	Burn.TranslationInput = FVector(1.0, 0.0, 0.0);
	Burn.bStabilizationEnabled = false;

	Clock->PauseSimulation();
	Pawn->ApplyFlightInputCommand(Burn, 0.1f);
	TestEqual(TEXT("Paused simulation clock runs no flight steps"), Flight->GetFlightDynamicsSnapshot().FlightStepCount, static_cast<int64>(0));
	TestTrue(TEXT("Paused simulation clock does not move the ship"), Pawn->GetActorLocation().Equals(FVector::ZeroVector, 0.0));
	TestEqual(TEXT("Paused simulation clock leaves velocity at rest"), Flight->GetSpeedMetersPerSecond(), 0.0f);

	Clock->ResumeSimulation();
	Pawn->ApplyFlightInputCommand(Burn, 0.1f);
	TestEqual(TEXT("Resumed flight runs twelve 120 Hz steps for 0.1 s"), Flight->GetFlightDynamicsSnapshot().FlightStepCount, static_cast<int64>(12));
	// 0.1 s at 10 m/s^2 from rest: v = 1 m/s, x = 0.05 m = 5 cm. No catch-up of paused time.
	TestTrue(
		FString::Printf(TEXT("Resumed flight speed is 1 m/s (%.9f)"), Flight->GetSpeedMetersPerSecond()),
		FMath::IsNearlyEqual(Flight->GetSpeedMetersPerSecond(), 1.0f, 1.0e-5f));
	TestTrue(
		FString::Printf(TEXT("Resumed flight moved 5 cm along +X (%s)"), *Pawn->GetActorLocation().ToString()),
		Pawn->GetActorLocation().Equals(FVector(5.0, 0.0, 0.0), 1.0e-6));

	const FEdenFlightDynamicsSnapshot Snapshot = Flight->GetFlightDynamicsSnapshot();
	TestTrue(TEXT("Snapshot acceleration is 10 m/s^2 along +X"), Snapshot.AccelerationWorldMetersPerSecondSquared.Equals(FVector(10.0, 0.0, 0.0), 1.0e-9));
	TestTrue(TEXT("Snapshot applied force is 100 kN forward"), Snapshot.AppliedBodyForceNewtons.Equals(FVector(100000.0, 0.0, 0.0), 1.0e-6));
	TestFalse(TEXT("Snapshot reports assist off"), Snapshot.bFlightAssistActive);
	TestEqual(TEXT("No dropped steps"), Snapshot.DroppedFlightStepCount, static_cast<int64>(0));

	// Rotation follows the same pause gate and acts only through torque.
	FEdenFlightInputCommand Yaw;
	Yaw.RotationInput = FVector(0.0, 1.0, 0.0);
	Yaw.bStabilizationEnabled = false;
	const FQuat OrientationBeforePause = Pawn->GetActorQuat();
	Clock->PauseSimulation();
	Pawn->ApplyFlightInputCommand(Yaw, 0.1f);
	TestTrue(TEXT("Paused simulation clock does not rotate the ship"), Pawn->GetActorQuat().Equals(OrientationBeforePause, 0.0));
	TestTrue(TEXT("Paused simulation clock leaves angular velocity at rest"), Flight->GetAngularVelocityBodyRadiansPerSecond().IsZero());

	Clock->ResumeSimulation();
	const FVector VelocityBeforeYaw = Flight->Velocity;
	Pawn->ApplyFlightInputCommand(Yaw, 0.1f);
	const FEdenFlightAttitudeSettings Attitude;
	const double ExpectedYawRate = Attitude.YawTorqueNewtonMeters / Attitude.PrincipalMomentsOfInertiaKilogramMetersSquared.Z * 0.1;
	TestTrue(
		FString::Printf(TEXT("0.1 s of full yaw torque gives w = (tau / I) t (%.9f)"), Flight->GetAngularVelocityBodyRadiansPerSecond().Z),
		FMath::IsNearlyEqual(Flight->GetAngularVelocityBodyRadiansPerSecond().Z, ExpectedYawRate, 1.0e-9));
	TestTrue(TEXT("Yaw input turned the nose right"), Pawn->GetActorRotation().Yaw > 0.0);
	TestTrue(TEXT("Turning without thrust leaves linear velocity unchanged"), Flight->Velocity.Equals(VelocityBeforeYaw, 1.0e-9));

	const FEdenFlightDynamicsSnapshot AttitudeSnapshot = Flight->GetFlightDynamicsSnapshot();
	TestTrue(
		TEXT("Snapshot reports the applied yaw torque"),
		AttitudeSnapshot.AppliedBodyTorqueNewtonMeters.Equals(FVector(0.0, 0.0, Attitude.YawTorqueNewtonMeters), 1.0e-6));
	TestTrue(
		TEXT("Snapshot reports available torque"),
		AttitudeSnapshot.AvailableTorqueNewtonMeters.Equals(
			FVector(Attitude.RollTorqueNewtonMeters, Attitude.PitchTorqueNewtonMeters, Attitude.YawTorqueNewtonMeters),
			1.0e-6));
	TestTrue(TEXT("Snapshot orientation matches the actor"), AttitudeSnapshot.OrientationWorld.Equals(Pawn->GetActorQuat(), 1.0e-12));
	TestTrue(TEXT("Snapshot reports angular momentum"), AttitudeSnapshot.AngularMomentumWorld.Size() > 0.0);

	Flight->ResetFlightMovement();
	TestEqual(TEXT("Reset clears flight step count"), Flight->GetFlightDynamicsSnapshot().FlightStepCount, static_cast<int64>(0));
	TestEqual(TEXT("Reset clears velocity"), Flight->GetSpeedMetersPerSecond(), 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenRuntimeCompositionPureRotationDoesNotLightMainEngineTest,
	"Eden.Integration.Flight.PureRotationDoesNotLightMainEngine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenRuntimeCompositionPureRotationDoesNotLightMainEngineTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenRuntimeCompositionTests::FFlightPropellantFixture Fixture;
	if (!TestTrue(TEXT("Blueprint pawn with fuel and power runs"), Fixture.Setup(*this)))
	{
		return false;
	}

	FEdenFlightInputCommand Pitch;
	Pitch.RotationInput = FVector(1.0, 0.0, 0.0);
	Pitch.bStabilizationEnabled = false;
	Fixture.Advance(Pitch, 5);

	// The aggregate demand counts attitude thrusters, which is what used to drive the flame.
	AddInfo(FString::Printf(TEXT("Pure pitch: aggregate demand %.3f, main-engine level %.3f"),
		Fixture.Flight->GetPropulsionDemandNormalized(), Fixture.Pawn->GetMainEngineVisualLevel()));
	TestTrue(TEXT("Attitude thrusters are firing (aggregate demand > 0)"), Fixture.Flight->GetPropulsionDemandNormalized() > 0.0f);
	TestEqual(TEXT("Pure rotation does not light the main engine"), Fixture.Pawn->GetMainEngineVisualLevel(), 0.0f);

	Fixture.Advance(EdenRuntimeCompositionTests::FFlightPropellantFixture::FullBurn(), 1);
	TestTrue(TEXT("A full forward burn lights the main engine at its delivered level"), Fixture.Pawn->GetMainEngineVisualLevel() > 0.99f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenRuntimeCompositionMouseStickHoldsAndCentresTest,
	"Eden.Integration.Flight.MouseStickHoldsAndCentres",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenRuntimeCompositionMouseStickHoldsAndCentresTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenRuntimeCompositionTests::FFlightPropellantFixture Fixture;
	if (!TestTrue(TEXT("Blueprint pawn with fuel and power runs"), Fixture.Setup(*this)))
	{
		return false;
	}
	AEdenFlightPlayerController* Controller = EdenRuntimeCompositionTests::PossessWithController(Fixture);
	if (!TestNotNull(TEXT("Flight controller possesses the pawn"), Controller))
	{
		return false;
	}

	const FEdenFlightMouseSettings& Mouse = Controller->MouseStickSettings;
	const double Raw = 40.0 * Mouse.Sensitivity;
	const double Expected = (Raw - Mouse.DeadZone) / (1.0 - Mouse.DeadZone);

	Controller->AddRotationInputSample(FVector(40.0, 0.0, 0.0));
	TestTrue(
		FString::Printf(TEXT("40 counts of mouse is a proportional pitch command (%.4f)"), Controller->GetCurrentFlightInputCommand().RotationInput.X),
		FMath::IsNearlyEqual(Controller->GetCurrentFlightInputCommand().RotationInput.X, Expected, 1.0e-5));

	// The action completes when the mouse stops; the deflection must survive it.
	Controller->ReleaseRotationInput();
	TestTrue(TEXT("Stopping the mouse holds the deflection"), FMath::IsNearlyEqual(Controller->GetCurrentFlightInputCommand().RotationInput.X, Expected, 1.0e-5));

	Controller->AddRotationInputSample(FVector(0.0, 0.0, 1.0));
	TestTrue(TEXT("Roll keys are direct"), Controller->GetCurrentFlightInputCommand().RotationInput.Z > 0.5);
	Controller->ReleaseRotationInput();
	TestTrue(TEXT("Releasing roll centres roll immediately"), Controller->GetCurrentFlightInputCommand().RotationInput.Z == 0.0);

	// Long enough for any default centring rate to bring a 40-count deflection inside the dead zone.
	const double SecondsToCentre = Mouse.AutoCenterRatePerSecond > 0.0f
		? FMath::Loge(FMath::Max(1.0, Raw / FMath::Max(0.001, static_cast<double>(Mouse.DeadZone) * 0.5))) / Mouse.AutoCenterRatePerSecond
		: 0.0;
	for (int32 Frame = 0; Frame < FMath::CeilToInt(SecondsToCentre / 0.1) + 1; ++Frame)
	{
		Controller->AdvanceMouseStick(0.1f);
	}
	TestTrue(TEXT("The stick auto-centres into the dead zone"), Controller->GetCurrentFlightInputCommand().RotationInput.IsZero());

	Controller->AddRotationInputSample(FVector(40.0, -30.0, 0.0));
	Controller->EdenCenterMouseStick();
	TestTrue(TEXT("EdenCenterMouseStick zeroes pitch and yaw"), Controller->GetCurrentFlightInputCommand().RotationInput.IsZero());
	TestTrue(TEXT("Stick state is zero"), Controller->GetMouseStickDeflection().IsZero());

	FEdenOperatorHudSnapshot Hud;
	Hud.bHasFlight = true;
	Hud.PilotRotationCommand = FVector(0.45, -0.2, 0.0);
	TestTrue(TEXT("HUD shows the shaped stick command"), UEdenOperatorHudWidget::FormatFlightPanel(Hud).Contains(TEXT("STICK     pitch +0.45  yaw -0.20  roll +0.00")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenRuntimeCompositionChaseCameraFramesHullTest,
	"Eden.Integration.Flight.ChaseCameraFramesHull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenRuntimeCompositionChaseCameraFramesHullTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenRuntimeCompositionTests::FFlightPropellantFixture Fixture;
	if (!TestTrue(TEXT("Blueprint pawn with fuel and power runs"), Fixture.Setup(*this)))
	{
		return false;
	}

	USpringArmComponent* Arm = Fixture.Pawn->GetChaseCameraArm();
	UCameraComponent* Camera = Fixture.Pawn->FindComponentByClass<UCameraComponent>();
	if (!TestNotNull(TEXT("Chase camera arm exists"), Arm) || !TestNotNull(TEXT("Blueprint camera exists"), Camera))
	{
		return false;
	}

	const double Radius = Fixture.Pawn->GetPresentationHullRadiusCentimeters();
	const FEdenFlightCameraSettings& Settings = Fixture.Pawn->CameraSettings;
	AddInfo(FString::Printf(TEXT("Hull radius %.0f cm, arm %.0f cm, FOV %.0f deg"), Radius, Arm->TargetArmLength, Camera->FieldOfView));
	TestTrue(TEXT("Presentation hull radius is known"), Radius > 100.0);
	TestTrue(TEXT("Camera rides the chase arm"), Camera->GetAttachParent() == Arm);
	TestTrue(
		TEXT("Arm length is the framing fit for the hull"),
		FMath::IsNearlyEqual(static_cast<double>(Arm->TargetArmLength), FEdenFlightPresentationModel::ComputeChaseArmLengthCentimeters(Radius, Settings), 1.0));
	TestTrue(
		TEXT("Whole hull sphere fits the frame at that length"),
		FEdenFlightPresentationModel::ComputeHullAngularExtentRadians(Radius, Arm->TargetArmLength, Settings)
			<= FEdenFlightPresentationModel::ComputeLimitingHalfFieldOfViewRadians(Settings) + 1.0e-6);
	TestFalse(TEXT("Boom does not collide (it would pop in front of the hull)"), Arm->bDoCollisionTest);
	TestTrue(TEXT("Location lag is capped"), !Arm->bEnableCameraLag || Arm->CameraLagMaxDistance > 0.0f);
	TestTrue(TEXT("Flight still moves the collision root, not a presentation component"), Fixture.Flight->UpdatedComponent.Get() == static_cast<USceneComponent*>(Fixture.Pawn->GetRequiredCollisionRoot()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenHudScaleAppliesToBuiltPanelsTest,
	"Eden.Integration.Hud.ScaleAppliesToBuiltPanels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenHudScaleAppliesToBuiltPanelsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestEqual(TEXT("Scale 1 keeps the original 10 pt body"), UEdenOperatorHudWidget::ScaledFontSize(10, 1.0f), 10);
	TestEqual(TEXT("Scale rounds to the nearest point"), UEdenOperatorHudWidget::ScaledFontSize(10, 1.35f), 14);
	TestEqual(TEXT("Scale clamps at 2.5"), UEdenOperatorHudWidget::ScaledFontSize(10, 9.0f), 25);
	TestEqual(TEXT("Non-finite scale falls back to 1"), UEdenOperatorHudWidget::ScaledFontSize(10, std::numeric_limits<float>::quiet_NaN()), 10);

	EdenRuntimeCompositionTests::FScopedRuntimeWorld RuntimeWorld;
	UWorld* World = RuntimeWorld.Get();
	UClass* HudClass = StaticLoadClass(UEdenOperatorHudWidget::StaticClass(), nullptr, EdenRuntimeCompositionTests::HudWidgetClassPath);
	if (!TestNotNull(TEXT("WBP_EdenOperatorHud loads"), HudClass) || !World)
	{
		return false;
	}
	UEdenOperatorHudWidget* Hud = CreateWidget<UEdenOperatorHudWidget>(World, HudClass);
	if (!TestNotNull(TEXT("HUD widget is created"), Hud))
	{
		return false;
	}
	const TSharedRef<SWidget> SlateHolder = Hud->TakeWidget();

	TestEqual(TEXT("The default scale is applied in the first build"), Hud->GetAppliedBodyFontSize(), UEdenOperatorHudWidget::ScaledFontSize(10, Hud->GetHudScale()));
	Hud->SetHudScale(2.0f);
	TestEqual(TEXT("A runtime scale change re-applies fonts"), Hud->GetAppliedBodyFontSize(), 20);
	TestEqual(TEXT("All five panels are still built"), Hud->GetBuiltPanelCount(), 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenRuntimeCompositionOperatorCoolingReachesPowerBusTest,
	"Eden.Integration.SolarCrisisV2.B.OperatorCoolingReachesPowerBus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenRuntimeCompositionOperatorCoolingReachesPowerBusTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	EdenRuntimeCompositionTests::FFlightPropellantFixture Fixture;
	if (!TestTrue(TEXT("Blueprint pawn with fuel and power runs"), Fixture.Setup(*this)))
	{
		return false;
	}
	UEdenOperatorControlComponent* Operator = Fixture.Pawn->GetOperatorControlComponent();
	UEdenThermalSystemComponent* Thermal = Fixture.Pawn->GetThermalSystemComponent();
	if (!TestNotNull(TEXT("Operator"), Operator) || !TestNotNull(TEXT("Thermal"), Thermal))
	{
		return false;
	}

	// Default spacecraft: unlimited bus, so every existing scenario keeps full cooling.
	TestTrue(TEXT("Boost accepted"), Operator->SetThermalControlMode(EEdenThermalControlMode::Boost));
	Fixture.Clock->Tick(0.1f);
	const FEdenPowerStateSnapshot Unlimited = Fixture.Power->GetPowerStateSnapshot();
	TestFalse(TEXT("Default bus is unlimited"), Unlimited.bBusLimited);
	TestTrue(TEXT("Boost cooling power reaches the bus as thermal control"), FMath::IsNearlyEqual(Unlimited.ThermalControlDemandKilowatts, 1.5f));
	TestTrue(TEXT("Boost active cooling reaches thermal"), FMath::IsNearlyEqual(Thermal->GetThermalStateSnapshot().ActiveCoolingDegreesCelsiusPerSecond, 1.0f));
	TestTrue(TEXT("Unlimited bus: full cooling authority"), Thermal->GetThermalStateSnapshot().CoolingAuthority == 1.0f);

	// A mission-declared 6 kW limit and an empty battery: generation 2 kW - essential 1 kW leaves 1 of 1.5 kW.
	TestTrue(TEXT("Discharge limit accepted"), Fixture.Power->SetBatteryDischargeLimitKilowatts(6.0f));
	TestTrue(TEXT("Battery emptied"), Fixture.Power->SetBatteryChargeKilowattHours(0.0f));
	Fixture.Clock->Tick(0.1f);
	TestTrue(
		FString::Printf(TEXT("Limited bus, empty battery: Boost authority 67%% (%.3f)"), Thermal->GetThermalStateSnapshot().CoolingAuthority),
		FMath::IsNearlyEqual(Thermal->GetThermalStateSnapshot().CoolingAuthority, 1.0f / 1.5f, 1.0e-3f));

	TestTrue(TEXT("Override cleared"), Fixture.Power->ClearBatteryDischargeLimitOverride());
	Fixture.Clock->Tick(0.1f);
	TestTrue(TEXT("Back to the unlimited default: full authority"), Thermal->GetThermalStateSnapshot().CoolingAuthority == 1.0f);
	return true;
}

namespace EdenRuntimeCompositionTests
{
/** A minimal scripted mission: optional declared initial conditions, an optional 4.25 C/s flare T 10-30 s. */
FEdenMissionDefinitionConfig MakeInitialConditionsMission(FName MissionId, bool bDeclareInitialConditions, bool bWithFlare)
{
	FEdenMissionDefinitionConfig Definition;
	Definition.MissionId = MissionId;
	FEdenMissionObjectiveConfig Survive;
	Survive.ObjectiveId = TEXT("SurviveLong");
	Survive.ObjectiveType = EEdenObjectiveType::SurviveUntilTime;
	Survive.TargetValue = 1000.0f;
	Survive.bActivateOnStart = true;
	Definition.Objectives.Add(Survive);
	if (bWithFlare)
	{
		FEdenMissionEventConfig Flare;
		Flare.EventId = TEXT("Flare");
		Flare.TriggerTimeSeconds = 10.0f;
		Flare.CommandType = EEdenMissionCommandType::SetExternalHeatingRate;
		Flare.FloatParameter = 4.25f;
		FEdenMissionEventConfig Clear;
		Clear.EventId = TEXT("ClearFlare");
		Clear.TriggerTimeSeconds = 30.0f;
		Clear.CommandType = EEdenMissionCommandType::ClearExternalHeatingRate;
		Definition.Events.Add(Flare);
		Definition.Events.Add(Clear);
	}
	if (bDeclareInitialConditions)
	{
		Definition.InitialConditions.bSetInitialTemperature = true;
		Definition.InitialConditions.InitialTemperatureCelsius = 20.0f;
		Definition.InitialConditions.bSetBatteryDischargeLimit = true;
		Definition.InitialConditions.BatteryMaxDischargeKilowatts = 6.0f;
	}
	return Definition;
}

void TickSeconds(FFlightPropellantFixture& Fixture, double Seconds)
{
	for (int32 Tick = 0; Tick < FMath::RoundToInt(Seconds / 0.1); ++Tick)
	{
		Fixture.Clock->Tick(0.1f);
	}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenRuntimeCompositionInitialConditionsAreScopedTest,
	"Eden.Integration.SolarCrisisV2.C.InitialConditionsAreScopedAndExplicit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenRuntimeCompositionInitialConditionsAreScopedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenRuntimeCompositionTests;

	FFlightPropellantFixture Fixture;
	if (!TestTrue(TEXT("Blueprint pawn with fuel and power runs"), Fixture.Setup(*this)))
	{
		return false;
	}
	UEdenOperatorControlComponent* Operator = Fixture.Pawn->GetOperatorControlComponent();
	UEdenThermalSystemComponent* Thermal = Fixture.Pawn->GetThermalSystemComponent();
	UEdenMissionSubsystem* Mission = Fixture.Pawn->GetWorld()->GetSubsystem<UEdenMissionSubsystem>();
	if (!Operator || !Thermal || !Mission)
	{
		return false;
	}

	// The ship sat with cooling Off for a minute before the player started the scenario.
	Operator->SetThermalControlMode(EEdenThermalControlMode::Off);
	TickSeconds(Fixture, 60.0);
	TestTrue(TEXT("Idle Off for 60 s heated the ship (about 50 C)"), Thermal->GetThermalStateSnapshot().TemperatureCelsius > 45.0f);
	Fixture.Power->SetBatteryChargeKilowattHours(15.0f);
	const float BatteryBefore = Fixture.Power->GetPowerStateSnapshot().BatteryChargeKilowattHours;
	const float FuelBefore = Fixture.Fuel->GetFuelStateSnapshot().FuelQuantityKilograms;

	Mission->SetMissionResourceTargets(Thermal, Fixture.Power, Fixture.Fuel);
	TestTrue(TEXT("Declaring mission loads"), Mission->LoadMission(MakeInitialConditionsMission(TEXT("V2InitDeclared"), true, false)));
	TestTrue(TEXT("Declaring mission starts"), Mission->StartMission());
	TestEqual(TEXT("Declared temperature applied exactly"), Thermal->GetThermalStateSnapshot().TemperatureCelsius, 20.0f);
	TestEqual(TEXT("Declared bus limit applied"), Fixture.Power->GetEffectiveBatteryDischargeLimitKilowatts(), 6.0f);
	TestEqual(TEXT("Battery charge NOT reset"), Fixture.Power->GetPowerStateSnapshot().BatteryChargeKilowattHours, BatteryBefore);
	TestEqual(TEXT("Propellant NOT reset"), Fixture.Fuel->GetFuelStateSnapshot().FuelQuantityKilograms, FuelBefore);
	TestTrue(TEXT("Operator mode NOT reset (still Off)"), Operator->GetOperatorStateSnapshot().ThermalMode == EEdenThermalControlMode::Off);
	TestTrue(TEXT("Runtime records the application"), Mission->GetMissionRuntimeState().bInitialConditionsApplied);

	TestTrue(TEXT("Abort"), Mission->AbortMission());
	TestEqual(TEXT("Abort releases the mission's bus limit"), Fixture.Power->GetEffectiveBatteryDischargeLimitKilowatts(), 0.0f);
	const float AfterAbort = Thermal->GetThermalStateSnapshot().TemperatureCelsius;

	// A mission that declares nothing starts from the ship's actual condition (orbital-mission mode).
	TickSeconds(Fixture, 10.0); // still Off: heats
	const float Current = Thermal->GetThermalStateSnapshot().TemperatureCelsius;
	TestTrue(TEXT("Ship kept heating after abort (temperature not reset by abort)"), Current > AfterAbort);
	TestTrue(TEXT("Reset before the next mission"), Mission->ResetMission());
	TestTrue(TEXT("Undeclared mission loads"), Mission->LoadMission(MakeInitialConditionsMission(TEXT("V2InitUndeclared"), false, false)));
	TestTrue(TEXT("Undeclared mission starts"), Mission->StartMission());
	TestEqual(TEXT("Undeclared mission keeps the current temperature"), Thermal->GetThermalStateSnapshot().TemperatureCelsius, Current);
	TestFalse(TEXT("Nothing applied"), Mission->GetMissionRuntimeState().bInitialConditionsApplied);
	TestEqual(TEXT("Undeclared mission leaves the bus unlimited"), Fixture.Power->GetEffectiveBatteryDischargeLimitKilowatts(), 0.0f);
	Mission->AbortMission();
	Mission->ResetMission();

	// Invalid declarations are rejected at load; impossible ones refuse to start.
	FEdenMissionDefinitionConfig Invalid = MakeInitialConditionsMission(TEXT("V2InitInvalid"), true, false);
	Invalid.InitialConditions.InitialTemperatureCelsius = std::numeric_limits<float>::quiet_NaN();
	AddExpectedError(TEXT("InitialTemperatureCelsius must be finite"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Non-finite initial temperature is rejected"), Mission->LoadMission(Invalid));

	Mission->SetMissionResourceTargets(nullptr, Fixture.Power, Fixture.Fuel);
	TestTrue(TEXT("Declaring mission loads without a thermal target"), Mission->LoadMission(MakeInitialConditionsMission(TEXT("V2InitNoThermal"), true, false)));
	AddExpectedError(TEXT("declares initial conditions but the thermal target"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Start fails visibly when a declared target is missing"), Mission->StartMission());
	TestEqual(TEXT("Mission stays Ready"), Mission->GetMissionState(), EEdenMissionState::Ready);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenRuntimeCompositionEarlyAndLateActivationMatchTest,
	"Eden.Integration.SolarCrisisV2.C.EarlyAndLateActivationMatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenRuntimeCompositionEarlyAndLateActivationMatchTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenRuntimeCompositionTests;

	TArray<float> ReferenceTrace;
	for (const double IdleSeconds : {0.0, 60.0, 300.0})
	{
		FFlightPropellantFixture Fixture;
		if (!TestTrue(TEXT("Blueprint pawn with fuel and power runs"), Fixture.Setup(*this)))
		{
			return false;
		}
		UEdenOperatorControlComponent* Operator = Fixture.Pawn->GetOperatorControlComponent();
		UEdenThermalSystemComponent* Thermal = Fixture.Pawn->GetThermalSystemComponent();
		UEdenMissionSubsystem* Mission = Fixture.Pawn->GetWorld()->GetSubsystem<UEdenMissionSubsystem>();
		if (!Operator || !Thermal || !Mission)
		{
			return false;
		}

		// The player lingers with cooling Off (the worst case), then returns to Nominal and starts.
		Operator->SetThermalControlMode(EEdenThermalControlMode::Off);
		TickSeconds(Fixture, IdleSeconds);
		Operator->SetThermalControlMode(EEdenThermalControlMode::Nominal);
		const float BeforeStart = Thermal->GetThermalStateSnapshot().TemperatureCelsius;

		Mission->SetMissionResourceTargets(Thermal, Fixture.Power, Fixture.Fuel);
		TestTrue(TEXT("Mission loads"), Mission->LoadMission(MakeInitialConditionsMission(TEXT("V2Activation"), true, true)));
		TestTrue(TEXT("Mission starts"), Mission->StartMission());

		TArray<float> Trace;
		for (int32 Tick = 0; Tick < 400; ++Tick)
		{
			Fixture.Clock->Tick(0.1f);
			Trace.Add(Thermal->GetThermalStateSnapshot().TemperatureCelsius);
		}
		AddInfo(FString::Printf(TEXT("Idle %.0f s: %.2f C before start; T=30 s %.3f C; T=40 s %.3f C"), IdleSeconds, BeforeStart, Trace[299], Trace[399]));
		if (ReferenceTrace.IsEmpty())
		{
			ReferenceTrace = Trace;
			// Passive (Nominal) through the 4.25 C/s flare: 20 + 4.25 x 20 = 105 C at the end of the flare.
			TestTrue(FString::Printf(TEXT("Reference reaches ~105 C at the end of the flare (%.3f)"), Trace[299]), FMath::IsNearlyEqual(Trace[299], 105.0f, 0.5f));
			continue;
		}
		bool bIdentical = true;
		for (int32 Index = 0; Index < Trace.Num(); ++Index)
		{
			if (!FMath::IsNearlyEqual(Trace[Index], ReferenceTrace[Index], 1.0e-4f))
			{
				AddError(FString::Printf(TEXT("Idle %.0f s diverges at T=%.1f s: %.4f vs %.4f C"), IdleSeconds, (Index + 1) * 0.1, Trace[Index], ReferenceTrace[Index]));
				bIdentical = false;
				break;
			}
		}
		TestTrue(FString::Printf(TEXT("Starting after %.0f s of idle gives the identical temperature trace"), IdleSeconds), bIdentical);
	}
	return true;
}

namespace EdenRuntimeCompositionTests
{
const TCHAR* SolarCrisisV2Path = TEXT("/Game/Eden/Data/Missions/DA_SolarCrisisV2.DA_SolarCrisisV2");

struct FV2Action
{
	double AtSeconds;
	EEdenThermalControlMode Mode;
	bool bShed;
};

struct FV2Expectation
{
	const TCHAR* Name;
	TArray<FV2Action> Actions;
	EEdenMissionState Outcome;
	double AtSeconds;          // simulated terminal time (ExecPlan 0011 revision 2, section D)
	double PeakCelsius;        // < 0: not compared (failures end mid-rise)
	double AuxCurtailedKw;     // < 0: not compared
	const TCHAR* ReasonContains;
};

struct FV2Run
{
	EEdenMissionState Outcome = EEdenMissionState::Running;
	double AtSeconds = -1.0;
	double PeakCelsius = 0.0;
	double MaxAuxCurtailedKw = 0.0;
	double MinCoolingAuthority = 1.0;
	FString Reason;
};

/** Runs DA_SolarCrisisV2 on the Blueprint pawn with operator actions at mission times; 10 Hz steps. */
FV2Run RunSolarCrisisV2(FAutomationTestBase& Test, const TArray<FV2Action>& Actions, TFunction<void(FFlightPropellantFixture&, double)> OnTick = nullptr)
{
	FV2Run Run;
	FFlightPropellantFixture Fixture;
	if (!Fixture.Setup(Test))
	{
		return Run;
	}
	PossessWithController(Fixture);
	UEdenOperatorControlComponent* Operator = Fixture.Pawn->GetOperatorControlComponent();
	UEdenThermalSystemComponent* Thermal = Fixture.Pawn->GetThermalSystemComponent();
	UEdenMissionSubsystem* Mission = Fixture.Pawn->GetWorld()->GetSubsystem<UEdenMissionSubsystem>();
	UEdenMissionDefinitionDataAsset* V2 = Cast<UEdenMissionDefinitionDataAsset>(FSoftObjectPath(SolarCrisisV2Path).TryLoad());
	if (!Test.TestNotNull(TEXT("DA_SolarCrisisV2 loads"), V2) || !Operator || !Thermal || !Mission)
	{
		return Run;
	}
	Mission->SetMissionResourceTargets(Thermal, Fixture.Power, Fixture.Fuel);
	if (!Test.TestTrue(TEXT("v2 loads"), Mission->LoadMissionFromDefinitionAsset(V2)) || !Test.TestTrue(TEXT("v2 starts"), Mission->StartMission()))
	{
		return Run;
	}

	TArray<bool> Applied;
	Applied.Init(false, Actions.Num());
	Run.PeakCelsius = Thermal->GetThermalStateSnapshot().TemperatureCelsius;
	for (int32 Tick = 0; Tick < 1500 && Mission->IsMissionRunning(); ++Tick)
	{
		const double Now = Mission->GetMissionElapsedTimeSeconds();
		for (int32 Index = 0; Index < Actions.Num(); ++Index)
		{
			if (!Applied[Index] && Actions[Index].AtSeconds <= Now + 1.0e-4)
			{
				Operator->SetThermalControlMode(Actions[Index].Mode);
				Operator->SetLoadShedMode(Actions[Index].bShed ? EEdenLoadShedMode::Shed : EEdenLoadShedMode::Normal);
				Applied[Index] = true;
			}
		}
		Fixture.Clock->Tick(0.1f);
		const FEdenPowerStateSnapshot Power = Fixture.Power->GetPowerStateSnapshot();
		Run.PeakCelsius = FMath::Max(Run.PeakCelsius, static_cast<double>(Thermal->GetThermalStateSnapshot().TemperatureCelsius));
		Run.MaxAuxCurtailedKw = FMath::Max(Run.MaxAuxCurtailedKw, static_cast<double>(Power.AuxiliaryCurtailedKilowatts));
		Run.MinCoolingAuthority = FMath::Min(Run.MinCoolingAuthority, static_cast<double>(Thermal->GetThermalStateSnapshot().CoolingAuthority));
		if (OnTick)
		{
			OnTick(Fixture, Mission->GetMissionElapsedTimeSeconds());
		}
	}
	Run.Outcome = Mission->GetMissionState();
	Run.AtSeconds = Mission->GetMissionElapsedTimeSeconds();
	Run.Reason = Mission->GetMissionRuntimeState().TerminalReason;
	return Run;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSolarCrisisV2AssetMatchesSourceTest,
	"Eden.Integration.SolarCrisisV2.E.AssetMatchesJsonSourceAndV1StaysDefault",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSolarCrisisV2AssetMatchesSourceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenRuntimeCompositionTests;

	FString Json;
	const FString JsonPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Data/Missions/SolarCrisisV2.json"));
	if (!TestTrue(TEXT("Mission source JSON is readable"), FFileHelper::LoadFileToString(Json, *JsonPath)))
	{
		return false;
	}
	FEdenMissionDefinitionConfig Source;
	TestTrue(TEXT("JSON parses into the definition"), FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Source, 0, 0));
	const UEdenMissionDefinitionDataAsset* Asset = Cast<UEdenMissionDefinitionDataAsset>(FSoftObjectPath(SolarCrisisV2Path).TryLoad());
	if (!TestNotNull(TEXT("DA_SolarCrisisV2 exists"), Asset))
	{
		return false;
	}
	const FEdenMissionDefinitionConfig& D = Asset->GetMissionDefinition();
	TestTrue(TEXT("Asset definition is valid"), FEdenMissionModel::ValidateDefinition(D));
	TestEqual(TEXT("MissionId"), D.MissionId, Source.MissionId);
	TestTrue(TEXT("Initial conditions match the source"),
		D.InitialConditions.bSetInitialTemperature == Source.InitialConditions.bSetInitialTemperature
		&& D.InitialConditions.InitialTemperatureCelsius == Source.InitialConditions.InitialTemperatureCelsius
		&& D.InitialConditions.bSetBatteryDischargeLimit == Source.InitialConditions.bSetBatteryDischargeLimit
		&& D.InitialConditions.BatteryMaxDischargeKilowatts == Source.InitialConditions.BatteryMaxDischargeKilowatts);
	if (TestEqual(TEXT("Event count"), D.Events.Num(), Source.Events.Num()))
	{
		for (int32 I = 0; I < D.Events.Num(); ++I)
		{
			const FEdenMissionEventConfig& A = D.Events[I];
			const FEdenMissionEventConfig& B = Source.Events[I];
			TestTrue(FString::Printf(TEXT("Event %s matches the source"), *B.EventId.ToString()),
				A.EventId == B.EventId && A.TriggerTimeSeconds == B.TriggerTimeSeconds && A.CommandType == B.CommandType
				&& A.FloatParameter == B.FloatParameter && A.PhaseParameter == B.PhaseParameter && A.NameParameter == B.NameParameter);
		}
	}
	if (TestEqual(TEXT("Objective count"), D.Objectives.Num(), Source.Objectives.Num()))
	{
		for (int32 I = 0; I < D.Objectives.Num(); ++I)
		{
			const FEdenMissionObjectiveConfig& A = D.Objectives[I];
			const FEdenMissionObjectiveConfig& B = Source.Objectives[I];
			TestTrue(FString::Printf(TEXT("Objective %s matches the source"), *B.ObjectiveId.ToString()),
				A.ObjectiveId == B.ObjectiveId && A.ObjectiveType == B.ObjectiveType && A.TargetValue == B.TargetValue
				&& A.HoldSeconds == B.HoldSeconds && A.DeadlineSeconds == B.DeadlineSeconds
				&& A.bRequired == B.bRequired && A.bActivateOnStart == B.bActivateOnStart);
		}
	}
	// Approved v2 values, pinned so a JSON edit cannot silently change the calibrated scenario.
	const FEdenMissionEventConfig* Flare = D.Events.FindByPredicate([](const FEdenMissionEventConfig& E) { return E.EventId == TEXT("SolarFlareHeating"); });
	TestTrue(TEXT("Flare is 4.25 C/s at T=10 s"), Flare && Flare->FloatParameter == 4.25f && Flare->TriggerTimeSeconds == 10.0f);
	const FEdenMissionObjectiveConfig* Recover = D.Objectives.FindByPredicate([](const FEdenMissionObjectiveConfig& O) { return O.ObjectiveType == EEdenObjectiveType::RecoverThermalStability; });
	TestTrue(TEXT("Recovery: < 70 C, 5 s hold, deadline T=100 s"), Recover && Recover->TargetValue == 70.0f && Recover->HoldSeconds == 5.0f && Recover->DeadlineSeconds == 100.0f);

	// Solar Crisis v1 remains the default until v2 passes human PIE.
	UClass* ControllerClass = StaticLoadClass(AEdenFlightPlayerController::StaticClass(), nullptr, TEXT("/Game/Eden/Blueprints/BP_EdenFlightPlayerController.BP_EdenFlightPlayerController_C"));
	const AEdenFlightPlayerController* Defaults = ControllerClass ? ControllerClass->GetDefaultObject<AEdenFlightPlayerController>() : GetDefault<AEdenFlightPlayerController>();
	TestEqual(TEXT("Default mission is still v1 (DA_SolarEventEmergency)"),
		Defaults->DefaultMissionDefinitionAsset.ToSoftObjectPath().GetAssetName(), FString(TEXT("DA_SolarEventEmergency")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSolarCrisisV2StrategyMatrixTest,
	"Eden.Integration.SolarCrisisV2.E.StrategyMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSolarCrisisV2StrategyMatrixTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenRuntimeCompositionTests;
	using M = EEdenThermalControlMode;
	const EEdenMissionState S = EEdenMissionState::Succeeded;
	const EEdenMissionState F = EEdenMissionState::Failed;

	const FV2Expectation Matrix[] = {
		{TEXT("Passive (Nominal)"), {}, F, 28.9, -1.0, -1.0, TEXT("PreventOverheating")},
		{TEXT("Immediate Boost"), {{10.0, M::Boost, false}}, S, 84.9, 95.0, 0.0, TEXT("all required objectives met")},
		{TEXT("Immediate Emergency"), {{10.0, M::Emergency, false}}, S, 38.3, 75.0, 2.5, TEXT("all required objectives met")},
		{TEXT("Boost at T 15"), {{15.0, M::Boost, false}}, S, 89.9, 97.5, -1.0, TEXT("all required objectives met")},
		{TEXT("Boost at T 18"), {{18.0, M::Boost, false}}, S, 92.9, 99.0, -1.0, TEXT("all required objectives met")},
		{TEXT("Boost at T 21"), {{21.0, M::Boost, false}}, F, 29.9, -1.0, -1.0, TEXT("PreventOverheating")},
		{TEXT("Load shed only"), {{10.0, M::Nominal, true}}, F, 27.3, -1.0, -1.0, TEXT("PreventOverheating")},
		{TEXT("Boost, then Nominal in recovery"), {{10.0, M::Boost, false}, {30.0, M::Nominal, false}}, F, 100.0, 95.0, -1.0, TEXT("recovery not achieved by T=100 s")},
		{TEXT("Boost, then Off in recovery"), {{10.0, M::Boost, false}, {30.0, M::Off, false}}, F, 40.1, -1.0, -1.0, TEXT("PreventOverheating")},
		{TEXT("Boost, then Emergency in recovery"), {{10.0, M::Boost, false}, {30.0, M::Emergency, false}}, S, 51.6, 95.0, -1.0, TEXT("all required objectives met")},
		{TEXT("Emergency + load shed"), {{10.0, M::Emergency, true}}, S, 46.8, 83.0, 0.5, TEXT("all required objectives met")},
	};

	for (const FV2Expectation& Expect : Matrix)
	{
		const FV2Run Run = RunSolarCrisisV2(*this, Expect.Actions);
		UE_LOG(LogEdenMission, Display,
			TEXT("V2 MATRIX | %-36s | %s at T=%.1f s (expected %s at %.1f) | peak %.2f C | aux curtailed max %.2f kW | min cooling authority %.0f%% | %s"),
			Expect.Name, *UEnum::GetValueAsString(Run.Outcome), Run.AtSeconds, *UEnum::GetValueAsString(Expect.Outcome), Expect.AtSeconds,
			Run.PeakCelsius, Run.MaxAuxCurtailedKw, Run.MinCoolingAuthority * 100.0, *Run.Reason);
		TestEqual(FString::Printf(TEXT("%s: outcome"), Expect.Name), Run.Outcome, Expect.Outcome);
		TestTrue(FString::Printf(TEXT("%s: resolves at T=%.1f s (expected %.1f +/- 0.15)"), Expect.Name, Run.AtSeconds, Expect.AtSeconds),
			FMath::Abs(Run.AtSeconds - Expect.AtSeconds) <= 0.15);
		if (Expect.PeakCelsius >= 0.0)
		{
			TestTrue(FString::Printf(TEXT("%s: peak %.2f C (expected %.2f +/- 0.05)"), Expect.Name, Run.PeakCelsius, Expect.PeakCelsius),
				FMath::Abs(Run.PeakCelsius - Expect.PeakCelsius) <= 0.05);
		}
		if (Expect.AuxCurtailedKw >= 0.0)
		{
			TestTrue(FString::Printf(TEXT("%s: auxiliary curtailed %.2f kW (expected %.2f)"), Expect.Name, Run.MaxAuxCurtailedKw, Expect.AuxCurtailedKw),
				FMath::Abs(Run.MaxAuxCurtailedKw - Expect.AuxCurtailedKw) <= 1.0e-3);
		}
		TestTrue(FString::Printf(TEXT("%s: reason '%s' mentions '%s'"), Expect.Name, *Run.Reason, Expect.ReasonContains), Run.Reason.Contains(Expect.ReasonContains));
		TestTrue(FString::Printf(TEXT("%s: with a charged battery cooling is never power-limited"), Expect.Name), Run.MinCoolingAuthority == 1.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenSolarCrisisV2SurfacesAgreeTest,
	"Eden.Integration.SolarCrisisV2.E.HudReviewAndPowerBusAgree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenSolarCrisisV2SurfacesAgreeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenRuntimeCompositionTests;

	bool bCheckedFlare = false;
	bool bCheckedRecovery = false;
	FEdenAfterActionResult Review;
	FString ReviewText;
	FEdenMissionStateSnapshot FinalMission;
	const FV2Run Run = RunSolarCrisisV2(*this, {{10.0, EEdenThermalControlMode::Emergency, false}},
		[&](FFlightPropellantFixture& Fixture, double Now)
		{
			AEdenFlightPlayerController* Controller = Cast<AEdenFlightPlayerController>(Fixture.Pawn->GetController());
			if (!Controller)
			{
				return;
			}
			if (!bCheckedFlare && Now >= 20.0)
			{
				bCheckedFlare = true;
				Controller->RefreshOperatorHudNow();
				const FEdenOperatorHudSnapshot Hud = Controller->GetOperatorHudSnapshot();
				const FEdenPowerStateSnapshot Power = Fixture.Power->GetPowerStateSnapshot();
				TestTrue(TEXT("Flare: HUD power bus equals the authoritative power snapshot"),
					Hud.Power.bBusLimited && Hud.Power.AuxiliaryCurtailedKilowatts == Power.AuxiliaryCurtailedKilowatts
					&& Hud.Power.AuxiliaryServedKilowatts == Power.AuxiliaryServedKilowatts && Hud.Power.CoolingAuthority == Power.CoolingAuthority);
				const FString Systems = UEdenOperatorHudWidget::FormatSystemsPanel(Hud);
				AddInfo(Systems);
				TestTrue(TEXT("Flare: SYSTEMS shows curtailed auxiliary load (never as fully powered)"), Systems.Contains(TEXT("CURTAILED 2.5 kW")));
				TestTrue(TEXT("Flare: SYSTEMS shows full cooling power"), Systems.Contains(TEXT("cooling 100%")));
			}
			if (!bCheckedRecovery && Now >= 33.0)
			{
				bCheckedRecovery = true;
				Controller->RefreshOperatorHudNow();
				const FString MissionText = UEdenOperatorHudWidget::FormatMissionPanel(Controller->GetOperatorHudSnapshot());
				AddInfo(MissionText);
				TestTrue(TEXT("Recovery: MISSION shows hold progress and deadline"), MissionText.Contains(TEXT("stable")) && MissionText.Contains(TEXT("deadline T=100 s")));
			}
			UWorld* World = Fixture.Pawn->GetWorld();
			UEdenMissionSubsystem* Mission = World->GetSubsystem<UEdenMissionSubsystem>();
			if (Mission && !Mission->IsMissionRunning() && FinalMission.MissionState == EEdenMissionState::Inactive)
			{
				FinalMission = Mission->GetMissionStateSnapshot();
				if (UEdenTelemetrySubsystem* Telemetry = World->GetSubsystem<UEdenTelemetrySubsystem>())
				{
					Review = Telemetry->BuildAfterActionResult();
					ReviewText = UEdenAfterActionReviewWidget::FormatResult(Review).ToString();
				}
			}
		});

	TestEqual(TEXT("Emergency strategy succeeds"), Run.Outcome, EEdenMissionState::Succeeded);
	TestTrue(TEXT("Flare and recovery checks ran"), bCheckedFlare && bCheckedRecovery);
	AddInfo(ReviewText);
	TestEqual(TEXT("Review outcome = mission outcome"), Review.FinalMissionState, FinalMission.MissionState);
	TestEqual(TEXT("Review reason = mission reason"), Review.TerminalReason, FinalMission.TerminalReason);
	if (TestEqual(TEXT("Review lists every objective"), Review.Objectives.Num(), FinalMission.ObjectiveSnapshots.Num()))
	{
		for (int32 I = 0; I < Review.Objectives.Num(); ++I)
		{
			TestTrue(FString::Printf(TEXT("Review objective %s state and time = mission"), *Review.Objectives[I].ObjectiveId.ToString()),
				Review.Objectives[I].ObjectiveId == FinalMission.ObjectiveSnapshots[I].ObjectiveId
				&& Review.Objectives[I].State == FinalMission.ObjectiveSnapshots[I].State
				&& Review.Objectives[I].ResolvedAtSeconds == FinalMission.ObjectiveSnapshots[I].ResolvedAtSeconds);
		}
	}
	TestTrue(TEXT("Review reports the limited power bus"), Review.bPowerBusLimited);
	TestTrue(FString::Printf(TEXT("Review peak curtailment %.2f kW = 2.5"), Review.PeakAuxiliaryCurtailedKilowatts), FMath::IsNearlyEqual(Review.PeakAuxiliaryCurtailedKilowatts, 2.5f, 1.0e-3f));
	TestTrue(TEXT("Review text says auxiliary systems were not fully powered"), ReviewText.Contains(TEXT("NOT fully powered")));
	return true;
}

#endif
