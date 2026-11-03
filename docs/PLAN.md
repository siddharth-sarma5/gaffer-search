# Ask the Gaffer — Plan

*The agreed plan. `[x]` = done and committed. Resume from the first unchecked item.*
*Status: **agreed v1** (2026-11-02).*

Working agreement: Claude writes the code; Sid reviews. Before each milestone, Claude shows that
milestone's commit list. After each commit, Claude explains what changed and why. At the end of
each milestone, Claude gives a summary and run instructions, then asks before continuing.
Claude asks before creating the GitHub repo, pushing, or doing anything destructive in git.

---

## Repository layout (target)

```
gaffer-search/
├── CMakeLists.txt, CMakePresets.json, .clang-format, .clang-tidy
├── .github/workflows/ci.yml
├── src/
│   ├── corpus/     Document, corpus loader (TSV)
│   ├── text/       tokenizer, ASCII folding, stopwords, S-stemmer, Analyzer
│   ├── index/      inverted index, postings, varint/delta codecs, on-disk format
│   ├── ranking/    BM25, score explanations ("VAR review")
│   ├── query/      query parser, nickname expansion, evaluation (TAAT → DAAT), top-k
│   ├── snippet/    snippet window selection + highlighting
│   └── net/ server/  (M3) sockets, framing, codec, shard server, broker
├── apps/           gaffer (CLI), gaffer-bench, gaffer-shard, gaffer-broker
├── tests/          GoogleTest suites, one per module, plus end-to-end goldens
├── bench/          benchmark scripts + recorded results (JSON/CSV)
├── eval/           queries.tsv, qrels.tsv (hand-labelled), eval results
├── tools/corpusgen/  Python 3 (stdlib only) corpus generator + its tests
├── data/
│   ├── raw/openfootball/   pinned CC0 source files + PROVENANCE.md
│   ├── corpus/             generated corpus (committed, reproducible)
│   └── nicknames.tsv
├── deploy/         Dockerfile, docker-compose.yml (M3)
└── docs/           REQUIREMENTS, PLAN, DECISIONS, PROGRESS, ARCHITECTURE, visualiser/
```

---

## Milestone 1 — Single-node search, end to end

**Goal.** Type a query, get BM25-ranked results from an in-memory index built over the
2025-26 corpus. CI is green with sanitizers.

**In scope.** Repo scaffolding, CI, the corpus generator for match/club/player docs, the TSV
loader, analysis pipeline (tokenize → fold → stopwords → S-stem), in-memory inverted index
(doc id + tf), BM25, term-at-a-time scoring, bounded-heap top-k, CLI, tests.

**Out of scope (M2+).** Positions and phrases, compression, on-disk index, snippets (M1 shows
the title plus the first line), nickname expansion, explanations, benchmarks.

**Acceptance criteria.**
- `cmake --preset asan && ctest --preset asan` passes locally and in CI (GCC + Clang).
- clang-format and clang-tidy checks pass in CI.
- `gaffer search "palmer hat-trick"` puts Cole Palmer's hat-trick match in the top 3 (his
  player page, which lists "Hat-tricks: 1", legitimately ranks first), and
  `gaffer search "haaland brace"` returns only Haaland two-goal matches in the top 5.
  (The source has no Haaland hat-trick in 2025-26; see PROGRESS notes.)
- `gaffer search "late winner anfield"` returns Anfield matches tagged "late winner" at the top.
- BM25 unit tests match hand-computed scores to 1e-6.
- Corpus generation is deterministic (same input → byte-identical output, checked in CI).

