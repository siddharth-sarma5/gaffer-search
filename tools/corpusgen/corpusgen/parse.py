"""Parser for openfootball "full" match files (e.g. 2025-26/1-premierleague-full.txt).

A match block looks like this:

    ▪ Regular Season - 1
    Fri Aug 15 19:00 UTC+0 @ Anfield, Liverpool, Att: 60315
      Liverpool v Bournemouth  4-2 (1-0)
         (Hugo EKITIKE 37', Cody GAKPO 49', Federico Chiesa 88', MOHAMED SALAH 90+4';
          Antoine SEMENYO 64', 76')

    Liverpool: ALISSON - Virgil VAN DIJK [c], Milos KERKEZ (60' Andy ROBERTSON), ...
        Yellow: Milos KERKEZ 43'

    Bournemouth: ...
    Refs: Anthony Taylor, Gary Beswick, Craig Taylor

Format quirks this parser handles (all seen in the real file):
  * Goals are grouped by the team that BENEFITED, so own goals sit with the
    opponent's goals. Groups are separated by ';', but the ';' is omitted when
    only one team scored, so we recover the side from the scoreline.
  * One scorer can have several minutes: "Antoine SEMENYO 64', 76'".
  * Substitutions nest: "A (16' B (75' C))" means B replaced A, then C replaced B.
  * Names mix case ("Erling HAALAND"); we normalise to "Erling Haaland".
  * Dates have no year; the season spans two calendar years.

Any inconsistency (e.g. goal count != score) raises ParseError with a line
number rather than silently producing a wrong corpus.
"""

from __future__ import annotations

import datetime as dt
import re
from dataclasses import dataclass, field


class ParseError(ValueError):
    def __init__(self, line_no: int, message: str) -> None:
        super().__init__(f"line {line_no}: {message}")
        self.line_no = line_no


@dataclass(frozen=True, order=True)
class Minute:
    """A match minute. 90+4 is base=90, extra=4. Ordering is chronological."""

    base: int
    extra: int = 0

    def __str__(self) -> str:
        return f"{self.base}+{self.extra}'" if self.extra else f"{self.base}'"

    @staticmethod
    def parse(text: str) -> Minute:
        base, _, extra = text.rstrip("'").partition("+")
        return Minute(int(base), int(extra) if extra else 0)


@dataclass(frozen=True)
class Goal:
    scorer: str
    minute: Minute
    team: str  # the team credited with the goal (for an own goal: the opponent of the scorer)
    penalty: bool = False
    own_goal: bool = False


@dataclass(frozen=True)
class Card:
    player: str
    minute: Minute
    team: str
    kind: str  # "yellow" | "second_yellow" | "red"


@dataclass(frozen=True)
class Substitution:
    player_off: str
    player_on: str
    minute: Minute


@dataclass
class Lineup:
    team: str
    starters: list[str] = field(default_factory=list)
    substitutions: list[Substitution] = field(default_factory=list)
    captain: str | None = None

    @property
    def players(self) -> list[str]:
        """Everyone who played: starters, then substitutes in order of appearance."""
        return self.starters + [s.player_on for s in self.substitutions]


@dataclass
class Match:
    matchday: int
    date: dt.date
    kickoff: str  # local "HH:MM"
    venue: str
    city: str
    attendance: int | None
    home: str
    away: str
    home_score: int
    away_score: int
    ht_home: int
    ht_away: int
    goals: list[Goal] = field(default_factory=list)
    cards: list[Card] = field(default_factory=list)
    lineups: dict[str, Lineup] = field(default_factory=dict)
    referee: str | None = None
    line_no: int = 0  # line of the header in the source file, for error messages


# --- name normalisation ------------------------------------------------------


def _fix_word(word: str) -> str:
    """'HAALAND' -> 'Haaland', 'VAN' -> 'Van', "O'BRIEN" -> "O'Brien".

    Mixed-case words ('McGinn', 'de') are left alone: the source only
    upper-cases surnames, so mixed case is already the intended spelling.
    """
    if not word.isupper() or len(word) == 1:
        return word
    # Capitalise each piece around hyphens and apostrophes.
    return re.sub(r"[^\W\d_]+", lambda m: m.group(0).capitalize(), word)


def normalise_name(raw: str) -> str:
    return " ".join(_fix_word(w) for w in raw.split())


# --- line-level patterns -----------------------------------------------------

