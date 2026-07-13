// Smoke test for the optional bundled content pack (issue #179).
//
// Verifies that every asset actually present under the content-pack root loads
// through its real engine loader end-to-end:
//   - .syx  → YSE::SYNTH::dx7SysEx::loadBank  (DX7 importer, #177)
//   - .sfz  → YSE::DSP::loadSFZ               (SFZ v1-subset parser, #173)
//   - wavetables/*.wav → fileBuffer::load     (libsndfile decode, #174)
//
// The repo commits a small CC0 seed (content/tools/generate_content.py), so the
// pack root always exists in a source build and the "at least one loaded"
// checks exercise real loads. Assets pulled only by the opt-in fetch path
// (AKWF, VSCO2/VCSL, Salamander, FreePats, DX7 factory) are validated too when
// present; when absent the test skips them gracefully — never fails.
//
// No audio device and no engine init are needed (all three loaders are offline
// file readers), so this runs on headless CI in its own "contentpack" suite.

#include <doctest/doctest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

#include "dsp/fileBuffer.hpp"
#include "dsp/fm/dx7Sysex.hpp"
#include "dsp/sfzModel.hpp"

#ifndef YSE_CONTENT_PACK_DIR
#define YSE_CONTENT_PACK_DIR "../../content"
#endif

namespace {
  namespace fs = std::filesystem;

  fs::path packRoot() {
    return fs::path(YSE_CONTENT_PACK_DIR);
  }

  std::string lowerExt(const fs::path& p) {
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
  }
} // namespace

TEST_SUITE("contentpack") {

  TEST_CASE("content pack: root is present (or gracefully absent)") {
    const fs::path root = packRoot();
    if (!fs::exists(root)) {
      MESSAGE("content pack absent at " << root.string() << " — skipping");
      return;
    }
    CHECK(fs::is_directory(root));
  }

  TEST_CASE("content pack: every bundled DX7 .syx bank loads") {
    const fs::path root = packRoot();
    if (!fs::exists(root)) {
      MESSAGE("content pack absent — skipping");
      return;
    }
    int loaded = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
      if (!entry.is_regular_file() || lowerExt(entry.path()) != ".syx") continue;
      YSE::SYNTH::dx7Bank bank;
      const std::string path = entry.path().string();
      INFO("bank: " << path);
      CHECK(YSE::SYNTH::dx7SysEx::loadBank(path.c_str(), bank));
      CHECK_FALSE(bank.empty());
      ++loaded;
    }
    // The committed CC0 seed (fm/original/yse_originals.syx) guarantees one.
    CHECK(loaded >= 1);
  }

  TEST_CASE("content pack: every bundled .sfz instrument loads") {
    const fs::path root = packRoot();
    if (!fs::exists(root)) {
      MESSAGE("content pack absent — skipping");
      return;
    }
    int loaded = 0;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
      if (!entry.is_regular_file() || lowerExt(entry.path()) != ".sfz") continue;
      const std::string path = entry.path().string();
      INFO("sfz: " << path);
      YSE::DSP::sfzInstrument inst = YSE::DSP::loadSFZ(path);
      CHECK(inst.valid);
      CHECK_FALSE(inst.regions.empty());
      ++loaded;
    }
    // The committed CC0 seed (sfz/yse_pulse.sfz) guarantees one.
    CHECK(loaded >= 1);
  }

  TEST_CASE("content pack: every bundled wavetable WAV decodes") {
    const fs::path wavetables = packRoot() / "wavetables";
    if (!fs::exists(wavetables)) {
      MESSAGE("wavetables absent — skipping");
      return;
    }
    int loaded = 0;
    for (const auto& entry : fs::recursive_directory_iterator(wavetables)) {
      if (!entry.is_regular_file() || lowerExt(entry.path()) != ".wav") continue;
      YSE::DSP::fileBuffer buf;
      const std::string path = entry.path().string();
      INFO("wavetable: " << path);
      CHECK(buf.load(path.c_str()));
      CHECK(buf.getLength() > 0);
      ++loaded;
    }
    // The committed CC0 seed ships four single-cycle tables.
    CHECK(loaded >= 1);
  }
}
