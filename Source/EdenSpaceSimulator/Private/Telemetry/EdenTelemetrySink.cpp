// Copyright Epic Games, Inc. All Rights Reserved.

#include "Telemetry/EdenTelemetrySink.h"

#include "Core/EdenLogCategories.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Telemetry/EdenTelemetryExportModel.h"

FEdenTelemetrySessionPayload::FEdenTelemetrySessionPayload(
	TConstArrayView<FEdenTelemetryEvent> InEvents,
	TConstArrayView<FEdenTelemetrySnapshot> InSnapshots,
	const FEdenTelemetrySessionMetadata& InMetadata,
	FString InSessionId,
	FName InMissionId)
	: Events(InEvents)
	, Snapshots(InSnapshots)
	, Metadata(InMetadata)
	, SessionId(MoveTemp(InSessionId))
	, MissionId(InMissionId)
{
}

FString FEdenTelemetrySessionPayload::GetSafeSessionId() const
{
	return SessionId.IsEmpty() ? TEXT("unknown-session") : SessionId;
}

FEdenTelemetrySinkResult FEdenTelemetrySinkResult::Succeeded(FString InDestination)
{
	FEdenTelemetrySinkResult Result;
	Result.Result = EEdenSinkResult::Success;
	Result.Destination = MoveTemp(InDestination);
	return Result;
}

FEdenTelemetrySinkResult FEdenTelemetrySinkResult::Failed(FString InErrorMessage)
{
	FEdenTelemetrySinkResult Result;
	Result.Result = EEdenSinkResult::Failed;
	Result.ErrorMessage = MoveTemp(InErrorMessage);
	return Result;
}

bool FEdenTelemetrySinkResult::IsSuccess() const
{
	return Result == EEdenSinkResult::Success;
}

bool FEdenTelemetrySinkDeliverySummary::WasAttempted() const
{
	return AttemptedCount > 0;
}

bool FEdenTelemetrySinkDeliverySummary::WasFullySuccessful() const
{
	return AttemptedCount > 0 && FailedCount == 0 && SucceededCount == AttemptedCount;
}

FEdenLocalJsonTelemetrySink::FEdenLocalJsonTelemetrySink(FString InOutputDirectory)
	: OutputDirectory(MoveTemp(InOutputDirectory))
{
}

FName FEdenLocalJsonTelemetrySink::GetTelemetrySinkName() const
{
	static const FName SinkName(TEXT("LocalJsonTelemetry"));
	return SinkName;
}

FEdenTelemetrySinkResult FEdenLocalJsonTelemetrySink::DeliverTelemetrySession(
	const FEdenTelemetrySessionPayload& Payload)
{
	const FString Directory = ResolveOutputDirectory();
	IFileManager::Get().MakeDirectory(*Directory, true);
	if (!IFileManager::Get().DirectoryExists(*Directory))
	{
		const FString Message = FString::Printf(TEXT("Telemetry sink could not create directory '%s'."), *Directory);
		UE_LOG(LogEdenTelemetry, Error, TEXT("%s"), *Message);
		return FEdenTelemetrySinkResult::Failed(Message);
	}

	const FString Json = FEdenTelemetryExportModel::BuildSessionJsonV1(Payload);
	const FString Filename = FString::Printf(TEXT("telemetry_%s.json"), *Payload.GetSafeSessionId());
	const FString AbsolutePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, Filename));

	if (!FFileHelper::SaveStringToFile(Json, *AbsolutePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		const FString Message = FString::Printf(TEXT("Failed to write telemetry export to '%s'."), *AbsolutePath);
		UE_LOG(LogEdenTelemetry, Error, TEXT("%s"), *Message);
		return FEdenTelemetrySinkResult::Failed(Message);
	}

	UE_LOG(LogEdenTelemetry, Log, TEXT("Wrote telemetry export '%s'."), *AbsolutePath);
	return FEdenTelemetrySinkResult::Succeeded(AbsolutePath);
}

FString FEdenLocalJsonTelemetrySink::ResolveOutputDirectory() const
{
	if (!OutputDirectory.IsEmpty())
	{
		return FPaths::ConvertRelativePathToFull(OutputDirectory);
	}

	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Telemetry")));
}
