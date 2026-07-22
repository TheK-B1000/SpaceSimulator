# Eden Space Simulator

Eden Space Simulator is a professional Unreal Engine 5.8 C++ project for building an AI-assisted spacecraft operations and emergency-response simulator.

The first vertical slice is intentionally focused: pilot a small spacecraft or habitat-adjacent vehicle, manage critical resources, respond to a mission failure, and emit telemetry that can later connect to EDEN OS.

## Project status

**Stage:** Foundation and architecture  
**Engine:** Unreal Engine 5.8  
**Primary platform:** Windows desktop  
**Primary build target:** `EdenSpaceSimulatorEditor Win64 Development`

## Product vision

The simulator should demonstrate:

* Six-degree-of-freedom spacecraft control
* Deterministic or reproducible system simulation where practical
* Power, fuel, thermal, oxygen, and failure-state modeling
* Data-driven mission scenarios
* Clear operator feedback and after-action review
* Telemetry suitable for later FastAPI or WebSocket integration with EDEN OS
* Professional C++ architecture, automated tests, and traceable engineering decisions

This is not initially an open-world space game. The first product is a compact simulation lab with a strong technical spine.

## First vertical slice

A player completes a docking and emergency-response scenario:

1. Approach an orbital habitat using six-axis movement.
2. Maintain fuel, power, and temperature within safe limits.
3. Respond to a solar-event or equipment-failure sequence.
4. Dock or stabilize the vehicle before a defined failure condition.
5. Produce a telemetry record and mission outcome.

## Prerequisites

* Unreal Engine 5.8
* Visual Studio 2022 with **Game development with C++**
* A compatible Windows SDK and MSVC toolchain
* Git
* Git LFS
* PowerShell 7 or Windows PowerShell 5.1

## Repository setup

Clone the repository, then run:

```powershell
git lfs install
git lfs pull
```

Confirm the repository root contains:

```text
EdenSpaceSimulator.uproject
Source/
Config/
Content/
AGENTS.md
docs/
scripts/
```

Set the engine location for local scripts without committing a machine-specific path:

```powershell
$env:UE_ENGINE_ROOT = "K:\Path\To\UE_5.8"
```

Validate the repository:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1
```

Build the editor target:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1 -Build -EngineRoot $env:UE_ENGINE_ROOT
```

Run project automation tests after tests exist:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Validate-Project.ps1 -Build -RunTests -EngineRoot $env:UE_ENGINE_ROOT
```

## Opening the project

Open `EdenSpaceSimulator.uproject` with Unreal Engine 5.8.

For a clean IDE regeneration:

1. Close Unreal Editor and Visual Studio.
2. Right-click the `.uproject`.
3. Select **Generate Visual Studio project files**.
4. Open the generated solution.
5. Build `Development Editor | Win64`.

Generated solution and build files are intentionally excluded from Git.

## Architecture at a glance

```text
Input / PlayerController
          |
          v
   Spacecraft Pawn
          |
          v
 Flight Movement Component

Mission Subsystem ----commands----> Spacecraft System Components
       |                                  |
       |                                  v
       +--------------------------> Immutable State Snapshots
                                          |
                                          v
                                  Telemetry Subsystem
                                          |
                                          v
                                  Telemetry Sink Adapter
```

Core rules:

* Each system owns its own mutable state.
* UI displays state and emits commands but does not own truth.
* Mission logic coordinates through public APIs and events.
* Telemetry observes immutable snapshots.
* Backend integration is isolated behind an adapter.
* C++ owns reusable rules; Blueprints own presentation and tuning.

Read [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the full design.

## Documentation map

|File|Purpose|
|-|-|
|`AGENTS.md`|Automatic repository instructions for Codex|
|`docs/IMPRINT.md`|Project identity and non-negotiable principles|
|`docs/REMEMBER.md`|Durable facts and approved decisions|
|`docs/PROJECT\_SPEC.md`|Scope, constraints, success criteria, and failure modes|
|`docs/ARCHITECTURE.md`|Components, dependencies, state ownership, and data flow|
|`docs/SDLC.md`|Required software-development lifecycle|
|`docs/REVIEW.md`|Review and quality checklist|
|`docs/RECOVER.md`|Session recovery and handoff ledger|
|`.agent/PLANS.md`|Rules for executable implementation plans|
|`docs/decisions/`|Architecture Decision Records|
|`docs/exec-plans/`|Active and completed implementation plans|

## Source organization

The intended source layout is:

```text
Source/EdenSpaceSimulator/
+-- Public/
|   +-- Core/
|   +-- Flight/
|   +-- Missions/
|   +-- Systems/
|   +-- Telemetry/
+-- Private/
    +-- Core/
    +-- Flight/
    +-- Missions/
    +-- Systems/
    +-- Telemetry/
    +-- Tests/
```

Do not create empty abstractions merely to match the folder tree. Add a folder or class when the corresponding responsibility exists.

## Content organization

```text
Content/Eden/
+-- Core/
+-- Data/
+-- Maps/
+-- Missions/
+-- Spacecraft/
+-- Systems/
+-- UI/
+-- Art/
+-- Audio/
+-- Developer/
```

Recommended asset prefixes:

* `BP\_` Blueprint class
* `WBP\_` Widget Blueprint
* `L\_` Level
* `IA\_` Input Action
* `IMC\_` Input Mapping Context
* `DA\_` Data Asset
* `DT\_` Data Table
* `M\_` Material
* `MI\_` Material Instance
* `SM\_` Static Mesh
* `SK\_` Skeletal Mesh
* `T\_` Texture
* `S\_` Sound

## Testing strategy

Use Unreal Automation Tests for domain calculations and low-level C++ behavior. Add higher-level functional or map tests only where they provide meaningful confidence.

Test naming convention:

```text
Eden.Unit.<Area>.<Behavior>
Eden.Integration.<Area>.<Behavior>
Eden.Functional.<Scenario>.<Behavior>
```

Every defect fix should include a regression test when the failure can be reproduced deterministically.

## Git and binary assets

Unreal assets are stored with Git LFS through `.gitattributes`.

Do not commit:

* `Binaries/`
* `DerivedDataCache/`
* `Intermediate/`
* `Saved/`
* `.vs/`
* generated solution files
* local IDE state
* packaged builds
* secrets or machine-specific engine paths

Do commit:

* `Config/`
* `Content/`
* `Source/`
* `Plugins/` source and descriptors
* `.uproject`
* documentation
* scripts
* intentional build resources

## Definition of done

A change is done only when:

* Acceptance criteria are met.
* State ownership and dependency rules remain valid.
* The editor target builds.
* Applicable tests pass.
* Manual editor verification is recorded where required.
* Logs and errors provide actionable context.
* Documentation and recovery state are updated.
* The final diff contains no generated or unrelated files.

## Roadmap

See [`docs/ROADMAP.md`](docs/ROADMAP.md).

## License

Owned by K-B

