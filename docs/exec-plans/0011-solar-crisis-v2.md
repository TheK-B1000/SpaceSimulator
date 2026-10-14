# 0011 Solar Crisis v2 — stable baseline, reproducible start, genuine recovery

## Status

**Revision 2 approved for implementation, 2026-10-14 (E1 with 6 kW discharge; auxiliary curtailment option (a), reporting only; flare D-a).** In progress: Checkpoints A-D done. Solar Crisis v1 (ExecPlan 0004) stays the approved, shipped scenario, with its balance and success criteria unchanged, until this plan is approved and implemented.

Revision 2 applies the reviewer's decisions: thermal baseline A2, 20 °C initialization, and meaningful electrical constraints. It replaces revision 1's infeasible numbers, which were computed for A1; under A2, immediate Boost reached exactly 100 °C. Every trajectory below comes from a step-by-step replica of `FEdenThermalModel::Step` (§Validation), and the implementation must reproduce them in automated tests.

Prerequisite on `feature/0011-thermal-diagnostics`: the effective thermal balance (`FEdenThermalModel::ComputeThermalBalance`), which the recovery objective reuses.

## Problem and outcome

The PIE audit of v1 (2026-10-14) found that it behaves as designed, but the design has three weaknesses:

1. **No reproducible start.** The ship heats from PIE start, not from `StartMission`. An 8.5 s delay left 0.7 °C of margin; about 19 s fails the mission.
2. **Unstable idle ship.** Baseline heat 1.0 °C/s against dissipation 0.5 °C/s overheats an idle Nominal ship in about 200 s.
3. **"Success" is a timer.** v1 succeeds at T = 50 s if the temperature never exceeded 100 °C. The ship overheated about 70 s after success in PIE.

When v2 is complete:

- A fresh Solar Crisis starts at a declared 20 °C, however long the player waited. Battery, fuel and other systems keep their state.
- In Nominal mode an idle, powered ship is thermally stable. Off mode keeps passive cooling only and slowly overheats.
- Active cooling is an electrical load. It is served from the power bus and **delivers less cooling when it cannot be fully powered**.
- Success requires demonstrated recovery: flare over, below Warning, stable or falling for 5 s, battery and propellant above limits, within a bounded window. Otherwise the mission fails with a stated reason.
- A passive player fails, a timely response succeeds, and the margins are large enough to be robust and testable.

## Scope

### In scope

- Thermal baseline A2 and power-limited active cooling (E1).
- Mission-declared initial conditions (thermal only), applied on a fresh start.
- New objective types: thermal recovery (hold time, deadline) and a fail-only battery floor.
- `DA_SolarCrisisV2` with the calibrated timeline; v1 is kept for regression with a pinned configuration.
- HUD/AAR reporting of recovery progress, cooling authority, curtailed load and failure reasons.

### Out of scope

- EDEN live advisory work (separate track) and any EDEN control authority. The deterministic EDEN safety policy is unchanged: load shed and reduced propulsion are never thermal remedies.
- Orbital missions. "Start from current conditions" is designed here and covered by tests only.
- Flight and propulsion rebalancing, damage models, radiator geometry.
- Telemetry Export v1 / EDEN wire schema changes. New fields (cooling authority, curtailed load) are in-process only until a separate contract review.

## Current repository state

