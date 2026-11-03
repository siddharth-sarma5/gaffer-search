import datetime as dt
import unittest

from corpusgen.derive import derive
from corpusgen.parse import Card, Goal, Lineup, Match, Minute


def make_match(home_score, away_score, goals, ht=(0, 0), cards=(), home="Arsenal",
               away="Chelsea FC"):
    gs = [Goal(scorer, Minute(base, extra), home if side == "h" else away,
               own_goal=og)
          for side, scorer, base, extra, og in goals]
    return Match(matchday=1, date=dt.date(2025, 8, 16), kickoff="15:00", venue="V", city="C",
                 attendance=None, home=home, away=away, home_score=home_score,
                 away_score=away_score, ht_home=ht[0], ht_away=ht[1],
                 goals=sorted(gs, key=lambda g: g.minute), cards=list(cards),
                 lineups={home: Lineup(home), away: Lineup(away)})


def g(side, scorer, base, extra=0, og=False):
    return (side, scorer, base, extra, og)


class DecisiveGoalTest(unittest.TestCase):
    def test_late_winner_is_the_goal_that_passes_the_losers_final_tally(self):
        # 1-0, 1-1, 2-1 (85'), 3-1 (90+3'): the 85' goal is the winner, not the last goal.
        m = make_match(3, 1, [g("h", "A", 10), g("a", "B", 50), g("h", "C", 85),
                              g("h", "D", 90, 3)])
        f = derive(m)
        self.assertEqual(f.winning_goal.scorer, "C")
        self.assertTrue(f.late_winner)
        self.assertIn("late winner", f.tags)

    def test_early_decisive_goal_is_not_a_late_winner(self):
        f = derive(make_match(2, 0, [g("h", "A", 10), g("h", "B", 89)]))
        self.assertEqual(f.winning_goal.scorer, "A")
        self.assertFalse(f.late_winner)

    def test_late_equaliser_in_a_draw(self):
        f = derive(make_match(1, 1, [g("h", "A", 10), g("a", "B", 88)]))
        self.assertEqual(f.late_equaliser.scorer, "B")
        self.assertEqual(f.tags[:2], ["score draw", "late equaliser"])


class ComebackTest(unittest.TestCase):
    def test_comeback_from_two_down(self):
        f = derive(make_match(2, 3, [g("h", "A", 5), g("h", "A", 20), g("a", "B", 50),
                                     g("a", "B", 60), g("a", "C", 70)], ht=(2, 0)))
        self.assertEqual(f.winner, "Chelsea FC")
        self.assertEqual(f.comeback_from, 2)
        self.assertEqual(f.led_at_half_time_and_lost, "Arsenal")
        self.assertIn("comeback win", f.tags)

    def test_never_behind_is_not_a_comeback(self):
        f = derive(make_match(2, 1, [g("h", "A", 5), g("a", "B", 50), g("h", "A", 60)]))
        self.assertEqual(f.comeback_from, 0)


class ScorerTagsTest(unittest.TestCase):
    def test_hat_trick_brace_and_own_goals_do_not_count(self):
        f = derive(make_match(5, 0, [g("h", "A", 1), g("h", "A", 2), g("h", "A", 3),
                                     g("h", "B", 4), g("h", "B", 5, og=False)]))
        self.assertEqual(f.hat_tricks, [("A", "Arsenal", 3)])
        self.assertEqual(f.braces, [("B", "Arsenal")])
        f2 = derive(make_match(2, 0, [g("h", "X", 1, og=True), g("h", "X", 2, og=True)]))
        self.assertEqual(f2.braces, [])
        self.assertEqual(len(f2.own_goals), 2)

    def test_thrashing_high_scoring_and_clean_sheet(self):
        f = derive(make_match(5, 0, [g("h", "A", i) for i in range(1, 6)]))
        for tag in ("home win", "thrashing", "high-scoring", "clean sheet"):
            self.assertIn(tag, f.tags)
        self.assertEqual(f.clean_sheets, ["Arsenal"])

    def test_goalless_draw_gives_both_clean_sheets(self):
        f = derive(make_match(0, 0, []))
        self.assertEqual(f.tags[0], "goalless draw")
        self.assertEqual(f.clean_sheets, ["Arsenal", "Chelsea FC"])


class OtherTagsTest(unittest.TestCase):
    def test_red_cards_stoppage_time_and_derby(self):
        m = make_match(1, 0, [g("h", "A", 90, 2)], home="Arsenal", away="Tottenham Hotspur",
                       cards=[Card("P", Minute(30), "Arsenal", "yellow"),
                              Card("Q", Minute(60), "Tottenham Hotspur", "second_yellow")])
        f = derive(m)
        self.assertEqual([c.player for c in f.sent_off], ["Q"])
        self.assertEqual(f.derby, "North London derby")
        for tag in ("red card", "stoppage-time goal", "derby", "late winner"):
            self.assertIn(tag, f.tags)

    def test_london_derby_fallback(self):
        f = derive(make_match(0, 0, [], home="Fulham", away="Brentford"))
        self.assertEqual(f.derby, "London derby")


if __name__ == "__main__":
    unittest.main()
