# REMEMBER

This file stores durable, verified project facts that future Codex sessions must preserve. It is not a chat transcript, scratchpad, or backlog.

## Current durable facts

- Project name: `EdenSpaceSimulator`
- Engine baseline: Unreal Engine 5.8
- Project type: C++ Unreal project
- Primary runtime module: `EdenSpaceSimulator`
- Primary platform: Windows desktop
- Primary development target: `EdenSpaceSimulatorEditor`, Win64, Development
- Repository root: the directory containing `.git` and `EdenSpaceSimulator.uproject`
- Local engine paths are machine-specific and must not be committed.
- Unreal binary assets are tracked with Git LFS.
- Enhanced Input is explicitly enabled in `EdenSpaceSimulator.uproject` and configured in `Config/DefaultInput.ini`.
- Default game map and editor startup map are `/Game/Eden/Maps/L_FlightSandbox`.
- Flight sandbox GameMode is `/Game/Eden/Blueprints/BP_EdenFlightGameMode.BP_EdenFlightGameMode_C`.
- Six-axis flight shell C++ classes exist under `Source/EdenSpaceSimulator/Public/Flight` and `Private/Flight`.
- Flight input assets exist under `/Game/Eden/Input`: `IA_FlightTranslate`, `IA_FlightRotate`, `IA_FlightStabilize`, `IA_ThermalMode`, `IA_LoadShed`, `IA_PropulsionPriority`, and `IMC_Flight`.
- Flight composition Blueprints exist under `/Game/Eden/Blueprints`: `BP_EdenSpacecraftPawn`, `BP_EdenFlightPlayerController`, and `BP_EdenFlightGameMode`.
- Operator HUD widget asset: `/Game/Eden/UI/WBP_EdenOperatorHud` (parent `UEdenOperatorHudWidget`), assigned on `BP_EdenFlightPlayerController`.
- Operator control config Data Asset: `/Game/Eden/Data/Operations/DA_EdenOperatorControlConfig`.
- Operator Enhanced Input bindings (first content pass): `T` cycles thermal mode, `L` toggles load-shed, `P` toggles propulsion priority; ownership path is Input → `AEdenFlightPlayerController` → `UEdenOperatorControlComponent` → resource/flight APIs.
- Interactive PIE verification of operator HUD and input passed on 2026-08-08 (`L_FlightSandbox`): HUD visible, `T`/`L`/`P` update operator state, Solar Crisis trade-offs visible, restart/reset clean, Output Log clean.
- Telemetry uses `UEdenTelemetrySubsystem` at simulation-clock priority 200 (`Observers`); exports Telemetry Export Schema v1 via `ExportTelemetry` to `Saved/Telemetry/*.json`.
- After-action review is built by pure `FEdenAfterActionModel` and shown manually with `ShowAfterAction` / `WBP_EdenAfterActionReview` (no automatic popup).
- Project-specific log categories are declared in `Source/EdenSpaceSimulator/Public/Core/EdenLogCategories.h`.
- Foundation automation smoke test name: `Eden.Unit.Foundation.Smoke`.
- Flight automation tests use the `Eden.Unit.Flight` prefix.
- Interactive PIE verification of the six-axis flight shell passed on 2026-07-22, including startup map, possession, camera, six-axis input, stabilization, blocker collision, PIE restart reset, and clean Output Log checks.
- Verified six-axis flight shell baseline tag: `v0.1.0-flight-shell` on commit `ed7fb55`.
- Verified spacecraft resource simulation + emergency mission shell tag: `v0.3.0-emergency-mission` (manual PIE 2026-08-08 on `L_FlightSandbox`).
- Resource simulation uses `UEdenSimulationClockSubsystem` as the world-scoped fixed-step simulation clock for Game and PIE worlds.
- `AEdenSpacecraftPawn` creates `FuelSystem`, `PowerSystem`, and `ThermalSystem` as inherited C++ default subobjects.
- `UEdenFuelSystemComponent`, `UEdenPowerSystemComponent`, and `UEdenThermalSystemComponent` are the authoritative owners for fuel, power, and thermal runtime state.
- `UEdenFlightMovementComponent` implements `IEdenPropulsionDemandSource`; fuel reads propulsion demand through that interface instead of depending on the concrete pawn class.
- Resource configuration Data Assets exist under `/Game/Eden/Data/Systems`: `DA_EdenFuelConfig`, `DA_EdenPowerConfig`, and `DA_EdenThermalConfig`.
- `BP_EdenSpacecraftPawn` assigns those resource Data Assets to the inherited C++ resource components.
- Development builds expose read-only resource visibility through `ShowDebug EdenSystems`.
- Development builds expose read-only mission visibility through `ShowDebug EdenMission`.
- Mission simulation uses `UEdenMissionSubsystem` as a world-scoped subsystem registered with `UEdenSimulationClockSubsystem` at Priority 100 (`EdenSimulationClockPriority::Mission`), stepping deterministically after Priority 0 resource components (`EdenSimulationClockPriority::Systems`).
- Emergency missions are data-driven via `UEdenMissionDefinitionDataAsset` and pure deterministic state machine `FEdenMissionModel`.
- Approved mission resource disturbance commands are external heating and external power demand only. `SetPowerGeneration` remains unsupported for the emergency-mission milestone.
- The reference emergency scenario is `SolarCrisis` (`Solar Event Emergency`), authored as `/Game/Eden/Data/Missions/DA_SolarEventEmergency`, configuring a 50s multi-phase timeline with external heating, external power demand, and four required objectives (`SurviveSolarEvent`, `PreventOverheating`, `RestoreBatteryCharge`, `ConservePropellant`).
- Developer console commands `StartMission`, `RestartMission`, and `AbortMission` load the Solar Event Data Asset soft path; interactive PIE verification of that operator workflow passed on 2026-08-08.
- Developer console commands `EnableEdenOs` and `SetEdenOsBearerFromEnv` configure the EDEN OS adapter for interactive PIE; defaults align with live E2E env vars `EDEN_OS_LIVE_E2E_BASE_URL` / `EDEN_OS_LIVE_E2E_BEARER_JWT` (JWT value is never logged or committed).
- Product goal for the first vertical slice: a docking and emergency-response trainer. Emergency mission infrastructure is verified; docking gameplay is a later milestone unless separately verified complete.
- The core architecture uses C++ for reusable behavior and Blueprints for composition and presentation.
- UI does not own authoritative simulation state.
- Telemetry observes state and does not mutate it.
- Backend or EDEN OS integration must be behind an adapter boundary.
- Core simulation behavior requires automated tests where deterministic testing is practical.
- Generated build, cache, IDE, and packaged-output directories are not tracked.

