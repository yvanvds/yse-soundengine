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
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>
#include <thread>
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

// Issue #826 — where a seek is allowed to leave the reader.
//
// BufferIO_Seek stored whatever position it was handed. A negative one was
// reachable from every whence (SEEK_SET with a negative offset, SEEK_CUR
// rewinding past the start, SEEK_END further back than the buffer is long) and
// passed #825's `startpos >= length` guard untouched, so BufferIO_Read then ran
// std::copy from `buffer + startpos` — memory in front of the registered
// buffer.
//
// The contract chosen is CLAMP, not reject: a registered buffer is a fixed
// range of bytes, so every seek folds into [0, length] and reports where it
// landed. Refusing would leave the reader somewhere the caller did not ask for
// with no more information than clamping gives it — sndfile detects a bad jump
// either way, by comparing the returned position against the one it asked for.
// Clamping also keeps the invariant this file relies on everywhere else: the
// reader is inside its buffer at all times, so no read can be unsafe.
TEST_SUITE("io") {

  TEST_CASE("BufferIO: an out-of-range seek lands inside the buffer (#826)") {
    YSE::BufferIO io;
    io.SetActive(true);

    constexpr int kLen = 32;
    // Heap-allocated on purpose: a read taken in front of the buffer is then a
    // heap-buffer-underflow ASan names and stops on, not a stack byte that
    // happens to be readable.
    std::vector<char> src(static_cast<std::size_t>(kLen));
    for (int i = 0; i < kLen; ++i) {
      src[static_cast<std::size_t>(i)] = static_cast<char>(i + 1);
    }
    REQUIRE(io.AddBuffer("seek-range", src.data(), kLen));

    long long size = 0;
    void* handle = nullptr;
    REQUIRE(YSE::INTERNAL::customFileReader::Open("seek-range", &size, &handle));
    REQUIRE(size == kLen);

    // Driven through the SF_VIRTUAL_IO table itself — the struct handed to
    // sf_open_virtual — so this is the same entry point libsndfile uses on a
    // registered buffer, not a private call into BufferIO.cpp.
    SF_VIRTUAL_IO& vio = YSE::INTERNAL::customFileReader::GetVIO();
    REQUIRE(vio.seek != nullptr);
    REQUIRE(vio.read != nullptr);
    REQUIRE(vio.tell != nullptr);
    const auto seek = [&](long long offset, int whence) {
      return static_cast<long long>(vio.seek(static_cast<sf_count_t>(offset), whence, handle));
    };
    const auto read = [&](void* into, long long bytes) {
      return static_cast<long long>(vio.read(into, static_cast<sf_count_t>(bytes), handle));
    };
    const auto tell = [&] { return static_cast<long long>(vio.tell(handle)); };

    char dest[kLen];
    std::memset(dest, 0, sizeof dest);

    // SEEK_SET before the start. The read that follows is the defect itself:
    // unfixed it copies from src.data() - 1.
    CHECK(seek(-1, 0 /* SEEK_SET */) == 0);
    CHECK(tell() == 0);
    CHECK(read(dest, 4) == 4);
    CHECK(dest[0] == src[0]);
    CHECK(dest[3] == src[3]);

    CHECK(seek(-4 * kLen, 0) == 0);
    CHECK(tell() == 0);

    // SEEK_SET past the end stops at the end, where a read is empty (#825).
    CHECK(seek(kLen + 1000, 0) == kLen);
    CHECK(read(dest, sizeof dest) == 0);

    // SEEK_CUR rewinding past the start.
    CHECK(seek(8, 0) == 8);
    CHECK(seek(-40, 1 /* SEEK_CUR */) == 0);
    CHECK(read(dest, 1) == 1);
    CHECK(dest[0] == src[0]);

    // SEEK_CUR running past the end.
    CHECK(seek(4 * kLen, 1) == kLen);

    // SEEK_END further back than the buffer is long, and forwards past it.
    CHECK(seek(-(kLen + 1), 2 /* SEEK_END */) == 0);
    CHECK(seek(16, 2) == kLen);

    // Clamping must not blunt an in-range seek: these land exactly.
    CHECK(seek(-8, 2) == kLen - 8);
    CHECK(seek(kLen, 0) == kLen); // one past the last byte is a legal position
    CHECK(seek(0, 0) == 0);
    CHECK(seek(kLen - 1, 0) == kLen - 1);
    CHECK(read(dest, 4) == 1); // only the last byte is left
    CHECK(dest[0] == src[kLen - 1]);

    // Offsets big enough to overflow the position arithmetic if it were
    // evaluated before being range-checked.
    constexpr long long kMin = std::numeric_limits<long long>::min();
    constexpr long long kMax = std::numeric_limits<long long>::max();
    CHECK(seek(kMin, 0) == 0);
    CHECK(seek(kMax, 0) == kLen);
    CHECK(seek(kMax, 1) == kLen);
    CHECK(seek(kMin, 1) == 0);
    CHECK(seek(kMin, 2) == 0);
    CHECK(seek(kMax, 2) == kLen);

    // A whence nobody defines leaves the reader where it was.
    CHECK(seek(4, 0) == 4);
    CHECK(seek(99, 7) == 4);
    CHECK(tell() == 4);

    YSE::INTERNAL::customFileReader::Close(handle);
    CHECK(io.RemoveBufferByName("seek-range"));
    io.SetActive(false);
  }

} // TEST_SUITE("io")

