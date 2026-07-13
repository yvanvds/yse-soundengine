# pack-manifest.cmake — third-party sources for the optional content pack (#179)
#
# This file is read ONLY when a build opts in with -DYSE_FETCH_CONTENT_PACK=ON.
# It never affects a default build. Each source is declared with yse_content_source()
# (defined by cmake/YseContentPack.cmake before this file is included); the module
# downloads the archive, verifies its SHA-256, and extracts it under
# ${YSE_CONTENT_PACK_DIR}/<DEST>.
#
# HASHES ARE DELIBERATELY LEFT EMPTY. Fetching a source with an empty SHA256 is a
# hard error — the maintainer must download the pinned archive, verify it against
# upstream, and paste the sha256 here (never invent a hash). This keeps the fetch
# path auditable and reproducible while making the "opt in" a conscious act.
#
# Every row below is mirrored, with its license, in the repo-root CONTENT-LICENSES.md.

# ── Wavetables ────────────────────────────────────────────────────────────────
yse_content_source(
  NAME    akwf
  URL     "https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE/archive/refs/heads/master.zip"
  SHA256  ""                       # TODO: pin before enabling
  DEST    "wavetables/akwf"
  LICENSE "CC0-1.0"
)

# ── Sampled instruments ───────────────────────────────────────────────────────
yse_content_source(
  NAME    vsco2-ce
  URL     "https://github.com/sgossner/VSCO-2-CE/archive/refs/heads/master.zip"
  SHA256  ""                       # TODO: pin before enabling
  DEST    "sfz/vsco2"
  LICENSE "CC0-1.0"
)
yse_content_source(
  NAME    vcsl
  URL     "https://github.com/sgossner/VCSL/archive/refs/heads/master.zip"
  SHA256  ""                       # TODO: pin before enabling
  DEST    "sfz/vcsl"
  LICENSE "CC0-1.0"
)
yse_content_source(
  NAME    salamander
  URL     "https://archive.org/download/SalamanderGrandPianoV3/SalamanderGrandPianoV3.tar.bz2"
  SHA256  ""                       # TODO: pin before enabling
  DEST    "sfz/salamander"
  LICENSE "CC-BY-3.0"              # ATTRIBUTION-Salamander.txt ships with the pack
)
yse_content_source(
  NAME    freepats-selection
  URL     "https://freepats.zenvoid.org/sf2/freepats-general-midi.sf2"  # placeholder pick; CC0 subset only
  SHA256  ""                       # TODO: pin before enabling
  DEST    "sfz/freepats"
  LICENSE "CC0-1.0"
)

# ── FM banks (TOLERATED, NOT LEGALLY CLEARED) ─────────────────────────────────
# Isolated in fm/dx7-factory/ so the risk is reversible by deleting one folder.
# See content/fm/dx7-factory/README.md.
yse_content_source(
  NAME    dx7-factory
  URL     "https://github.com/asb2m10/dexed/raw/master/Resources/DX7_0628.SYX"
  SHA256  ""                       # TODO: pin before enabling
  DEST    "fm/dx7-factory"
  LICENSE "tolerated-unclear"
)
