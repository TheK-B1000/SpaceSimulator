# Contributing to Eden Space Simulator

## Before starting work

1. Read `AGENTS.md`.
2. Read the mandatory project documents listed there.
3. Confirm the task has acceptance criteria.
4. Create or update an ExecPlan for significant work.
5. Start from a clean or understood working tree.

## Branch naming

Use short, descriptive branches:

```text
feature/flight-movement
feature/power-system
fix/thermal-overflow
refactor/telemetry-boundary
docs/architecture-state-ownership
```

## Commit messages

Use conventional, readable messages:

```text
feat(flight): add six-axis thrust command model
fix(thermal): clamp invalid heat dissipation
test(power): cover battery depletion transition
docs(architecture): record telemetry ownership
chore(repo): add Unreal validation script
```

Each commit should represent one coherent change.

## Pull request expectations

A pull request should include:

- Problem and intended outcome
- Design summary
- Acceptance criteria
- Screenshots or video for visual/editor changes
- Build and test evidence
- Manual verification steps
- Architecture or state-ownership impact
- Risks and rollback plan
- Documentation updates

Use `.github/PULL_REQUEST_TEMPLATE.md`.

## Coding expectations

- Follow Epic's C++ coding standard.
- Keep public interfaces small.
- Prefer composition over deep inheritance.
- Use Unreal object ownership and garbage collection correctly.
- Avoid hidden state changes.
- Validate input at boundaries.
- Keep simulation logic independent from UI and network adapters.
- Add logs for meaningful state transitions and failures.
- Add tests for deterministic domain behavior.
- Do not silence warnings without documenting why.

## Blueprint expectations

- Blueprints should compose C++ systems, tune values, bind presentation, and author content.
- Core simulation rules do not belong in Level Blueprints.
- Avoid giant Blueprint graphs. Split responsibilities into components, functions, or C++.
- Name assets using the prefixes documented in `README.md`.
- Add comments to non-obvious graph sections.
- Repair redirectors after intentional asset moves.
- Never rename or move large sets of assets without a dedicated plan and source-control checkpoint.

## Review expectations

Follow `docs/REVIEW.md`. Compilation is necessary but not sufficient.
