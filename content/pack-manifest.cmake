# pack-manifest.cmake — third-party sources for the optional content pack (#179)
#
# This file is read ONLY when a build opts in with -DYSE_FETCH_CONTENT_PACK=ON.
# It never affects a default build. Each source is declared with yse_content_source()
# (defined by cmake/YseContentPack.cmake before this file is included); the module
# downloads the archive, optionally verifies its SHA-256, and extracts it under
# ${YSE_CONTENT_PACK_DIR}/<DEST>.
#
# SHA-256 PINS ARE OPTIONAL. A source with an empty SHA256 downloads UNVERIFIED
# (the fetch emits a warning); a non-empty SHA256 is verified exactly and a
# mismatch fails the fetch. To pin a source, download the archive, verify it
# against upstream, and paste the sha256 here — never invent a hash. This keeps
# the "opt in" a conscious act while letting the pack be fetched before pins land.
#
# Every row below is mirrored, with its license, in the repo-root CONTENT-LICENSES.md.

# ── Wavetables ────────────────────────────────────────────────────────────────
yse_content_source(
  NAME    akwf
  URL     "https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE/archive/refs/heads/main.zip"
  SHA256  ""                       # optional: add a verified sha256 to pin
  DEST    "wavetables/akwf"
  LICENSE "CC0-1.0"
)

# ── Sampled instruments ───────────────────────────────────────────────────────
yse_content_source(
  NAME    vsco2-ce
  URL     "https://github.com/sgossner/VSCO-2-CE/archive/refs/heads/master.zip"
  SHA256  ""                       # optional: add a verified sha256 to pin
  DEST    "sfz/vsco2"
  LICENSE "CC0-1.0"
)
yse_content_source(
  NAME    vcsl
  URL     "https://github.com/sgossner/VCSL/archive/refs/heads/master.zip"
  SHA256  ""                       # optional: add a verified sha256 to pin
  DEST    "sfz/vcsl"
  LICENSE "CC0-1.0"
)
yse_content_source(
  NAME    salamander
  URL     "https://archive.org/download/SalamanderGrandPianoV3/SalamanderGrandPianoV3_44.1khz16bit.tar.bz2"
  SHA256  ""                       # optional: add a verified sha256 to pin
  DEST    "sfz/salamander"
  LICENSE "CC-BY-3.0"              # ATTRIBUTION-Salamander.txt ships with the pack
)
# freepats-selection — DISABLED (#367): no working CC0 URL. The old placeholder
# (https://freepats.zenvoid.org/sf2/freepats-general-midi.sf2) is 404, and the
# current FreePats General MIDI set is licensed GPLv3 (not CC0) and shipped as a
# .7z archive this fetch path cannot extract. Re-enable once a genuinely CC0
# source with a supported archive format (zip/tar[.gz|.bz2|.xz]) is identified.
# yse_content_source(
#   NAME    freepats-selection
#   URL     "https://freepats.zenvoid.org/..."   # TODO: real CC0 source, not the GPL GM set
#   SHA256  ""                       # optional: add a verified sha256 to pin
#   DEST    "sfz/freepats"
#   LICENSE "CC0-1.0"
# )

# ── FM banks (TOLERATED, NOT LEGALLY CLEARED) ─────────────────────────────────
# Isolated in fm/dx7-factory/ so the risk is reversible by deleting one folder.
# See content/fm/dx7-factory/README.md.
yse_content_source(
  NAME    dx7-factory
  URL     "https://github.com/asb2m10/dexed/raw/master/assets/builtin_pgm.zip"  # dexed's built-in DX7 factory-style banks (Dexed_01 + SynprezFM_01..32 .syx)
  SHA256  ""                       # optional: add a verified sha256 to pin
  DEST    "fm/dx7-factory"
  LICENSE "tolerated-unclear"
)
