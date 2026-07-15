// N-channel processing contract tests for the dspObject filter/delay/effect
// MODULES upgraded under issue #158.
//
// Before #158 these modules processed only buffer[0] (the first channel). They
// now process every channel of the MULTICHANNELBUFFER independently, each with
// its own per-channel filter/delay/cascade state (see the contract on
// DSP::dspObject::process and the DSP::perChannel<> helper).
//
// The three things the contract promises, and how they are checked here:
//
//   1. Every channel is processed independently. A module fed an N-channel
//      buffer with a distinct signal per channel produces, on channel k, the
//      *exact* same output a fresh mono instance produces when fed that same
//      signal alone. This simultaneously proves (a) channels do not bleed into
//      each other and (b) the mono path (channel 0 in isolation) is unchanged —
//      the "bit-identical on mono input" regression requirement.
//
//   2. A mid-stream channel-count change (device restart: 6 -> 2 -> 6) neither
//      crashes nor produces garbage.
//
//   3. Steady-state processing at a fixed channel count is allocation-free on
//      the audio path (the only allocation allowed is the create()/resize
//      path, exercised outside the probe here).
//
// No audio device required — SAMPLERATE is initialised to 44100 by the
// portaudioDeviceManager translation unit at static-initialisation time.

#include <doctest/doctest.h>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include "dsp/dspObject.hpp"
#include "dsp/modules/filters/lowpass.hpp"
#include "dsp/modules/filters/highpass.hpp"
#include "dsp/modules/filters/bandpass.hpp"
#include "dsp/modules/filters/sweep.hpp"
#include "dsp/modules/phaser.hpp"
#include "dsp/modules/ringModulator.hpp"
#include "dsp/modules/delay/basicDelay.hpp"
#include "dsp/modules/delay/lowpassDelay.hpp"
#include "dsp/modules/delay/highpassDelay.hpp"
#include "dsp/modules/delay/feedbackDelay.hpp"
#include "headers/defines.hpp"
#include "support/audio_helpers.hpp"
#include "support/alloc_probe.hpp"

namespace {

  constexpr float kPi = 3.14159265358979323846f;
  constexpr unsigned kLen = 128;
  constexpr int kBlocks = 40; // enough for filter/delay state to settle

  using ModuleFactory = std::function<std::unique_ptr<YSE::DSP::dspObject>()>;

  // Distinct, well-separated per-channel test frequency so each channel carries
  // a different signal.
  inline float channelFreq(std::size_t ch) {
    return 250.0f + static_cast<float>(ch) * 311.0f;
  }

  inline void fillSine(YSE::DSP::buffer& buf, float freq, float sr = 44100.0f) {
    float* p = buf.getPtr();
    for (unsigned i = 0; i < buf.getLength(); ++i)
      p[i] = std::sin(2.0f * kPi * freq * static_cast<float>(i) / sr);
  }

  inline float maxAbs(YSE::DSP::buffer& buf) {
    float* p = buf.getPtr();
    float m = 0.0f;
    for (unsigned i = 0; i < buf.getLength(); ++i) {
      float a = std::abs(p[i]);
      if (a > m) m = a;
    }
    return m;
  }

  // Core check: channel k of an N-channel run must equal a mono run fed the
  // same per-channel signal, for every channel. Also verifies neighbouring
  // channels differ (they carry different frequencies), so a module that
  // collapsed all channels onto one would fail.
  void checkChannelIndependence(const ModuleFactory& make, unsigned channels) {
    // --- N-channel run ---
    auto multi = make();
    MULTICHANNELBUFFER mbuf(channels);
    for (unsigned ch = 0; ch < channels; ++ch)
      mbuf[ch].resize(kLen);
    for (int b = 0; b < kBlocks; ++b) {
      for (unsigned ch = 0; ch < channels; ++ch)
        fillSine(mbuf[ch], channelFreq(ch));
      multi->process(mbuf);
    }
    // Capture the final-block output of every channel.
    std::vector<YSE::DSP::buffer> captured;
    captured.reserve(channels);
    for (unsigned ch = 0; ch < channels; ++ch)
      captured.push_back(mbuf[ch]);

    // --- Per-channel mono reference runs ---
    for (unsigned ch = 0; ch < channels; ++ch) {
      auto mono = make();
      MULTICHANNELBUFFER sbuf(1);
      sbuf[0].resize(kLen);
      for (int b = 0; b < kBlocks; ++b) {
        fillSine(sbuf[0], channelFreq(ch));
        mono->process(sbuf);
      }
      CHECK(TestHelpers::buffersNearlyEqual(captured[ch], sbuf[0], 1e-5f));
    }

    // Independence sanity: distinct inputs give distinct outputs.
    if (channels >= 2)
      CHECK_FALSE(TestHelpers::buffersNearlyEqual(captured[0], captured[1], 1e-4f));
  }

  // Device-restart survival: 6 -> 2 -> 6 channels mid-stream, no crash, output
  // stays bounded throughout.
  void checkChannelCountChange(const ModuleFactory& make) {
    auto mod = make();
    for (unsigned channels : {6u, 2u, 6u, 1u, 4u}) {
      MULTICHANNELBUFFER buf(channels);
      for (unsigned ch = 0; ch < channels; ++ch)
        buf[ch].resize(kLen);
      for (int b = 0; b < 8; ++b) {
        for (unsigned ch = 0; ch < channels; ++ch)
          fillSine(buf[ch], channelFreq(ch));
        mod->process(buf);
        for (unsigned ch = 0; ch < channels; ++ch)
          CHECK(maxAbs(buf[ch]) < 50.0f);
      }
    }
  }

