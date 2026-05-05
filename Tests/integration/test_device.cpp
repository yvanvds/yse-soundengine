// Integration tests for the YSE device layer, MIDI enumeration, and the
// end-to-end audio path.
//
// Coverage:
//   - Audio device enumeration (name, default device)
//   - MIDI device enumeration — Windows only; hardware not required to be present
//   - Engine lifecycle: init, repeated updates, cpuLoad
//   - Audio callback fires within a bounded timeout
//   - Sound loading and playback on a real output device
//   - End-to-end signal probe: DSP source signal reaches an attached effect processor
//
// These tests require a real audio output device and are DISABLED in CTest by
// default (LABELS integration, DISABLED TRUE).  Run explicitly with:
//
//   ctest -L integration
//   yse_tests --test-suite=integration
//
// DSP lifetime note: sound implementations are process-scoped static singletons
// that outlive individual test cases.  DSP source / effect objects passed to
// sound::create() or sound::setDSP() MUST therefore outlive the test binary.
// g_source and g_probe are file-scope statics for exactly this reason.
// Calling System().close() mid-run violates the invariant documented in
// null_device.hpp and is not tested here; shutdown is exercised by process exit.

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include "yse.hpp"
#include "support/null_device.hpp"
#include "headers/defines.hpp"

#ifndef YSE_TEST_FIXTURES_DIR
#  define YSE_TEST_FIXTURES_DIR "../../Tests/support/fixtures"
#endif
static const char* const WAV_FIXTURE = YSE_TEST_FIXTURES_DIR "/test_mono_44100.wav";

namespace {

// DSP source that continuously outputs a constant non-zero signal.
// Declared at file scope so it outlives all test cases and any sound
// implementation that holds a pointer to it.
struct ConstantSource : YSE::DSP::dspSourceObject {
    void process(YSE::SOUND_STATUS& intent) override {
        float* p = samples[0].getPtr();
        UInt len = samples[0].getLength();
        for (UInt i = 0; i < len; i++) p[i] = 0.5f;
        intent = YSE::SS_PLAYING;
    }
    void frequency(float) override {}
};

// Passthrough DSP effect that sets an atomic flag when non-silent audio arrives.
// Declared at file scope for the same lifetime reason as ConstantSource.
struct ProbeEffect : YSE::DSP::dspObject {
    std::atomic<bool> triggered{false};

    void reset() { triggered.store(false, std::memory_order_relaxed); }
    void create() override {}

    void process(MULTICHANNELBUFFER& buf) override {
        for (auto& ch : buf) {
            const float* p = ch.getPtr();
            for (UInt i = 0; i < ch.getLength(); i++) {
                if (std::fabs(p[i]) > 1e-6f) {
                    triggered.store(true, std::memory_order_relaxed);
                    return;
                }
            }
        }
    }
};

static ConstantSource g_source;
static ProbeEffect    g_probe;

// Sleep briefly, pump one update tick, then report whether the audio callback
// fired during that interval (missedCallbacks resets to 0 when callbacks arrive).
bool audioStreamRunning() {
    YSE::System().sleep(50);
    YSE::System().update();
    return YSE::System().missedCallbacks() == 0;
}

} // namespace