- `UEdenMissionSubsystem` owns lifecycle, time, objectives and event dispatch. `StartMission` only transitions Ready → Running. Terminal states stop mission time; resources keep simulating.
- Commands available to missions: phase, external heating set/clear, external power demand set/clear, `SetPowerGeneration` (unused by v1), `ActivateObjective`. Under 0004, missions must not use direct temperature or battery setters.
- Objective types: `SurviveUntilTime`, `KeepTemperatureBelow` (fail-only), `RestorePowerAbove`, `MaintainFuelAbove` (fail-only).
- Thermal: constant dissipation toward ambient (never past it), clamped to [−50, 120] °C. Operator cooling today: Off and Nominal +0, Boost +1.0 °C/s (1.5 kW), Emergency +2.0 °C/s (4.5 kW). Load shed −0.4 °C/s and −2 kW, and disables Flight Assist.
- **Cooling is not power-limited today.** The operator cooling term is applied regardless of battery state. The power system tracks energy (generation 2 kW, baseline 1 kW, 20 kWh) but never curtails a load, even when depleted.
- **Battery energy cannot be the constraint in a 90–100 s scenario.** The heaviest v2 strategy (Emergency plus the 5 kW auxiliary load for 20 s) uses about 0.05 kWh, 0.25 % of 20 kWh. Making energy matter would mean shrinking the battery or draining it, which the reviewer rejected as arbitrary. The physically meaningful constraint over seconds is **power** (kW available now), not energy.

## Architecture alignment

| New mutable state | Authoritative owner |
| --- | --- |
| Served power per load class, curtailed kW, cooling authority fraction | `UEdenPowerSystemComponent` (bus allocation each 10 Hz step) |
| Delivered active cooling (requested × authority) | `UEdenThermalSystemComponent`, from the power system's authority (same coupling pattern as propulsion power tiers) |
| Initial-condition record for the run | `UEdenMissionSubsystem` runtime state (reset by `ResetMission`) |
| Recovery hold timer and deadline result | `UEdenMissionSubsystem` objective runtime |
| Thermal/power configuration values | `DA_EdenThermalConfig`, `DA_EdenPowerConfig`, `DA_EdenOperatorControlConfig` (data) |

- Missions still change resources only through named, logged, validated commands. The one new permission is an **initial-condition** command, valid only at a fresh Ready → Running transition of a definition that declares it. It is never a timeline event.
- Fuel and battery keep their reset semantics. Operator modes are not touched by the mission.
- Power allocation is deterministic and ordered; the same inputs always give the same allocation.

## Alternatives considered

- **A1 (base dissipation 1.0, Off = radiators off).** Rejected by the reviewer in favour of A2: passive radiation should continue when active control is off.
- **Global thermal reset in `ResetMission`.** Rejected: it silently repairs every ship.
- **Make battery energy matter by shrinking or pre-draining the battery.** Rejected as arbitrary; see Current repository state.
- **Auxiliary load prioritized over cooling (E1-alt).** Simulated: Emergency is then starved to 44 % authority during the flare and still succeeds (peak 96.2 °C); Boost is unaffected. It is kept as a configurable ordering, but not recommended, because it makes Emergency silently weaker with no player-visible decision.
- **Longer, weaker flare (30 s at about 2.9 °C/s).** Widens the separation between Boost and passive to 15 °C; see §D option D-b.

## Milestones

| Checkpoint | Deliverable |
| --- | --- |
| A | A2 thermal baseline and Off/Nominal semantics; v1 regression pinned to its original configuration |
| B | Power bus allocation (E1) and power-limited active cooling; existing power/thermal tests unchanged with the bus unlimited |
| C | Mission initial conditions (thermal), with early/late activation tests |
| D | `RecoverThermalStability` and `MaintainPowerAbove` objectives with hold and deadline semantics |
| E | `DA_SolarCrisisV2`; strategy-matrix tests reproducing §D trajectories; HUD/AAR reporting; PIE checklist |

## Detailed steps

### A. Thermal baseline A2 (approved)

| Mode | Operator cooling | Total cooling | Net, no external heat | Cooling power |
| --- | --- | --- | --- | --- |
| Off | 0.0 | 0.5 °C/s | +0.5 °C/s | 0 kW |
| Nominal | +0.5 | 1.0 °C/s | 0.0 °C/s | **0.5 kW (new, configurable)** |
| Boost | +1.0 (replaces Nominal) | 1.5 °C/s | −0.5 °C/s | 1.5 kW |
| Emergency | +2.0 (replaces Nominal) | 2.5 °C/s | −1.5 °C/s | 4.5 kW |

