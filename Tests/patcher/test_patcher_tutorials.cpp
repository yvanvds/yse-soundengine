// The patcher tutorials, compiled and run (issue #883).
//
// The code the tutorials under documentation/source/tutorials/ show is the code
// in this file: each page pulls its snippets out of here with `literalinclude`
// between the `// tutorial:<name>:begin` / `:end` markers. So a tutorial whose
// patch stops building, or builds but stops making the sound the page promises,
// fails this suite instead of rotting silently. Keep the markers, and keep the
// code between them written for a reader: it is what the page shows.
//
// Each case renders real audio through `DSP::patcherInsert`, the same rig
// test_patcher_subpatcher_signal.cpp uses, so what is asserted is what a
// listener would hear rather than the state of some object along the way.
//
// Tutorial 05 walks through Demo13/Demo14 instead; its case loads the patch file
// those demos share (TestResources/patcher.yap), which is desktop-only because
// it reads the source tree.
//
// No audio device required. The MIDI tutorial injects note messages into the
// input hub, as test_patcher_midiin.cpp does, so it needs no hardware either.

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "dsp/buffer.hpp"
#include "dsp/patcherInsert.hpp"
#include "headers/constants.hpp"
#include "headers/defines.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"

#if YSE_ENABLE_MIDI_DEVICE
#include "midi/midiInHub.h"
#endif

namespace {

  // ─── tutorial code ──────────────────────────────────────────────────────────

  // tutorial:step-sequencer:begin
  struct StepSequencer {
    YSE::pHandle* metro;
    YSE::pHandle* counter;
    YSE::pHandle* pattern;
  };

  StepSequencer BuildStepSequencer(YSE::patcher& patch) {
    // The clock: a bang every 125 ms, a sixteenth note at 120 BPM.
    YSE::pHandle* run = patch.CreateObject(".r", "run"); // 1 starts, 0 stops
    YSE::pHandle* rate = patch.CreateObject(".r", "rate"); // step length in ms
    YSE::pHandle* metro = patch.CreateObject(".metro", "125");
    patch.Connect(run, 0, metro, 0);
    patch.Connect(rate, 0, metro, 1);

    // The step number. .counter adds its step before it sends, so it counts
    // 1, 2, 3 ...; one less, folded into 0-7, is the step.
    YSE::pHandle* counter = patch.CreateObject(".counter");
    YSE::pHandle* minus = patch.CreateObject(".-", "1");
    YSE::pHandle* wrap = patch.CreateObject(".%", "8");
    patch.Connect(metro, 0, counter, 0);
    patch.Connect(counter, 0, minus, 0);
    patch.Connect(minus, 0, wrap, 0);

    // The pattern: a step number goes in, the note stored for it comes out.
    YSE::pHandle* pattern = patch.CreateObject(".coll", "pattern");
    YSE::pHandle* edit = patch.CreateObject(".r", "edit"); // "<step> <note>" rewrites a step
    patch.Connect(wrap, 0, pattern, 0);
    patch.Connect(edit, 0, pattern, 0);

    // The sound: a filtered sawtooth at the note's pitch, on both channels.
    YSE::pHandle* mtof = patch.CreateObject(".mtof");
    YSE::pHandle* osc = patch.CreateObject("~saw");
    YSE::pHandle* filter = patch.CreateObject("~lp", "1200");
    YSE::pHandle* level = patch.CreateObject("~*", "0.2");
    YSE::pHandle* dac = patch.CreateObject("~dac");
    patch.Connect(pattern, 0, mtof, 0);
    patch.Connect(mtof, 0, osc, 0);
    patch.Connect(osc, 0, filter, 0);
    patch.Connect(filter, 0, level, 0);
    patch.Connect(level, 0, dac, 0);
    patch.Connect(level, 0, dac, 1);

    return {metro, counter, pattern};
  }
  // tutorial:step-sequencer:end

