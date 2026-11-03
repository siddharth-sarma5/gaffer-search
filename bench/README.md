# Benchmarks

All numbers come from `gaffer-bench` runs whose raw output is in `results/*.json`.
Nothing here is estimated.

## How to reproduce

```sh
cmake --preset release && cmake --build --preset release
./build/release/apps/gaffer-bench --corpus data/corpus/pl-2025-26.tsv \
    --runs 5 --reps 200 --label pl-2025-26 --json bench/results/pl-2025-26.json
./build/release/apps/gaffer-bench --corpus data/corpus/pl-history.tsv \
    --corpus data/corpus/pl-2025-26.tsv \
    --runs 5 --reps 200 --label pl-1992-2026 --json bench/results/pl-1992-2026.json
```

## Method

- **Indexing:** `build_index` over the whole corpus, 5 runs; median reported as docs/s and MB/s
  of indexed text (title + body).
- **Size:** compressed postings (delta + varint, M2-6) vs the same postings stored as fixed-width
  `uint32` (doc, tf, position offset, positions), which is how they're held in memory.
- **Codec:** `serialize_index` / `deserialize_index` of the full index in memory (no disk I/O),
  median of 5. Load includes full validation and decoding (D-017).
- **Latency:** the 38-query log in `queries.tsv` (term, multi-term OR, phrase, mixed, nickname
  expansion), 3 warm-up rounds, then 200 rounds; every `search(q, k=10)` is timed on its own
  with `steady_clock`. Nearest-rank percentiles over all 7,600 samples. Single thread, nickname
  expansion on.

## Results — 2026-11-03

Machine: cloud VM, Intel Xeon @ 2.10 GHz, 2 vCPUs, GCC 13.3.0, `-O3` (Release).

| | 2025-26 corpus | 1992-2026 corpus |
|---|---:|---:|
| documents | 938 | 13,724 |
| indexed text | 0.66 MB | 4.06 MB |
| tokens / terms / postings | 95,544 / 1,574 / 60,424 | 652,085 / 1,660 / 378,734 |
| indexing (median of 5) | 34.3 ms → 27,377 docs/s, 19.3 MB/s | 145.1 ms → 94,581 docs/s, 28.0 MB/s |
| postings, fixed-width uint32 | 1,107,264 B | 7,153,148 B |
| postings, delta + varint | 237,406 B (**21.4%**) | 1,433,730 B (**20.0%**) |
| index file | 250,489 B | 1,460,332 B |
| serialise / load (in memory) | 3.7 ms / 4.4 ms | 15.0 ms / 18.2 ms |
| query latency p50 / p90 / p99 | **4.3 / 16.5 / 24.4 µs** | **32.9 / 231.8 / 436.7 µs** |
| query latency mean | 6.8 µs | 76.3 µs |

Per category, 1992-2026 corpus (p50 / p99, µs): term 12.7 / 155.7 · phrase 41.3 / 395.2 ·
mixed 52.0 / 388.4 · nickname 46.4 / 452.4 · multi-term OR 87.0 / 464.1.

**Variance:** an immediate re-run of the 1992-2026 benchmark on the same VM gave p50 26.4 µs,
p99 279.3 µs and indexing at 86,537 docs/s. Tail latency on a shared 2-vCPU VM moves by tens
of percent between runs. Treat these numbers as an order of magnitude, and compare configurations
only within one run.

## Reading the numbers

- **Compression:** d-gaps + varint store postings in ~20% of their fixed-width size. The bigger
  corpus compresses slightly better because its lists are denser (smaller gaps).
- **Latency grows with postings scanned, not with document count:** 14.6× the documents gives
  ~7.6× the p50. The history corpus is short documents over a small vocabulary (1,660 terms), so
  common terms such as `win` or `arsenal` have long lists, and every OR query walks all of them.
  Dynamic pruning (WAND / block-max WAND) is the standard fix; it's a stretch item.
- **Indexing gets faster per doc on the larger corpus:** history documents are short and uniform.
  MB/s is the fairer comparison (19.3 vs 28.0).
