# Relevance evaluation

20 queries over the 2025-26 corpus (938 docs), 87 relevant documents in total. Run it with:

```sh
./build/release/apps/gaffer-eval --json eval/results.json
```

## How relevance was judged

`make_qrels.py` defines each query's relevant set with an **explicit criterion evaluated on the
structured match data** (parsed goals, venues, cards and derived facts such as "late winner").
Example: *q01 "late winner at anfield"* = matches at Anfield whose decisive goal came at 80' or
later. The criteria are in `queries.tsv`. Relevance never depends on what the engine returns,
so the evaluation isn't circular, and CI regenerates the judgements to keep them in sync.

Limitations: binary relevance; 20 queries written by the engine's authors; criteria are literal
(a doc that *mentions* Palmer's hat-trick but isn't that match or his page counts as
not relevant).

## Metrics

P@k = relevant in top k / k (denominator always k, so a query with 2 relevant docs maxes at
P@5 = 0.4) · R@10 = relevant in top 10 / all relevant · MRR = mean of 1 / rank of the first
relevant doc (searched to depth 100).

## Results — 2026-11-03

| Configuration | P@5 | P@10 | R@10 | MRR |
|---|---:|---:|---:|---:|
| **default** (k1 1.2, b 0.75, nicknames ×0.7) | **0.520** | **0.380** | **0.895** | **0.718** |
| no nickname expansion | 0.400 | 0.290 | 0.670 | 0.628 |
| nicknames ×0.5 | 0.520 | 0.380 | 0.895 | 0.721 |
| nicknames ×1.0 | 0.520 | 0.375 | 0.879 | 0.714 |
| b = 0.5 | 0.560 | 0.390 | 0.919 | 0.754 |
| b = 0.9 | 0.510 | 0.370 | 0.862 | 0.710 |

Per-query numbers for the default configuration are in `results.json`.

## What the numbers say

- **Nickname expansion is the biggest single win:** recall@10 goes from 0.670 to 0.895 and MRR
  from 0.628 to 0.718. Without it, "toffees home win" or "wolves red card" can only find the one
  club page that mentions the nickname.
- **The expansion weight barely matters between 0.5 and 0.7** (MRR 0.721 vs 0.718); 1.0 is
  slightly worse. 0.7 stays: the difference is one rank on one query, well inside the noise of
  20 queries.
- **b = 0.5 scores best, but the default isn't changed.** These 20 queries are the test set,
  and choosing parameters on the test set overfits. Tuning `b` properly needs a separate held-out
  query set. Recorded as a stretch item.

## Failure analysis (default configuration)

| Query | Result | Cause |
|---|---|---|
| q14 "comeback from two goals down" | P@5 0.0, RR 0.04 | **Vocabulary mismatch:** documents say "came from **2** goals down", the query says "two". Number words aren't normalised. |
| q15 "hat-trick" | P@5 0.0, R@10 0.43 | **Length normalisation:** 32-word player pages ("Hat-tricks: 1.") outrank 200-word match reports for the same words. |
| q18 "wolves red card" | P@5 0.2 | **Zero-count facts:** every player page says "Red cards: 0.", so all 537 match "red card". The corpus should state counts only when they're non-zero. |
| q05, q10–q13, q17 | RR 0.5 | The club or player page ranks first and the specific match second; arguably correct-but-unlabelled. |

These are concrete next steps (generator fix for zero counts, number-word normalisation, a
held-out set for tuning b), deliberately left as findings rather than tuned away.