TEST_SUITE("integration") {

// ─── Audio device enumeration ─────────────────────────────────────────────────

TEST_CASE("device: getNumDevices does not crash after init") {
    if (!TestHelpers::engineInit()) return;
    (void)YSE::System().getNumDevices();
    CHECK(true);
}

TEST_CASE("device: each enumerated device has a non-empty name") {
    if (!TestHelpers::engineInit()) return;
    unsigned int n = YSE::System().getNumDevices();
    for (unsigned int i = 0; i < n; i++)
        CHECK_FALSE(YSE::System().getDevice(i).getName().empty());
}

TEST_CASE("device: default device name is non-empty when devices exist") {
    if (!TestHelpers::engineInit()) return;
    if (YSE::System().getNumDevices() == 0) return;
    CHECK_FALSE(YSE::System().getDefaultDevice().empty());
}

// ─── MIDI device enumeration (Windows only) ───────────────────────────────────

#if YSE_WINDOWS
TEST_CASE("midi: getNumMidiInDevices does not crash") {
    if (!TestHelpers::engineInit()) return;
    (void)YSE::System().getNumMidiInDevices();
    CHECK(true);
}

TEST_CASE("midi: getNumMidiOutDevices does not crash") {
    if (!TestHelpers::engineInit()) return;
    (void)YSE::System().getNumMidiOutDevices();
    CHECK(true);
}

TEST_CASE("midi: device names are accessible without crash") {
    if (!TestHelpers::engineInit()) return;
    unsigned int nIn  = YSE::System().getNumMidiInDevices();
    unsigned int nOut = YSE::System().getNumMidiOutDevices();
    for (unsigned int i = 0; i < nIn;  i++) (void)YSE::System().getMidiInDeviceName(i);
    for (unsigned int i = 0; i < nOut; i++) (void)YSE::System().getMidiOutDeviceName(i);
    CHECK(true);
}
#endif // YSE_WINDOWS

// ─── Engine lifecycle ─────────────────────────────────────────────────────────

TEST_CASE("engine: init succeeds and channel master is valid") {
    bool ok = TestHelpers::engineInit();
    CHECK(ok);
    CHECK(TestHelpers::engineInitialized());
}

TEST_CASE("engine: repeated update calls do not crash") {
    if (!TestHelpers::engineInit()) return;
    for (int i = 0; i < 5; i++) {
        YSE::System().sleep(10);
        YSE::System().update();
    }
    CHECK(true);
}

TEST_CASE("engine: cpuLoad returns a non-negative value") {
    if (!TestHelpers::engineInit()) return;
    CHECK(YSE::System().cpuLoad() >= 0.0f);
}

// ─── Audio callback ───────────────────────────────────────────────────────────

TEST_CASE("engine: audio callback fires within 100ms on a real output device") {
    if (!TestHelpers::engineInit()) return;
    if (YSE::System().getNumDevices() == 0) return;
    CHECK(audioStreamRunning());
}

// ─── Sound loading and playback ───────────────────────────────────────────────

TEST_CASE("sound: WAV fixture creates a valid sound on a real device") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(WAV_FIXTURE);
    CHECK(s.isValid());
}

TEST_CASE("sound: playing WAV fixture does not crash during update loop") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(WAV_FIXTURE);
    if (!s.isValid()) return;
    s.relative(true);
    s.play();
    for (int i = 0; i < 10; i++) {
        YSE::System().sleep(20);
        YSE::System().update();
    }
    CHECK(true);
}

TEST_CASE("sound: DSP source sound plays on a real device without crash") {
    if (!TestHelpers::engineInit()) return;
    if (YSE::System().getNumDevices() == 0) return;
    YSE::sound s;
    s.create(g_source);
    s.relative(true);
    s.play();
    for (int i = 0; i < 5; i++) {
        YSE::System().sleep(20);
        YSE::System().update();
    }
    s.stop();
    CHECK(true);
}

// ─── End-to-end signal probe ──────────────────────────────────────────────────

TEST_CASE("engine: DSP source signal reaches an attached effect processor") {
    if (!TestHelpers::engineInit()) return;
    if (YSE::System().getNumDevices() == 0) return;
    if (!audioStreamRunning()) return;

    g_probe.reset();
    YSE::sound s;
    s.create(g_source);
    s.setDSP(&g_probe);
    s.relative(true);
    s.play();

    for (int i = 0; i < 10 && !g_probe.triggered.load(std::memory_order_relaxed); i++) {
        YSE::System().sleep(50);
        YSE::System().update();
    }
    s.stop();

    CHECK(g_probe.triggered.load(std::memory_order_relaxed));
}

} // TEST_SUITE("integration")
