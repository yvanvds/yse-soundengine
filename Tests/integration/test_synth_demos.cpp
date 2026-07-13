// End-to-end coverage for the synth & effects demo scenes (issue #180).
//
// The interactive Demo18–Demo21 console pages (FM+MIDI keyboard, SFZ piano,
// swarm, mixer) cannot be driven headless — they need a MIDI keyboard, an audio
// device and a human at the keys. What *is* testable, and what actually breaks,
// is the engine wiring each scene sets up: a bundled DX7 bank loaded into an FM
// synth and played; the bundled SFZ loaded into a sampler synth with the sustain
// pedal engaged; the swarm/orbit position handler moving voices around the
// listener; and a channel insert chain (EQ→compressor→chorus) plus a send/return
// plate reverb. These cases drive exactly those paths, offline, against the real
// content pack — the "demos are living integration tests" contract from the
// issue, minus the parts that require hardware.
//
// ISOLATION: like the `midisynth` / `synthpositioning` suites these drive
// System::initOffline()/close() (process-global engine state), so they run in a
// dedicated ctest process (`synthdemos`), excluded from the combined run.
// initOffline() needs no audio hardware, so they run in CI. Content assets that
// are only present after the opt-in fetch are skipped gracefully; the committed
// CC0 seed guarantees the FM bank and the SFZ instrument, so those cases always
// exercise a real load.

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "yse.hpp"
#include "channel/channelInterface.hpp"
#include "sound/soundInterface.hpp"
#include "synth/sineVoice.hpp"
#include "synth/samplerVoice.hpp"
#include "synth/synthInterface.hpp"
#include "synth/positionHandlers.hpp"
#include "dsp/fm/fmVoice.hpp"
#include "dsp/fm/dx7Sysex.hpp"
#include "dsp/modules/parametricEQ.hpp"
#include "dsp/modules/compressor.hpp"
#include "dsp/modules/chorus.hpp"
#include "dsp/modules/plateReverb.hpp"

#ifndef YSE_CONTENT_PACK_DIR
#define YSE_CONTENT_PACK_DIR "../../content"
#endif

using namespace std::chrono_literals;

namespace {

  // onNoteEvent is a captureless function pointer, so it logs into this
  // process-static buffer. The offline render loop is single-threaded (the test
  // thread), so no synchronisation is needed.
  struct NoteLogEntry {
    bool on;
    int note;
  };
  std::vector<NoteLogEntry> g_noteLog;

  void logNote(bool on, float* noteNumber, float* /*velocity*/) {
    g_noteLog.push_back({on, static_cast<int>(std::lround(*noteNumber))});
  }

  bool fileExists(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return f.good();
  }

  std::string contentPath(const char* rel) {
    return std::string(YSE_CONTENT_PACK_DIR) + "/" + rel;
  }

  // Bring a synth attached to a sound up to OBJECT_READY (voice cloning is async
  // on the slow pool). Returns true once its voices are allocated.
  bool bringReady(YSE::synth& syn, int expectedVoices) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
      YSE::System().update();
      YSE::System().renderOffline(1);
      if (syn.getNumVoices() == expectedVoices) return true;
      std::this_thread::sleep_for(5ms);
    }
    return syn.getNumVoices() == expectedVoices;
  }

  // Advance the offline engine by n blocks. update() before each render flushes
  // control-thread messages (note events, pedal, handler params) to the audio
  // side — without it a directly-driven noteOn never reaches the voices.
  void pump(int n) {
    for (int b = 0; b < n; ++b) {
      YSE::System().update();
      YSE::System().renderOffline(1);
    }
  }

  // Drain the engine so released impls are fully deleted before teardown.
  void drain(std::chrono::milliseconds budget = 500ms) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      YSE::System().update();
      YSE::System().renderOffline(1);
      std::this_thread::sleep_for(2ms);
    }
  }

  double planarRadius(const YSE::Pos& p) {
    return std::sqrt(static_cast<double>(p.x) * p.x + static_cast<double>(p.z) * p.z);
  }

} // namespace

