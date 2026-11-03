# Architecture

*Status: Milestones 1 and 2 built (2026-11-03). Parts marked **(M3)** are planned, not built.
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
        shard["gaffer-shard (M3)"]
        broker["gaffer-broker (M3)"]
    end

    subgraph core["src/ — library: gaffer_core"]
        corpusm["corpus/<br/>Document · TsvLoader"]
        text["text/<br/>Tokenizer · AsciiFold · Stopwords · SStemmer · Analyzer"]
        index["index/<br/>InvertedIndex · PostingList · PostingCursor<br/>IndexBuilder · varint · postings codec · index file"]
        ranking["ranking/<br/>Bm25 · Explanation"]
        query["query/<br/>Searcher (DAAT) · TopK · phrase matching<br/>query parser · NicknameExpander"]
        snippet["snippet/<br/>make_snippet · render"]
        evalm["eval/<br/>precision@k · recall@k · MRR"]
        net["net/ (M3)<br/>Socket · Framing · Codec"]
        server["server/ (M3)<br/>ShardServer · Broker · GlobalStats"]
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
    actor U as Client
    participant BR as Broker
    participant S0 as Shard 0
    participant S1 as Shard 1
    participant S2 as Shard N-1

    Note over BR,S2: Startup: broker gathers N, total length and per-term df from every shard → GlobalStats
    U->>BR: Search(q, k)
    par fan-out with per-shard deadline
        BR->>S0: Search(q, k, global df/N/avgdl)
        BR->>S1: Search(q, k, global df/N/avgdl)
        BR->>S2: Search(q, k, global df/N/avgdl)
    end
    S0-->>BR: local top-k
    S1-->>BR: local top-k
    S2--xBR: timeout / connection refused
    BR->>BR: k-way merge (score desc, doc id asc)
    BR-->>U: top-k, partial=true, missing=[N-1]
```

## 6. As built (M1 + M2)

| Module | Files | Key types / functions |
|--------|-------|-----------------------|
| common | `src/common/` | `gaffer::Error`, `IoError`, `ParseError`, `FormatError`; `DocId`; `crc32` |
| corpus | `src/corpus/` | `Document`, `Corpus`, `load_tsv(_file/_files)`, `fingerprint` |
| text | `src/text/` | `decode_utf8`, `fold_code_point`, `tokenize`, `is_stopword`, `s_stem`, `Analyzer` |
| index | `src/index/` | `PostingList`, `InvertedIndex`, `IndexBuilder`, `PostingCursor::advance_to`, `put_varint`/`VarintReader`, `encode/decode_posting_list`, `serialize_index`/`deserialize_index` |
| ranking | `src/ranking/` | `Bm25`, `Explanation`/`Contribution` |
| query | `src/query/` | `parse_query`, `count_phrase_matches`, `NicknameExpander`, `Searcher::search`/`explain`, `TopK`, `ranks_before` |
| snippet | `src/snippet/` | `make_snippet`, `render` |
| eval | `src/eval/` | `precision_at`, `recall_at`, `reciprocal_rank`, `load_qrels` |
| apps | `apps/` | `gaffer` (index/search), `gaffer-bench`, `gaffer-eval` |
| generators | `tools/corpusgen/`, `eval/make_qrels.py` | `parse_file`, `derive`, `render_*`, `history.parse_season`; qrels from criteria |

## 7. Key design choices (details in DECISIONS.md)
- Immutable indexes: build once, query many. This makes global stats exact and cheap (D-014).
- Doc-partitioned shards (D-013).
- Raw TCP with length-prefixed frames (D-005).
- Exceptions only at load/parse boundaries (D-009).
- Phrases required, free words optional; a phrase scores as one BM25 pseudo-term (D-015).
- Compressed on disk, decoded at load (D-017).
- Nickname expansion on raw tokens, additive, weight 0.7 (D-018).