- Passive base dissipation stays at 0.5 °C/s. `FEdenOperatorControlConfig` gains `NominalDissipationDegreesCelsiusPerSecond` (0.5) and `NominalCoolingDemandKilowatts` (0.5). The existing Boost and Emergency values are unchanged and replace, not add to, Nominal.
- Load shed keeps v1 semantics (−0.4 °C/s cooling, −2 kW): Nominal plus shed gives net +0.4 °C/s.
- Idle power under A2: 2 − (1 + 0.5) = +0.5 kW, so the battery charges in Nominal.
- All cooling still respects the ambient floor.
- v1 regression tests pin the original values (Nominal +0, 0 kW), so v1 results are reproducible.

### B. Power-limited cooling (E1, recommended)

- New `FEdenPowerConfig` field `BatteryMaxDischargeKilowatts`. Default 0 means unlimited, which keeps every existing behaviour and test. v2 uses **6 kW**.
- Bus capacity = generation + (battery charged ? max discharge : 0). With battery: 2 + 6 = 8 kW. Empty: 2 kW.
- Allocation each power step, in priority order:
  1. essential baseline (1 kW);
  2. thermal control (active cooling);
  3. auxiliary / mission external demand;
  4. battery charging.

  Load shed removes 2 kW of auxiliary demand before allocation.
- **Cooling authority** = served cooling kW ÷ requested cooling kW. The thermal system delivers operator cooling × authority. Passive cooling never depends on power.
- The power snapshot reports served and curtailed kW per class. The HUD shows `COOLING AUTH` and `AUX CURTAILED`.
- What this makes meaningful during the flare (8 kW capacity, 1 kW essential, 5 kW auxiliary):
  - Boost: 1 + 1.5 + 5 = 7.5 kW, which fits, so full cooling.
  - Emergency: 1 + 4.5 + 5 = 10.5 kW. Cooling is served first, and **2.5 kW of auxiliary load is curtailed**. Emergency plus load shed curtails only 0.5 kW.
  - Battery empty: only 2 kW. Boost runs at 67 % authority, Emergency at 22 %, and both strategies fail (simulated). Battery reserve therefore matters once it is gone, not by being drained.
- **Decision for review:** is curtailed auxiliary load (a) reported only (HUD, AAR, telemetry), or (b) a fail-only objective (for example auxiliary power served ≥ 60 % throughout)? With (b), Emergency alone fails it (2.5 of 5 kW served, 50 %), and Emergency plus load shed passes (2.5 of the remaining 3 kW, 83 %). That is a genuine thermal-versus-electrical decision. Load shed is then used for *electrical* relief while it still costs 0.4 °C/s of cooling, which matches the EDEN policy. **Recommend (a) for v2.0** and (b) once the auxiliary load has a named function and consequence.

### C. Mission initial conditions (approved)

- `FEdenMissionDefinition.InitialConditions` with `bSetInitialTemperature` and `InitialTemperatureCelsius` (default off, so existing missions are unchanged).
- On a fresh Ready → Running start (including `RestartMission`), one logged command sets the thermal target to 20 °C and clears any leftover mission external heating and demand. If the target is missing or the value is rejected, the start fails visibly.
- Battery, fuel, operator modes and flight state are untouched. Missions without declared conditions start from the current state.

### D. Recovery objectives and `DA_SolarCrisisV2`

- `RecoverThermalStability` (complete-seeking). It is satisfied on a mission step if:
  - mission external heating is 0;
  - temperature < `TargetValue` (default Warning, 70 °C);
  - the realized net rate from the thermal balance is ≤ +0.01 °C/s.

  It completes after `HoldSeconds` (5) of continuous satisfaction; any break resets the timer. It fails at `DeadlineSeconds`. It is activated at Recovery.
- `MaintainPowerAbove` (fail-only): battery charge fraction ≥ `TargetValue` (0.10).
- Recommended timeline (**D-a**):
  - **T = 0 s:** 20 °C.
  - **T = 5 s:** Warning.
  - **T = 10–30 s:** flare at **4.25 °C/s** and 5 kW auxiliary.
  - **T = 30 s:** Recovery; recovery objective active.
  - **T = 100 s:** deadline (was 90 s).
