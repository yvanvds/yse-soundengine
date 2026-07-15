// End-to-end tests for the generative player driving a synth (issue #156).
//
// #156 reconnects YSE::player to a YSE::synth: player.create(synth&) registers
// the player with the engine and every note it generates is routed through the
// synth's public note API (its lock-free inbox), exactly as MIDI input is
// routed in #155. Two layers are covered here:
//
//   * A cheap, engine-free guard test (TEST_SUITE "music") for the
//     use-before-create null-safety (#156 / #268): calling player methods on a
//     default-constructed player must log and no-op, never dereference a null
//     implementation.
//
//   * Full end-to-end tests (TEST_SUITE "playersynth") that drive the whole
//     path offline: player -> synth inbox -> voice allocator -> aggregate render
//     -> master output. They assert the notes the synth actually received (via
//     an onNoteEvent log) are scale-constrained, that audio is rendered, and
//     that stop() halts generation.
//
// ISOLATION: like the "midisynth" / "synthlifecycle" suites, the playersynth
// cases run as a dedicated ctest process (excluded from the combined run)
// because they call System::close(), which permanently stops the global thread
// pools, and because constructing the synth + sound managers can expose the
// pre-existing SOUND/CHANNEL static-teardown fragility (issue #298) at exit.
// initOffline() needs no audio hardware, so they run in CI; if it returns false
// on some host the case bails out and doctest counts it as a pass.

#include <doctest/doctest.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <thread>
#include <vector>

#include "yse.hpp"
#include "channel/channelInterface.hpp"
#include "sound/soundInterface.hpp"
#include "synth/sineVoice.hpp"
#include "synth/synthInterface.hpp"
#include "music/scale/scaleInterface.hpp"
#include "music/motif/motifInterface.hpp"
#include "music/pNote.hpp"
#include "player/playerInterface.hpp"
#include "internal/time.h"

using namespace std::chrono_literals;

namespace {

  // onNoteEvent is a captureless function pointer, so it logs into this
  // process-static buffer. The offline render loop is single-threaded (the test
  // thread also drives the "audio" render), so no synchronisation is needed.
  struct NoteLogEntry {
    bool on;
    int note;
  };
  std::vector<NoteLogEntry> g_noteLog;

  void logNote(bool on, float* noteNumber, float* /*velocity*/) {
    g_noteLog.push_back({on, static_cast<int>(std::lround(*noteNumber))});
  }

