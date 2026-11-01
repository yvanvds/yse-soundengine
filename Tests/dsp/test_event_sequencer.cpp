// Tests for YSE::DSP::eventSequencer (YseEngine/dsp/generators/
// eventSequencer.hpp), the class behind ~seq (issue #1069). The driving
// phase is built by hand, sample by sample, so which sample crosses an
// event is known exactly.

#include <doctest/doctest.h>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#include "dsp/generators/eventSequencer.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::DSP::buffer;
using YSE::DSP::eventSequencer;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr float NaN = std::numeric_limits<float>::quiet_NaN();

  bool Add(eventSequencer& seq, Int sequence, float phase, const char* text) {
    return seq.add(sequence, phase, text, static_cast<UInt>(std::strlen(text)));
  }

  // A ramp: sample i is start + i * step, wrapped into 0..1.
  void Ramp(buffer& b, float start, float step, unsigned int length = N) {
    b.resize(length);
    for (unsigned int i = 0; i < length; ++i) {
      const float p = start + static_cast<float>(i) * step;
      b.getPtr()[i] = p - std::floor(p);
    }
  }

  // What the last block fired, as "text@offset".
  std::vector<std::string> Fired(const eventSequencer& seq) {
    std::vector<std::string> out;
    for (UInt i = 0; i < seq.fired(); ++i) {
      const eventSequencer::firing& f = seq.firedEvent(i);
      out.push_back(std::string(f.text, f.length) + "@" + std::to_string(f.offset));
    }
    return out;
  }

  using List = std::vector<std::string>;
} // namespace

