# Operator Systems Control and Mission HUD

## Status

**Approved — implementing (core spine + automation green; HUD Blueprint / input assets / PIE pending).**

Design locks L1–L5 (section 12) are accepted via ADR-0002. Implementation proceeds on `main`. ExecPlan 0004 Complete/tag remains gated on human PIE evidence; do not reopen Checkpoint F.

## Prerequisite status

> [!NOTE]
> 0004 automated remediation is on `main`. Manual PIE for 0004 H (+ delayed 0003) remains outstanding for Complete/tag only. 0005 implementation is authorized without waiting for that PIE closeout.
>
> 0006 implementation remains blocked until 0005 Checkpoint H scenario tests are green.

---

## 1. Problem and outcome

The simulation is complete but not operable. The spacecraft has authoritative fuel, power, and thermal state; the mission subsystem creates timed emergencies and judges outcomes. The player can fly, and can watch numbers move on a development debug overlay — but cannot *act* on them.

Today the Solar Crisis runs to completion identically whether the player is present or not. Objective success is a function of configuration, not decisions.

This milestone closes that gap:

```text
Solar Crisis → warning → power + thermal pressure
    → player diagnoses via production HUD
    → player issues operator commands
    → each command trades one pressure for another
    → mission evaluates the resulting state
    → success or failure reflects operational judgment
```

The outcome is a playable emergency-response slice where **the mission result is determined by what the operator did**.

---

## 2. Codebase discovery

Verified against source at `4a12241`.

### Existing command surfaces (authoritative owners, already public)

| Component | Commands available today |
|---|---|
| `UEdenPowerSystemComponent` | `SetGenerationKilowatts`, `SetBaselineDemandKilowatts`, `SetBatteryChargeKilowattHours`, `SetExternalDemandKilowatts`, `ClearExternalDemand`, `ResetPowerState` |
| `UEdenThermalSystemComponent` | `SetTemperatureCelsius`, `SetHeatGenerationDegreesCelsiusPerSecond`, `SetDissipationDegreesCelsiusPerSecond`, `SetExternalHeatingRateDegreesCelsiusPerSecond`, `ClearExternalHeatingRate`, `ResetThermalState` |
| `UEdenFuelSystemComponent` | `SetConsumptionDemandNormalized`, `SetFuelQuantityKilograms`, `RefreshPropulsionDemandSource`, `ResetFuelState` |
| `UEdenMissionSubsystem` | `LoadMission`, `StartMission`, `AbortMission`, `ResetMission`, snapshot accessors |

### Existing read surfaces

All three components expose `Get*StateSnapshot()` (immutable, `BlueprintPure`) and a non-reflected `Get*DebugSnapshot()`. Power snapshot already carries `ChargeFraction`; fuel already carries `FuelFraction`. **The HUD needs no new domain math** — normalized values exist.

### Existing presentation and input

- `AEdenFlightHUD` — `DrawHUD()` with `DrawEdenSystemsOverlay()` / `DrawEdenMissionOverlay()`. Canvas-drawn **development debug only**.
- `AEdenFlightPlayerController` — Enhanced Input for translate/rotate/stabilize; `Exec` commands `StartMission`, `RestartMission`, `AbortMission`.
- Input assets: `IA_FlightTranslate`, `IA_FlightRotate`, `IA_FlightStabilize`, `IMC_Flight`.
- Blueprints: `BP_EdenSpacecraftPawn`, `BP_EdenFlightPlayerController`, `BP_EdenFlightGameMode`.

### The critical gap

Every existing "set" command writes an **authoritative baseline** (`SetDissipation...`, `SetBaselineDemand...`). The mission layer deliberately does *not* use those — Checkpoint D added a **separate additive `External*` channel** so mission effects can be cleared on reset without destroying configured baselines.

Operator commands need the same treatment. If the operator writes `SetDissipationDegreesCelsiusPerSecond` directly, then:
- mission reset and operator reset fight over one field,
- the configured Data Asset baseline is permanently lost,
- there is no way to answer "what did the operator change?" for 0006 telemetry.

**This is the central architectural decision of 0005.**

---

## 3. Locked design decisions (proposed)

### 3.1 Three additive modifier channels, one owner each

```text
EffectiveDemand      = max(0, BaselineDemand + MissionExternalDemand + OperatorDemand)
EffectiveDissipation = max(0, BaselineDissipation + OperatorDissipation)
```

| Channel | Written by | Cleared by |
|---|---|---|
| Config baseline | Data Asset at init | `Reset*State()` |
| `External*` | `UEdenMissionSubsystem` | mission abort/reset |
| `Operator*` | `UEdenOperatorControlComponent` | operator reset / PIE reset |

Channels never overwrite one another. The resource component remains the single authoritative owner of the resulting state.

### 3.2 Operator intent has one owner

`UEdenOperatorControlComponent` on `AEdenSpacecraftPawn` owns **operator intent** (which cooling mode is selected, which loads are shed, propulsion priority). It translates intent into resource commands. It does not own temperature, charge, or fuel.

