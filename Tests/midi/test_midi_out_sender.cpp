// Tests for the clip-transport external MIDI-out sender (issue #350).
//
// The sender is the audio-thread -> sender-thread hand-off: the audio thread
// stamps encoded MIDI messages with an absolute steady_clock deadline and
// try_pushes them onto a bounded lock-free SPSC queue; a dedicated worker
// drains the queue and performs the RtMidi send when each message comes due.
//
// Everything here runs against the test send-hook seam, so no RtMidi port is
// ever opened (or dereferenced) — the suite runs headless in CI. Timing
// assertions are one-sided ("never sent before its deadline") so scheduler
// jitter on a loaded CI machine cannot flake them.

#include <doctest/doctest.h>

#if YSE_ENABLE_MIDI_DEVICE

#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include "midi/midiOutSender.h"

namespace {

  using YSE::MIDI::outEvent;

  // Thread-safe recorder for the send hook (fires on the sender thread).
  struct HookRecorder {
    struct Entry {
      outEvent event;
      std::int64_t arrivedNs;
    };
    std::mutex m;
    std::vector<Entry> entries;

    static void hook(const outEvent& e, void* user) {
      auto* self = static_cast<HookRecorder*>(user);
      std::scoped_lock lk(self->m);
      self->entries.push_back({e, YSE::MIDI::nowNs()});
    }

    std::size_t count() {
      std::scoped_lock lk(m);
      return entries.size();
    }
    Entry at(std::size_t i) {
      std::scoped_lock lk(m);
      return entries.at(i);
    }
  };

  // Poll until `recorder` holds at least `n` entries (generous CI timeout).
  bool awaitCount(HookRecorder& recorder, std::size_t n, int timeoutMs = 5000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
      if (recorder.count() >= n) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return recorder.count() >= n;
  }

} // namespace

