# gaffer-search: *Ask the Gaffer*

[![CI](https://github.com/siddharth-sarma5/gaffer-search/actions/workflows/ci.yml/badge.svg)](https://github.com/siddharth-sarma5/gaffer-search/actions/workflows/ci.yml)

A search engine written from scratch in C++20 over a Premier League 2025-26 corpus. Ask it
`late winner at anfield` or `palmer hat-trick` and it returns BM25-ranked matches, players and
clubs.

Everything is implemented by hand with the standard library: UTF-8 tokenization, an inverted
index, BM25 ranking and top-k retrieval. GoogleTest is the only dependency. Later milestones add
phrase queries, index compression and a sharded, distributed deployment.

| Milestone | Scope | Status |
|-----------|-------|--------|
| **M1** single-node search | corpus generator, tokenizer, inverted index, BM25, top-k, CLI | ✅ done |
| **M2** fast and good | positions & phrases, varint/delta compression, on-disk index, snippets, nicknames, VAR review, benchmarks, precision@k | planned |
| **M3** distributed | shard servers over TCP, broker fan-out, global IDF, partial results, docker-compose | planned |

## Demo

```text
$ gaffer search "late winner at anfield" -k 5

Ask the Gaffer: "late winner at anfield"
Kick-off: late (df 104), winner (df 64), anfield (df 20)

  1. Liverpool 1-0 Arsenal (31 Aug 2025)                    8.359  match
     Premier League 2025-26, matchday 3. Liverpool v Arsenal at Anfield, Liverpool, on Sunday 31 August 2025, ...
  2. Liverpool 1-2 Manchester United (19 Oct 2025)          8.035  match
     Premier League 2025-26, matchday 8. Liverpool v Manchester United at Anfield, Liverpool, on Sunday 19 October ...
  3. Liverpool 1-2 Manchester City (8 Feb 2026)             7.682  match
     Premier League 2025-26, matchday 25. Liverpool v Manchester City at Anfield, Liverpool, on Sunday 8 February ...
  4. Liverpool 4-2 AFC Bournemouth (15 Aug 2025)            7.408  match
     Premier League 2025-26, matchday 1. Liverpool v AFC Bournemouth at Anfield, Liverpool, on Friday 15 August ...
  5. Nottingham Forest 1-2 Manchester City (27 Dec 2025)    5.575  match
     Premier League 2025-26, matchday 18. Nottingham Forest v Manchester City at The City Ground, Nottingham, on ...

Full-time: 5 of 118 matching documents in 0.02 ms
```

The top four are exactly the four matches in the corpus where a late winner was scored at Anfield.
(In M1 the second line is the start of the document. Query-aware snippets arrive in M2.)

## Quickstart

Requires CMake ≥ 3.24, Ninja, and GCC ≥ 13 or Clang ≥ 18. GoogleTest is fetched automatically.

```sh
git clone https://github.com/siddharth-sarma5/gaffer-search && cd gaffer-search

cmake --preset release && cmake --build --preset release
./build/release/apps/gaffer search "palmer hat-trick"
./build/release/apps/gaffer search            # interactive: one query per line
./build/release/apps/gaffer index             # collection statistics

# Tests (69 C++ tests incl. end-to-end goldens) under ASan + UBSan
cmake --preset asan && cmake --build --preset asan && ctest --preset asan

# Corpus generator tests (Python 3, stdlib only)
python3 -m unittest discover -s tools/corpusgen/tests -t tools/corpusgen
```

CLI options: `-k N`, `--k1 X`, `--b X` (BM25 parameters), `--no-stem`, `--keep-stopwords`,
`--corpus PATH`.

## How it works

```
openfootball (CC0) ──corpusgen──▶ corpus.tsv ──load──▶ Analyzer ──▶ InvertedIndex
                                                                         │
query ──▶ Analyzer ──▶ terms ──▶ postings ──▶ BM25 (term-at-a-time) ──▶ TopK heap ──▶ results
```

- **Corpus** (`tools/corpusgen/`): parses the public-domain openfootball 2025-26 file (lineups,
  goals with minutes, cards, venues). It works out facts such as *late winner*, *comeback win*,
  *hat-trick* or *derby* from the goal timeline and writes 938 factual documents: 380 matches,
  537 players, 20 clubs and 1 season.
- **Analysis** (`src/text/`): strict UTF-8 decoding, ASCII folding (`Ødegaard` → `odegaard`),
  splitting on punctuation, a short football-aware stopword list, and the Harman S-stemmer.
  Positions keep gaps where stopwords were removed, ready for phrase queries.
- **Index** (`src/index/`): term → postings `(doc, tf)`, sorted by doc id, plus document lengths
  and collection statistics.
- **Ranking** (`src/ranking/`, `src/query/`): BM25 (k1 = 1.2, b = 0.75, Lucene's non-negative
  idf). Scores accumulate term by term in a dense array; a size-k min-heap selects the top k in
  O(n log k). Ties break by doc id, so results are deterministic.

More detail, with diagrams: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Benchmarks

Not measured yet. `gaffer-bench` arrives in Milestone 2 and will report indexing throughput,
query latency p50/p99 and index size before and after compression. Every number here will come
from a recorded run.

## Engineering

- C++20, CMake presets (`debug`, `asan`, `tsan`, `release`), `-Wall -Wextra -Wconversion … -Werror`
- CI on every push: GCC 13 and Clang 18 × {ASan+UBSan, Release}, clang-format, clang-tidy,
  corpus-generator tests, and a byte-for-byte check that the committed corpus matches the generator
- Golden-file test of top-5 results for 15 queries, so ranking changes are always deliberate

## Docs
[Requirements](docs/REQUIREMENTS.md) · [Plan](docs/PLAN.md) · [Architecture](docs/ARCHITECTURE.md) ·
[Decisions](docs/DECISIONS.md) · [Progress](docs/PROGRESS.md)

## What I learned
*To be written at the end of the project.*

## Data and licence
Match data: [openfootball/england](https://github.com/openfootball/england), CC0 / public
domain, vendored at a pinned commit (see [provenance](data/raw/openfootball/PROVENANCE.md)).
All documents are generated from facts; no articles or match reports are used.
Code: MIT.