  // tutorial:voice:begin
  // One voice: the list "pitch velocity" in, audio out. Velocity 0 releases it.
  YSE::pHandle* CreateVoice(YSE::patcher& patch) {
    YSE::pHandle* voice = patch.CreateObject("patcher");

    YSE::pHandle* in = patch.CreateObject(".inlet", "0");
    YSE::pHandle* split = patch.CreateObject(".unpack", "0 0"); // pitch, velocity
    YSE::pHandle* mtof = patch.CreateObject(".mtof");
    YSE::pHandle* osc = patch.CreateObject("~saw");
    YSE::pHandle* scale = patch.CreateObject("./", "127"); // velocity to 0-1
    YSE::pHandle* env = patch.CreateObject("~line", "0 20"); // 20 ms ramps
    YSE::pHandle* amp = patch.CreateObject("~*");
    YSE::pHandle* out = patch.CreateObject("~outlet", "0");

    for (YSE::pHandle* h : {in, split, mtof, osc, scale, env, amp, out}) {
      patch.SetContainer(h, voice);
    }

    patch.Connect(in, 0, split, 0);
    patch.Connect(split, 0, mtof, 0); // pitch
    patch.Connect(mtof, 0, osc, 0);
    patch.Connect(split, 1, scale, 0); // velocity
    patch.Connect(scale, 0, env, 0);
    patch.Connect(osc, 0, amp, 0);
    patch.Connect(env, 0, amp, 1);
    patch.Connect(amp, 0, out, 0);
    return voice;
  }
  // tutorial:voice:end

  // tutorial:voice-parent:begin
  YSE::pHandle* BuildMonoSynth(YSE::patcher& patch) {
    YSE::pHandle* note = patch.CreateObject(".r", "note");
    YSE::pHandle* voice = CreateVoice(patch);
    YSE::pHandle* dac = patch.CreateObject("~dac");

    patch.Connect(note, 0, voice, 0); // lands on the voice's .inlet 0
    patch.Connect(voice, 0, dac, 0); // leaves from its ~outlet 0
    patch.Connect(voice, 0, dac, 1);
    return voice;
  }
  // tutorial:voice-parent:end

#if YSE_ENABLE_MIDI_DEVICE
  // tutorial:midi-synth:begin
  // A four-voice synth played from MIDI input `port` (an index into
  // YSE::System().getMidiInDeviceName()). Uses CreateVoice() from the voice
  // tutorial.
  void BuildMidiSynth(YSE::patcher& patch, const std::string& port) {
    YSE::pHandle* keys = patch.CreateObject(".notein", port); // pitch, velocity, channel
    YSE::pHandle* alloc = patch.CreateObject(".poly", "4 1"); // 4 voices, steal the oldest
    YSE::pHandle* pack = patch.CreateObject(".pack", "0 0 0"); // "voice pitch velocity"
    YSE::pHandle* route = patch.CreateObject(".route", "1 2 3 4");

    patch.Connect(keys, 0, alloc, 0); // pitch
    patch.Connect(keys, 1, alloc, 1); // velocity
    patch.Connect(alloc, 0, pack, 0); // voice number: sent last, so it is the hot one
    patch.Connect(alloc, 1, pack, 1); // pitch
    patch.Connect(alloc, 2, pack, 2); // velocity
    patch.Connect(pack, 0, route, 0);

    // Voice n gets what .route leaves of "n pitch velocity": "pitch velocity".
    // The voices are summed with a chain of ~+, because a signal inlet takes
    // one cord.
    YSE::pHandle* mix = nullptr;
    for (int n = 0; n < 4; n++) {
      YSE::pHandle* voice = CreateVoice(patch);
      patch.Connect(route, n, voice, 0);
      if (mix == nullptr) {
        mix = voice;
      } else {
        YSE::pHandle* sum = patch.CreateObject("~+");
        patch.Connect(mix, 0, sum, 0);
        patch.Connect(voice, 0, sum, 1);
        mix = sum;
      }
    }

    YSE::pHandle* level = patch.CreateObject("~*", "0.25");
    YSE::pHandle* dac = patch.CreateObject("~dac");
    patch.Connect(mix, 0, level, 0);
    patch.Connect(level, 0, dac, 0);
    patch.Connect(level, 0, dac, 1);
  }
  // tutorial:midi-synth:end
#endif