_MONTHS = {m: i for i, m in enumerate(
    ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"], 1)}

_MATCHDAY_RE = re.compile(r"^▪ .*?(\d+)\s*$")
_HEADER_RE = re.compile(
    r"^(?:Mon|Tue|Wed|Thu|Fri|Sat|Sun) (?P<mon>[A-Z][a-z]{2}) (?P<day>\d{1,2}) "
    r"(?P<time>\d\d:\d\d) UTC[+-]\d+ @ (?P<venue>[^,]+), (?P<city>.+?)"
    r"(?:, Att: (?P<att>\d+))?\s*$")
_FIXTURE_RE = re.compile(
    r"^\s+(?P<home>\S.*?) v (?P<away>\S.*?)\s{2,}(?P<hs>\d+)-(?P<as>\d+) "
    r"\((?P<hth>\d+)-(?P<hta>\d+)\)\s*$")
_GOAL_ITEM_RE = re.compile(
    r"^\s*(?:(?P<name>.*?\S)\s+)?(?P<min>\d+(?:\+\d+)?)'(?:\((?P<flag>p|og)\))?\s*$")
_CARD_RE = re.compile(r"^\s+(?P<kind>Yellow|Red|Yellow-Red):\s+(?P<items>.*)$")
_CARD_ITEM_RE = re.compile(r"^\s*(?P<name>.*?\S)\s+(?P<min>\d+(?:\+\d+)?)'\s*$")
_REFS_RE = re.compile(r"^Refs:\s*(?P<refs>.*)$")

_CARD_KINDS = {"Yellow": "yellow", "Yellow-Red": "second_yellow", "Red": "red"}


def season_date(month: str, day: int, season_start_year: int) -> dt.date:
    """Aug-Dec belong to the first calendar year of the season, Jan-Jul to the second."""
    m = _MONTHS[month]
    year = season_start_year if m >= 8 else season_start_year + 1
    return dt.date(year, m, day)


# --- goals -------------------------------------------------------------------


def _parse_goal_group(text: str, team: str, line_no: int) -> list[Goal]:
    goals: list[Goal] = []
    current: str | None = None
    for item in text.split(","):
        if not item.strip():
            continue
        m = _GOAL_ITEM_RE.match(item)
        if not m:
            raise ParseError(line_no, f"unrecognised goal entry {item.strip()!r}")
        if m["name"]:
            current = normalise_name(m["name"])
        if current is None:
            raise ParseError(line_no, f"goal minute without a scorer: {item.strip()!r}")
        flag = m["flag"]
        goals.append(Goal(current, Minute.parse(m["min"]), team,
                          penalty=flag == "p", own_goal=flag == "og"))
    return goals


def parse_goals(text: str, home: str, away: str, home_score: int, away_score: int,
                line_no: int) -> list[Goal]:
    """Parse the text inside the goal parentheses and attribute each goal to a side."""
    if ";" in text:
        home_text, away_text = text.split(";", 1)
    elif home_score == 0:
        home_text, away_text = "", text
    elif away_score == 0:
        home_text, away_text = text, ""
    else:
        raise ParseError(line_no, "both teams scored but goals have no ';' separator")

    goals = (_parse_goal_group(home_text, home, line_no)
             + _parse_goal_group(away_text, away, line_no))
    got_home = sum(g.team == home for g in goals)
    got_away = len(goals) - got_home
    if (got_home, got_away) != (home_score, away_score):
        raise ParseError(line_no, f"goals list gives {got_home}-{got_away} "
                                  f"but score is {home_score}-{away_score}")
    return sorted(goals, key=lambda g: g.minute)


# --- lineups -----------------------------------------------------------------


def _split_top_level(text: str) -> list[str]:
    """Split on ',' and ' - ' (formation separators) outside parentheses.

    ' - ' needs surrounding spaces so hyphenated names (Jean-Philippe) survive.
    """
    parts, depth, start, i = [], 0, 0, 0
    while i < len(text):
        c = text[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif depth == 0 and (c == "," or text.startswith(" - ", i)):
            parts.append(text[start:i])
            i += 1 if c == "," else 3
            start = i
            continue
        i += 1
    parts.append(text[start:])
    return [p.strip() for p in parts if p.strip()]


_SUB_RE = re.compile(r"^(?P<min>\d+(?:\+\d+)?)'\s+(?P<rest>.+)$")


def _parse_player_chain(token: str, lineup: Lineup, line_no: int) -> str:
    """Parse 'Name [c] (60' Sub (80' Sub2))'; record subs; return the first player's name."""
    name_part, paren, inner = token.partition("(")
    is_captain = "[c]" in name_part
    name = normalise_name(name_part.replace("[c]", "").strip())
    if not name:
        raise ParseError(line_no, f"empty player name in {token!r}")
    if is_captain:
        lineup.captain = name
    if paren:
        if not inner.endswith(")"):
            raise ParseError(line_no, f"unbalanced parentheses in {token!r}")
        m = _SUB_RE.match(inner[:-1].strip())
        if not m:
            raise ParseError(line_no, f"unrecognised substitution {inner!r}")
        # Recurse first so the replacement's own replacement is handled.
        on = _parse_player_chain(m["rest"], lineup, line_no)
        lineup.substitutions.append(Substitution(name, on, Minute.parse(m["min"])))
    return name


def parse_lineup(team: str, text: str, line_no: int) -> Lineup:
    lineup = Lineup(team)
    for token in _split_top_level(text):
        lineup.starters.append(_parse_player_chain(token, lineup, line_no))
    lineup.substitutions.sort(key=lambda s: s.minute)
    return lineup


# --- file --------------------------------------------------------------------


def parse_file(text: str, season_start_year: int) -> list[Match]:
    """Parse a whole file into matches, in file order."""
    lines = text.splitlines()
    matches: list[Match] = []
    matchday = 0
    match: Match | None = None
    card_team: str | None = None
    i = 0

    def collect_continuation(start: int, first: str, stop) -> tuple[str, int]:
        """Join `first` with following indented lines until stop(line) or a blank line."""
        parts, j = [first], start + 1
        while j < len(lines) and lines[j].startswith(" ") and lines[j].strip() \
                and not stop(lines[j]):
            parts.append(lines[j].strip())
            j += 1
        return " ".join(parts), j

    while i < len(lines):
        line = lines[i]
        line_no = i + 1

        if m := _MATCHDAY_RE.match(line):
            matchday = int(m[1])
        elif m := _HEADER_RE.match(line):
            match = Match(matchday=matchday,
                          date=season_date(m["mon"], int(m["day"]), season_start_year),
                          kickoff=m["time"], venue=m["venue"].strip(), city=m["city"].strip(),
                          attendance=int(m["att"]) if m["att"] else None,
                          home="", away="", home_score=0, away_score=0, ht_home=0, ht_away=0,
                          line_no=line_no)
            matches.append(match)
            card_team = None
        elif match is not None and not match.home and (m := _FIXTURE_RE.match(line)):
            match.home, match.away = m["home"].strip(), m["away"].strip()
            match.home_score, match.away_score = int(m["hs"]), int(m["as"])
            match.ht_home, match.ht_away = int(m["hth"]), int(m["hta"])
            # Goal list: an indented "(...)" block directly after the fixture line.
            if i + 1 < len(lines) and lines[i + 1].strip().startswith("("):
                text_, i = collect_continuation(i + 1, lines[i + 1].strip(), lambda _: False)
                if not text_.endswith(")"):
                    raise ParseError(i, "unterminated goal list")
                match.goals = parse_goals(text_[1:-1], match.home, match.away,
                                          match.home_score, match.away_score, i + 1)
                continue
            if match.home_score + match.away_score:
                raise ParseError(line_no, "score has goals but no goal list follows")
        elif match is not None and (m := _CARD_RE.match(line)):
            if card_team is None:
                raise ParseError(line_no, "card line before any lineup")
            for item in m["items"].split(","):
                cm = _CARD_ITEM_RE.match(item)
                if not cm:
                    raise ParseError(line_no, f"unrecognised card entry {item.strip()!r}")
                match.cards.append(Card(normalise_name(cm["name"]), Minute.parse(cm["min"]),
                                        card_team, _CARD_KINDS[m["kind"]]))
        elif match is not None and (m := _REFS_RE.match(line)):
            refs = [r.strip() for r in m["refs"].split(",") if r.strip()]
            match.referee = refs[0] if refs else None
        elif match is not None and match.home and ":" in line and not line.startswith(" "):
            team, _, rest = line.partition(":")
            if team not in (match.home, match.away):
                raise ParseError(line_no, f"lineup for {team!r}, which isn't playing")
            text_, i = collect_continuation(i, rest.strip(),
                                            lambda ln: _CARD_RE.match(ln) is not None)
            match.lineups[team] = parse_lineup(team, text_, line_no)
            card_team = team
            continue
        i += 1

    for mt in matches:
        if not mt.home:
            raise ParseError(mt.line_no, "match header without a fixture line")
        if len(mt.lineups) != 2:
            raise ParseError(mt.line_no, f"expected 2 lineups, found {len(mt.lineups)}")
    return matches
