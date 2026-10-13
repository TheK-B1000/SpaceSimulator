// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/EdenSimulationClockSubsystem.h"
#include "Flight/EdenFlightMovementComponent.h"
#include "Flight/EdenFlightPlayerController.h"
#include "Flight/EdenFlightTypes.h"
#include "Flight/EdenPropulsionDemandSource.h"
#include "Flight/EdenSpacecraftPawn.h"
#include "Missions/EdenMissionDefinitionDataAsset.h"
#include "Missions/EdenMissionSubsystem.h"
#include "Operations/EdenOperatorControlComponent.h"
#include "Systems/EdenFuelConfigDataAsset.h"
#include "Systems/EdenFuelSystemComponent.h"
#include "Systems/EdenPowerConfigDataAsset.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalConfigDataAsset.h"
#include "Systems/EdenThermalSystemComponent.h"

#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

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
	TestTrue(TEXT("Thermal model advances in runtime wiring"), Thermal->GetThermalStateSnapshot().TemperatureCelsius > InitialTemperatureCelsius);

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

#endif
