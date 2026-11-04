# Architecture

*Status: Milestones 1, 2 and 3 built (2026-11-04).
These diagrams are updated whenever the design changes.*

## 1. System context

```mermaid
flowchart LR
    user([Fan / developer])
    of[(openfootball/england<br/>CC0 text files)]
    gen[corpusgen<br/>Python, offline]
    corpus[(data/corpus/*.tsv<br/>generated docs)]
    gaffer[Ask the Gaffer<br/>C++20 search engine]
    ci[GitHub Actions CI]

    of -- pinned raw file --> gen
    gen -- match / club / player docs --> corpus
    corpus -- index build --> gaffer
    user -- "gaffer search 'late winner anfield'" --> gaffer
    gaffer -- ranked results + snippets --> user
    ci -. build, test, sanitizers .-> gaffer
```

## 2. Components

```mermaid
flowchart TB
    subgraph apps[apps/]
        cli["gaffer CLI<br/>index · search · --var"]
        bench["gaffer-bench · gaffer-eval"]
        shard["gaffer-shard"]
        broker["gaffer-broker"]
    end

    subgraph core["src/ — library: gaffer_core"]
        corpusm["corpus/<br/>Document · TsvLoader"]
        text["text/<br/>Tokenizer · AsciiFold · Stopwords · SStemmer · Analyzer"]
        index["index/<br/>InvertedIndex · PostingList · PostingCursor<br/>IndexBuilder · varint · postings codec · index file"]
        ranking["ranking/<br/>Bm25 · Explanation"]
        query["query/<br/>Searcher (DAAT) · TopK · phrase matching<br/>query parser · NicknameExpander"]
        snippet["snippet/<br/>make_snippet · render"]
        evalm["eval/<br/>precision@k · recall@k · MRR"]
        net["net/<br/>Fd · Deadline · framing · messages · rpc"]
        server["server/<br/>TcpServer (reactor) · ShardServer · Broker<br/>merge_stats · merge_top_k"]
    end

    cli --> corpusm & index & query & snippet
    bench --> index & query & evalm
    shard --> server
    broker --> server
    server --> net & query & index
    query --> text & index & ranking
    index --> text & corpusm
    snippet --> text
```

Dependency rule: arrows point downwards only. `text/` and `corpus/` depend on nothing else in
the project, so each module can be unit-tested in isolation.

## 3. Data flow — indexing

```mermaid
flowchart LR
    hist[33 results-only seasons] --> hparse[corpusgen.history:<br/>3 layouts, round-robin check]
    hparse --> htsv[(pl-history.tsv)]
    raw[1-premierleague-full.txt] --> parse[corpusgen: parse match blocks]
    parse --> derive[derive tags<br/>late winner · comeback · hat-trick ·<br/>clean sheet · derby · red card]
    derive --> write[write docs<br/>match · club · player]
    write --> tsv[(corpus.tsv)]
    tsv --> load[TsvLoader<br/>→ Document]
    load --> analyze[Analyzer<br/>tokenize → fold → stop → stem]
    analyze --> build[IndexBuilder<br/>term → postings<br/>doc lengths, N, avgdl]
    build --> mem[(In-memory index<br/>postings + positions)]
    mem -- delta + varint, CRC-32 --> file[(index file)]
    file -- validate + decode --> mem
    file -- M3: partition by doc --> shards[(shard_0..N-1)]
```

## 4. Data flow — querying (single node, M1–M2)

```mermaid
sequenceDiagram
    actor U as User
    participant C as gaffer CLI
    participant P as parse_query
    participant A as Analyzer
    participant X as NicknameExpander
    participant E as Searcher
    participant I as InvertedIndex
    participant B as Bm25
    participant S as make_snippet

    U->>C: search "gunners late winner"
    C->>P: parse (terms, "phrases")
    P->>A: analyze each clause
    A-->>X: [gunners, late, winner]
    X-->>E: [arsenal(w=0.7), gunners, late, winner]
    E->>I: postings for each term
    loop each candidate doc (DAAT: min doc across cursors, or leapfrog for phrases)
        E->>E: phrase check on positions (count_phrase_matches)
        E->>B: score(idf, tf, doc length)
        B-->>E: per-term contributions
        E->>E: push into size-k min-heap
    end
    E-->>C: top-k (doc, score, explanation)
    C->>S: snippets for top-k
    C-->>U: "Full-time: 10 results in 3 ms"
```

## 5. Data flow — querying (distributed, M3)

```mermaid
sequenceDiagram
    actor U as Client (gaffer search --broker)
    participant BR as Broker
    participant S0 as Shard 0
    participant S1 as Shard 1
    participant S2 as Shard N-1

    Note over BR,S2: Startup: StatsRequest to every shard (retried while shards boot)<br/>merge_stats: N = Σ docs, total length = Σ, df(t) = Σ df_shard(t)
    U->>BR: SearchRequest(q, k)
    BR->>BR: plan_query(q): parse, analyse, expand nicknames (no index needed)<br/>attach global N, total length, df of the query's terms
    par one std::jthread per shard, one shared Deadline
        BR->>S0: ShardSearchRequest(q, k, global stats)
        BR->>S1: ShardSearchRequest(q, k, global stats)
        BR->>S2: ShardSearchRequest(q, k, global stats)
    end
    S0-->>BR: local top-k (global DocIds, scores identical to single node, snippets)
    S1-->>BR: local top-k
    S2--xBR: timeout / refused / garbage
    BR->>BR: merge_top_k: heap over list heads (score desc, global DocId asc)
    BR-->>U: global top-k, matched, missing_shards = [N-1] (partial)
```