- Objectives:
  - `KeepTemperatureBelow` 100 °C;
  - `MaintainFuelAbove` 0.20;
  - `MaintainPowerAbove` 0.10;
  - `RecoverThermalStability` (< 70 °C, 5 s, deadline T 100 s).

**Why these values.** Boost cools only 0.5 °C/s more than Nominal, so over a 20 s flare the two differ by exactly 10 °C at the peak. No flare value can give both a passive failure and a Boost success more than about 5 °C from the 100 °C line. 4.25 °C/s centres that window: passive would peak at 105 °C, Boost peaks at 95 °C. The 90 s deadline left immediate Boost only 5.1 s to complete recovery, so the deadline moves to 100 s (15.1 s).

**Simulated trajectories, D-a (4.25 °C/s, deadline T 100 s, A2, E1, 6 kW discharge):**

| Strategy | Outcome | When | Peak | Margin to 100 °C | Margin to deadline | Power |
| --- | --- | --- | --- | --- | --- | --- |
| Passive (Nominal) | **Fail**, overheat | T 28.9 s | would reach 105.0 °C | −5.0 °C | — | — |
| Immediate Boost (T 10) | **Succeed** | T 84.9 s | 95.0 °C | +5.0 °C | 15.1 s | full authority |
| Immediate Emergency (T 10) | **Succeed** | T 38.3 s | 75.0 °C | +25.0 °C | 61.7 s | 2.5 kW aux curtailed |
| Boost at T 15 | Succeed | T 89.9 s | 97.5 °C | +2.5 °C | 10.1 s | — |
| Boost at T 18 | Succeed | T 92.9 s | 99.0 °C | +1.0 °C | 7.1 s | — |
| Latest Boost that succeeds | T 20.0 | — | — | — | — | — |
| Boost at T 21 | **Fail**, overheat | T 29.9 s | — | — | — | — |
| Latest Emergency that succeeds | T 26.6 | — | — | — | — | — |
| Load shed only (T 10) | **Fail**, overheat | T 27.3 s | would reach 113 °C | −13 °C | — | — |
| Boost in flare, Nominal in recovery | **Fail**, deadline | T 100 s | 95.0 °C, flat | +5.0 °C | 0 (still 95 °C) | — |
| Boost in flare, Off in recovery | **Fail**, overheat | T 40.1 s | — | — | — | — |
| Boost in flare, Emergency in recovery | Succeed | T 51.6 s | 95.0 °C | +5.0 °C | 48.4 s | — |
| Emergency + load shed (T 10) | Succeed | T 46.8 s | 83.0 °C | +17.0 °C | 53.2 s | 0.5 kW aux curtailed |
| Boost or Emergency with an empty battery | **Fail**, overheat | T ≈ 29–30 s | — | — | — | authority 67 % / 22 % |

Battery use in every strategy: ≤ 0.05 kWh, so `MaintainPowerAbove` never binds in this scenario (expected; see §B).

**Option D-b (wider margins, longer and gentler flare):** T 10–40 s at about 2.9 °C/s, deadline T 100 s. The Boost-versus-passive separation becomes 15 °C (passive about 107 °C, Boost about 92 °C). It is not yet simulated in full; it would be calibrated the same way if preferred.

### E. Reporting

- HUD MISSION panel: recovery state, hold progress ("stable 3.2/5.0 s"), time to deadline, failure reason. SYSTEMS panel: cooling authority and curtailed kW.
- After-action review: per-objective final states, terminal reason, recovery completion time, minimum cooling authority, peak temperature.

## Validation