## Approved engineering rules

- Follow Epic's C++ coding standard.
- Prefer composition over deep inheritance.
- Use one authoritative owner per mutable state value.
- Avoid `Tick` unless a per-frame responsibility is justified.
- Resource simulation should use an explicit fixed-step clock rather than rendering frame rate.
- Flight physics may update at the engine or physics cadence, while resource and mission simulation use a separate fixed cadence.
- Units must be explicit.
- Use project-specific log categories.
- Use ADRs for architecture decisions.
- Use ExecPlans for significant work.
- Update `docs/RECOVER.md` before ending a substantial session.

## Facts not yet verified

The following must be inspected in the actual repository before being promoted to durable facts:

- Exact engine installation path
- Existing map and asset names
- Whether Git LFS has already been applied to existing binary history

## Update policy

Treat `docs/REMEMBER.md` as verified implementation only.

Do not add a feature merely because:

- it exists in an ExecPlan
- `ARCHITECTURE.md` describes it
- another branch contains a sketch
- or a test plan mentions it

Before adding or updating a durable fact, verify it in the integrated branch through source, config, and assets, plus successful validation.

Document ownership:

- `ARCHITECTURE.md` — approved system shape and future boundaries
- ExecPlans — current work and evidence
- `docs/RECOVER.md` — transient checkpoint and branch state
- `docs/REMEMBER.md` — durable verified truth every future agent may safely assume

Add information only when it is:

- Expected to remain true across many sessions
- Verified in source, configuration, build output, or an approved decision
- Important enough to alter future implementation choices

Move transient work status to `docs/RECOVER.md`. Move design rationale to an ADR.
