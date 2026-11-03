"""Canonical club table: the source's names mapped to display names and facts.

Nicknames are hand-curated common knowledge. They appear in club documents
only. Match documents use full names, so a query for "Gunners" needs the
query-side nickname expansion added in Milestone 2.
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Club:
    slug: str
    name: str  # display name
    short: str  # what fans type: "Man City", "Spurs"
    nicknames: tuple[str, ...]
    london: bool = False


_CLUBS = [
    # source name,               slug,                display,                    short,          nicknames,                            london
    ("Arsenal",                  "arsenal",           "Arsenal",                  "Arsenal",      ("Gunners",),                          True),
    ("Aston Villa",              "aston-villa",       "Aston Villa",              "Villa",        ("Villans",),                          False),
    ("Bournemouth",              "bournemouth",       "AFC Bournemouth",          "Bournemouth",  ("Cherries",),                         False),
    ("Brentford",                "brentford",         "Brentford",                "Brentford",    ("Bees",),                             True),
    ("Brighton & Hove Albion",   "brighton",          "Brighton & Hove Albion",   "Brighton",     ("Seagulls",),                         False),
    ("Burnley",                  "burnley",           "Burnley",                  "Burnley",      ("Clarets",),                          False),
    ("Chelsea FC",               "chelsea",           "Chelsea",                  "Chelsea",      ("Blues",),                            True),
    ("Crystal Palace",           "crystal-palace",    "Crystal Palace",           "Palace",       ("Eagles",),                           True),
    ("Everton",                  "everton",           "Everton",                  "Everton",      ("Toffees",),                          False),
    ("Fulham",                   "fulham",            "Fulham",                   "Fulham",       ("Cottagers",),                        True),
    ("Leeds United",             "leeds",             "Leeds United",             "Leeds",        ("Whites", "Peacocks"),                False),
    ("Liverpool",                "liverpool",         "Liverpool",                "Liverpool",    ("Reds",),                             False),
    ("Manchester City",          "man-city",          "Manchester City",          "Man City",     ("Citizens", "Cityzens", "Sky Blues"), False),
    ("Manchester United",        "man-united",        "Manchester United",        "Man Utd",      ("Red Devils",),                       False),
    ("Newcastle United",         "newcastle",         "Newcastle United",         "Newcastle",    ("Magpies", "Toon"),                   False),
    ("Nottingham Forest",        "nottingham-forest", "Nottingham Forest",        "Forest",       ("Tricky Trees",),                     False),
    ("Sunderland",               "sunderland",        "Sunderland",               "Sunderland",   ("Black Cats",),                       False),
    ("Tottenham Hotspur",        "tottenham",         "Tottenham Hotspur",        "Spurs",        ("Lilywhites",),                       True),
    ("West Ham United",          "west-ham",          "West Ham United",          "West Ham",     ("Hammers", "Irons"),                  True),
    ("Wolverhampton Wanderers",  "wolves",            "Wolverhampton Wanderers",  "Wolves",       ("Wanderers",),                        False),
]  # fmt: skip

CLUBS: dict[str, Club] = {src: Club(slug, name, short, nicks, london)
                          for src, slug, name, short, nicks, london in _CLUBS}

# Named derbies, keyed by the unordered pair of source names.
_DERBIES = {
    frozenset({"Arsenal", "Tottenham Hotspur"}): "North London derby",
    frozenset({"Liverpool", "Everton"}): "Merseyside derby",
    frozenset({"Manchester City", "Manchester United"}): "Manchester derby",
    frozenset({"Newcastle United", "Sunderland"}): "Tyne-Wear derby",
    frozenset({"Aston Villa", "Wolverhampton Wanderers"}): "West Midlands derby",
    frozenset({"Liverpool", "Manchester United"}): "North West derby",
}


def club(source_name: str) -> Club:
    try:
        return CLUBS[source_name]
    except KeyError:
        raise KeyError(f"unknown club {source_name!r}: add it to corpusgen/clubs.py") from None


def derby_name(home: str, away: str) -> str | None:
    """Named derby if there is one, else 'London derby' for two London clubs."""
    if name := _DERBIES.get(frozenset({home, away})):
        return name
    if club(home).london and club(away).london:
        return "London derby"
    return None