  // tutorial:presets:begin
  struct PresetSynth {
    YSE::pHandle* note;
    YSE::pHandle* cutoff;
    YSE::pHandle* level;
    YSE::pHandle* presets;
  };

  PresetSynth BuildPresetSynth(YSE::patcher& patch) {
    // Three controls. A .f sends its value on whenever it is set.
    YSE::pHandle* note = patch.CreateObject(".f", "60");
    YSE::pHandle* cutoff = patch.CreateObject(".f", "1000");
    YSE::pHandle* level = patch.CreateObject(".f", "0.5");

    YSE::pHandle* mtof = patch.CreateObject(".mtof");
    YSE::pHandle* osc = patch.CreateObject("~saw");
    YSE::pHandle* filter = patch.CreateObject("~lp", "1000");
    YSE::pHandle* amp = patch.CreateObject("~*", "0.5");
    YSE::pHandle* dac = patch.CreateObject("~dac");
    patch.Connect(note, 0, mtof, 0);
    patch.Connect(mtof, 0, osc, 0);
    patch.Connect(osc, 0, filter, 0);
    patch.Connect(cutoff, 0, filter, 1);
    patch.Connect(filter, 0, amp, 0);
    patch.Connect(level, 0, amp, 1);
    patch.Connect(amp, 0, dac, 0);
    patch.Connect(amp, 0, dac, 1);

    // Eight preset slots. The .loadmess recalls slot 0 whenever a saved copy
    // of this patch is loaded.
    YSE::pHandle* presets = patch.CreateObject(".preset", "8");
    YSE::pHandle* startup = patch.CreateObject(".loadmess", "0");
    patch.Connect(startup, 0, presets, 0);

    return {note, cutoff, level, presets};
  }
  // tutorial:presets:end

  // ─── test rig ───────────────────────────────────────────────────────────────

  // Renders `blocks` blocks of a patcher and keeps channel 0 of every one of
  // them, so a case can measure a stretch of audio longer than a block.
  struct Renderer {
    YSE::DSP::patcherInsert insert;
    MULTICHANNELBUFFER io;

    Renderer(YSE::patcher& patch, int channels) : insert(patch) {
      io.resize(channels);
      for (auto& channel : io)
        channel.resize(YSE::STANDARD_BUFFERSIZE);
    }

    std::vector<float> Render(int blocks) {
      std::vector<float> out;
      out.reserve(static_cast<size_t>(blocks) * YSE::STANDARD_BUFFERSIZE);
      for (int b = 0; b < blocks; b++) {
        for (auto& channel : io)
          channel = 0.f;
        insert.process(io);
        const float* p = io[0].getPtr();
        out.insert(out.end(), p, p + io[0].getLength());
      }
      return out;
    }
  };

  float Rms(const std::vector<float>& samples) {
    if (samples.empty()) return 0.f;
    double sum = 0.0;
    for (float s : samples)
      sum += static_cast<double>(s) * s;
    return static_cast<float>(std::sqrt(sum / static_cast<double>(samples.size())));
  }

  // The frequency of a waveform that crosses zero twice per period, which a
  // sine and a sawtooth both do.
  float Frequency(const std::vector<float>& samples) {
    int crossings = 0;
    for (size_t i = 1; i < samples.size(); i++) {
      if ((samples[i - 1] < 0.f) != (samples[i] < 0.f)) crossings++;
    }
    const float seconds = static_cast<float>(samples.size()) / static_cast<float>(YSE::SAMPLERATE);
    return static_cast<float>(crossings) / 2.f / seconds;
  }

  float Mtof(float note) {
    return 440.f * std::pow(2.f, (note - 69.f) / 12.f);
  }

