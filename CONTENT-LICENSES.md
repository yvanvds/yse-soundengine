# Content pack — provenance & licenses

This file is the audit trail for the **optional** YSE content pack (issue #179,
part of epic #149). The pack is never baked into the library binary: `libyse`
links no asset. It lives under [`content/`](content/) and is assembled by the
opt-in CMake path (`-DYSE_FETCH_CONTENT_PACK=ON`, see
[`cmake/YseContentPack.cmake`](cmake/YseContentPack.cmake) and
[`content/pack-manifest.cmake`](content/pack-manifest.cmake)). Default builds
fetch and install nothing.

Every asset the pack can contain is listed below with its source, license, and
required attribution. Two categories:

- **Committed (CC0 seed)** — small, authored by this project, generated
  deterministically by [`content/tools/generate_content.py`](content/tools/generate_content.py)
  and committed to the repo. Unambiguously CC0.
- **Fetched (third-party)** — pulled on demand by the fetch path; **not**
  committed. Each has a pinned SHA-256 in `pack-manifest.cmake` before it can be
  fetched (an empty pin is a hard error — hashes are verified, never invented).

## Committed CC0 seed (authored by YSE)

| Asset | Path | License | Source | Attribution |
|-------|------|---------|--------|-------------|
| Single-cycle wavetables (sine/saw/square/triangle) | `content/wavetables/ysewt_*.wav` | CC0-1.0 | This repo (`content/tools/generate_content.py`) | None |
| FM bank — 32 original voices | `content/fm/original/yse_originals.syx` | CC0-1.0 | This repo (`content/tools/generate_content.py`) | None |
| SFZ instrument — pulse | `content/sfz/yse_pulse.sfz` | CC0-1.0 | This repo | None |
| SFZ one-shot sample | `content/sfz/samples/yse_pulse_c4.wav` | CC0-1.0 | This repo (`content/tools/generate_content.py`) | None |

## Fetched third-party sources (not committed; opt-in)

| Asset | Dest under pack | License | Source URL | Attribution |
|-------|-----------------|---------|------------|-------------|
| AKWF single-cycle wavetables (curated subset) | `wavetables/akwf/` | CC0-1.0 | https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE | None (CC0) |
| VSCO 2 Community Edition (SFZ subset) | `sfz/vsco2/` | CC0-1.0 | https://github.com/sgossner/VSCO-2-CE | None (CC0) |
| VCSL — Versilian Community Sample Library | `sfz/vcsl/` | CC0-1.0 | https://github.com/sgossner/VCSL | None (CC0) |
| Salamander Grand Piano | `sfz/salamander/` | **CC-BY-3.0** | https://archive.org/details/SalamanderGrandPianoV3 | Required — see [`content/ATTRIBUTION-Salamander.txt`](content/ATTRIBUTION-Salamander.txt) |
| FreePats selection (CC0 picks only) | `sfz/freepats/` | CC0-1.0 | https://freepats.zenvoid.org/ | None (CC0) |
| DX7 factory-style `.syx` banks | `fm/dx7-factory/` | **tolerated-unclear** | https://github.com/asb2m10/dexed (Resources) | See risk note below |

## The DX7 factory-bank decision (tolerated, not cleared)

The DX7 factory-style ROM voice banks are **tolerated, not legally cleared** —
a deliberate maintainer decision taken while scoping this epic. Yamaha never
released this voice data under an open license; it is bundled because it is the
de-facto reference material for the FM importer (#177) and is distributed
widely.

The risk is **contained and reversible**:

- The banks are isolated in `content/fm/dx7-factory/`
  ([README](content/fm/dx7-factory/README.md)) — deleting that one folder
  removes the risk entirely.
- Nothing in the engine, the build, or the smoke test depends on them; all
  treat the directory as optional.
- They are never committed to the repo — only fetched on explicit opt-in.

For an unambiguously CC0 FM bank, use `content/fm/original/yse_originals.syx`.
