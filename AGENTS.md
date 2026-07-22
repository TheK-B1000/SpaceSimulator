# Codex Instructions for Eden Space Simulator

This file is the repository-level operating contract for Codex and other coding agents.

## Mandatory preflight

Before planning, editing, generating, deleting, renaming, or reviewing code, read these files in order:

1. `docs/IMPRINT.md`
2. `docs/REMEMBER.md`
3. `docs/PROJECT_SPEC.md`
4. `docs/ARCHITECTURE.md`
5. `docs/SDLC.md`
6. `docs/REVIEW.md`
7. `docs/RECOVER.md`
8. The active ExecPlan under `docs/exec-plans/`, when one exists
9. Relevant ADRs under `docs/decisions/`

Do not begin implementation until the task can be explained against the project spec, state ownership model, dependency rules, and success criteria.

## Working mode

- Inspect the repository before changing it.
- Preserve user-authored Unreal assets and configuration.
- Never delete or regenerate `.uasset`, `.umap`, `Config`, or `Source` content without an explicit task requirement and a recovery path.
- Do not make speculative cleanup changes unrelated to the requested task.
- Use an ExecPlan for multi-file features, architecture changes, migrations, risky refactors, or work expected to take more than about 30 minutes.
- Keep the working tree understandable. No code may remain that the maintainer cannot explain.
- Record important decisions in an ADR instead of burying them in chat or comments.
- Update `docs/RECOVER.md` before ending a substantial session.

## Engineering standards

### Unreal Engine

- Target Unreal Engine 5.8 unless `docs/REMEMBER.md` records an approved migration.
- Follow Epic's C++ coding standard.
- Use Unreal reflection, lifetime, and ownership conventions correctly.
- Use `UPROPERTY` for reflected UObject references that must participate in garbage collection.
- Prefer forward declarations in headers and precise includes in `.cpp` files.
- Use `TObjectPtr` for owned UObject references where appropriate.
- Use `TWeakObjectPtr` or soft references when ownership is not intended.
- Avoid hard asset references when a soft reference or data asset is appropriate.
- C++ owns reusable behavior and simulation rules. Blueprints own composition, tuning, presentation, and content authoring.
- Do not place core simulation rules in Level Blueprints.
- Avoid per-frame `Tick` by default. Use events, timers, physics callbacks, or the fixed-step simulation clock when appropriate.
- Keep high-frequency code allocation-free where practical.
- Make units explicit in names and documentation, such as `TemperatureCelsius`, `PowerKilowatts`, and `FuelKilograms`.
- Create project-specific log categories. Do not rely on `LogTemp` in committed code.

### SOLID

- Single Responsibility: each class has one primary reason to change.
- Open/Closed: extend through interfaces, components, data assets, or policies instead of repeated conditionals.
- Liskov Substitution: derived types must honor the contracts of their base types.
- Interface Segregation: prefer focused interfaces over broad manager APIs.
- Dependency Inversion: high-level simulation policy depends on stable abstractions, not UI, network, or asset-loading details.

### DRY without abstraction theater

- Remove duplicated domain rules and repeated validation.
- Do not create a base class or utility merely because two snippets look similar.
- Abstract only after the shared concept and ownership are clear.
- Prefer readable duplication over a premature abstraction that obscures behavior.

## Architecture guardrails

- Every mutable state value has one authoritative owner.
- UI reads state and sends commands. UI never owns simulation truth.
- Mission logic may coordinate systems but may not reach through UI widgets.
- Telemetry observes immutable snapshots and may not mutate simulation state.
- External services are accessed through adapters or interfaces.
- No global mutable singleton state.
- No circular module or class dependencies.
- The dependency direction defined in `docs/ARCHITECTURE.md` is mandatory.

## Quality gate

A task is not complete until all applicable checks pass:

1. Repository preflight and required-file validation.
2. Unreal Editor target build for Win64 Development.
3. Relevant automated tests.
4. Manual verification for editor, Blueprint, map, input, or visual changes.
5. No new warnings that were caused by the change.
6. Documentation updated when behavior, architecture, commands, state ownership, or recovery status changed.
7. Git diff reviewed for generated files, secrets, accidental binaries, and unrelated edits.

Use `scripts/Validate-Project.ps1` as the baseline validation entry point.

## Review behavior

When reviewing code:

- Report findings first, ordered by severity.
- Include file paths and line references.
- Prioritize correctness, state ownership, lifetime safety, architecture violations, regressions, and missing tests.
- Distinguish confirmed defects from risks or suggestions.
- Do not approve based only on successful compilation.

## Completion report

At the end of a task, report:

- What changed
- Why the chosen design fits the architecture
- Files changed
- Build and test commands run
- Results and any unverified items
- Risks, follow-up work, and the next clean checkpoint
