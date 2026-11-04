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

## Cluster: latency vs shard count — 2026-11-03

```sh
./build/release/apps/gaffer-bench --shards 1,2,4,8 --reps 100 \
    --corpus data/corpus/pl-history.tsv --corpus data/corpus/pl-2025-26.tsv \
    --label cluster-1992-2026 --json bench/results/cluster-1992-2026.json
```

Method: for each shard count, the corpus is partitioned and each shard runs as a `ShardServer`,
with a `Broker` in front, all in one process on localhost TCP. A client sends the 38-query log
over one persistent connection (3 warm-up rounds, then 100), timing each round trip. "Inside
broker" is the broker's own time for the request (planning, fan-out, merge). Same VM as above:
**2 vCPUs**, so more than 2 shards means more busy threads than cores.

| | p50 | p90 | p99 |
|---|---:|---:|---:|
| single node, search only (in-process) | 32.6 µs | 227.3 µs | 367.9 µs |
| single node, search + 10 snippets (in-process) | 183.4 µs | 407.1 µs | 618.2 µs |
| 1 shard via broker | 492.7 µs | 785.3 µs | 1,261.3 µs |
| 2 shards via broker | **426.7 µs** | 759.4 µs | 1,297.4 µs |
| 4 shards via broker | 722.8 µs | 1,442.1 µs | 2,170.8 µs |
| 8 shards via broker | 1,230.2 µs | 2,277.7 µs | 3,421.8 µs |

An immediate re-run gave the same shape (p50: 476 / 401 / 661 / 1,186 µs for 1 / 2 / 4 / 8 shards).

### Reading the numbers

- **Snippets dominate a results page.** Rendering 10 snippets costs ~150 µs at p50 here (search
  + snippets 183 µs vs search 33 µs; ~38 µs per snippet in an isolated measurement). Each one
  re-tokenises a ~200-word body.
- **The distributed overhead** (1 shard via broker vs single node + snippets) is ~300 µs at p50:
  two extra localhost round trips (client→broker, broker→shard), a thread spawn per shard per
  query, and reactor hand-offs between poller and worker threads.
- **2 shards beat 1** (p50 427 vs 493 µs): the two shards' work really runs in parallel on the
  two cores.
- **Beyond 2 shards latency rises.** On 2 cores, extra shards add threads, not parallelism.
  Each shard also renders snippets for its own top-10, so 8 shards render 80 snippets to return
  10. This "over-fetch" is why production systems use **query-then-fetch**: phase 1 gathers
  only (doc, score) from each shard, phase 2 fetches snippets for the final k. It's the next
  optimisation (stretch).
- This benchmark measures fan-out overhead and parallelism on one small machine. It doesn't
  show the reason to shard in production: an index too big for one machine's memory, and many
  cores across machines.
