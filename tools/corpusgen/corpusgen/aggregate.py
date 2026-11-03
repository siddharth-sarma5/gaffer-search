"""Season-level aggregates: league table, club documents, player documents, season document.

Everything is computed from the parsed matches, so the numbers agree with the
match documents by construction.
"""

from __future__ import annotations

import re
import unicodedata
from collections import Counter, defaultdict
from dataclasses import dataclass, field

from corpusgen.clubs import club
from corpusgen.derive import MatchFacts
from corpusgen.parse import Goal, Match
from corpusgen.render import SEASON_LABEL, match_id, ordinal
from corpusgen.tsv import Document

RELEGATION_PLACES = 3


def slugify(name: str) -> str:
    ascii_ = unicodedata.normalize("NFKD", name).encode("ascii", "ignore").decode()
    return re.sub(r"[^a-z0-9]+", "-", ascii_.lower()).strip("-")


def _fmt_date(m: Match) -> str:
    return f"{m.date.day} {m.date:%b %Y}"


def _scoreline(m: Match) -> str:
    return f"{club(m.home).name} {m.home_score}-{m.away_score} {club(m.away).name}"


# --- league table --------------------------------------------------------------


@dataclass
class TableRow:
    team: str
    played: int = 0
    won: int = 0
    drawn: int = 0
    lost: int = 0
    goals_for: int = 0
    goals_against: int = 0
    home: list[int] = field(default_factory=lambda: [0, 0, 0])  # W D L
    away: list[int] = field(default_factory=lambda: [0, 0, 0])

    @property
    def points(self) -> int:
        return 3 * self.won + self.drawn

    @property
    def goal_difference(self) -> int:
        return self.goals_for - self.goals_against


def league_table(matches: list[Match]) -> list[TableRow]:
    rows: dict[str, TableRow] = {}
    for m in matches:
        for team, gf, ga, venue in ((m.home, m.home_score, m.away_score, "home"),
                                    (m.away, m.away_score, m.home_score, "away")):
            r = rows.setdefault(team, TableRow(team))
            r.played += 1
            r.goals_for += gf
            r.goals_against += ga
            idx = 0 if gf > ga else 1 if gf == ga else 2  # index into W/D/L
            if idx == 0:
                r.won += 1
            elif idx == 1:
                r.drawn += 1
            else:
                r.lost += 1
            getattr(r, venue)[idx] += 1
    # Premier League order: points, goal difference, goals scored (then name for determinism).
    return sorted(rows.values(),
                  key=lambda r: (-r.points, -r.goal_difference, -r.goals_for, r.team))


# --- clubs -------------------------------------------------------------------


def _wdl(x: list[int]) -> str:
    return f"won {x[0]}, drawn {x[1]}, lost {x[2]}"


def render_clubs(matches: list[Match], facts: dict[str, MatchFacts],
                 table: list[TableRow]) -> list[Document]:
    docs = []
    n_teams = len(table)
    for pos, row in enumerate(table, 1):
        team = row.team
        c = club(team)
        played = [m for m in matches if team in (m.home, m.away)]
        home_venues = Counter((m.venue, m.city) for m in played if m.home == team)
        (venue, city), _ = home_venues.most_common(1)[0]
        scorers = Counter(g.scorer for m in played for g in m.goals
                          if g.team == team and not g.own_goal)
        captains = Counter(m.lineups[team].captain for m in played if m.lineups[team].captain)
        squad = {p for m in played for p in m.lineups[team].players}

        def margin(m: Match, t: str = team) -> int:
            return (m.home_score - m.away_score) * (1 if m.home == t else -1)

        best = max(played, key=lambda m: (margin(m), m.date))
        worst = min(played, key=lambda m: (margin(m), m.date))
        clean_sheets = sum(team in facts[match_id(m)].clean_sheets for m in played)
        reds = sum(1 for m in played for cd in m.cards
                   if cd.team == team and cd.kind != "yellow")

        nick = " or ".join(f"the {n}" for n in c.nicknames)
        aka = f", also known as {c.short}" if c.short != c.name else ""
        out = [f"{c.name}, nicknamed {nick}{aka}.",
               f"Home ground: {venue}, {city}.",
               f"Premier League {SEASON_LABEL}: finished {ordinal(pos)} of {n_teams} with "
               f"{row.points} points from {row.played} matches ({_wdl([row.won, row.drawn, row.lost])}), "
               f"scored {row.goals_for}, conceded {row.goals_against}, "
               f"goal difference {row.goal_difference:+d}.",
               f"Home record: {_wdl(row.home)}. Away record: {_wdl(row.away)}."]
        if pos == 1:
            out.append(f"{c.name} were the {SEASON_LABEL} Premier League champions.")
        if pos > n_teams - RELEGATION_PLACES:
            out.append(f"{c.name} were relegated from the Premier League.")
        if scorers:
            top = ", ".join(f"{p} {n}" for p, n in
                            sorted(scorers.items(), key=lambda kv: (-kv[1], kv[0]))[:5])
            out.append(f"Top scorers: {top}.")
        if margin(best) > 0:
            out.append(f"Biggest win: {_scoreline(best)} on {_fmt_date(best)}.")
        if margin(worst) < 0:
            out.append(f"Heaviest defeat: {_scoreline(worst)} on {_fmt_date(worst)}.")
        out.append(f"Clean sheets: {clean_sheets}. Red cards: {reds}. "
                   f"Players used: {len(squad)}.")
        if captains:
            cap, n = captains.most_common(1)[0]
            out.append(f"Most frequent captain: {cap} ({n} matches).")
        docs.append(Document(f"club-{c.slug}", "club", c.name, " ".join(out)))
    return docs


