# Codex Bootstrap Prompt

Use this prompt from the Git root that contains `EdenSpaceSimulator.uproject`.

```text
You are setting up the existing Unreal Engine 5.8 C++ repository EdenSpaceSimulator for long-term professional development.

First, read AGENTS.md and every mandatory file it lists. Then inspect the repository before making changes.

Goal:
Establish a clean, buildable, documented Unreal project foundation that supports the first Eden Space Simulator vertical slice without prematurely implementing the full game.

Required boundaries:
- Follow the project spec and architecture in docs/.
- Follow Epic Unreal C++ conventions.
- Follow SOLID and pragmatic DRY.
- Follow the SDLC and review gates in this repository.
- Preserve single ownership of mutable state.
- Use C++ for reusable behavior and simulation rules.
- Use Blueprints for composition, tuning, content, and presentation.
- Add automated tests for deterministic core behavior.
- Do not implement flight movement, resource simulation, mission gameplay, UI, networking, or EDEN OS integration during this bootstrap unless a prerequisite requires a tiny compile-safe seam.

Required closeout:
- Record an ExecPlan under docs/exec-plans/.
- Preserve authored Unreal assets.
- Keep generated Unreal and IDE files untracked.
- Add project-specific logging.
- Add a minimal Eden.Unit.Foundation automation test.
- Run scripts/Validate-Project.ps1.
- Build and run tests only with a discovered or provided Unreal Engine 5.8 root.
- Mark Unreal build, automation, and editor checks pending when local tooling is unavailable.
- Update docs/REMEMBER.md only with durable verified facts.
- Update docs/RECOVER.md with enough information for a new Codex session to resume safely.
```
