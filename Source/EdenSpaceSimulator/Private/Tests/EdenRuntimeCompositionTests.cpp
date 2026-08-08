// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/EdenSimulationClockSubsystem.h"
#include "Flight/EdenFlightMovementComponent.h"
#include "Flight/EdenFlightPlayerController.h"
#include "Flight/EdenFlightTypes.h"
#include "Flight/EdenPropulsionDemandSource.h"
#include "Flight/EdenSpacecraftPawn.h"
#include "Missions/EdenMissionDefinitionDataAsset.h"
#include "Missions/EdenMissionSubsystem.h"
#include "Systems/EdenFuelConfigDataAsset.h"
#include "Systems/EdenFuelSystemComponent.h"
#include "Systems/EdenPowerConfigDataAsset.h"
#include "Systems/EdenPowerSystemComponent.h"
#include "Systems/EdenThermalConfigDataAsset.h"
#include "Systems/EdenThermalSystemComponent.h"

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

	TestNotNull(TEXT("Flight movement component exists"), Flight);
	TestNotNull(TEXT("Fuel component exists"), Fuel);
	TestNotNull(TEXT("Power component exists"), Power);
	TestNotNull(TEXT("Thermal component exists"), Thermal);
	if (!Flight || !Fuel || !Power || !Thermal)
	{
		return false;
	}

	TestEqual(TEXT("Exactly one flight movement component exists"), EdenRuntimeCompositionTests::CountComponents<UEdenFlightMovementComponent>(Pawn), 1);
	TestEqual(TEXT("Exactly one fuel component exists"), EdenRuntimeCompositionTests::CountComponents<UEdenFuelSystemComponent>(Pawn), 1);
	TestEqual(TEXT("Exactly one power component exists"), EdenRuntimeCompositionTests::CountComponents<UEdenPowerSystemComponent>(Pawn), 1);
	TestEqual(TEXT("Exactly one thermal component exists"), EdenRuntimeCompositionTests::CountComponents<UEdenThermalSystemComponent>(Pawn), 1);
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

	TestEqual(TEXT("Fuel, power, and thermal registered with clock"), Clock->GetSubscriberCount(), 3);

	const float InitialElapsedSeconds = Clock->GetElapsedSimulationTimeSeconds();
	const float InitialFuelKilograms = Fuel->GetFuelStateSnapshot().FuelQuantityKilograms;
	const float InitialTemperatureCelsius = Thermal->GetThermalStateSnapshot().TemperatureCelsius;

	FEdenFlightInputCommand ThrustCommand;
	ThrustCommand.TranslationInput = FVector(1.0f, 0.0f, 0.0f);
	Pawn->ApplyFlightInputCommand(ThrustCommand, 0.1f);
	TestTrue(TEXT("Flight movement exposes nonzero propulsion demand"), Flight->GetPropulsionDemandNormalized() > 0.0f);

	Clock->Tick(1.0f);
	TestTrue(TEXT("Simulation time advances"), Clock->GetElapsedSimulationTimeSeconds() > InitialElapsedSeconds);
	TestTrue(TEXT("Sustained runtime propulsion demand reduces fuel"), Fuel->GetFuelStateSnapshot().FuelQuantityKilograms < InitialFuelKilograms);
	TestTrue(TEXT("Thermal model advances in runtime wiring"), Thermal->GetThermalStateSnapshot().TemperatureCelsius > InitialTemperatureCelsius);

	const float FuelAfterThrustKilograms = Fuel->GetFuelStateSnapshot().FuelQuantityKilograms;
	FEdenFlightInputCommand ZeroCommand;
	Pawn->ApplyFlightInputCommand(ZeroCommand, 0.1f);
	TestEqual(TEXT("Zero command clears propulsion demand"), Flight->GetPropulsionDemandNormalized(), 0.0f);

	Clock->Tick(1.0f);
	EdenRuntimeCompositionTests::TestFloatNearlyEqual(
		*this,
		TEXT("Zero propulsion demand stops propulsion fuel consumption"),
		Fuel->GetFuelStateSnapshot().FuelQuantityKilograms,
		FuelAfterThrustKilograms);

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
	Flight->SetAngularVelocityLocalDegreesPerSecond(FVector(10.0f, 20.0f, 30.0f));

	Controller->Possess(Pawn);

	const FEdenFlightInputCommand CurrentCommand = Controller->GetCurrentFlightInputCommand();
	TestEqual(TEXT("Controller OnPossess clears translation intent"), CurrentCommand.TranslationInput, FVector::ZeroVector);
	TestEqual(TEXT("Controller OnPossess clears rotation intent"), CurrentCommand.RotationInput, FVector::ZeroVector);
	TestTrue(TEXT("Controller OnPossess restores stabilization default"), CurrentCommand.bStabilizationEnabled);
	TestEqual(TEXT("Pawn reset during OnPossess clears linear velocity"), Flight->Velocity, FVector::ZeroVector);
	TestEqual(TEXT("Pawn reset during OnPossess clears angular velocity"), Flight->GetAngularVelocityLocalDegreesPerSecond(), FVector::ZeroVector);
	TestEqual(TEXT("Pawn reset during OnPossess clears propulsion demand"), Flight->GetPropulsionDemandNormalized(), 0.0f);

	TestTrue(TEXT("Test re-seeds controller input intent after possession"), EdenRuntimeCompositionTests::SeedControllerInputIntent(Controller, SeededCommand));
	Flight->Velocity = FVector(10.0f, 20.0f, 30.0f);
	Flight->SetAngularVelocityLocalDegreesPerSecond(FVector(5.0f, 6.0f, 7.0f));

	Controller->ClearFlightInputIntent();
	Pawn->ResetFlightState();

	const FEdenFlightInputCommand ClearedCommand = Controller->GetCurrentFlightInputCommand();
	TestEqual(TEXT("Explicit controller reset clears translation intent"), ClearedCommand.TranslationInput, FVector::ZeroVector);
	TestEqual(TEXT("Explicit controller reset clears rotation intent"), ClearedCommand.RotationInput, FVector::ZeroVector);
	TestTrue(TEXT("Explicit controller reset restores stabilization default"), ClearedCommand.bStabilizationEnabled);
	TestEqual(TEXT("Explicit pawn reset clears linear velocity"), Flight->Velocity, FVector::ZeroVector);
	TestEqual(TEXT("Explicit pawn reset clears angular velocity"), Flight->GetAngularVelocityLocalDegreesPerSecond(), FVector::ZeroVector);
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

#endif
