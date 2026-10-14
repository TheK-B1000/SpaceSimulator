# Exports what ProjectEden received and answered during a Start-EdenPieSession run.
# Reads only the run's temp SQLite database. No credentials are read or written.
param(
    [Parameter(Mandatory = $true)][string]$RunRoot,
    [string]$ProjectEdenRoot = "B:\repo\ProjectEden"
)
$ErrorActionPreference = "Stop"

$Db = Join-Path $RunRoot "projecteden-pie.sqlite"
if (-not (Test-Path $Db)) { throw "No PIE database at $Db" }
$ApiRoot = Join-Path $ProjectEdenRoot "packages\api"
$Python = Join-Path $ApiRoot ".venv\Scripts\python.exe"
$Out = Join-Path $RunRoot "pie-evidence.json"

$Script = @'
import json, sqlite3, sys

RICH = [
    "windowDurationSeconds", "thermalWarningCelsius", "thermalCriticalCelsius",
    "thermalAmbientCelsius", "thermalAbsoluteMaxCelsius", "heatGenerationDegreesCelsiusPerSecond",
    "effectiveDissipationDegreesCelsiusPerSecond", "externalHeatingRateDegreesCelsiusPerSecond",
    "thermalControlMode", "loadShedMode", "propulsionPriority", "powerGenerationKilowatts",
    "powerDemandKilowatts", "netPowerKilowatts",
]
db, out = sys.argv[1], sys.argv[2]
con = sqlite3.connect(db)
con.row_factory = sqlite3.Row
tables = {r[0] for r in con.execute("select name from sqlite_master where type='table'")}
rows = con.execute(
    "select created_at, session_id, evaluation_id, simulation_time_seconds, "
    "context_snapshot_simulation_time_seconds, trigger_reasons, context, recommendation, "
    "rationale, reasoner_kind from mission_advisories order by id"
).fetchall() if "mission_advisories" in tables else []
advisories = []
for r in rows:
    ctx = json.loads(r["context"])
    advisories.append({
        "receivedAt": r["created_at"],
        "sessionId": r["session_id"],
        "evaluationId": r["evaluation_id"],
        "simulationTimeSeconds": r["simulation_time_seconds"],
        "contextSnapshotSimulationTimeSeconds": r["context_snapshot_simulation_time_seconds"],
        "triggerReasons": json.loads(r["trigger_reasons"]),
        "richFieldsPresent": [k for k in RICH if k in ctx],
        "richFieldsMissing": [k for k in RICH if k not in ctx],
        "context": ctx,
        "reasonerKind": r["reasoner_kind"],
        "recommendation": r["recommendation"],
        "rationale": r["rationale"],
    })
proposals = (
    con.execute("select count(*) from mission_command_proposals").fetchone()[0]
    if "mission_command_proposals" in tables else 0
)
report = {"advisoryCount": len(advisories), "commandProposalCount": proposals, "advisories": advisories}
open(out, "w", encoding="utf-8").write(json.dumps(report, indent=2))
for a in advisories:
    c = a["context"]
    print(f'{a["receivedAt"]} t={a["simulationTimeSeconds"]:.1f}s {a["evaluationId"]} '
          f'T={c.get("temperatureCelsius")} mode={c.get("thermalControlMode")}/'
          f'{c.get("loadShedMode")}/{c.get("propulsionPriority")} rich={len(a["richFieldsPresent"])}/14 '
          f'[{a["reasonerKind"]}] {a["recommendation"]}')
print(f"advisories={len(advisories)} commandProposals={proposals} -> {out}")
'@
$ScriptPath = Join-Path $RunRoot "export_pie_evidence.py"
$Script | Out-File -Encoding utf8 $ScriptPath
& $Python $ScriptPath $Db $Out
if ($LASTEXITCODE -ne 0) { throw "Evidence export failed." }