// Issue #837 — deactivating the custom IO layer while a loader is mid-flight.
//
// io::setActive(false) → ResetVIO() plain-wrote the CALLBACK::* pointer
// globals and the SF_VIRTUAL_IO table while slow-pool threads were still
// reading them (the dev-push TSan sweep caught soundFile::~soundFile's
// customFileReader::Close() racing the closePtr write). The fix publishes the
// whole callback set as an immutable snapshot behind one atomic pointer, so a
// toggle can never tear the set a reader is using. This drives that exact
// interleaving on purpose; TSan is the oracle — unfixed, every toggle races
// the loader's reads and the tests-tsan gate reports on it.
TEST_SUITE("io") {

  TEST_CASE("BufferIO: activation toggles do not race in-flight readers (#837)") {
    YSE::BufferIO io;
    io.SetActive(true);

    std::atomic<bool> stop{false};
    std::thread loader([&stop] {
      while (!stop.load(std::memory_order_acquire)) {
        // The name misses on purpose: BufferIO_Open then answers false without
        // creating a handle, so no handle can leak when a deactivation lands
        // between an open and its close — while the openPtr read still races
        // the unfixed ResetVIO().
        long long size = 0;
        void* handle = nullptr;
        if (YSE::INTERNAL::customFileReader::Open("no-such-buffer", &size, &handle)) {
          YSE::INTERNAL::customFileReader::Close(handle);
        }
        // The closePtr read — the pair TSan flagged from soundFile's
        // destructor. BufferIO_Close on a null handle is a no-op delete.
        YSE::INTERNAL::customFileReader::Close(nullptr);
        // A field of the published SF_VIRTUAL_IO table: the same read
        // sf_open_virtual's struct copy performs against ResetVIO()'s wipe.
        (void)YSE::INTERNAL::customFileReader::GetVIO().read;
      }
    });

    for (int i = 0; i < 400; ++i) {
      io.SetActive(false);
      io.SetActive(true);
    }

    stop.store(true, std::memory_order_release);
    loader.join();
    io.SetActive(false);

    // The assertion proper is TSan's: no report from the interleaving above.
    CHECK_FALSE(io.GetActive());
  }

} // TEST_SUITE("io")

// The user-visible half of #826: the ordinary way an out-of-range seek is
// reached is a file whose chunk header lies about its size, which sends
// libsndfile's chunk walk outside the buffer. A registered buffer holding one
// has to fail to load and leave the VFS usable — not take the reader outside
// the bytes it was given.
TEST_SUITE("io") {

  TEST_CASE("BufferIO: a WAV whose chunk size lies fails to load safely (#826)") {
    constexpr unsigned int kFrames = 40;

    YSE::DSP::fileBuffer original(kFrames);
    for (unsigned int i = 0; i < kFrames; ++i) {
      original.getPtr()[i] = 0.25f + static_cast<float>(i) * 0.01f;
    }

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "yse_bufferio_826.wav";
    std::error_code ec;
    std::filesystem::remove(path, ec); // best effort: start from a clean slate
    REQUIRE(original.save(path.string().c_str()));

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    REQUIRE(f.is_open());
    const auto size = static_cast<std::streamsize>(f.tellg());
    f.seekg(0);
    std::vector<char> bytes(static_cast<std::size_t>(size));
    f.read(bytes.data(), size);
    f.close();
    REQUIRE(bytes.size() > 12u);

    // Same file with a JUNK chunk spliced in behind the RIFF/WAVE header,
    // claiming a size no buffer could hold. sndfile skips a JUNK chunk by
    // seeking over it, so the walk is thrown clean outside the buffer.
    std::vector<char> corrupt;
    corrupt.reserve(bytes.size() + 8u);
    corrupt.insert(corrupt.end(), bytes.begin(), bytes.begin() + 12); // RIFF/size/WAVE
    const char junk[4] = {'J', 'U', 'N', 'K'};
    corrupt.insert(corrupt.end(), junk, junk + 4);
    for (int i = 0; i < 4; ++i) { // 0xFFFFFF00, little endian
      corrupt.push_back(static_cast<char>(i == 0 ? 0x00 : 0xFF));
    }
    corrupt.insert(corrupt.end(), bytes.begin() + 12, bytes.end());
    // Keep the RIFF size honest about the bytes that follow it, so the only
    // thing wrong with the file is the chunk size under test.
    const std::uint32_t riffSize = static_cast<std::uint32_t>(corrupt.size() - 8u);
    for (int i = 0; i < 4; ++i) {
      corrupt[4u + static_cast<std::size_t>(i)] = static_cast<char>((riffSize >> (8 * i)) & 0xFF);
    }

    {
      YSE::BufferIO io;
      io.SetActive(true);
      REQUIRE(io.AddBuffer("wav-lying-chunk", corrupt.data(), static_cast<int>(corrupt.size())));
      REQUIRE(io.AddBuffer("wav-intact", bytes.data(), static_cast<int>(bytes.size())));

      // No 'fmt '/'data' is reachable once the walk has been thrown past the
      // end, so the open fails. What matters is where the reader is while that
      // happens: inside the buffer, at its end.
      YSE::DSP::fileBuffer bad(1);
      CHECK_FALSE(bad.load("wav-lying-chunk", 0));

      // ... and the VFS is no worse for it: the intact buffer beside it still
      // decodes to the samples it was built from.
      YSE::DSP::fileBuffer good(1);
      REQUIRE(good.load("wav-intact", 0));
      CHECK(good.getLength() == kFrames);
      CHECK(good.getPtr()[kFrames - 1] == original.getPtr()[kFrames - 1]);

      CHECK(io.RemoveBufferByName("wav-lying-chunk"));
      CHECK(io.RemoveBufferByName("wav-intact"));
      io.SetActive(false);
    }

    std::filesystem::remove(path, ec); // best effort
  }

} // TEST_SUITE("io")
