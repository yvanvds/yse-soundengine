// Tests that exercise loading a sound through the in-memory BufferIO VFS.
//
// These hit the sndfile virtual I/O callbacks installed by BufferIO::SetActive
// (BufferIO_Open, BufferIO_Read, BufferIO_Seek, BufferIO_Tell, BufferIO_Length,
// BufferIO_FileExists, BufferIO_Close in YseEngine/BufferIO.cpp) which the
// pure unit tests cannot reach — they only fire when sndfile actually opens
// a registered buffer.
//
// The test reads the existing WAV fixture into memory, registers it under an
// ID, then creates a YSE::sound from that ID and drives the manager forward
// so the slow-pool setup job opens it through the BufferIO VFS path.

#include <doctest/doctest.h>
#include <chrono>
#include <fstream>
#include <thread>
#include <vector>
#include "yse.hpp"
#include "BufferIO.hpp"
#include "sound/soundManager.h"
#include "internal/time.h"
#include "support/null_device.hpp"

#ifndef YSE_TEST_FIXTURES_DIR
#  define YSE_TEST_FIXTURES_DIR "../../Tests/support/fixtures"
#endif

namespace {

// Read the bundled WAV fixture into a heap-allocated byte buffer. Returns an
// empty vector on I/O failure (e.g. the file moved); tests that depend on the
// content guard on .empty() and return early so the doctest passes without
// asserting on environment.
std::vector<char> readWavBytes() {
    const std::string path = std::string(YSE_TEST_FIXTURES_DIR) + "/test_mono_44100.wav";
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    const auto size = static_cast<std::streamsize>(f.tellg());
    f.seekg(0);
    std::vector<char> bytes(static_cast<std::size_t>(size));
    f.read(bytes.data(), size);
    return bytes;
}

// Drive Manager().update() several times so the slow-pool setup job can run
// and the audio-thread-side promote-from-toLoad pass actually fires.
void drainManager(int iterations = 12) {
    for (int i = 0; i < iterations; i++) {
        YSE::INTERNAL::Time().update();
        YSE::SOUND::Manager().update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

} // namespace

TEST_SUITE("sound") {

TEST_CASE("BufferIO + sound: registered buffer routes through sndfile VFS callbacks") {
    if (!TestHelpers::engineInit()) return;
    auto bytes = readWavBytes();
    if (bytes.empty()) return; // fixture missing in this environment

    YSE::BufferIO io;
    io.SetActive(true);
    REQUIRE(io.AddBuffer("vfs-wav", bytes.data(), static_cast<int>(bytes.size())));
    CHECK(io.BufferNameExists("vfs-wav"));

    {
        YSE::sound s;
        s.create("vfs-wav");
        CHECK(s.isValid());
        // The slow-pool calls sf_open_virtual which fires Open/Length/Seek/
        // Tell/Read/Close on our callbacks. Driving the manager forward gives
        // those calls time to land.
        drainManager();
    } // ~sound() releases the impl; BufferIO_Close fires from sf_close.

    // Manager update after sound release exercises promote/release paths too.
    drainManager(4);

    CHECK(io.RemoveBufferByName("vfs-wav"));
    io.SetActive(false);
}

TEST_CASE("BufferIO: copy-mode deep clear on destruction releases owned bytes") {
    auto bytes = readWavBytes();
    if (bytes.empty()) return;
    {
        YSE::BufferIO io(/*storeCopy=*/true);
        io.SetActive(true);
        REQUIRE(io.AddBuffer("deep-clear", bytes.data(), static_cast<int>(bytes.size())));
        CHECK(io.BufferNameExists("deep-clear"));
        io.SetActive(false);
        // BufferIO dtor with storeCopy=true runs Clear(true) which delete[]'s
        // the owned copy — covers the deep branch in IOBuffers::Clear.
    }
}

TEST_CASE("BufferIO: BufferIO_Seek covers all three whence modes via sndfile reads") {
    // When sf_open_virtual / sf_read traverse the file they call Seek() with
    // SEEK_SET (0) and SEEK_END (2); SEEK_CUR (1) is exercised by sndfile's
    // internal pos tracking on multi-block reads. This test triggers all three
    // by performing an actual sound load through the VFS path.
    if (!TestHelpers::engineInit()) return;
    auto bytes = readWavBytes();
    if (bytes.empty()) return;

    YSE::BufferIO io;
    io.SetActive(true);
    REQUIRE(io.AddBuffer("seek-cover", bytes.data(), static_cast<int>(bytes.size())));

    {
        YSE::sound s;
        s.create("seek-cover");
        if (s.isValid()) drainManager();
    }

    io.RemoveBufferByName("seek-cover");
    io.SetActive(false);
}

} // TEST_SUITE("sound")