```text
HUD button / Input action
   → AEdenFlightPlayerController (input → intent)
   → UEdenOperatorControlComponent (intent → commands, validation)
   → resource component public command
   → resource component owns the result
```

No widget calls a resource component directly. No widget holds mutable domain state.

### 3.3 Commands are discrete modes, not sliders

Discrete modes are testable, loggable, readable on a HUD, and meaningful in an after-action report. Continuous sliders produce unbounded state space and untestable trade-offs.

### 3.4 Alerts derive from state transitions, not polling

The resource components already broadcast `OnStateChanged(Previous, Current)`. The alert layer subscribes; it never polls per frame and never re-derives thresholds.

---

## 4. Operator action design — alternatives considered

### Rejected

- **Manual battery charge control** — no physical meaning; trivially wins the scenario.
- **Direct temperature set** — writes authoritative state; is a cheat, not a decision.
- **Repair/patch minigame** — presentation, not simulation; no resource trade-off.
- **Continuous cooling slider** — unbounded state space, weak trade-off legibility.

### Recommended: three commands, each with a real cost

**1. Thermal control mode** — `Off / Nominal / Boost / Emergency`

```text
Boost      → dissipation +X °C/s   → power demand +Y kW
Emergency  → dissipation +2X °C/s  → power demand +3Y kW
```
Solves heat, spends battery. Under a solar impact that is already suppressing generation, sustained Emergency cooling will flatten the battery before the recovery phase.

**2. Nonessential load shedding** — `Normal / Shed`

```text
Shed → baseline demand −Z kW
     → stabilization assist unavailable
     → thermal dissipation baseline reduced (fans/pumps on the shed bus)
```
Saves power, costs capability *and* makes the thermal problem harder. This is the interesting one: it is not a free win.

**3. Propulsion priority** — `Full / Reduced`

```text
Reduced → max thrust authority scaled to 0.5
        → propulsion power draw and fuel burn reduced
```
Saves power and fuel, costs maneuverability.

Together these produce a genuine dilemma during Impact: cooling demands power, shedding load frees power but worsens cooling, and reducing propulsion frees power but costs control authority.

---

## 5. State ownership table (additions to ARCHITECTURE.md §5)

| State | Owner | Readers | Writers |
|---|---|---|---|
| Operator thermal mode | `UEdenOperatorControlComponent` | HUD, telemetry (0006) | Operator control component only |
| Operator load-shed state | `UEdenOperatorControlComponent` | HUD, telemetry | Operator control component only |
| Operator propulsion priority | `UEdenOperatorControlComponent` | HUD, flight movement, telemetry | Operator control component only |
| Operator cooling modifier (applied) | `UEdenThermalSystemComponent` | Mission, HUD | Thermal component only |
| Operator demand modifier (applied) | `UEdenPowerSystemComponent` | Mission, HUD | Power component only |
| Thrust authority scalar | `UEdenFlightMovementComponent` | Fuel, HUD | Movement component only |
| Active alerts | `UEdenAlertSubsystem` | HUD, telemetry | Alert subsystem only |
| Alert acknowledgement | `UEdenAlertSubsystem` | HUD | Alert subsystem only |
| HUD display values | Operator HUD view-model | Widget | Derived from snapshots |

---

## 6. Alert architecture

```cpp
enum class EEdenAlertSeverity : uint8 { Info, Warning, Critical, Emergency };

struct FEdenAlert
{
    FName    AlertId;
    EEdenAlertSeverity Severity;
    FText    DisplayText;
    FName    SourceSystem;        // Fuel / Power / Thermal / Mission
    float    RaisedAtSimTimeSeconds;
    bool     bAcknowledged;
};
```

`UEdenAlertSubsystem` (`UWorldSubsystem`):
- Binds to existing `OnStateChanged` delegates on fuel/power/thermal and to `OnMissionEventTriggered` / `OnMissionStateChanged`.
- Raises and clears alerts on **transitions only**.
- Maintains a bounded active-alert list (fixed cap, oldest-Info evicted first).
- Exposes `TArray<FEdenAlert> GetActiveAlerts() const` — an immutable copy.

Deliberately **not** coupled to telemetry: 0006 will subscribe to the same transition stream through its own sink interface. The alert subsystem never calls telemetry.

---

## 7. HUD architecture

```text
Resource snapshots + mission snapshot + alert list
        ↓
FEdenOperatorHudSnapshot   (assembled in C++, once per UI refresh)
        ↓
UEdenOperatorHudWidget     (UUserWidget, Blueprint-composed presentation)
```

- Assembly runs on a **UI refresh cadence (~10 Hz), not the fixed simulation step**. The HUD must never drive or gate simulation.
- The widget performs **no domain math** — no unit conversion, no threshold comparison, no percentage derivation. `ChargeFraction` and `FuelFraction` already exist for exactly this reason.
- `AEdenFlightHUD`'s canvas overlays remain development-only and untouched. The production HUD is a separate UMG surface so `ShowDebug` and the operator HUD never compete.

Minimum display: mission name, mission state, mission phase, objective list with per-objective state, fuel fraction, battery fraction, generation kW, total demand kW, temperature °C, per-system warning state, active alerts, and current operator mode selections.

