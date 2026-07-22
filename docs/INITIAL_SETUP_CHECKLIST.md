# Initial Setup Checklist

## Repository

- [ ] `.git` is in the same directory as `EdenSpaceSimulator.uproject`.
- [ ] `git rev-parse --show-toplevel` returns the project directory.
- [ ] Git LFS is installed.
- [ ] `.uasset` and `.umap` use LFS.
- [ ] Generated directories are ignored.
- [ ] No generated directory is already tracked.
- [ ] First foundation commit is reviewed.

## Toolchain

- [ ] Unreal Engine 5.8 launches.
- [ ] Visual Studio 2022 has Game development with C++.
- [ ] MSVC and Windows SDK are installed.
- [ ] `UE_ENGINE_ROOT` is configured locally.
- [ ] Project files can be generated.
- [ ] `Development Editor | Win64` builds.

## Unreal project

- [ ] Exactly one `.uproject` exists at the repository root.
- [ ] The project has a valid C++ source module.
- [ ] Module and target names match the project.
- [ ] Enhanced Input status is verified.
- [ ] Default map status is verified.
- [ ] Project-specific log categories exist.
- [ ] A minimal `Eden.Unit.Foundation` test exists.
- [ ] The automation test can be discovered and run.

## Documentation

- [ ] `AGENTS.md` is present.
- [ ] `IMPRINT.md` is accurate.
- [ ] `REMEMBER.md` contains only durable facts.
- [ ] `PROJECT_SPEC.md` matches the intended vertical slice.
- [ ] `ARCHITECTURE.md` matches implementation.
- [ ] `SDLC.md` and `REVIEW.md` are adopted.
- [ ] `RECOVER.md` records a verified checkpoint.
- [ ] `ADR-0001` is accepted.
- [ ] `0001-project-foundation.md` is complete.

## First clean checkpoint

- [ ] Repository validation passes.
- [ ] Editor target build passes.
- [ ] Foundation automation test passes.
- [ ] Unreal Editor opens the project.
- [ ] Git status is understood.
- [ ] Recovery file is updated.
- [ ] Next task is `0002-six-axis-flight.md`.
