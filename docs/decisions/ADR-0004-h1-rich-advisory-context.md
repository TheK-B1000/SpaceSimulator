# ADR-0004: H.1 Rich Advisory Context and Snapshot Source Availability

## Status

Proposed (ExecPlan 0010; awaiting human PIE)

## Date

2026-10-14

## Context

The EDEN OS advisory request (ExecPlan 0007, H.1) is a flat `context` object built from the newest telemetry snapshot. ProjectEden needs the thermal limits, heat rates, operator modes and power balance to explain a thermal situation without guessing. Two facts constrain how those values can be sent:

- `UEdenTelemetrySubsystem::AssembleSnapshot` leaves default values (0.0, `Normal`, `Full`) when an owner is unbound or not simulating. Those defaults look like observations.
- Thermal limits live in the active `FEdenThermalConfig` and were not part of any snapshot, so reading them at serialization time would not describe the step being reported.

## Decision

1. Each `FEdenTelemetrySnapshot` records `Sources` (`bThermal`, `bPower`, `bOperator`): the owner was bound and its simulation or control was enabled at that step.
2. Each snapshot captures `ThermalLimits` (ambient, warning, critical, absolute max) by value when thermal is available. `bIsValid` is false when the config fails `FEdenThermalModel::ValidateConfig`.
3. `FEdenOsAdvisoryContext` copies `Sources` and `ThermalLimits` from the same newest snapshot as `Thermal`, `Power` and `Operator`. It derives `WindowDurationSeconds` from the bounded snapshot window.
4. `BuildAdvisoryJsonV1` appends 14 optional keys after the legacy keys. Each is written only when its source is available and its value is finite. Absence means unknown, and an observed zero is sent. With no available source, the body is byte-identical to the pre-ADR body.
5. The schema version stays 1. The keys are additive, and ProjectEden accepts any finite context field.

## Alternatives considered

- Read live configuration while serializing: rejected because it misreports thresholds that changed after the snapshot.
- Infer availability from non-default values: rejected because it confuses observed zero with unknown.
- Version the H.1 schema: rejected because older and newer clients already interoperate on additive optional fields.

## Consequences

- Telemetry Export Schema v1 is unchanged; the exporter writes fields explicitly.
- Every snapshot is larger by three booleans and one small struct. No allocation is added on the fixed-step path, except error strings when a thermal config is invalid.
- ProjectEden can compute thermal and power balance and see active modes. Its advisory policy must respect ADR-0002: load shedding reduces cooling, and propulsion priority has no thermal effect.
- Operator modes report intent. During the startup deferral in `UEdenOperatorControlComponent`, they can lead the applied rates by one step.

## Validation

`Eden.Unit.EdenOs.RichContext.*` (ExecPlan 0010) and the existing `Eden.` suite. Human PIE remains required.

## Supersedes / Superseded by

None.
