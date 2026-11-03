"""Derives searchable facts ("late winner", "comeback win", ...) from a parsed Match.

Every tag is computed from the goal timeline and cards. Nothing is invented.
Thresholds are named constants so the definitions are explicit and testable.
"""

from __future__ import annotations

from collections import Counter
from dataclasses import dataclass, field

from corpusgen.clubs import derby_name
from corpusgen.parse import Card, Goal, Match

LATE_MINUTE = 80  # a goal at 80' or later counts as "late"
THRASHING_MARGIN = 4  # win by 4+ goals
HIGH_SCORING_TOTAL = 5  # 5+ goals in the match


@dataclass
class MatchFacts:
    winner: str | None  # None for a draw
    loser: str | None
    winning_goal: Goal | None = None  # goal that put the winner ahead for good
    late_winner: bool = False
    late_equaliser: Goal | None = None
    comeback_from: int = 0  # largest deficit the winner overturned (0 = never behind)
    led_at_half_time_and_lost: str | None = None
    clean_sheets: list[str] = field(default_factory=list)
    hat_tricks: list[tuple[str, str, int]] = field(default_factory=list)  # (player, team, goals)
    braces: list[tuple[str, str]] = field(default_factory=list)
    stoppage_time_goals: list[Goal] = field(default_factory=list)
    penalties: list[Goal] = field(default_factory=list)
    own_goals: list[Goal] = field(default_factory=list)
    sent_off: list[Card] = field(default_factory=list)
    derby: str | None = None
    tags: list[str] = field(default_factory=list)


def derive(m: Match) -> MatchFacts:
    if m.home_score > m.away_score:
        f = MatchFacts(winner=m.home, loser=m.away)
    elif m.away_score > m.home_score:
        f = MatchFacts(winner=m.away, loser=m.home)
    else:
        f = MatchFacts(winner=None, loser=None)

    # Replay the goals in order, tracking the score.
    score = {m.home: 0, m.away: 0}
    for g in m.goals:  # parse_goals returns goals sorted by minute
        score[g.team] += 1
        if f.winner:
            loser_final = m.away_score if f.winner == m.home else m.home_score
            if g.team == f.winner and score[f.winner] == loser_final + 1:
                # Winner's goal count passes the loser's FINAL tally: decisive goal.
                f.winning_goal = g
            f.comeback_from = max(f.comeback_from, score[f.loser] - score[f.winner])
    if f.winning_goal is not None:
        f.late_winner = f.winning_goal.minute.base >= LATE_MINUTE
    if f.winner is None and m.goals and m.goals[-1].minute.base >= LATE_MINUTE:
        # In a draw the last goal always levels the score.
        f.late_equaliser = m.goals[-1]

    ht_leader = (m.home if m.ht_home > m.ht_away else m.away if m.ht_away > m.ht_home
                 else None)
    if ht_leader and ht_leader == f.loser:
        f.led_at_half_time_and_lost = ht_leader

    if m.away_score == 0:
        f.clean_sheets.append(m.home)
    if m.home_score == 0:
        f.clean_sheets.append(m.away)

    # Own goals don't count towards a player's brace or hat-trick.
    scored = Counter((g.scorer, g.team) for g in m.goals if not g.own_goal)
    for (player, team), n in sorted(scored.items()):
        if n >= 3:
            f.hat_tricks.append((player, team, n))
        elif n == 2:
            f.braces.append((player, team))

    f.stoppage_time_goals = [g for g in m.goals if g.minute.extra > 0]
    f.penalties = [g for g in m.goals if g.penalty]
    f.own_goals = [g for g in m.goals if g.own_goal]
    f.sent_off = [c for c in m.cards if c.kind in ("red", "second_yellow")]
    f.derby = derby_name(m.home, m.away)
    f.tags = _tags(m, f)
    return f


def _tags(m: Match, f: MatchFacts) -> list[str]:
    total = m.home_score + m.away_score
    margin = abs(m.home_score - m.away_score)
    tags: list[str] = []
    if f.winner is None:
        tags.append("goalless draw" if total == 0 else "score draw")
    else:
        tags.append("home win" if f.winner == m.home else "away win")
    if f.late_winner:
        tags.append("late winner")
    if f.late_equaliser:
        tags.append("late equaliser")
    if f.comeback_from:
        tags.append("comeback win")
    if f.led_at_half_time_and_lost:
        tags.append("half-time lead lost")
    if f.winner and margin >= THRASHING_MARGIN:
        tags.append("thrashing")
    if total >= HIGH_SCORING_TOTAL:
        tags.append("high-scoring")
    if f.clean_sheets:
        tags.append("clean sheet")
    if f.hat_tricks:
        tags.append("hat-trick")
    if f.braces:
        tags.append("brace")
    if f.stoppage_time_goals:
        tags.append("stoppage-time goal")
    if f.penalties:
        tags.append("penalty")
    if f.own_goals:
        tags.append("own goal")
    if f.sent_off:
        tags.append("red card")
    if f.derby:
        tags.append("derby")
    return tags
