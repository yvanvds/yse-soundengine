# YseContentPack.cmake — optional bundled content pack (issue #179)

# CMake 3.31 (CMP0174) leaves a one-value keyword variable *unset* when it is
# given an empty string, instead of setting it to "". Opt in explicitly so the
# behaviour is deterministic; the pin check below handles the unset case.
if(POLICY CMP0174)
  cmake_policy(SET CMP0174 NEW)
endif()

#
# The content pack is an OPTIONAL asset pack (wavetables, SFZ instruments, FM
# banks). It is NEVER baked into the library binary — libyse links no asset —
# and it costs a default build nothing: with both options below OFF this module
# only sets a cache variable and returns.
#
# Layout (content/, "TestResources-style"):
#   wavetables/        single-cycle tables (VA/wavetable voice, #175)
#   sfz/               SFZ v1-subset instruments (#172/#173) + samples/
#   fm/original/       CC0 FM bank authored by us
#   fm/dx7-factory/    tolerated-not-cleared banks — populated by fetch only
#
# The repo commits a small CC0 seed (authored via content/tools/generate_content.py).
# Large third-party CC0/CC-BY collections and the tolerated DX7 factory banks are
# pulled on demand by the opt-in fetch path from content/pack-manifest.cmake.
#
# Options:
#   YSE_FETCH_CONTENT_PACK   (OFF) download + verify the third-party sources
#   YSE_INSTALL_CONTENT_PACK (OFF) install the pack next to the library
#   YSE_CONTENT_PACK_DIR     (content/) where the pack lives / is assembled

option(YSE_FETCH_CONTENT_PACK
  "Download the third-party content pack sources (opt-in; SHA-256 pins are optional — see content/pack-manifest.cmake)"
  OFF)
option(YSE_INSTALL_CONTENT_PACK
  "Install the content pack under <prefix>/share/yse/content"
  OFF)
set(YSE_CONTENT_PACK_DIR "${CMAKE_SOURCE_DIR}/content"
  CACHE PATH "Root directory of the bundled content pack (seed + any fetched sources)")

# Downloads one manifest source, verifies its SHA-256, and extracts it under
# ${YSE_CONTENT_PACK_DIR}/${DEST}. A re-configure is idempotent: a per-source
# .fetched marker short-circuits work already done.
function(yse_content_source)
  # PARSE_ARGV (not ${ARGN}): unquoted ARGN expansion silently drops an empty
  # SHA256 "" element and shifts the remaining keywords, defeating the pin check.
  cmake_parse_arguments(PARSE_ARGV 0 A "" "NAME;URL;SHA256;DEST;LICENSE" "")
  if(NOT A_NAME OR NOT A_URL OR NOT A_DEST)
    message(FATAL_ERROR "yse_content_source: NAME, URL and DEST are required")
  endif()
  # SHA-256 verification is OPTIONAL. A non-empty SHA256 is verified exactly (a
  # mismatch fails the download); an empty SHA256 downloads the source UNVERIFIED
  # after a warning, so a pin can be added to the manifest later.
  set(_verify_sha TRUE)
  if(NOT DEFINED A_SHA256 OR A_SHA256 STREQUAL "")
    set(_verify_sha FALSE)
    message(WARNING
      "Content source '${A_NAME}' has no SHA256 pin — downloading ${A_URL} "
      "UNVERIFIED. To pin it, download the archive, verify it against upstream, "
      "and set its sha256 in content/pack-manifest.cmake. (Never invent a hash.)")
  endif()

  set(_dest "${YSE_CONTENT_PACK_DIR}/${A_DEST}")
  set(_marker "${_dest}/.fetched")
  if(EXISTS "${_marker}")
    message(STATUS "Content pack: '${A_NAME}' already present — skipping")
    return()
  endif()

  get_filename_component(_ext "${A_URL}" LAST_EXT)
  set(_archive "${CMAKE_BINARY_DIR}/_content/${A_NAME}${_ext}")
  message(STATUS "Content pack: fetching '${A_NAME}' (${A_LICENSE}) → ${A_DEST}")
  if(_verify_sha)
    file(DOWNLOAD "${A_URL}" "${_archive}"
      EXPECTED_HASH SHA256=${A_SHA256}
      SHOW_PROGRESS
      STATUS _dl_status)
  else()
    file(DOWNLOAD "${A_URL}" "${_archive}"
      SHOW_PROGRESS
      STATUS _dl_status)
  endif()
  list(GET _dl_status 0 _dl_code)
  if(NOT _dl_code EQUAL 0)
    list(GET _dl_status 1 _dl_msg)
    message(FATAL_ERROR "Content pack: download of '${A_NAME}' failed: ${_dl_msg}")
  endif()

  file(MAKE_DIRECTORY "${_dest}")
  # SF2 and bare .syx are single files, not archives — copy verbatim.
  if(_ext MATCHES "\\.(zip|tar|gz|bz2|tgz|tbz2|xz)$")
    file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_dest}")
  else()
    get_filename_component(_leaf "${A_URL}" NAME)
    file(COPY "${_archive}" DESTINATION "${_dest}")
    file(RENAME "${_dest}/${A_NAME}${_ext}" "${_dest}/${_leaf}")
  endif()
  file(WRITE "${_marker}" "${A_NAME} ${A_SHA256}\n")
endfunction()

if(YSE_FETCH_CONTENT_PACK)
  set(_manifest "${YSE_CONTENT_PACK_DIR}/pack-manifest.cmake")
  if(NOT EXISTS "${_manifest}")
    message(FATAL_ERROR "YSE_FETCH_CONTENT_PACK is ON but ${_manifest} is missing")
  endif()
  message(STATUS "Content pack: assembling from ${_manifest}")
  include("${_manifest}")
endif()

if(YSE_INSTALL_CONTENT_PACK)
  # Ship the whole pack as data — never linked into the library. Drop the fetch
  # bookkeeping markers from the installed tree.
  install(DIRECTORY "${YSE_CONTENT_PACK_DIR}/"
    DESTINATION "share/yse/content"
    PATTERN ".fetched" EXCLUDE
    PATTERN ".gitkeep" EXCLUDE)
endif()
