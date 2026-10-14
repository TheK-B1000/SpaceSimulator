# 0009 Visual Vertical Slice

## Status

Draft — 2026-10-13. Not approved. No production scene work, asset import, or branch creation until this plan is approved.

Fab library check the same day: this machine has Unreal Engine 5.8, the Fab plugin, Quixel Bridge, and the Megascans plugin installed. It does not have a Vault cache or a local copy of any space pack. A library screenshot from the signed-in account (2026-10-13) shows military vehicles, FPS content, vegetation, and Megascans earth surfaces. It does not include Star Sparrow, Arghanion's flight system, Space Game Starter Kit, a space station, or the Niagara Examples Pack. Those still have to be added from free Fab listings. Nothing in the current library replaces the cone with a spacecraft.

## Problem and outcome

`L_FlightSandbox` is a physics prototype: a BasicShapes cone, one directional light, and a BasicShapes cube named `SM_FlightSandbox_Blocker`. The Newtonian flight model on `feature/0008-newtonian-flight` can already deliver force, torque, propellant limits, and power limits, but the picture does not show any of that.

Observable result when this plan is complete, played from the Newtonian branch:

- A recognizable spacecraft, a lit planet and moon, a starfield, and a station that replaces the cube as the thing you fly toward.
- Main-engine and RCS effects that appear only while delivered thrust or torque is non-zero. Coasting, an empty tank, and zero power authority produce no plume.
- A flight HUD that reads speed, attitude rate, fuel, power, flight assist, range to the station, and the existing EDEN advisory.
- The 120 Hz integrator, mass, fuel ownership, and EDEN command boundary are unchanged.

## Scope

### In scope

- A separate Unreal 5.8 content-test project used only to import and reject packs.
- A later visual branch cut from `feature/0008-newtonian-flight` after Checkpoints A–D have had a human PIE pass.
- One presentation level. It may replace the look of `L_FlightSandbox` or sit beside it as `L_OrbitalApproach`. The sphere collision root stays the movement root.
- Reading `FEdenFlightDynamicsSnapshot` and the existing operator HUD snapshot. No new forces.
- Free or already-owned presentation assets, with licenses recorded before they enter the game project.
- Engine sky, atmosphere, directional light, skylight, and a restrained post-process volume.

### Out of scope

- Any edit to `EdenSpaceSimulator 5.8` or to `main`.
- Merging 0008 or 0009.
- Purchasing assets.
- Adopting Arghanion or Space Game Starter Kit flight, cameras, or pawns as the simulation.
- Meshy generation, Quixel dressing, docking gameplay, orbital mechanics, planetary collision, and atmospheric flight.
- Changes to `FEdenFlightMovementModel`, fuel draw, the EDEN wire schema, or ProjectEden.

## Current repository state

Audited 2026-10-13.

| Item | Fact |
|---|---|
| Editor actually launched | `EdenSpaceSimulator 5.8`, branch `main` @ `455c238`. PIE log loaded `L_FlightSandbox` at 10:22. |
| Newtonian work | `EdenSpaceSimulator`, branch `feature/0008-newtonian-flight` @ `114a525`. Editor log last written 2026-08-19. Checkpoints A–D implemented; human PIE still pending. Not mergeable until ExecPlan 0007 Checkpoint M PIE passes on `main`. |
| Visible ship | `BP_EdenSpacecraftPawn` → `DebugPlaceholderMesh` = `/Engine/BasicShapes/Cone`, plus `DebugFlightCamera`. |
| Physics root | `USphereComponent` `RequiredCollisionRoot`, radius 100 cm. Movement refuses a non-sphere updated component. |
| Level | `PlayerStart_FlightSandbox`, `DirectionalLight_FlightSandbox`, `SM_FlightSandbox_Blocker` (`/Engine/BasicShapes/Cube`). No sky, skylight, fog, post-process, Niagara, or planet. |
| HUD | `UEdenOperatorHudWidget` text: mission, fuel %, battery, power, temperature, operator modes, alerts, read-only EDEN advisory. No velocity or attitude. |
| Dynamics available to visuals | `UEdenFlightMovementComponent::GetFlightDynamicsSnapshot()`. Relevant fields: `AppliedBodyForceNewtons`, `AppliedBodyTorqueNewtonMeters`, `DeliveredThrusterOutputNewtons`, `SpeedMetersPerSecond`, `VelocityWorldMetersPerSecond`, `AngularVelocityBodyRadiansPerSecond`, `PropellantMassKilograms`, `PowerThrustFraction`, `bPowerLimited`, `bPropellantLimited`, `bFlightAssistActive`. |
| Startup warnings | On possess, `ResetFlightState()` runs before power, thermal, and operator `BeginPlay`. The three "disabled" lines are ordering noise. The same PIE log then records fuel, power, and thermal active. |

