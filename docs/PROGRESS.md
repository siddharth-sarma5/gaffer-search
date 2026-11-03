# Progress

*Updated after every commit or decision. New sessions: read PLAN.md, DECISIONS.md and this file,
then continue from the first unchecked item in PLAN.md.*

## Where we are
- **Phase:** Milestone 1 complete (19/19 commits). Waiting for Sid's go-ahead to start Milestone 2.
- **Last commit:** `docs: write README quickstart and sample output; update architecture`.

## Next step
- Sid reviews Milestone 1, then M2 starts at commit 1 (positional postings).

## Open questions
- **Q1 (corpus scale, needed for M2):** No answer yet, so the default stands: add 1992–2025
  results-only docs (~13k) as a separate *scale corpus* for benchmarks, and keep the 2025-26
  corpus for relevance evaluation.

## Known issues / notes
- Bag-of-words limits recorded in the golden file (tests/golden/pl-2025-26-top5.txt):
  "north london derby" also ranks North West derbies 3rd and 4th (M2 phrase queries fix this);
  "own goal" is dominated by short player pages (consider tuning b in M2 evaluation).
- S-stemmer: "matches" -> "matche" doesn't conflate with "match"; "reds" -> "red" collides with
  red cards. Both are documented and pinned by tests.
- M1 preview lines are the start of each document. Query-aware snippets come in M2.
- The source has no Premier League hat-trick for Haaland in 2025-26 (27 goals, 7 braces).
  The M1 acceptance query changed to "palmer hat-trick" / "haaland brace" (2026-11-02).
- The openfootball 2026-27 file is in progress (season under way) and uses a different layout.
  It is excluded.
- Player names in the source use mixed capitalisation (e.g. `Erling HAALAND`). The generator
  normalises case.
- Club names differ between files ("Chelsea FC" vs "Chelsea"). The generator uses a canonical
  club table, which also holds nicknames and home grounds.
