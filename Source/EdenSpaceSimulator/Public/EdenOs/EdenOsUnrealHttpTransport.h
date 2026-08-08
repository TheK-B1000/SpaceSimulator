// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdenOs/EdenOsTransport.h"

class EDENSPACESIMULATOR_API FEdenOsUnrealHttpTransport final : public IEdenOsHttpTransport
{
public:
	virtual bool SendAsync(const FEdenOsHttpRequestData& Request, FEdenOsHttpCompletion Completion) override;
};
