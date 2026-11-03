# Decision Log

Format: ID · date · decision · options considered · why. Status is **Agreed** (Sid decided or
approved) or **Proposed** (Claude's recommendation; Sid approved the plan as a whole on
2026-11-02, so these stand unless Sid objects).

---

### D-001 · 2026-11-02 · Data source: openfootball (CC0) — Agreed
- **Options:** (a) openfootball/england, CC0; (b) openfootball + StatsBomb Open Data 2015/16
  (User Agreement: credit + logo, research/non-commercial); (c) Fjelstul English Football
  Database (CC-BY-SA 4.0, share-alike on derived corpus, no scorers); (d) hand seed + synthetic.
- **Why:** CC0 means no attribution or share-alike obligations, so both the raw file and the
  generated corpus can live in a public repo. All documents are generated from facts (scores,
  dates, scorers), so there is no copied prose.

### D-002 · 2026-11-02 · Corpus scope: the 2025-26 season first — Agreed
- **Finding:** Checked at openfootball commit `9ee7a0c3`. Only `2025-26/1-premierleague-full.txt`
  has scorers with minutes, plus venue, attendance, lineups, cards and referees (380/380 matches).
  Seasons 1992-93 to 2024-25 have scores only.
- **Options:** derive score-only facts for all seasons; add StatsBomb; try Wikidata; focus on 2025-26.
- **Decision (Sid):** Build on 2025-26. Add earlier seasons later as results-only docs if we need more.
- **Consequence:** The rich corpus is about 1k docs, which is too small for meaningful
  benchmarks. PLAN M2 #12 adds the 1992-2025 results-only seasons (~13k docs, count to be measured)
  for the scale corpus, and the 2025-26 corpus stays the relevance corpus. → Open question Q1.

### D-003 · 2026-11-02 · Repo: `siddharth-sarma5/gaffer-search`, public — Agreed
- **Options:** ask-the-gaffer, epl-search-engine, premier-search, gaffer-search; public vs private-until-M1.
- **Why:** Sid's choice. Public from day one makes the commit history part of the portfolio.
  "Ask the Gaffer" stays the product/CLI name.

### D-004 · 2026-11-02 · C++20 — Agreed
- **Options:** C++20, C++23.
- **Why:** Solid in GCC 13 / Clang 18 (CI). We get concepts, `std::span`, ranges and
  `std::jthread`/`stop_token`. C++23's `std::expected` would be nice, but library support is uneven.

### D-005 · 2026-11-02 · M3 transport: raw TCP, custom length-prefixed protocol — Agreed
- **Options:** raw POSIX TCP; cpp-httplib (header-only, MIT); decide later.
- **Why:** No dependency, and the networking itself (framing, partial I/O, deadlines) is
  interview material that an HTTP library would hide.

### D-006 · 2026-11-02 · Work split: Claude writes, Sid reviews — Agreed
- **Options:** hybrid (Sid writes the IR core); Claude writes all; Sid writes most.
- **Why:** Sid's choice. To compensate, every commit gets a "what and why" explanation, and each
  milestone ends with a list of things Sid should be able to explain.

### D-007 · 2026-11-02 · Corpus interchange format: TSV with escapes — Proposed
- **Options:** JSONL (needs a JSON parser, i.e. nlohmann/json or hand-rolled); TSV; binary.
- **Why:** The docs are flat (`id, type, title, body`). TSV with `\t \n \\` escapes is trivially
  parsed with the standard library, is diff-friendly in git, and needs no dependency.

### D-008 · 2026-11-02 · Generator in Python 3 (stdlib only) — Proposed
- **Options:** Python; C++.
- **Why:** The generator is offline data prep, not part of the engine. Python keeps it short and
  readable. The stdlib-only rule keeps CI simple. It has its own unittest suite.

### D-009 · 2026-11-02 · Error-handling policy — Proposed
- **Policy:** Exceptions (a small `gaffer::Error` hierarchy) for unrecoverable load/parse/format
  failures at module boundaries. `std::optional` for "not found" lookups. No exceptions on hot
  query paths. In M3, network errors become status values in responses (partial results), never
  exceptions crossing threads.
- **Why:** This is the idiomatic C++20 option without `std::expected`, and it keeps hot loops clean.

### D-010 · 2026-11-02 · Stemming: S-stemmer (Harman 1991) — Proposed
- **Options:** none; S-stemmer (plural stripping); Porter.
- **Why:** "Optional simple stemming". The S-stemmer is ~15 lines and explainable; it handles
  "goals/goal" and "wins/win" without Porter's over-stemming of names ("Ings" → "ing").
  Stemming can be turned off by a flag for comparison.

### D-011 · 2026-11-02 · Licence: MIT for code; data stays CC0 — Proposed
- **Why:** MIT is common and permissive. CC0 data needs no attribution, but we credit
  openfootball anyway in `data/raw/openfootball/PROVENANCE.md`.

