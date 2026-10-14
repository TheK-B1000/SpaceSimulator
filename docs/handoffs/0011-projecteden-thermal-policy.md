# Handoff — ProjectEden thermal policy for Solar Crisis v2

**For:** the ProjectEden session, which owns `B:\repo\ProjectEden`. Make the policy changes in that repository on its own branch, with independent tests. Do not weaken safety validation to improve LLM acceptance.
**From:** SpaceSimulator, `feature/0011-solar-crisis-v2` @ `faf11b5` (ExecPlan 0011, revision 2, Checkpoints A–E).
**Status:** the SpaceSimulator side is implemented. The ProjectEden policy is **incompatible** with the new physics, and live v2 acceptance is blocked on it.

## 1. What changed in the simulator

### Thermal modes (baseline A2)

The ship's baseline heating is 1.0 °C/s and its passive radiation 0.5 °C/s.

| Mode | Active (powered) cooling | Electrical demand | Total cooling, full power | Net, no flare |
| --- | --- | --- | --- | --- |
| Off | 0 | 0 kW | 0.5 °C/s (passive only) | +0.5 °C/s |
| Nominal | +0.5 °C/s | 0.5 kW | 1.0 °C/s | 0.0 |
| Boost | +1.0 °C/s (replaces Nominal) | 1.5 kW | 1.5 °C/s | −0.5 |
| Emergency | +2.0 °C/s (replaces Nominal) | 4.5 kW | 2.5 °C/s | −1.5 |

- Off does **not** generate heat. It removes the powered cooling, so baseline heating exceeds passive cooling.
- Load shed: −2 kW of demand and −0.4 °C/s of cooling, and it disables Flight Assist (unchanged from v1).

### Power bus (E1, Solar Crisis v2 only; the default bus is unlimited)

- Capacity = 2 kW generation + 6 kW battery discharge = **8 kW** while the battery holds charge, and **2 kW** when it is empty.
- Allocation order: essential baseline (1 kW), then thermal control, then auxiliary (mission/external load, after load shed), then charging. Load shed removes auxiliary load first; any excess comes off the essential load.
- **Cooling authority** = served thermal-control kW ÷ requested kW. Effective dissipation = passive + operator cooling − active × (1 − authority). Passive cooling and the load-shed penalty never depend on power.

## 2. Policy defects (file references at ProjectEden `e3a316a`)

1. `eden_api/infrastructure/advisory/thermal_safety_analysis.py` — `COOLING_MODES = {"Boost", "Emergency"}`. Nominal is powered cooling and an appropriate low-cost mitigation; Off is passive only. There is no "return to Nominal" action, so with Off reported EDEN escalates to Boost, which costs 3× the power of the cheapest fix.
2. Same file, `TRADEOFFS["thermal_control_emergency"]` = "Emergency adds at least as much cooling and electrical demand as Boost", together with the Boost → Emergency escalation ladder. **This is false under power constraints** (see §4, cases C and D): with an empty battery, Emergency delivers *less* cooling than Boost, and less than Nominal.
3. Recommendations are a fixed hierarchy of mode names. They must instead be chosen from measured and predicted thermal response under the power actually available.
4. Load shed: the simulator now allocates whatever power is genuinely freed, so in one narrow case load shed slightly improves cooling (§4, case C). **Policy must remain**: load shed is a power-management action with potentially negative thermal consequences, and it must not be recommended for cooling without separately verified safety justification.

## 3. What Unreal sends today, and what is missing

Already on the wire (rich context): `heatGenerationDegreesCelsiusPerSecond`, `externalHeatingRateDegreesCelsiusPerSecond`, and **`effectiveDissipationDegreesCelsiusPerSecond`**. That last one is authoritative: it already includes A2 and power limiting, so the *current* thermal balance computed by `thermal_diagnostics.py` stays correct. Also sent: `thermalControlMode`, `loadShedMode`, `batteryFraction`, `powerGenerationKilowatts`, `powerDemandKilowatts` (the requested total) and `netPowerKilowatts` (generation minus *delivered* load).

Missing, so EDEN cannot evaluate *alternative* actions or explain power-limited cooling: cooling authority, bus capacity/limit, thermal-control demand, and auxiliary requested/curtailed load.

