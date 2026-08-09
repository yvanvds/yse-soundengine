// Tests for the clip transport (issue #250): loop a beat-timed note-event list
// against a domain clock, dispatched from the audio thread.
//
// Two layers:
//   * The timing core — transport::evaluateWindow() / releaseAll() — is a pure
//     function of the audio-thread-owned state (event list, loop length,
//     sounding-note set). It is driven here with explicit beat windows against a
//     recording sink, with no engine, clock, or audio device, so every firing
//     rule is deterministic and isolated.
//   * A manager + clock integration smoke test drives CLOCK::Manager().update()
//     and CLIP::Manager().update() directly on the test thread (like the domain
//     clock suite), exercising bind + the create/advance handoff end-to-end.
//
// The suite runs in its own process (yse_tests_clip): it ticks the clock/clip
// managers directly, which must not share a process with a live audio thread.

#include <doctest/doctest.h>
#include <atomic>
#include <string>
#include <vector>

#include "yse.hpp"
#include "clip/clip.hpp"
#include "clip/clipTransport.h"
#include "clip/clipManager.h"
#include "clock/clockManager.h"
#include "internal/global.h"
#include "internal/threadPool.h"

#if YSE_ENABLE_MIDI_DEVICE
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include "midi/midiOutSender.h"
#endif

namespace {

  // Records every sink call the transport makes so a test can assert the exact
  // firing. Matches the YSE::synth note-API shape the real fan-out targets.
  struct RecordingSink {
    enum Kind { NoteOn, NoteOff, PitchWheel };
    struct Call {
      Kind kind;
      int channel;
      int pitch;
      float value;
    };
    std::vector<Call> calls;

    void noteOn(int ch, int pitch, float vel) {
      calls.push_back({NoteOn, ch, pitch, vel});
    }
    void noteOff(int ch, int pitch, float vel) {
      calls.push_back({NoteOff, ch, pitch, vel});
    }
    void pitchWheel(int ch, float val) {
      calls.push_back({PitchWheel, ch, 0, val});
    }

    int count(Kind k) const {
      int n = 0;
      for (const auto& c : calls)
        if (c.kind == k) ++n;
      return n;
    }
  };

  YSE::clipEvent ev(double start, double dur, int ch, int pitch, float vel = 0.8f,
                    float bend = 0.f) {
    YSE::clipEvent e;
    e.startBeat = start;
    e.durationBeats = dur;
    e.channel = ch;
    e.pitch = pitch;
    e.velocity = vel;
    e.pitchBend = bend;
    return e;
  }

  // Wait until the clock manager's slow-pool delete job has run to completion.
  // The background pool is a single worker over a FIFO ring, so a job pushed
  // after the reap was enqueued cannot finish before the reap does. Returns
  // false if the pool was not running, so a caller that depends on the reap
  // having happened can say so rather than assert on a sequence that did not.
  struct barrierJob : YSE::INTERNAL::threadPoolJob {
    std::atomic<bool> ran{false};
    void run() override {
      ran.store(true);
    }
  };

  bool DrainSlowPool() {
    barrierJob barrier;
    YSE::INTERNAL::Global().addSlowJob(&barrier);
    barrier.join();
    return barrier.ran.load();
  }

  // destroyClock's full retirement sequence: one tick drops the clock from the
  // audio-thread working list, the next enqueues the slow-pool delete job, and
  // the barrier waits for that job to finish.
  //
  // Drain-then-tick, repeatedly, because the manager skips the enqueue while a
  // previous delete job is still queued — and the clock manager is a
  // process-global singleton, so an earlier case in this suite may well have
  // left one in flight. Draining first makes `isQueued()` false at the moment
  // the tick wants to enqueue ours.
  bool ReapClock(const std::string& name) {
    auto& clocks = YSE::CLOCK::Manager();
    clocks.destroyClock(name);
    bool pooled = true;
    for (int i = 0; i < 3; i++) {
      pooled = DrainSlowPool() && pooled;
      clocks.update(0.01f);
    }
    return DrainSlowPool() && pooled;
  }

