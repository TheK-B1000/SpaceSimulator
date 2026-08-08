// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Telemetry/EdenTelemetrySink.h"

class UEdenOsAdapterSubsystem;

class EDENSPACESIMULATOR_API FEdenOsTelemetrySink final : public IEdenTelemetrySink
{
public:
	explicit FEdenOsTelemetrySink(UEdenOsAdapterSubsystem& InAdapterSubsystem);

	virtual FName GetTelemetrySinkName() const override;
	virtual FEdenTelemetrySinkResult DeliverTelemetrySession(const FEdenTelemetrySessionPayload& Payload) override;

private:
	TWeakObjectPtr<UEdenOsAdapterSubsystem> AdapterSubsystem;
};
