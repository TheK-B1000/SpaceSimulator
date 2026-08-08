// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Telemetry/EdenTelemetryTypes.h"

/**
 * Pure Telemetry Export Schema v1 builder.
 * Owns the wire contract for future EDEN OS (0007). Does not serialize Unreal structs blindly.
 */
struct EDENSPACESIMULATOR_API FEdenTelemetryExportModel
{
	static FString BuildSessionJsonV1(
		const TArray<FEdenTelemetryEvent>& Events,
		const TArray<FEdenTelemetrySnapshot>& Snapshots,
		const FEdenTelemetrySessionMetadata& Metadata,
		const FString& SessionId,
		const FName MissionId);

	static FString EscapeJsonString(const FString& Value);
	static FString EnumToken(const UEnum* Enum, int64 Value);

	template <typename TEnum>
	static FString EnumToken(TEnum Value)
	{
		return EnumToken(StaticEnum<TEnum>(), static_cast<int64>(Value));
	}
};