**Proposed contract addition** (Unreal side, additive; needs reviewer approval before implementation):
- Bus fields: `coolingAuthority`, `busLimited`, `busCapacityKilowatts`, `thermalControlDemandKilowatts`, `activeCoolingDegreesCelsiusPerSecond`, `auxiliaryRequestedKilowatts`, `auxiliaryCurtailedKilowatts`.
- **`coolingOptions`** (recommended): for each mode (Off, Nominal, Boost, Emergency), with and without load shed, the predicted effective dissipation, net rate and authority, computed by Unreal's own `FEdenPowerModel::AllocateBus` and `FEdenThermalModel::WithCooling`. EDEN then compares authoritative predictions without duplicating the physics.

**Interim rule until those fields exist:** when `batteryFraction` is at or near 0, or the effect of a higher mode cannot be established, EDEN must **not** recommend a higher cooling mode. It should state that cooling may be power-limited and escalate to the operator.

## 4. Regression cases (expected physics from the shipped equations)

Sandbox values: 2 kW generation, 1 kW essential, 6 kW discharge limit. The flare is +4.25 °C/s with a 5 kW auxiliary load. Net rates are above ambient.

| Case | Condition | Off | Nominal | Boost | Emergency | Required EDEN behaviour |
| --- | --- | --- | --- | --- | --- | --- |
| A | Charged, no flare, warming in Off | +0.500 | **0.000** | −0.500 | −1.500 | Offer **Nominal** as the low-cost fix (0.5 kW). Do not say Off generates heat. |
| B | Charged, flare | +4.750 | +4.250 | +3.750 | **+2.750** (auxiliary 2.5 of 5 kW curtailed) | Emergency cools most here but state that it curtails auxiliary load. Whether any mode keeps T ≤ 100 °C depends on time remaining; predict it, do not assume. |
| C | **Empty battery**, no flare | +0.500 | 0.000 | **−0.167** (authority 67 %) | **+0.056** (authority 22 %; *the ship heats*) | Must not recommend or keep Emergency as "more cooling". Boost is best, and Nominal beats Emergency. Load shed with Emergency gives +0.011 (authority 44 %): still worse than Nominal or Boost, so never recommend it for cooling. |
| D | **Empty battery**, flare | +4.750 | +4.250 | **+4.083** | +4.306 | No option stops the rise. Escalate ("no safe recovery option") with the predicted time to the limit; never present Emergency as the strongest response. |
| E | Load shed, charged | net +0.4 worse in every mode (e.g. Nominal 0.000 → +0.400; flare Boost +3.750 → +4.150) | | | | Never a thermal mitigation; may be discussed as electrical relief with its cooling penalty stated. |

The case D Emergency figure (+4.306 °C/s) is also asserted by the SpaceSimulator integration test `Eden.Integration.SolarCrisisV2.B.CoolingFollowsActualPowerAvailability`.

Constraints that must hold in every case:
- deterministic fallback, validation, and human authorization are unchanged;
- advisory-only mode;
- no command execution;
- numbers cited must match the authoritative telemetry.

## 5. Live Solar Crisis v2 validation procedure (after the ProjectEden fix)

**Prerequisites:**
- The ProjectEden policy fix is on its own branch with the §4 regression cases passing.
- The contract addition in §3 is approved and implemented, or the interim rule is in force.
- The memory check in `docs/testing/phase5-integrated-playtest.md` §0 passes.

**Run:**
1. Start ProjectEden in **advisory-only** mode.
2. Open SpaceSimulator on `feature/0011-solar-crisis-v2`.
3. In PIE, run `EnableEdenOs`, then `SetEdenOsBearerFromEnv`. The JWT comes from an environment variable and is never pasted or logged.
4. Run these scenarios with `StartSolarCrisisV2`, recording the HUD, the EDEN panel and `ShowAfterAction` for each:
   1. **Idle in Off before start** → EDEN offers Nominal (case A).
   2. **Passive through the flare** → EDEN recommends added cooling, states the trade-offs, and predicts the 100 °C crossing near T = 29 s.
   3. **Emergency at T = 10 s** → EDEN reports the 2.5 kW auxiliary curtailment and does not call it fully powered (case B).
   4. **Boost in the flare, then Nominal in recovery** → EDEN flags that recovery is not progressing before the T = 100 s deadline.
   5. **Power-constrained check:** during recovery, run `EdenSetBatteryFraction 0` with Emergency selected. This is an advisory test only: the mission's battery floor fails at once. EDEN must not claim Emergency is the strongest cooling (case C) and must not recommend load shed for cooling.

**Accept only if:**
- every recommendation improves the predicted net rate, or escalates when none can;
- no load shed or reduced propulsion is offered as thermal relief;
- the numbers EDEN cites match the HUD and telemetry;
- no command is executed.
