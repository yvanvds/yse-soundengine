// Tests for YSE::BufferIO (YseEngine/BufferIO.{hpp,cpp}).
//
// BufferIO is the in-memory VFS layer used to feed sounds from byte buffers
// rather than the filesystem. We exercise the public registration API here.
//
// NOTE — BufferIO uses a process-global `IOBuffers*` and its destructor
//   deletes it without nulling, so creating multiple BufferIO instances in a
//   single process triggers a use-after-free (pre-existing bug, out of scope
//   for this commit). Tests therefore live inside ONE TEST_CASE with
//   SUBCASEs sharing a single BufferIO instance.

#include <doctest/doctest.h>
#include "BufferIO.hpp"
#include "dsp/fileBuffer.hpp"
#include "internal/customFileReader.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

TEST_SUITE("io") {

  TEST_CASE("BufferIO: public API exercised on a single instance") {
    YSE::BufferIO io;

    SUBCASE("default-constructed is inactive") {
      CHECK_FALSE(io.GetActive());
    }

    SUBCASE("SetActive toggles state") {
      io.SetActive(true);
      CHECK(io.GetActive());
      io.SetActive(false);
      CHECK_FALSE(io.GetActive());
    }

    SUBCASE("idempotent activate / deactivate") {
      io.SetActive(true);
      io.SetActive(true); // double-activate
      CHECK(io.GetActive());
      io.SetActive(false);
      io.SetActive(false); // double-deactivate
      CHECK_FALSE(io.GetActive());
    }

    SUBCASE("AddBuffer / BufferNameExists round-trip") {
      io.SetActive(true);
      char data[16] = {0};
      CHECK(io.AddBuffer("buf-rt", data, 16));
      CHECK(io.BufferNameExists("buf-rt"));
      CHECK(io.BufferExists(data));
      CHECK(io.RemoveBuffer(data));
      CHECK_FALSE(io.BufferExists(data));
      io.SetActive(false);
    }

    SUBCASE("BufferNameExists false for unknown ID") {
      CHECK_FALSE(io.BufferNameExists("not-registered"));
    }

    SUBCASE("BufferExists false for unknown pointer") {
      char data[4] = {0};
      CHECK_FALSE(io.BufferExists(data));
    }

    SUBCASE("AddBuffer rejects duplicate IDs") {
      char a[4] = {1, 2, 3, 4};
      char b[4] = {5, 6, 7, 8};
      CHECK(io.AddBuffer("dup-id", a, 4));
      CHECK_FALSE(io.AddBuffer("dup-id", b, 4));
      CHECK(io.RemoveBufferByName("dup-id"));
    }

    SUBCASE("RemoveBufferByName succeeds for known ID") {
      char data[4] = {0};
      REQUIRE(io.AddBuffer("rm-byname", data, 4));
      CHECK(io.RemoveBufferByName("rm-byname"));
      CHECK_FALSE(io.BufferNameExists("rm-byname"));
    }

    SUBCASE("RemoveBufferByName fails for unknown ID") {
      CHECK_FALSE(io.RemoveBufferByName("never-added"));
    }

    SUBCASE("RemoveBuffer by pointer") {
      char data[4] = {0};
      REQUIRE(io.AddBuffer("rm-byptr", data, 4));
      CHECK(io.RemoveBuffer(data));
      CHECK_FALSE(io.BufferExists(data));
    }

    SUBCASE("RemoveBuffer fails for unknown pointer") {
      char unknown[4] = {0};
      CHECK_FALSE(io.RemoveBuffer(unknown));
    }

    SUBCASE("RemoveBufferByName clears via BufferNameExists") {
      char data[4] = {1, 2, 3, 4};
      REQUIRE(io.AddBuffer("name-roundtrip", data, 4));
      CHECK(io.BufferNameExists("name-roundtrip"));
      CHECK(io.RemoveBufferByName("name-roundtrip"));
      CHECK_FALSE(io.BufferNameExists("name-roundtrip"));
    }
  }

} // TEST_SUITE("io")

// Separate TEST_SUITE so the storeCopy=true instance does not collide with the
// shared default-constructed one above (the BufferIO process-global is
// recreated whenever the previous instance dies — see the comment at the top
// of this file).
TEST_SUITE("io") {

  TEST_CASE("BufferIO storeCopy mode: owned copy survives caller scope") {
    YSE::BufferIO io(/*storeCopy=*/true);
    io.SetActive(true);
    {
      char volatile_data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
      REQUIRE(io.AddBuffer("copy-survives", volatile_data, 8));
      CHECK(io.BufferNameExists("copy-survives"));
    } // volatile_data goes out of scope; the owned copy is intact.
    CHECK(io.BufferNameExists("copy-survives"));
    // Remove by name in copy mode runs delete[] on the owned copy.
    CHECK(io.RemoveBufferByName("copy-survives"));
    io.SetActive(false);
  }

} // TEST_SUITE("io")

