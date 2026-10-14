# Phase 5 — Integrated playtest procedure

**Scope:** the integrated feature branch only. Checkpoint M is validated separately (see the end of this document) and is not part of this pass.

| | |
| --- | --- |
| Project | `K:\UnrealProjects\SpaceSimulator\EdenSpaceSimulator\EdenSpaceSimulator.uproject` |
| Branch / commit under test | `feature/0009-visual-flight-integration` @ `e0f8c96` |
| **Not** this project | `K:\UnrealProjects\SpaceSimulator\EdenSpaceSimulator 5.8\…` (Checkpoint M baseline copy with the original visual commits) |
| Map | `/Game/Eden/Maps/L_FlightSandbox` |
| Automated baseline | 396/396 `Eden.*` tests; determinism references in §11 |
| Out of scope | `feature/0010-h1-rich-advisory-context` (H.1 rich advisory). This pass validates flight and presentation; live rich-advisory acceptance is a separate requirement. |

Mark each check **Pass / Fail / Not tested** and add a note on every Fail. Automated results do not substitute for any check below.

---

## 0. Before launching (low-memory configuration)

### What actually crashed

`Fatal error: PipelineStateCache.cpp … Shader compilation failures are Fatal` comes after `D3D12RHI: Failed to create pipeline state … error 8007000e` (E_OUTOFMEMORY). Both crashes so far (the scripted capture on 2026-10-13 and the Editor launch at 09:06 on 2026-10-14) happened during start-up pipeline creation, before any EDEN code renders.

The limiting resource was **system commit memory, not GPU memory**. At the 09:06 launch the Editor log records `Virtual Memory: 76371 MB used, 2723 MB free, 79095 MB total`, while the Editor itself was using only 0.6 GB. The NVIDIA driver allocates system commit memory when it compiles pipeline states, so it failed first. The commit limit is RAM (48 GB) plus a fixed 30 GB page file on `K:`.

Large commit holders measured afterwards:

| Process | Commit |
| --- | --- |
| SeaGuard `eval_exploitability.py` (1 parent + 13 workers, since 05:02) | ≈ 23 GB |
| Cursor (38 processes) | ≈ 6.3 GB |
| Brave (26) | ≈ 6.1 GB |
| ProjectEden `evaluate_local_advisory.py` + Ollama `llama-server` (qwen3:4b, started 09:07) | ≈ 4.5 GB, plus ≈ 3 GB VRAM |
| ChatGPT/Codex, WebView2, Discord, Teams, Slack | ≈ 7 GB together |

The 09:06 log also says `Process is running as part of a Windows Job with separate resource limits`. Launch the Editor from Explorer or the Epic Games Launcher, not from a terminal or IDE task, to rule out an inherited job limit.

### Checklist

1. **Protect the research runs; do not kill them.** Let the SeaGuard exploitability evaluation finish, or stop it only through a mechanism you have confirmed resumes without losing progress or invalidating results. Do the same for any ProjectEden evaluation: let it finish or use its documented shutdown. Ollama keeps a model loaded after a run; once no evaluation needs it, `ollama stop <model>` releases it (about 4 GB commit and 3 GB VRAM).
2. Close every other Unreal instance, including the `EdenSpaceSimulator 5.8` Editor and any leftover `CrashReportClientEditor`. Run one Unreal instance only.
3. Close unnecessary applications: browsers, Cursor, Wallpaper Engine.
4. Measure **both** memories. Practical starting targets, not guaranteed requirements: **≥ 15 GB** free physical RAM, **≥ 15 GB** commit free and about **6 GB** free VRAM:

   ```powershell
   $os = Get-CimInstance Win32_OperatingSystem
   'physical free {0:N1} GB' -f ($os.FreePhysicalMemory/1MB)
   'commit free {0:N1} GB of {1:N1} GB' -f ($os.FreeVirtualMemory/1MB), ($os.TotalVirtualMemorySize/1MB)
   nvidia-smi --query-gpu=memory.used,memory.free --format=csv
   ```

