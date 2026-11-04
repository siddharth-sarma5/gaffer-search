# Progress

*Updated after every commit or decision. New sessions: read PLAN.md, DECISIONS.md and this file,
then continue from the first unchecked item in PLAN.md.*

## Where we are
- **Phase:** Milestones 1, 2 and 3 complete (19 + 15 + 13 commits, plus fixes). Remaining: the final phase (visualiser, codebase walkthrough, what-I-learned).
- **Last commit:** `docs: update architecture and write up the global-IDF trade-off`.

## Next step
- Final phase: interactive visualiser, codebase walkthrough with Sid, what-I-learned (Sid will schedule).

## Open questions
- None right now.

## Known issues / notes
- Cluster: each shard renders snippets for its own top-k, so S shards render S x k snippets for
  k results (bench/README.md). Query-then-fetch is the documented next optimisation.
- The docker image can't be built in the dev sandbox (Docker Hub blocked); the CI `docker` job
  builds and exercises it. Its first run failed because `compose run` restarted the stopped
  shard; fixed with --no-deps (4eda78a). bench/results/cluster-1992-2026.json was committed in
  4eda78a by mistake; history left as is rather than force-pushed.
- Relevance failure analysis (eval/README.md): "two" vs "2" vocabulary mismatch (q14); short
  player pages outranking matches (q15); "Red cards: 0" in every player page polluting "red card"
  (q18). Candidate fixes: omit zero counts in the generator, number-word normalisation, a held-out
  query set before tuning b (b=0.5 scored better on the test set, deliberately not adopted).
- Benchmarks run on a shared 2-vCPU VM; p99 varies by tens of percent between runs (bench/README.md).
- Bag-of-words limits recorded in the golden file (tests/golden/pl-2025-26-top5.txt):
  "north london derby" also ranks North West derbies 3rd and 4th when unquoted; quoted
  (M2-4), it returns exactly the two North London derbies;
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