**Commits.**
- [x] 1. `docs: add requirements, plan, decision log, progress tracker and architecture`
- [x] 2. `build: add CMake project, presets (debug/asan-ubsan/release) and GoogleTest via FetchContent`
- [x] 3. `style: add clang-format and clang-tidy configuration`
- [x] 4. `ci: build and test with GCC and Clang under ASan/UBSan; check format and tidy`
- [x] 5. `chore(data): vendor pinned openfootball 2025-26 Premier League file with provenance`
- [x] 6. `feat(corpusgen): parse match blocks (date, venue, attendance, score, scorers, cards, referees)`
- [x] 7. `feat(corpusgen): derive match tags and write match documents`
- [x] 8. `feat(corpusgen): generate club, player and season documents`
- [x] 9. `test(corpusgen): cover parser edge cases and tag derivation; run in CI`
- [x] 10. `feat(data): commit generated 2025-26 corpus with measured doc/token counts`
- [x] 11. `feat(corpus): add Document type and TSV corpus loader with error reporting`
- [x] 12. `feat(text): add UTF-8 aware tokenizer with ASCII folding`
- [x] 13. `feat(text): add stopword filter, S-stemmer and Analyzer pipeline`
- [x] 14. `feat(index): add in-memory inverted index with collection statistics`
- [x] 15. `feat(ranking): add BM25 scorer`
- [x] 16. `feat(query): add term-at-a-time evaluation with bounded min-heap top-k`
- [x] 17. `feat(cli): add gaffer CLI with index and search commands and themed output`
- [x] 18. `test: add end-to-end golden query tests on a fixture corpus`
- [x] 19. `docs: write README quickstart and sample output; update architecture`

**After M1 you should be able to explain:**
- Why an inverted index beats scanning, and what a postings list stores.
- Tokenization choices: why ASCII folding ("Ødegaard" → "odegaard"), why stopwords, and
  what stemming costs in precision.
- BM25 term by term: IDF, tf saturation (k1), length normalisation (b), and why it beats raw TF-IDF.
- Term-at-a-time accumulators; why a size-k min-heap gives O(n log k) top-k.
- How the build/CI/sanitizer setup catches bugs, and what ASan and UBSan each detect.

---

## Milestone 2 — Make it fast and good

**Goal.** Phrase queries, a compressed persistent index, nicer results (snippets, expansion,
explanations), and real measurements of speed, size and relevance.

**In scope.** Positional postings; switching to document-at-a-time (DAAT) evaluation with
postings cursors; a query parser for `"phrases"`; delta + LEB128 varint compression; a versioned
binary index file with CRC32; snippets; nickname expansion; `--var` explanations; a
results-only historical corpus for scale; `gaffer-bench`; a precision@k/MRR evaluator with
~20 labelled queries.

**Out of scope.** Skip pointers/block-max WAND (stretch), mmap (stretch), field weighting/BM25F
(stretch), spelling correction.

**Acceptance criteria.**
- `"late winner"` (quoted) matches only docs with those words adjacent; the unquoted query ranks more broadly.
- Index save → load → search gives results identical to the in-memory index.
- Truncated or corrupted index files are rejected with a clear error (tested).
- Compressed index size is reported next to the uncompressed (fixed 32-bit) size. Both are measured.
- `gaffer search --var "gunners"` shows the expansion to Arsenal and per-term BM25 contributions.
- README benchmark table: indexing docs/s and MB/s; query p50/p99 over a fixed query log; index
  size before and after compression; hardware, build type and corpus size noted.
- precision@5, precision@10 and MRR reported for the labelled query set.

**Commits.**
- [x] 1. `feat(index): store term positions in postings`
- [x] 2. `refactor(query): switch to document-at-a-time evaluation with postings cursors`
- [x] 3. `feat(query): add query parser supporting quoted phrases`
- [x] 4. `feat(query): match phrases by positional intersection`
- [x] 5. `feat(index): add LEB128 varint codec`
- [x] 6. `feat(index): delta-encode doc ids and positions in compressed postings`
- [x] 7. `feat(index): write versioned on-disk index format with CRC32`
- [x] 8. `feat(index): load and validate on-disk index; reject corrupt files`
- [x] 9. `feat(snippet): select best snippet window and highlight matched terms`
- [x] 10. `feat(query): expand club nicknames with down-weighted synonym terms`
- [x] 11. `feat(ranking): add VAR review per-term score explanations`
- [x] 12. `feat(corpusgen): add results-only historical seasons for the scale corpus`
- [x] 13. `perf(bench): add gaffer-bench for indexing throughput, latency percentiles and index size`
- [x] 14. `feat(eval): add precision@k and MRR evaluator with 20 labelled queries`
- [x] 15. `docs: publish measured benchmark and relevance results`

