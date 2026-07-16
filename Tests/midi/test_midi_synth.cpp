// End-to-end MIDI -> synth routing tests (issue #155).
//
// These drive the *whole* path: a MIDI source (file playback or device input)
// pushes MESSAGE ops onto a real synth's inbox, the synth renders them offline,
// and we assert the note sequence the synth actually received via an
// onNoteEvent log. This is the acceptance-criterion "event log comparison on a
// test synth".
//
// ISOLATION: like the "synthlifecycle" suite, these run as a dedicated ctest
// process (excluded from the combined run) because they call System::close(),
// which permanently stops the global thread pools. Keeping them in their own
// process also contains the pre-existing SOUND/CHANNEL static-teardown
// fragility (issue #298) that constructing the synth + sound managers can
// expose at exit.
//
// initOffline() needs no audio hardware, so these run in CI; if it returns
// false on some host the case bails out and doctest counts it as a pass.

#include <doctest/doctest.h>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

#include "yse.hpp"
#include "headers/defines.hpp"
#include "sound/soundInterface.hpp"
#include "synth/sineVoice.hpp"
#include "synth/synthInterface.hpp"
#include "midi/midifile.hpp"
#include "internal/time.h"

// The MIDI file C API (yse_midi_file_*) is platform-agnostic — unlike MIDI
// device I/O it is not RtMidi-gated — so its C headers are included
// unconditionally for the #372 file->synth C-API case below.
#include "yse_c/yse_midi.h"
#include "yse_c/yse_synth.h"
#include "yse_c/yse_sound.h"

#if YSE_ENABLE_MIDI_DEVICE
#include "midi/device.hpp"
#include "support/midi_dispatch_tester.hpp"
#endif

using namespace std::chrono_literals;

namespace {

  // onNoteEvent is a captureless function pointer, so it logs into this
  // process-static buffer. The offline render loop is single-threaded (the
  // test thread), so no synchronisation is needed.
  struct NoteLogEntry {
    bool on;
    int note;
  };
  std::vector<NoteLogEntry> g_noteLog;

  void logNote(bool on, float* noteNumber, float* /*velocity*/) {
    g_noteLog.push_back({on, static_cast<int>(*noteNumber + 0.5f)});
  }

  // Same note log, reached through the flat C ABI note-rewrite hook
  // (YseSynthNoteCallback: `int note_on` instead of the C++ `bool on`). Used by
  // the #372 C-API case so it can observe note delivery without an engine synth
  // handle. Captureless, as the hook contract requires.
  void YSE_C_CALLBACK cLogNote(int on, float* noteNumber, float* /*velocity*/) {
    g_noteLog.push_back({on != 0, static_cast<int>(std::lround(*noteNumber))});
  }

  // Bring a synth attached to a sound up to OBJECT_READY (voice cloning is
  // async on the slow pool). Returns true once its voices are allocated.
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

  // Drain the engine so released impls are fully deleted before teardown.
  void drain(std::chrono::milliseconds budget = 500ms) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
      YSE::System().update();
      YSE::System().renderOffline(1);
      std::this_thread::sleep_for(2ms);
    }
  }

} // namespace

