import datetime as dt
import unittest
from pathlib import Path

from corpusgen.history import build_documents, normalise_team, parse_season, render_result
from corpusgen.parse import ParseError

REPO = Path(__file__).resolve().parents[3]

# A 3-team double round robin (6 games) using all three file layouts.
NINETIES = """\
= English Premier League 1992/93

▪ Matchday 1
Sat Aug 15
  Arsenal FC               2-4  Norwich City FC
  Sheffield Wed            1-0  Arsenal FC
▪ Matchday 2
Sat Aug 22
  Norwich City FC          0-0  Sheffield Wed
  Arsenal FC               1-1  Sheffield Wednesday FC
▪ Matchday 3
Sat Jan 9
  Norwich City FC          3-1  Arsenal FC
  Sheffield Wed            2-0  Norwich City
"""

MODERN = """\
▪ Matchday 1
Sat Aug 13
  15:00  Aston Villa              2-2 (2-2)  Bolton Wanderers
         Fulham FC                1-0  Aston Villa
▪ Matchday 2
Sun Aug 21
  16:00  Bolton Wanderers         0-3 (0-1)  Fulham FC
         Bolton Wanderers         1-2 (1-0)  Aston Villa
         Fulham FC                0-0  Bolton Wanderers
         Aston Villa              4-0  Fulham FC
"""

V_LAYOUT = """\
▪ Matchday 1
  Fri Aug 16 2024
    20:00  Manchester United FC    v Fulham FC                1-0 (0-0)
           Fulham FC               v Brentford FC             2-1 (0-1)
  Sat Aug 17 2024
    12:30  Brentford FC            v Manchester United FC     0-0 (0-0)
           Fulham FC               v Manchester United FC     3-3 (1-2)
           Manchester United FC    v Brentford FC             5-1 (2-0)
           Brentford FC            v Fulham FC                1-1 (1-1)
"""


class NormaliseTest(unittest.TestCase):
    def test_suffixes_and_aliases(self):
        self.assertEqual(normalise_team("Arsenal FC"), "Arsenal")
        self.assertEqual(normalise_team("Sunderland AFC"), "Sunderland")
        self.assertEqual(normalise_team("AFC Bournemouth"), "AFC Bournemouth")
        self.assertEqual(normalise_team("Sheffield Wed"), "Sheffield Wednesday")
        self.assertEqual(normalise_team("Newcastle Utd"), "Newcastle United")


class ParseSeasonTest(unittest.TestCase):
    def test_nineties_layout_and_name_normalisation(self):
        rs = parse_season(NINETIES, "1992-93")
        self.assertEqual(len(rs), 6)
        self.assertEqual((rs[0].home, rs[0].away, rs[0].home_score, rs[0].away_score),
                         ("Arsenal", "Norwich City", 2, 4))
        self.assertIsNone(rs[0].half_time)
        self.assertEqual(rs[1].home, "Sheffield Wednesday")  # "Sheffield Wed" alias
        self.assertEqual(rs[3].away, "Sheffield Wednesday")  # "... FC" suffix
        self.assertEqual(rs[4].date, dt.date(1993, 1, 9))  # January -> second calendar year

    def test_modern_layout_with_optional_time_and_half_time(self):
        rs = parse_season(MODERN, "2005-06")
        self.assertEqual(rs[0].half_time, (2, 2))
        self.assertIsNone(rs[1].half_time)
        self.assertEqual((rs[1].home, rs[1].away), ("Fulham", "Aston Villa"))

    def test_v_layout_with_year_in_date(self):
        rs = parse_season(V_LAYOUT, "2024-25")
        self.assertEqual((rs[0].home, rs[0].away, rs[0].half_time),
                         ("Manchester United", "Fulham", (0, 0)))
        self.assertEqual(rs[2].date, dt.date(2024, 8, 17))

    def test_incomplete_round_robin_is_rejected(self):
        with self.assertRaisesRegex(ParseError, "not a complete round robin"):
            parse_season("\n".join(MODERN.splitlines()[:-1]), "2005-06")

    def test_wrong_year_and_garbage_lines_are_rejected(self):
        with self.assertRaisesRegex(ParseError, "doesn't fit season"):
            parse_season(V_LAYOUT.replace("Aug 16 2024", "Aug 16 2023"), "2024-25")
        with self.assertRaisesRegex(ParseError, "unrecognised"):
            parse_season(MODERN.replace("2-2 (2-2)", "postponed"), "2005-06")


class RenderTest(unittest.TestCase):
    def test_document_text_and_tags(self):
        rs = parse_season(MODERN, "2005-06")
        # Bolton 1-2 Aston Villa, half-time 1-0: Villa came from behind.
        doc = render_result(rs[3])
        self.assertEqual(doc.id, "hist-2005-08-21-bolton-wanderers-aston-villa")
        self.assertEqual(doc.title, "Bolton Wanderers 1-2 Aston Villa (21 Aug 2005)")
        self.assertIn("Aston Villa beat Bolton Wanderers 2-1 away.", doc.body)
        self.assertIn("came from behind at half-time to win", doc.body)
        self.assertIn("Tags: away win, comeback win, half-time lead lost.", doc.body)
        # Aston Villa 4-0 Fulham: thrashing and clean sheet.
        self.assertIn("thrashing", render_result(rs[5]).body)
        self.assertIn("Aston Villa kept a clean sheet.", render_result(rs[5]).body)


class RealHistoryTest(unittest.TestCase):
    def test_all_seasons_parse(self):
        docs = build_documents(REPO / "data/raw/openfootball/history")
        self.assertEqual(len(docs), 3 * 462 + 30 * 380)  # 22-team seasons until 1994-95
        self.assertEqual(len({d.id for d in docs}), len(docs))


if __name__ == "__main__":
    unittest.main()