Both content trees currently contain the same 18 files. Switching projects changes the flight code, not the art.

## Architecture alignment

Unreal continues to own reality. This plan only adds a presentation layer.

| Mutable state | Owner | Visuals may |
|---|---|---|
| Velocity, force, torque, mass, propellant draw | `UEdenFlightMovementComponent` and `UEdenFuelSystemComponent` | Read the dynamics snapshot. Never write it. |
| Power and thermal | Their system components | Read snapshots for dimming and HUD. |
| Operator intent and EDEN advisory | Operator control and the Eden OS adapter | Read the existing HUD snapshot. |
| Plume intensity, light brightness, camera lag | New presentation component or Blueprint on the pawn | Own only cosmetic state derived from the snapshot. |
| Ship mesh transform | Child of the sphere root, or attached with a fixed offset | Follow the root. Do not become the updated component. |

Dependency direction stays: presentation → flight snapshot. Flight does not include Niagara, materials, or UMG.

The plume rule is: intensity follows delivered body force and delivered body torque from the last flight step. Speed does not drive the plume. A coasting ship at any speed shows no exhaust.

## Alternatives considered

| Option | Verdict |
|---|---|
| **A. Scratch project, then copy approved art onto a visual branch (chosen)** | A bad pack cannot land in the physics project. Fab import stays out of git until a pack is accepted. |
| B. Import packs directly into `EdenSpaceSimulator` | Rejected. Marketplace Blueprints, input assets, and demo maps would mix with the flight pawn and `L_FlightSandbox`. |
| C. Replace Eden flight with Arghanion's flight system | Rejected. That pack is a reference for thruster FX, HUD layout, and camera feel. Its movement component must not run. |
| D. Generate the hero ship in Meshy before any environment exists | Deferred. Star Sparrow, if owned or still free, is the stand-in. An original EDEN hull is a later plan. |

## Asset policy

Confirm each pack in Fab while signed in. Record license, version, and whether it was already owned. Do not download a paid SKU.

Owned Vault library, from the 2026-10-13 screenshot, is not a 5.8 space-art library. High-Tech Pack 1, Quarry Collections, and Lava Field Collections were rejected by the launcher as incompatible with Unreal 5.8. Do not add them to `EdenVisualScratch`, and do not install Eden into the older 5.0 or 5.5 engines on `C:` to force them open. Military vehicles, aircraft, vegetation, and FPS kits stay unused.

Still not owned, and still the packs to add from free Fab listings:

| Priority | Pack | Use | Do not use |
|---|---|---|---|
| 1 | Spaceship Modular Star Sparrow (`932ce79b-06e8-416a-ba0d-36435621a0a1`) | Modular hull to replace the cone. Not in the current library; add it only if the listing is free. | Its demo game mode, if it has one |
| 2 | Space Game Starter Kit (`ce6c1d76-ddca-4f4c-ba52-a9383197dd92`) | Starfield, planets, lighting reference | Its spaceship Blueprint and flight system |
| 3 | Niagara Examples Pack (`0e188eca-4e54-4fb2-a9ed-d8b8a565e600`) | Source effects to adapt for plumes and RCS | Dropping an unrelated fire system on the ship unchanged |
| 4 | Free station: Gerardo Justel (`03fc052f-e479-4fef-afe9-7efb324dc375`) or Free Space Station Modules (`431ca84e-2d53-4f94-b6b8-031aecc6b6f1`) | Destination mesh and docking lights | Gameplay docking |
| Reference only | Arghanion's Space Ship Flight System (`62a69df5-b447-415d-8d91-82f9f1a46894`) | Look at thruster sockets, HUD, and camera in the scratch project | Enabling that pawn in Eden |
| Later | Solar System Scope textures (CC BY 4.0) or a checked NASA map | Planet and moon materials, with attribution | Untested commercial use of a specific NASA image |
| Later | Poly Haven CC0, owned or free Quixel surfaces | Hull wear, station metal, asteroid rocks | Assuming every Megascans asset is free |
| Deferred | Meshy EDEN Explorer | Original hull after the slice plays | Free-tier models until attribution and cleanup are planned |

