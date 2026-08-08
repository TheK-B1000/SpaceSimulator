// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenOsAdapterSubsystem.h"
#include "EdenOs/EdenOsConnectionSettings.h"
#include "EdenOs/EdenOsTypes.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace EdenOsConnectionTests
{
	FEdenOsConnectionConfig MakeEnabledValidConfig()
	{
		FEdenOsConnectionConfig Config;
		Config.bEnabled = true;
		Config.BaseUrl = TEXT("https://eden.test");
		Config.ConnectionTimeoutSeconds = 2.0f;
		Config.RequestTimeoutSeconds = 5.0f;
		Config.MaxQueueDepth = 128;
		Config.AdvisoryHeartbeatSimulationSeconds = 5.0f;
		Config.AuthorityMode = EEdenOsAuthorityMode::Advisory;
		return Config;
	}

	bool ContainsText(const TArray<FString>& Values, const FString& Needle)
	{
		for (const FString& Value : Values)
		{
			if (Value.Contains(Needle))
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsConfigDisabledValidatesWithoutUrlOrTokenTest,
	"Eden.Unit.EdenOs.Config.DisabledValidatesWithoutUrlOrToken",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsConfigDisabledValidatesWithoutUrlOrTokenTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FEdenOsConnectionConfig Config;
	Config.bEnabled = false;
	Config.BaseUrl.Reset();
	Config.RuntimeBearerJwt.Reset();

	const FEdenOsValidationResult Result = FEdenOsConnectionConfigModel::Validate(Config);
	const FEdenOsConnectionSnapshot Snapshot = FEdenOsConnectionConfigModel::MakeInitialSnapshot(Config, Result);

	TestTrue(TEXT("Disabled config is valid without URL/token"), Result.IsValid());
	TestEqual(TEXT("Disabled state"), Snapshot.ConnectionState, EEdenOsConnectionState::Disabled);
	TestFalse(TEXT("No bearer token"), Snapshot.bHasBearerJwt);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsConfigEnabledRejectsEmptyBaseUrlTest,
	"Eden.Unit.EdenOs.Config.EnabledRejectsEmptyBaseUrl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsConfigEnabledRejectsEmptyBaseUrlTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsConnectionTests;

	FEdenOsConnectionConfig Config = MakeEnabledValidConfig();
	Config.BaseUrl.Reset();

	const FEdenOsValidationResult Result = FEdenOsConnectionConfigModel::Validate(Config);
	TestFalse(TEXT("Enabled empty URL rejected"), Result.IsValid());
	TestTrue(TEXT("Error mentions BaseUrl"), ContainsText(Result.Errors, TEXT("BaseUrl")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsConfigEnabledRejectsEmptyScenarioIdTest,
	"Eden.Unit.EdenOs.Config.EnabledRejectsEmptyScenarioId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsConfigEnabledRejectsEmptyScenarioIdTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsConnectionTests;

	FEdenOsConnectionConfig Config = MakeEnabledValidConfig();
	Config.DefaultScenarioId.Reset();

	const FEdenOsValidationResult Result = FEdenOsConnectionConfigModel::Validate(Config);
	TestFalse(TEXT("Enabled empty scenario rejected"), Result.IsValid());
	TestTrue(TEXT("Error mentions DefaultScenarioId"), ContainsText(Result.Errors, TEXT("DefaultScenarioId")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsConfigMalformedBaseUrlRejectedTest,
	"Eden.Unit.EdenOs.Config.MalformedBaseUrlRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsConfigMalformedBaseUrlRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsConnectionTests;

	FEdenOsConnectionConfig MissingScheme = MakeEnabledValidConfig();
	MissingScheme.BaseUrl = TEXT("eden.test");
	TestFalse(TEXT("Missing scheme rejected"), FEdenOsConnectionConfigModel::Validate(MissingScheme).IsValid());

	FEdenOsConnectionConfig UnsupportedScheme = MakeEnabledValidConfig();
	UnsupportedScheme.BaseUrl = TEXT("ws://eden.test");
	const FEdenOsValidationResult UnsupportedResult = FEdenOsConnectionConfigModel::Validate(UnsupportedScheme);
	TestFalse(TEXT("Unsupported scheme rejected"), UnsupportedResult.IsValid());
	TestTrue(TEXT("Unsupported error mentions http"), ContainsText(UnsupportedResult.Errors, TEXT("http")));

	FEdenOsConnectionConfig MissingHost = MakeEnabledValidConfig();
	MissingHost.BaseUrl = TEXT("https://");
	const FEdenOsValidationResult MissingHostResult = FEdenOsConnectionConfigModel::Validate(MissingHost);
	TestFalse(TEXT("Missing host rejected"), MissingHostResult.IsValid());
	TestTrue(TEXT("Missing host error"), ContainsText(MissingHostResult.Errors, TEXT("host")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsConfigInvalidTimeoutsRejectedTest,
	"Eden.Unit.EdenOs.Config.InvalidTimeoutsRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsConfigInvalidTimeoutsRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsConnectionTests;

	FEdenOsConnectionConfig ZeroConnectionTimeout = MakeEnabledValidConfig();
	ZeroConnectionTimeout.ConnectionTimeoutSeconds = 0.0f;
	TestFalse(TEXT("Zero connection timeout rejected"), FEdenOsConnectionConfigModel::Validate(ZeroConnectionTimeout).IsValid());

	FEdenOsConnectionConfig NegativeRequestTimeout = MakeEnabledValidConfig();
	NegativeRequestTimeout.RequestTimeoutSeconds = -0.5f;
	TestFalse(TEXT("Negative request timeout rejected"), FEdenOsConnectionConfigModel::Validate(NegativeRequestTimeout).IsValid());

	FEdenOsConnectionConfig InfiniteTimeout = MakeEnabledValidConfig();
	InfiniteTimeout.RequestTimeoutSeconds = std::numeric_limits<float>::infinity();
	TestFalse(TEXT("Infinite request timeout rejected"), FEdenOsConnectionConfigModel::Validate(InfiniteTimeout).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsConfigInvalidMaxQueueDepthRejectedTest,
	"Eden.Unit.EdenOs.Config.InvalidMaxQueueDepthRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsConfigInvalidMaxQueueDepthRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsConnectionTests;

	FEdenOsConnectionConfig Config = MakeEnabledValidConfig();
	Config.MaxQueueDepth = 0;
	const FEdenOsValidationResult Result = FEdenOsConnectionConfigModel::Validate(Config);

	TestFalse(TEXT("Zero queue depth rejected"), Result.IsValid());
	TestTrue(TEXT("Queue depth error"), ContainsText(Result.Errors, TEXT("MaxQueueDepth")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsConfigInvalidAdvisoryHeartbeatRejectedTest,
	"Eden.Unit.EdenOs.Config.InvalidAdvisoryHeartbeatRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsConfigInvalidAdvisoryHeartbeatRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsConnectionTests;

	FEdenOsConnectionConfig Config = MakeEnabledValidConfig();
	Config.AdvisoryHeartbeatSimulationSeconds = -1.0f;
	const FEdenOsValidationResult Result = FEdenOsConnectionConfigModel::Validate(Config);

	TestFalse(TEXT("Negative heartbeat rejected"), Result.IsValid());
	TestTrue(TEXT("Heartbeat error"), ContainsText(Result.Errors, TEXT("AdvisoryHeartbeatSimulationSeconds")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsAuthJwtAbsentBehaviorExplicitTest,
	"Eden.Unit.EdenOs.Auth.JwtAbsentBehaviorExplicit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsAuthJwtAbsentBehaviorExplicitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsConnectionTests;

	FEdenOsConnectionConfig Config = MakeEnabledValidConfig();
	Config.RuntimeBearerJwt.Reset();

	const FEdenOsValidationResult Result = FEdenOsConnectionConfigModel::Validate(Config);
	const FEdenOsConnectionSnapshot Snapshot = FEdenOsConnectionConfigModel::MakeInitialSnapshot(Config, Result);

	TestTrue(TEXT("Enabled config can be staged before runtime token injection"), Result.IsValid());
	TestTrue(TEXT("Absent token warning is explicit"), ContainsText(Result.Warnings, TEXT("Bearer JWT absent")));
	TestFalse(TEXT("Snapshot exposes token presence only"), Snapshot.bHasBearerJwt);
	TestEqual(TEXT("Valid enabled config starts disconnected"), Snapshot.ConnectionState, EEdenOsConnectionState::Disconnected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsConfigAuthorityModeDefaultsSafelyTest,
	"Eden.Unit.EdenOs.Config.AuthorityModeDefaultsSafely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsConfigAuthorityModeDefaultsSafelyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FEdenOsConnectionConfig Config;
	TestEqual(TEXT("0007 default ceiling is Advisory"), Config.AuthorityMode, EEdenOsAuthorityMode::Advisory);
	TestNotEqual(TEXT("AuthorizedControl is not default"), Config.AuthorityMode, EEdenOsAuthorityMode::AuthorizedControl);
	TestTrue(
		TEXT("Advisory heartbeat default is locked at 5 seconds"),
		FMath::IsNearlyEqual(Config.AdvisoryHeartbeatSimulationSeconds, 5.0f));

	FEdenOsConnectionConfig Authorized = Config;
	Authorized.AuthorityMode = EEdenOsAuthorityMode::AuthorizedControl;
	const FEdenOsValidationResult Result = FEdenOsConnectionConfigModel::Validate(Authorized);
	TestTrue(TEXT("Authority enum exists without making config invalid"), Result.IsValid());
	TestTrue(TEXT("AuthorizedControl warning is explicit"), Result.Warnings.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsStateConnectionSnapshotDefaultsDeterministicallyTest,
	"Eden.Unit.EdenOs.State.ConnectionSnapshotDefaultsDeterministically",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsStateConnectionSnapshotDefaultsDeterministicallyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FEdenOsConnectionConfig Config;
	const FEdenOsValidationResult Result = FEdenOsConnectionConfigModel::Validate(Config);
	const FEdenOsConnectionSnapshot Snapshot = FEdenOsConnectionConfigModel::MakeInitialSnapshot(Config, Result);

	TestEqual(TEXT("Schema version"), Snapshot.SchemaVersion, 1);
	TestFalse(TEXT("Disabled by default"), Snapshot.bEnabled);
	TestEqual(TEXT("Default state"), Snapshot.ConnectionState, EEdenOsConnectionState::Disabled);
	TestEqual(TEXT("Default authority"), Snapshot.AuthorityMode, EEdenOsAuthorityMode::Advisory);
	TestFalse(TEXT("External command validation disabled by default"), Snapshot.bExternalCommandValidationEnabled);
	TestEqual(TEXT("Pending count"), Snapshot.PendingMessageCount, 0);
	TestEqual(TEXT("Dropped count"), Snapshot.DroppedMessageCount, 0);
	TestTrue(TEXT("No default error"), Snapshot.LastErrorSummary.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsSecuritySecretsNotSerializedOrLoggedTest,
	"Eden.Unit.EdenOs.Security.SecretsNotSerializedOrLogged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsSecuritySecretsNotSerializedOrLoggedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsConnectionTests;

	FEdenOsConnectionConfig Config = MakeEnabledValidConfig();
	Config.RuntimeBearerJwt = TEXT("test-token");
	const FEdenOsValidationResult Result = FEdenOsConnectionConfigModel::Validate(Config);
	const FEdenOsConnectionSnapshot Snapshot = FEdenOsConnectionConfigModel::MakeInitialSnapshot(Config, Result);
	const FString Summary = FEdenOsConnectionConfigModel::DescribeForLog(Config, Snapshot, Result);

	TestTrue(TEXT("Synthetic token makes presence true"), Snapshot.bHasBearerJwt);
	TestFalse(TEXT("Log summary does not contain token value"), Summary.Contains(Config.RuntimeBearerJwt));
	TestTrue(TEXT("Log summary reports token only as set"), Summary.Contains(TEXT("BearerJwt=set")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsSubsystemOwnsRuntimeSnapshotTest,
	"Eden.Unit.EdenOs.Subsystem.OwnsRuntimeSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsSubsystemOwnsRuntimeSnapshotTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace EdenOsConnectionTests;

	UEdenOsAdapterSubsystem* Subsystem = NewObject<UEdenOsAdapterSubsystem>();
	FEdenOsConnectionConfig Config = MakeEnabledValidConfig();

	TestTrue(TEXT("Runtime config applies"), Subsystem->ApplyRuntimeConfig(Config));
	FEdenOsConnectionSnapshot Snapshot = Subsystem->GetConnectionSnapshot();
	TestEqual(TEXT("Enabled valid config starts disconnected"), Snapshot.ConnectionState, EEdenOsConnectionState::Disconnected);
	TestFalse(TEXT("No token before injection"), Snapshot.bHasBearerJwt);

	Subsystem->SetRuntimeBearerJwt(TEXT("test-token"));
	Snapshot = Subsystem->GetConnectionSnapshot();
	TestTrue(TEXT("Token presence updates through owner"), Snapshot.bHasBearerJwt);

	Subsystem->ClearRuntimeBearerJwt();
	Snapshot = Subsystem->GetConnectionSnapshot();
	TestFalse(TEXT("Token clears through owner"), Snapshot.bHasBearerJwt);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEdenOsSubsystemEnableRuntimeConnectionPreservesJwtTest,
	"Eden.Unit.EdenOs.Subsystem.EnableRuntimeConnectionPreservesJwt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEdenOsSubsystemEnableRuntimeConnectionPreservesJwtTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UEdenOsAdapterSubsystem* Subsystem = NewObject<UEdenOsAdapterSubsystem>();
	Subsystem->SetRuntimeBearerJwt(TEXT("preserved-token"));
	TestTrue(
		TEXT("EnableRuntimeConnection accepts valid BaseUrl"),
		Subsystem->EnableRuntimeConnection(TEXT("http://127.0.0.1:8791")));

	const FEdenOsConnectionSnapshot Snapshot = Subsystem->GetConnectionSnapshot();
	TestTrue(TEXT("EnableRuntimeConnection leaves transport enabled"), Snapshot.bEnabled);
	TestTrue(TEXT("EnableRuntimeConnection preserves injected Bearer JWT"), Snapshot.bHasBearerJwt);
	TestEqual(
		TEXT("EnableRuntimeConnection starts disconnected until delivery"),
		Snapshot.ConnectionState,
		EEdenOsConnectionState::Disconnected);
	return true;
}

#endif
