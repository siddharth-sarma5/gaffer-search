"""Renders parsed matches and derived facts as plain factual English.

The wording is deliberate. It uses the words fans search with ("late winner",
"hat-trick", "sent off", venue names), and each fact appears once, so term
frequency reflects real emphasis. A hat-trick scorer's name appears in the
goal list, in the hat-trick sentence and in the lineup; a substitute who
didn't score appears only in the lineup.
"""

from __future__ import annotations

from corpusgen.clubs import club
from corpusgen.derive import MatchFacts
from corpusgen.parse import Goal, Match, Minute
from corpusgen.tsv import Document

SEASON_LABEL = "2025-26"


def ordinal(n: int) -> str:
    suffix = "th" if 10 <= n % 100 <= 20 else {1: "st", 2: "nd", 3: "rd"}.get(n % 10, "th")
    return f"{n}{suffix}"


def minute_phrase(m: Minute) -> str:
    """'88th minute' or '4th minute of stoppage time (90+4')'."""
    if m.extra:
        return f"{ordinal(m.extra)} minute of stoppage time ({m})"
    return f"{ordinal(m.base)} minute"


def match_id(m: Match) -> str:
    return f"match-{m.date.isoformat()}-{club(m.home).slug}-{club(m.away).slug}"


def _goal_entry(g: Goal) -> str:
    notes = []
    if g.penalty:
        notes.append("penalty")
    if g.own_goal:
        notes.append("own goal")
    notes.append(club(g.team).name)
    return f"{g.scorer} {g.minute} ({', '.join(notes)})"


def _lineup_sentence(m: Match, team: str) -> str:
    lu = m.lineups[team]
    s = f"{club(team).name} lineup: {', '.join(lu.starters)}."
    if lu.captain:
        s += f" Captain: {lu.captain}."
    if lu.substitutions:
        subs = ", ".join(f"{x.player_on} for {x.player_off} {x.minute}" for x in lu.substitutions)
        s += f" Substitutes: {subs}."
    return s


def render_match(m: Match, f: MatchFacts) -> Document:
    home, away = club(m.home).name, club(m.away).name
    date = f"{m.date:%A} {m.date.day} {m.date:%B %Y}"
    out: list[str] = [
        f"Premier League {SEASON_LABEL}, matchday {m.matchday}. {home} v {away} at {m.venue}, "
        f"{m.city}, on {date}, kick-off {m.kickoff}."
    ]

    if f.winner is None:
        if m.home_score == 0:
            out.append(f"{home} and {away} played out a goalless draw.")
        else:
            out.append(f"{home} and {away} drew {m.home_score}-{m.away_score}.")
    else:
        w, lo = club(f.winner).name, club(f.loser).name
        hi, low = max(m.home_score, m.away_score), min(m.home_score, m.away_score)
        venue = "at home" if f.winner == m.home else "away"
        out.append(f"{w} beat {lo} {hi}-{low} {venue}.")
    out.append(f"Half-time score: {home} {m.ht_home}-{m.ht_away} {away}.")

    if m.goals:
        out.append("Goals: " + "; ".join(_goal_entry(g) for g in m.goals) + ".")
    for player, team, n in f.hat_tricks:
        count = "" if n == 3 else f" ({n} goals)"
        out.append(f"{player} scored a hat-trick{count} for {club(team).name}.")
    for player, team in f.braces:
        out.append(f"{player} scored twice, a brace, for {club(team).name}.")
    if f.late_winner and f.winning_goal:
        g = f.winning_goal
        out.append(f"{g.scorer} scored a late winner for {club(g.team).name} "
                   f"in the {minute_phrase(g.minute)}.")
    if f.late_equaliser:
        g = f.late_equaliser
        out.append(f"{g.scorer} scored a late equaliser for {club(g.team).name} "
                   f"in the {minute_phrase(g.minute)}.")
    if f.comeback_from:
        deficit = "a goal" if f.comeback_from == 1 else f"{f.comeback_from} goals"
        out.append(f"{club(f.winner).name} came from {deficit} down to win, a comeback.")
    if f.led_at_half_time_and_lost:
        out.append(f"{club(f.led_at_half_time_and_lost).name} led at half-time but lost.")
    for g in f.stoppage_time_goals:
        if g is not f.winning_goal or not f.late_winner:
            out.append(f"{g.scorer} scored in stoppage time ({g.minute}).")
    for t in f.clean_sheets:
        out.append(f"{club(t).name} kept a clean sheet.")
    for c in f.sent_off:
        how = "after a second yellow card" if c.kind == "second_yellow" else "with a straight red card"
        out.append(f"{c.player} of {club(c.team).name} was sent off {how} ({c.minute}).")
    if f.derby:
        out.append(f"This was the {f.derby}.")

    if m.attendance is not None:
        out.append(f"Attendance: {m.attendance:,}.")
    if m.referee:
        out.append(f"Referee: {m.referee}.")
    out.append(_lineup_sentence(m, m.home))
    out.append(_lineup_sentence(m, m.away))
    yellows = sorted((c for c in m.cards if c.kind == "yellow"), key=lambda c: c.minute)
    if yellows:
        out.append("Yellow cards: " + ", ".join(f"{c.player} {c.minute}" for c in yellows) + ".")
    out.append("Tags: " + ", ".join(f.tags) + ".")

    title = f"{home} {m.home_score}-{m.away_score} {away} ({m.date.day} {m.date:%b %Y})"
    return Document(match_id(m), "match", title, " ".join(out))