5. Optional, a system setting and your decision: make the page file system-managed, or raise it to about 48 GB. This raises the commit limit without changing the project.
6. **Verify the checkout before opening the project.** The folder is shared with `feature/0010`, so the path alone does not identify the build. On 2026-10-14 the workspace was on `feature/0010-h1-rich-advisory-context` @ `c9bdf89`, with the module DLL built at 09:21 from that work. Coordinate with whoever owns the `0010` work first, then, with the Editor closed:

   ```powershell
   $r = "K:\UnrealProjects\SpaceSimulator\EdenSpaceSimulator"
   git -C $r status --short          # only untracked docs expected; commit or stash anything else first
   git -C $r switch feature/0009-visual-flight-integration
   git -C $r rev-parse --short HEAD  # must print e0f8c96
   ```

   Then rebuild the module for `e0f8c96`. Either accept the Editor's "rebuild modules" prompt, or run `Build.bat EdenSpaceSimulatorEditor Win64 Development -Project=<uproject> -WaitMutex` before launching. Otherwise the Editor may load the `0010` binaries. Afterwards, `git switch feature/0010-h1-rich-advisory-context` returns the folder to its previous state (rebuild again before using it).
7. The first launch after a branch switch may compile shaders, which runs `ShaderCompileWorker` processes. Let it finish before pressing Play.

### Editor settings that do not change flight behaviour

These are per-user Editor settings (stored under `Saved/`, never committed):

- **Settings ▸ Engine Scalability Settings:** Medium (Low if it still fails).
- **Play ▸ Selected Viewport** for the main pass. Use New Editor Window (PIE) only for the one HUD check that requires it, at 1280×720.
- **Editor Preferences ▸ Performance:** enable *Use Less CPU when in Background*. Keep only one level viewport open, with Realtime off in any others.
- Optional console command for extra headroom: `r.ScreenPercentage 75`.

Lumen is already disabled (`r.DynamicGlobalIlluminationMethod=0`). Do not change the graphics RHI (DX12) or delete assets as a fix. If you want to try `-dx11` as a diagnostic, record that the result came from a different RHI.

**Why this preserves flight behaviour:** flight integrates at a fixed 120 Hz independent of frame rate. The harness verifies identical results at 30/60/144/240 fps and with jittered frames. Lower scalability or resolution changes only presentation. The single exception is a stall longer than 0.5 s in one frame: steps beyond 60 per update are dropped, *counted* and reported (`showdebug` ▸ Flight: `Dropped=`). If `Dropped` is non-zero, note it with the result.

---

## How to use the console