### D-012 · 2026-11-02 · Test framework: GoogleTest via FetchContent, pinned release — Proposed
- **Why:** Required by the brief. Pinned to a release tag for reproducibility.

### D-013 · 2026-11-02 · Sharding: document-partitioned — Proposed (M3)
- **Options:** doc-partitioned (each shard owns whole docs); term-partitioned (each shard owns terms).
- **Why:** Doc partitioning balances load, keeps phrase queries shard-local, and degrades
  gracefully (losing a shard loses some docs, not whole terms). Its cost is that every query hits
  every shard, which is exactly the fan-out and tail-latency problem M3 is meant to show.

### D-014 · 2026-11-02 · Global statistics: precomputed at broker startup — Proposed (M3)
- **Options:** (a) local per-shard IDF; (b) the broker gathers N, total length and per-term df
  once at startup (indexes are immutable) and sends the needed stats with each query;
  (c) a two-round query (fetch df, then search); (d) approximate/sampled global stats.
- **Why:** Indexes are immutable, so (b) is exact at zero per-query cost. (c) adds an RTT per
  query. (a) makes scores incomparable across shards. To be revisited if indexes become mutable.

### D-015 · 2026-11-03 · Phrase query semantics and scoring — Proposed (M2)
- **Semantics:** quoted phrases are *required* (AND), free words are *optional* (OR) and only add
  score once all phrases match. A query without quotes behaves exactly as in M1.
- **Adjacency:** analysed positions are compared, including stopword gaps:
  `"stadium of light"` → offsets {0, 2}. Any stopword fills a gap ("Stadium and Light" also
  matches), because stopwords aren't indexed. That's the cost of a smaller index.
- **Scoring options:** (a) score phrase words individually and only filter on adjacency;
  (b) score the phrase as one BM25 pseudo-term with tf = occurrences and idf = sum of member
  idfs (Lucene's PhraseQuery); (c) compute an exact phrase df with an extra pass.
- **Why (b):** one pass, no double counting of words, and rarer phrases still score higher.
  (c) needs every candidate to be checked before any scoring, which defeats DAAT streaming.
- **Evaluation:** leapfrog intersection of phrase-word cursors with galloping `advance_to`.
  Positions are checked only for docs containing every word.

### D-016 · 2026-11-03 · Benchmark corpus: all earlier seasons, results only — Agreed
- **Options:** (a) 1992-93 to 2024-25 Premier League results-only docs as a separate scale corpus;
  (b) 2025-26 Championship/League One/League Two results; (c) synthetic scale-up.
- **Decision (Sid):** (a), all previous seasons, used for benchmarks (commit M2-12). The rich
  2025-26 corpus stays the relevance-evaluation corpus. Same CC0 source (openfootball/england).

### D-017 · 2026-11-03 · Compressed on disk, decoded at load — Proposed (M2)
- **Options:** (a) keep postings compressed in memory and decode inside the cursor as it
  advances; (b) store compressed on disk, decode everything into PostingLists at load time.
- **Why (b):** the search path stays exactly as tested (PostingCursor over plain arrays), and the
  disk/transfer saving is where compression pays off for this corpus size. Load-time decoding is
  measured in M2-13. (a) is listed as a stretch item: it would need per-block skip data to keep
  `advance_to` fast.
- **Measured (M2-6):** 2025-26 corpus postings: 1,107,264 B fixed-width uint32 → 237,406 B
  delta+varint (21.4%).

### D-018 · 2026-11-03 · Nickname expansion design — Proposed (M2)
- **Matching on raw tokens** (lower-cased, accent-folded, not stemmed): the stemmer maps
  "reds" -> "red", so matching after stemming would expand "red card" to Liverpool.
- **Additive, down-weighted (x0.7)**: the typed word stays (it matches the club page that lists
  the nickname), and the expansion adds the club name at 0.7, because it's a guess about intent.
  Typed words are never demoted (expansion runs after all typed words are collected).
- **Free words only**; quoted phrases stay literal. Ambiguous aliases ("blues", "wanderers")
  are left out of `data/nicknames.tsv`: a wrong expansion hurts precision more than a missing one
  hurts recall.
- **Golden file**: the e2e golden keeps measuring plain BM25 (no expander), so expansion changes
  are tested separately and the baseline stays comparable. 0.7 will be checked by the M2-14
  relevance evaluation.

### D-019 · 2026-11-03 · Partition round-robin by DocId — Proposed (M3)
- **Options:** (a) round-robin `shard = g mod N`; (b) hash of the document key; (c) contiguous
  ranges.
- **Why (a):** shards are balanced within one document; `global = local x N + shard` needs no
  lookup table; and order within a shard matches global order, so per-shard tie-breaks agree with
  single-node tie-breaks (required for exact equivalence). (b) keeps assignments stable when
  documents are inserted, which matters for mutable indexes; ours are rebuilt in full.
  (c) would skew load (all 2025-26 docs in one shard).