- **Calibration replica:** a Python step model of `FEdenThermalModel::Step` at 10 Hz (ambient floor, [−50, 120] clamp, fail on > 100 °C, hold reset on a break). It is used only to choose values. The C++ strategy-matrix tests are the authority, and must reproduce every §D row within 0.1 s and 0.05 °C.
- **Unit:**
  - bus allocation: priority order, unlimited default, empty battery, load shed;
  - cooling authority scaling;
  - recovery hold reset;
  - a float-noise plateau;
  - the deadline boundary;
  - an active flare blocks recovery;
  - the initial-condition command is accepted only on a fresh start of a declaring definition.
- **Integration:**
  - start at 0 s, 60 s and 300 s after PIE gives identical v2 outcomes and temperature traces;
  - an undeclared mission starts from current conditions;
  - an idle Nominal ship holds 20 °C for 10 min;
  - Off drifts up at +0.5 °C/s;
  - the full §D matrix through operator control;
  - restart reproduces the run exactly;
  - abort clears mission effects;
  - v1 pinned-config regression is unchanged.
- **Determinism:** outcomes identical across the frame-rate harness sources.
- **Regression:** full `Eden.*` suite, the flight determinism references, and the Telemetry v1 schema test, all unchanged.
- **EDEN policy:** the existing deterministic advisory policy tests still never propose load shed or reduced propulsion as thermal relief. Add a v2 case: overheating while Emergency curtails auxiliary load, where shed is suggested only as electrical relief, with its cooling cost stated.
- **PIE (human):** each §D strategy once; HUD hold progress, cooling authority and curtailment; the AAR matches the HUD; no overheating in idle Nominal after success.

## Failure modes and rollback

- Numbers too easy or too hard in PIE: tune `DA_SolarCrisisV2` data only. The default mission stays v1 until v2 PIE passes.
- Bus allocation changes existing behaviour: guarded by the unlimited default, with existing tests unchanged.
- Initial-condition command misuse: validated to the start transition, with a test that a timeline event of that type is rejected.
- Rollback: revert the 0011 v2 branch; the v1 assets and pinned configuration are untouched.

## Progress log