**After M2 you should be able to explain:**
- TAAT vs DAAT, and why phrases and compressed postings push you towards DAAT.
- Positional intersection for phrases, and its cost.
- Why d-gaps make varint effective, the varint byte layout, and the size/CPU trade-off.
- File format design: magic, version, section offsets, checksums, and handling of untrusted input.
- Query expansion's precision/recall trade-off, and why synonyms get lower weight.
- How p50/p99 were measured (warm-up, fixed query log, timer choice), and why p99 matters.
- precision@k vs MRR, and what a 20-query evaluation can and can't tell you.

---

## Milestone 3 — Distributed search

**Goal.** N shard server processes, each owning a slice of the corpus, behind a broker that fans
out queries in parallel and merges top-k. It stays correct under global statistics and survives
slow or dead shards.

**In scope.** Doc-partitioned sharding; RAII socket wrappers; length-prefixed binary protocol;
shard server (bounded thread pool, graceful shutdown); broker (parallel fan-out, per-shard
deadline, k-way merge); global statistics; partial results; TSan; docker-compose; latency vs
shard count.

**Out of scope.** Replication or failover, consensus, service discovery, TLS/auth, async I/O
frameworks (stretch: epoll), dynamic re-sharding.

**Acceptance criteria.**
- Equivalence test: for every eval query, broker(N = 1, 2, 4) top-k equals single-node top-k
  (same docs and order; scores within 1e-9).
- With one shard killed or delayed past the deadline, the broker returns within
  deadline + a small margin, with `partial=true` and the missing shard ids listed.
- Malformed or oversized frames are rejected without crashing (tested).
- TSan build of the integration tests passes in CI.
- `docker compose up` brings up a broker + 4 shards; a CLI query against the broker works.
- Measured latency p50/p99 vs shard count (1/2/4/8) is in the README.

**Commits.**
- [x] 1. `feat(net): add RAII socket wrappers and deadline-aware read_exact/write_all`
- [x] 2. `feat(net): add length-prefixed framing and versioned message codec`
- [x] 3. `test(net): cover codec round-trips and malformed or oversized frames`
- [x] 4. `feat(index): partition corpus into N shards and build per-shard indexes`
- [x] 5. `feat(server): add shard server with bounded thread pool and graceful shutdown`
- [x] 6. `feat(server): aggregate global statistics at the broker and score shards with them`
- [ ] 7. `feat(server): add broker with parallel fan-out, per-shard deadlines and k-way merge`
- [ ] 8. `feat(server): return flagged partial results on shard timeout or failure`
- [ ] 9. `test(server): add multi-shard integration and fault-injection tests; run TSan in CI`
- [ ] 10. `test: assert sharded results equal single-node results`
- [ ] 11. `build: add Dockerfile and docker-compose for broker plus shards`
- [ ] 12. `perf(bench): measure query latency against shard count`
- [ ] 13. `docs: update architecture and write up the global-IDF trade-off`

**After M3 you should be able to explain:**
- Doc-partitioned vs term-partitioned indexes, and why doc partitioning suits this setup.
- Why local IDF breaks score comparability across shards, and the three fixes
  (static global stats, two-round stats query, approximate stats) with their costs.
- Framing over TCP: partial reads and writes, length prefixes, max frame size, versioning.
- Deadlines vs timeouts, the tail-at-scale effect (fan-out amplifies p99), and partial results.
- Merging N top-k lists (k-way heap merge) and deterministic tie-breaking.
- What TSan found (or guards against) in the broker's concurrency.

---

## Final — Visualiser, walkthrough, resume

- [ ] `docs(visualiser): add interactive query walkthrough linked to source lines`
      (one self-contained HTML page: tokenize → expand → postings → score → merge → rank,
      showing real intermediate data and linking to GitHub permalinks at a pinned commit)
- [ ] Guided codebase walkthrough with Sid using the visualiser
- [ ] `docs: add what-I-learned section and final benchmark table`
- [ ] 1–2 resume bullets (result-by-method style, measured numbers only)

## Stretch ideas (not committed to)
Skip pointers / block-max WAND; mmap'd index; BM25F field weights (title vs body); epoll-based
shard server; query result cache at the broker; replicated shards with hedged requests.
