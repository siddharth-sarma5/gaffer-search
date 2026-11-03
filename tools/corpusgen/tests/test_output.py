"""Output-level tests: TSV escaping, determinism, and invariants on the real data."""

import tempfile
import unittest
from pathlib import Path

from corpusgen.__main__ import build_documents
from corpusgen.aggregate import slugify
from corpusgen.render import ordinal
from corpusgen.tsv import HEADER, Document, escape, write_corpus

REPO = Path(__file__).resolve().parents[3]
SOURCE = REPO / "data/raw/openfootball/2025-26/1-premierleague-full.txt"


class TsvTest(unittest.TestCase):
    def test_escape(self):
        self.assertEqual(escape("a\tb\nc\\d"), "a\\tb\\nc\\\\d")

    def test_one_line_per_document_and_header(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "c.tsv"
            n = write_corpus([Document("x", "match", "T\tT", "line1\nline2")], path)
            lines = path.read_text(encoding="utf-8").split("\n")
        self.assertEqual(n, 1)
        self.assertEqual(lines[0], HEADER)
        self.assertEqual(lines[1].count("\t"), 3)
        self.assertEqual(lines[2], "")

    def test_duplicate_ids_rejected(self):
        with tempfile.TemporaryDirectory() as d, self.assertRaisesRegex(ValueError, "duplicate"):
            write_corpus([Document("x", "m", "", ""), Document("x", "m", "", "")],
                         Path(d) / "c.tsv")


class HelpersTest(unittest.TestCase):
    def test_ordinal(self):
        self.assertEqual([ordinal(n) for n in (1, 2, 3, 4, 11, 12, 13, 21, 88, 90)],
                         ["1st", "2nd", "3rd", "4th", "11th", "12th", "13th", "21st", "88th",
                          "90th"])

    def test_slugify_folds_accents(self):
        self.assertEqual(slugify("Martin Ødegaard"), "martin-odegaard")
        self.assertEqual(slugify("Łukasz Fabiański"), "lukasz-fabianski")
        self.assertEqual(slugify("Adama Traoré"), "adama-traore")


class RealDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = SOURCE.read_text(encoding="utf-8")
        cls.docs = build_documents(cls.text, 2025)

    def test_document_counts(self):
        types = [d.type for d in self.docs]
        self.assertEqual(types.count("season"), 1)
        self.assertEqual(types.count("club"), 20)
        self.assertEqual(types.count("match"), 380)
        self.assertGreater(types.count("player"), 400)

    def test_generation_is_deterministic(self):
        self.assertEqual(self.docs, build_documents(self.text, 2025))

    def test_ids_are_unique(self):
        ids = [d.id for d in self.docs]
        self.assertEqual(len(ids), len(set(ids)))

    def test_known_facts(self):
        by_id = {d.id: d for d in self.docs}
        opener = by_id["match-2025-08-15-liverpool-bournemouth"]
        self.assertIn("Federico Chiesa scored a late winner for Liverpool", opener.body)
        self.assertIn("at Anfield", opener.body)
        self.assertIn("Champions: Arsenal", by_id["season-2025-26"].body)


if __name__ == "__main__":
    unittest.main()
