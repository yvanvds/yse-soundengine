// Tests for the YSE::synth object, voice allocator, stealing policy and
// manager/lifecycle — issue #153, implementing §2 / §4 / §8 / §9 of
// docs/design/synth_core.md.
//
// Two layers:
//   * DSP / allocator tests drive a SYNTH::implementationObject directly (no
//     engine, no audio device needed — SAMPLERATE is initialised to 44100 by
//     the device translation unit at static-init time). They exercise the
//     allocator, stealing, click-free declick and mixing by pushing note
//     messages and calling the aggregate outputSource's process().
//   * Manager / lifecycle tests drive the public YSE::synth + YSE::sound API
//     through the real managers (engineInit; skipped on CI hosts without an
//     audio device, like the sibling sound/reverb manager tests).

#include <doctest/doctest.h>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "yse.hpp"
#include "synth/sineVoice.hpp"
#include "synth/synthImplementation.h"
#include "synth/synthInterface.hpp"
#include "synth/synthManager.h"
#include "synth/synthMessage.h"
#include "sound/soundInterface.hpp"
#include "sound/soundManager.h"
#include "dsp/fourier/fft.hpp"
#include "internal/time.h"
#include "support/audio_helpers.hpp"
#include "support/alloc_probe.hpp"
#include "support/null_device.hpp"

using namespace std::chrono_literals;

namespace {

  using YSE::SOUND_STATUS;
  using YSE::SYNTH::dspVoice;
  using YSE::SYNTH::implementationObject;
  using YSE::SYNTH::messageObject;
  using YSE::SYNTH::sineVoice;

  messageObject noteOnMsg(int channel, int note, float velocity) {
    messageObject m;
    m.ID = YSE::SYNTH::NOTE_ON;
    m.noteOn.channel = channel;
    m.noteOn.note = note;
    m.noteOn.velocity = velocity;
    return m;
  }

  messageObject noteOffMsg(int channel, int note) {
    messageObject m;
    m.ID = YSE::SYNTH::NOTE_OFF;
    m.noteOff.channel = channel;
    m.noteOff.note = note;
    m.noteOff.velocity = 0.f;
    return m;
  }

  messageObject allNotesOffMsg(int channel) {
    messageObject m;
    m.ID = YSE::SYNTH::ALL_NOTES_OFF;
    m.allOff.channel = channel;
    return m;
  }

  // Render one block of the aggregate and return its (mono) output buffer.
  YSE::DSP::buffer& renderBlock(implementationObject& impl, SOUND_STATUS& intent) {
    impl.getOutputSource().process(intent);
    return impl.getOutputSource().samples[0];
  }

  // Squared FFT magnitude at the bin nearest `hz`.
  float magAtHz(YSE::DSP::fft& f, unsigned N, float hz) {
    const float binHz = static_cast<float>(YSE::SAMPLERATE) / static_cast<float>(N);
    unsigned bin = static_cast<unsigned>(hz / binHz + 0.5f);
    if (bin > N / 2) bin = N / 2;
    float re = f.getReal().getPtr()[bin];
    float im = f.getImaginary().getPtr()[bin];
    return re * re + im * im;
  }

  // Capture N samples of steady output into `re`, driving the synth in PLAYING.
  void captureSpectrum(implementationObject& impl, YSE::DSP::buffer& re, unsigned N) {
    re = 0.f;
    const unsigned block = YSE::STANDARD_BUFFERSIZE;
    unsigned filled = 0;
    SOUND_STATUS intent = YSE::SS_PLAYING;
    while (filled + block <= N) {
      YSE::DSP::buffer& out = renderBlock(impl, intent);
      re.copyFrom(out, 0, filled, block);
      filled += block;
    }
  }

} // namespace

