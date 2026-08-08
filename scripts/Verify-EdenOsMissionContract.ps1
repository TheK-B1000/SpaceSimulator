param(
    [string]$ProjectEdenRoot = "B:\repo\ProjectEden",
    [string]$RequestArtifact = "",
    [string]$PythonExe = ""
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($RequestArtifact)) {
    $RequestArtifact = Join-Path $RepoRoot "Saved\Automation\EdenOsMissionLifecycleRequests.json"
}
$RequestArtifact = (Resolve-Path $RequestArtifact).Path

if (!(Test-Path $ProjectEdenRoot)) {
    throw "ProjectEden root not found: $ProjectEdenRoot"
}

$ProjectEdenApiRoot = Join-Path $ProjectEdenRoot "packages\api"
if (!(Test-Path $ProjectEdenApiRoot)) {
    throw "ProjectEden API package not found: $ProjectEdenApiRoot"
}

if ([string]::IsNullOrWhiteSpace($PythonExe)) {
    $VenvPython = Join-Path $ProjectEdenApiRoot ".venv\Scripts\python.exe"
    if (Test-Path $VenvPython) {
        $PythonExe = $VenvPython
    } else {
        $PythonExe = "python"
    }
}

$Verifier = @'
import json
import asyncio
import sys
import tempfile
from pathlib import Path

from fastapi.testclient import TestClient
from sqlalchemy import create_engine
from sqlalchemy.ext.asyncio import async_sessionmaker, create_async_engine
from sqlalchemy.orm import sessionmaker

from eden_api.database.models import (
    Base,
    MissionEnvironmentEvent,
    MissionTelemetryPayload,
    SimulationRun,
    TelemetryState,
)
from eden_api.database.session import get_async_db, get_db, seed_db
from eden_api.main import app


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def expected_status(route_path: str, index: int) -> set[int]:
    if route_path == "/api/missions/sessions":
        return {201, 200}
    if route_path.endswith("/telemetry") or route_path.endswith("/events"):
        return {202}
    if route_path.endswith("/complete"):
        return {200}
    fail(f"unexpected route at index {index}: {route_path}")


def load_requests(path: Path) -> list[dict]:
    if not path.exists():
        fail(f"request artifact not found: {path}")
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, list) or not payload:
        fail("request artifact must contain a non-empty array")
    for index, item in enumerate(payload):
        if not isinstance(item, dict):
            fail(f"request {index} is not an object")
        if not isinstance(item.get("routePath"), str):
            fail(f"request {index} is missing routePath")
        if not isinstance(item.get("body"), dict):
            fail(f"request {index} is missing object body")
    return payload


def auth_headers(client: TestClient) -> dict[str, str]:
    email = "unreal-contract-verifier@eden.org"
    client.post("/api/auth/register", json={"email": email, "password": "securepassword123"})
    response = client.post("/api/auth/login", json={"email": email, "password": "securepassword123"})
    if response.status_code != 200:
        fail(f"login failed: {response.status_code} {response.text}")
    return {"Authorization": f"Bearer {response.json()['access_token']}"}


def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: verifier.py <request-artifact>")
    requests = load_requests(Path(sys.argv[1]))

    with tempfile.TemporaryDirectory(prefix="eden_contract_") as tmp:
        db_file = Path(tmp) / "projecteden_contract.db"
        engine = create_engine(
            f"sqlite:///{db_file.as_posix()}",
            connect_args={"check_same_thread": False},
        )
        Base.metadata.create_all(bind=engine)
        testing_session_local = sessionmaker(autocommit=False, autoflush=False, bind=engine)

        seed_session = testing_session_local()
        seed_db(seed_session)
        seed_session.close()

        async_engine = create_async_engine(
            f"sqlite+aiosqlite:///{db_file.as_posix()}",
            connect_args={"check_same_thread": False},
        )
        async_session_local = async_sessionmaker(bind=async_engine, expire_on_commit=False)

        def override_get_db():
            db = testing_session_local()
            try:
                yield db
            finally:
                db.close()

        async def override_get_async_db():
            async with async_session_local() as db:
                yield db

        app.dependency_overrides[get_db] = override_get_db
        app.dependency_overrides[get_async_db] = override_get_async_db
        client = TestClient(app)

        try:
            headers = auth_headers(client)
            for index, request in enumerate(requests):
                route_path = request["routePath"]
                response = client.post(route_path, json=request["body"], headers=headers)
                allowed = expected_status(route_path, index)
                if response.status_code not in allowed:
                    fail(f"{route_path} returned {response.status_code}, expected {sorted(allowed)}: {response.text}")

            db = testing_session_local()
            try:
                session_id = requests[0]["body"]["sessionId"]
                run = db.query(SimulationRun).filter_by(external_session_id=session_id).one_or_none()
                if run is None:
                    fail(f"no SimulationRun persisted for {session_id}")
                if run.origin != "mission_environment":
                    fail(f"unexpected run origin: {run.origin}")
                if run.run_status.name != "completed":
                    fail(f"expected completed run status, got {run.run_status.name}")
                if run.ended_at is None:
                    fail("completion did not persist ended_at")
                if run.alerts_count != 1:
                    fail(f"expected alerts_count 1, got {run.alerts_count}")

                telemetry_count = db.query(TelemetryState).count()
                payload_count = db.query(MissionTelemetryPayload).count()
                event_count = db.query(MissionEnvironmentEvent).count()
                expected_events = sum(1 for request in requests if request["routePath"].endswith("/events"))
                if telemetry_count != 1 or payload_count != 1:
                    fail(f"expected one telemetry row and payload, got {telemetry_count}/{payload_count}")
                if event_count != expected_events:
                    fail(f"expected {expected_events} events, got {event_count}")
            finally:
                db.close()
        finally:
            client.close()
            app.dependency_overrides.clear()
            engine.dispose()
            asyncio.run(async_engine.dispose())

    print(
        f"PASS: replayed {len(requests)} Unreal mission lifecycle requests through ProjectEden routes "
        f"and verified persisted session, telemetry, events, and completion."
    )


if __name__ == "__main__":
    main()
'@

Push-Location $ProjectEdenApiRoot
try {
    $TempVerifierPath = Join-Path ([System.IO.Path]::GetTempPath()) ("eden_mission_contract_verifier_{0}.py" -f ([System.Guid]::NewGuid().ToString("N")))
    Set-Content -Path $TempVerifierPath -Value $Verifier -Encoding UTF8
    try {
        & $PythonExe $TempVerifierPath $RequestArtifact
        if ($LASTEXITCODE -ne 0) {
            throw "ProjectEden mission contract verifier failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Remove-Item -LiteralPath $TempVerifierPath -Force -ErrorAction SilentlyContinue
    }
}
finally {
    Pop-Location
}
