// Tests for the YSE::synth keyboard / pedal state machine, the controller and
// pitch-wheel / aftertouch op-set, and the onNoteEvent hook — issue #154,
// implementing §5 / §6 / §7 of docs/design/synth_core.md.
//
// These drive a SYNTH::implementationObject directly (no engine, no audio
// device): pushing control messages and calling the aggregate outputSource's
// process() renders real audio through the real voice + allocator, which is the
// end-to-end audio verification for these paths (same approach as the #153
// allocator tests in test_synth.cpp). A couple of cases also exercise the
// public YSE::synth interface through the live managers, gated on engineInit().

#include <doctest/doctest.h>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "yse.hpp"
#include "synth/dspVoice.hpp"
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

  messageObject wheelMsg(int channel, float value) {
    messageObject m;
    m.ID = YSE::SYNTH::PITCH_WHEEL;
    m.wheel.channel = channel;
    m.wheel.value = value;
    return m;
  }

  messageObject ccMsg(int channel, int number, float value) {
    messageObject m;
    m.ID = YSE::SYNTH::CONTROLLER;
    m.cc.channel = channel;
    m.cc.number = number;
    m.cc.value = value;
    return m;
  }

  messageObject aftertouchMsg(int channel, int note, float value) {
    messageObject m;
    m.ID = YSE::SYNTH::AFTERTOUCH;
    m.touch.channel = channel;
    m.touch.note = note;
    m.touch.value = value;
    return m;
  }

  messageObject sustainMsg(int channel, bool down) {
    messageObject m;
    m.ID = YSE::SYNTH::SUSTAIN;
    m.pedal.channel = channel;
    m.pedal.down = down;
    return m;
  }

  messageObject sostenutoMsg(int channel, bool down) {
    messageObject m;
    m.ID = YSE::SYNTH::SOSTENUTO;
    m.pedal.channel = channel;
    m.pedal.down = down;
    return m;
  }

  messageObject softPedalMsg(int channel, bool down) {
    messageObject m;
    m.ID = YSE::SYNTH::SOFTPEDAL;
    m.pedal.channel = channel;
    m.pedal.down = down;
    return m;
  }

  YSE::DSP::buffer& renderBlock(implementationObject& impl, SOUND_STATUS& intent) {
    impl.getOutputSource().process(intent);
    return impl.getOutputSource().samples[0];
  }

  // Render `blocks` blocks in the steady PLAYING state.
  void render(implementationObject& impl, int blocks) {
    SOUND_STATUS intent = YSE::SS_PLAYING;
    for (int i = 0; i < blocks; ++i)
      renderBlock(impl, intent);
  }

  float magAtHz(YSE::DSP::fft& f, unsigned N, float hz) {
    const float binHz = static_cast<float>(YSE::SAMPLERATE) / static_cast<float>(N);
    unsigned bin = static_cast<unsigned>(std::lround(hz / binHz));
    if (bin > N / 2) bin = N / 2;
    float re = f.getReal().getPtr()[bin];
    float im = f.getImaginary().getPtr()[bin];
    return re * re + im * im;
  }

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

  // A minimal voice that renders a DC level equal to whatever expressive input
  // is under test — lets a test read back per-voice atomic delivery as RMS.
  enum class ProbeInput { Aftertouch };

  class probeVoice : public dspVoice {
  public:
    explicit probeVoice(ProbeInput in = ProbeInput::Aftertouch) : dspVoice(1), input(in) {}
    dspVoice* clone() override {
      return new probeVoice(*this);
    }
    void process(SOUND_STATUS& intent) override {
      if (intent == YSE::SS_WANTSTOPLAY || intent == YSE::SS_WANTSTORESTART)
        intent = YSE::SS_PLAYING;
      else if (intent == YSE::SS_WANTSTOSTOP)
        intent = YSE::SS_STOPPED;

      float value = 0.f;
      if (intent == YSE::SS_PLAYING || intent == YSE::SS_PLAYING_FULL_VOLUME) {
        value = (input == ProbeInput::Aftertouch) ? getAftertouch() : 0.f;
      }
      for (UInt i = 0; i < samples.size(); i++)
        samples[i] = value;
    }

  protected:
    probeVoice(const probeVoice& o) : dspVoice(o), input(o.input) {}

  private:
    ProbeInput input;
  };

} // namespace