Third-party source art stays in the scratch project. The game repo receives only the meshes, materials, and Niagara systems that Milestone 0 accepts, plus a `Content/Eden/Art/THIRD_PARTY.md` credit file. Do not commit Fab pack source, demo maps, or another project's Blueprints.

## Milestones

Each milestone is reviewable on its own. Milestones 1–4 do not start until Milestone 0 accepts at least a ship, a station, and a space backdrop.

| CP | Deliverable | Acceptance |
|---|---|---|
| 0 | Scratch project `K:\UnrealProjects\EdenVisualScratch` on UE 5.8. Import only packs the library actually exposes. Write a one-page keep/reject note per pack. | No Eden repo files change except this plan's progress log. A human can open the scratch map and see the candidate ship and station. |
| 1 | Visual branch from `feature/0008-newtonian-flight`. Orbital presentation level: starfield, one planet, one moon, sunlight, skylight, station. Cube is no longer the visual target. Physics root and flight code unchanged. | PIE from the Newtonian branch: black void and rectangle are gone. Assist-off release still drifts. `Eden.` flight tests still pass. |
| 2 | Star Sparrow (or the accepted substitute) attached to the sphere root. Cone hidden. Materials and a couple of emissive window or radiator lights. | The ship reads as a spacecraft in hard sunlight. Collision root is still the sphere. |
| 3 | Niagara main plume and four RCS cues. Driven by `AppliedBodyForceNewtons` and `AppliedBodyTorqueNewtonMeters`. | Plume is off while coasting. Plume is off with an empty tank and with power authority at zero. Rotation jets track torque sign, not speed. |
| 4 | HUD adds speed, body rates, assist, propellant mass, power fraction, and station range. Existing mission and EDEN advisory text stays. | Instruments move when thrust, assist, fuel, or power changes. Advisory text still comes from the adapter snapshot. |

## Detailed steps

### Milestone 0 — content test project

1. `K:\UnrealProjects\EdenVisualScratch` already exists and is associated with UE 5.8. Do not retarget it.
2. Skip Vault **Add to Project** for High-Tech Pack 1, Quarry, and Lava Field. Their engine versions stop short of 5.8.
3. In Fab, filter listings to Unreal 5.8. Download Star Sparrow only if the button says free and the download includes FBX or a 5.8 project. Import the FBX into `EdenVisualScratch`. Do not buy the Ultimate Spaceships Creator.
4. Place one graybox map: imported hull, a large planet sphere, a moon sphere, directional light, sky atmosphere, skylight. Station and Niagara come after the hull is accepted.
5. Append a keep/reject line to the progress log. Reject a pack that requires a flight pawn, fails to import in 5.8, or has no usable license.

### Milestone 1 — orbital environment

Branch `feature/0009-visual-slice` from `feature/0008-newtonian-flight` only after a human PIE note exists for 0008 Checkpoints A–D. Do not branch from `5.8`.

- Add presentation actors to a copy of the sandbox map, or a new `L_OrbitalApproach` set as the editor startup map on this branch only.
- Planet and moon are scenery. They do not block the sphere and they are not physics bodies.
- Keep `SM_FlightSandbox_Blocker` only if a collision target is still required, and hide its mesh.
- Leave `UEdenFlightMovementComponent` and the player controller's flight tick untouched.

### Milestone 2 — hero spacecraft

- Import the accepted hull into `Content/Eden/Art/Spacecraft/`.
- Add a static mesh component on `BP_EdenSpacecraftPawn`, attached to `RequiredCollisionRoot`, and hide `DebugPlaceholderMesh`.
- Do not assign that mesh to `SetUpdatedComponent`.
- Sockets, if the pack has them: `Engine_Main`, `RCS_PosX`, `RCS_NegX`, `RCS_PosY`, `RCS_NegY`, `RCS_PosZ`, `RCS_NegZ`. Add empty sockets on the hull if the pack has none.

### Milestone 3 — living propulsion

- Add `UEdenPropulsionPresentationComponent` (name can change, ownership cannot). It ticks in the render frame, reads `GetFlightDynamicsSnapshot()`, and sets Niagara parameters.
- Main plume user parameter is the magnitude of `AppliedBodyForceNewtons` along the engine axis. Zero means deactivate or spawn rate 0.
- Each RCS jet uses the sign of `AppliedBodyTorqueNewtonMeters` on its axis. Zero torque means off.
- Also force plumes off when `DeliveredThrusterOutputNewtons` is 0, `bPropellantLimited` and available thrust are 0, or `PowerThrustFraction` is 0.
- No component in this class may call `MoveWithCommand`, `AddForce`, or fuel draw.