---

## 8. Checkpoint breakdown

| # | Scope | Gate |
|---|---|---|
| **A** | `FEdenOperatorControlModel` pure model: mode enums, trade-off tables, validation, intent→modifier resolution | Unit tests, no world |
| **B** | Operator modifier APIs on thermal/power + thrust authority scalar on movement | Unit tests per component |
| **C** | `UEdenOperatorControlComponent`: intent ownership, command dispatch, reset | Integration tests |
| **D** | `FEdenAlert`, `EEdenAlertSeverity`, `UEdenAlertSubsystem`, transition binding | Integration tests |
| **E** | `FEdenOperatorHudSnapshot` assembly + query API | Unit tests |
| **F** | `WBP_EdenOperatorHud` + `UEdenOperatorHudWidget` binding | Editor asset verification |
| **G** | Enhanced Input actions + `IMC_Flight` additions + Blueprint composition | Asset verification |
| **H** | Solar Crisis trade-off integration tests, deterministic operator scenarios | Full automation |
| **I** | Manual PIE acceptance + docs closeout | Hands-on gate |

---

## 9. Test matrix

**Unit** — trade-off table correctness; intent validation rejects non-finite/out-of-range; modifier additivity (`baseline + mission + operator`); mode transitions; alert severity mapping; snapshot assembly.

**Integration** — operator cooling raises power demand and lowers temperature over fixed steps; load shed lowers demand and reduces dissipation; propulsion priority scales thrust authority and fuel burn; mission `External*` and operator modifiers coexist without clobbering; mission reset clears mission channel and leaves operator channel intact; operator reset clears operator channel only; alerts raise once per transition and clear on recovery; alert list respects its cap.

**Scenario (the milestone's real proof)** —
- `SolarCrisisPassiveOperatorFails` — no operator action → mission fails.
- `SolarCrisisCompetentOperatorSucceeds` — scripted correct actions → mission succeeds.
- `SolarCrisisOverCoolingDrainsBattery` — Emergency cooling held throughout → different failure mode.

These three are the acceptance criterion for "the player's decisions matter."

**Manual PIE** — HUD legibility, input responsiveness, alert visibility, `ShowDebug` non-interference, PIE restart cleanliness, clean Output Log.

---

## 10. Proposed source layout

```text
Public/Operations/
    EdenOperatorTypes.h              (modes, intent, trade-off config)
    EdenOperatorControlModel.h       (pure)
    EdenOperatorControlComponent.h
    EdenAlertTypes.h
    EdenAlertSubsystem.h
    EdenOperatorHudTypes.h           (FEdenOperatorHudSnapshot)
Private/Operations/                  (mirrors)
Private/Tests/
    EdenOperatorControlModelTests.cpp
    EdenOperatorControlComponentTests.cpp
    EdenAlertSubsystemTests.cpp
    EdenOperatorScenarioTests.cpp

Content/Eden/UI/WBP_EdenOperatorHud.uasset
Content/Eden/Input/IA_ThermalMode.uasset
Content/Eden/Input/IA_LoadShed.uasset
Content/Eden/Input/IA_PropulsionPriority.uasset
Content/Eden/Data/Operations/DA_EdenOperatorControlConfig.uasset
```

A new `Operations/` folder keeps operator concerns out of `Flight/` (movement) and `Systems/` (resources), matching the existing separation.

---

## 11. Out of scope

Telemetry transport and after-action review (0006); EDEN OS adapter (0007); networking; save/load; docking; procedural scenarios; polished art, audio, or voice; multiplayer; additional resource types; mission-selection UI.

---

## 12. Locked design resolutions (approved)

See also `docs/decisions/ADR-0002-operator-modifier-channels-and-thrust-authority.md`.

### L1 — Additive channels; corrected dissipation formula

```text
EffectiveDemand      = max(0, BaselineDemand + MissionExternalDemand + OperatorDemand)
EffectiveHeatGen     = BaselineHeatGen + MissionExternalHeating
EffectiveDissipation = max(0, BaselineDissipation + OperatorDissipation)
```

Mission keeps `External*` only. Operator writes `OperatorDemandKilowatts` / `OperatorDissipationDegreesCelsiusPerSecond` only.

### L2 — Load-shed ↔ cooling coupling: YES, in the operator trade-off table only

Shed reduces demand and dissipation and disables stabilization assist. Boost/Emergency add dissipation and cooling demand. Power and thermal never call each other.

### L3 — Thrust authority ownership: `UEdenFlightMovementComponent`

`ThrustAuthority` in `[0,1]` (Full=1.0, Reduced=0.5). Scales translation accel and propulsion demand. Fuel remains a demand reader only.

### L4 — HUD / alerts defaults

HUD assemble/refresh at **10 Hz**. Alert list cap **8** (evict oldest Info, then Warning; never silently drop Critical/Emergency).

### L5 — 0006-facing surfaces

`FEdenOperatorStateSnapshot`, operator intent/command-issued delegate, `OnAlertRaised` / `OnAlertCleared`. Mission `OnObjectiveStateChanged` remains 0006 Checkpoint D0.