TEST_SUITE("synth") {

  // ─── sustain pedal ────────────────────────────────────────────────────────

  TEST_CASE("synth keyboard: sustain defers note-off until the pedal lifts") {
    implementationObject impl(nullptr);
    sineVoice proto;
    proto.attack(0.001f).decay(0.001f).sustain(1.f).release(0.02f);
    impl.addVoiceGroup(&proto, 4, 0, 0, 127);
    impl.setup();

    impl.sendMessage(sustainMsg(1, true)); // pedal down
    impl.sendMessage(noteOnMsg(1, 60, 1.f));
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    renderBlock(impl, intent);
    render(impl, 6);
    CHECK(impl.getSustain(1));
    CHECK(TestHelpers::measureRms(impl.getOutputSource().samples[0]) > 0.05f);

    // Key up while the pedal is down: the note must keep sounding.
    impl.sendMessage(noteOffMsg(1, 60));
    render(impl, 100);
    CHECK(TestHelpers::measureRms(impl.getOutputSource().samples[0]) > 0.05f);

    // Pedal up: now it releases.
    impl.sendMessage(sustainMsg(1, false));
    render(impl, 400);
    CHECK_FALSE(impl.getSustain(1));
    CHECK(TestHelpers::decaysToSilence(impl.getOutputSource().samples[0], 1e-4f));
  }

  // ─── sostenuto pedal: captures only currently-held notes ──────────────────

  TEST_CASE("synth keyboard: sostenuto sustains only the notes held when it engaged") {
    implementationObject impl(nullptr);
    sineVoice proto;
    proto.attack(0.001f).decay(0.001f).sustain(1.f).release(0.02f);
    impl.addVoiceGroup(&proto, 4, 0, 0, 127);
    impl.setup();

    // Hold note 60, then press sostenuto (captures 60), then play note 67.
    impl.sendMessage(noteOnMsg(1, 60, 1.f));
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    renderBlock(impl, intent);
    render(impl, 6);
    impl.sendMessage(sostenutoMsg(1, true)); // captures the held 60
    impl.sendMessage(noteOnMsg(1, 67, 1.f)); // played AFTER — not captured
    render(impl, 8);
    CHECK(impl.getSostenuto(1));

    // Release both keys: 60 is captured (sustains), 67 is not (releases).
    impl.sendMessage(noteOffMsg(1, 60));
    impl.sendMessage(noteOffMsg(1, 67));
    render(impl, 120);

    const unsigned N = 4096;
    YSE::DSP::buffer re(N), im(N);
    im = 0.f;
    captureSpectrum(impl, re, N);
    YSE::DSP::fft f;
    f(re, im);
    const float m60 = magAtHz(f, N, YSE::DSP::MidiToFreq(60.f));
    const float m67 = magAtHz(f, N, YSE::DSP::MidiToFreq(67.f));
    CHECK(m60 > 25.f * m67); // 60 still sounding, 67 gone

    // Sostenuto up: the captured note now releases.
    impl.sendMessage(sostenutoMsg(1, false));
    render(impl, 400);
    CHECK_FALSE(impl.getSostenuto(1));
    CHECK(TestHelpers::decaysToSilence(impl.getOutputSource().samples[0], 1e-4f));
  }

  // ─── sustain + sostenuto interaction ──────────────────────────────────────

  TEST_CASE("synth keyboard: a note held by both pedals releases only when both lift") {
    implementationObject impl(nullptr);
    sineVoice proto;
    proto.attack(0.001f).decay(0.001f).sustain(1.f).release(0.02f);
    impl.addVoiceGroup(&proto, 2, 0, 0, 127);
    impl.setup();

    impl.sendMessage(noteOnMsg(1, 60, 1.f));
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    renderBlock(impl, intent);
    render(impl, 6);
    impl.sendMessage(sustainMsg(1, true));
    impl.sendMessage(sostenutoMsg(1, true)); // 60 now claimed by BOTH
    impl.sendMessage(noteOffMsg(1, 60));
    render(impl, 60);
    CHECK(TestHelpers::measureRms(impl.getOutputSource().samples[0]) > 0.05f);

    // Lift sustain only — sostenuto still claims 60, so it keeps sounding.
    impl.sendMessage(sustainMsg(1, false));
    render(impl, 60);
    CHECK(TestHelpers::measureRms(impl.getOutputSource().samples[0]) > 0.05f);

    // Lift sostenuto — now nothing holds it, it releases.
    impl.sendMessage(sostenutoMsg(1, false));
    render(impl, 400);
    CHECK(TestHelpers::decaysToSilence(impl.getOutputSource().samples[0], 1e-4f));
  }

  // ─── soft pedal scales the velocity of new notes only ─────────────────────

  TEST_CASE("synth keyboard: the soft pedal lowers the level of notes started under it") {
    implementationObject impl(nullptr);
    sineVoice proto;
    proto.attack(0.001f).decay(0.001f).sustain(1.f).release(0.02f);
    impl.addVoiceGroup(&proto, 2, 0, 0, 127);
    impl.setup();

    // Baseline: full-velocity note.
    impl.sendMessage(noteOnMsg(1, 60, 1.f));
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    renderBlock(impl, intent);
    render(impl, 8);
    const float loud = TestHelpers::measureRms(impl.getOutputSource().samples[0]);
    impl.sendMessage(noteOffMsg(1, 60));
    render(impl, 400);

    // Same note, same velocity, but with the soft pedal down.
    impl.sendMessage(softPedalMsg(1, true));
    impl.sendMessage(noteOnMsg(1, 60, 1.f));
    render(impl, 8);
    const float soft = TestHelpers::measureRms(impl.getOutputSource().samples[0]);

    CHECK(impl.getSoftPedal(1));
    CHECK(soft < loud); // quieter…
    CHECK(soft > 0.5f * loud); // …but not silenced (≈0.7×)
  }

  // ─── pitch wheel bends a sounding voice (FFT) ─────────────────────────────

  TEST_CASE("synth keyboard: the pitch wheel measurably bends a sounding sine voice") {
    implementationObject impl(nullptr);
    sineVoice proto;
    proto.attack(0.001f).decay(0.001f).sustain(1.f).release(0.05f);
    impl.addVoiceGroup(&proto, 1, 0, 0, 127);
    impl.setup();

    impl.sendMessage(noteOnMsg(1, 69, 1.f)); // A4, 440 Hz
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    renderBlock(impl, intent);
    render(impl, 8);

    const unsigned N = 4096;
    const float a4 = YSE::DSP::MidiToFreq(69.f);
    const float bent = YSE::DSP::MidiToFreq(71.f); // +1.0 wheel → +2 semitones

    // Unbent: energy sits at A4, not at the +2-semitone target.
    {
      YSE::DSP::buffer re(N), im(N);
      im = 0.f;
      captureSpectrum(impl, re, N);
      YSE::DSP::fft f;
      f(re, im);
      CHECK(magAtHz(f, N, a4) > 25.f * magAtHz(f, N, bent));
    }

    // Bend the wheel fully up and let the new pitch settle.
    impl.sendMessage(wheelMsg(1, 1.f));
    render(impl, 8);
    CHECK(impl.getChannelPitchWheel(1) == doctest::Approx(1.f));

    // Bent: the peak has moved up to the +2-semitone frequency.
    {
      YSE::DSP::buffer re(N), im(N);
      im = 0.f;
      captureSpectrum(impl, re, N);
      YSE::DSP::fft f;
      f(re, im);
      CHECK(magAtHz(f, N, bent) > 25.f * magAtHz(f, N, a4));
    }
  }

  // ─── pitch wheel primes newly-started notes ───────────────────────────────

  TEST_CASE("synth keyboard: a note started while the wheel is bent begins in tune with it") {
    implementationObject impl(nullptr);
    sineVoice proto;
    proto.attack(0.001f).decay(0.001f).sustain(1.f).release(0.05f);
    impl.addVoiceGroup(&proto, 1, 0, 0, 127);
    impl.setup();

    impl.sendMessage(wheelMsg(1, 1.f)); // wheel up BEFORE any note
    impl.sendMessage(noteOnMsg(1, 69, 1.f));
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    renderBlock(impl, intent);
    render(impl, 8);

    const unsigned N = 4096;
    YSE::DSP::buffer re(N), im(N);
    im = 0.f;
    captureSpectrum(impl, re, N);
    YSE::DSP::fft f;
    f(re, im);
    const float a4 = YSE::DSP::MidiToFreq(69.f);
    const float bent = YSE::DSP::MidiToFreq(71.f);
    CHECK(magAtHz(f, N, bent) > 25.f * magAtHz(f, N, a4));
  }

  // ─── onNoteEvent rewrites the note before it sounds ───────────────────────

  namespace {
    // Captureless: transpose every note up an octave before it is allocated.
    void transposeUpOctave(bool /*noteOn*/, float* note, float* /*velocity*/) {
      *note += 12.f;
    }
  } // namespace

  TEST_CASE("synth keyboard: onNoteEvent transposes a note before it sounds") {
    implementationObject impl(nullptr);
    sineVoice proto;
    proto.attack(0.001f).decay(0.001f).sustain(1.f).release(0.05f);
    impl.addVoiceGroup(&proto, 1, 0, 0, 127);
    impl.setup();

    impl.setNoteCallback(&transposeUpOctave);
    impl.sendMessage(noteOnMsg(1, 60, 1.f)); // C4 → rewritten to C5
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    renderBlock(impl, intent);
    render(impl, 8);

    const unsigned N = 4096;
    YSE::DSP::buffer re(N), im(N);
    im = 0.f;
    captureSpectrum(impl, re, N);
    YSE::DSP::fft f;
    f(re, im);
    const float c4 = YSE::DSP::MidiToFreq(60.f);
    const float c5 = YSE::DSP::MidiToFreq(72.f);
    CHECK(magAtHz(f, N, c5) > 25.f * magAtHz(f, N, c4)); // sounds an octave up

    // The rewrite is symmetric: a note-off rewritten the same way releases it.
    impl.sendMessage(noteOffMsg(1, 60)); // → 72, matches the sounding voice
    render(impl, 400);
    CHECK(TestHelpers::decaysToSilence(impl.getOutputSource().samples[0], 1e-4f));
  }

  TEST_CASE("synth keyboard: clearing onNoteEvent restores pass-through") {
    implementationObject impl(nullptr);
    sineVoice proto;
    proto.attack(0.001f).decay(0.001f).sustain(1.f).release(0.05f);
    impl.addVoiceGroup(&proto, 1, 0, 0, 127);
    impl.setup();

    impl.setNoteCallback(&transposeUpOctave);
    impl.setNoteCallback(nullptr); // cleared → note passes through unmodified
    impl.sendMessage(noteOnMsg(1, 60, 1.f));
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    renderBlock(impl, intent);
    render(impl, 8);

    const unsigned N = 4096;
    YSE::DSP::buffer re(N), im(N);
    im = 0.f;
    captureSpectrum(impl, re, N);
    YSE::DSP::fft f;
    f(re, im);
    const float c4 = YSE::DSP::MidiToFreq(60.f);
    const float c5 = YSE::DSP::MidiToFreq(72.f);
    CHECK(magAtHz(f, N, c4) > 25.f * magAtHz(f, N, c5)); // sounds at C4
  }

  // ─── aftertouch reaches the sounding voice ────────────────────────────────

  TEST_CASE("synth keyboard: aftertouch is delivered per-note and channel-wide") {
    implementationObject impl(nullptr);
    probeVoice proto(ProbeInput::Aftertouch);
    impl.addVoiceGroup(&proto, 2, 0, 0, 127);
    impl.setup();

    impl.sendMessage(noteOnMsg(1, 60, 1.f));
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    renderBlock(impl, intent);
    render(impl, 2);
    CHECK(TestHelpers::measureRms(impl.getOutputSource().samples[0]) < 1e-3f); // no pressure yet

    // Per-note pressure reaches the voice sounding note 60. The aggregate is now
    // a per-voice-panned device-width bed (Route 2, issue #169), so measure the
    // note's amplitude across the whole bed — combinedRms is pan-invariant.
    impl.sendMessage(aftertouchMsg(1, 60, 0.8f));
    render(impl, 2);
    CHECK(impl.getChannelAftertouch(1) == doctest::Approx(0.8f));
    CHECK(TestHelpers::combinedRms(impl.getOutputSource().samples) ==
          doctest::Approx(0.8f).epsilon(0.02));

    // A wrong-note message does not touch it.
    impl.sendMessage(aftertouchMsg(1, 61, 0.2f));
    render(impl, 2);
    CHECK(TestHelpers::combinedRms(impl.getOutputSource().samples) ==
          doctest::Approx(0.8f).epsilon(0.02));

    // Channel-wide (note -1) reaches it.
    impl.sendMessage(aftertouchMsg(1, -1, 0.4f));
    render(impl, 2);
    CHECK(TestHelpers::combinedRms(impl.getOutputSource().samples) ==
          doctest::Approx(0.4f).epsilon(0.02));
  }

  // ─── controllers: pedal CCs intercepted, others stored ────────────────────

  TEST_CASE("synth keyboard: CC 64 acts as sustain; other CCs are stored per channel") {
    implementationObject impl(nullptr);
    sineVoice proto;
    proto.attack(0.001f).decay(0.001f).sustain(1.f).release(0.02f);
    impl.addVoiceGroup(&proto, 2, 0, 0, 127);
    impl.setup();

    // A non-pedal CC is stored as the channel's last value.
    impl.sendMessage(ccMsg(1, 20, 0.6f));
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    renderBlock(impl, intent);
    CHECK(impl.getChannelController(1, 20) == doctest::Approx(0.6f));

    // CC 64 IS the sustain pedal — routed, not stored as a plain controller.
    impl.sendMessage(ccMsg(1, 64, 1.f));
    impl.sendMessage(noteOnMsg(1, 60, 1.f));
    renderBlock(impl, intent);
    render(impl, 6);
    CHECK(impl.getSustain(1));
    impl.sendMessage(noteOffMsg(1, 60));
    render(impl, 80);
    CHECK(TestHelpers::measureRms(impl.getOutputSource().samples[0]) > 0.05f); // sustained

    impl.sendMessage(ccMsg(1, 64, 0.f)); // pedal up
    render(impl, 400);
    CHECK_FALSE(impl.getSustain(1));
    CHECK(TestHelpers::decaysToSilence(impl.getOutputSource().samples[0], 1e-4f));
  }

  // ─── real-time discipline on the keyboard paths ───────────────────────────

  TEST_CASE("synth keyboard: control ops do not allocate on the audio thread") {
    implementationObject impl(nullptr);
    sineVoice proto;
    proto.attack(0.002f).decay(0.002f).sustain(0.8f).release(0.05f);
    impl.addVoiceGroup(&proto, 8, 0, 0, 127);
    impl.setup();

    // Warm up.
    impl.sendMessage(noteOnMsg(1, 60, 1.f));
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    renderBlock(impl, intent);
    render(impl, 8);

    {
      TestHelpers::ProbeScope probe;
      impl.sendMessage(wheelMsg(1, 0.5f));
      impl.sendMessage(aftertouchMsg(1, 60, 0.7f));
      impl.sendMessage(aftertouchMsg(1, -1, 0.3f));
      impl.sendMessage(ccMsg(1, 21, 0.9f));
      impl.sendMessage(sustainMsg(1, true));
      impl.sendMessage(noteOnMsg(1, 64, 1.f));
      impl.sendMessage(noteOffMsg(1, 64));
      impl.sendMessage(sostenutoMsg(1, true));
      impl.sendMessage(noteOnMsg(1, 67, 1.f));
      impl.sendMessage(softPedalMsg(1, true));
      impl.sendMessage(noteOnMsg(1, 72, 1.f));
      impl.sendMessage(sustainMsg(1, false));
      impl.sendMessage(sostenutoMsg(1, false));
      impl.sendMessage(softPedalMsg(1, false));
      render(impl, 40);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  // ─── public interface smoke test (needs a live engine) ────────────────────

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

  TEST_CASE("synth keyboard: the public interface routes pedals, wheel and onNoteEvent") {
    if (!TestHelpers::engineInit()) return;
    {
      sineVoice proto;
      proto.attack(0.005f).release(0.1f);
      YSE::synth syn;
      syn.create().addVoices(proto, 8);
      YSE::sound snd;
      snd.create(syn);
      drainSynth();
      CHECK(syn.getNumVoices() == 8);
      snd.play();

      // Exercise the whole control surface; the point is that none of these
      // crash or block and the chain returns *this.
      syn.onNoteEvent(&transposeUpOctave);
      syn.sustain(1, true).softPedal(1, true);
      syn.noteOn(1, 60, 0.9f);
      syn.pitchWheel(1, 0.5f);
      syn.aftertouch(1, 72, 0.6f); // note is transposed to 72 by the hook
      syn.controller(1, 20, 0.5f);
      drainSynth();
      syn.noteOff(1, 60);
      syn.sustain(1, false);
      syn.sostenuto(1, true).sostenuto(1, false);
      syn.allNotesOff();
      syn.onNoteEvent(nullptr);
      snd.stop();
      drainSynth();
    }
    drainSynth();
    CHECK(true);
  }

} // TEST_SUITE("synth")
