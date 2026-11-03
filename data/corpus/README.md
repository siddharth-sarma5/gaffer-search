# Generated corpus

`pl-2025-26.tsv` is generated. **Don't edit it by hand.** Regenerate it with:

```sh
cd tools/corpusgen
python3 -m corpusgen --input ../../data/raw/openfootball/2025-26/1-premierleague-full.txt \
                     --output ../../data/corpus/pl-2025-26.tsv
```

CI regenerates it and fails if the result differs from the committed file, byte for byte.

Format: `# gaffer-corpus v1` header line, then one document per line:
`id <TAB> type <TAB> title <TAB> body`, with `\\`, `\t` and `\n` escaped (DECISIONS D-007).

## Measured statistics (2026-11-02)

"Words" are whitespace-separated runs in title + body, counted before our tokenizer runs.
Token counts after analysis will be reported once the C++ analyzer exists.

| Type | Docs | Words | Mean words/doc | Min | Max |
|------|-----:|------:|---------------:|----:|----:|
| match | 380 | 78,152 | 205.7 | 150 | 306 |
| player | 537 | 17,098 | 31.8 | 22 | 74 |
| club | 20 | 2,279 | 114.0 | 105 | 129 |
| season | 1 | 159 | 159.0 | 159 | 159 |
| **total** | **938** | **97,688** | 104.1 | | |

File size: 694,768 bytes. SHA-256 `a83f73a89fae7e06d7e6e9b5fd294b99a6ed481c40043a3c71f945292079f6db`.

Note: the mixed lengths (32-word player docs vs 206-word match docs) are exactly what BM25's
length normalisation (`b`) exists for.
