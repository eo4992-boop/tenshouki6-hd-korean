# Project Context

## Facts
- Target: Steam version of NOBUNAGA'S AMBITION: Tenshouki with Power Up Kit HD Version.
- Work is for files owned and installed by the user.
- Initial analysis targets include `MSG/MESSAGE.N6P` and `n6data/MESSAGE.N6P`.
- Original game assets must not be committed.

## Decisions
- Use Luna Agentic Workflow v2.
- Prefer read-only analysis before any mutation.
- Patch application must create backups and support restoration.
- 5-A is independent technical review; 5-B is user acceptance on the real PC.

## Unknowns
- Exact N6P container format and text encoding.
- Whether the executable requires additional localization changes.
- Korean font rendering and text-length constraints.

## Current phase
- Context, intent, specification, and initial plan completed.
- Implementation begins with a read-only binary analyzer.
