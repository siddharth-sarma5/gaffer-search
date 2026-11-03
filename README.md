# gaffer-search — *Ask the Gaffer*

A from-scratch search engine in C++20 over a Premier League corpus. Type
`"late winner at Anfield"` or `"Haaland hat-trick"` and get BM25-ranked results with snippets.
It covers the core pieces of Information Retrieval: tokenization, inverted index, BM25,
top-k retrieval, phrase queries, index compression, and (later) sharded index servers behind
a broker.

> **Status:** planning. Nothing is runnable yet. See [docs/PLAN.md](docs/PLAN.md) and
> [docs/PROGRESS.md](docs/PROGRESS.md).

## Docs
- [Requirements and scope](docs/REQUIREMENTS.md)
- [Plan and commit checklist](docs/PLAN.md)
- [Architecture (Mermaid diagrams)](docs/ARCHITECTURE.md)
- [Decision log](docs/DECISIONS.md)
- [Progress](docs/PROGRESS.md)

## Data
Match data comes from [openfootball/england](https://github.com/openfootball/england)
(CC0 / public domain). All documents are generated from facts (scores, dates, scorers, venues);
no news articles or match reports are used.
