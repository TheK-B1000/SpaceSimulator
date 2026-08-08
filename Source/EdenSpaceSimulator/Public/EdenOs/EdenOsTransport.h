// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdenOs/EdenOsTypes.h"

namespace EdenOsTransport
{
	inline constexpr int32 MaxErrorSummaryCharacters = 256;
}

enum class EEdenOsOutboundMessageType : uint8
{
	SessionCreate,
	Telemetry,
	Event,
	SessionComplete,
	Advisory,
	CommandProposal
};

struct EDENSPACESIMULATOR_API FEdenOsQueuedRequest
{
	EEdenOsOutboundMessageType MessageType = EEdenOsOutboundMessageType::Telemetry;
	FString RoutePath;
	FString BodyJson;
	int64 SequenceNumber = 0;
};

struct EDENSPACESIMULATOR_API FEdenOsDeliveryRecord
{
	EEdenOsOutboundMessageType MessageType = EEdenOsOutboundMessageType::Telemetry;
	FString RoutePath;
	int64 SequenceNumber = 0;
	int32 HttpStatusCode = 0;
	bool bSucceeded = false;
	FString ErrorSummary;
	FString ResponseBodyJson;
};

struct EDENSPACESIMULATOR_API FEdenOsHttpRequestData
{
	FString Url;
	FString Verb = TEXT("POST");
	FString BodyJson;
	FString AuthorizationBearerJwt;
	float TimeoutSeconds = 5.0f;
};

struct EDENSPACESIMULATOR_API FEdenOsHttpResult
{
	static FEdenOsHttpResult Succeeded(int32 InHttpStatusCode, FString InResponseBodyJson = FString());
	static FEdenOsHttpResult Failed(
		int32 InHttpStatusCode,
		FString InErrorSummary,
		FString InResponseBodyJson = FString());

	bool IsSuccess() const;

	bool bCompleted = true;
	int32 HttpStatusCode = 0;
	FString ErrorSummary;
	FString ResponseBodyJson;
};

DECLARE_DELEGATE_OneParam(FEdenOsHttpCompletion, FEdenOsHttpResult);

class EDENSPACESIMULATOR_API IEdenOsHttpTransport
{
public:
	virtual ~IEdenOsHttpTransport() = default;

	virtual bool SendAsync(const FEdenOsHttpRequestData& Request, FEdenOsHttpCompletion Completion) = 0;
};

struct EDENSPACESIMULATOR_API FEdenOsUrlModel
{
	static FString JoinBaseUrlAndRoute(const FString& BaseUrl, const FString& RoutePath);
	static FString BuildSessionRoute(const FString& RouteTemplate, const FString& SessionId);
};

struct EDENSPACESIMULATOR_API FEdenOsTransportModel
{
	static bool IsSuccessfulHttpStatus(int32 StatusCode);
	static FString SanitizeErrorSummary(const FString& ErrorSummary);
	static FEdenOsConnectionSnapshot MakeSnapshotForOutcome(
		const FEdenOsConnectionSnapshot& PreviousSnapshot,
		const FEdenOsConnectionConfig& Config,
		const FEdenOsValidationResult& Validation,
		bool bHasInFlightRequest,
		int32 PendingMessageCount,
		int32 DroppedMessageCount,
		const FEdenOsHttpResult* LatestResult);
};
