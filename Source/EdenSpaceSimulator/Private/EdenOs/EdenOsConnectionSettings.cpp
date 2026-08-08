// Copyright Epic Games, Inc. All Rights Reserved.

#include "EdenOs/EdenOsConnectionSettings.h"

namespace
{
	bool IsPositiveFinite(float Value)
	{
		return FMath::IsFinite(Value) && Value > 0.0f;
	}

	FString GetEnumName(EEdenOsAuthorityMode AuthorityMode)
	{
		const UEnum* Enum = StaticEnum<EEdenOsAuthorityMode>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(AuthorityMode)) : TEXT("Unknown");
	}

	FString GetEnumName(EEdenOsConnectionState ConnectionState)
	{
		const UEnum* Enum = StaticEnum<EEdenOsConnectionState>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(ConnectionState)) : TEXT("Unknown");
	}

	void ValidateBaseUrl(const FString& BaseUrl, FEdenOsValidationResult& Result)
	{
		const FString Trimmed = BaseUrl.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			Result.AddError(TEXT("EDEN OS BaseUrl is required when the connection is enabled."));
			return;
		}

		const FString SchemeSeparator = TEXT("://");
		int32 SeparatorIndex = INDEX_NONE;
		if (!Trimmed.FindChar(TEXT(':'), SeparatorIndex) || !Trimmed.Mid(SeparatorIndex).StartsWith(SchemeSeparator))
		{
			Result.AddError(TEXT("EDEN OS BaseUrl must include an http:// or https:// scheme."));
			return;
		}

		const FString Scheme = Trimmed.Left(SeparatorIndex).ToLower();
		if (Scheme != TEXT("http") && Scheme != TEXT("https"))
		{
			Result.AddError(TEXT("EDEN OS BaseUrl scheme must be http or https."));
			return;
		}

		FString AuthorityAndPath = Trimmed.Mid(SeparatorIndex + SchemeSeparator.Len());
		int32 PathIndex = INDEX_NONE;
		if (AuthorityAndPath.FindChar(TEXT('/'), PathIndex))
		{
			AuthorityAndPath = AuthorityAndPath.Left(PathIndex);
		}

		AuthorityAndPath.TrimStartAndEndInline();
		if (AuthorityAndPath.IsEmpty())
		{
			Result.AddError(TEXT("EDEN OS BaseUrl must include a host."));
		}
	}
}

FEdenOsConnectionConfig UEdenOsConnectionSettings::MakeConnectionConfig() const
{
	FEdenOsConnectionConfig Config;
	Config.bEnabled = bEnabled;
	Config.BaseUrl = BaseUrl;
	Config.ConnectionTimeoutSeconds = ConnectionTimeoutSeconds;
	Config.RequestTimeoutSeconds = RequestTimeoutSeconds;
	Config.MaxQueueDepth = MaxQueueDepth;
	Config.AdvisoryHeartbeatSimulationSeconds = AdvisoryHeartbeatSimulationSeconds;
	Config.AuthorityMode = AuthorityMode;
	return Config;
}

FEdenOsValidationResult FEdenOsConnectionConfigModel::Validate(const FEdenOsConnectionConfig& Config)
{
	FEdenOsValidationResult Result;

	if (!IsPositiveFinite(Config.ConnectionTimeoutSeconds))
	{
		Result.AddError(TEXT("EDEN OS ConnectionTimeoutSeconds must be positive and finite."));
	}
	if (!IsPositiveFinite(Config.RequestTimeoutSeconds))
	{
		Result.AddError(TEXT("EDEN OS RequestTimeoutSeconds must be positive and finite."));
	}
	if (Config.MaxQueueDepth <= 0)
	{
		Result.AddError(TEXT("EDEN OS MaxQueueDepth must be greater than zero."));
	}
	if (!IsPositiveFinite(Config.AdvisoryHeartbeatSimulationSeconds))
	{
		Result.AddError(TEXT("EDEN OS AdvisoryHeartbeatSimulationSeconds must be positive and finite."));
	}
	if (Config.AuthorityMode == EEdenOsAuthorityMode::AuthorizedControl)
	{
		Result.AddWarning(TEXT("AuthorizedControl is defined for the contract but remains disabled by default in 0007."));
	}

	if (Config.bEnabled)
	{
		ValidateBaseUrl(Config.BaseUrl, Result);
		if (Config.RuntimeBearerJwt.IsEmpty())
		{
			Result.AddWarning(TEXT("Bearer JWT absent; authenticated EDEN OS calls require runtime token injection."));
		}
	}

	return Result;
}

FEdenOsConnectionSnapshot FEdenOsConnectionConfigModel::MakeInitialSnapshot(
	const FEdenOsConnectionConfig& Config,
	const FEdenOsValidationResult& Validation)
{
	FEdenOsConnectionSnapshot Snapshot;
	Snapshot.bEnabled = Config.bEnabled;
	Snapshot.AuthorityMode = Config.AuthorityMode;
	Snapshot.bHasBearerJwt = !Config.RuntimeBearerJwt.IsEmpty();
	Snapshot.LastErrorSummary = Validation.GetFirstErrorOrEmpty();

	if (!Config.bEnabled)
	{
		Snapshot.ConnectionState = EEdenOsConnectionState::Disabled;
	}
	else if (!Validation.IsValid())
	{
		Snapshot.ConnectionState = EEdenOsConnectionState::Degraded;
	}
	else
	{
		Snapshot.ConnectionState = EEdenOsConnectionState::Disconnected;
	}

	return Snapshot;
}

FString FEdenOsConnectionConfigModel::DescribeForLog(
	const FEdenOsConnectionConfig& Config,
	const FEdenOsConnectionSnapshot& Snapshot,
	const FEdenOsValidationResult& Validation)
{
	return FString::Printf(
		TEXT("Enabled=%s BaseUrl=%s ConnectionTimeoutSeconds=%.3f RequestTimeoutSeconds=%.3f MaxQueueDepth=%d AdvisoryHeartbeatSimulationSeconds=%.3f AuthorityMode=%s BearerJwt=%s ConnectionState=%s ErrorCount=%d WarningCount=%d"),
		Config.bEnabled ? TEXT("true") : TEXT("false"),
		Config.BaseUrl.IsEmpty() ? TEXT("empty") : TEXT("set"),
		Config.ConnectionTimeoutSeconds,
		Config.RequestTimeoutSeconds,
		Config.MaxQueueDepth,
		Config.AdvisoryHeartbeatSimulationSeconds,
		*GetEnumName(Config.AuthorityMode),
		Config.RuntimeBearerJwt.IsEmpty() ? TEXT("absent") : TEXT("set"),
		*GetEnumName(Snapshot.ConnectionState),
		Validation.Errors.Num(),
		Validation.Warnings.Num());
}