TEST_SUITE("midisynth") {

  // ─── MIDI file -> synth ───────────────────────────────────────────────────

  TEST_CASE("midisynth: MIDI file playback drives the note on/off sequence into a synth") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;
    g_noteLog.clear();

    YSE::SYNTH::sineVoice proto; // declared first: outlives the synth
    proto.attack(0.005f).release(0.05f);
    {
      YSE::synth syn;
      syn.create().addVoices(proto, 8);
      syn.onNoteEvent(logNote);
      {
        YSE::sound snd;
        snd.create(syn);
        REQUIRE(bringReady(syn, 8));
        snd.play();

        YSE::MIDI::file mf;
        REQUIRE(mf.create(std::string(YSE_TEST_FIXTURES_DIR) + "/test_type0.mid"));
        mf.connect(syn);
        mf.play();

        // One update() drains the file into the MIDI manager's audio-thread
        // list; then render blocks until both note events land (or a generous
        // cap — the fixture's note-off is half a second in).
        YSE::System().update();
        for (int i = 0; i < 4000 && g_noteLog.size() < 2; ++i)
          YSE::System().renderOffline(1);

        mf.disconnect(syn);
        snd.stop();
        YSE::System().renderOffline(8);
      } // sound destroyed first (§9 order)
      drain();
    } // synth destroyed after its sound
    drain();
    YSE::System().close();

    REQUIRE(g_noteLog.size() == 2);
    CHECK(g_noteLog[0].on == true);
    CHECK(g_noteLog[0].note == 60);
    CHECK(g_noteLog[1].on == false);
    CHECK(g_noteLog[1].note == 60);
  }

  // ─── C API surface: yse_midi_file_connect_synth / disconnect_synth (#372) ───
  //
  // Drives the SAME MIDI file -> synth route as the engine-level case above, but
  // wires it entirely through the flat C ABI (yse_midi_file_* + yse_synth_* +
  // yse_sound_*), proving the new C wrappers bridge the opaque YseMidiFile /
  // YseSynth handles into the engine's routing table and actually deliver notes.
  // Unlike the device path in the #371 case, the MIDI file path needs no RtMidi
  // port, so the full note-delivery semantics ARE reachable and asserted at the C
  // layer. The note-rewrite hook (cLogNote) records what the synth received.
  TEST_CASE("midisynth: C API routes MIDI file playback into a synth (#372)") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;
    g_noteLog.clear();

    YseSynth* syn = yse_synth_create();
    REQUIRE(syn != nullptr);
    // 8-voice omni pool, snappy envelope so the note-off tail resolves quickly.
    REQUIRE(yse_synth_add_voices_sine(syn, 8, 0, 0, 127, 0.005f, 0.001f, 1.0f, 0.05f) == YSE_OK);
    yse_synth_set_note_callback(syn, &cLogNote);

    YseSound* snd = yse_sound_create();
    REQUIRE(snd != nullptr);
    REQUIRE(yse_synth_attach_to_sound(syn, snd, nullptr, 0.8f) == YSE_OK);
    yse_sound_play(snd);

    // Setup pool clones the voices asynchronously; pump until the pool is ready.
    {
      const auto deadline = std::chrono::steady_clock::now() + 2s;
      while (yse_synth_get_num_voices(syn) < 8 && std::chrono::steady_clock::now() < deadline) {
        YSE::System().update();
        YSE::System().renderOffline(1);
        std::this_thread::sleep_for(5ms);
      }
      REQUIRE(yse_synth_get_num_voices(syn) == 8);
    }

    YseMidiFile* mf = yse_midi_file_create();
    REQUIRE(mf != nullptr);
    REQUIRE(yse_midi_file_load(
                mf, (std::string(YSE_TEST_FIXTURES_DIR) + "/test_type0.mid").c_str()) == YSE_OK);
    yse_midi_file_connect_synth(mf, syn);
    yse_midi_file_connect_synth(mf, syn); // already connected -> no-op (idempotent)
    yse_midi_file_play(mf);

    // One update() drains the file into the MIDI manager's audio-thread list;
    // then render blocks until both note events land (or a generous cap — the
    // fixture's note-off is half a second in).
    YSE::System().update();
    for (int i = 0; i < 4000 && g_noteLog.size() < 2; ++i)
      YSE::System().renderOffline(1);

    yse_midi_file_disconnect_synth(mf, syn);
    yse_midi_file_disconnect_synth(mf, syn); // disconnect when not connected: safe

    // Null-safety matrix: any NULL handle / NULL synth combination is a no-op.
    yse_midi_file_connect_synth(nullptr, syn);
    yse_midi_file_connect_synth(mf, nullptr);
    yse_midi_file_connect_synth(nullptr, nullptr);
    yse_midi_file_disconnect_synth(nullptr, syn);
    yse_midi_file_disconnect_synth(mf, nullptr);

    yse_sound_stop(snd);
    YSE::System().renderOffline(8);

    yse_midi_file_destroy(mf);
    yse_sound_destroy(snd); // sound must go before the synth it renders
    yse_synth_destroy(syn);
    drain();
    YSE::System().close();

    REQUIRE(g_noteLog.size() == 2);
    CHECK(g_noteLog[0].on == true);
    CHECK(g_noteLog[0].note == 60);
    CHECK(g_noteLog[1].on == false);
    CHECK(g_noteLog[1].note == 60);
  }

