# gaffer-search: *Ask the Gaffer*

[![CI](https://github.com/siddharth-sarma5/gaffer-search/actions/workflows/ci.yml/badge.svg)](https://github.com/siddharth-sarma5/gaffer-search/actions/workflows/ci.yml)

A search engine written from scratch in C++20 over Premier League data. Ask it
`gunners "late winner"` or `"north london derby"` and it returns BM25-ranked matches, players and
clubs, with highlighted snippets and an optional "VAR review" of each score.

Everything is implemented with the standard library: UTF-8 tokenization, a positional inverted
index, BM25, document-at-a-time top-k retrieval, phrase queries, delta + varint compression, a
checksummed on-disk index, snippets and query expansion. GoogleTest is the only dependency.

| Milestone | Scope | Status |
|-----------|-------|--------|
| **M1** single-node search | corpus generator, tokenizer, inverted index, BM25, top-k heap, CLI | ✅ done |
| **M2** fast and good | positions & phrases, DAAT, varint/delta compression, on-disk index, snippets, nickname expansion, VAR review, benchmarks, relevance evaluation | ✅ done |
| **M3** distributed | shard servers over TCP, broker fan-out, global IDF, partial results, docker-compose | planned |

## Demo

```text
$ gaffer search 'gunners "late winner"' -k 2 --var

Ask the Gaffer: "gunners "late winner""
Kick-off: "late winner" (phrase, required), gunner (df 1), arsenal (df 91, from "gunners" x0.7)

  1. Arsenal 2-0 Everton (14 Mar 2026)                      8.358  match
     ... Half-time score: **Arsenal** 0-0 Everton. Goals: Viktor Gyokeres 89' (**Arsenal**); Max Dowman 90+7' (**Arsenal**). Viktor Gyokeres scored a **late** **winner** ...
     VAR review (doc length 188, average 101.9):
       "late winner"                      tf  2  idf  4.87  x1.0  ->   5.414
       gunner                             not in this document
       arsenal (from gunners)             tf  9  idf  2.33  x0.7  ->   2.944
                                                                     total   8.358
  2. West Ham United 0-1 Arsenal (10 May 2026)              8.081  match
     ... United 0-0 **Arsenal**. Goals: Leandro Trossard 83' (**Arsenal**). Leandro Trossard scored a **late** **winner** for **Arsenal** in the 83rd minute. **Arsenal** kept ...
     ...
Full-time: 2 of 64 matching documents in 0.05 ms
```

`"late winner"` is a required phrase (words adjacent, in order). "Gunners" is a club nickname,
expanded to Arsenal at weight 0.7. The VAR review shows each term's BM25 contribution, and the
contributions sum exactly to the ranked score (bitwise; a test checks this). On a terminal, the
highlights are bold yellow instead of `**`.

## Quickstart

Requires Linux (or WSL2 on Windows), CMake ≥ 3.24, Ninja, and GCC ≥ 13 or Clang ≥ 18.
GoogleTest is fetched automatically. Native Windows/MSVC isn't supported: the sanitizer presets
and the Milestone 3 networking code are POSIX-only.

On Ubuntu 24.04 (native or WSL2), one command installs everything:

```sh
sudo apt install -y build-essential cmake ninja-build git python3 \
  clang-18 clang-format-18 clang-tidy-18 libclang-rt-18-dev
```

On Ubuntu 22.04 the stock CMake (3.22) and GCC (11) are too old. Install `g++-13` from
`ppa:ubuntu-toolchain-r/test` and CMake with `pip install --user "cmake>=3.24"`, then
`export CXX=g++-13`. On WSL, clone into your Linux home directory, not `/mnt/c` or `/mnt/e`:
builds there are much slower.

```sh
git clone https://github.com/siddharth-sarma5/gaffer-search && cd gaffer-search
cmake --preset release && cmake --build --preset release
G=./build/release/apps/gaffer

$G search "late winner at anfield"          # ranked results with snippets
$G search '"north london derby"'            # phrase query
$G search "red devils comeback" --var       # nickname expansion + score breakdown
$G index --out pl.gaffer                    # build and save a compressed index
$G search --index pl.gaffer "palmer hat-trick"
$G search                                   # interactive: one query per line

./build/release/apps/gaffer-bench --corpus data/corpus/pl-history.tsv --corpus data/corpus/pl-2025-26.tsv
./build/release/apps/gaffer-eval            # precision@k / MRR on 20 labelled queries

# Tests (162 C++ tests: unit, property, end-to-end goldens, CLI) under ASan + UBSan
cmake --preset asan && cmake --build --preset asan && ctest --preset asan
python3 -m unittest discover -s tools/corpusgen/tests -t tools/corpusgen
```

Search options: `-k N`, `--k1 X`, `--b X`, `--no-stem`, `--keep-stopwords`, `--corpus PATH`,
`--index PATH`, `--nicknames PATH`, `--no-expand`, `--var`.

## How it works

```
openfootball (CC0) ──corpusgen──▶ corpus.tsv ──▶ Analyzer ──▶ IndexBuilder ──▶ InvertedIndex ──▶ index file
                                                                                (positions)      (delta+varint, CRC-32)
query ──▶ parse "phrases" ──▶ Analyzer ──▶ nickname expansion ──▶ PostingCursors (DAAT)
      ──▶ phrase check (positions) ──▶ BM25 ──▶ TopK heap ──▶ snippets / VAR review
```

- **Corpus** (`tools/corpusgen/`): parses public-domain openfootball data. The 2025-26 season
  file has goals with minutes, lineups, cards and venues, from which the generator works out facts
  such as *late winner*, *comeback win*, *hat-trick* or *derby*: 938 documents (matches, players,
  clubs, season). The 1992–2025 seasons have results only and form a 12,786-document benchmark
  corpus.
- **Analysis** (`src/text/`): strict UTF-8, ASCII folding (`Ødegaard` → `odegaard`), punctuation
  splitting, football-aware stopwords, the Harman S-stemmer. Removed stopwords leave position
  gaps, so `"stadium of light"` still has to match with exactly one word in between.
- **Index** (`src/index/`): per term, `(doc, tf)` postings plus positions as parallel arrays.
  On disk: d-gaps + LEB128 varints, a versioned header, the analyzer flags, a corpus fingerprint
  and a CRC-32. Loading validates everything, including structure behind a valid checksum.
- **Query** (`src/query/`): quoted phrases are required, free words optional. Cursors with
  galloping `advance_to` drive a leapfrog intersection for phrases. BM25 (k1 1.2, b 0.75,
  Lucene's non-negative idf). A size-k min-heap selects top-k in O(n log k), with deterministic
  tie-breaks.
- **Results** (`src/snippet/`, `src/ranking/`): the best snippet window by distinct query terms,
  highlighted in the original spelling; a per-term score explanation.

Details and Mermaid diagrams: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). Every design choice,
with the alternatives considered: [docs/DECISIONS.md](docs/DECISIONS.md).

## Benchmarks

From `gaffer-bench` (method and raw JSON in [bench/](bench/README.md)). Cloud VM, Intel Xeon @
2.10 GHz, 2 vCPUs, GCC 13.3 `-O3`, single-threaded queries, 2026-11-03.

| | 2025-26 corpus | 1992-2026 corpus |
|---|---:|---:|
| documents / indexed text | 938 / 0.66 MB | 13,724 / 4.06 MB |
| indexing throughput (median of 5) | 27,377 docs/s (19.3 MB/s) | 94,581 docs/s (28.0 MB/s) |
| postings: fixed-width uint32 → delta + varint | 1,107,264 B → 237,406 B (**21.4%**) | 7,153,148 B → 1,433,730 B (**20.0%**) |
| index load (decode + validate, in memory) | 4.4 ms | 18.2 ms |
| query latency p50 / p99 (38-query log × 200) | **4.3 µs / 24.4 µs** | **32.9 µs / 436.7 µs** |

On this shared VM, an immediate re-run of the larger benchmark gave p50 26.4 µs and p99 279.3 µs.
Tail latency moves by tens of percent between runs, so these are orders of magnitude, not
precise figures.

## Relevance

From `gaffer-eval`: 20 queries whose relevant documents are defined by explicit criteria on the
structured match data, never by engine output ([eval/](eval/README.md)).

| Configuration | P@5 | P@10 | R@10 | MRR |
|---|---:|---:|---:|---:|
| default (BM25 + nickname expansion ×0.7) | 0.520 | 0.380 | 0.895 | 0.718 |
| without nickname expansion | 0.400 | 0.290 | 0.670 | 0.628 |

Many queries have fewer than 10 relevant documents, which caps P@10. Failure analysis, such as
"two" vs "2" or short player pages outranking matches, is in the eval README.

## Engineering

- C++20, CMake presets (`debug`, `asan`, `tsan`, `release`), `-Wall -Wextra -Wconversion … -Werror`
- CI on every push: GCC 13 and Clang 18 × {ASan+UBSan, Release}, clang-format, clang-tidy,
  corpus-generator tests, and byte-for-byte checks that the committed corpora and judgements match
  their generators and that vendored data matches its checksums
- Property tests against brute-force references: top-k vs full sort, DAAT vs exhaustive scoring,
  phrase search vs a naive adjacency scan, galloping search vs a linear scan
- Fuzz-style tests: every truncation and every byte flip of an index file is rejected cleanly
  under ASan
- Golden-file test of top-5 results for 15 queries

## Docs
[Requirements](docs/REQUIREMENTS.md) · [Plan](docs/PLAN.md) · [Architecture](docs/ARCHITECTURE.md) ·
[Decisions](docs/DECISIONS.md) · [Progress](docs/PROGRESS.md) · [Benchmarks](bench/README.md) ·
[Evaluation](eval/README.md)

## What I learned
*To be written at the end of the project.*

## Data and licence
Match data: [openfootball/england](https://github.com/openfootball/england), CC0 / public
domain, vendored at a pinned commit (see [provenance](data/raw/openfootball/PROVENANCE.md)).
All documents are generated from facts; no articles or match reports are used.
Code: MIT.
