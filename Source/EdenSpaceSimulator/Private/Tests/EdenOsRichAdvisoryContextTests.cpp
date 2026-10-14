// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenOsAdvisoryModel.h"
#include "EdenOs/EdenOsWireTypes.h"
#include "Systems/EdenThermalModel.h"
#include "Systems/EdenThermalSystemComponent.h"

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// H.1 rich advisory context (ExecPlan 0010, ADR-0004). Every rich field is optional: present only
// when its source was available at the newest snapshot, so absence means unknown and an
// observed zero is still sent. The body without rich fields must be byte-identical to today's.

namespace EdenOsRichContextTests
{
	const TCHAR* const RichFields[] = {
		TEXT("windowDurationSeconds"),
		TEXT("thermalWarningCelsius"),
		TEXT("thermalCriticalCelsius"),
		TEXT("thermalAmbientCelsius"),
		TEXT("thermalAbsoluteMaxCelsius"),
		TEXT("heatGenerationDegreesCelsiusPerSecond"),
		TEXT("effectiveDissipationDegreesCelsiusPerSecond"),
		TEXT("externalHeatingRateDegreesCelsiusPerSecond"),
		TEXT("thermalControlMode"),
		TEXT("loadShedMode"),
		TEXT("propulsionPriority"),
		TEXT("powerGenerationKilowatts"),
		TEXT("powerDemandKilowatts"),
		TEXT("netPowerKilowatts")};

	/** The pre-rich context used by the existing dual-timestamp test. */
	FEdenOsAdvisoryContext MakeLegacyContext()
	{
		FEdenOsAdvisoryContext Context;
		Context.bIsValid = true;
		Context.SessionId = TEXT("session-advisory-001");
		Context.SimulationTimeSeconds = 0.7f;
		Context.ContextSnapshotSimulationTimeSeconds = 0.5f;
		Context.MissionElapsedTimeSeconds = 0.5f;
		Context.ActiveMissionId = TEXT("SolarCrisis");
		Context.MissionState = EEdenMissionState::Running;
		Context.MissionPhase = EEdenMissionPhase::Impact;
		Context.Thermal.TemperatureCelsius = 91.0f;
		Context.Power.ChargeFraction = 0.42f;
		Context.Fuel.FuelFraction = 0.76f;
		Context.TriggerReasons = {
			EEdenOsAdvisoryTriggerReason::AlertTransition,
			EEdenOsAdvisoryTriggerReason::Heartbeat};
		return Context;
	}

	/** Same context with every rich source populated, as Unreal would build it in play. */
	FEdenOsAdvisoryContext MakeRichContext()
	{
		FEdenOsAdvisoryContext Context = MakeLegacyContext();
		Context.bHasWindowDuration = true;
		Context.WindowDurationSeconds = 4.5f;
		Context.Sources.bThermal = true;
		Context.Sources.bPower = true;
		Context.Sources.bOperator = true;
		Context.ThermalLimits.bIsValid = true;
		Context.ThermalLimits.AmbientTemperatureCelsius = 20.0f;
		Context.ThermalLimits.WarningTemperatureCelsius = 70.0f;
		Context.ThermalLimits.CriticalTemperatureCelsius = 100.0f;
		Context.ThermalLimits.AbsoluteMaxTemperatureCelsius = 120.0f;
		Context.Thermal.HeatGenerationDegreesCelsiusPerSecond = 1.0f;
		Context.Thermal.EffectiveDissipationDegreesCelsiusPerSecond = 2.5f;
		Context.Thermal.ExternalHeatingRateDegreesCelsiusPerSecond = 2.5f;
		Context.Operator.ThermalMode = EEdenThermalControlMode::Emergency;
		Context.Operator.LoadShedMode = EEdenLoadShedMode::Normal;
		Context.Operator.PropulsionPriority = EEdenPropulsionPriorityMode::Full;
		Context.Power.GenerationKilowatts = 2.0f;
		Context.Power.TotalDemandKilowatts = 6.0f;
		Context.Power.NetPowerKilowatts = -4.0f;
		return Context;
	}