// Issue #825 — what a registered buffer is worth is what comes back out of it.
//
// BufferIO_Read clamped its end position with `if (endpos >= length) endpos =
// length - 1`, so the clamp fired on a read ending exactly at the end of the
// buffer and dropped the final byte. Every buffer-backed source was therefore
// one byte short at EOF, which libsndfile reads as a truncated last frame. The
// same expression made a read issued once the reader is already at the end
// produce `endpos < startpos`, i.e. std::copy called with `last` before
// `first` — undefined behaviour, and a negative byte count returned to the
// caller. A zero-length read at EOF is an ordinary thing for a VFS to be asked
// for, so it has to answer 0.
TEST_SUITE("io") {

  TEST_CASE("BufferIO: a registered buffer reads back byte-for-byte (#825)") {
    YSE::BufferIO io;
    io.SetActive(true);

    // Distinct, all non-zero, so a dropped byte is visible wherever it falls —
    // the last one above all, since that is the byte the clamp used to eat.
    constexpr int kLen = 64;
    char src[kLen];
    for (int i = 0; i < kLen; ++i) {
      src[i] = static_cast<char>(i + 1);
    }
    REQUIRE(io.AddBuffer("byte-exact", src, kLen));

    long long size = 0;
    void* handle = nullptr;
    REQUIRE(YSE::INTERNAL::customFileReader::Open("byte-exact", &size, &handle));
    CHECK(size == kLen);

    char dest[kLen];
    std::memset(dest, 0, sizeof dest);
    // One read covering the whole buffer: the exact shape the clamp mishandled.
    CHECK(YSE::INTERNAL::CALLBACK::readPtr(dest, kLen, handle) == kLen); // was kLen - 1
    CHECK(std::memcmp(dest, src, kLen) == 0); // src[kLen - 1] included
    CHECK(YSE::INTERNAL::CALLBACK::getPosPtr(handle) == kLen);

    // Reading on at EOF: 0 bytes, no copy, and nothing negative.
    char tail[8] = {0};
    CHECK(YSE::INTERNAL::CALLBACK::readPtr(tail, sizeof tail, handle) == 0);
    CHECK(YSE::INTERNAL::CALLBACK::getPosPtr(handle) == kLen);

    // The same request after an explicit seek to the end. This is the reversed
    // range: without the guard std::copy runs from buffer + length towards
    // buffer + length - 1 and walks off the object.
    CHECK(YSE::INTERNAL::CALLBACK::seekPtr(0, 2 /* SEEK_END */, handle) == kLen);
    CHECK(YSE::INTERNAL::CALLBACK::readPtr(tail, sizeof tail, handle) == 0);

    // A partial read still stops where it was asked to, not one byte earlier.
    CHECK(YSE::INTERNAL::CALLBACK::seekPtr(0, 0 /* SEEK_SET */, handle) == 0);
    std::memset(dest, 0, sizeof dest);
    CHECK(YSE::INTERNAL::CALLBACK::readPtr(dest, 16, handle) == 16);
    CHECK(std::memcmp(dest, src, 16) == 0);
    // ... and the tail after it arrives complete.
    CHECK(YSE::INTERNAL::CALLBACK::readPtr(dest + 16, kLen - 16, handle) == kLen - 16);
    CHECK(std::memcmp(dest, src, kLen) == 0);

    YSE::INTERNAL::customFileReader::Close(handle);
    CHECK(io.RemoveBufferByName("byte-exact"));
    io.SetActive(false);
  }

} // TEST_SUITE("io")

// The user-visible half of #825: audio registered as a buffer has to decode to
// the same samples as the file it came from, last frame included. A buffer
// whose final byte never arrives leaves libsndfile one frame short, so the last
// frame of every buffer-backed sound was silence.
TEST_SUITE("io") {

  TEST_CASE("BufferIO: a WAV loaded through the VFS keeps its last frame (#825)") {
    constexpr unsigned int kFrames = 40;

    // Build the source from a buffer we know sample by sample, rather than from
    // a fixture, so the final sample is unmistakably non-zero and a dropped
    // frame cannot pass as a rounding difference.
    YSE::DSP::fileBuffer original(kFrames);
    for (unsigned int i = 0; i < kFrames; ++i) {
      original.getPtr()[i] = 0.25f + static_cast<float>(i) * 0.01f;
    }

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "yse_bufferio_825.wav";
    std::error_code ec;
    std::filesystem::remove(path, ec); // best effort: start from a clean slate
    // save() refuses while a custom IO layer is active (it is read-only), so
    // the file is written before the VFS goes up.
    REQUIRE(original.save(path.string().c_str()));

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    REQUIRE(f.is_open());
    const auto size = static_cast<std::streamsize>(f.tellg());
    f.seekg(0);
    std::vector<char> bytes(static_cast<std::size_t>(size));
    f.read(bytes.data(), size);
    f.close();
    REQUIRE(bytes.size() > 0u);

    {
      YSE::BufferIO io;
      io.SetActive(true);
      REQUIRE(io.AddBuffer("wav-in-ram", bytes.data(), static_cast<int>(bytes.size())));

      YSE::DSP::fileBuffer fromBuffer(1);
      REQUIRE(fromBuffer.load("wav-in-ram", 0));
      // Without the fix libsndfile sees EOF one byte early and hands back
      // kFrames - 1 frames, leaving the last slot at whatever resize() left.
      CHECK(fromBuffer.getLength() == kFrames);
      bool identical = true;
      for (unsigned int i = 0; i < kFrames; ++i) {
        if (fromBuffer.getPtr()[i] != original.getPtr()[i]) {
          identical = false;
          break;
        }
      }
      CHECK(identical);
      CHECK(fromBuffer.getPtr()[kFrames - 1] == original.getPtr()[kFrames - 1]);

      CHECK(io.RemoveBufferByName("wav-in-ram"));
      io.SetActive(false);
    }

    std::filesystem::remove(path, ec); // best effort
  }

} // TEST_SUITE("io")
