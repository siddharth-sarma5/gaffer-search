import datetime as dt
import unittest

from corpusgen.parse import (
    Minute, ParseError, normalise_name, parse_file, parse_goals, parse_lineup, season_date)

# Two matches in the real format, trimmed to the parts the parser cares about.
SAMPLE = """\
= England | Premier League 2025/26

▪ Regular Season - 1
Fri Aug 15 19:00 UTC+0 @ Anfield, Liverpool, Att: 60315
  Liverpool v Bournemouth  2-1 (1-0)
     (MOHAMED SALAH 10', 90+4'(p);
      Antoine SEMENYO 64')

Liverpool: ALISSON - Virgil VAN DIJK [c], Milos KERKEZ (60' Andy ROBERTSON (80' Joe Gomez))
    Yellow: Milos KERKEZ 43'

Bournemouth: Djordje PETROVIC - Adam SMITH [c]
    Yellow-Red: Adam SMITH 88'

Refs: Anthony Taylor, Gary Beswick, Craig Taylor

▪ Regular Season - 22
Sat Jan 10 15:00 UTC+0 @ Elland Road, Leeds, West Yorkshire
  Manchester United v Arsenal  0-1 (0-1)
     (Riccardo Calafiori 13'(og))

Manchester United: Andre ONANA - Jean-Philippe MATETA
Arsenal: David RAYA - Riccardo Calafiori
    Red:    David RAYA 70'
Refs: Michael Oliver
"""


class MinuteTest(unittest.TestCase):
    def test_parse_and_str(self):
        self.assertEqual(Minute.parse("90+4'"), Minute(90, 4))
        self.assertEqual(str(Minute(90, 4)), "90+4'")
        self.assertEqual(str(Minute(37)), "37'")

    def test_stoppage_time_orders_between_regular_minutes(self):
        self.assertLess(Minute(45, 2), Minute(46))
        self.assertLess(Minute(90), Minute(90, 1))


class NameTest(unittest.TestCase):
    def test_upper_case_words_are_title_cased(self):
        self.assertEqual(normalise_name("Erling HAALAND"), "Erling Haaland")
        self.assertEqual(normalise_name("MOHAMED SALAH"), "Mohamed Salah")

    def test_hyphens_apostrophes_and_mixed_case_survive(self):
        self.assertEqual(normalise_name("Jean-Philippe MATETA"), "Jean-Philippe Mateta")
        self.assertEqual(normalise_name("JEAN-PHILIPPE"), "Jean-Philippe")
        self.assertEqual(normalise_name("O'BRIEN"), "O'Brien")
        self.assertEqual(normalise_name("John McGinn"), "John McGinn")


class SeasonDateTest(unittest.TestCase):
    def test_autumn_and_spring_years(self):
        self.assertEqual(season_date("Aug", 15, 2025), dt.date(2025, 8, 15))
        self.assertEqual(season_date("May", 24, 2025), dt.date(2026, 5, 24))


class GoalsTest(unittest.TestCase):
    def test_semicolon_splits_home_and_away(self):
        goals = parse_goals("A 1', 2'; B 3'", "H", "W", 2, 1, 1)
        self.assertEqual([(g.scorer, g.team) for g in goals], [("A", "H"), ("A", "H"), ("B", "W")])

    def test_missing_semicolon_uses_the_scoreline(self):
        away_only = parse_goals("B 3'", "H", "W", 0, 1, 1)
        self.assertEqual(away_only[0].team, "W")
        home_only = parse_goals("A 3'", "H", "W", 1, 0, 1)
        self.assertEqual(home_only[0].team, "H")

    def test_own_goal_is_credited_to_the_listed_side(self):
        (g,) = parse_goals("Riccardo Calafiori 13'(og)", "H", "W", 1, 0, 1)
        self.assertTrue(g.own_goal)
        self.assertEqual(g.team, "H")

    def test_goals_are_returned_in_chronological_order(self):
        goals = parse_goals("A 80'; B 10'", "H", "W", 1, 1, 1)
        self.assertEqual([str(g.minute) for g in goals], ["10'", "80'"])

    def test_count_mismatch_is_an_error(self):
        with self.assertRaisesRegex(ParseError, "line 7: goals list gives 1-0 but score is 2-0"):
            parse_goals("A 1'", "H", "W", 2, 0, 7)

    def test_ambiguous_attribution_is_an_error(self):
        with self.assertRaisesRegex(ParseError, "no ';' separator"):
            parse_goals("A 1', B 2'", "H", "W", 1, 1, 1)

    def test_minute_without_a_scorer_is_an_error(self):
        with self.assertRaisesRegex(ParseError, "without a scorer"):
            parse_goals("5'", "H", "W", 1, 0, 1)


class LineupTest(unittest.TestCase):
    def test_nested_substitution_chain(self):
        lu = parse_lineup("H", "A [c] (16' B (75' C)), D - E-F", 1)
        self.assertEqual(lu.starters, ["A", "D", "E-F"])
        self.assertEqual([(s.player_off, s.player_on, str(s.minute)) for s in lu.substitutions],
                         [("A", "B", "16'"), ("B", "C", "75'")])
        self.assertEqual(lu.captain, "A")
        self.assertEqual(lu.players, ["A", "D", "E-F", "B", "C"])

    def test_unbalanced_parentheses_are_an_error(self):
        with self.assertRaises(ParseError):
            parse_lineup("H", "A (16' B", 1)


class ParseFileTest(unittest.TestCase):
    def setUp(self):
        self.matches = parse_file(SAMPLE, 2025)

    def test_header_fields(self):
        m = self.matches[0]
        self.assertEqual((m.matchday, m.date, m.kickoff), (1, dt.date(2025, 8, 15), "19:00"))
        self.assertEqual((m.venue, m.city, m.attendance), ("Anfield", "Liverpool", 60315))
        self.assertEqual((m.home, m.away, m.home_score, m.away_score), (
            "Liverpool", "Bournemouth", 2, 1))
        self.assertEqual((m.ht_home, m.ht_away), (1, 0))
        self.assertEqual(m.referee, "Anthony Taylor")

    def test_multi_line_goal_block_with_penalty(self):
        g = self.matches[0].goals
        self.assertEqual([(x.scorer, str(x.minute), x.penalty) for x in g], [
            ("Mohamed Salah", "10'", False), ("Antoine Semenyo", "64'", False),
            ("Mohamed Salah", "90+4'", True)])

    def test_cards_belong_to_the_preceding_lineup(self):
        cards = [(c.player, c.team, c.kind) for c in self.matches[0].cards]
        self.assertEqual(cards, [("Milos Kerkez", "Liverpool", "yellow"),
                                 ("Adam Smith", "Bournemouth", "second_yellow")])
        self.assertEqual(self.matches[1].cards[0].kind, "red")

    def test_missing_attendance_and_comma_in_city(self):
        m = self.matches[1]
        self.assertIsNone(m.attendance)
        self.assertEqual((m.venue, m.city), ("Elland Road", "Leeds, West Yorkshire"))
        self.assertEqual((m.matchday, m.date), (22, dt.date(2026, 1, 10)))

    def test_lineup_for_a_team_not_playing_is_an_error(self):
        bad = SAMPLE.replace("Arsenal: David", "Chelsea FC: David")
        with self.assertRaisesRegex(ParseError, "isn't playing"):
            parse_file(bad, 2025)


if __name__ == "__main__":
    unittest.main()