### Milestone 4 — flight HUD

- Extend `FEdenOperatorHudSnapshot` with copies of the dynamics fields the widget needs, filled by the existing HUD refresh in `AEdenFlightPlayerController`. The widget still does not query the pawn itself if the snapshot already carries the values.
- Add speed (m/s), body angular rate, assist requested/active, propellant kilograms, power fraction, and distance from the pawn to the station actor.
- Keep the current mission, resource, alert, and EDEN advisory block.

## Validation

- Milestone 0 is a human look in `EdenVisualScratch` only.
- From Milestone 1 onward, on the visual branch: Win64 Development Editor build of `EdenSpaceSimulator`, then the existing `Eden.Unit.Flight` and `Eden.Integration.Flight` automation. Those tests must stay green because flight code is not supposed to change.
- Manual PIE, Newtonian branch:
  - Assist off, release the stick: the ship keeps its velocity and the plume goes out.
  - Assist on, release the stick: the ship brakes and the plume is visible only while thrust is delivered.
  - Empty tank: no translation, no rotation thrust, no plume.
  - Power depleted or thrust fraction 0: no plume.
  - High speed, stick centered, assist off: no plume.
- Output Log may still print the three startup "disabled" warnings until a separate fix. They are not an acceptance failure for this plan. Fuel, power, and thermal must still log active.

## Failure modes and rollback

- A pack drags in a pawn or game mode. Leave it in the scratch project and do not migrate it.
- The mesh is assigned as the movement root. Reattach the mesh to the sphere and set the updated component back to `RequiredCollisionRoot`.
- A Niagara system or Blueprint writes velocity. Delete that binding. Presentation is read-only.
- The visual branch is bad. Delete the branch. `5.8` `main` and `feature/0008-newtonian-flight` stay as they were.
- Scratch project is disposable. Deleting `K:\UnrealProjects\EdenVisualScratch` does not affect Eden.

## Progress log

- 2026-10-13: Plan drafted from the sandbox audit and a local library search. No Fab purchases were visible on disk. No assets imported. `EdenSpaceSimulator 5.8` was not modified.
- 2026-10-13: User library screenshot reviewed. About 56 owned packs, none of them a spacecraft, station, starfield, or Niagara space FX. Closest secondary material is Megascans rock and the high-tech / storage / workshop prop packs. Star Sparrow is still not owned.
- 2026-10-13: Empty evaluation project created at `K:\UnrealProjects\EdenVisualScratch` (UE 5.8, no game modules). Vault packs have not been added. Star Sparrow's Fab listing still describes itself as a free promotional sample; price and license were not confirmed, and the listing says AI use is not allowed. No Fab or Meshy download was made.
- 2026-10-13: User confirmed High-Tech Pack 1, Quarry Collections, and Lava Field Collections are not compatible with Unreal 5.8. Those packs are dropped from this slice. Installed engines elsewhere are UE 5.0 and 5.5 on `C:`; Eden stays on 5.8.
- 2026-10-13: User added the free Star Sparrow sample to `EdenSpaceSimulator 5.8/Content/StarSparrow` (40 example meshes, 8 modules, 33 MB). Presentation hull `SM_StarSparrow01` is applied in `AEdenSpacecraftPawn::ApplyPresentationHull` on that copy only. Sphere collision root and flight integration are unchanged. Editor Win64 Development build succeeded. The Newtonian branch does not have this content yet.

## Decision log

- 2026-10-13: Presentation reads delivered force and torque. Speed is an instrument only.
- 2026-10-13: Planet and moon are scenery in this slice. Collision, gravity, and docking wait.
- 2026-10-13: Arghanion is a look reference, not a flight dependency.
- 2026-10-13: Meshy and Quixel dressing are follow-on work, not part of the first playable picture.

## Acceptance evidence

None. This plan is still a draft.

## Handoff

Do not create the scratch project, cut `feature/0009-visual-slice`, or import packs until this draft is approved.

Before Milestone 1, a human still needs to PIE `feature/0008-newtonian-flight` in the workspace project (the folder without `5.8` in the path) and confirm assist-off drift, assist-on settle, and no thrust from an empty tank.

Next clean task after approval: Milestone 0, with the user signed into Fab so Star Sparrow can be confirmed in the library.