TEST_SUITE("synthdemos") {

  // ─── Demo18: FM + MIDI keyboard ─────────────────────────────────────────────

  TEST_CASE("synthdemos: bundled DX7 bank drives an FM synth end-to-end") {
    const std::string bankPath = contentPath("fm/original/yse_originals.syx");
    if (!fileExists(bankPath)) {
      MESSAGE("FM bank absent at " << bankPath << " — skipping");
      return;
    }
    YSE::SYNTH::dx7Bank bank;
    REQUIRE(YSE::SYNTH::dx7SysEx::loadBank(bankPath.c_str(), bank));
    REQUIRE_FALSE(bank.empty());

    YSE::System().close();
    if (!YSE::System().initOffline()) return;
    g_noteLog.clear();

    YSE::SYNTH::fmVoice proto; // declared first: outlives the synth
    proto.setPatch(bank.voices[0]);
    {
      YSE::synth syn;
      syn.create().addVoices(proto, 8);
      syn.onNoteEvent(logNote);
      {
        YSE::sound snd;
        snd.create(syn);
        REQUIRE(bringReady(syn, 8));
        snd.play();

        syn.noteOn(1, 60, 0.8f);
        pump(16);
        syn.noteOff(1, 60);
        pump(16);

        snd.stop();
        pump(8);
      }
      drain();
    }
    drain();
    YSE::System().close();

    REQUIRE(g_noteLog.size() == 2);
    CHECK(g_noteLog[0].on == true);
    CHECK(g_noteLog[0].note == 60);
    CHECK(g_noteLog[1].on == false);
    CHECK(g_noteLog[1].note == 60);
  }

  // ─── Demo19: SFZ piano with sustain pedal ───────────────────────────────────

  TEST_CASE("synthdemos: bundled SFZ instrument plays through a synth with sustain") {
    const std::string sfzPath = contentPath("sfz/yse_pulse.sfz");
    if (!fileExists(sfzPath)) {
      MESSAGE("SFZ instrument absent at " << sfzPath << " — skipping");
      return;
    }

    YSE::System().close();
    if (!YSE::System().initOffline()) return;
    g_noteLog.clear();

    YSE::SYNTH::samplerVoice proto; // declared first: outlives the synth
    REQUIRE(proto.loadSFZ(sfzPath));
    REQUIRE(proto.instrument()->valid());
    {
      YSE::synth syn;
      syn.create().addVoices(proto, 8);
      syn.onNoteEvent(logNote);
      {
        YSE::sound snd;
        snd.create(syn);
        REQUIRE(bringReady(syn, 8));
        snd.play();

        // Pedal down, play and release a note: the note-off is deferred by the
        // sustain state (unit-tested in `synth keyboard`), but the whole demo
        // path — instrument load, synth build, pedal + note events — runs here.
        syn.sustain(1, true);
        syn.noteOn(1, 60, 0.8f);
        pump(16);
        syn.noteOff(1, 60);
        pump(8);
        syn.sustain(1, false);
        pump(16);

        snd.stop();
        pump(8);
      }
      drain();
    }
    drain();
    YSE::System().close();

    REQUIRE(g_noteLog.size() == 2);
    CHECK(g_noteLog[0].on == true);
    CHECK(g_noteLog[0].note == 60);
    CHECK(g_noteLog[1].on == false);
    CHECK(g_noteLog[1].note == 60);
  }

  // ─── Demo20: swarm / orbit position handler ─────────────────────────────────

  TEST_CASE("synthdemos: swarm handler orbits voices around the listener") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;

    YSE::SYNTH::sineVoice voiceProto; // declared first: outlives the synth
    voiceProto.attack(0.02f).release(0.3f);
    YSE::SYNTH::orbitHandler handlerProto;
    handlerProto.radius(1.0f).velocityRadius(2.0f).rate(2.5f);
    {
      YSE::synth syn;
      syn.create().addVoices(voiceProto, 8).positionHandler(handlerProto);
      {
        YSE::sound snd;
        snd.create(syn);
        REQUIRE(bringReady(syn, 8));
        snd.play();

        syn.noteOn(1, 60, 0.9f);
        syn.noteOn(1, 64, 0.9f);
        syn.noteOn(1, 67, 0.9f);
        pump(8);

        const YSE::Pos before = syn.getVoicePosition(1, 60);
        pump(64);
        const YSE::Pos after = syn.getVoicePosition(1, 60);

        // The voice orbits: it sits off the origin and its position moves.
        CHECK(planarRadius(before) > 0.01);
        CHECK((before.x != after.x || before.z != after.z));

        syn.allNotesOff();
        snd.stop();
        pump(8);
      }
      drain();
    }
    drain();
    YSE::System().close();
    CHECK(true);
  }

  // ─── Demo21: mixer — insert chain + send/return plate reverb ─────────────────

  TEST_CASE("synthdemos: mixer insert chain and send/return reverb wire and render") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;

    // Effects declared first so they outlive the channels that reference them.
    YSE::DSP::MODULES::parametricEQ eq;
    YSE::DSP::MODULES::compressor comp;
    YSE::DSP::MODULES::chorus chorus;
    YSE::DSP::MODULES::plateReverb plate;

    YSE::SYNTH::sineVoice voiceProto;
    voiceProto.attack(0.01f).release(0.2f);
    {
      YSE::channel musicCh;
      YSE::channel reverbReturn;
      musicCh.create("music", YSE::ChannelMaster());

      // Insert chain: EQ -> compressor -> chorus.
      comp.threshold(-18.f).ratio(4.f).makeup(4.f);
      chorus.impact(0.4f);
      eq.link(comp);
      comp.link(chorus);
      musicCh.setDSP(&eq);

      // Send/return plate reverb.
      reverbReturn.makeReturn("reverb");
      plate.decay(0.6f).impact(1.0f);
      reverbReturn.setDSP(&plate);
      musicCh.send(0, reverbReturn, 0.3f);

      // Wiring assertions — deterministic, through the public surface.
      CHECK(reverbReturn.isReturn());
      CHECK_FALSE(musicCh.isReturn());
      CHECK(musicCh.getDSP() == &eq);
      CHECK(reverbReturn.getDSP() == &plate);
      CHECK(musicCh.getSendLevel(0) == doctest::Approx(0.3f));

      {
        // Drive audio through the whole chain: a synth-backed sound on the music
        // channel, sending into the plate reverb. Exercises every process()
        // path (EQ, compressor, chorus, plate) — the ASan/TSan value of the run.
        YSE::synth syn;
        syn.create().addVoices(voiceProto, 4);
        YSE::sound snd;
        snd.create(syn, &musicCh);
        REQUIRE(bringReady(syn, 4));
        snd.play();
        syn.noteOn(1, 60, 0.9f);
        pump(64);
        syn.noteOff(1, 60);
        pump(16);
        snd.stop();
        pump(8);
        drain();
      }

      // Tear down routing before the channels/effects go out of scope.
      musicCh.clearSend(0);
      musicCh.setDSP(nullptr);
      reverbReturn.setDSP(nullptr);
      drain();
    }
    drain();
    YSE::System().close();
    CHECK(true);
  }

} // TEST_SUITE("synthdemos")