TEST_SUITE("synth") {

  // ─── allocator: polyphonic chord ──────────────────────────────────────────

  TEST_CASE("synth allocator: a chord renders N distinct FFT peaks") {
    implementationObject impl(nullptr);
    sineVoice proto;
    proto.attack(0.001f).decay(0.001f).sustain(1.f).release(0.05f);
    impl.addVoiceGroup(&proto, 8, /*omni*/ 0, 0, 127);
    impl.setup();
    REQUIRE(impl.getNumVoices() == 8);

    // Sound a C-major triad.
    impl.sendMessage(noteOnMsg(1, 60, 1.f)); // C4  ~261.6 Hz
    impl.sendMessage(noteOnMsg(1, 64, 1.f)); // E4  ~329.6 Hz
    impl.sendMessage(noteOnMsg(1, 67, 1.f)); // G4  ~392.0 Hz

    // First block drains the inbox (allocates) and attacks; settle a few more.
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    renderBlock(impl, intent);
    CHECK(intent == YSE::SS_PLAYING);
    for (int i = 0; i < 8; ++i)
      renderBlock(impl, intent);

    const unsigned N = 4096;
    YSE::DSP::buffer re(N), im(N);
    im = 0.f;
    captureSpectrum(impl, re, N);

    YSE::DSP::fft f;
    f(re, im);

    const float c4 = YSE::DSP::MidiToFreq(60.f);
    const float e4 = YSE::DSP::MidiToFreq(64.f);
    const float g4 = YSE::DSP::MidiToFreq(67.f);
    const float gap = magAtHz(f, N, 300.f); // between C4 and E4 — no note here

    // Every played note is a strong peak, each far above the empty gap bin.
    CHECK(magAtHz(f, N, c4) > 25.f * gap);
    CHECK(magAtHz(f, N, e4) > 25.f * gap);
    CHECK(magAtHz(f, N, g4) > 25.f * gap);
  }

  // ─── allocator: free-slot reuse ───────────────────────────────────────────

  TEST_CASE("synth allocator: a released voice is reused for the next note") {
    implementationObject impl(nullptr);
    sineVoice proto;
    proto.attack(0.001f).decay(0.001f).sustain(1.f).release(0.02f);
    impl.addVoiceGroup(&proto, 1, 0, 0, 127); // single voice
    impl.setup();

    // Play then fully release note 60.
    impl.sendMessage(noteOnMsg(1, 60, 1.f));
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    renderBlock(impl, intent);
    for (int i = 0; i < 5; ++i)
      renderBlock(impl, intent);
    impl.sendMessage(noteOffMsg(1, 60));
    for (int i = 0; i < 400; ++i)
      renderBlock(impl, intent);
    CHECK(TestHelpers::decaysToSilence(impl.getOutputSource().samples[0], 1e-4f));

    // The freed single voice must accept a new, different note.
    impl.sendMessage(noteOnMsg(1, 72, 1.f)); // C5 ~523 Hz
    renderBlock(impl, intent);
    for (int i = 0; i < 8; ++i)
      renderBlock(impl, intent);

    const unsigned N = 4096;
    YSE::DSP::buffer re(N), im(N);
    im = 0.f;
    captureSpectrum(impl, re, N);
    YSE::DSP::fft f;
    f(re, im);
    CHECK(magAtHz(f, N, YSE::DSP::MidiToFreq(72.f)) > 25.f * magAtHz(f, N, 300.f));
  }

  // ─── stealing: correctness ────────────────────────────────────────────────

  TEST_CASE("synth stealing: exhausting polyphony steals the oldest voice") {
    implementationObject impl(nullptr);
    sineVoice proto;
    proto.attack(0.001f).decay(0.001f).sustain(1.f).release(0.05f);
    impl.addVoiceGroup(&proto, 2, 0, 0, 127); // only 2 voices
    impl.setup();

    // Fill both voices: 60 (oldest) then 64.
    impl.sendMessage(noteOnMsg(1, 60, 1.f));
    impl.sendMessage(noteOnMsg(1, 64, 1.f));
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    renderBlock(impl, intent);
    for (int i = 0; i < 10; ++i)
      renderBlock(impl, intent);

    // Third note with no free voice → steals the oldest overall (60).
    impl.sendMessage(noteOnMsg(1, 67, 1.f));
    for (int i = 0; i < 300; ++i) // let the steal fade + new attack settle
      renderBlock(impl, intent);

    const unsigned N = 4096;
    YSE::DSP::buffer re(N), im(N);
    im = 0.f;
    captureSpectrum(impl, re, N);
    YSE::DSP::fft f;
    f(re, im);

    const float m60 = magAtHz(f, N, YSE::DSP::MidiToFreq(60.f));
    const float m64 = magAtHz(f, N, YSE::DSP::MidiToFreq(64.f));
    const float m67 = magAtHz(f, N, YSE::DSP::MidiToFreq(67.f));

    // 64 survived, 67 replaced 60, and 60 is gone.
    CHECK(m64 > 25.f * m60);
    CHECK(m67 > 25.f * m60);
  }

  // ─── stealing: click-free handoff ─────────────────────────────────────────

  TEST_CASE("synth stealing: the stolen-voice handoff is click-free") {
    implementationObject impl(nullptr);
    sineVoice proto;
    proto.attack(0.002f).decay(0.002f).sustain(1.f).release(0.2f);
    impl.addVoiceGroup(&proto, 2, 0, 0, 127);
    impl.setup();

    impl.sendMessage(noteOnMsg(1, 60, 1.f));
    impl.sendMessage(noteOnMsg(1, 64, 1.f));
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    renderBlock(impl, intent);
    for (int i = 0; i < 20; ++i)
      renderBlock(impl, intent);

    // Concatenate a stretch of output spanning the steal transition.
    std::vector<float> stream;
    const unsigned block = YSE::STANDARD_BUFFERSIZE;
    auto grab = [&](int blocks) {
      for (int b = 0; b < blocks; ++b) {
        YSE::DSP::buffer& out = renderBlock(impl, intent);
        float* p = out.getPtr();
        for (unsigned s = 0; s < block; ++s)
          stream.push_back(p[s]);
      }
    };

    grab(4); // baseline (2 voices sounding)
    impl.sendMessage(noteOnMsg(1, 67, 1.f)); // trigger the steal
    grab(16); // across the ~5 ms fade + re-attack

    // No abrupt discontinuity: a hard voice cut would jump by ~1 or more between
    // adjacent samples. The engine's declick keeps every step small (the signal
    // itself only slews a fraction of a unit per sample at these pitches).
    float maxDelta = 0.f;
    for (size_t i = 1; i < stream.size(); ++i)
      maxDelta = std::max(maxDelta, std::fabs(stream[i] - stream[i - 1]));
    CHECK(maxDelta < 0.5f);
  }

  // ─── all notes off ────────────────────────────────────────────────────────

  TEST_CASE("synth: allNotesOff releases every held voice") {
    implementationObject impl(nullptr);
    sineVoice proto;
    proto.attack(0.001f).decay(0.001f).sustain(1.f).release(0.02f);
    impl.addVoiceGroup(&proto, 8, 0, 0, 127);
    impl.setup();

    for (int n = 60; n < 68; ++n)
      impl.sendMessage(noteOnMsg(1, n, 1.f));
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    renderBlock(impl, intent);
    for (int i = 0; i < 5; ++i)
      renderBlock(impl, intent);
    CHECK(TestHelpers::measureRms(impl.getOutputSource().samples[0]) > 0.05f);

    impl.sendMessage(allNotesOffMsg(0)); // 0 = all channels
    for (int i = 0; i < 400; ++i)
      renderBlock(impl, intent);
    CHECK(TestHelpers::decaysToSilence(impl.getOutputSource().samples[0], 1e-4f));
  }

  // ─── channel / range routing (voice groups) ───────────────────────────────

  TEST_CASE("synth groups: a split keyboard routes notes to the matching group") {
    implementationObject impl(nullptr);
    sineVoice bass;
    bass.attack(0.001f).decay(0.001f).sustain(1.f).release(0.02f);
    sineVoice lead;
    lead.attack(0.001f).decay(0.001f).sustain(1.f).release(0.02f);
    impl.addVoiceGroup(&bass, 2, 1, 0, 47); // channel 1, low keys
    impl.addVoiceGroup(&lead, 2, 1, 48, 127); // channel 1, high keys
    impl.setup();
    CHECK(impl.getNumVoices() == 4);

    // A low note (40) sounds only in the bass group; a high note (72) only in
    // the lead group. Both together must produce their two distinct pitches.
    impl.sendMessage(noteOnMsg(1, 40, 1.f));
    impl.sendMessage(noteOnMsg(1, 72, 1.f));
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    renderBlock(impl, intent);
    for (int i = 0; i < 8; ++i)
      renderBlock(impl, intent);

    const unsigned N = 4096;
    YSE::DSP::buffer re(N), im(N);
    im = 0.f;
    captureSpectrum(impl, re, N);
    YSE::DSP::fft f;
    f(re, im);
    CHECK(magAtHz(f, N, YSE::DSP::MidiToFreq(40.f)) > 25.f * magAtHz(f, N, 1500.f));
    CHECK(magAtHz(f, N, YSE::DSP::MidiToFreq(72.f)) > 25.f * magAtHz(f, N, 1500.f));
  }

  // ─── note routed outside every range is dropped ───────────────────────────

  TEST_CASE("synth groups: a note outside every group range is silently dropped") {
    implementationObject impl(nullptr);
    sineVoice proto;
    proto.attack(0.001f).decay(0.001f).sustain(1.f).release(0.02f);
    impl.addVoiceGroup(&proto, 4, 1, 48, 72); // only C3..C5 on channel 1
    impl.setup();

    impl.sendMessage(noteOnMsg(1, 100, 1.f)); // above range → no voice
    impl.sendMessage(noteOnMsg(2, 60, 1.f)); // wrong channel → no voice
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    for (int i = 0; i < 10; ++i)
      renderBlock(impl, intent);
    CHECK(TestHelpers::decaysToSilence(impl.getOutputSource().samples[0], 1e-4f));
  }

  // ─── real-time discipline ─────────────────────────────────────────────────

  TEST_CASE("synth render: process() does not allocate after warm-up") {
    implementationObject impl(nullptr);
    sineVoice proto;
    proto.attack(0.002f).decay(0.002f).sustain(0.8f).release(0.05f);
    impl.addVoiceGroup(&proto, 8, 0, 0, 127);
    impl.setup();

    // Warm up every path: allocate, steal, release, free.
    for (int n = 60; n < 68; ++n)
      impl.sendMessage(noteOnMsg(1, n, 1.f));
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    renderBlock(impl, intent);
    for (int i = 0; i < 20; ++i)
      renderBlock(impl, intent);
    impl.sendMessage(noteOnMsg(1, 70, 1.f)); // steal
    for (int i = 0; i < 40; ++i)
      renderBlock(impl, intent);

    {
      TestHelpers::ProbeScope probe;
      impl.sendMessage(noteOnMsg(1, 71, 1.f)); // another steal
      impl.sendMessage(noteOffMsg(1, 61));
      for (int i = 0; i < 60; ++i)
        renderBlock(impl, intent);
      impl.sendMessage(allNotesOffMsg(0));
      for (int i = 0; i < 400; ++i)
        renderBlock(impl, intent);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  // ─── manager / lifecycle (needs a live engine) ────────────────────────────

  namespace {
    void drainSynth(int n = 16) {
      for (int i = 0; i < n; ++i) {
        YSE::INTERNAL::Time().update();
        YSE::SOUND::Manager().update();
        YSE::SYNTH::Manager().update();
        std::this_thread::sleep_for(5ms);
      }
    }
  } // namespace

  TEST_CASE("synth lifecycle: create -> addVoices -> attach reaches READY, then releases") {
    if (!TestHelpers::engineInit()) return;
    {
      sineVoice proto;
      proto.attack(0.005f).release(0.1f);
      YSE::synth syn;
      syn.create().addVoices(proto, 8);
      YSE::sound snd;
      snd.create(syn); // attaches the synth's aggregate behind a positioned sound
      drainSynth();
      CHECK(syn.getNumVoices() == 8);
      CHECK(YSE::SYNTH::Manager().empty() == false);
      snd.play();
      syn.noteOn(1, 60, 0.9f);
      drainSynth();
      syn.noteOff(1, 60);
      snd.stop();
      drainSynth();
      // snd (declared last) destructs first, before syn — the required order.
    }
    drainSynth();
    CHECK(true); // no crash through the full create/attach/release/delete cycle
  }

  TEST_CASE("synth lifecycle: rapid create/destroy under a note storm") {
    if (!TestHelpers::engineInit()) return;
    for (int round = 0; round < 3; ++round) {
      sineVoice proto;
      proto.attack(0.002f).release(0.05f);
      YSE::synth syn;
      syn.create().addVoices(proto, 16);
      YSE::sound snd;
      snd.create(syn);
      drainSynth(6);
      snd.play();
      for (int i = 0; i < 200; ++i) {
        syn.noteOn(1, 48 + (i % 24), 0.8f);
        if (i % 3 == 0) syn.noteOff(1, 48 + (i % 24));
      }
      drainSynth(4);
      syn.allNotesOff();
      snd.stop();
      drainSynth(4);
    }
    drainSynth(8);
    CHECK(true);
  }

} // TEST_SUITE("synth")