  // Clocks created purely to reclaim the heap a reaped clock used to occupy, so
  // a dangling read lands on live data rather than on its own stale bytes.
  // Without them a use-after-free on a quiet heap still reads the old value and
  // a regression test would pass on the very bug it exists for.
  constexpr int kFillerClocks = 256;

  std::string FillerName(int i) {
    return "clip.filler" + std::to_string(i);
  }

  void MakeFillerClocks() {
    for (int i = 0; i < kFillerClocks; i++)
      YSE::CLOCK::Manager().createClock(FillerName(i), 60.f * (float)(i + 2));
  }

  void DropFillerClocks() {
    for (int i = 0; i < kFillerClocks; i++)
      YSE::CLOCK::Manager().destroyClock(FillerName(i));
    YSE::CLOCK::Manager().update(0.01f);
  }

} // namespace

TEST_SUITE("clip") {

  // ─── timing core: note-on firing ─────────────────────────────────────────

  TEST_CASE("clip: a note fires exactly once when its start crosses the window") {
    YSE::CLIP::transport t(nullptr);
    t.setLoopForTest(4.0);
    t.adoptEventsForTest({ev(1.0, 1.0, 1, 60)});

    RecordingSink s;
    t.evaluateWindow(s, 0.0, 2.0); // window (0, 2] contains beat 1
    REQUIRE(s.calls.size() == 1);
    CHECK(s.calls[0].kind == RecordingSink::NoteOn);
    CHECK(s.calls[0].channel == 1);
    CHECK(s.calls[0].pitch == 60);
    CHECK(s.calls[0].value == doctest::Approx(0.8f));
  }

  TEST_CASE("clip: a note whose start is outside the window does not fire") {
    YSE::CLIP::transport t(nullptr);
    t.setLoopForTest(4.0);
    t.adoptEventsForTest({ev(3.0, 1.0, 1, 60)});

    RecordingSink s;
    t.evaluateWindow(s, 0.0, 2.0); // beat 3 not in (0, 2]
    CHECK(s.calls.empty());
  }

  TEST_CASE("clip: the window is half-open — a start exactly on `from` waits") {
    YSE::CLIP::transport t(nullptr);
    t.setLoopForTest(4.0);
    t.adoptEventsForTest({ev(1.0, 1.0, 1, 60)});

    RecordingSink s;
    t.evaluateWindow(s, 1.0, 2.0); // from == 1.0, start 1.0 is not > from
    CHECK(s.calls.empty());
    t.evaluateWindow(s, 0.0, 1.0); // to == 1.0, start 1.0 <= to -> fires
    CHECK(s.count(RecordingSink::NoteOn) == 1);
  }

  // ─── timing core: note-off scheduling ────────────────────────────────────

  TEST_CASE("clip: a note-off fires when the note's duration elapses") {
    YSE::CLIP::transport t(nullptr);
    t.setLoopForTest(8.0);
    t.adoptEventsForTest({ev(1.0, 1.0, 1, 60)}); // on at 1, off at 2

    RecordingSink s;
    t.evaluateWindow(s, 0.0, 1.5); // fires the note-on
    CHECK(s.count(RecordingSink::NoteOn) == 1);
    CHECK(s.count(RecordingSink::NoteOff) == 0);

    t.evaluateWindow(s, 1.5, 3.0); // off-beat 2 falls in this window
    CHECK(s.count(RecordingSink::NoteOff) == 1);
    const auto& off = s.calls.back();
    CHECK(off.channel == 1);
    CHECK(off.pitch == 60);
  }

  // ─── looping ──────────────────────────────────────────────────────────────

  TEST_CASE("clip: an event repeats every loop length") {
    YSE::CLIP::transport t(nullptr);
    t.setLoopForTest(4.0);
    t.adoptEventsForTest({ev(1.0, 0.5, 1, 60)});

    RecordingSink s;
    // Beats 0.5 .. 9.0 — the event at loop-phase 1 recurs at absolute beats
    // 1, 5 and 9.
    t.evaluateWindow(s, 0.5, 9.0);
    CHECK(s.count(RecordingSink::NoteOn) == 3);
  }

  TEST_CASE("clip: firing works across a loop boundary") {
    YSE::CLIP::transport t(nullptr);
    t.setLoopForTest(4.0);
    t.adoptEventsForTest({ev(1.0, 0.5, 1, 60)});

    RecordingSink s;
    // Window (3, 6] spans the loop boundary at beat 4; the next occurrence is
    // the second loop's phase-1 hit at absolute beat 5.
    t.evaluateWindow(s, 3.0, 6.0);
    CHECK(s.count(RecordingSink::NoteOn) == 1);
  }

  TEST_CASE("clip: loop length <= 0 fires each event at most once") {
    YSE::CLIP::transport t(nullptr);
    t.setLoopForTest(0.0);
    t.adoptEventsForTest({ev(2.0, 1.0, 1, 60)});

    RecordingSink s;
    t.evaluateWindow(s, 0.0, 5.0);
    CHECK(s.count(RecordingSink::NoteOn) == 1);
    t.evaluateWindow(s, 5.0, 10.0); // no repeat
    CHECK(s.count(RecordingSink::NoteOn) == 1);
  }

  // ─── list swap survival ─────────────────────────────────────────────────

  TEST_CASE("clip: a sounding note gets its note-off even after it leaves the list") {
    YSE::CLIP::transport t(nullptr);
    t.setLoopForTest(8.0);
    t.adoptEventsForTest({ev(1.0, 2.0, 1, 60)}); // on at 1, off at 3

    RecordingSink s;
    t.evaluateWindow(s, 0.5, 1.5); // note-on
    REQUIRE(s.count(RecordingSink::NoteOn) == 1);

    // The event vanishes from the list mid-note.
    t.adoptEventsForTest({});

    t.evaluateWindow(s, 1.5, 3.5); // off-beat 3 still fires from the sounding set
    CHECK(s.count(RecordingSink::NoteOff) == 1);
    CHECK(s.calls.back().pitch == 60);
  }

  // ─── pitch bend ──────────────────────────────────────────────────────────

  TEST_CASE("clip: a per-note pitch bend is emitted just before the note-on") {
    YSE::CLIP::transport t(nullptr);
    t.setLoopForTest(4.0);
    t.adoptEventsForTest({ev(1.0, 1.0, 2, 64, 0.5f, 0.25f)});

    RecordingSink s;
    t.evaluateWindow(s, 0.5, 1.5);
    REQUIRE(s.calls.size() == 2);
    CHECK(s.calls[0].kind == RecordingSink::PitchWheel);
    CHECK(s.calls[0].channel == 2);
    CHECK(s.calls[0].value == doctest::Approx(0.25f));
    CHECK(s.calls[1].kind == RecordingSink::NoteOn);
    CHECK(s.calls[1].pitch == 64);
  }

  TEST_CASE("clip: a zero pitch bend emits no pitch-wheel message") {
    YSE::CLIP::transport t(nullptr);
    t.setLoopForTest(4.0);
    t.adoptEventsForTest({ev(1.0, 1.0, 1, 60, 0.8f, 0.f)});

    RecordingSink s;
    t.evaluateWindow(s, 0.5, 1.5);
    CHECK(s.count(RecordingSink::PitchWheel) == 0);
    CHECK(s.count(RecordingSink::NoteOn) == 1);
  }

  // ─── releaseAll (stop) ───────────────────────────────────────────────────

  TEST_CASE("clip: releaseAll emits a note-off for everything sounding") {
    YSE::CLIP::transport t(nullptr);
    t.setLoopForTest(8.0);
    t.adoptEventsForTest({ev(1.0, 4.0, 1, 60), ev(1.0, 4.0, 1, 64)});

    RecordingSink s;
    t.evaluateWindow(s, 0.5, 1.5); // both notes on
    REQUIRE(s.count(RecordingSink::NoteOn) == 2);

    t.releaseAll(s);
    CHECK(s.count(RecordingSink::NoteOff) == 2);

    // Idempotent: nothing left sounding.
    RecordingSink s2;
    t.releaseAll(s2);
    CHECK(s2.calls.empty());
  }

  // ─── manager + clock integration ─────────────────────────────────────────

  TEST_CASE("clip: bind resolves a live clock and rejects an unknown name") {
    auto& clocks = YSE::CLOCK::Manager();
    REQUIRE(clocks.createClock("clip.bind", 120.f));

    YSE::CLIP::transport t(nullptr);
    CHECK(t.bind("clip.bind"));
    CHECK_FALSE(t.bind("clip.bind.nope"));

    clocks.destroyClock("clip.bind");
    clocks.update(0.01f); // let the audio side retire it
  }

  TEST_CASE("clip: a bound transport survives destroyClock of its clock (#707)") {
    // Issue #707: destroyClock retired the clock, the slow pool freed it, and
    // every bound transport went on dereferencing it in advance() — on the
    // audio thread, every block. The transport cannot notice: isReleased() is
    // itself a load through the dangling pointer. So binding now takes a share
    // of the clock's lifetime and a destroyed clock merely stops advancing.
    //
    // What this case asserts directly is that the sequence completes and the
    // transport keeps running; the *memory* claim is asserted by the asan/tsan
    // CI jobs, for which this is the reproduction — every advance() below is a
    // read that used to be a read of freed memory. The firing consequence is
    // pinned in the MIDI-out case further down, which is this file's only seam
    // onto what advance() actually does.
    auto& clocks = YSE::CLOCK::Manager();
    REQUIRE(clocks.createClock("clip.doomed", 60.f)); // 1 beat / second

    YSE::CLIP::transport t(nullptr);
    REQUIRE(t.bind("clip.doomed"));
    t.setLoopForTest(0.0);
    t.setEvents({ev(1.0, 100.0, 1, 60)});
    t.play();

    for (int i = 0; i < 2; ++i) {
      clocks.update(0.25f);
      t.advance();
    }
    CHECK(t.isPlaying());

    REQUIRE(ReapClock("clip.doomed"));
    CHECK_FALSE(clocks.clockExists("clip.doomed"));

    // Reclaim the heap the reap would have freed, and add a same-named
    // replacement: every advance() below is then a read of memory that is
    // definitely somebody else's if the transport is still dangling.
    MakeFillerClocks();
    REQUIRE(clocks.createClock("clip.doomed", 60.f));
    for (int i = 0; i < 16; ++i) {
      clocks.update(0.25f);
      t.advance();
    }
    CHECK(t.isPlaying());
    CHECK(clocks.beatPosition("clip.doomed") == doctest::Approx(4.0));

    t.stop();
    clocks.update(0.25f);
    t.advance();
    CHECK_FALSE(t.isPlaying());

    REQUIRE(ReapClock("clip.doomed"));
    DropFillerClocks();
  }

  TEST_CASE("clip: create -> play -> stop through the manager and a real clock") {
    auto& clocks = YSE::CLOCK::Manager();
    REQUIRE(clocks.createClock("clip.run", 60.f)); // 60 BPM -> 1 beat / second

    YSE::clip c;
    REQUIRE(c.create("clip.run"));
    c.loopLength(4.0);
    c.setEvents({ev(1.0, 1.0, 1, 60), ev(2.0, 1.0, 1, 64)});
    CHECK_FALSE(c.isPlaying());

    c.play();
    // Drive a few blocks: clocks first (as in deviceManager::doOnCallback), then
    // the clip transports. This exercises the create->inbox->inUse handoff, the
    // bind, and advance() reading the clock — no synth attached, so we only
    // assert the transport survives and reports the right play state.
    for (int i = 0; i < 8; ++i) {
      clocks.update(0.25f);
      YSE::CLIP::Manager().update();
    }
    CHECK(c.isPlaying());
    CHECK(clocks.beatPosition("clip.run") > 0.0);

    c.stop();
    clocks.update(0.25f);
    YSE::CLIP::Manager().update();
    CHECK_FALSE(c.isPlaying());

    clocks.destroyClock("clip.run");
    clocks.update(0.01f);
  }

#if YSE_ENABLE_MIDI_DEVICE

  // ─── external MIDI-out sink (issue #350) ──────────────────────────────────
  //
  // The transport's audio-thread side stamps fired events with the block's
  // absolute send time and try_pushes them onto MIDI::OutSender()'s bounded
  // queue; the dedicated sender thread performs the send. The test send-hook
  // intercepts before any RtMidi call, so the "port" below is a dummy pointer
  // that is stored and compared but never dereferenced.

  struct MidiHookRecorder {
    struct Entry {
      YSE::MIDI::outEvent event;
    };
    std::mutex m;
    std::vector<Entry> entries;

    static void hook(const YSE::MIDI::outEvent& e, void* user) {
      auto* self = static_cast<MidiHookRecorder*>(user);
      std::scoped_lock lk(self->m);
      self->entries.push_back({e});
    }

    std::size_t count() {
      std::scoped_lock lk(m);
      return entries.size();
    }
    Entry at(std::size_t i) {
      std::scoped_lock lk(m);
      return entries.at(i);
    }
    bool await(std::size_t n, int timeoutMs = 5000) {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
      while (std::chrono::steady_clock::now() < deadline) {
        if (count() >= n) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      return count() >= n;
    }
  };

  TEST_CASE("clip: a connected MIDI-out port receives time-stamped note bytes") {
    auto& clocks = YSE::CLOCK::Manager();
    REQUIRE(clocks.createClock("clip.midiout", 60.f)); // 1 beat / second

    MidiHookRecorder rec;
    auto& sender = YSE::MIDI::OutSender();
    sender.setSendHookForTest(&MidiHookRecorder::hook, &rec);

    int dummy = 0; // never dereferenced — the hook intercepts before the send
    auto* fakePort = reinterpret_cast<RtMidiOut*>(&dummy);

    {
      YSE::CLIP::transport t(nullptr);
      REQUIRE(t.bind("clip.midiout"));
      t.connectMidiOut(fakePort); // lazily starts the sender thread
      CHECK(sender.isRunning());
      t.setLoopForTest(0.0);
      t.setEvents({ev(1.0, 1.0, 1, 60)}); // on at beat 1, off at beat 2
      t.play();

      // Drive blocks like deviceManager::doOnCallback: clock first, then the
      // transport. 0.25 s per update at 60 BPM = 0.25 beats per block.
      for (int i = 0; i < 6; ++i) { // -> beat 1.5: the note-on has fired
        clocks.update(0.25f);
        t.advance();
      }
      REQUIRE(rec.await(1));
      auto on = rec.at(0);
      CHECK(on.event.port == fakePort);
      CHECK(on.event.bytes[0] == 0x90); // channel 1 -> status nibble 0
      CHECK(on.event.bytes[1] == 60);
      CHECK(on.event.bytes[2] == 102); // round(0.8 * 127)
      CHECK(on.event.dueNs > 0);

      for (int i = 0; i < 4; ++i) { // -> beat 2.5: the note-off has fired
        clocks.update(0.25f);
        t.advance();
      }
      REQUIRE(rec.await(2));
      auto off = rec.at(1);
      CHECK(off.event.bytes[0] == 0x80);
      CHECK(off.event.bytes[1] == 60);
      // Deadlines are paced forward one block at a time — never backwards.
      CHECK(off.event.dueNs >= on.event.dueNs);
    }

    sender.setSendHookForTest(nullptr, nullptr);
    sender.stop();
    clocks.destroyClock("clip.midiout");
    clocks.update(0.01f);
  }

  TEST_CASE("clip: stopping a clip releases sounding notes to the MIDI-out sink") {
    auto& clocks = YSE::CLOCK::Manager();
    REQUIRE(clocks.createClock("clip.midiout.stop", 60.f));

    MidiHookRecorder rec;
    auto& sender = YSE::MIDI::OutSender();
    sender.setSendHookForTest(&MidiHookRecorder::hook, &rec);

    int dummy = 0;
    auto* fakePort = reinterpret_cast<RtMidiOut*>(&dummy);

    {
      YSE::CLIP::transport t(nullptr);
      REQUIRE(t.bind("clip.midiout.stop"));
      t.connectMidiOut(fakePort);
      t.setLoopForTest(0.0);
      t.setEvents({ev(0.5, 100.0, 1, 64)}); // long note: still sounding at stop
      t.play();

      for (int i = 0; i < 4; ++i) { // -> beat 1: note-on fired, off at beat 100.5
        clocks.update(0.25f);
        t.advance();
      }
      REQUIRE(rec.await(1));
      CHECK(rec.at(0).event.bytes[0] == 0x90);

      t.stop();
      clocks.update(0.25f);
      t.advance(); // releaseAll runs through the same sink

      REQUIRE(rec.await(2));
      CHECK(rec.at(1).event.bytes[0] == 0x80);
      CHECK(rec.at(1).event.bytes[1] == 64);

      // disconnect: further playback reaches no port.
      t.disconnectMidiOut(fakePort);
      t.play();
      for (int i = 0; i < 8; ++i) {
        clocks.update(0.25f);
        t.advance();
      }
      t.stop();
      clocks.update(0.25f);
      t.advance();
      CHECK(rec.count() == 2);
    }

    sender.setSendHookForTest(nullptr, nullptr);
    sender.stop();
    clocks.destroyClock("clip.midiout.stop");
    clocks.update(0.01f);
  }

  TEST_CASE("clip: a transport on a destroyed clock stops firing rather than dangling (#707)") {
    // The behavioural half of #707. The transport is bound to a clock that is
    // then destroyed and reaped, and a *new* clock of the same name is run well
    // past a later event. If the binding had followed the freed clock's
    // recycled memory, the beat would jump and that later event would fire.
    // With the binding owning a share, the clock it holds simply stopped: the
    // beat is frozen, the window never advances, nothing more comes out.
    auto& clocks = YSE::CLOCK::Manager();
    REQUIRE(clocks.createClock("clip.midiout.doomed", 60.f)); // 1 beat / second

    MidiHookRecorder rec;
    auto& sender = YSE::MIDI::OutSender();
    sender.setSendHookForTest(&MidiHookRecorder::hook, &rec);

    int dummy = 0;
    auto* fakePort = reinterpret_cast<RtMidiOut*>(&dummy);

    {
      YSE::CLIP::transport t(nullptr);
      REQUIRE(t.bind("clip.midiout.doomed"));
      t.connectMidiOut(fakePort);
      t.setLoopForTest(0.0);
      // Beat 1 fires while the clock is alive (the anchor that proves the pipe
      // works); beat 5 must never fire. Both notes are long enough that no
      // note-off is due either.
      t.setEvents({ev(1.0, 100.0, 1, 60), ev(5.0, 100.0, 1, 62)});
      t.play();

      for (int i = 0; i < 6; ++i) { // -> beat 1.5
        clocks.update(0.25f);
        t.advance();
      }
      REQUIRE(rec.await(1));
      CHECK(rec.at(0).event.bytes[1] == 60);

      REQUIRE(ReapClock("clip.midiout.doomed"));
      // Reclaim the reaped clock's heap, then run a same-named replacement well
      // past the beat-5 event. A transport still reading the freed clock sees a
      // beat that jumps, and fires.
      MakeFillerClocks();
      REQUIRE(clocks.createClock("clip.midiout.doomed", 60.f));

      // Eight beats on the replacement — three times past the beat-5 event.
      for (int i = 0; i < 32; ++i) {
        clocks.update(0.25f);
        t.advance();
      }
      CHECK(clocks.beatPosition("clip.midiout.doomed") == doctest::Approx(8.0));
      CHECK_FALSE(rec.await(2, 200));
      CHECK(rec.count() == 1);

      t.stop();
      clocks.update(0.25f);
      t.advance(); // releaseAll: the beat-1 note is still sounding
      REQUIRE(rec.await(2));
      CHECK(rec.at(1).event.bytes[0] == 0x80);
      CHECK(rec.at(1).event.bytes[1] == 60);
      t.disconnectMidiOut(fakePort);
    }

    sender.setSendHookForTest(nullptr, nullptr);
    sender.stop();
    REQUIRE(ReapClock("clip.midiout.doomed"));
    DropFillerClocks();
  }

#endif // YSE_ENABLE_MIDI_DEVICE

} // TEST_SUITE("clip")
