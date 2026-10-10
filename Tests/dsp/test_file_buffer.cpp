// Tests for YSE::DSP::fileBuffer's save() path (YseEngine/dsp/fileBuffer.cpp).
//
// save() used to be a JUCE-era stub: it appended ".wav" to the caller's path,
// ran an entirely commented-out writer, and returned true regardless — so a
// caller was told the write succeeded while nothing reached the disk (issue
// #580). These cases pin the writer that replaced it: a real file appears at
// the exact path given, its samples survive the round trip through load(), and
// a write that cannot happen reports false.

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "dsp/fileBuffer.hpp"

TEST_SUITE("dsp") {

  TEST_CASE("fileBuffer: save writes a file that load reads back unchanged") {
    YSE::DSP::fileBuffer b(16);
    // Includes values outside [-1, 1]: a drawn or generated buffer is not
    // limited to the nominal range, and the float WAV must not clip them.
    for (unsigned int i = 0; i < 16; ++i) {
      b.getPtr()[i] = static_cast<float>(i) * 0.25f - 1.75f;
    }

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "yse_file_buffer_580.wav";
    std::error_code ec;
    std::filesystem::remove(path, ec); // best effort: start from a clean slate

    REQUIRE(b.save(path.string().c_str()));
    REQUIRE(std::filesystem::exists(path)); // the stub wrote nothing at all
    CHECK(std::filesystem::file_size(path) > 0u);
    // The caller's path is used verbatim -- no silent ".wav" on top of it.
    CHECK_FALSE(std::filesystem::exists(path.string() + ".wav"));

    YSE::DSP::fileBuffer reloaded(4);
    REQUIRE(reloaded.load(path.string().c_str(), 0));
    REQUIRE(reloaded.getLength() == 16u);
    bool identical = true;
    for (unsigned int i = 0; i < 16; ++i) {
      if (reloaded.getPtr()[i] != b.getPtr()[i]) {
        identical = false;
        break;
      }
    }
    CHECK(identical); // float WAV is lossless

    std::filesystem::remove(path, ec); // best effort
  }

  TEST_CASE("fileBuffer: save reports failure instead of claiming success") {
    YSE::DSP::fileBuffer b(8);
    b = 0.5f;

    CHECK_FALSE(b.save(nullptr));

    // A directory that does not exist cannot be written into; the old stub
    // returned true here too.
    const std::filesystem::path missing =
        std::filesystem::temp_directory_path() / "yse_no_such_dir_580" / "out.wav";
    CHECK_FALSE(b.save(missing.string().c_str()));
    CHECK_FALSE(std::filesystem::exists(missing));
  }

} // TEST_SUITE("dsp")
