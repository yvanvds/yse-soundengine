# YSE content pack

Optional asset pack for the YSE instruments (issue #179, epic #149):
wavetables for the VA/wavetable voice (#175), SFZ instruments for the sampler
voice (#174, opcode subset #172/#173), and FM banks for the DX7-class FM voice
(#176) / SysEx importer (#177).

**Nothing here is baked into the library binary** — `libyse` links no asset.
The pack is data, loaded at runtime by the normal file loaders.

## Layout

```
content/
  wavetables/          single-cycle tables (mono WAV) — VA/wavetable voice
  sfz/                 SFZ v1-subset instruments + samples/
  fm/original/         CC0 FM bank authored by us (yse_originals.syx)
  fm/dx7-factory/      tolerated-not-cleared banks — populated by fetch only
  tools/               generate_content.py — deterministic CC0 seed generator
  pack-manifest.cmake  third-party fetch sources (opt-in)
```

## What ships in the repo vs. what is fetched

- **Committed:** a small **CC0 seed** authored by this project and produced by
  `tools/generate_content.py` (re-run to regenerate byte-for-byte). This is
  enough to exercise every loader and keeps the smoke test meaningful without a
  network fetch.
- **Fetched on demand:** the large third-party CC0/CC-BY collections and the
  tolerated DX7 factory banks. These are **not** committed; they are pulled by
  the opt-in CMake path.

## Enabling the full pack

```
cmake -S . -B build -DYSE_BUILD_TESTS=ON -DYSE_FETCH_CONTENT_PACK=ON
```

The fetch verifies every source against a pinned SHA-256 in
`pack-manifest.cmake` (an unpinned source is a hard error). To install the pack
alongside a build, add `-DYSE_INSTALL_CONTENT_PACK=ON` (installs to
`<prefix>/share/yse/content`).

## Provenance & licenses

Every asset — committed or fetched — is recorded with its source, license, and
required attribution in the repo-root
[`CONTENT-LICENSES.md`](../CONTENT-LICENSES.md). The Salamander piano ships with
[`ATTRIBUTION-Salamander.txt`](ATTRIBUTION-Salamander.txt) per its CC-BY terms;
the DX7 factory-bank risk decision is documented in
[`fm/dx7-factory/README.md`](fm/dx7-factory/README.md).
