"""Builds the relevance judgements (qrels) for the evaluation in eval/.

Relevance is defined by an explicit criterion per query, evaluated on the
*structured* match data (parsed goals, venues, cards, derived facts). It is
never decided by looking at what the engine returns, so the evaluation isn't
circular, and anyone can read exactly what "relevant" means for each query.

Run from the repo root:
    PYTHONPATH=tools/corpusgen python3 -I eval/make_qrels.py
Writes eval/queries.tsv (qid, query, criterion) and eval/qrels.tsv (qid, doc key).
"""

from __future__ import annotations

import sys
from collections import Counter
from pathlib import Path

from corpusgen.aggregate import league_table, slugify
from corpusgen.clubs import club
from corpusgen.derive import derive
from corpusgen.parse import parse_file
from corpusgen.render import match_id

REPO = Path(__file__).resolve().parents[1]
SOURCE = REPO / "data/raw/openfootball/2025-26/1-premierleague-full.txt"


def main() -> int:
    matches = parse_file(SOURCE.read_text(encoding="utf-8"), 2025)
    facts = {match_id(m): derive(m) for m in matches}
    table = league_table(matches)

    def f(m):
        return facts[match_id(m)]

    def goals_by(m, player, *, penalty=None):
        return [g for g in m.goals if g.scorer == player and not g.own_goal
                and (penalty is None or g.penalty == penalty)]

    def involves(m, source_name):
        return source_name in (m.home, m.away)

    def player(name):
        return f"player-{slugify(name)}"

    def matches_where(pred):
        return [match_id(m) for m in matches if pred(m)]

    # (query, criterion in words, relevant doc keys)
    spec = [
        ("late winner at anfield", "match at Anfield with a late winner (decisive goal 80'+)",
         matches_where(lambda m: m.venue == "Anfield" and f(m).late_winner)),
        ("palmer hat-trick", "match where Cole Palmer scored 3+; his player page",
         matches_where(lambda m: len(goals_by(m, "Cole Palmer")) >= 3) + [player("Cole Palmer")]),
        ("haaland brace", "match where Erling Haaland scored exactly 2; his player page",
         matches_where(lambda m: len(goals_by(m, "Erling Haaland")) == 2)
         + [player("Erling Haaland")]),
        ("north london derby", "Arsenal v Tottenham match",
         matches_where(lambda m: f(m).derby == "North London derby")),
        ("gunners comeback", "match Arsenal won after trailing",
         matches_where(lambda m: f(m).winner == "Arsenal" and f(m).comeback_from > 0)),
        ("goalless draw at stamford bridge", "0-0 at Stamford Bridge",
         matches_where(lambda m: m.venue == "Stamford Bridge" and m.home_score + m.away_score == 0)),
        ("merseyside derby", "Liverpool v Everton match",
         matches_where(lambda m: f(m).derby == "Merseyside derby")),
        ("relegated", "club pages of the three relegated clubs; the season page",
         [f"club-{club(r.team).slug}" for r in table[-3:]] + ["season-2025-26"]),
        ("champions", "the champions' club page; the season page",
         [f"club-{club(table[0].team).slug}", "season-2025-26"]),
        # (No Tottenham match in 2025-26 was won by 4+, so "thrashing" has no relevant docs.)
        ("spurs high-scoring", "Tottenham match with 5+ goals",
         matches_where(lambda m: involves(m, "Tottenham Hotspur") and "high-scoring" in f(m).tags)),
        ("toffees home win", "Everton home win",
         matches_where(lambda m: m.home == "Everton" and m.home_score > m.away_score)),
        ("man utd late equaliser", "Manchester United match with a late equaliser",
         matches_where(lambda m: involves(m, "Manchester United") and f(m).late_equaliser)),
        ("salah penalty", "match where Mohamed Salah scored a penalty",
         matches_where(lambda m: goals_by(m, "Mohamed Salah", penalty=True))),
        ("comeback from two goals down", "match won after trailing by 2+",
         matches_where(lambda m: f(m).comeback_from >= 2)),
        ("hat-trick", "match with a hat-trick",
         matches_where(lambda m: bool(f(m).hat_tricks))),
        ("clean sheet at the stadium of light", "match at the Stadium of Light with a clean sheet",
         matches_where(lambda m: m.venue == "Stadium of Light" and f(m).clean_sheets)),
        ("watkins brace", "match where Ollie Watkins scored exactly 2",
         matches_where(lambda m: len(goals_by(m, "Ollie Watkins")) == 2)),
        ("wolves red card", "Wolves match where a Wolves player was sent off",
         matches_where(lambda m: any(c.team == "Wolverhampton Wanderers" for c in f(m).sent_off))),
        ("gyokeres goal", "match where Viktor Gyokeres scored; his player page",
         matches_where(lambda m: goals_by(m, "Viktor Gyokeres")) + [player("Viktor Gyokeres")]),
        ("own goal late winner", "match whose late winner was an own goal",
         matches_where(lambda m: f(m).late_winner and f(m).winning_goal.own_goal)),
    ]

    out_q = REPO / "eval/queries.tsv"
    out_r = REPO / "eval/qrels.tsv"
    with out_q.open("w", encoding="utf-8", newline="\n") as q, \
            out_r.open("w", encoding="utf-8", newline="\n") as r:
        q.write("# qid\tquery\trelevance criterion (generated by eval/make_qrels.py)\n")
        r.write("# qid\tdoc key (every listed doc is relevant; unlisted = not relevant)\n")
        for i, (query, criterion, rel) in enumerate(spec, 1):
            qid = f"q{i:02d}"
            if not rel:
                print(f"{qid} '{query}' has no relevant documents", file=sys.stderr)
                return 1
            q.write(f"{qid}\t{query}\t{criterion}\n")
            for key in rel:
                r.write(f"{qid}\t{key}\n")
            print(f"{qid}  {len(rel):3d} relevant  {query}", file=sys.stderr)
    counts = Counter(len(rel) for _, _, rel in spec)
    print(f"{len(spec)} queries, {sum(k * v for k, v in counts.items())} judgements", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