  float GuiFloat(YSE::pHandle* h) {
    return std::stof(h->GetGuiValue());
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── tutorial 05: the patch Demo13 saves and Demo14 loads ───────────────────

#ifdef YSE_REPO_ROOT
  TEST_CASE("tutorials: 05 the bundled patcher.yap initialises itself and plays (#883)") {
    // Demo14's fallback file. The tutorial's claim is that the patch carries
    // its own starting values in .loadmess objects, so a host loads it and
    // hears it without sending anything.
    std::ifstream in(YSE_REPO_ROOT "/TestResources/patcher.yap");
    REQUIRE(in.good());
    const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    YSE::patcher patch;
    patch.create(1);
    patch.ParseJSON(json);
    REQUIRE(patch.Objects() == 13);

    // One second covers four periods of the 4 Hz tremolo. A sine times a sine
    // at gain 0.5 has an RMS of 0.5 * 0.5: 0.25. Without the .loadmess on the
    // volume, the ~* would still be at its default of 1 and read 0.5; without
    // the one on the LFO it would sit at 0 Hz and read silence.
    Renderer render(patch, 1);
    const int second = static_cast<int>(YSE::SAMPLERATE / YSE::STANDARD_BUFFERSIZE);
    const float rms = Rms(render.Render(second));
    CHECK(rms > 0.2f);
    CHECK(rms < 0.3f);

    // The receivers still drive it, as the demo's hotkeys do.
    CHECK(patch.PassData(1.0f, "volume"));
    const float louder = Rms(render.Render(second));
    CHECK(louder > 0.4f);
    CHECK(louder < 0.6f);

    CHECK(patch.PassData(0.0f, "volume"));
    render.Render(1);
    CHECK(Rms(render.Render(4)) < 1e-6f);
  }
#endif

  // ─── tutorial 11: step sequencer ────────────────────────────────────────────

  TEST_CASE("tutorials: 11 the step sequencer plays its pattern in order (#883)") {
    YSE::patcher patch;
    patch.create(2);
    StepSequencer seq = BuildStepSequencer(patch);
    REQUIRE(seq.metro != nullptr);
    REQUIRE(seq.counter != nullptr);
    REQUIRE(seq.pattern != nullptr);

    // tutorial:step-sequencer-fill:begin
    const int notes[8] = {48, 55, 60, 63, 48, 58, 60, 67};
    for (int step = 0; step < 8; step++) {
      seq.pattern->SetListData(0, std::to_string(step) + " " + std::to_string(notes[step]));
    }
    // tutorial:step-sequencer-fill:end

    // Listen to what the pattern sends. A parentless sink is the test's, not
    // the patch's (see AcceptsHandlesUnlocked).
    TestHelpers::IntSink played;
    YSE::pHandle playedHandle(&played);
    patch.Connect(seq.pattern, 0, &playedHandle, 0);

    // Stand in for the metro: every bang of the counter is one step. Ten steps
    // run off the end of the pattern and start it again.
    std::vector<int> heard;
    for (int i = 0; i < 10; i++) {
      played.gotInt = false;
      seq.counter->SetBang(0);
      REQUIRE(played.gotInt);
      heard.push_back(played.received);
    }
    CHECK(heard == std::vector<int>{48, 55, 60, 63, 48, 58, 60, 67, 48, 55});

    // The last step set the saw to note 55. It is audible.
    Renderer render(patch, 2);
    render.Render(8);
    const std::vector<float> audio = render.Render(64);
    CHECK(Rms(audio) > 0.05f);
    CHECK(Frequency(audio) == doctest::Approx(Mtof(55.f)).epsilon(0.05));

    // Rewriting a step from the host lands in the pattern: "reset" and one bang
    // replay step 0 with its new note.
    CHECK(patch.PassData(std::string("0 36"), "edit"));
    render.Render(1); // PassData is delivered at the start of the next block
    seq.counter->SetListData(0, "reset");
    played.gotInt = false;
    seq.counter->SetBang(0);
    REQUIRE(played.gotInt);
    CHECK(played.received == 36);

    // And the metro really is what drives the counter: starting it sends a
    // bang straight away. Stopped again at once, so no timer tick is awaited.
    played.gotInt = false;
    seq.metro->SetIntData(0, 1);
    seq.metro->SetIntData(0, 0);
    CHECK(played.gotInt);

    patch.Disconnect(seq.pattern, 0, &playedHandle, 0);
  }

  // ─── tutorial 12: a voice in a subpatcher ───────────────────────────────────

  TEST_CASE("tutorials: 12 the subpatched voice plays, releases and survives a reload (#883)") {
    YSE::patcher patch;
    patch.create(2);
    YSE::pHandle* voice = BuildMonoSynth(patch);
    REQUIRE(voice != nullptr);
    CHECK(patch.SubpatcherInlets(voice) == 1);
    CHECK(patch.SubpatcherOutlets(voice) == 1);

    Renderer render(patch, 2);
    CHECK(Rms(render.Render(4)) < 1e-6f); // silent until a note arrives

    // tutorial:voice-play:begin
    patch.PassData(std::string("57 100"), "note"); // A3 at velocity 100
    // tutorial:voice-play:end
    render.Render(16); // past the 20 ms attack
    const std::vector<float> held = render.Render(64);
    CHECK(Rms(held) > 0.2f);
    CHECK(Frequency(held) == doctest::Approx(Mtof(57.f)).epsilon(0.05));

    // tutorial:voice-release:begin
    patch.PassData(std::string("57 0"), "note"); // velocity 0: release
    // tutorial:voice-release:end
    render.Render(16); // past the 20 ms release
    CHECK(Rms(render.Render(4)) < 1e-4f);

    // The host can also play the voice through its pin, without the .r.
    voice->SetListData(0, "57 100");
    render.Render(16);
    CHECK(Rms(render.Render(16)) > 0.2f);

    // The voice is saved as a "patcher" record plus its contents, and loads
    // back into something that plays the same way.
    YSE::patcher copy;
    copy.create(2);
    copy.ParseJSON(patch.DumpJSON());
    REQUIRE(copy.Objects() == patch.Objects());
    Renderer again(copy, 2);
    copy.PassData(std::string("57 100"), "note");
    again.Render(16);
    CHECK(Rms(again.Render(16)) > 0.2f);
  }

  // ─── tutorial 13: a polyphonic synth played from MIDI ───────────────────────

#if YSE_ENABLE_MIDI_DEVICE
  TEST_CASE("tutorials: 13 the MIDI synth plays chords and releases them (#883)") {
    // Port 7, for the reason test_patcher_midiin.cpp gives: the last port the
    // hub accepts, so a real keyboard cannot play into the assertions. The
    // tutorial itself uses port 0.
    constexpr unsigned int port = 7;
    YSE::patcher patch;
    patch.create(2);
    BuildMidiSynth(patch, std::to_string(port));

    const auto wire = [](unsigned char status, unsigned char pitch, unsigned char velocity) {
      const unsigned char bytes[3] = {status, pitch, velocity};
      YSE::MIDI::InHub().Deliver(port, bytes, 3);
    };

    Renderer render(patch, 2);
    CHECK(Rms(render.Render(4)) < 1e-6f);

    // One key: one voice, at its pitch.
    wire(0x90, 57, 127);
    render.Render(16);
    const std::vector<float> one = render.Render(64);
    const float oneRms = Rms(one);
    CHECK(oneRms > 0.05f);
    CHECK(Frequency(one) == doctest::Approx(Mtof(57.f)).epsilon(0.05));

    // Two more keys: three voices sound together, so the mix is louder.
    wire(0x90, 64, 127);
    wire(0x90, 69, 127);
    render.Render(16);
    CHECK(Rms(render.Render(64)) > oneRms * 1.3f);

    // A note-off message and a note-on at velocity 0 are both releases.
    wire(0x80, 57, 64);
    wire(0x90, 64, 0);
    wire(0x80, 69, 0);
    render.Render(16);
    CHECK(Rms(render.Render(4)) < 1e-4f);

    // Five keys on four voices: the oldest is stolen, nothing hangs.
    for (unsigned char pitch = 60; pitch < 65; pitch++)
      wire(0x90, pitch, 100);
    render.Render(16);
    CHECK(Rms(render.Render(16)) > 0.05f);
    for (unsigned char pitch = 60; pitch < 65; pitch++)
      wire(0x80, pitch, 0);
    render.Render(16);
    CHECK(Rms(render.Render(4)) < 1e-4f);

    // A saved copy loads with its voices and plays from the keyboard at once.
    YSE::patcher copy;
    copy.create(2);
    copy.ParseJSON(patch.DumpJSON());
    REQUIRE(copy.Objects() == patch.Objects());
    Renderer again(copy, 2);
    wire(0x90, 57, 127);
    again.Render(16);
    CHECK(Rms(again.Render(16)) > 0.05f);
    wire(0x80, 57, 0);
    again.Render(16);
  }
#endif

  // ─── tutorial 14: presets ───────────────────────────────────────────────────

  TEST_CASE("tutorials: 14 presets store, recall, and come back on load (#883)") {
    YSE::patcher patch;
    patch.create(2);
    PresetSynth synth = BuildPresetSynth(patch);
    REQUIRE(synth.presets != nullptr);

    // tutorial:presets-store:begin
    // A dark, quiet sound in slot 0 ...
    synth.note->SetFloatData(0, 48.f);
    synth.cutoff->SetFloatData(0, 400.f);
    synth.level->SetFloatData(0, 0.3f);
    synth.presets->SetListData(0, "store 0");

    // ... and a bright, louder one in slot 1.
    synth.note->SetFloatData(0, 55.f);
    synth.cutoff->SetFloatData(0, 4000.f);
    synth.level->SetFloatData(0, 0.6f);
    synth.presets->SetListData(0, "store 1");
    // tutorial:presets-store:end

    Renderer render(patch, 2);
    render.Render(4);
    const float bright = Rms(render.Render(32));

    // tutorial:presets-recall:begin
    synth.presets->SetIntData(0, 0); // back to slot 0
    // tutorial:presets-recall:end
    CHECK(GuiFloat(synth.note) == doctest::Approx(48.f));
    CHECK(GuiFloat(synth.cutoff) == doctest::Approx(400.f));
    CHECK(GuiFloat(synth.level) == doctest::Approx(0.3f));
    CHECK(synth.presets->GetGuiValue() == "0");

    // The recall reached the sound, not only the controls.
    render.Render(4);
    const std::vector<float> dark = render.Render(32);
    CHECK(Rms(dark) > 0.01f);
    CHECK(Rms(dark) < bright);
    CHECK(Frequency(dark) == doctest::Approx(Mtof(48.f)).epsilon(0.1));

    // Leave slot 1 active, then save and load. The slots travel with the
    // patch and the .loadmess recalls slot 0, so the loaded copy comes up in
    // slot 0 rather than at the .f creation values (60, 1000, 0.5).
    synth.presets->SetIntData(0, 1);
    // tutorial:presets-reload:begin
    YSE::patcher copy;
    copy.create(2);
    copy.ParseJSON(patch.DumpJSON());
    // tutorial:presets-reload:end
    REQUIRE(copy.Objects() == patch.Objects());
    CHECK(GuiFloat(copy.GetHandleFromID(synth.note->GetID())) == doctest::Approx(48.f));
    CHECK(GuiFloat(copy.GetHandleFromID(synth.cutoff->GetID())) == doctest::Approx(400.f));
    CHECK(GuiFloat(copy.GetHandleFromID(synth.level->GetID())) == doctest::Approx(0.3f));
    CHECK(copy.GetHandleFromID(synth.presets->GetID())->GetGuiValue() == "0");

    // A host message through PassData is delivered on the audio thread, where
    // .preset hands it to the timer thread: the recall lands a tick after the
    // block that carried it (issue #952, gui.rst).
    synth.presets->SetIntData(0, 0);
    // tutorial:presets-passdata:begin
    YSE::pHandle* recall = patch.CreateObject(".r", "recall");
    patch.Connect(recall, 0, synth.presets, 0);
    patch.PassData(1, "recall"); // recalls slot 1 shortly after the next block
    // tutorial:presets-passdata:end
    render.Render(1);
    // Polled with a five-second bound for a broken build; it lands in about
    // a millisecond.
    bool landed = false;
    for (int i = 0; i < 5000 && !landed; i++) {
      landed =
          synth.presets->GetGuiValue() == "1" && std::fabs(GuiFloat(synth.note) - 55.f) < 0.01f;
      if (!landed) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(landed);
  }

} // TEST_SUITE
