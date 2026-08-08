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
- Flight input assets exist under `/Game/Eden/Input`: `IA_FlightTranslate`, `IA_FlightRotate`, `IA_FlightStabilize`, and `IMC_Flight`.
- Flight composition Blueprints exist under `/Game/Eden/Blueprints`: `BP_EdenSpacecraftPawn`, `BP_EdenFlightPlayerController`, and `BP_EdenFlightGameMode`.
- Project-specific log categories are declared in `Source/EdenSpaceSimulator/Public/Core/EdenLogCategories.h`.
- Foundation automation smoke test name: `Eden.Unit.Foundation.Smoke`.
- Flight automation tests use the `Eden.Unit.Flight` prefix.
- Interactive PIE verification of the six-axis flight shell passed on 2026-07-22, including startup map, possession, camera, six-axis input, stabilization, blocker collision, PIE restart reset, and clean Output Log checks.
- Verified six-axis flight shell baseline tag: `v0.1.0-flight-shell` on commit `ed7fb55`.
- Resource simulation work proceeds on branch `feature/spacecraft-resource-simulation` from that tagged baseline.
- Resource simulation uses `UEdenSimulationClockSubsystem` as the world-scoped fixed-step simulation clock for Game and PIE worlds.
- `AEdenSpacecraftPawn` creates `FuelSystem`, `PowerSystem`, and `ThermalSystem` as inherited C++ default subobjects.
- `UEdenFuelSystemComponent`, `UEdenPowerSystemComponent`, and `UEdenThermalSystemComponent` are the authoritative owners for fuel, power, and thermal runtime state.
- `UEdenFlightMovementComponent` implements `IEdenPropulsionDemandSource`; fuel reads propulsion demand through that interface instead of depending on the concrete pawn class.
- Resource configuration Data Assets exist under `/Game/Eden/Data/Systems`: `DA_EdenFuelConfig`, `DA_EdenPowerConfig`, and `DA_EdenThermalConfig`.
- `BP_EdenSpacecraftPawn` assigns those resource Data Assets to the inherited C++ resource components.
- Development builds expose read-only resource visibility through `ShowDebug EdenSystems` and mission visibility through `ShowDebug EdenMission`.
- Mission simulation uses `UEdenMissionSubsystem` as a world-scoped subsystem registered with `UEdenSimulationClockSubsystem` at Priority 100 (`EdenSimulationClockPriority::Mission`), stepping deterministically after Priority 0 resource components (`EdenSimulationClockPriority::Systems`).
- Emergency missions are data-driven via `UEdenMissionDefinitionDataAsset` and pure deterministic state machine `FEdenMissionModel`.
- The reference emergency scenario is `SolarCrisis` (`Solar Event Emergency`), configuring a 50s multi-phase timeline with external heating, power demand, generation adjustments, and four required objectives (`SurviveSolarEvent`, `PreventOverheating`, `RestoreBatteryCharge`, `ConservePropellant`).
- Developer console commands `StartMission`, `RestartMission`, and `AbortMission` exist on `AEdenFlightPlayerController` for interactive testing.
- The first vertical slice is a docking and emergency-response trainer.
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

Add information only when it is:

- Expected to remain true across many sessions
- Verified in source, configuration, build output, or an approved decision
- Important enough to alter future implementation choices

Move transient work status to `docs/RECOVER.md`. Move design rationale to an ADR.
