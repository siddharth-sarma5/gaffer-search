# Architecture

*Status: design draft (pre-M1). Parts marked **(M2)** or **(M3)** are planned, not built.
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
        bench["gaffer-bench (M2)"]
        shard["gaffer-shard (M3)"]
        broker["gaffer-broker (M3)"]
    end

    subgraph core["src/ — library: gaffer_core"]
        corpusm["corpus/<br/>Document · TsvLoader"]
        text["text/<br/>Tokenizer · AsciiFold · Stopwords · SStemmer · Analyzer"]
        index["index/<br/>InvertedIndex · Postings · IndexBuilder<br/>Varint · Delta · IndexFile (M2)"]
        ranking["ranking/<br/>Bm25 · Explanation (M2)"]
        query["query/<br/>QueryParser (M2) · NicknameExpander (M2)<br/>Evaluator TAAT→DAAT · TopK"]
        snippet["snippet/<br/>Snippeter (M2)"]
        net["net/ (M3)<br/>Socket · Framing · Codec"]
        server["server/ (M3)<br/>ShardServer · Broker · GlobalStats"]
    end

    cli --> corpusm & index & query & snippet
    bench --> index & query
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
    raw[1-premierleague-full.txt] --> parse[corpusgen: parse match blocks]
    parse --> derive[derive tags<br/>late winner · comeback · hat-trick ·<br/>clean sheet · derby · red card]
    derive --> write[write docs<br/>match · club · player]
    write --> tsv[(corpus.tsv)]
    tsv --> load[TsvLoader<br/>→ Document]
    load --> analyze[Analyzer<br/>tokenize → fold → stop → stem]
    analyze --> build[IndexBuilder<br/>term → postings<br/>doc lengths, N, avgdl]
    build --> mem[(In-memory index)]
    mem -- M2: delta + varint, CRC32 --> file[(index.gaffer)]
    file -- M3: partition by doc --> shards[(shard_0..N-1)]
```

## 4. Data flow — querying (single node, M1–M2)

```mermaid
sequenceDiagram
    actor U as User
    participant C as gaffer CLI
    participant P as QueryParser (M2)
    participant A as Analyzer
    participant X as NicknameExpander (M2)
    participant E as Evaluator
    participant I as InvertedIndex
    participant B as Bm25
    participant S as Snippeter (M2)

    U->>C: search "gunners late winner"
    C->>P: parse (terms, "phrases")
    P->>A: analyze each clause
    A-->>X: [gunners, late, winner]
    X-->>E: [arsenal(w=0.7), gunners, late, winner]
    E->>I: postings for each term
    loop each candidate doc (TAAT in M1, DAAT in M2)
        E->>B: score(tf, df, dl, avgdl)
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

## 6. Key design choices (details in DECISIONS.md)
- Immutable indexes: build once, query many. This makes global stats exact and cheap (D-014).
- Doc-partitioned shards (D-013).
- Raw TCP with length-prefixed frames (D-005).
- Exceptions only at load/parse boundaries (D-009).
