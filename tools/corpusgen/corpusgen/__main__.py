"""CLI: python -m corpusgen --input <full.txt> --output <corpus.tsv>

Run from tools/corpusgen (or with it on PYTHONPATH). Output is deterministic:
the same input file always produces a byte-identical corpus.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from corpusgen.derive import derive
from corpusgen.parse import ParseError, parse_file
from corpusgen.render import render_match
from corpusgen.tsv import Document, write_corpus


def build_documents(text: str, season_start_year: int) -> list[Document]:
    matches = parse_file(text, season_start_year)
    matches.sort(key=lambda m: (m.date, m.kickoff, m.home))
    return [render_match(m, derive(m)) for m in matches]


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="corpusgen", description=__doc__)
    ap.add_argument("--input", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--season-start-year", type=int, default=2025)
    args = ap.parse_args(argv)

    try:
        docs = build_documents(args.input.read_text(encoding="utf-8"), args.season_start_year)
    except ParseError as e:
        print(f"{args.input}: {e}", file=sys.stderr)
        return 1
    n = write_corpus(docs, args.output)
    print(f"wrote {n} documents to {args.output}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