TEST_SUITE("dsp") {

  TEST_CASE("eventSequencer: defaults (#1069)") {
    eventSequencer seq;
    CHECK(seq.capacity() == eventSequencer::DEFAULT_EVENTS);
    CHECK(seq.firedCapacity() == eventSequencer::DEFAULT_FIRED);
    CHECK(seq.size() == 0);
    CHECK(seq.selected() == 0);
    CHECK_FALSE(seq.playing());
    CHECK_FALSE(seq.recording());
    CHECK_FALSE(seq.overdubbing());
    CHECK(seq.phase() == 0.f);
    CHECK(seq.fired() == 0);
    CHECK(seq.missed() == 0);
    CHECK(std::string(seq.eventText(0)).empty());

    eventSequencer tiny(0, 0);
    CHECK(tiny.capacity() == 1);
    CHECK(tiny.firedCapacity() == 1);
    eventSequencer huge(eventSequencer::MAX_EVENTS + 1, 4);
    CHECK(huge.capacity() == eventSequencer::MAX_EVENTS);
  }

  TEST_CASE("eventSequencer: events sort by sequence, phase and the order stored (#1069)") {
    eventSequencer seq;
    CHECK(Add(seq, 1, 0.5f, "b"));
    CHECK(Add(seq, 0, 0.75f, "z"));
    CHECK(Add(seq, 1, 0.25f, "a"));
    CHECK(Add(seq, 1, 0.5f, "c"));
    CHECK(Add(seq, -2, 0.f, "neg"));
    REQUIRE(seq.size() == 5);
    const char* texts[] = {"neg", "z", "a", "b", "c"};
    const Int sequences[] = {-2, 0, 1, 1, 1};
    const float phases[] = {0.f, 0.75f, 0.25f, 0.5f, 0.5f};
    for (UInt i = 0; i < 5; ++i) {
      CAPTURE(i);
      CHECK(std::string(seq.eventText(i)) == texts[i]);
      CHECK(seq.eventLength(i) == std::strlen(texts[i]));
      CHECK(seq.eventSequence(i) == sequences[i]);
      CHECK(seq.eventPhase(i) == phases[i]);
    }
    CHECK(seq.count(1) == 3);
    CHECK(seq.count(0) == 1);
    CHECK(seq.count(7) == 0);
  }

  TEST_CASE("eventSequencer: refused events change nothing (#1069)") {
    eventSequencer seq(2);
    CHECK_FALSE(Add(seq, 0, NaN, "x"));
    CHECK_FALSE(Add(seq, 0, INFINITY, "x"));
    CHECK_FALSE(Add(seq, 0, 0.5f, ""));
    CHECK_FALSE(seq.add(0, 0.5f, nullptr, 3));
    CHECK_FALSE(Add(seq, 0, 0.5f, "two\nlines"));
    CHECK_FALSE(Add(seq, 0, 0.5f, "cr\r"));
    CHECK_FALSE(seq.add(0, 0.5f, "a\0b", 3));
    const std::string longest(eventSequencer::TEXT_CAPACITY - 1, 'x');
    const std::string tooLong(eventSequencer::TEXT_CAPACITY, 'x');
    CHECK_FALSE(seq.add(0, 0.5f, tooLong.c_str(), static_cast<UInt>(tooLong.size())));
    CHECK(seq.size() == 0);
    CHECK(seq.add(0, 0.5f, longest.c_str(), static_cast<UInt>(longest.size())));
    CHECK(Add(seq, 0, 0.6f, "second"));
    CHECK_FALSE(Add(seq, 0, 0.7f, "full"));
    CHECK(seq.size() == 2);
    CHECK(std::string(seq.eventText(0)) == longest);
  }

  TEST_CASE("eventSequencer: plays the current sequence where the ramp crosses each event, "
            "sample-accurately (#1069)") {
    eventSequencer seq;
    Add(seq, 0, 0.25f, "quarter");
    Add(seq, 0, 0.5f, "half");
    Add(seq, 0, 0.5f, "half2");
    Add(seq, 1, 0.5f, "other");
    buffer in(N);

    // Not playing: nothing fires.
    Ramp(in, 0.f, 1.f / N);
    seq(in);
    CHECK(seq.fired() == 0);
    CHECK(seq.phase() == doctest::Approx(127.f / 128.f));

    // Playing: one cycle per block. Sample 32 is 0.25 exactly, 64 is 0.5.
    seq.play(true);
    Ramp(in, 0.f, 1.f / N);
    seq(in);
    CHECK(Fired(seq) == List{"quarter@32", "half@64", "half2@64"});
    seq(in);
    CHECK(Fired(seq) == List{"quarter@32", "half@64", "half2@64"});

    // Another sequence.
    seq.select(1);
    CHECK(seq.selected() == 1);
    seq(in);
    CHECK(Fired(seq) == List{"other@64"});

    // Stopped: nothing.
    seq.play(false);
    seq(in);
    CHECK(seq.fired() == 0);
  }

  TEST_CASE("eventSequencer: a wrap fires the end of the cycle and the start of the next; a "
            "small fall fires nothing (#1069)") {
    eventSequencer seq;
    Add(seq, 0, 0.f, "zero");
    Add(seq, 0, 0.95f, "late");
    Add(seq, 0, 1.f, "one");
    buffer in(4);
    in.getPtr()[0] = 0.9f;
    in.getPtr()[1] = 0.92f;
    in.getPtr()[2] = 0.02f; // wraps past 0.95, 1 and 0
    in.getPtr()[3] = 0.03f;
    seq.play(true);
    seq(in, 4);
    CHECK(Fired(seq) == List{"late@2", "one@2", "zero@2"});

    // Back by less than half a cycle: nothing; forward again re-crosses.
    // 0.03 -> 0.97 rises over late; 0.97 -> 0.94 falls (nothing); 0.94 ->
    // 0.96 crosses late again; 0.96 -> 0.9 falls.
    in.getPtr()[0] = 0.97f;
    in.getPtr()[1] = 0.94f;
    in.getPtr()[2] = 0.96f;
    in.getPtr()[3] = 0.9f;
    seq(in, 4);
    CHECK(Fired(seq) == List{"late@0", "late@2"});
  }

  TEST_CASE("eventSequencer: play starts from the current phase, its events included (#1069)") {
    eventSequencer seq;
    Add(seq, 0, 0.f, "start");
    Add(seq, 0, 0.1f, "early");
    Add(seq, 0, 0.6f, "late");
    buffer in(N);
    Ramp(in, 0.f, 0.5f / N); // 0 to just under 0.5
    seq.play(true);
    seq(in);
    CHECK(Fired(seq) == List{"start@0", "early@26"});

    // Restarting mid-cycle: only what is ahead (and exactly here).
    seq.play(false);
    Ramp(in, 0.5f, 0.f, 4); // holds 0.5
    seq(in, 4);
    seq.play(true);
    in.getPtr()[0] = 0.6f;
    in.getPtr()[1] = 0.7f;
    in.getPtr()[2] = 0.8f;
    in.getPtr()[3] = 0.9f;
    seq(in, 4);
    CHECK(Fired(seq) == List{"late@0"});
  }

  TEST_CASE("eventSequencer: remove by range and text, clear and clear all (#1069)") {
    eventSequencer seq;
    Add(seq, 0, 0.1f, "a");
    Add(seq, 0, 0.2f, "b");
    Add(seq, 0, 0.3f, "a");
    Add(seq, 0, 0.4f, "c");
    Add(seq, 1, 0.2f, "b");
    CHECK(seq.remove(0, 0.2f, 0.2f) == 1); // one phase
    CHECK(seq.count(0) == 3);
    CHECK(seq.remove(0, 1.f, 0.f, "a", 1) == 2); // the text, anywhere
    CHECK(seq.count(0) == 1);
    CHECK(std::string(seq.eventText(0)) == "c");
    CHECK(seq.remove(0, NaN, 1.f) == 0);
    CHECK(seq.remove(0, 0.f, 0.3f) == 0);
    CHECK(seq.clear(1) == 1);
    CHECK(seq.size() == 1);
    seq.clearAll();
    CHECK(seq.size() == 0);

    // The freed slots are used again, up to the capacity.
    eventSequencer small(3);
    for (int round = 0; round < 4; ++round) {
      CHECK(Add(small, 0, 0.1f, "x"));
      CHECK(Add(small, 0, 0.2f, "yy"));
      CHECK(Add(small, 0, 0.3f, "zzz"));
      CHECK_FALSE(Add(small, 0, 0.4f, "w"));
      CHECK(std::string(small.eventText(2)) == "zzz");
      CHECK(small.clear(0) == 3);
    }
  }

  TEST_CASE("eventSequencer: edits while playing take effect from the current phase (#1069)") {
    eventSequencer seq;
    Add(seq, 0, 0.75f, "late");
    buffer in(N);
    seq.play(true);
    Ramp(in, 0.f, 0.5f / N); // 0 .. 0.5
    seq(in);
    CHECK(seq.fired() == 0);
    Add(seq, 0, 0.25f, "behind"); // already passed this cycle
    Add(seq, 0, 0.625f, "ahead");
    Ramp(in, 0.5f, 0.5f / N); // 0.5 .. 1
    seq(in);
    CHECK(Fired(seq) == List{"ahead@32", "late@64"});
    seq.remove(0, 0.75f, 0.75f);
    Ramp(in, 0.f, 1.f / N);
    seq(in);
    CHECK(Fired(seq) == List{"behind@32", "ahead@80"});
  }

  TEST_CASE("eventSequencer: record empties the current sequence, overdub keeps it, both "
            "store at the last phase and fire next time round (#1069)") {
    eventSequencer seq;
    Add(seq, 0, 0.5f, "old");
    Add(seq, 2, 0.5f, "kept");
    CHECK_FALSE(seq.recordEvent("x", 1)); // not recording
    buffer in(N);
    Ramp(in, 0.f, 0.25f / N); // ends just under 0.25
    seq(in);
    const float at = seq.phase();

    seq.overdub(true);
    CHECK(seq.overdubbing());
    CHECK(seq.recordEvent("dub", 3));
    CHECK(seq.count(0) == 2);
    CHECK(seq.eventPhase(0) == at);
    seq.overdub(false);

    seq.record(true);
    CHECK(seq.recording());
    CHECK(seq.count(0) == 0); // emptied
    CHECK(seq.count(2) == 1); // other sequences untouched
    CHECK(seq.recordEvent("rec", 3));
    seq.play(true);
    // Play starts here, where the event was recorded: it fires now, as an
    // event at the starting phase does.
    in.getPtr()[0] = at;
    seq(in, 1);
    CHECK(Fired(seq) == List{"rec@0"});
    // Recorded while playing: not echoed at once, but next time round.
    CHECK(seq.recordEvent("echo", 4));
    Ramp(in, at, 1.f / N); // round to just short of it
    seq(in);
    CHECK(seq.fired() == 0);
    Ramp(in, at - 0.1f, 1.f / N); // and over it
    seq(in);
    REQUIRE(seq.fired() == 2);
    CHECK(std::string(seq.firedEvent(0).text) == "rec");
    CHECK(std::string(seq.firedEvent(1).text) == "echo");
    CHECK(seq.firedEvent(0).offset == seq.firedEvent(1).offset);
    seq.record(false);
    CHECK_FALSE(seq.recordEvent("late", 4));
  }

  TEST_CASE("eventSequencer: more events than the fired list holds are counted (#1069)") {
    eventSequencer seq(16, 2);
    Add(seq, 0, 0.5f, "a");
    Add(seq, 0, 0.5f, "b");
    Add(seq, 0, 0.5f, "c");
    buffer in(N);
    Ramp(in, 0.f, 1.f / N);
    seq.play(true);
    seq(in);
    CHECK(Fired(seq) == List{"a@64", "b@64"});
    CHECK(seq.missed() == 1);
    Ramp(in, 0.f, 0.25f / N);
    seq(in);
    CHECK(seq.missed() == 0);
  }

  TEST_CASE("eventSequencer: non-finite samples hold the phase; any block length; no sample "
            "rate (#1069)") {
    eventSequencer seq;
    Add(seq, 0, 0.5f, "half");
    seq.play(true);
    buffer in(N);
    Ramp(in, 0.f, 0.25f / N, 2);
    seq(in, 2);
    in.getPtr()[0] = NaN;
    in.getPtr()[1] = INFINITY;
    seq(in, 2);
    CHECK(seq.phase() == doctest::Approx(0.25f / N));
    CHECK(seq.fired() == 0);

    // The same ramp in blocks of 64, 13 and 1 fires on the same sample.
    for (unsigned int block : {64u, 13u, 1u}) {
      CAPTURE(block);
      ScopedSampleRate rate(block == 13u ? 1000 : 96000);
      eventSequencer s;
      Add(s, 0, 0.5f, "half");
      s.play(true);
      std::vector<unsigned int> at;
      unsigned int done = 0;
      while (done < N) {
        const unsigned int length = N - done < block ? N - done : block;
        buffer b(length);
        for (unsigned int i = 0; i < length; ++i)
          b.getPtr()[i] = static_cast<float>(done + i) / N;
        s(b, length);
        for (UInt k = 0; k < s.fired(); ++k)
          at.push_back(done + s.firedEvent(k).offset);
        done += length;
      }
      CHECK(at == std::vector<unsigned int>{64});
    }

    // A block longer than the standard size is processed up to it.
    buffer longer(N * 2);
    for (unsigned int i = 0; i < N * 2; ++i)
      longer.getPtr()[i] = 0.25f;
    seq(longer, N * 2);
    CHECK(seq.phase() == 0.25f);
  }

  TEST_CASE("eventSequencer: reset (#1069)") {
    eventSequencer seq(8, 4);
    Add(seq, 3, 0.5f, "x");
    seq.select(3).play(true).overdub(true);
    seq.record(true);
    buffer in(N);
    Ramp(in, 0.f, 1.f / N);
    seq(in);
    seq.reset();
    CHECK(seq.size() == 0);
    CHECK(seq.selected() == 0);
    CHECK_FALSE(seq.playing());
    CHECK_FALSE(seq.recording());
    CHECK_FALSE(seq.overdubbing());
    CHECK(seq.phase() == 0.f);
    CHECK(seq.fired() == 0);
    CHECK(seq.capacity() == 8);
    CHECK(seq.firedCapacity() == 4);
  }

  TEST_CASE("eventSequencer: edits, recording and blocks allocate nothing (#1069)") {
    eventSequencer seq;
    buffer in(N);
    Ramp(in, 0.f, 1.f / N);
    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 64; ++i)
        Add(seq, i % 3, static_cast<float>(i) / 64.f, "event 60 100");
      seq.play(true);
      seq(in);
      seq.overdub(true);
      (void)seq.recordEvent("dub", 3);
      seq.record(true);
      (void)seq.recordEvent("rec", 3);
      (void)seq.remove(1, 0.f, 0.5f, "event 60 100", 12);
      (void)seq.clear(2);
      seq.select(1);
      seq(in);
      seq.clearAll();
      seq.reset();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("dsp")