- 2026-10-14: Revision 1 drafted after the v1 audit.
- 2026-10-14: Checkpoint A. A2 implemented in `FEdenOperatorControlModel::ResolveIntent`: Nominal +0.5 C/s at 0.5 kW (new `NominalDissipation...`/`NominalCoolingDemand...` config, C++ defaults so the shipped DA picks them up); Off = passive only; Boost/Emergency replace Nominal. Modifiers now also carry the mode's `ThermalControlDemandKilowatts` and `ActiveCoolingDegreesCelsiusPerSecond` for Checkpoint B. Three existing tests updated to A2 expectations (load-shed net demand -1.5 kW; idle Nominal ship holds temperature, Off heats; Nominal costs 0.5 kW). New `Eden.Unit.SolarCrisisV2.A.*`. Suite 412/412, flight determinism unchanged.
- 2026-10-14: Checkpoint B. `FEdenPowerConfig::BatteryMaxDischargeKilowatts` (0 = unlimited default) and `FEdenPowerModel::AllocateBus`/`WithBusAllocation`: capacity = generation + (charged ? min(limit, energy left this step) : 0), served essential > thermal control > auxiliary; load shed removes auxiliary first. Power snapshot reports requested/served/curtailed per class, delivered demand, battery discharge and `CoolingAuthority`; limited-bus `Step` draws energy only for powered load (unlimited path uses the legacy formula unchanged). Mission override `SetBatteryDischargeLimitKilowatts`/`ClearBatteryDischargeLimitOverride`. Thermal snapshot carries `ActiveCoolingDegreesCelsiusPerSecond` and `CoolingAuthority`; effective dissipation = base + operator - active x (1 - authority); thermal pulls authority from the owner's power system every step. Operator control passes each mode's cooling kW and active cooling (`SetOperatorDemandBreakdown`, `SetOperatorCooling`). Bus inputs are re-applied in each component's `ApplySnapshot`, so no setter can drop them. Logs on bus curtailment start/end and cooling-authority changes. Wire/Telemetry v1 unchanged (hand-picked fields); note `netPowerKilowatts` on a limited bus is generation minus delivered load. Suite 418/418, flight determinism unchanged.
- 2026-10-14: Checkpoint C. `FEdenMissionInitialConditions` on the definition (`bSetInitialTemperature`/`InitialTemperatureCelsius`, `bSetBatteryDischargeLimit`/`BatteryMaxDischargeKilowatts`), validated at load. `StartMission` (Ready -> Running is always a fresh run) applies only what is declared: clears leftover mission external heating/demand, sets the temperature, sets the bus limit; refuses to start (logged, stays Ready) if a declared target is missing or rejects the value. Runtime records `bInitialConditionsApplied`/`bAppliedBatteryDischargeLimit`; abort, reset and the next load release the run's bus limit. Battery, propellant, flight state and operator modes are untouched; undeclared missions start from current conditions. Note: after a terminal Succeeded/Failed the run's bus limit stays until reset/abort/next load (the ship keeps simulating). Initial conditions are not a mission command type, so no timeline event can apply them. Tests: `Eden.Integration.SolarCrisisV2.C.{InitialConditionsAreScopedAndExplicit, EarlyAndLateActivationMatch}` (0/60/300 s idle in Off then start: identical traces; passive peak ~105 C at T 30 s). Suite 420/420.
- 2026-10-14: Checkpoint D. Objective types `RecoverThermalStability` (complete-seeking) and `MaintainPowerAbove` (fail-only) appended to `EEdenObjectiveType`; config gains `HoldSeconds` (5) and `DeadlineSeconds` (0 = none). `FEdenMissionObservations` adds mission heating and the realized net rate (thermal balance) to temperature/battery/fuel; `EvaluateObjectives(State, Def, Observations, Step)` keeps the legacy signature as a wrapper. Recovery: heating 0, T < target (strict), realized rate <= +0.01 C/s, held continuously (any break resets); completion on the deadline step wins; hold and deadline resolve on the nearest float step (half-step tolerance). Every objective records `ResolvedAtSeconds` and a plain-words `FailureReason`; the runtime and snapshot carry `TerminalReason` (`DescribeOutcome`); objective resolutions and the outcome are logged. Tests: `Eden.Unit.SolarCrisisV2.D.{RecoveryHoldSemantics, RecoveryDeadlineAndFloors}`. Suite 422/422.
- **Deviation (A):** the thermal baseline is spacecraft configuration, not mission configuration, so Solar Crisis v1 *played in PIE* now runs on A2 (the 2.5 C/s flare peaks near 70 C, making v1 much easier). v1's asset and automated tests are unchanged and pass. Pinning v1's original balance live would need a mission-declared configuration override; not done without review.
- 2026-10-14: Revision 2. A2 adopted; flare recalibrated by simulation (revision 1's 4.5 °C/s was infeasible under A2: immediate Boost reached 100 °C); deadline 90 → 100 s; electrical design E1 added after finding that cooling is not power-limited and battery energy cannot bind within 100 s.

## Decision log

- 2026-10-14 (reviewer): v1 matches its approved design; post-success overheating is not a code regression. Keep v1 unchanged during the current validation cycle.
- 2026-10-14 (reviewer): Thermal baseline **A2** — passive 0.5 °C/s kept; Nominal adds 0.5 °C/s of active cooling at a configurable electrical cost; Off keeps passive cooling only.
- 2026-10-14 (reviewer): A fresh Solar Crisis initializes to 20 °C; other missions keep their physical state; battery, fuel and unrelated systems are not reset.
- 2026-10-14 (reviewer): Battery and power must be meaningful through calibrated constraints, not arbitrary drain; load shed must not be presented as thermal relief.
- Pending review: E1 bus model and 6 kW discharge limit; curtailment reporting (a) or objective (b); D-a (4.25 °C/s, 20 s, deadline 100 s) or D-b.

## Acceptance evidence

None yet.

## Handoff

Review revision 2: E1, the curtailment option, and D-a versus D-b. Implementation starts at Checkpoint A only after approval.