Press **`** (the key left of `1`, also `~`) during Play to open the console. Type a command and press Enter. Commands used below:

| Command | Effect |
| --- | --- |
| `EdenHudDiagnostics` | Logs the HUD root, viewport attachment, visibility, size and panel count |
| `EdenSetFuelKilograms 5` | Sets stored propellant through the fuel system |
| `EdenSetBatteryFraction 0.05` | Sets battery charge (0.05 → Critical, 0 → Depleted) |
| `EdenCenterMouseStick` | Re-centres the mouse virtual stick immediately |
| `StartMission` / `RestartMission` / `AbortMission` | Mission control |
| `showdebug` | Toggles the engine debug overlay (Flight / Attitude / Propulsion lines) |
| `EdenCaptureScreenshot 3 0` | Screenshot with HUD in 3 s, saved to `Saved/Screenshots/` (`1` instead of `0` quits afterwards) |

Controls: W/S forward/back, A/D left/right, Space/LCtrl up/down, mouse pitch/yaw, Q/E roll, **X** flight assist, **T** thermal mode, **L** load shed, **P** propulsion priority.

---

## 1. HUD visibility

- [ ] In Selected Viewport: five panels are visible — FLIGHT (top-left), MISSION (top-centre), SYSTEMS (top-right), OPERATOR (bottom-left), EDEN (bottom-right).
- [ ] Text is readable at your normal viewport size, and nothing is clipped. FLIGHT now has a `STICK` line.
- [ ] In New Editor Window (PIE): run `EdenHudDiagnostics`. Expected: `inViewport=true`, `panelsBuilt=5/5`, **non-zero** hud size.
- [ ] Unavailable values show `--`, never a misleading `0`.

## 2. Newtonian drift (translation, assist off)

Make sure FLIGHT shows `ASSIST    OFF  (momentum conserved)` (press **X** if needed).

- [ ] Hold W for about 3 s and release. SPEED stays constant after release; the ship keeps drifting.
- [ ] Tap S. Speed falls only by the reverse impulse and does not snap to zero.
- [ ] Strafe with A/D and then yaw away. The velocity direction (TRAVEL “deg off the nose”) is unchanged by rotating.

## 3. Angular momentum (rotation, assist off)

The required behaviour: **releasing input stops the torque request, not the rotation.**

- [ ] Hold E for 1 s and release. The roll RATE on the HUD stays constant; the ship keeps rolling, and `STICK roll` returns to `+0.00`.
- [ ] Hold Q until the roll rate returns to about 0. Only opposite torque stops it.
- [ ] Mouse: pitch up and let go. The `STICK` value centres over about 2 s (auto-centre). The pitch RATE stops *increasing* but does not return to zero. Run `EdenCenterMouseStick` and confirm the same thing.
- [ ] Combine pitch and yaw, then release. The ship keeps tumbling with no drift to a stop.

## 4. Flight assist on (X)

- [ ] **X** shows a confirmed banner, and FLIGHT shows `ASSIST    ON   (thrusters hold velocity/rate)`, or `ON - UNAVAILABLE (load shed)`.
- [ ] With velocity and rotation present, assist brakes both using available thrust and torque, not instantly. Expect seconds; at about 29 °/s², stopping 60 °/s of pitch takes about 2 s.
- [ ] Propellant falls while assist is braking (assist thrust is real thrust).
- [ ] **X** again returns to `OFF  (momentum conserved)`, and momentum is conserved from that point.

## 5. Mouse response

- [ ] Small mouse movements give small, proportional rate commands. There are no full-stick jumps and no stutter when the mouse stops.
- [ ] Larger movements reach full deflection (`STICK ±1.00`) progressively, at about 83 counts.
- [ ] Pitch direction feels correct (if not, note it; `MouseStickSettings.bInvertPitch` on the controller is the fix).
- [ ] Note any axis that feels sluggish. **No torque change will be made before this result is reviewed.**

## 6. Camera framing

- [ ] At rest, the whole spacecraft is in frame, slightly below centre.
- [ ] Pitching (including past ±90°), yawing and rolling keep the whole ship in frame. The camera trails a little and never flips or pops.
- [ ] Under sustained acceleration and at high speed, the ship stays in frame and the lag does not grow with speed.
- [ ] The station is visible and recognisable when you fly towards it. Note readability and lighting.

## 7. Thruster visuals

The main-engine effect is disabled by default (`bEnableThrusterEffects`, pending exactly this validation). For this check, open `BP_EdenSpacecraftPawn` ▸ Class Defaults ▸ Eden|Presentation, tick **Enable Thruster Effects**, then Compile. **Do not save the Blueprint**: the approved build must not change merely to perform validation. If it is saved by accident, record that and revert it with `git restore Content/Eden/Blueprints/BP_EdenSpacecraftPawn.uasset`.

- [ ] Forward thrust (W) lights the main engine, and the field of view widens slightly.
- [ ] **Pure rotation** (mouse or Q/E only, assist off) does **not** light the main engine.
- [ ] Reverse (S) and strafing do not light it.
- [ ] `EdenSetFuelKilograms 0`, then W: no flame and no acceleration.
- [ ] Note any Niagara errors or performance drop when the effect is enabled.

## 8. Fuel and power constraints

- [ ] Propellant kg falls during a burn, and flow kg/s is shown. Mass decreases.
- [ ] `EdenSetFuelKilograms 0.5`, then W: thrust stops when the tank empties, and the SYSTEMS state goes to Depleted.
- [ ] `EdenSetFuelKilograms 50`, then `EdenSetBatteryFraction 0.05`: power Critical, delivered thrust about 40 % of requested; the HUD shows delivered vs requested and the limit flag.
- [ ] `EdenSetBatteryFraction 0`: power Depleted, delivered thrust 0. (Known: recovery from Depleted to Critical happens within about one tick; then the ship stays at 40 % for a long time. That is recorded, not a new failure.)
- [ ] The SYSTEMS panel tint follows the worst state (normal / warning / critical).

## 9. Operator controls

- [ ] **T** cycles the thermal mode; the banner and OPERATOR panel show the confirmed mode (or the real rejection reason).
- [ ] **L** toggles load shed with a confirmed state.
- [ ] **P** toggles propulsion priority, and the displayed authority changes accordingly.
- [ ] No “cannot set operator demand / dissipation” warnings at PIE start (the startup-ordering fix).

## 10. Mission progression

- [ ] `StartMission`: MISSION shows Solar Crisis, its phase and the current objective.
- [ ] The objective and phase advance as the scenario progresses. Alerts appear in the MISSION panel.
- [ ] `RestartMission` resets it cleanly; `AbortMission` ends it.
- [ ] EDEN panel: shows the real link state. With EDEN transport not enabled, it shows `OFFLINE  (transport disabled - console: EnableEdenOs)` and `ADVISORY   none received`, with no invented advisories.

## 11. Deterministic regression (automated)

With no other Unreal instance running:

```powershell
& "K:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "K:\UnrealProjects\SpaceSimulator\EdenSpaceSimulator\EdenSpaceSimulator.uproject" `
  -Unattended -NoSplash -NullRHI -NoP4 "-ExecCmds=Automation RunTests Eden.; Quit" `
  "-TestExit=Automation Test Queue Empty" -Log -AbsLog="K:\UnrealProjects\SpaceSimulator\EdenSpaceSimulator\Saved\Automation\phase5-suite.log"
```

