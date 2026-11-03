# Progress

*Updated after every commit or decision. New sessions: read PLAN.md, DECISIONS.md and this file,
then continue from the first unchecked item in PLAN.md.*

## Where we are
- **Phase:** Milestone 1, in progress.
- **Last commit:** M1-1 `docs: add requirements, plan, decision log, progress tracker and architecture`.

## Next step
- M1-2: CMake project, presets and GoogleTest.

## Open questions
- **Q1 (corpus scale, needed for M2):** No answer yet, so the default stands: add 1992–2025
  results-only docs (~13k) as a separate *scale corpus* for benchmarks, and keep the 2025-26
  corpus for relevance evaluation.
- **GitHub push access:** As of 2026-11-02, the Claude GitHub App isn't installed for this repo,
  so pushes are refused. Commits are made locally and will be pushed once access is granted.

## Known issues / notes
- The openfootball 2026-27 file is in progress (season under way) and uses a different layout.
  It is excluded.
- Player names in the source use mixed capitalisation (e.g. `Erling HAALAND`). The generator
  normalises case.
- Club names differ between files ("Chelsea FC" vs "Chelsea"). The generator uses a canonical
  club table, which also holds nicknames and home grounds.
