# Eden ExecPlans

An ExecPlan is a living implementation specification for work that is too large, risky, or architectural to manage safely as an informal checklist.

Use an ExecPlan for:

- Multi-file features
- New gameplay or simulation systems
- Architecture or dependency changes
- Asset migrations
- Engine upgrades
- Persistence, networking, or external-service integration
- Refactors with meaningful blast radius
- Work expected to take more than about 30 minutes

Store plans in `docs/exec-plans/` using a numeric prefix:

```text
0001-project-foundation.md
0002-six-axis-flight.md
0003-power-and-thermal-systems.md
```

## Required ExecPlan structure

```markdown
# [Plan title]

## Status
Draft | Approved | In progress | Blocked | Complete

## Problem and outcome
What problem is being solved and what observable result will exist?

## Scope
### In scope
### Out of scope

## Current repository state
Relevant files, behavior, constraints, and known debt.

## Architecture alignment
State owners, dependency direction, interfaces, data flow, and ADR impact.

## Alternatives considered
At least two reasonable options for decisions with architectural weight.

## Milestones
Small, verifiable increments. Each milestone must leave the repository understandable.

## Detailed steps
Exact files, symbols, assets, and commands. Write for an engineer with only the repository and this plan.

## Validation
Build, automated tests, manual editor checks, performance checks, and expected results.

## Failure modes and rollback
How the work can fail, how it will be detected, and how to return to a safe state.

## Progress log
Timestamped factual updates. Record deviations from the original plan.

## Decision log
Decisions made during execution and why.

## Acceptance evidence
Concrete evidence that every acceptance criterion passed.

## Handoff
Remaining risks, deferred work, and the next clean task.
```

## Plan rules

- Read the relevant source and documentation before writing the plan.
- A plan must name the authoritative owner of every new mutable state value.
- A plan must identify the feedback surfaces that prove the feature works.
- A plan must describe the blast radius of the change.
- Keep the plan synchronized with implementation.
- Record discoveries and changed assumptions immediately.
- Do not mark a plan complete with unverified build or test claims.
- Update `docs/RECOVER.md` at meaningful checkpoints.
