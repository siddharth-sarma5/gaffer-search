"""corpusgen: turns openfootball Football.TXT match files into a search corpus.

Pipeline: parse (raw text -> Match records) -> derive (tags such as
"late winner") -> render (factual sentences) -> write (TSV corpus).
Standard library only (docs/DECISIONS.md D-008).
"""