#if YSE_ENABLE_MIDI_DEVICE

  // ─── MIDI device -> synth ──────────────────────────────────────────────────

  TEST_CASE("midisynth: device MIDI note on/off is routed into a connected synth") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;
    g_noteLog.clear();

    YSE::SYNTH::sineVoice proto;
    proto.attack(0.005f).release(0.05f);
    {
      YSE::synth syn;
      syn.create().addVoices(proto, 8);
      syn.onNoteEvent(logNote);
      {
        YSE::sound snd;
        snd.create(syn);
        REQUIRE(bringReady(syn, 8));
        snd.play();

        YSE::midiIn in; // no real port opened — dispatch() is driven directly
        in.connect(syn); // omni

        const unsigned char noteOn[3] = {0x90, 60, 100}; // ch 1 note-on
        MidiInDispatchTester::dispatch(in, 0.0, noteOn, 3);
        YSE::System().update();
        for (int i = 0; i < 8; ++i)
          YSE::System().renderOffline(1);

        const unsigned char noteOff[3] = {0x80, 60, 0}; // ch 1 note-off
        MidiInDispatchTester::dispatch(in, 0.0, noteOff, 3);
        for (int i = 0; i < 8; ++i)
          YSE::System().renderOffline(1);

        in.disconnect(syn);
        snd.stop();
        YSE::System().renderOffline(8);
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

  TEST_CASE("midisynth: device channel filter drops messages on other channels") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;
    g_noteLog.clear();

    YSE::SYNTH::sineVoice proto;
    proto.attack(0.005f).release(0.05f);
    {
      YSE::synth syn;
      syn.create().addVoices(proto, 8);
      syn.onNoteEvent(logNote);
      {
        YSE::sound snd;
        snd.create(syn);
        REQUIRE(bringReady(syn, 8));
        snd.play();

        YSE::midiIn in;
        in.connect(syn, /*channelFilter*/ 2); // accept MIDI channel 2 only

        // Channel 1 (status nibble channel 0) — filtered out.
        const unsigned char ch1[3] = {0x90, 60, 100};
        MidiInDispatchTester::dispatch(in, 0.0, ch1, 3);
        YSE::System().update();
        for (int i = 0; i < 8; ++i)
          YSE::System().renderOffline(1);
        CHECK(g_noteLog.empty());

        // Channel 2 (status nibble channel 1) — accepted.
        const unsigned char ch2[3] = {0x91, 62, 100};
        MidiInDispatchTester::dispatch(in, 0.0, ch2, 3);
        for (int i = 0; i < 8; ++i)
          YSE::System().renderOffline(1);
        REQUIRE(g_noteLog.size() == 1);
        CHECK(g_noteLog[0].on == true);
        CHECK(g_noteLog[0].note == 62);

        in.disconnect(syn);
        snd.stop();
        YSE::System().renderOffline(8);
      }
      drain();
    }
    drain();
    YSE::System().close();
    CHECK(true);
  }

  // ─── C API surface: yse_midi_in_connect_synth / disconnect_synth (#371) ─────
  //
  // Mirrors the "device MIDI -> connected synth" wiring above, but drives the
  // connect / disconnect through the flat C ABI (yse_midi_in_* + yse_synth_*),
  // proving the C wrapper bridges the opaque YseSynth / YseMidiIn handles into
  // the engine's routing table and is null-safe. The note-delivery semantics
  // (channel offset, filter, normalization) are pinned by the engine-level
  // cases above and by test_midi_synth_routing.cpp; they are not reachable at
  // the C layer, because the opaque YseMidiIn hides the inner YSE::midiIn the
  // dispatch tester needs and no RtMidi port is available in CI.
  TEST_CASE("midisynth: C API routes a MIDI input device into a synth (#371)") {
    YSE::System().close();
    if (!YSE::System().initOffline()) return;

    YseSynth* syn = yse_synth_create();
    REQUIRE(syn != nullptr);
    YseMidiIn* in = yse_midi_in_create();
    REQUIRE(in != nullptr);

    // Connect omni, then re-connect to exercise the engine's idempotent
    // "already connected -> refresh channel filter" path, then disconnect.
    yse_midi_in_connect_synth(in, syn, 0);
    yse_midi_in_connect_synth(in, syn, 2); // update filter to MIDI channel 2
    yse_midi_in_disconnect_synth(in, syn);
    yse_midi_in_disconnect_synth(in, syn); // disconnect when not connected: safe

    // Null-safety matrix: any NULL handle / NULL synth combination is a no-op.
    yse_midi_in_connect_synth(nullptr, syn, 0);
    yse_midi_in_connect_synth(in, nullptr, 0);
    yse_midi_in_connect_synth(nullptr, nullptr, 5);
    yse_midi_in_disconnect_synth(nullptr, syn);
    yse_midi_in_disconnect_synth(in, nullptr);

    // Destroy the port before the synth (the port holds the routing pointer).
    yse_midi_in_destroy(in);
    yse_synth_destroy(syn);

    drain();
    YSE::System().close();
    CHECK(true);
  }

#endif // YSE_ENABLE_MIDI_DEVICE

} // TEST_SUITE("midisynth")
