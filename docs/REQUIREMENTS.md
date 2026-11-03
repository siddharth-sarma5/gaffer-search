# Ask the Gaffer — Requirements and Scope

*Informal scope document. Status: draft v1 (2026-11-02), pending review.*

## 1. Problem

Football fans remember matches by fragments: "that late winner at Anfield", "Haaland's hat-trick",
"the Gunners losing at Goodison". Structured stats sites need you to know the date, the fixture and
the competition first. **Ask the Gaffer** is a small full-text search engine over a Premier League
corpus that takes free-text queries and returns ranked, snippeted results.

It is also a teaching and portfolio project. Every Information Retrieval building block
(analysis, inverted index, BM25, top-k, phrase queries, compression, sharding) is implemented from
scratch in modern C++, so that each one can be explained in an interview.

## 2. Goals

1. Build a working single-node search engine with an inverted index and BM25 ranking.
2. Make it fast and good: positional index, phrase queries, compressed on-disk index,
   snippets, query expansion, and a measured relevance evaluation.
3. Distribute it: sharded index servers over TCP, plus a broker that fans out queries,
   handles slow or dead shards, and merges results using correct global statistics.
4. Measure everything: indexing throughput, query latency p50/p99, index size and precision@k.
   Only measured numbers appear in docs and on the resume.
5. Keep the code explainable: small modules, tests, clean history.

## 3. Non-goals

- Web crawling, or scraping news and match reports. The corpus is generated from facts.
- Learning-to-rank, embeddings or neural retrieval. BM25 is the ranking model.
- A web UI. The interface is the CLI; the final visualiser is a static teaching page.
- Real-time index updates. Indexes are built offline and are immutable once built.
- Fault-tolerant replication or consensus. Milestone 3 is about partitioning, fan-out and
  partial results, not replication.
- Production security (TLS, auth) on the shard protocol. It is localhost/docker only.

## 4. Data

- **Source:** [openfootball/england](https://github.com/openfootball/england),
  file `2025-26/1-premierleague-full.txt`, pinned to commit `9ee7a0c3`.
- **Licence:** CC0 / public domain ("use it as you please with no restrictions").
  Vendoring the raw file and committing the generated corpus are both allowed.
- **Contents (measured):** 380 matches. Every match has a date, kick-off time, venue, city,
  attendance, full-time and half-time scores, lineups, substitutions, cards and referees.
  Goal scorers include minutes and penalty/own-goal markers.
- **Generated documents** (factual sentences written by our generator, no copied prose):
  - match docs (one per match): result, venue, scorers, derived tags such as
    "late winner", "comeback", "hat-trick", "clean sheet", "derby" and "red card";
  - club docs (one per club): season record, home ground, top scorers, nicknames;
  - player docs (one per player who appears): club, appearances, goals and cards.
- **Scale corpus (Milestone 2 benchmarks only):** results-only docs for 1992-93 to 2024-25, from
  the same CC0 repo (date, teams, full-time and half-time score; no scorers). This raises the doc
  count into the ~13k range, so benchmarks are meaningful. The exact count will be measured.

## 5. Functional requirements

| ID | Requirement | Milestone |
|----|-------------|-----------|
| F1 | Load a corpus file of documents (id, type, title, body). | M1 |
| F2 | Analyse text: lowercase, ASCII-fold, split on punctuation, drop stopwords, optional stemming. | M1 |
| F3 | Build an in-memory inverted index: term → postings (doc id, term frequency). | M1 |
| F4 | Rank with BM25 (k1, b configurable) and return top-k using a bounded heap. | M1 |
| F5 | CLI: `gaffer index` and `gaffer search "<query>" [-k N]`, with themed output. | M1 |
| F6 | Positional postings; `"quoted phrase"` queries. | M2 |
| F7 | Save and load the index in a versioned binary format, with delta + varint compression. | M2 |
| F8 | Snippets: the best window around query terms, with matched terms highlighted. | M2 |
| F9 | Club-nickname query expansion ("Gunners" → Arsenal), weighted below original terms. | M2 |
| F10 | `--var` ("VAR review"): per-term BM25 contribution for each result. | M2 |
| F11 | Relevance evaluation: precision@k (and MRR) on ~20 hand-labelled queries. | M2 |
| F12 | Partition the corpus into N shards; each shard is its own TCP server process. | M3 |
| F13 | Broker: parallel fan-out, per-shard deadline, merge top-k, partial results flagged. | M3 |
| F14 | Global IDF / avgdl across shards, so sharded scores equal single-node scores. | M3 |
| F15 | docker-compose that runs broker + N shards locally. | M3 |

## 6. Non-functional requirements

- **Language/toolchain:** C++20, CMake ≥ 3.24, GCC 13 and Clang 18 in CI.
- **Dependencies:** standard library first. GoogleTest (via FetchContent) for tests.
  Any other third-party library needs written justification in `DECISIONS.md` first.
- **Code quality:** RAII everywhere, no raw owning pointers, const-correct interfaces,
  clang-format clean, clang-tidy clean (agreed check set).
- **Correctness:** unit tests for every module; ASan + UBSan build in CI; TSan build for M3.
- **Error handling:** malformed corpus/index files and network failures produce clear
  errors, never UB or crashes (see DECISIONS D-009).
- **Performance targets:** none promised up front. We measure and report.
  Benchmarks record hardware, build type and corpus size alongside every number.
- **Reproducibility:** pinned data commit, deterministic corpus generation, one-command build.
- **History:** Conventional Commits, one logical change per commit, CI green on every push.

## 7. Constraints

- Corpus must be legally clean (CC0 source, generated text).
- The project must be explainable line by line by Sid in an interview.
- Docs (`PLAN.md`, `DECISIONS.md`, `PROGRESS.md`) are updated with every commit or decision,
  so any session can resume from the repo alone.

## 8. Success criteria

- **M1:** `gaffer search "palmer hat-trick"` returns his hat-trick match first; CI is green
  with ASan/UBSan.
- **M2:** phrase queries work; the compressed index is measurably smaller than the uncompressed
  one; a benchmark table and precision@k results are in the README from real runs.
- **M3:** `docker compose up` gives a working cluster; sharded results equal single-node results
  (same top-k, same scores within float tolerance); killing a shard gives flagged partial results
  within the deadline; latency vs shard count is measured.
- **Final:** the visualiser walks one query through every stage, linked to real code lines.
  Resume bullets use only measured numbers.
