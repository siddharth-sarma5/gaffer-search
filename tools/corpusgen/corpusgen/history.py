"""Results-only documents for Premier League seasons 1992-93 to 2024-25 (D-016).

These seasons have dates, teams and full-time scores (half-time scores from
2004-05 onwards in most files) but no scorers, venues or lineups. They form the
*benchmark* corpus: ~12.8k realistic, short documents to measure indexing and
query speed at scale. Relevance is evaluated on the rich 2025-26 corpus.

The files come in three layouts, all handled here:

  1990s        ▪ Matchday 1 / Sat Aug 15 /   Arsenal FC   2-4  Norwich City FC
  2000s-2023   ▪ Matchday 1 / Sat Aug 13 /   15:00  Aston Villa   2-2 (2-2)  Bolton Wanderers
  2024-25      ▪ Matchday 1 /   Fri Aug 16 2024 /     20:00  Man Utd FC  v Fulham FC   1-0 (0-0)

Usage: python -m corpusgen.history --input-dir DIR --output FILE
where DIR holds <season>/1-premierleague.txt for each season.
"""

from __future__ import annotations

import argparse
import datetime as dt
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

from corpusgen.aggregate import slugify
from corpusgen.parse import ParseError, season_date
from corpusgen.tsv import Document, write_corpus

# 1990s files abbreviate some names; map them to the full names used elsewhere.
_ALIASES = {
    "Bradford": "Bradford City",
    "Coventry": "Coventry City",
    "Derby": "Derby County",
    "Leeds": "Leeds United",
    "Leicester": "Leicester City",
    "Newcastle Utd": "Newcastle United",
    "Sheffield Wed": "Sheffield Wednesday",
    "Tottenham": "Tottenham Hotspur",
    "West Ham": "West Ham United",
}

_MATCHDAY_RE = re.compile(r"^▪ .*?(\d+)\s*$")
_DATE_RE = re.compile(
    r"^\s*(?:Mon|Tue|Wed|Thu|Fri|Sat|Sun) (?P<mon>[A-Z][a-z]{2}) (?P<day>\d{1,2})(?: (?P<year>\d{4}))?\s*$")
_TIME_PREFIX_RE = re.compile(r"^\d\d[:.]\d\d\s+")
_SCORE = r"(?P<hs>\d+)-(?P<as>\d+)(?:\s+\((?P<hth>\d+)-(?P<hta>\d+)\))?"
_V_LAYOUT_RE = re.compile(rf"^(?P<home>.+?)\s+v\s+(?P<away>.+?)\s+{_SCORE}$")
_SCORE_LAYOUT_RE = re.compile(rf"^(?P<home>.+?)\s+{_SCORE}\s+(?P<away>.+)$")


def normalise_team(raw: str) -> str:
    """'Arsenal FC' -> 'Arsenal', 'Sunderland AFC' -> 'Sunderland', 'Sheffield Wed' -> 'Sheffield Wednesday'."""
    name = re.sub(r"\s+A?FC$", "", raw.strip())
    return _ALIASES.get(name, name)


@dataclass(frozen=True)
class Result:
    season: str  # "1995-96"
    matchday: int
    date: dt.date
    home: str
    away: str
    home_score: int
    away_score: int
    half_time: tuple[int, int] | None


def parse_season(text: str, season: str) -> list[Result]:
    start_year = int(season[:4])
    results: list[Result] = []
    matchday = 0
    date: dt.date | None = None
    for line_no, line in enumerate(text.splitlines(), 1):
        if not line.strip() or line.startswith(("=", "#")):
            continue
        if m := _MATCHDAY_RE.match(line):
            matchday = int(m[1])
            continue
        if m := _DATE_RE.match(line):
            date = season_date(m["mon"], int(m["day"]), start_year)
            if m["year"] and int(m["year"]) != date.year:
                raise ParseError(line_no, f"date year {m['year']} doesn't fit season {season}")
            continue
        if not line.startswith(" "):
            raise ParseError(line_no, f"unrecognised line {line.strip()!r}")
        body = _TIME_PREFIX_RE.sub("", line.strip())
        m = _V_LAYOUT_RE.match(body) or _SCORE_LAYOUT_RE.match(body)
        if not m:
            raise ParseError(line_no, f"unrecognised fixture {line.strip()!r}")
        if date is None or matchday == 0:
            raise ParseError(line_no, "fixture before any matchday/date line")
        ht = (int(m["hth"]), int(m["hta"])) if m["hth"] is not None else None
        results.append(Result(season, matchday, date, normalise_team(m["home"]),
                              normalise_team(m["away"]), int(m["hs"]), int(m["as"]), ht))
    _validate_round_robin(results, season)
    return results