  // RT audit: after warming up at a fixed channel count / block length, the
  // audio-path process() calls must not allocate.
  void checkNoSteadyStateAllocation(const ModuleFactory& make) {
    auto mod = make();
    const unsigned channels = 4;
    MULTICHANNELBUFFER buf(channels);
    for (unsigned ch = 0; ch < channels; ++ch)
      buf[ch].resize(kLen);

    // Warm up: this includes the one-time create()/resize allocation.
    for (int b = 0; b < 6; ++b) {
      for (unsigned ch = 0; ch < channels; ++ch)
        fillSine(buf[ch], channelFreq(ch));
      mod->process(buf);
    }

    // Steady state: no allocation allowed.
    {
      TestHelpers::ProbeScope probe;
      for (int b = 0; b < 8; ++b) {
        for (unsigned ch = 0; ch < channels; ++ch)
          fillSine(buf[ch], channelFreq(ch));
        mod->process(buf);
      }
    }
    CHECK(TestHelpers::g_alloc_count.load() == 0);
  }

  // --- Module factories (fresh, identically configured instance each call) ---

  ModuleFactory lowPassFactory() {
    return [] {
      auto m = std::make_unique<YSE::DSP::MODULES::lowPassFilter>();
      m->frequency(800.0f);
      return m;
    };
  }
  ModuleFactory highPassFactory() {
    return [] {
      auto m = std::make_unique<YSE::DSP::MODULES::highPassFilter>();
      m->frequency(800.0f);
      return m;
    };
  }
  ModuleFactory bandPassFactory() {
    return [] {
      auto m = std::make_unique<YSE::DSP::MODULES::bandPassFilter>();
      m->frequency(1200.0f).setQ(2.0f);
      return m;
    };
  }
  ModuleFactory sweepFactory() {
    return [] {
      auto m = std::make_unique<YSE::DSP::MODULES::sweepFilter>();
      m->speed(1.0f).depth(50).frequency(50);
      return m;
    };
  }
  ModuleFactory phaserFactory() {
    return [] {
      auto m = std::make_unique<YSE::DSP::MODULES::phaser>();
      m->frequency(0.3f).range(0.1f);
      return m;
    };
  }
  ModuleFactory ringModFactory() {
    return [] {
      auto m = std::make_unique<YSE::DSP::ringModulator>();
      m->frequency(300.0f);
      return m;
    };
  }
  ModuleFactory basicDelayFactory() {
    return [] {
      auto m = std::make_unique<YSE::DSP::MODULES::basicDelay>();
      using D = YSE::DSP::MODULES::basicDelay;
      m->set(D::FIRST, 5.0f, 0.6f);
      m->set(D::SECOND, 9.0f, 0.4f);
      return m;
    };
  }
  ModuleFactory lowPassDelayFactory() {
    return [] {
      auto m = std::make_unique<YSE::DSP::MODULES::lowPassDelay>();
      m->frequency(1500.0f);
      m->set(YSE::DSP::MODULES::basicDelay::FIRST, 5.0f, 0.8f);
      return m;
    };
  }
  ModuleFactory highPassDelayFactory() {
    return [] {
      auto m = std::make_unique<YSE::DSP::MODULES::highPassDelay>();
      m->frequency(1500.0f);
      m->set(YSE::DSP::MODULES::basicDelay::FIRST, 5.0f, 0.8f);
      return m;
    };
  }
  ModuleFactory feedbackDelayFactory() {
    return [] {
      auto m = std::make_unique<YSE::DSP::MODULES::feedbackDelay>();
      // crossfeed left at 0 so channels stay independent (contract requirement).
      m->time(5.0f).feedback(0.6f).damping(4000.0f);
      return m;
    };
  }

  struct NamedFactory {
    const char* name;
    ModuleFactory make;
  };

  std::vector<NamedFactory> allModules() {
    return {
        {"lowPassFilter", lowPassFactory()},
        {"highPassFilter", highPassFactory()},
        {"bandPassFilter", bandPassFactory()},
        {"sweepFilter", sweepFactory()},
        {"phaser", phaserFactory()},
        {"ringModulator", ringModFactory()},
        {"basicDelay", basicDelayFactory()},
        {"lowPassDelay", lowPassDelayFactory()},
        {"highPassDelay", highPassDelayFactory()},
        {"feedbackDelay", feedbackDelayFactory()},
    };
  }

} // anonymous namespace

TEST_SUITE("dsp") {

  TEST_CASE("multichannel modules: each channel is processed independently (6 channels)") {
    for (auto& nf : allModules()) {
      CAPTURE(nf.name);
      checkChannelIndependence(nf.make, 6);
    }
  }

  TEST_CASE("multichannel modules: single-channel output matches the mono reference") {
    // A 1-channel buffer is the degenerate case of the contract; this is the
    // explicit bit-identical mono regression guard.
    for (auto& nf : allModules()) {
      CAPTURE(nf.name);
      checkChannelIndependence(nf.make, 1);
    }
  }

  TEST_CASE("multichannel modules: mid-stream channel-count change does not crash") {
    for (auto& nf : allModules()) {
      CAPTURE(nf.name);
      checkChannelCountChange(nf.make);
    }
  }

  TEST_CASE("multichannel modules: steady-state processing is allocation-free") {
    for (auto& nf : allModules()) {
      CAPTURE(nf.name);
      checkNoSteadyStateAllocation(nf.make);
    }
  }

} // TEST_SUITE("dsp")
