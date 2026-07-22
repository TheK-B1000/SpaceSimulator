# Software Development Lifecycle

Eden Space Simulator uses a lightweight but explicit SDLC. The purpose is not ceremony. It is to prevent comprehension debt, accidental architecture, and unverifiable AI-generated work.

## Phase 0: Triage

Decide whether the task needs the full process.

Use an ExecPlan when the task:

- Changes multiple files or systems
- Introduces mutable state
- Alters architecture or dependencies
- Touches binary assets broadly
- Integrates an external service
- Has meaningful failure or rollback risk
- Is expected to take more than about 30 minutes

Small documentation corrections or isolated low-risk fixes may use a concise task contract instead.

## Phase 1: Discover

Before coding:

1. Read `AGENTS.md` and its mandatory documents.
2. Inspect the current branch and working tree.
3. Locate relevant code, assets, config, tests, and prior ADRs.
4. Draw or describe:
   - Components
   - Data flow
   - State owners
   - Failure points
   - Boundary and blast radius
5. Explain the design in plain language.

Exit condition: the engineer can explain the current system and intended change without guessing.

## Phase 2: Specify

Write acceptance criteria that are observable and testable.

The task contract must cover:

- Problem
- In scope
- Out of scope
- Constraints
- State ownership
- Feedback surfaces
- Failure modes
- Validation
- Rollback

Significant work uses an ExecPlan under `docs/exec-plans/`.

Exit condition: another engineer could determine whether the task is complete.

## Phase 3: Design

For every architectural decision:

- Identify at least two reasonable approaches.
- Explain tradeoffs.
- Confirm dependency direction.
- Confirm state ownership.
- Confirm failure handling.
- Confirm test strategy.
- Record durable decisions in an ADR.

Apply SOLID deliberately, not mechanically. Apply DRY to domain knowledge and validation, not to coincidental syntax.

Exit condition: the design fits `docs/ARCHITECTURE.md` or an approved ADR changes it.

## Phase 4: Implement

Implementation rules:

- Work in small, reviewable increments.
- Keep the project buildable at meaningful checkpoints.
- Add tests alongside behavior.
- Avoid unrelated cleanup.
- Use precise names and explicit units.
- Add actionable logs for state transitions and failures.
- Preserve user-authored assets.
- Update the ExecPlan as discoveries occur.
- Stop when assumptions are invalidated.

AI-generated code is treated as untrusted until inspected and explained.

Exit condition: the implementation is understandable and acceptance criteria appear satisfied.

## Phase 5: Verify

Required verification, as applicable:

1. Static repository checks
2. Win64 Development Editor build
3. Unit and integration automation tests
4. Functional or editor tests
5. Manual editor verification
6. Performance check for hot paths
7. Save/load or restart verification where state is involved
8. Git diff review

Use:

```powershell
.\scripts\Validate-Project.ps1 -Build -RunTests -EngineRoot $env:UE_ENGINE_ROOT
```

Do not claim a check passed if it was not run.

Exit condition: objective evidence supports the acceptance criteria.

## Phase 6: Review

Follow `docs/REVIEW.md`.

Review must examine:

- Correctness
- State ownership
- Lifetime and garbage collection
- Dependency direction
- Error and failure behavior
- Performance
- Tests
- Blueprint impact
- Asset safety
- Documentation
- Recovery

Exit condition: no unresolved critical or high-severity findings.

## Phase 7: Close and recover

Before ending the session:

- Update the ExecPlan status and progress.
- Add or update ADRs.
- Update `docs/REMEMBER.md` only for durable facts.
- Update `docs/RECOVER.md`.
- Record validation commands and results.
- Identify the next clean task.
- Ensure Git status is understood.

Exit condition: a fresh engineer or Codex session can resume without relying on chat history.

## Task states

```text
Backlog -> Ready -> In Progress -> Review -> Verified -> Done
```

Blocked work records:

- Blocking fact
- Evidence
- Owner or next action
- Safe repository state

## Definition of ready

A task is ready when:

- Problem and outcome are clear.
- Scope is bounded.
- Acceptance criteria exist.
- State ownership is understood.
- Dependencies and failure modes are identified.
- Validation is possible.
- Required assets or tools are available.

## Definition of done

A task is done when:

- Acceptance criteria pass.
- Build and tests pass or unverified items are explicitly documented.
- No architecture rule is violated.
- Logs and errors are actionable.
- Documentation is synchronized.
- Final diff is reviewed.
- Recovery information is current.
- The maintainer can explain the implementation.

## Rebuild versus refine

Recommend a rebuild when:

- State ownership is fragmented and cannot be repaired incrementally.
- Blast radius is unpredictable.
- Core behavior has no coherent spec.
- The architecture depends on hidden Blueprint or global state.
- Comprehension debt exceeds the value of preserving the implementation.

Recommend refinement when:

- Core ownership and boundaries are sound.
- Missing observability can be added safely.
- Tests can be added around understandable behavior.
- Large files can be separated without changing semantics.