	FString Serialize(const FEdenOsAdvisoryContext& Context)
	{
		FEdenOsAdvisoryRequestV1 Request;
		Request.SessionId = Context.SessionId;
		Request.EvaluationId = TEXT("eval-42");
		Request.Context = Context;
		const FEdenOsWireSerializationResult Result = FEdenOsWireSerializationModel::BuildAdvisoryJsonV1(Request);
		check(Result.IsSuccess());
		return Result.Json;
	}

	/** Parses the body like ProjectEden would and returns the context object. */
	TSharedPtr<FJsonObject> ParseContext(const FString& Json)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return nullptr;
		}
		const TSharedPtr<FJsonObject>* Context = nullptr;
		return Root->TryGetObjectField(TEXT("context"), Context) ? *Context : nullptr;
	}

	FEdenTelemetrySnapshot MakeSnapshot(int64 Sequence, float SimulationTimeSeconds)
	{
		FEdenTelemetrySnapshot Snapshot;
		Snapshot.SequenceNumber = Sequence;
		Snapshot.SimulationTimeSeconds = SimulationTimeSeconds;
		Snapshot.MissionElapsedTimeSeconds = SimulationTimeSeconds;
		Snapshot.Mission.MissionState = EEdenMissionState::Running;
		Snapshot.Mission.MissionPhase = EEdenMissionPhase::Impact;
		Snapshot.Thermal.TemperatureCelsius = 90.0f;
		return Snapshot;
	}

	FEdenOsAdvisoryContext BuildContextFrom(const TArray<FEdenTelemetrySnapshot>& Snapshots)
	{
		FEdenOsAdvisoryEvaluationInput Input;
		Input.Snapshots = Snapshots;
		Input.SessionId = TEXT("rich-session");
		Input.SimulationTimeSeconds = Snapshots.Num() > 0 ? Snapshots.Last().SimulationTimeSeconds : 0.0f;
		Input.MissionState = EEdenMissionState::Running;
		Input.AuthorityMode = EEdenOsAuthorityMode::Advisory;
		return FEdenOsAdvisoryModel::BuildContext(Input, {EEdenOsAdvisoryTriggerReason::Heartbeat});
	}

	FEdenThermalConfig MakeThermalConfig(float WarningCelsius)
	{
		FEdenThermalConfig Config;
		Config.AbsoluteMinTemperatureCelsius = -50.0f;
		Config.AmbientTemperatureCelsius = 20.0f;
		Config.WarningTemperatureCelsius = WarningCelsius;
		Config.CriticalTemperatureCelsius = 100.0f;
		Config.AbsoluteMaxTemperatureCelsius = 120.0f;
		Config.InitialTemperatureCelsius = 20.0f;
		return Config;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsRichContextLegacyBodyUnchangedTest,
	"Eden.Unit.EdenOs.RichContext.LegacyBodyIsByteIdenticalWithoutSources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsRichContextLegacyBodyUnchangedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsRichContextTests;

	const FString Expected = TEXT(
		"{\n"
		"  \"schemaVersion\": 1,\n"
		"  \"evaluationId\": \"eval-42\",\n"
		"  \"simulationTimeSeconds\": 0.700000,\n"
		"  \"contextSnapshotSimulationTimeSeconds\": 0.500000,\n"
		"  \"triggerReasons\": [\"alert_transition\", \"heartbeat\"],\n"
		"  \"context\": {\n"
		"    \"missionId\": \"SolarCrisis\",\n"
		"    \"missionState\": \"Running\",\n"
		"    \"phase\": \"Impact\",\n"
		"    \"missionElapsedTimeSeconds\": 0.500000,\n"
		"    \"temperatureCelsius\": 91.000000,\n"
		"    \"batteryFraction\": 0.420000,\n"
		"    \"fuelFraction\": 0.760000,\n"
		"    \"temperatureDeltaCelsius\": 0.000000,\n"
		"    \"batteryFractionDelta\": 0.000000,\n"
		"    \"fuelFractionDelta\": 0.000000,\n"
		"    \"snapshotSampleCount\": 0,\n"
		"    \"eventSampleCount\": 0,\n"
		"    \"contextTruncated\": false,\n"
		"    \"upstreamHistoryTruncated\": false\n"
		"  }\n"
		"}\n");
	TestEqual(TEXT("Legacy H.1 body is unchanged"), Serialize(MakeLegacyContext()), Expected);

	// Default values left by unavailable sources must not leak onto the wire.
	FEdenOsAdvisoryContext Stale = MakeRichContext();
	Stale.Sources = FEdenTelemetrySourceAvailability();
	Stale.bHasWindowDuration = false;
	TestEqual(TEXT("Unavailable sources serialize as the legacy body"), Serialize(Stale), Expected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsRichContextSerializesEveryFieldTest,
	"Eden.Unit.EdenOs.RichContext.SerializesEveryFieldFromAvailableSources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsRichContextSerializesEveryFieldTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsRichContextTests;

	const FString Json = Serialize(MakeRichContext());
	const TSharedPtr<FJsonObject> Context = ParseContext(Json);
	if (!TestTrue(TEXT("Rich body is valid JSON with a context object"), Context.IsValid()))
	{
		return false;
	}

	TestEqual(TEXT("14 legacy + 14 rich context keys"), Context->Values.Num(), 28);
	for (const TCHAR* Field : RichFields)
	{
		TestTrue(FString::Printf(TEXT("%s present"), Field), Context->HasField(Field));
	}

	TestEqual(TEXT("windowDurationSeconds"), Context->GetNumberField(TEXT("windowDurationSeconds")), 4.5);
	TestEqual(TEXT("thermalWarningCelsius"), Context->GetNumberField(TEXT("thermalWarningCelsius")), 70.0);
	TestEqual(TEXT("thermalCriticalCelsius"), Context->GetNumberField(TEXT("thermalCriticalCelsius")), 100.0);
	TestEqual(TEXT("thermalAmbientCelsius"), Context->GetNumberField(TEXT("thermalAmbientCelsius")), 20.0);
	TestEqual(TEXT("thermalAbsoluteMaxCelsius"), Context->GetNumberField(TEXT("thermalAbsoluteMaxCelsius")), 120.0);
	TestEqual(
		TEXT("heatGeneration"),
		Context->GetNumberField(TEXT("heatGenerationDegreesCelsiusPerSecond")),
		1.0);
	TestEqual(
		TEXT("effectiveDissipation"),
		Context->GetNumberField(TEXT("effectiveDissipationDegreesCelsiusPerSecond")),
		2.5);
	TestEqual(
		TEXT("externalHeating"),
		Context->GetNumberField(TEXT("externalHeatingRateDegreesCelsiusPerSecond")),
		2.5);
	TestEqual(TEXT("thermalControlMode"), Context->GetStringField(TEXT("thermalControlMode")), FString(TEXT("Emergency")));
	TestEqual(TEXT("loadShedMode"), Context->GetStringField(TEXT("loadShedMode")), FString(TEXT("Normal")));
	TestEqual(TEXT("propulsionPriority"), Context->GetStringField(TEXT("propulsionPriority")), FString(TEXT("Full")));
	TestEqual(TEXT("powerGeneration"), Context->GetNumberField(TEXT("powerGenerationKilowatts")), 2.0);
	TestEqual(TEXT("powerDemand"), Context->GetNumberField(TEXT("powerDemandKilowatts")), 6.0);
	TestEqual(TEXT("netPower"), Context->GetNumberField(TEXT("netPowerKilowatts")), -4.0);
	TestTrue(TEXT("Legacy keys keep their order and values"), Json.Contains(TEXT("\"upstreamHistoryTruncated\": false,\n")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsRichContextOmitsUnavailableSourcesTest,
	"Eden.Unit.EdenOs.RichContext.OmitsEachUnavailableSourceIndependently",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsRichContextOmitsUnavailableSourcesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsRichContextTests;

	FEdenOsAdvisoryContext NoThermal = MakeRichContext();
	NoThermal.Sources.bThermal = false;
	const TSharedPtr<FJsonObject> WithoutThermal = ParseContext(Serialize(NoThermal));
	TestFalse(TEXT("No thresholds without thermal"), WithoutThermal->HasField(TEXT("thermalWarningCelsius")));
	TestFalse(TEXT("No ambient without thermal"), WithoutThermal->HasField(TEXT("thermalAmbientCelsius")));
	TestFalse(
		TEXT("No heat rates without thermal"),
		WithoutThermal->HasField(TEXT("heatGenerationDegreesCelsiusPerSecond")));
	TestTrue(TEXT("Power still sent"), WithoutThermal->HasField(TEXT("netPowerKilowatts")));
	TestTrue(TEXT("Modes still sent"), WithoutThermal->HasField(TEXT("thermalControlMode")));

	FEdenOsAdvisoryContext NoPower = MakeRichContext();
	NoPower.Sources.bPower = false;
	const TSharedPtr<FJsonObject> WithoutPower = ParseContext(Serialize(NoPower));
	TestFalse(TEXT("No generation without power"), WithoutPower->HasField(TEXT("powerGenerationKilowatts")));
	TestFalse(TEXT("No demand without power"), WithoutPower->HasField(TEXT("powerDemandKilowatts")));
	TestFalse(TEXT("No net power without power"), WithoutPower->HasField(TEXT("netPowerKilowatts")));
	TestTrue(TEXT("Thermal still sent"), WithoutPower->HasField(TEXT("thermalWarningCelsius")));
	TestTrue(TEXT("Legacy batteryFraction is unchanged"), WithoutPower->HasField(TEXT("batteryFraction")));

	FEdenOsAdvisoryContext NoOperator = MakeRichContext();
	NoOperator.Sources.bOperator = false;
	const TSharedPtr<FJsonObject> WithoutOperator = ParseContext(Serialize(NoOperator));
	TestFalse(TEXT("No thermal mode without operator"), WithoutOperator->HasField(TEXT("thermalControlMode")));
	TestFalse(TEXT("No load shed mode without operator"), WithoutOperator->HasField(TEXT("loadShedMode")));
	TestFalse(TEXT("No propulsion priority without operator"), WithoutOperator->HasField(TEXT("propulsionPriority")));
	TestTrue(TEXT("Rates still sent"), WithoutOperator->HasField(TEXT("effectiveDissipationDegreesCelsiusPerSecond")));

	FEdenOsAdvisoryContext InvalidLimits = MakeRichContext();
	InvalidLimits.ThermalLimits.bIsValid = false;
	const TSharedPtr<FJsonObject> WithoutLimits = ParseContext(Serialize(InvalidLimits));
	TestFalse(TEXT("No warning with invalid limits"), WithoutLimits->HasField(TEXT("thermalWarningCelsius")));
	TestFalse(TEXT("No absolute max with invalid limits"), WithoutLimits->HasField(TEXT("thermalAbsoluteMaxCelsius")));
	TestTrue(
		TEXT("Thermal rates kept with invalid limits"),
		WithoutLimits->HasField(TEXT("heatGenerationDegreesCelsiusPerSecond")));

	FEdenOsAdvisoryContext NoWindow = MakeRichContext();
	NoWindow.bHasWindowDuration = false;
	TestFalse(
		TEXT("No window duration without two snapshots"),
		ParseContext(Serialize(NoWindow))->HasField(TEXT("windowDurationSeconds")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsRichContextObservedZeroAndNonFiniteTest,
	"Eden.Unit.EdenOs.RichContext.ObservedZeroIsSentAndNonFiniteIsOmitted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsRichContextObservedZeroAndNonFiniteTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsRichContextTests;

	FEdenOsAdvisoryContext Context = MakeRichContext();
	Context.Thermal.ExternalHeatingRateDegreesCelsiusPerSecond = 0.0f;
	Context.Power.NetPowerKilowatts = 0.0f;
	Context.Power.TotalDemandKilowatts = std::numeric_limits<float>::quiet_NaN();
	Context.Thermal.HeatGenerationDegreesCelsiusPerSecond = std::numeric_limits<float>::infinity();

	const FString Json = Serialize(Context);
	const TSharedPtr<FJsonObject> Parsed = ParseContext(Json);
	if (!TestTrue(TEXT("Body stays valid JSON"), Parsed.IsValid()))
	{
		return false;
	}
	TestTrue(
		TEXT("Observed zero external heating is sent"),
		Json.Contains(TEXT("\"externalHeatingRateDegreesCelsiusPerSecond\": 0.000000")));
	TestTrue(TEXT("Observed zero net power is sent"), Json.Contains(TEXT("\"netPowerKilowatts\": 0.000000")));
	TestFalse(TEXT("NaN demand omitted"), Parsed->HasField(TEXT("powerDemandKilowatts")));
	TestFalse(TEXT("Infinite generation omitted"), Parsed->HasField(TEXT("heatGenerationDegreesCelsiusPerSecond")));
	TestTrue(TEXT("Other power fields kept"), Parsed->HasField(TEXT("powerGenerationKilowatts")));
	TestFalse(TEXT("No literal nan"), Json.Contains(TEXT("nan"), ESearchCase::IgnoreCase));
	TestFalse(TEXT("No literal inf"), Json.Contains(TEXT("inf"), ESearchCase::IgnoreCase));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsRichContextOperatorModeVocabularyTest,
	"Eden.Unit.EdenOs.RichContext.OperatorModesUseExactWireVocabulary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsRichContextOperatorModeVocabularyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsRichContextTests;

	const TPair<EEdenThermalControlMode, const TCHAR*> ThermalModes[] = {
		{EEdenThermalControlMode::Off, TEXT("Off")},
		{EEdenThermalControlMode::Nominal, TEXT("Nominal")},
		{EEdenThermalControlMode::Boost, TEXT("Boost")},
		{EEdenThermalControlMode::Emergency, TEXT("Emergency")}};
	for (const TPair<EEdenThermalControlMode, const TCHAR*>& Mode : ThermalModes)
	{
		FEdenOsAdvisoryContext Context = MakeRichContext();
		Context.Operator.ThermalMode = Mode.Key;
		TestEqual(
			FString::Printf(TEXT("thermalControlMode %s"), Mode.Value),
			ParseContext(Serialize(Context))->GetStringField(TEXT("thermalControlMode")),
			FString(Mode.Value));
	}

	FEdenOsAdvisoryContext Shed = MakeRichContext();
	Shed.Operator.LoadShedMode = EEdenLoadShedMode::Shed;
	Shed.Operator.PropulsionPriority = EEdenPropulsionPriorityMode::Reduced;
	const TSharedPtr<FJsonObject> Parsed = ParseContext(Serialize(Shed));
	TestEqual(TEXT("loadShedMode Shed"), Parsed->GetStringField(TEXT("loadShedMode")), FString(TEXT("Shed")));
	TestEqual(
		TEXT("propulsionPriority Reduced"),
		Parsed->GetStringField(TEXT("propulsionPriority")),
		FString(TEXT("Reduced")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsRichContextWindowDurationTest,
	"Eden.Unit.EdenOs.RichContext.WindowDurationComesFromSnapshotTimestamps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsRichContextWindowDurationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsRichContextTests;

	const FEdenOsAdvisoryContext Three =
		BuildContextFrom({MakeSnapshot(1, 10.0f), MakeSnapshot(2, 10.5f), MakeSnapshot(3, 11.0f)});
	TestTrue(TEXT("Three snapshots have a window"), Three.bHasWindowDuration);
	TestEqual(TEXT("Newest minus oldest"), Three.WindowDurationSeconds, 1.0f);

	const FEdenOsAdvisoryContext One = BuildContextFrom({MakeSnapshot(1, 10.0f)});
	TestFalse(TEXT("One snapshot has no window"), One.bHasWindowDuration);

	const FEdenOsAdvisoryContext SameTime = BuildContextFrom({MakeSnapshot(1, 10.0f), MakeSnapshot(2, 10.0f)});
	TestFalse(TEXT("Zero span is not a window"), SameTime.bHasWindowDuration);

	// The bounded window, not the whole history, defines the duration the trend deltas cover.
	TArray<FEdenTelemetrySnapshot> Long;
	for (int32 Index = 0; Index < 14; ++Index)
	{
		Long.Add(MakeSnapshot(Index + 1, 0.5f * static_cast<float>(Index)));
	}
	const FEdenOsAdvisoryContext Bounded = BuildContextFrom(Long);
	TestEqual(TEXT("Default bound keeps 10 snapshots"), Bounded.RecentSnapshots.Num(), 10);
	TestEqual(TEXT("Duration spans the bounded window"), Bounded.WindowDurationSeconds, 4.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsRichContextNewestSnapshotAlignmentTest,
	"Eden.Unit.EdenOs.RichContext.SourcesAndLimitsComeFromTheNewestSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsRichContextNewestSnapshotAlignmentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsRichContextTests;

	FEdenTelemetrySnapshot Older = MakeSnapshot(1, 10.0f);
	Older.Sources.bThermal = true;
	Older.ThermalLimits = FEdenThermalModel::MakeLimitsSnapshot(MakeThermalConfig(70.0f));
	Older.Thermal.ExternalHeatingRateDegreesCelsiusPerSecond = 0.0f;

	FEdenTelemetrySnapshot Newer = MakeSnapshot(2, 10.5f);
	Newer.Sources.bThermal = true;
	Newer.ThermalLimits = FEdenThermalModel::MakeLimitsSnapshot(MakeThermalConfig(80.0f));
	Newer.Thermal.ExternalHeatingRateDegreesCelsiusPerSecond = 1.5f;

	const FEdenOsAdvisoryContext Context = BuildContextFrom({Older, Newer});
	TestEqual(TEXT("Snapshot time is the newest"), Context.ContextSnapshotSimulationTimeSeconds, 10.5f);
	TestEqual(TEXT("Warning from the newest snapshot"), Context.ThermalLimits.WarningTemperatureCelsius, 80.0f);
	TestEqual(
		TEXT("Rates from the same snapshot"),
		Context.Thermal.ExternalHeatingRateDegreesCelsiusPerSecond,
		1.5f);

	// Thermal lost at the newest snapshot: older observations are not substituted.
	FEdenTelemetrySnapshot Lost = MakeSnapshot(3, 11.0f);
	const FEdenOsAdvisoryContext AfterLoss = BuildContextFrom({Older, Newer, Lost});
	TestFalse(TEXT("Newest snapshot governs availability"), AfterLoss.Sources.bThermal);
	TestFalse(
		TEXT("No thresholds serialized after loss"),
		ParseContext(Serialize(AfterLoss))->HasField(TEXT("thermalWarningCelsius")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsRichContextLimitsCapturedByValueTest,
	"Eden.Unit.EdenOs.RichContext.ThermalLimitsAreCapturedAtTheSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsRichContextLimitsCapturedByValueTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsRichContextTests;

	UEdenThermalSystemComponent* Thermal = NewObject<UEdenThermalSystemComponent>();
	TestTrue(TEXT("Initial config accepted"), Thermal->InitializeThermalSimulation(MakeThermalConfig(70.0f)));
	const FEdenThermalLimitsSnapshot Captured = FEdenThermalModel::MakeLimitsSnapshot(Thermal->GetActiveThermalConfig());

	// A threshold override after the snapshot must not rewrite what that snapshot reported.
	TestTrue(TEXT("Override accepted"), Thermal->InitializeThermalSimulation(MakeThermalConfig(80.0f)));
	const FEdenThermalLimitsSnapshot AfterOverride =
		FEdenThermalModel::MakeLimitsSnapshot(Thermal->GetActiveThermalConfig());

	TestTrue(TEXT("Captured limits valid"), Captured.bIsValid);
	TestEqual(TEXT("Earlier snapshot keeps 70 C"), Captured.WarningTemperatureCelsius, 70.0f);
	TestEqual(TEXT("Later snapshot sees the override"), AfterOverride.WarningTemperatureCelsius, 80.0f);
	TestEqual(TEXT("Ambient captured"), Captured.AmbientTemperatureCelsius, 20.0f);
	TestEqual(TEXT("Absolute max captured"), Captured.AbsoluteMaxTemperatureCelsius, 120.0f);

	FEdenThermalConfig Inverted = MakeThermalConfig(70.0f);
	Inverted.CriticalTemperatureCelsius = 60.0f;
	TestFalse(TEXT("Invalid config yields invalid limits"), FEdenThermalModel::MakeLimitsSnapshot(Inverted).bIsValid);
	return true;
}

#endif
