# Review Standard

## Review output format

Report findings first, ordered by severity:

- **Critical:** data loss, corruption, security exposure, crash, unrecoverable asset damage
- **High:** incorrect behavior, broken state ownership, serious lifetime defect, architecture violation, major regression
- **Medium:** maintainability, performance, error-handling, or test gap likely to create defects
- **Low:** localized clarity, consistency, or non-blocking improvement

Each finding should include:

- Severity
- File and line or asset path
- Confirmed behavior or risk
- Why it matters
- Recommended correction
- Missing evidence, when applicable

Then include:

- Open questions
- Validation performed
- Residual risks
- Summary

## Correctness checklist

- Acceptance criteria are implemented.
- Edge cases and invalid input are handled.
- State transitions are valid and observable.
- Reset, restart, and repeated execution behave correctly.
- Numerical operations guard against invalid ranges, NaN, and division by zero.
- Units and coordinate spaces are explicit.
- Expected failure is not handled as an invariant crash.
- No success is inferred merely from compilation.

## State ownership checklist

- Every mutable state value has one authoritative owner.
- No UI widget duplicates domain truth.
- No Blueprint and C++ implementation independently own the same rule.
- Cross-system changes use commands or an orchestrator.
- Cached derived state has an invalidation strategy.
- Telemetry is observational.
- External adapters cannot mutate domain internals.

## Unreal lifetime checklist

- UObject references that require GC tracking use appropriate reflected properties.
- Raw pointers do not outlive their owners.
- Delegates are unbound or lifetime-safe.
- Timers and async callbacks cannot invoke destroyed objects.
- World, game instance, actor, component, and local-player lifetimes are respected.
- Subsystems use the correct scope.
- Constructors do not perform unsafe world-dependent work.
- Editor-only code is guarded appropriately.
- Blueprint-exposed APIs have safe categories, metadata, and access.

## C++ and dependency checklist

- Epic naming and formatting conventions are followed.
- Public API appears before private implementation.
- Headers use `#pragma once`.
- Includes are precise and physical coupling is minimized.
- No global `using` declarations.
- Module dependencies are minimal and justified.
- No circular dependency exists.
- Public headers do not leak private implementation types.
- Class responsibilities remain cohesive.
- Inheritance is substitutable and justified.
- Interfaces are focused.
- High-level policy does not depend on UI or transport details.
- DRY is applied to domain knowledge without premature abstraction.

## Blueprint and asset checklist

- Core rules are not hidden in Level Blueprints or widgets.
- Asset names and folders follow conventions.
- Hard references are justified.
- Asset moves have redirector and source-control plans.
- Default values are safe.
- Blueprint graphs remain readable.
- Binary changes are accompanied by screenshots, notes, or reproducible manual steps.
- No authored asset was regenerated accidentally.

## Simulation checklist

- Fixed-step systems are independent from render frame rate.
- Catch-up and overrun behavior are bounded.
- Reset clears all owned runtime state.
- Deterministic calculations are tested where intended.
- Flight and resource update cadences are not conflated.
- High-frequency paths avoid unnecessary allocation and logging.
- Mission conditions are explicit, not inferred through UI.

## Error handling and observability checklist

- Failures include actionable context.
- State transitions are logged at appropriate levels.
- Repeated per-frame spam is absent.
- No committed `LogTemp` remains.
- External calls define timeout and failure behavior.
- Expected offline behavior is explicit.
- Test and build failures return nonzero status.
- Secrets and personal data are absent from logs.

## Testing checklist

- New behavior has relevant tests.
- Defect fixes include regression tests where practical.
- Tests assert meaningful outcomes, not implementation trivia.
- Test names use the `Eden.*` hierarchy.
- Tests reset state and do not depend on execution order.
- Manual editor checks are documented for behavior automation cannot cover.
- Build and test output corresponds to the final diff.

## Documentation and recovery checklist

- `README.md` commands remain accurate.
- `docs/ARCHITECTURE.md` matches implementation.
- State ownership changes are recorded.
- ADRs exist for durable architecture decisions.
- `docs/REMEMBER.md` contains only verified durable facts.
- `docs/RECOVER.md` records the latest safe checkpoint.
- Active ExecPlan reflects actual progress and deviations.
- No critical design exists only in chat.

## Deletion test

For any new component:

1. Predict what breaks if it is removed.
2. Inspect actual dependencies.
3. Compare prediction to reality.
4. Refactor or document if the blast radius is surprising.

A surprising blast radius is an architecture finding, not merely a documentation issue.
