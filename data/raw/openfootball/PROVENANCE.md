# Provenance: openfootball

| Field | Value |
|-------|-------|
| Upstream | https://github.com/openfootball/england |
| Upstream commit | `9ee7a0c351805c99cc334478a9bf04b7d0432fec` (2026-10-15) |
| File | `2025-26/1-premierleague-full.txt` → `2025-26/1-premierleague-full.txt` here, unmodified |
| SHA-256 | `14c29435ee90da7ec6f6abbde183e514262304216d4dac80208bb50882b0ba93` |
| Licence | CC0 1.0 Universal (public domain dedication); copy in `LICENSE.md` |

## Historical seasons (benchmark corpus, D-016)

`history/<season>/1-premierleague.txt` for 1992-93 to 2024-25 (33 seasons), copied unmodified
from the same upstream commit: `archive/1990s/<season>/` for 1992-93 to 1999-00, and
`<season>/` for 2000-01 onwards. SHA-256 checksums are in `history/SHA256SUMS`, and CI checks
them. Same CC0 licence.

## Notes

The files are vendored, not downloaded at build time, so corpus generation is reproducible and works
offline. To update it, copy the newer upstream file, update the commit and hash above, regenerate
the corpus, and log the change in `docs/DECISIONS.md`.

Thanks to the openfootball contributors. CC0 doesn't require credit, but they deserve it.