# --- players -----------------------------------------------------------------


@dataclass
class PlayerStats:
    name: str
    clubs: list[str] = field(default_factory=list)  # in order of first appearance
    starts: int = 0
    sub_apps: int = 0
    captain: int = 0
    goals: list[tuple[Goal, Match]] = field(default_factory=list)
    own_goals: int = 0
    yellows: int = 0
    reds: int = 0


def player_stats(matches: list[Match]) -> dict[str, PlayerStats]:
    stats: dict[str, PlayerStats] = {}

    def get(name: str) -> PlayerStats:
        return stats.setdefault(name, PlayerStats(name))

    for m in matches:
        for team, lu in sorted(m.lineups.items()):
            for p in lu.starters:
                s = get(p)
                s.starts += 1
                if team not in s.clubs:
                    s.clubs.append(team)
            for sub in lu.substitutions:
                s = get(sub.player_on)
                s.sub_apps += 1
                if team not in s.clubs:
                    s.clubs.append(team)
            if lu.captain:
                get(lu.captain).captain += 1
        for g in m.goals:
            if g.own_goal:
                get(g.scorer).own_goals += 1
            else:
                get(g.scorer).goals.append((g, m))
        for cd in m.cards:
            if cd.player in stats:  # bench bookings for players who never appeared are skipped
                if cd.kind == "yellow":
                    stats[cd.player].yellows += 1
                else:
                    stats[cd.player].reds += 1
    return stats


def render_players(matches: list[Match]) -> list[Document]:
    docs = []
    slugs: dict[str, str] = {}
    for name, s in sorted(player_stats(matches).items()):
        if s.starts + s.sub_apps == 0:
            continue
        slug = slugify(name)
        if slug in slugs:
            raise ValueError(f"player slug collision: {name!r} and {slugs[slug]!r}")
        slugs[slug] = name

        clubs = " and ".join(club(t).name for t in s.clubs)
        apps = s.starts + s.sub_apps
        out = [f"{name}, {clubs}.",
               f"Premier League {SEASON_LABEL}: {apps} appearances "
               f"({s.starts} starts, {s.sub_apps} as a substitute)."]
        if s.goals:
            pens = sum(g.penalty for g, _ in s.goals)
            per_match = Counter(match_id(m) for _, m in s.goals)
            braces = sum(n == 2 for n in per_match.values())
            hats = sum(n >= 3 for n in per_match.values())
            line = f"Scored {len(s.goals)} goal{'s' if len(s.goals) != 1 else ''}"
            if pens:
                line += f" ({pens} penalt{'ies' if pens != 1 else 'y'})"
            out.append(line + ".")
            if braces:
                out.append(f"Braces: {braces}.")
            if hats:
                out.append(f"Hat-tricks: {hats}.")
            victims = Counter(club(m.away if g.team == m.home else m.home).name
                              for g, m in s.goals)
            out.append("Scored against: " + ", ".join(
                f"{t} {n}" for t, n in sorted(victims.items(), key=lambda kv: (-kv[1], kv[0])))
                + ".")
        else:
            out.append("No goals.")
        if s.own_goals:
            out.append(f"Own goals: {s.own_goals}.")
        if s.captain:
            out.append(f"Captain in {s.captain} matches.")
        out.append(f"Yellow cards: {s.yellows}. Red cards: {s.reds}.")
        docs.append(Document(f"player-{slug}", "player", name, " ".join(out)))
    return docs


# --- season ------------------------------------------------------------------


def render_season(matches: list[Match], table: list[TableRow]) -> Document:
    stats = player_stats(matches)
    top = sorted(stats.values(), key=lambda s: (-len(s.goals), s.name))[:5]
    champs, runners_up = club(table[0].team).name, club(table[1].team).name
    relegated = [club(r.team).name for r in table[-RELEGATION_PLACES:]]
    goals = sum(m.home_score + m.away_score for m in matches)
    first, last = min(m.date for m in matches), max(m.date for m in matches)
    out = [f"Premier League {SEASON_LABEL} season, {first:%d %B %Y} to {last:%d %B %Y}.",
           f"Champions: {champs} with {table[0].points} points. Runners-up: {runners_up}.",
           f"Relegated: {', '.join(relegated)}.",
           f"Top scorers (Golden Boot race): "
           + ", ".join(f"{s.name} ({club(s.clubs[0]).name}) {len(s.goals)}" for s in top) + ".",
           f"{len(matches)} matches, {goals} goals.",
           "Final table: " + "; ".join(
               f"{i} {club(r.team).name} {r.points} pts" for i, r in enumerate(table, 1)) + "."]
    return Document(f"season-{SEASON_LABEL}", "season",
                    f"Premier League {SEASON_LABEL} season", " ".join(out))