def _validate_round_robin(results: list[Result], season: str) -> None:
    """A full double round robin: each of n teams plays 2(n-1) games, n(n-1) in total."""
    games = Counter([r.home for r in results] + [r.away for r in results])
    n = len(games)
    bad = {t: c for t, c in games.items() if c != 2 * (n - 1)}
    if bad or len(results) != n * (n - 1):
        raise ParseError(0, f"{season}: not a complete round robin ({len(results)} results, "
                            f"{n} teams, unexpected counts {bad})")
    pairs = Counter((r.home, r.away) for r in results)
    if any(c != 1 for c in pairs.values()):
        raise ParseError(0, f"{season}: a home/away pairing appears more than once")


def _tags(r: Result) -> tuple[list[str], list[str]]:
    """(tags, extra sentences) derived from the scoreline only."""
    tags: list[str] = []
    sentences: list[str] = []
    total, margin = r.home_score + r.away_score, abs(r.home_score - r.away_score)
    if r.home_score == r.away_score:
        tags.append("goalless draw" if total == 0 else "score draw")
        winner = loser = None
    else:
        home_won = r.home_score > r.away_score
        tags.append("home win" if home_won else "away win")
        winner, loser = (r.home, r.away) if home_won else (r.away, r.home)
    if r.half_time and winner:
        ht_h, ht_a = r.half_time
        trailing_at_ht = (ht_h < ht_a) if winner == r.home else (ht_a < ht_h)
        if trailing_at_ht:
            tags.append("comeback win")
            sentences.append(f"{winner} came from behind at half-time to win, a comeback.")
        loser_led = (ht_h > ht_a) if loser == r.home else (ht_a > ht_h)
        if loser_led:
            tags.append("half-time lead lost")
    if winner and margin >= 4:
        tags.append("thrashing")
    if total >= 5:
        tags.append("high-scoring")
    for team, conceded in ((r.home, r.away_score), (r.away, r.home_score)):
        if conceded == 0:
            sentences.append(f"{team} kept a clean sheet.")
            if "clean sheet" not in tags:
                tags.append("clean sheet")
    return tags, sentences


def render_result(r: Result) -> Document:
    date = f"{r.date:%A} {r.date.day} {r.date:%B %Y}"
    out = [f"Premier League {r.season}, matchday {r.matchday}. {r.home} v {r.away} on {date}."]
    if r.home_score == r.away_score:
        out.append(f"{r.home} and {r.away} played out a goalless draw." if r.home_score == 0
                   else f"{r.home} and {r.away} drew {r.home_score}-{r.away_score}.")
    else:
        home_won = r.home_score > r.away_score
        w, lo = (r.home, r.away) if home_won else (r.away, r.home)
        hi, low = max(r.home_score, r.away_score), min(r.home_score, r.away_score)
        out.append(f"{w} beat {lo} {hi}-{low} {'at home' if home_won else 'away'}.")
    if r.half_time:
        out.append(f"Half-time score: {r.home} {r.half_time[0]}-{r.half_time[1]} {r.away}.")
    tags, sentences = _tags(r)
    out.extend(sentences)
    out.append("Tags: " + ", ".join(tags) + ".")
    key = f"hist-{r.date.isoformat()}-{slugify(r.home)}-{slugify(r.away)}"
    title = f"{r.home} {r.home_score}-{r.away_score} {r.away} ({r.date.day} {r.date:%b %Y})"
    return Document(key, "match", title, " ".join(out))


def build_documents(input_dir: Path) -> list[Document]:
    seasons = sorted(p.name for p in input_dir.iterdir()
                     if p.is_dir() and re.fullmatch(r"\d{4}-\d{2}", p.name))
    docs: list[Document] = []
    for season in seasons:
        text = (input_dir / season / "1-premierleague.txt").read_text(encoding="utf-8")
        try:
            results = parse_season(text, season)
        except ParseError as e:
            raise ParseError(e.line_no, f"{season}: {e}") from None
        docs.extend(render_result(r) for r in results)
    return docs


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="corpusgen.history", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--input-dir", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args(argv)
    try:
        docs = build_documents(args.input_dir)
    except ParseError as e:
        print(f"{args.input_dir}: {e}", file=sys.stderr)
        return 1
    n = write_corpus(docs, args.output)
    print(f"wrote {n} documents to {args.output}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