  // Pitch classes of a C-major scale (C D E F G A B). A pitch is in-scale iff
  // its pitch class (note % 12) is set here — octave-invariant, so it holds for
  // every octave the player's octave-replicated scale can land on.
  bool inCMajor(int note) {
    static const bool pc[12] = {true,  false, true,  false, true,  true,
                                false, true,  false, true,  false, true};
    return pc[((note % 12) + 12) % 12];
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

  // Advance the engine `blocks` blocks, returning the peak master output level
  // seen across them. Drives System().update() each block so the gated managers
  // (scale / motif drain, synth lifecycle) tick alongside the every-block
  // player generation and synth render.
  float run(int blocks) {
    float peak = 0.f;
    for (int i = 0; i < blocks; ++i) {
      YSE::System().update();
      YSE::System().renderOffline(1);
      peak = std::max(peak, YSE::ChannelMaster().getPeakLinearPost());
    }
    return peak;
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

  // Every note-on logged so far is a member of the C-major scale.
  void checkAllNotesInScale() {
    for (const auto& e : g_noteLog) {
      if (e.on) {
        CHECK(inCMajor(e.note));
      }
    }
  }

  bool anyNoteOn() {
    for (const auto& e : g_noteLog)
      if (e.on) return true;
    return false;
  }

} // namespace

// ─── use-before-create guard (no engine) ─────────────────────────────────────

TEST_SUITE("music") {

  TEST_CASE("player: methods before create() log and no-op instead of null-deref (#156/#268)") {
    // A default-constructed player has no implementation. Every public method
    // must guard against the null pimpl rather than dereferencing it — the exact
    // crash issue #268 tracked once yse_player_create() started handing these to
    // bindings. None of the calls below may crash; play state stays false.
    YSE::player p;
    CHECK(p.isPlaying() == false);

    p.play();
    CHECK(p.isPlaying() == false); // play() before create() is a no-op

    p.stop();
    p.setMinimumPitch(48.f);
    p.setMaximumPitch(72.f);
    p.setMinimumVelocity(0.3f);
    p.setMaximumVelocity(0.8f);
    p.setMinimumGap(0.1f);
    p.setMaximumGap(0.2f);
    p.setMinimumLength(0.1f);
    p.setMaximumLength(0.3f);
    p.setVoices(4);
    p.playMotifs(0.5f);
    p.playPartialMotifs(0.5f);
    p.fitMotifsToScale(1.f);

    CHECK(p.isPlaying() == false);
    CHECK(true); // reached here without dereferencing a null implementation
  }

} // TEST_SUITE("music")

// ─── end-to-end player -> synth -> sound (offline engine) ─────────────────────

TEST_SUITE("playersynth") {

  TEST_CASE("playersynth: player + scale drives in-scale notes into a synth and renders audio") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;
    g_noteLog.clear();

    // Reference sine voice — declared first so it outlives the synth (§9 order).
    YSE::SYNTH::sineVoice proto;
    proto.attack(0.004f).decay(0.01f).sustain(0.8f).release(0.05f);
    {
      YSE::synth syn;
      syn.create().addVoices(proto, 16);
      syn.onNoteEvent(logNote); // capture every note the player sends the synth
      {
        YSE::sound snd;
        snd.create(syn);
        REQUIRE(bringReady(syn, 16));
        snd.play();

        // A C-major scale: every pitch the player generates is snapped to it, so
        // every note the synth receives must be in-scale.
        YSE::scale sc;
        sc.add(60.f, 12.f); // C
        sc.add(62.f, 12.f); // D
        sc.add(64.f, 12.f); // E
        sc.add(65.f, 12.f); // F
        sc.add(67.f, 12.f); // G
        sc.add(69.f, 12.f); // A
        sc.add(71.f, 12.f); // B

        YSE::player pl;
        pl.create(syn);
        pl.setScale(sc);
        pl.setMinimumPitch(48.f);
        pl.setMaximumPitch(84.f);
        pl.setMinimumVelocity(0.4f);
        pl.setMaximumVelocity(0.9f);
        pl.setMinimumGap(0.f);
        pl.setMaximumGap(0.f);
        pl.setMinimumLength(0.02f);
        pl.setMaximumLength(0.06f);
        pl.setVoices(4);

        // Drain the scale + player-config messages into their impls before we
        // start generating (both are drained on the audio thread).
        run(20);
        pl.play();

        // Generate a stream of notes and render them.
        const float peak = run(3000);

        // 1. play() actually produced notes on the synth.
        REQUIRE(anyNoteOn());
        // 2. every generated note the synth received is in the scale.
        checkAllNotesInScale();
        // 3. the synth rendered audio in response.
        CHECK(peak > 1e-3f);

        // 4. stop() halts generation: no NEW note-ons after it settles.
        pl.stop();
        run(300); // let outstanding notes finish (note-offs only)
        const std::size_t mark = g_noteLog.size();
        run(600);
        std::size_t noteOnsAfterStop = 0;
        for (std::size_t k = mark; k < g_noteLog.size(); ++k)
          if (g_noteLog[k].on) ++noteOnsAfterStop;
        CHECK(noteOnsAfterStop == 0);

        snd.stop();
        YSE::System().renderOffline(8);
      } // sound destroyed first
      drain();
    } // synth destroyed after its sound
    drain();
    YSE::System().close();
  }

  TEST_CASE("playersynth: player + motif generates scale-quantised notes into a synth") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;
    g_noteLog.clear();

    YSE::SYNTH::sineVoice proto;
    proto.attack(0.004f).decay(0.01f).sustain(0.8f).release(0.05f);
    {
      YSE::synth syn;
      syn.create().addVoices(proto, 16);
      syn.onNoteEvent(logNote);
      {
        YSE::sound snd;
        snd.create(syn);
        REQUIRE(bringReady(syn, 16));
        snd.play();

        YSE::scale sc;
        sc.add(60.f, 12.f);
        sc.add(62.f, 12.f);
        sc.add(64.f, 12.f);
        sc.add(65.f, 12.f);
        sc.add(67.f, 12.f);
        sc.add(69.f, 12.f);
        sc.add(71.f, 12.f);

        // A short motif. Its Eb (63) is deliberately out of C-major; with
        // fitMotifsToScale(1) every emitted motif note is snapped to the scale,
        // so the synth still only ever sees in-scale pitches.
        YSE::motif mot;
        mot.add(YSE::MUSIC::pNote(0.00f, 60.f, 0.8f, 0.05f));
        mot.add(YSE::MUSIC::pNote(0.05f, 63.f, 0.8f, 0.05f));
        mot.add(YSE::MUSIC::pNote(0.10f, 67.f, 0.8f, 0.05f));
        mot.add(YSE::MUSIC::pNote(0.15f, 72.f, 0.8f, 0.05f));
        mot.setLength();

        YSE::player pl;
        pl.create(syn);
        pl.setScale(sc);
        pl.setMinimumPitch(48.f);
        pl.setMaximumPitch(84.f);
        pl.setMinimumVelocity(0.4f);
        pl.setMaximumVelocity(0.9f);
        pl.setMinimumGap(0.f);
        pl.setMaximumGap(0.f);
        pl.setMinimumLength(0.03f);
        pl.setMaximumLength(0.08f);
        pl.setVoices(2);
        pl.addMotif(mot, 4);
        pl.playMotifs(0.9f); // almost always draw from the motif
        pl.fitMotifsToScale(1.f); // snap every motif note to the scale

        // Warm-up: drain the scale, motif and player-config messages into their
        // impls before generation starts, so the motif is populated before the
        // player ever draws from it.
        run(40);
        pl.play();

        run(2500);

        REQUIRE(anyNoteOn());
        checkAllNotesInScale();

        pl.stop();
        run(200);
        snd.stop();
        YSE::System().renderOffline(8);
      }
      drain();
    }
    drain();
    YSE::System().close();
  }

} // TEST_SUITE("playersynth")
