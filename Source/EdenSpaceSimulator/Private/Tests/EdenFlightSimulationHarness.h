// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Flight/EdenFlightMovementModel.h"
#include "Flight/EdenFlightTypes.h"
#include "Math/RandomStream.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Deterministic, world-free flight harness (ExecPlan 0008).
 *
 * Drives FEdenFlightMovementModel::StepFlight through the same fixed-step
 * accumulator the movement component uses, fed by synthetic render frames.
 * Collision is not modeled. Frame deltas are rounded through float, as the
 * engine passes DeltaTime to UEdenFlightMovementComponent::MoveWithCommand.
 */
namespace EdenFlightHarness
{
/** Command active from StartSeconds until the next segment starts. */
struct FScriptedSegment
{
	double StartSeconds = 0.0;
	FEdenFlightInputCommand Command;
};

enum class EInputTiming : uint8
{
	/** Command chosen per fixed step from that step's simulation time. Frame-rate independent by construction. */
	SimulationTime,

	/** Command chosen once per render frame from the frame's start time, as AEdenFlightPlayerController does. */
	FrameSampled
};

struct FFrameSource
{
	FString Name;
	double MeanHz = 60.0;

	/** 0 = fixed cadence. Otherwise each frame is MeanPeriod * (1 + Jitter * U(-1, 1)). */
	double JitterFraction = 0.0;
	int32 Seed = 0;

	static FFrameSource Fixed(double Hz)
	{
		FFrameSource Source;
		Source.Name = FString::Printf(TEXT("Fixed %.0f Hz"), Hz);
		Source.MeanHz = Hz;
		return Source;
	}

	static FFrameSource Jittered(double MeanHz, double JitterFraction, int32 Seed)
	{
		FFrameSource Source;
		Source.Name = FString::Printf(TEXT("Jittered %.0f Hz +/-%.0f%% seed %d"), MeanHz, JitterFraction * 100.0, Seed);
		Source.MeanHz = MeanHz;
		Source.JitterFraction = JitterFraction;
		Source.Seed = Seed;
		return Source;
	}
};

struct FBodyState
{
	FEdenFlightTranslationState Translation;
	FEdenFlightRotationState Rotation;

	/** Stored propellant when metered; the harness plays the fuel system and mirrors its ledger arithmetic. */
	double PropellantKilograms = 0.0;
};

struct FRunConfig
{
	FEdenFlightPropulsionSettings Propulsion;
	FEdenFlightAttitudeSettings Attitude;
	FEdenFlightStepSettings Step;
	TArray<FScriptedSegment> Script;
	FFrameSource Frames = FFrameSource::Fixed(60.0);
	EInputTiming Timing = EInputTiming::SimulationTime;
	FBodyState Initial;
	double DurationSeconds = 60.0;
	double ThrustAuthority = 1.0;
	double PowerThrustFraction = 1.0;
	bool bAssistAvailable = true;

	/** Meter propellant from Initial.PropellantKilograms (false = unconstrained, massless, as before Checkpoint D). */
	bool bPropellantMetered = false;
};

struct FRunResult
{
	FBodyState Final;
	int64 Steps = 0;
	int64 Frames = 0;
	int64 DroppedSteps = 0;
	double MaxFrameDeltaSeconds = 0.0;

	/** Largest per-step |delta v| observed, and the largest |F| / envelope violation (should be 0). */
	double MaxStepDeltaVelocityMetersPerSecond = 0.0;
	double MaxEnvelopeExcessNewtons = 0.0;
	double MaxTorqueEnvelopeExcessNewtonMeters = 0.0;
	double PropellantConsumedKilograms = 0.0;
	double MinPropellantKilograms = TNumericLimits<double>::Max();
	int64 PropellantLimitedSteps = 0;
};

inline double NextFrameDeltaSeconds(const FFrameSource& Source, FRandomStream& Random)
{
	const double MeanPeriod = 1.0 / Source.MeanHz;
	const double Jitter = Source.JitterFraction > 0.0 ? Source.JitterFraction * ((2.0 * Random.FRand()) - 1.0) : 0.0;
	// Mirror the engine: DeltaTime reaches MoveWithCommand as a float.
	return static_cast<double>(static_cast<float>(MeanPeriod * (1.0 + Jitter)));
}

inline int64 SecondsToStepIndex(double Seconds, double FixedStepSeconds)
{
	return static_cast<int64>(FMath::RoundToDouble(Seconds / FixedStepSeconds));
}

inline FEdenFlightInputCommand CommandForStep(const TArray<FScriptedSegment>& Script, int64 StepIndex, double FixedStepSeconds)
{
	FEdenFlightInputCommand Active;
	for (const FScriptedSegment& Segment : Script)
	{
		if (StepIndex >= SecondsToStepIndex(Segment.StartSeconds, FixedStepSeconds))
		{
			Active = Segment.Command;
		}
	}
	return Active;
}

inline FEdenFlightInputCommand CommandForTime(const TArray<FScriptedSegment>& Script, double TimeSeconds)
{
	FEdenFlightInputCommand Active;
	for (const FScriptedSegment& Segment : Script)
	{
		if (TimeSeconds + 1.0e-9 >= Segment.StartSeconds)
		{
			Active = Segment.Command;
		}
	}
	return Active;
}

inline double TorqueEnvelopeExcessNewtonMeters(const FVector& Torque, const FEdenFlightTorqueEnvelope& Envelope)
{
	double Excess = 0.0;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		Excess = FMath::Max(Excess, FMath::Abs(Torque[Axis]) - Envelope.MaxNewtonMeters[Axis]);
	}
	return Excess;
}

