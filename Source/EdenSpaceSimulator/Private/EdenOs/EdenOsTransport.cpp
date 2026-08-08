// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenOsTransport.h"

#include "GenericPlatform/GenericPlatformHttp.h"

FEdenOsHttpResult FEdenOsHttpResult::Succeeded(int32 InHttpStatusCode, FString InResponseBodyJson)
{
	FEdenOsHttpResult Result;
	Result.HttpStatusCode = InHttpStatusCode;
	Result.ResponseBodyJson = MoveTemp(InResponseBodyJson);
	return Result;
}

FEdenOsHttpResult FEdenOsHttpResult::Failed(
	int32 InHttpStatusCode,
	FString InErrorSummary,
	FString InResponseBodyJson)
{
	FEdenOsHttpResult Result;
	Result.HttpStatusCode = InHttpStatusCode;
	Result.ErrorSummary = FEdenOsTransportModel::SanitizeErrorSummary(MoveTemp(InErrorSummary));
	Result.ResponseBodyJson = MoveTemp(InResponseBodyJson);
	return Result;
}

bool FEdenOsHttpResult::IsSuccess() const
{
	return bCompleted && FEdenOsTransportModel::IsSuccessfulHttpStatus(HttpStatusCode) && ErrorSummary.IsEmpty();
}

FString FEdenOsUrlModel::JoinBaseUrlAndRoute(const FString& BaseUrl, const FString& RoutePath)
{
	FString TrimmedBase = BaseUrl.TrimStartAndEnd();
	FString TrimmedRoute = RoutePath.TrimStartAndEnd();

	while (TrimmedBase.EndsWith(TEXT("/")))
	{
		TrimmedBase.LeftChopInline(1);
	}
	while (TrimmedRoute.StartsWith(TEXT("/")))
	{
		TrimmedRoute.RightChopInline(1);
	}

	if (TrimmedRoute.IsEmpty())
	{
		return TrimmedBase;
	}
	return FString::Printf(TEXT("%s/%s"), *TrimmedBase, *TrimmedRoute);
}

FString FEdenOsUrlModel::BuildSessionRoute(const FString& RouteTemplate, const FString& SessionId)
{
	return RouteTemplate.Replace(TEXT("{id}"), *FGenericPlatformHttp::UrlEncode(SessionId), ESearchCase::CaseSensitive);
}

bool FEdenOsTransportModel::IsSuccessfulHttpStatus(int32 StatusCode)
{
	return StatusCode >= 200 && StatusCode <= 299;
}

FString FEdenOsTransportModel::SanitizeErrorSummary(const FString& ErrorSummary)
{
	FString Sanitized = ErrorSummary.Left(EdenOsTransport::MaxErrorSummaryCharacters);
	Sanitized.ReplaceInline(TEXT("\r"), TEXT(" "));
	Sanitized.ReplaceInline(TEXT("\n"), TEXT(" "));
	return Sanitized;
}

FEdenOsConnectionSnapshot FEdenOsTransportModel::MakeSnapshotForOutcome(
	const FEdenOsConnectionSnapshot& PreviousSnapshot,
	const FEdenOsConnectionConfig& Config,
	const FEdenOsValidationResult& Validation,
	bool bHasInFlightRequest,
	int32 PendingMessageCount,
	int32 DroppedMessageCount,
	const FEdenOsHttpResult* LatestResult)
{
	FEdenOsConnectionSnapshot Snapshot = PreviousSnapshot;
	Snapshot.bEnabled = Config.bEnabled;
	Snapshot.AuthorityMode = Config.AuthorityMode;
	Snapshot.bExternalCommandValidationEnabled = Config.bExternalCommandValidationEnabled;
	Snapshot.bExternalCommandExecutionEnabled = Config.bExternalCommandExecutionEnabled;
	Snapshot.bExternalCommandAutomationEnabled = Config.bExternalCommandAutomationEnabled;
	Snapshot.bHasBearerJwt = !Config.RuntimeBearerJwt.IsEmpty();
	Snapshot.PendingMessageCount = PendingMessageCount + (bHasInFlightRequest ? 1 : 0);
	Snapshot.DroppedMessageCount = DroppedMessageCount;

	if (!Config.bEnabled)
	{
		Snapshot.ConnectionState = EEdenOsConnectionState::Disabled;
		Snapshot.LastErrorSummary.Reset();
		return Snapshot;
	}

	if (!Validation.IsValid())
	{
		Snapshot.ConnectionState = EEdenOsConnectionState::Degraded;
		Snapshot.LastErrorSummary = Validation.GetFirstErrorOrEmpty();
		return Snapshot;
	}

	if (bHasInFlightRequest)
	{
		Snapshot.ConnectionState = EEdenOsConnectionState::Connecting;
		return Snapshot;
	}

	if (!LatestResult)
	{
		Snapshot.ConnectionState = EEdenOsConnectionState::Disconnected;
		Snapshot.LastErrorSummary.Reset();
		return Snapshot;
	}

	if (LatestResult->IsSuccess())
	{
		Snapshot.ConnectionState = EEdenOsConnectionState::Connected;
		Snapshot.LastErrorSummary.Reset();
		return Snapshot;
	}

	Snapshot.ConnectionState =
		PreviousSnapshot.ConnectionState == EEdenOsConnectionState::Connected
			? EEdenOsConnectionState::Degraded
			: EEdenOsConnectionState::Disconnected;
	Snapshot.LastErrorSummary = LatestResult->ErrorSummary.IsEmpty()
		? FString::Printf(TEXT("HTTP status %d"), LatestResult->HttpStatusCode)
		: LatestResult->ErrorSummary;
	return Snapshot;
}
