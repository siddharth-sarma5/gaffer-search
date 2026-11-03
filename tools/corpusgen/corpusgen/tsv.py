"""Corpus TSV format (docs/DECISIONS.md D-007).

    # gaffer-corpus v1
    <id>\\t<type>\\t<title>\\t<body>\\n      (one document per line)

Fields escape backslash, tab and newline as \\\\, \\t and \\n, so every record
is exactly one physical line with exactly three real tabs. The C++ loader
(src/corpus/) implements the inverse.
"""

from __future__ import annotations

from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path

HEADER = "# gaffer-corpus v1"


@dataclass(frozen=True)
class Document:
    id: str
    type: str  # "match" | "club" | "player" | "season"
    title: str
    body: str


def escape(field: str) -> str:
    return field.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n")


def write_corpus(docs: Iterable[Document], path: Path) -> int:
    seen: set[str] = set()
    n = 0
    # newline="\n" so the file is byte-identical on every OS (reproducibility).
    with path.open("w", encoding="utf-8", newline="\n") as out:
        out.write(HEADER + "\n")
        for d in docs:
            if d.id in seen:
                raise ValueError(f"duplicate document id {d.id!r}")
            seen.add(d.id)
            out.write("\t".join(escape(x) for x in (d.id, d.type, d.title, d.body)) + "\n")
            n += 1
    return n
