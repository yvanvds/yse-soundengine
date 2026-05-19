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
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>
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

// Multi-sine DSP source — 11 sine generators feeding a low-pass, modelled
// on AudioTest's shepard tone (YseEngine/internal/AudioTest.cpp) and on
// Demo07_DspSource. This is the canonical "several sines + IIR feedback"
// shape that triggered the original #53 / #82 reports; using it in the
// recovery test keeps the cpuLoad measurement honest against exactly the
// graph topology the bug appeared with. File-scope for the same lifetime
// reason as ConstantSource.
struct MultiSineSource : YSE::DSP::dspSourceObject {
    static constexpr int kNumGens = 11;
    YSE::DSP::sine    generators[kNumGens];
    YSE::DSP::lowPass lp;
    YSE::DSP::buffer  out;
    float             freq[kNumGens];

    MultiSineSource() {
        // Spread sines across the audible band — keeps every generator
        // contributing real signal rather than collapsing into beating.
        for (int i = 0; i < kNumGens; ++i) {
            freq[i] = 110.f + (float)i * 87.f;
        }
        lp.setFrequency(1500.f);
    }

    void process(YSE::SOUND_STATUS& intent) override {
        out = 0.0f;
        for (int i = 0; i < kNumGens; ++i) out += generators[i](freq[i]);
        out *= (1.f / (float)kNumGens);
        YSE::DSP::buffer& result = lp(out);
        for (UInt i = 0; i < samples.size(); ++i) samples[i] = result;
        if (intent == YSE::SS_WANTSTOSTOP) intent = YSE::SS_STOPPED;
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

static ConstantSource  g_source;
static MultiSineSource g_multi_sine;
static ProbeEffect     g_probe;

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

// ─── MIDI device enumeration (gated on YSE_ENABLE_MIDI_DEVICE) ───────────────

#if YSE_WINDOWS && YSE_ENABLE_MIDI_DEVICE
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
#endif // YSE_WINDOWS && YSE_ENABLE_MIDI_DEVICE

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

// Issue #82: cpuLoad() is our own callback wall-clock / buffer-period EMA.
// Without any user graph attached, the engine's per-callback work is tiny;
// the smoothed reading should sit well below 1.0 (would mean callback taking
// the entire buffer period). 0.5 is a generous ceiling that catches a
// totally broken measurement without being flaky on slow CI machines.
TEST_CASE("engine: cpuLoad stays bounded with no graph activity") {
    if (!TestHelpers::engineInitWithAudio()) return;
    if (YSE::System().getNumDevices() == 0) return;
    // Let callbacks fire long enough for the ~1 s EMA to settle.
    for (int i = 0; i < 30; i++) {
        YSE::System().sleep(50);
        YSE::System().update();
    }
    const float load = YSE::System().cpuLoad();
    CHECK(load >= 0.0f);
    CHECK(load < 0.5f);
}

// Issue #82 (reopened): the original report observed Pa_GetStreamCpuLoad
// staying elevated after stopping AudioTest, and the phi client still sees
// elevated cpuLoad after stopping its sine playback even after the timing
// rewrite landed. Diagnose by sampling cpuLoad once per second through a
// full idle → playing → stopped cycle and writing the trace to a CSV the
// user can post-hoc correlate against an external probe (GetProcessTimes,
// perf, etc.). The graph here mirrors AudioTest (11 sines + low-pass),
// the same DSP shape that triggered both #53 and #82.
//
// Total runtime: ~13 s on a Release build. Run explicitly with:
//
//   python yse.py test --integration
//   yse_tests --test-suite=integration --test-case='*cpuLoad recovers*'
TEST_CASE("engine: cpuLoad recovers after multi-sine playback stops [issue #82]") {
    if (!TestHelpers::engineInitWithAudio()) return;
    if (YSE::System().getNumDevices() == 0) return;

    auto sample_cpu = []() { return YSE::System().cpuLoad(); };

    // Let the EMA settle (~1 s time constant) before the trace begins so
    // we have a clean idle baseline to compare the post-stop reading to.
    for (int i = 0; i < 4; i++) {
        YSE::System().sleep(500);
        YSE::System().update();
    }
    const float idle_baseline = sample_cpu();

    struct Sample {
        int         t_secs;
        const char* phase;
        float       cpu;
    };
    std::vector<Sample> trace;
    trace.push_back({0, "idle", idle_baseline});

    YSE::sound s;
    s.create(g_multi_sine);
    s.relative(true);
    s.play();

    // 5 seconds of active playback.
    for (int i = 1; i <= 5; i++) {
        YSE::System().sleep(1000);
        YSE::System().update();
        trace.push_back({i, "playing", sample_cpu()});
    }

    s.stop();

    // 5 seconds after stop. By the end of this window the ~1 s EMA should
    // have decayed any "playing" contribution to under 1 %, so the final
    // reading should sit very close to the idle baseline.
    for (int i = 1; i <= 5; i++) {
        YSE::System().sleep(1000);
        YSE::System().update();
        trace.push_back({5 + i, "stopped", sample_cpu()});
    }

    // Drop the trace to a deterministic temp path. Whoever runs the test
    // can diff this against an external CPU probe to confirm whether the
    // engine reading still drifts up under their setup.
    const auto log_path = std::filesystem::temp_directory_path() / "yse_cpuload_issue82.csv";
    {
        std::ofstream csv(log_path);
        csv << "t_secs,phase,cpu_load\n";
        for (const auto& smp : trace) {
            csv << smp.t_secs << "," << smp.phase << "," << smp.cpu << "\n";
        }
    }
    MESSAGE("cpuLoad trace written to " << log_path.string());

    float max_playing = 0.f;
    for (int i = 1; i <= 5; i++) max_playing = std::max(max_playing, trace[i].cpu);
    const float final_stopped = trace.back().cpu;

    INFO("idle_baseline = " << idle_baseline);
    INFO("max_playing   = " << max_playing);
    INFO("final_stopped = " << final_stopped);

    // Sanity bounds: nothing negative, no impossible (>1.0) values.
    for (const auto& smp : trace) {
        CHECK(smp.cpu >= 0.0f);
        CHECK(smp.cpu < 1.5f);
    }

    // The recovery assertion. 5 s after stop with a ~1 s tau, the playing
    // component has decayed by e^-5 ≈ 0.7 %, so the final reading must
    // sit very close to the idle baseline. Allow a generous 25 % of
    // max_playing as slack for slow machines and EMA noise; if the
    // engine still reports half its playing value at this point, the
    // bug from the original report is back.
    CHECK(final_stopped <= max_playing * 0.25f + idle_baseline + 0.01f);
}

// ─── Audio callback ───────────────────────────────────────────────────────────

TEST_CASE("engine: audio callback fires within 100ms on a real output device") {
    if (!TestHelpers::engineInitWithAudio()) return;
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
    if (!TestHelpers::engineInitWithAudio()) return;
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
    if (!TestHelpers::engineInitWithAudio()) return;
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