- [ ] 396/396 pass.
- [ ] The log contains, unchanged:
  - `60 s profile final: position X=3235.959 Y=-261.001 Z=-246.422`
  - `Depletion profile final: velocity X=10.889 Y=12.016 … |w| 4.797122 rad/s, propellant-limited steps 6001`
  - `Tumble profile final: orientation P=44.458903 Y=60.225946 R=31.667263`

## 12. Evidence capture

- [ ] One screenshot (`EdenCaptureScreenshot 3 0`, or a short recording) in PIE. It should show the whole spacecraft in frame and the HUD with real velocity, propellant, power and the flight-assist state.

---

## Results template

```
Phase 5 quick playtest — <date>
Checkout: branch __ @ __ (must be feature/0009-visual-flight-integration @ e0f8c96); module rebuilt after switch: y/n
Memory before launch: commit free __ GB, VRAM free __ GB; scalability __; PIE mode __
1 HUD ............. Pass/Fail/Not tested — notes
2 Drift ........... 
3 Angular momentum  
4 Assist .......... 
5 Mouse ........... 
6 Camera .......... 
7 Thruster visuals  (Blueprint change saved? y/n)
8 Fuel/power ...... 
9 Operator ........ 
10 Mission ........ 
11 Regression ..... 
12 Evidence ....... file:
Dropped steps seen: y/n
Overall: does the flight model feel enjoyable while still behaving physically correctly? __
Observations on mouse sensitivity, rotation speed, flight assist and camera framing: __
```

## Solar Crisis v2 (ExecPlan 0011) — human PIE, pending

Branch `feature/0011-solar-crisis-v2`. v1 stays the default; start v2 with the console command `StartSolarCrisisV2` (or `EdenStartMission /Game/Eden/Data/Missions/DA_SolarCrisisV2`; `EdenStartMission default` returns to v1). Optional: `EdenHudScale 1.5`.

- [ ] Idle in Nominal before starting: temperature holds (A2). With **T** set to Off it drifts up about 0.5 °C/s. Starting v2 resets it to 20 °C regardless; battery, fuel and the selected mode are not reset.
- [ ] Passive (no input): fails at about T = 29 s, with MISSION showing `PreventOverheating failed: temperature ... exceeded 100 C`.
- [ ] Boost (**T** to Boost) at T = 10 s: peak about 95 °C. In recovery MISSION shows `stable x/5.0 s  deadline T=100 s`. Succeeds at about T = 85 s.
- [ ] Emergency at T = 10 s: peak about 75 °C. SYSTEMS shows `BUS cap 8.0 kW  cooling 100%  aux 2.5/5.0 kW  CURTAILED 2.5 kW` during the flare. Succeeds at about T = 38 s.
- [ ] Boost through the flare, then back to Nominal: temperature stays flat at about 95 °C and the mission fails at T = 100 s with `recovery not achieved`.
- [ ] Load shed only: fails earlier (about T = 27 s). Load shed does not help cooling.
- [ ] `ShowAfterAction` after each run: outcome, reason, objective times and reasons match the HUD; the Power Bus section says when auxiliary systems were not fully powered.
- [ ] The log shows the thermal balance and power-bus lines (`external heating changed`, `power bus curtailing load`) at T = 10 s and T = 30 s.

## Checkpoint M (separate, not part of this pass)

Checkpoint M is validated in its own session against its baseline (automated baseline `455c238`; the human gate uses the PIE checklist in ExecPlan 0007). Do not use this branch for it, and do not combine its results with this report. The Checkpoint A–D PIE checklist (ExecPlan 0008 §F.5) also remains required and pending.