TEST_SUITE("midi") {

  // ─── message encoders ──────────────────────────────────────────────────────

  TEST_CASE("midiout: note-on encoding maps channel/pitch/velocity to wire bytes") {
    // clipEvent channels are 1..16 -> status low nibble 0..15.
    outEvent e = YSE::MIDI::makeNoteOn(nullptr, 42, 1, 60, 0.8f);
    CHECK(e.dueNs == 42);
    CHECK(e.len == 3);
    CHECK(e.bytes[0] == 0x90);
    CHECK(e.bytes[1] == 60);
    CHECK(e.bytes[2] == 102); // round(0.8 * 127)

    e = YSE::MIDI::makeNoteOn(nullptr, 0, 16, 127, 1.f);
    CHECK(e.bytes[0] == 0x9F);
    CHECK(e.bytes[2] == 127);
  }

  TEST_CASE("midiout: note-on encoding clamps out-of-range fields") {
    outEvent e = YSE::MIDI::makeNoteOn(nullptr, 0, 0, -3, -1.f); // channel below 1
    CHECK(e.bytes[0] == 0x90);
    CHECK(e.bytes[1] == 0);
    CHECK(e.bytes[2] == 0);

    e = YSE::MIDI::makeNoteOn(nullptr, 0, 99, 200, 2.f); // channel above 16
    CHECK(e.bytes[0] == 0x9F);
    CHECK(e.bytes[1] == 127);
    CHECK(e.bytes[2] == 127);
  }

  TEST_CASE("midiout: note-off encoding uses the 0x80 status") {
    outEvent e = YSE::MIDI::makeNoteOff(nullptr, 0, 2, 64, 0.f);
    CHECK(e.bytes[0] == 0x81);
    CHECK(e.bytes[1] == 64);
    CHECK(e.bytes[2] == 0);
  }

  TEST_CASE("midiout: pitch-wheel encoding maps [-1, 1] onto the 14-bit range") {
    outEvent e = YSE::MIDI::makePitchWheel(nullptr, 0, 1, 0.f); // center
    CHECK(e.bytes[0] == 0xE0);
    CHECK(e.bytes[1] == 0x00); // 8192 & 0x7F
    CHECK(e.bytes[2] == 0x40); // 8192 >> 7

    e = YSE::MIDI::makePitchWheel(nullptr, 0, 1, 1.f); // max, clamped to 16383
    CHECK(e.bytes[1] == 0x7F);
    CHECK(e.bytes[2] == 0x7F);

    e = YSE::MIDI::makePitchWheel(nullptr, 0, 1, -1.f); // min
    CHECK(e.bytes[1] == 0x00);
    CHECK(e.bytes[2] == 0x00);
  }

  // ─── sender thread ─────────────────────────────────────────────────────────

  TEST_CASE("midiout: due messages are delivered to the sink in FIFO order") {
    HookRecorder rec;
    YSE::MIDI::outSender sender;
    sender.setSendHookForTest(&HookRecorder::hook, &rec);
    sender.start();

    const std::int64_t now = YSE::MIDI::nowNs();
    CHECK(sender.tryEnqueue(YSE::MIDI::makeNoteOn(nullptr, now, 1, 60, 0.5f)));
    CHECK(sender.tryEnqueue(YSE::MIDI::makeNoteOn(nullptr, now, 1, 62, 0.5f)));
    CHECK(sender.tryEnqueue(YSE::MIDI::makeNoteOff(nullptr, now, 1, 60, 0.f)));

    REQUIRE(awaitCount(rec, 3));
    CHECK(rec.at(0).event.bytes[1] == 60);
    CHECK(rec.at(0).event.bytes[0] == 0x90);
    CHECK(rec.at(1).event.bytes[1] == 62);
    CHECK(rec.at(2).event.bytes[0] == 0x80);

    sender.stop();
  }

  TEST_CASE("midiout: a message is never sent before its deadline") {
    HookRecorder rec;
    YSE::MIDI::outSender sender;
    sender.setSendHookForTest(&HookRecorder::hook, &rec);
    sender.start();

    const std::int64_t due = YSE::MIDI::nowNs() + 100'000'000; // now + 100 ms
    CHECK(sender.tryEnqueue(YSE::MIDI::makeNoteOn(nullptr, due, 1, 60, 0.5f)));

    REQUIRE(awaitCount(rec, 1));
    // One-sided: the worker only sends once now >= due, so arrival can never
    // precede the deadline (lateness is scheduler-dependent and not asserted).
    CHECK(rec.at(0).arrivedNs >= due);

    sender.stop();
  }

  TEST_CASE("midiout: stop() flushes messages that were not yet due") {
    HookRecorder rec;
    YSE::MIDI::outSender sender;
    sender.setSendHookForTest(&HookRecorder::hook, &rec);
    sender.start();

    // Far-future deadlines: the worker will not send these on its own.
    const std::int64_t due = YSE::MIDI::nowNs() + 3600LL * 1000000000LL;
    CHECK(sender.tryEnqueue(YSE::MIDI::makeNoteOff(nullptr, due, 1, 60, 0.f)));
    CHECK(sender.tryEnqueue(YSE::MIDI::makeNoteOff(nullptr, due, 1, 64, 0.f)));

    sender.stop(); // join + immediate flush — shutdown must not strand note-offs
    CHECK(rec.count() == 2);
    CHECK(rec.at(0).event.bytes[1] == 60);
    CHECK(rec.at(1).event.bytes[1] == 64);
  }

  TEST_CASE("midiout: the hand-off queue is bounded — overflow drops, never grows") {
    YSE::MIDI::outSender sender; // never started: nothing drains the queue
    const std::int64_t now = YSE::MIDI::nowNs();
    bool sawFull = false;
    for (int i = 0; i < 100000 && !sawFull; ++i) {
      sawFull = !sender.tryEnqueue(YSE::MIDI::makeNoteOn(nullptr, now, 1, 60, 0.5f));
    }
    // try_push must refuse (rather than allocate) once the fixed capacity is
    // reached — the audio thread relies on this being wait-free.
    CHECK(sawFull);
  }

  TEST_CASE("midiout: restart after stop spawns a working sender again") {
    HookRecorder rec;
    YSE::MIDI::outSender sender;
    sender.setSendHookForTest(&HookRecorder::hook, &rec);
    sender.start();
    sender.stop();
    CHECK_FALSE(sender.isRunning());

    sender.start();
    CHECK(sender.isRunning());
    CHECK(sender.tryEnqueue(YSE::MIDI::makeNoteOn(nullptr, YSE::MIDI::nowNs(), 1, 72, 1.f)));
    REQUIRE(awaitCount(rec, 1));
    CHECK(rec.at(0).event.bytes[1] == 72);
    sender.stop();
  }

} // TEST_SUITE("midi")

#endif // YSE_ENABLE_MIDI_DEVICE
