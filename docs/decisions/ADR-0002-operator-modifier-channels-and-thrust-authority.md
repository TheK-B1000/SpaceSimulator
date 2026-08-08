# ADR-0002: Operator Modifier Channels and Thrust Authority

## Status

Accepted

## Date

2026-08-08

## Context

ExecPlan 0005 introduces operator commands that must change power demand, thermal dissipation, and thrust capability without fighting mission `External*` disturbance channels or Data Asset baselines. Two design questions were open: whether load shedding may reduce thermal dissipation, and who owns thrust authority.

## Decision

### Additive operator channels on resource owners

```text
EffectiveDemand      = max(0, BaselineDemand + MissionExternalDemand + OperatorDemand)
EffectiveHeatGen     = BaselineHeatGen + MissionExternalHeating
EffectiveDissipation = max(0, BaselineDissipation + OperatorDissipation)
```

- Mission continues to write only `ExternalDemand` / `ExternalHeating`.
- Operator writes only `OperatorDemandKilowatts` / `OperatorDissipationDegreesCelsiusPerSecond`.
- Resource components remain the sole owners of applied values and resulting state.
- Operator demand and dissipation modifiers may be negative (load shed) or positive (cooling bus). Totals clamp at zero.

### Load-shed ↔ cooling coupling lives in the operator trade-off table only

`FEdenOperatorControlModel` resolves discrete modes into net operator modifiers. Power and thermal never call each other. Shed reduces demand and dissipation; Boost/Emergency increase dissipation and cooling demand.

### Thrust authority owned by flight movement

`UEdenFlightMovementComponent` owns `ThrustAuthority` in `[0,1]` and `bStabilizationAssistAvailable`. Operator control commands those values. Movement scales translation acceleration and `GetPropulsionDemandNormalized()` by thrust authority, and forces stabilization off when assist is unavailable. Fuel continues to read demand only through `IEdenPropulsionDemandSource`.

### HUD and alerts

- Production HUD assembles `FEdenOperatorHudSnapshot` at 10 Hz; widgets perform no domain math.
- `UEdenAlertSubsystem` owns active alerts (cap 8); transition-only raise/clear.

## Alternatives considered

### Operator writes baseline SetDissipation / SetBaselineDemand

Rejected: fights mission reset, destroys Data Asset baselines, erases operator contribution for telemetry.

### Thrust authority on fuel as a consumption multiplier

Rejected: fuel would own flight capability; movement is the correct propulsion-demand owner.

### No load-shed thermal penalty

Rejected: makes Shed a free win and removes the emergency dilemma.

## Consequences

### Positive

- Clear channel ownership matching mission External* pattern.
- Real operator trade-offs without cross-resource coupling.
- 0006 can observe operator intent and applied modifiers without schema collision.

### Negative

- Touches stable flight movement since `v0.1.0-flight-shell` (scalar + assist gate only).
- Snapshot/MakeSnapshot signatures expand; callers must pass operator channels.

### Risks and mitigations

- Negative operator modifiers overshooting baselines → clamp effective totals to >= 0.
- PIE reset must clear operator channel separately from mission External*.

## Validation

- Unit tests: modifier additivity, shed + boost net resolution, thrust authority scales demand.
- Integration: mission External* and operator modifiers coexist; mission reset leaves operator intact.
- Scenario: passive fail / competent succeed / over-cooling drain battery.

## Supersedes / Superseded by

None.