## 6. Deployment and server internals

```mermaid
flowchart LR
    client["gaffer search --broker"] -- "TCP :7000<br/>length-prefixed frames" --> broker
    subgraph broker["gaffer-broker"]
        bpoll["poller thread<br/>poll(): listener + idle conns"] --> bwork["worker pool"]
        bwork --> fan["fan-out: jthread per shard<br/>pooled connection per shard"]
    end
    fan -- ":7001" --> s0["gaffer-shard 0<br/>shard-0.tsv + shard-0.gaffer"]
    fan -- ":7001" --> s1["gaffer-shard 1"]
    fan -- ":7001" --> s2["gaffer-shard 2"]
    fan -- ":7001" --> s3["gaffer-shard 3"]
    part["gaffer partition --shards 4<br/>(round-robin by DocId)"] -. "build time" .-> s0 & s1 & s2 & s3
```

Each server (`TcpServer`) is a reactor: one poller thread `poll()`s the listener and every idle
connection, hands ready connections to a worker pool, and gets them back through a self-pipe after
one request. Idle pooled connections cost a pollfd, not a thread (D-021).
`deploy/docker-compose.yml` runs this topology; `deploy/local-cluster.sh` runs it as local processes.

## 7. Global statistics: the trade-off

With document partitioning, each shard sees only its slice. Scoring with **local** statistics
(the shard's own N, avgdl and df) makes scores incomparable across shards. A term that's rare on
one shard but common overall gets an inflated idf there, so that shard's documents win the merge
for the wrong reason. In this corpus, one shard on its own scored Liverpool 1-2 Man Utd 10.269
for a query where the single-node score is 8.035.

| Approach | Exact? | Cost per query | Notes |
|---|---|---|---|
| Local stats per shard | no | none | Simple; skewed when term distribution differs across shards. Acceptable only with many docs per shard and random placement. |
| **Global stats gathered once, df sent per query** (ours, D-020) | **yes** | a few dozen bytes per term | Needs immutable indexes (stats never change after startup). Shards stay stateless. |
| Two-round query: ask shards for df, then search | yes | an extra round trip | Works with mutable indexes; doubles fan-out latency. |
| Push global stats to shards and cache them | yes | none | Shards become stateful: a restarted shard must be re-synced, and two brokers can disagree. |
| Approximate / sampled global df | almost | none | What large engines often accept; exactness traded for simplicity. |

Exactness is tested: sharded results equal single-node results (documents, order, and
scores compared with `==`) for N = 1–4 on every evaluation and golden query, and on the full
13,724-doc corpus with k = 50.

## 8. As built

| Module | Files | Key types / functions |
|--------|-------|-----------------------|
| common | `src/common/` | `gaffer::Error`, `IoError`, `ParseError`, `FormatError`; `DocId`; `crc32` |
| corpus | `src/corpus/` | `Document`, `Corpus`, `load_tsv(_file/_files)`, `fingerprint` |
| text | `src/text/` | `decode_utf8`, `fold_code_point`, `tokenize`, `is_stopword`, `s_stem`, `Analyzer` |
| index | `src/index/` | `partition`, `shard_of`/`global_of`, `PostingList`, `InvertedIndex`, `IndexBuilder`, `PostingCursor::advance_to`, `put_varint`/`VarintReader`, `encode/decode_posting_list`, `serialize_index`/`deserialize_index` |
| ranking | `src/ranking/` | `Bm25`, `Explanation`/`Contribution` |
| query | `src/query/` | `parse_query`, `count_phrase_matches`, `NicknameExpander`, `Searcher::search`/`explain`, `TopK`, `ranks_before` |
| snippet | `src/snippet/` | `make_snippet`, `render` |
| eval | `src/eval/` | `precision_at`, `recall_at`, `reciprocal_rank`, `load_qrels` |
| net | `src/net/` | `Fd`, `Deadline`, `IoStatus`, `read_exact`/`write_all`, `read_frame`/`write_frame`, `Message` codec, `exchange`/`call` |
| server | `src/server/` | `TcpServer` (reactor), `ShardServer`, `Broker`, `merge_stats`, `merge_top_k`, `parse_address` |
| apps | `apps/` | `gaffer` (index/search/partition/--broker), `gaffer-shard`, `gaffer-broker`, `gaffer-bench`, `gaffer-eval` |
| generators | `tools/corpusgen/`, `eval/make_qrels.py` | `parse_file`, `derive`, `render_*`, `history.parse_season`; qrels from criteria |

## 9. Key design choices (details in DECISIONS.md)
- Immutable indexes: build once, query many. This makes global stats exact and cheap (D-014).
- Doc-partitioned shards (D-013).
- Raw TCP with length-prefixed frames (D-005).
- Exceptions only at load/parse boundaries (D-009).
- Phrases required, free words optional; a phrase scores as one BM25 pseudo-term (D-015).
- Compressed on disk, decoded at load (D-017).
- Nickname expansion on raw tokens, additive, weight 0.7 (D-018).
- Round-robin partitioning by DocId (D-019); stateless shards with per-query global df (D-020);
  reactor servers and thread-per-shard fan-out (D-021).