inline double EnvelopeExcessNewtons(const FVector& Force, const FEdenFlightThrustEnvelope& Envelope)
{
	double Excess = 0.0;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		Excess = FMath::Max(Excess, Force[Axis] - Envelope.PositiveNewtons[Axis]);
		Excess = FMath::Max(Excess, -Force[Axis] - Envelope.NegativeNewtons[Axis]);
	}
	return Excess;
}

/** Runs exactly DurationSeconds / FixedStep fixed steps, driven by render frames. */
inline FRunResult Run(const FRunConfig& Config)
{
	FRunResult Result;
	Result.Final = Config.Initial;

	const double FixedStep = Config.Step.GetFixedStepSeconds();
	const int64 TargetSteps = SecondsToStepIndex(Config.DurationSeconds, FixedStep);
	FRandomStream Random(Config.Frames.Seed);
	double Accumulator = 0.0;
	double FrameStartSeconds = 0.0;

	while (Result.Steps < TargetSteps)
	{
		const double FrameDelta = NextFrameDeltaSeconds(Config.Frames, Random);
		Result.MaxFrameDeltaSeconds = FMath::Max(Result.MaxFrameDeltaSeconds, FrameDelta);
		const FEdenFlightInputCommand FrameCommand = CommandForTime(Config.Script, FrameStartSeconds);

		int32 Dropped = 0;
		const int32 StepsThisFrame =
			FEdenFlightMovementModel::CalculateFixedSteps(FrameDelta, FixedStep, Config.Step.MaxStepsPerUpdate, Accumulator, Dropped);
		Result.DroppedSteps += Dropped;

		for (int32 StepIndex = 0; StepIndex < StepsThisFrame && Result.Steps < TargetSteps; ++StepIndex)
		{
			FEdenFlightStepInput Input;
			Input.Command = Config.Timing == EInputTiming::SimulationTime
				? CommandForStep(Config.Script, Result.Steps, FixedStep)
				: FrameCommand;
			Input.Translation = Result.Final.Translation;
			Input.Rotation = Result.Final.Rotation;
			Input.DryMassKilograms = Config.Propulsion.DryMassKilograms;
			Input.bPropellantMetered = Config.bPropellantMetered;
			Input.PropellantMassKilograms = Result.Final.PropellantKilograms;
			Input.PowerThrustFraction = Config.PowerThrustFraction;
			Input.ThrustAuthority = Config.ThrustAuthority;
			Input.bAssistAvailable = Config.bAssistAvailable;

			const FEdenFlightStepOutput Output =
				FEdenFlightMovementModel::StepFlight(Input, Config.Propulsion, Config.Attitude, FixedStep);

			Result.MaxStepDeltaVelocityMetersPerSecond = FMath::Max(
				Result.MaxStepDeltaVelocityMetersPerSecond,
				(Output.Translation.State.VelocityWorldMetersPerSecond - Input.Translation.VelocityWorldMetersPerSecond).Size());
			Result.MaxEnvelopeExcessNewtons =
				FMath::Max(Result.MaxEnvelopeExcessNewtons, EnvelopeExcessNewtons(Output.AppliedBodyForceNewtons, Output.Envelope));
			Result.MaxTorqueEnvelopeExcessNewtonMeters = FMath::Max(
				Result.MaxTorqueEnvelopeExcessNewtonMeters,
				TorqueEnvelopeExcessNewtonMeters(Output.AppliedBodyTorqueNewtonMeters, Output.TorqueEnvelope));

			Result.Final.Translation = Output.Translation.State;
			Result.Final.Rotation = Output.Rotation.State;
			if (Config.bPropellantMetered)
			{
				const double Stored = Result.Final.PropellantKilograms;
				const double Granted = Output.PropellantConsumedKilograms;
				Result.Final.PropellantKilograms = Granted >= Stored ? 0.0 : Stored - Granted;
				Result.PropellantConsumedKilograms += Granted;
				Result.MinPropellantKilograms = FMath::Min(Result.MinPropellantKilograms, Result.Final.PropellantKilograms);
				Result.PropellantLimitedSteps += Output.bPropellantLimited ? 1 : 0;
			}
			++Result.Steps;
		}

		FrameStartSeconds += FrameDelta;
		++Result.Frames;
	}

	return Result;
}

/**
 * Fixed steps the accumulator yields once rendered frame time first reaches DurationSeconds.
 * OutElapsedFrameSeconds is the frame time actually rendered (it overshoots by up to one frame).
 */
inline int64 CountStepsForFrameTime(
	const FFrameSource& Source,
	const FEdenFlightStepSettings& Step,
	double DurationSeconds,
	double& OutElapsedFrameSeconds,
	int64& OutDroppedSteps)
{
	FRandomStream Random(Source.Seed);
	double Accumulator = 0.0;
	OutElapsedFrameSeconds = 0.0;
	OutDroppedSteps = 0;
	int64 Steps = 0;
	while (OutElapsedFrameSeconds < DurationSeconds)
	{
		const double FrameDelta = NextFrameDeltaSeconds(Source, Random);
		int32 Dropped = 0;
		Steps += FEdenFlightMovementModel::CalculateFixedSteps(FrameDelta, Step.GetFixedStepSeconds(), Step.MaxStepsPerUpdate, Accumulator, Dropped);
		OutDroppedSteps += Dropped;
		OutElapsedFrameSeconds += FrameDelta;
	}
	return Steps;
}

inline TArray<FFrameSource> StandardFrameSources()
{
	return {
		FFrameSource::Fixed(30.0),
		FFrameSource::Fixed(60.0),
		FFrameSource::Fixed(144.0),
		FFrameSource::Fixed(240.0),
		FFrameSource::Jittered(90.0, 0.6, 1234),
		FFrameSource::Jittered(45.0, 0.9, 98765)
	};
}
}

#endif
