// Tests for .mtr (issue #501) — the patcher's multi-track message recorder.
//
// .mtr is .qlist's sibling and the suite is shaped by the difference. A cue
// list is *written*, so its whole surface can be tested standalone; this one is
// *recorded*, so half of what it does only exists on a clock. Hence two layers,
// and they are not interchangeable:
//
//   - **standalone cases** pin the grammar and the tape: what is a command and
//     what is data, which track a message reaches, what `record` / `next` /
//     `rewind` / `clear` / `mute` do to a cursor, what leaves an outlet in what
//     kind, and what survives a save. A standalone object has no patcher and so
//     no clock at all, which makes every recorded delta 0 — perfect for testing
//     everything *except* the timing.
//
//   - **patcher cases** pin the clock, which cannot exist standalone: that a
//     gap really is measured off the block counter, that playback really waits
//     it out again, that `timescale 200` really halves the wait, that `first`
//     really delays the start, that `mute` keeps the clock while silencing the
//     send, and that two tracks really run independently. Deadlines are
//     asserted through messageScheduler::BlocksForMillis / MillisForBlocks at
//     the live SAMPLERATE rather than through hard-coded block counts, so the
//     suite holds at any negotiated rate.
//
// Six rules carry the file, each of them something a plausible implementation
// gets backwards without ever crashing:
//
//   - **record and playback share one clock.** The gap is read off the same
//     block counter the wait is armed against. An implementation that recorded
//     off a wall clock passes every standalone test and then plays back at the
//     wrong speed on a patcher whose engine was ever paused.
//   - **the first delta is measured from the `record` message**, not from the
//     first event. That is what gives Max's `delay` something to overwrite.
//   - **`play` rewinds.** Max: "plays back all messages recorded earlier."
//     A play that resumed would make stop-then-play a continue.
//   - **`timescale` divides, and `play` resets it to 100.** Both halves are
//     Max's, the second one surprising enough that only a test keeps it.
//   - **`mute` keeps playing.** Max: "still continuing to 'play'." A mute that
//     stopped the clock would leave the tape where it was silenced.
//   - **nothing is saved until `embed 1`.** Max 5 said mtr could not embed at
//     all; Max 8 added the flag. The family rule is save iff Max gives a save
//     flag, so the flag is the whole of it.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "headers/constants.hpp"
#include "patcher/genericObjects/gMtr.h"
#include "patcher/inlet.h"
#include "patcher/io/fileScheduler.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "patcher/time/messageScheduler.h"

using TestHelpers::Wire;
using YSE::PATCHER::gMtr;
using YSE::PATCHER::messageScheduler;
using YSE::PATCHER::patcherImplementation;

namespace {

  // Records every value it receives, in order and with its kind. Both matter:
  // a tape is a *sequence*, and an event recorded as the int 60 has to come
  // back as an int rather than as a one-element list.
  struct Recorder : YSE::PATCHER::pObject {
    // "i60", "f60.50", "s1 0 0", "!" for a bang — one string per send.
    std::vector<std::string> seen;

    Recorder() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { seen.emplace_back("!"); });
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { seen.push_back("i" + std::to_string(v)); });
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
        char text[32];
        std::snprintf(text, sizeof(text), "f%.2f", (double)v);
        seen.emplace_back(text);
      });
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { seen.push_back("s" + v); });
    }
    const char* Type() const override {
      return "mtr_recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    void reset() {
      seen.clear();
    }
  };

  std::string Joined(const std::vector<std::string>& seen) {
    std::string all;
    for (std::size_t i = 0; i < seen.size(); i++) {
      if (i > 0) all += ",";
      all += seen[i];
    }
    return all;
  }

  // A standalone .mtr with a recorder on every outlet. Standalone means no
  // patcher, so no scheduler and no clock: every delta records as 0 and
  // playback walks straight through, which is exactly what makes this rig the
  // right place to test everything that is not timing.
  struct Rig {
    gMtr obj;
    std::vector<std::unique_ptr<Recorder>> outs;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) obj.SetParams(args);
      for (int i = 0; i < obj.NumOutputs(); i++) {
        outs.emplace_back(new Recorder());
        Wire(obj, i, *outs.back());
      }
    }

    void List(int inlet, const std::string& message) {
      obj.GetInlet(inlet)->SetList(message, YSE::T_GUI);
    }
    void Int(int inlet, int value) {
      obj.GetInlet(inlet)->SetInt(value, YSE::T_GUI);
    }
    void Float(int inlet, float value) {
      obj.GetInlet(inlet)->SetFloat(value, YSE::T_GUI);
    }
    void Bang(int inlet) {
      obj.GetInlet(inlet)->SetBang(YSE::T_GUI);
    }
    // Outlet `track` (1-based), which is where track `track` plays back.
    const std::string Out(int index) {
      return Joined(outs[(std::size_t)index]->seen);
    }
    void reset() {
      for (auto& out : outs)
        out->reset();
    }
  };

  // A whole tape as one string, so an assertion reads as the recording it made
  // rather than as a pile of index lookups.
  std::string Tape(const gMtr& obj, int track) {
    std::string all;
    for (std::size_t i = 0; i < obj.Count(track); i++) {
      if (i > 0) all += " | ";
      all += obj.EventAt(track, i);
    }
    return all;
  }

  // ─── file helpers (issue #691) ────────────────────────────────────────────

  // A path in the system temp directory, deleted first so a leftover from an
  // earlier run cannot make a test pass for the wrong reason.
  std::string TempFile(const char* name) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path.string();
  }

  void WriteWholeFile(const std::string& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary);
    out << contents;
  }

  std::string ReadWholeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }

  void Remove(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
  }

  // Drive a patcher until its file requests have landed. The two halves are
  // deterministic for different reasons: WaitIdle joins the background pool's
  // jobs (so no sleep and no polling), and the single Calculate is the dispatch
  // frame the completions are handed out in — there is deliberately no other way
  // for them to arrive.
  void SettleFiles(patcherImplementation& p) {
    YSE::PATCHER::fileScheduler* io = p.FileIO();
    REQUIRE(io != nullptr);
    io->WaitIdle();
    p.Calculate(YSE::T_DSP);
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("mtr: creatable through the registry (#501)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_MTR);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".mtr");
  }

  TEST_CASE("mtr: listed by pRegistry::AllNames (#501)") {
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".mtr")) != names.end());
  }

  TEST_CASE("mtr: no argument is one track, and one track is two of each port (#501)") {
    // Max: "if there is no argument, there will be only one track", and "the
    // number of tracks determines the number of inlets and outlets in addition
    // to the leftmost inlet and outlet".
    gMtr obj;
    CHECK(obj.TrackCount() == 1);
    CHECK(obj.NumInputs() == 2);
    // Max's N+1, plus the file outlet appended after them (#691), which Max has
    // not got — see the outlet's documentation for why it is here anyway.
    CHECK(obj.NumOutputs() == 3);
    CHECK(obj.FileOutlet() == 2);
    CHECK(obj.GetCategory() == YSE::PATCHER::pCategory::TIME);
  }

  TEST_CASE("mtr: the argument builds one inlet and one outlet per track (#501)") {
    gMtr obj;
    obj.SetParams("4");
    CHECK(obj.TrackCount() == 4);
    CHECK(obj.NumInputs() == 5);
    // Four track outlets, the report outlet, and the file outlet last (#691).
    CHECK(obj.NumOutputs() == 6);
    CHECK(obj.FileOutlet() == 5);
  }

  TEST_CASE("mtr: the track count is clamped to Max's 1-32 (#501)") {
    // Max: "up to 32 tracks are possible." The lower end is Max's no-argument
    // shape, and the upper is also what the shared pending set can afford.
    gMtr high;
    high.SetParams("99");
    CHECK(high.TrackCount() == gMtr::MAX_TRACKS);

    gMtr low;
    low.SetParams("0");
    CHECK(low.TrackCount() == gMtr::MIN_TRACKS);

    gMtr negative;
    negative.SetParams("-3");
    CHECK(negative.TrackCount() == gMtr::MIN_TRACKS);
  }

  TEST_CASE("mtr: clearing the parameters restores the no-argument shape (#501)") {
    gMtr obj;
    obj.SetParams("6");
    REQUIRE(obj.TrackCount() == 6);
    obj.SetParams("");
    CHECK(obj.TrackCount() == 1);
    CHECK(obj.NumInputs() == 2);
  }

  TEST_CASE("mtr: a fresh object is empty, stopped and at the original speed (#501)") {
    gMtr obj;
    CHECK(obj.Count(0) == 0);
    CHECK(obj.Position(0) == 0);
    CHECK_FALSE(obj.IsRecording(0));
    CHECK_FALSE(obj.IsPlaying(0));
    CHECK_FALSE(obj.IsMuted(0));
    CHECK(obj.Timescale(0) == gMtr::DEFAULT_TIMESCALE);
    CHECK(obj.First() == 0);
    CHECK_FALSE(obj.Embeds());
  }

  // ─── recording ──────────────────────────────────────────────────────────────

  TEST_CASE("mtr: 'record' stores what arrives in a track inlet (#501)") {
    // Max: "begins recording all messages received in the other inlets."
    Rig rig("2");
    rig.List(0, "record");
    CHECK(rig.obj.IsRecording(0));
    CHECK(rig.obj.IsRecording(1));

    rig.Int(1, 60);
    rig.Float(1, 60.5f);
    rig.List(1, "note 60 100");
    rig.Bang(1);

    // Standalone, so there is no clock and every delta is 0. The point here is
    // *what* was stored, in order and with its kind intact.
    CHECK(Tape(rig.obj, 0) == "0 60 | 0 60.5 | 0 note 60 100 | 0 bang");
    CHECK(rig.obj.Count(1) == 0);
  }

  TEST_CASE("mtr: each track inlet records only its own track (#501)") {
    Rig rig("3");
    rig.List(0, "record");
    rig.Int(1, 11);
    rig.Int(2, 22);
    rig.Int(3, 33);
    CHECK(Tape(rig.obj, 0) == "0 11");
    CHECK(Tape(rig.obj, 1) == "0 22");
    CHECK(Tape(rig.obj, 2) == "0 33");
  }

  TEST_CASE("mtr: 'record' with track numbers arms only those tracks (#501)") {
    Rig rig("3");
    rig.List(0, "record 1 3");
    CHECK(rig.obj.IsRecording(0));
    CHECK_FALSE(rig.obj.IsRecording(1));
    CHECK(rig.obj.IsRecording(2));

    rig.Int(1, 11);
    rig.Int(2, 22);
    rig.Int(3, 33);
    CHECK(rig.obj.Count(0) == 1);
    CHECK(rig.obj.Count(1) == 0);
    CHECK(rig.obj.Count(2) == 1);
  }

  TEST_CASE("mtr: a track that is not recording stores nothing (#501)") {
    Rig rig;
    rig.Int(1, 60);
    CHECK(rig.obj.Count(0) == 0);
  }

  TEST_CASE("mtr: 'record' starts a fresh take rather than appending (#501)") {
    // The tape is wound back to zero, which is also what makes the first delta
    // measurable from the record message and gives `delay` something to
    // overwrite.
    Rig rig;
    rig.List(0, "record");
    rig.Int(1, 1);
    rig.Int(1, 2);
    REQUIRE(rig.obj.Count(0) == 2);

    rig.List(0, "record");
    CHECK(rig.obj.Count(0) == 0);
    rig.Int(1, 3);
    CHECK(Tape(rig.obj, 0) == "0 3");
  }

  TEST_CASE("mtr: 'stop' ends recording (#501)") {
    // Max: "stops mtr when it is recording or playing."
    Rig rig;
    rig.List(0, "record");
    rig.Int(1, 1);
    rig.List(0, "stop");
    CHECK_FALSE(rig.obj.IsRecording(0));
    rig.Int(1, 2);
    CHECK(rig.obj.Count(0) == 1);
  }

  TEST_CASE("mtr: 'clear' erases the tape (#501)") {
    // Max: "erases the contents of mtr."
    Rig rig;
    rig.List(0, "record");
    rig.Int(1, 1);
    rig.List(0, "clear");
    CHECK(rig.obj.Count(0) == 0);
    CHECK(rig.obj.Position(0) == 0);
    CHECK_FALSE(rig.obj.IsRecording(0));
  }

  TEST_CASE("mtr: a message longer than an event is dropped whole (#501)") {
    // Refused rather than truncated: half a message is a different message, and
    // growing the tape would allocate on whichever thread the message arrived
    // on.
    Rig rig;
    rig.List(0, "record");
    rig.List(1, std::string(gMtr::EVENT_CAPACITY, 'x'));
    CHECK(rig.obj.Count(0) == 1);
    rig.List(1, std::string(gMtr::EVENT_CAPACITY + 1, 'x'));
    CHECK(rig.obj.Count(0) == 1);
  }

  TEST_CASE("mtr: events past the tape size are dropped (#501)") {
    Rig rig;
    rig.List(0, "record");
    for (std::size_t i = 0; i < gMtr::MAX_EVENTS + 10; i++)
      rig.Int(1, (int)i);
    CHECK(rig.obj.Count(0) == gMtr::MAX_EVENTS);
  }

  // ─── the manual walk ────────────────────────────────────────────────────────

  TEST_CASE("mtr: 'next' outputs one event and reports it on outlet 0 (#501)") {
    // Max: "causes each track to output only the next message in its recorded
    // sequence", and the leftmost outlet carries "track number, delta time, and
    // absolute time of each message being output ... as a list".
    Rig rig;
    rig.List(0, "record");
    rig.Int(1, 60);
    rig.Int(1, 61);
    rig.List(0, "stop");
    rig.reset();

    rig.List(0, "next");
    CHECK(rig.Out(1) == "i60");
    CHECK(rig.Out(0) == "s1 0 0");
    CHECK(rig.obj.Position(0) == 1);

    rig.reset();
    rig.List(0, "next");
    CHECK(rig.Out(1) == "i61");
    CHECK(rig.Out(0) == "s1 0 0");
  }

  TEST_CASE("mtr: 'next' steps every track, each reporting its own number (#501)") {
    Rig rig("2");
    rig.List(0, "record");
    rig.Int(1, 11);
    rig.Int(2, 22);
    rig.List(0, "stop");
    rig.reset();

    rig.List(0, "next");
    CHECK(rig.Out(1) == "i11");
    CHECK(rig.Out(2) == "i22");
    CHECK(rig.Out(0) == "s1 0 0,s2 0 0");
  }

  TEST_CASE("mtr: 'next' on a tape that has run out does nothing (#501)") {
    Rig rig;
    rig.List(0, "record");
    rig.Int(1, 60);
    rig.List(0, "stop");
    rig.List(0, "next");
    rig.reset();

    rig.List(0, "next");
    CHECK(rig.Out(1).empty());
    CHECK(rig.Out(0).empty());
  }

  TEST_CASE("mtr: 'rewind' puts the cursor back at the start (#501)") {
    // Max: "resets mtr to the beginning of its recorded sequence."
    Rig rig;
    rig.List(0, "record");
    rig.Int(1, 60);
    rig.Int(1, 61);
    rig.List(0, "stop");
    rig.List(0, "next");
    REQUIRE(rig.obj.Position(0) == 1);

    rig.List(0, "rewind");
    CHECK(rig.obj.Position(0) == 0);
    rig.reset();
    rig.List(0, "next");
    CHECK(rig.Out(1) == "i60");
  }

  TEST_CASE("mtr: an event leaves in the kind it went in (#501)") {
    // `.route`'s rule, shared with `.coll`, `.textfile` and `.qlist`: a
    // recorded 60 does not come back as 60., and a bang comes back as a bang
    // rather than as the word.
    Rig rig;
    rig.List(0, "record");
    rig.Int(1, 60);
    rig.Float(1, 60.5f);
    rig.List(1, "note 60 100");
    rig.Bang(1);
    rig.List(0, "stop");
    rig.reset();

    for (int i = 0; i < 4; i++)
      rig.List(0, "next");
    CHECK(rig.Out(1) == "i60,f60.50,snote 60 100,!");
  }

  // ─── mute ───────────────────────────────────────────────────────────────────

  TEST_CASE("mtr: 'mute' silences a track without moving it off the tape (#501)") {
    // Max: "causes mtr to stop producing output, while still continuing to
    // 'play'" — so the cursor advances either way and only the send is skipped.
    Rig rig;
    rig.List(0, "record");
    rig.Int(1, 60);
    rig.Int(1, 61);
    rig.List(0, "stop");
    rig.reset();

    rig.List(0, "mute");
    CHECK(rig.obj.IsMuted(0));
    rig.List(0, "next");
    CHECK(rig.Out(1).empty());
    // The cursor moved and the report still fired: the tape kept running.
    CHECK(rig.obj.Position(0) == 1);
    CHECK(rig.Out(0) == "s1 0 0");

    rig.List(0, "unmute");
    rig.reset();
    rig.List(0, "next");
    CHECK(rig.Out(1) == "i61");
  }

  // ─── commands in a track inlet ──────────────────────────────────────────────

  TEST_CASE("mtr: the transport words in a track inlet address that track (#501)") {
    // Max reserves them on this inlet too, so a patch brought across behaves
    // the same. It is the reason a track inlet cannot record a message that
    // begins with one of the eight words.
    Rig rig("2");
    rig.List(1, "record");
    CHECK(rig.obj.IsRecording(0));
    CHECK_FALSE(rig.obj.IsRecording(1));

    rig.Int(1, 60);
    rig.Int(2, 61);
    CHECK(rig.obj.Count(0) == 1);
    CHECK(rig.obj.Count(1) == 0);

    rig.List(1, "stop");
    CHECK_FALSE(rig.obj.IsRecording(0));
  }

  TEST_CASE("mtr: a message that is not a transport word is data (#501)") {
    Rig rig;
    rig.List(0, "record");
    rig.List(1, "recorder 1");
    rig.List(1, "playful");
    CHECK(Tape(rig.obj, 0) == "0 recorder 1 | 0 playful");
  }

  // ─── settings ───────────────────────────────────────────────────────────────

  TEST_CASE("mtr: 'timescale' is stored, and a non-positive one is refused (#501)") {
    // Max: "100 is the original timescale, whereas 200 would be twice as fast."
    Rig rig("2");
    rig.List(0, "timescale 200");
    CHECK(rig.obj.Timescale(0) == 200);
    CHECK(rig.obj.Timescale(1) == 200);

    rig.List(0, "timescale 0");
    CHECK(rig.obj.Timescale(0) == 200);
    rig.List(0, "timescale -5");
    CHECK(rig.obj.Timescale(0) == 200);
  }

  TEST_CASE("mtr: 'play' resets a track's timescale to 100 (#501)") {
    // Max's own footnote: "when a track is played again, the timescale is reset
    // to 100." Surprising enough that only a test keeps it, and the reason a
    // scale is sent after a play rather than before.
    Rig rig;
    rig.List(0, "record");
    rig.Int(1, 60);
    rig.List(0, "stop");

    rig.List(0, "timescale 200");
    REQUIRE(rig.obj.Timescale(0) == 200);
    rig.List(0, "play");
    CHECK(rig.obj.Timescale(0) == gMtr::DEFAULT_TIMESCALE);
  }

  TEST_CASE("mtr: 'play <n> <scale>' in a track inlet carries Max's arguments (#501)") {
    // Max's `play 3 200`: three times through, at twice the speed. Only the
    // scale is observable standalone; the repeat count is a patcher case.
    Rig rig;
    rig.List(0, "record");
    rig.Int(1, 60);
    rig.List(0, "stop");

    rig.List(1, "play 3 200");
    CHECK(rig.obj.Timescale(0) == 200);
  }

  TEST_CASE("mtr: 'first' is a setting and does not touch the tape (#501)") {
    // Max: "causes mtr to wait that amount of time after a play message is
    // received before playing back."
    Rig rig;
    rig.List(0, "record");
    rig.Int(1, 60);
    rig.List(0, "stop");

    rig.List(0, "first 250");
    CHECK(rig.obj.First() == 250);
    // The tape is untouched — the difference from `delay`, which writes into
    // it. Asserted side by side because getting these two the same way round is
    // the whole risk.
    CHECK(Tape(rig.obj, 0) == "0 60");
  }

  TEST_CASE("mtr: 'delay' is an edit and rewrites the first delta (#501)") {
    // Max: "sets the first delta time value of each track to that number." The
    // difference from `first` is exactly that this is written into the tape.
    Rig rig("2");
    rig.List(0, "record");
    rig.Int(1, 60);
    rig.Int(1, 61);
    rig.Int(2, 70);
    rig.List(0, "stop");

    rig.List(0, "delay 250");
    CHECK(Tape(rig.obj, 0) == "250 60 | 0 61");
    CHECK(Tape(rig.obj, 1) == "250 70");
  }

  TEST_CASE("mtr: a standalone object consumes read and write without doing anything (#691)") {
    // No patcher means no file plumbing, and the honest answer is silence: the
    // words are still consumed, because Max dispatches on the selector, but
    // nothing is remembered and nothing is asked for.
    Rig rig;
    rig.List(0, "record");
    rig.Int(1, 60);
    rig.List(0, "stop");
    rig.reset();

    rig.List(0, "read tape.txt");
    rig.List(0, "write tape.txt");
    rig.List(0, "read");
    rig.List(0, "write");
    CHECK(rig.obj.Count(0) == 1);
    CHECK(rig.Out(0).empty());
    CHECK(rig.Out(1).empty());
    CHECK(rig.Out(rig.obj.FileOutlet()).empty());
    CHECK(rig.obj.ReadFile().empty());
    CHECK(rig.obj.WriteFile().empty());
  }

  TEST_CASE("mtr: 'read' and 'write' in a track inlet are commands, not data (#691)") {
    // Max registers both on every inlet — "in other inlets: opens a file
    // containing only the track that corresponds to the inlet" — so they join
    // the eight transport words already reserved there. A recording track must
    // therefore not store them, which is the observable half of that rule and
    // the one a plausible implementation gets wrong.
    Rig rig;
    rig.List(0, "record");
    rig.List(1, "read tape.txt");
    rig.List(1, "write tape.txt");
    rig.Int(1, 60);
    CHECK(Tape(rig.obj, 0) == "0 60");
  }

  TEST_CASE("mtr: an unknown message and a bang in the control inlet do nothing (#501)") {
    // Max 8 answers a bang with a dictionary, a type this patcher does not
    // have. Anything else is simply not understood, which is Max.
    Rig rig;
    rig.List(0, "record");
    rig.Int(1, 60);
    rig.List(0, "stop");
    rig.reset();

    rig.Bang(0);
    rig.List(0, "wobble 1 2 3");
    CHECK(rig.Out(0).empty());
    CHECK(rig.Out(1).empty());
    CHECK(rig.obj.Count(0) == 1);
  }

  TEST_CASE("mtr: a track number outside the range names no track (#501)") {
    Rig rig("2");
    rig.List(0, "record 5");
    CHECK_FALSE(rig.obj.IsRecording(0));
    CHECK_FALSE(rig.obj.IsRecording(1));
    rig.List(0, "record 0");
    CHECK_FALSE(rig.obj.IsRecording(0));
  }

  TEST_CASE("mtr: Calculate sends nothing (#501)") {
    // The object is driven by its inlets and by its own clock; one that emitted
    // would restart itself on every DSP tick.
    Rig rig;
    rig.List(0, "record");
    rig.Int(1, 60);
    rig.List(0, "stop");
    rig.reset();

    for (int i = 0; i < 8; i++)
      rig.obj.Calculate(YSE::T_DSP);
    CHECK(rig.Out(0).empty());
    CHECK(rig.Out(1).empty());
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("mtr: nothing is written until 'embed' is on (#501)") {
    // Max 5: "the object's contents cannot be embedded in a patcher file."
    // Max 8 added the flag, and the family rule is save iff Max gives one.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR);
    REQUIRE(mtr != nullptr);
    mtr->SetListData(0, "record");
    mtr->SetIntData(1, 60);
    mtr->SetListData(0, "stop");
    CHECK(p.DumpJSON().find("\"state\"") == std::string::npos);
  }

  TEST_CASE("mtr: with 'embed 1' the tapes survive a DumpJSON / ParseJSON round trip (#501)") {
    // Max 8: "when embed is set to 1, any recorded data is saved with the
    // patcher."
    Recorder report;
    Recorder data;
    YSE::pHandle reportHandle(&report);
    YSE::pHandle dataHandle(&data);

    YSE::patcher src;
    src.create(2);
    YSE::pHandle* mtr = src.CreateObject(YSE::OBJ::G_MTR, "2");
    REQUIRE(mtr != nullptr);
    mtr->SetListData(0, "embed 1");
    mtr->SetListData(0, "record");
    mtr->SetIntData(1, 60);
    mtr->SetListData(1, "note 61 100");
    mtr->SetIntData(2, 70);
    mtr->SetListData(0, "stop");
    mtr->SetListData(0, "delay 40");

    const std::string json = src.DumpJSON();
    CHECK(json.find(".mtr") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".mtr");
    loaded.Connect(copy, 0, &reportHandle, 0);
    loaded.Connect(copy, 1, &dataHandle, 0);

    // Read back through the outlets rather than through an accessor: what has
    // to survive is a tape a patch can play. The report carries the reloaded
    // delta, which is where the `delay 40` edit shows up.
    copy->SetListData(0, "next");
    CHECK(Joined(data.seen) == "i60");
    // Track 2 reloaded too, and reports as track 2 — one `next` steps both.
    CHECK(Joined(report.seen) == "s1 40 40,s2 40 40");

    data.reset();
    report.reset();
    copy->SetListData(0, "next");
    CHECK(Joined(data.seen) == "snote 61 100");
    // Track 2 has run out, so only track 1 reports; the absolute clock kept
    // the 40 the reloaded first delta put there.
    CHECK(Joined(report.seen) == "s1 0 40");
  }

  TEST_CASE("mtr: the embed flag itself survives a save (#501)") {
    // Otherwise a reloaded object would forget to embed itself the next time
    // the patch is saved, and the tapes would quietly stop being written.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* mtr = src.CreateObject(YSE::OBJ::G_MTR);
    REQUIRE(mtr != nullptr);
    mtr->SetListData(0, "embed 1");

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(src.DumpJSON());
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(loaded.DumpJSON().find("\"embed\"") != std::string::npos);
  }

  TEST_CASE("mtr: cursors and mutes are run-time state and do not survive (#501)") {
    // `.coll`'s pointer rule. A reloaded patch that came back mid-tape, or
    // silently muted, would be a surprise rather than a feature.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* mtr = src.CreateObject(YSE::OBJ::G_MTR);
    REQUIRE(mtr != nullptr);
    mtr->SetListData(0, "embed 1");
    mtr->SetListData(0, "record");
    mtr->SetIntData(1, 60);
    mtr->SetIntData(1, 61);
    mtr->SetListData(0, "stop");
    mtr->SetListData(0, "next");
    mtr->SetListData(0, "mute");
    mtr->SetListData(0, "timescale 200");

    Recorder data;
    YSE::pHandle dataHandle(&data);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(src.DumpJSON());
    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    loaded.Connect(copy, 1, &dataHandle, 0);

    // Not muted, at the original speed, and back at the top of the tape.
    copy->SetListData(0, "next");
    CHECK(Joined(data.seen) == "i60");
  }

  // ─── the clock, which needs a real patcher ──────────────────────────────────

  TEST_CASE("mtr: a recorded gap is measured on the patcher's block clock (#501)") {
    // The property the object exists for and the one no standalone test can
    // show: Max's "the amount of time elapsed since the previous event", read
    // off the same clock playback will later wait on. The first gap is measured
    // from the `record` message itself, which is what gives `delay` something
    // to overwrite.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "");
    REQUIRE(mtr != nullptr);

    mtr->SetListData(0, "record");
    const std::uint64_t gap = 12;
    for (std::uint64_t block = 0; block < gap; ++block)
      p.Calculate(YSE::T_DSP);
    mtr->SetIntData(1, 60);
    for (std::uint64_t block = 0; block < gap; ++block)
      p.Calculate(YSE::T_DSP);
    mtr->SetIntData(1, 61);
    mtr->SetListData(0, "stop");

    Recorder report;
    YSE::pHandle reportHandle(&report);
    p.Connect(mtr, 0, &reportHandle, 0);

    const int expected = messageScheduler::MillisForBlocks(gap);
    REQUIRE(expected > 0);
    mtr->SetListData(0, "next");
    CHECK(Joined(report.seen) == "s1 " + std::to_string(expected) + " " + std::to_string(expected));
    report.reset();
    mtr->SetListData(0, "next");
    CHECK(Joined(report.seen) ==
          "s1 " + std::to_string(expected) + " " + std::to_string(2 * expected));
  }

  TEST_CASE("mtr: 'play' waits out each recorded gap (#501)") {
    // Max: "plays back all messages recorded earlier, sending them out the
    // corresponding outlets in the same rhythm and at the same speed they were
    // recorded."
    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "");
    REQUIRE(mtr != nullptr);

    Recorder data;
    YSE::pHandle dataHandle(&data);
    p.Connect(mtr, 1, &dataHandle, 0);

    // Record two events, each a real gap apart.
    const std::uint64_t gap = 10;
    mtr->SetListData(0, "record");
    for (std::uint64_t block = 0; block < gap; ++block)
      p.Calculate(YSE::T_DSP);
    mtr->SetIntData(1, 60);
    for (std::uint64_t block = 0; block < gap; ++block)
      p.Calculate(YSE::T_DSP);
    mtr->SetIntData(1, 61);
    mtr->SetListData(0, "stop");
    data.reset();

    const int recorded = messageScheduler::MillisForBlocks(gap);
    const std::uint64_t due = messageScheduler::BlocksForMillis(recorded);

    mtr->SetListData(0, "play");
    // Nothing goes out in the arming dispatch: the first event has a delta of
    // its own and is waited out like every other one.
    CHECK(data.seen.empty());
    CHECK(p.Scheduler()->PendingCount() == 1);

    for (std::uint64_t block = 1; block < due; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(data.seen.empty());
    p.Calculate(YSE::T_DSP);
    CHECK(Joined(data.seen) == "i60");

    for (std::uint64_t block = 1; block <= due; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(Joined(data.seen) == "i60,i61");
    // The tape finished rather than stalling on a step whose delivery never
    // came.
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  TEST_CASE("mtr: 'timescale 200' halves every wait (#501)") {
    // Max: "100 is the original timescale, whereas 200 would be twice as fast."
    // The sign of this is a coin flip if you do not read the reference, and the
    // test is what pins it. Sent after the play, because a play resets it.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "");
    REQUIRE(mtr != nullptr);

    Recorder data;
    YSE::pHandle dataHandle(&data);
    p.Connect(mtr, 1, &dataHandle, 0);

    const std::uint64_t gap = 20;
    mtr->SetListData(0, "record");
    for (std::uint64_t block = 0; block < gap; ++block)
      p.Calculate(YSE::T_DSP);
    mtr->SetIntData(1, 60);
    mtr->SetListData(0, "stop");
    data.reset();

    const int recorded = messageScheduler::MillisForBlocks(gap);
    // `play <n> <scale>` in the track inlet, which is Max's way of setting a
    // scale that a play will not immediately reset.
    mtr->SetListData(1, "play 1 200");

    const std::uint64_t scaled = messageScheduler::BlocksForMillis(recorded / 2);
    const std::uint64_t full = messageScheduler::BlocksForMillis(recorded);
    REQUIRE(scaled < full);

    for (std::uint64_t block = 1; block < scaled; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(data.seen.empty());
    p.Calculate(YSE::T_DSP);
    CHECK(Joined(data.seen) == "i60");
  }

  TEST_CASE("mtr: 'first' delays the start of playback (#501)") {
    // Max: "causes mtr to wait that amount of time after a play message is
    // received before playing back." It is added to the first event's own delta
    // rather than replacing it.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "");
    REQUIRE(mtr != nullptr);

    Recorder data;
    YSE::pHandle dataHandle(&data);
    p.Connect(mtr, 1, &dataHandle, 0);

    // A tape whose only event has no gap in front of it, so the wait that
    // follows is `first` and nothing else.
    mtr->SetListData(0, "record");
    mtr->SetIntData(1, 60);
    mtr->SetListData(0, "stop");
    data.reset();

    mtr->SetListData(0, "first 200");
    mtr->SetListData(0, "play");

    const std::uint64_t due = messageScheduler::BlocksForMillis(200);
    for (std::uint64_t block = 1; block < due; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(data.seen.empty());
    p.Calculate(YSE::T_DSP);
    CHECK(Joined(data.seen) == "i60");
  }

  TEST_CASE("mtr: 'stop' cancels the pending step (#501)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "");
    REQUIRE(mtr != nullptr);

    Recorder data;
    YSE::pHandle dataHandle(&data);
    p.Connect(mtr, 1, &dataHandle, 0);

    mtr->SetListData(0, "record");
    mtr->SetIntData(1, 60);
    mtr->SetListData(0, "stop");
    mtr->SetListData(0, "first 200");
    mtr->SetListData(0, "play");
    REQUIRE(p.Scheduler()->PendingCount() == 1);

    mtr->SetListData(0, "stop");
    CHECK(p.Scheduler()->PendingCount() == 0);

    const std::uint64_t due = messageScheduler::BlocksForMillis(200);
    for (std::uint64_t block = 1; block <= due + 4; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(data.seen.empty());
  }

  TEST_CASE("mtr: a muted track keeps its clock and only skips the send (#501)") {
    // Max: "still continuing to 'play'". A mute that stopped the clock would
    // leave the tape wherever it was silenced.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "");
    REQUIRE(mtr != nullptr);

    Recorder data;
    YSE::pHandle dataHandle(&data);
    p.Connect(mtr, 1, &dataHandle, 0);

    const std::uint64_t gap = 8;
    mtr->SetListData(0, "record");
    mtr->SetIntData(1, 60);
    for (std::uint64_t block = 0; block < gap; ++block)
      p.Calculate(YSE::T_DSP);
    mtr->SetIntData(1, 61);
    mtr->SetListData(0, "stop");
    data.reset();

    mtr->SetListData(0, "mute");
    mtr->SetListData(0, "play");
    const std::uint64_t due =
        messageScheduler::BlocksForMillis(messageScheduler::MillisForBlocks(gap));
    for (std::uint64_t block = 1; block <= due + 4; ++block)
      p.Calculate(YSE::T_DSP);

    // Nothing came out, but the tape ran to the end: no step is left pending
    // and the track has stopped playing of its own accord.
    CHECK(data.seen.empty());
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  TEST_CASE("mtr: two tracks play independently (#501)") {
    // The whole point of the object, and the reason each track carries its own
    // clock rather than the object carrying one.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "2");
    REQUIRE(mtr != nullptr);

    Recorder one;
    Recorder two;
    YSE::pHandle oneHandle(&one);
    YSE::pHandle twoHandle(&two);
    p.Connect(mtr, 1, &oneHandle, 0);
    p.Connect(mtr, 2, &twoHandle, 0);

    // Track 1 gets a short gap, track 2 a long one.
    const std::uint64_t shortGap = 4;
    const std::uint64_t longGap = 20;
    mtr->SetListData(0, "record");
    for (std::uint64_t block = 0; block < shortGap; ++block)
      p.Calculate(YSE::T_DSP);
    mtr->SetIntData(1, 11);
    for (std::uint64_t block = shortGap; block < longGap; ++block)
      p.Calculate(YSE::T_DSP);
    mtr->SetIntData(2, 22);
    mtr->SetListData(0, "stop");
    one.reset();
    two.reset();

    mtr->SetListData(0, "play");
    CHECK(p.Scheduler()->PendingCount() == 2);

    const std::uint64_t first =
        messageScheduler::BlocksForMillis(messageScheduler::MillisForBlocks(shortGap));
    for (std::uint64_t block = 1; block <= first; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(Joined(one.seen) == "i11");
    CHECK(two.seen.empty());

    const std::uint64_t second =
        messageScheduler::BlocksForMillis(messageScheduler::MillisForBlocks(longGap));
    for (std::uint64_t block = first; block <= second + 2; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(Joined(two.seen) == "i22");
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  TEST_CASE("mtr: 'play <n>' repeats the tape n times (#501)") {
    // Max's `play 3 200` in a track inlet — the repeat count half of it, which
    // is what makes a recorded gesture loop.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "");
    REQUIRE(mtr != nullptr);

    Recorder data;
    YSE::pHandle dataHandle(&data);
    p.Connect(mtr, 1, &dataHandle, 0);

    mtr->SetListData(0, "record");
    mtr->SetIntData(1, 60);
    mtr->SetListData(0, "stop");
    data.reset();

    mtr->SetListData(1, "play 3");
    // A zero delta is floored to one block by the scheduler, so three passes
    // take three blocks rather than spinning.
    for (int block = 0; block < 8; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(Joined(data.seen) == "i60,i60,i60");
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  TEST_CASE("mtr: 'play' rewinds rather than resuming (#501)") {
    // Max: "plays back all messages recorded earlier" — the whole tape. A play
    // that resumed would quietly turn stop-then-play into a continue.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "");
    REQUIRE(mtr != nullptr);

    Recorder data;
    YSE::pHandle dataHandle(&data);
    p.Connect(mtr, 1, &dataHandle, 0);

    mtr->SetListData(0, "record");
    mtr->SetIntData(1, 60);
    mtr->SetIntData(1, 61);
    mtr->SetListData(0, "stop");
    data.reset();

    mtr->SetListData(0, "play");
    p.Calculate(YSE::T_DSP);
    REQUIRE(Joined(data.seen) == "i60");
    mtr->SetListData(0, "stop");
    data.reset();

    mtr->SetListData(0, "play");
    p.Calculate(YSE::T_DSP);
    CHECK(Joined(data.seen) == "i60");
  }

  TEST_CASE("mtr: deleting the object drops its pending steps safely (#501)") {
    // Armed possibly long before it is due — far outside the reclaimer's
    // two-block grace — so delivery must re-resolve the target and find
    // nothing. An ASan build trips here if the retired object is touched.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "2");
    REQUIRE(mtr != nullptr);

    Recorder data;
    YSE::pHandle dataHandle(&data);
    p.Connect(mtr, 1, &dataHandle, 0);

    mtr->SetListData(0, "record");
    mtr->SetIntData(1, 60);
    mtr->SetIntData(2, 70);
    mtr->SetListData(0, "stop");
    mtr->SetListData(0, "first 200");
    mtr->SetListData(0, "play");
    REQUIRE(p.Scheduler()->PendingCount() == 2);

    p.DeleteObject(mtr);
    const std::uint64_t due = messageScheduler::BlocksForMillis(200);
    for (std::uint64_t block = 1; block <= due + 2; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(data.seen.empty());
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  TEST_CASE("mtr: standalone, playback falls back to no waits (#501)") {
    // A standalone object has no dispatch to defer into, so a play walks the
    // whole tape at once rather than abandoning it unplayed — `.bondo`'s
    // fallback, for `.bondo`'s reason.
    Rig rig;
    rig.List(0, "record");
    rig.Int(1, 60);
    rig.Int(1, 61);
    rig.Int(1, 62);
    rig.List(0, "stop");
    rig.reset();

    rig.List(0, "play");
    CHECK(rig.Out(1) == "i60,i61,i62");
    CHECK_FALSE(rig.obj.IsPlaying(0));
  }

  // ─── tape files (issue #691) ────────────────────────────────────────────────
  //
  // Every case here needs a real patcherImplementation and a real file: the
  // background job and the completion delivered into a dispatch frame only exist
  // there, and a standalone object deliberately has neither.

  TEST_CASE("mtr: a write then a read round-trips every track (#691)") {
    // The acceptance criterion, end to end through a real patcher, a real file
    // on disk and real recorders: Max's text format out, the same tapes back,
    // and every kind of event surviving it. Asserted through the outlets rather
    // than through an accessor, because what has to come back is a tape a patch
    // can *play*.
    const std::string path = TempFile("yse_mtr_roundtrip_691.txt");

    Recorder report;
    Recorder one;
    Recorder two;
    Recorder file;
    YSE::pHandle reportHandle(&report);
    YSE::pHandle oneHandle(&one);
    YSE::pHandle twoHandle(&two);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "2");
    REQUIRE(mtr != nullptr);
    p.Connect(mtr, 0, &reportHandle, 0);
    p.Connect(mtr, 1, &oneHandle, 0);
    p.Connect(mtr, 2, &twoHandle, 0);
    p.Connect(mtr, 3, &fileHandle, 0);

    // No block is rendered between these, so every gap is 0 and the file is
    // deterministic — the timing itself is pinned by the clock cases above.
    mtr->SetListData(0, "record");
    mtr->SetIntData(1, 60);
    mtr->SetFloatData(1, 60.5f);
    mtr->SetBang(1);
    mtr->SetListData(2, "note 60 100");
    mtr->SetListData(0, "stop");

    mtr->SetListData(0, "write " + path);
    SettleFiles(p);

    // Max's format, quoted from the reference: "Line 1: track <track number>;
    // … Line 2, etc.: <delta time> <message>; … Last line: end;". A bang has no
    // text of its own, so it is spelled out as the word.
    CHECK(ReadWholeFile(path) ==
          "track 1;\n0 60;\n0 60.5;\n0 bang;\nend;\ntrack 2;\n0 note 60 100;\nend;\n");
    // Max has no outlet for a finished write anywhere in this family, and
    // neither does this.
    CHECK(file.seen.empty());

    mtr->SetListData(0, "clear");
    REQUIRE(p.FileIO() != nullptr);

    mtr->SetListData(0, "read " + path);
    SettleFiles(p);
    // Exactly one bang, and only after the tapes are in place.
    REQUIRE(Joined(file.seen) == "!");

    // Four `next`es walk both tapes: `next` addresses every track, so the first
    // three step track 1 while track 2 runs out after the first.
    for (int i = 0; i < 3; i++)
      mtr->SetListData(0, "next");

    CHECK(Joined(one.seen) == "i60,f60.50,!");
    CHECK(Joined(two.seen) == "snote 60 100");
    // Outlet 0's <track> <delta> <absolute> report, one per event a `next`
    // produced.
    CHECK(Joined(report.seen) == "s1 0 0,s2 0 0,s1 0 0,s1 0 0");

    Remove(path);
  }

  TEST_CASE("mtr: read does nothing in the message handler (#691)") {
    // The reason the plumbing exists. A `read` may be dispatched on the audio
    // callback, so the handler must not open anything — which is observable: the
    // tapes are still empty when the message returns, and only a rendered block
    // puts the file on them.
    const std::string path = TempFile("yse_mtr_deferred_691.txt");
    WriteWholeFile(path, "track 1;\n0 11;\n0 22;\nend;\n");

    Recorder one;
    Recorder file;
    YSE::pHandle oneHandle(&one);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "");
    REQUIRE(mtr != nullptr);
    p.Connect(mtr, 1, &oneHandle, 0);
    p.Connect(mtr, 2, &fileHandle, 0);

    mtr->SetListData(0, "read " + path);
    // Nothing yet: the tape is still empty, so a `next` has no event to output
    // and the file outlet has not fired. The request is a claim on a slot and
    // the disk has not been touched on this thread.
    mtr->SetListData(0, "next");
    CHECK(one.seen.empty());
    CHECK(file.seen.empty());
    REQUIRE(p.FileIO() != nullptr);
    CHECK(p.FileIO()->PendingCount() == 1);

    SettleFiles(p);
    REQUIRE(Joined(file.seen) == "!");
    CHECK(p.FileIO()->PendingCount() == 0);

    mtr->SetListData(0, "next");
    mtr->SetListData(0, "next");
    CHECK(Joined(one.seen) == "i11,i22");

    Remove(path);
  }

  TEST_CASE("mtr: a read replaces the tapes and stops the transport (#691)") {
    // Max's read loads a file into the object; it is not a merge. The transport
    // goes with the tape, which is where this parts company with `.qlist`: a
    // track left playing would already have a step armed at a delta belonging to
    // a recording that no longer exists, so its next gap would be one nothing
    // ever made. The cancelled step is visible in the scheduler's pending count.
    const std::string path = TempFile("yse_mtr_replace_691.txt");
    WriteWholeFile(path, "track 1;\n0 9;\nend;\n");

    Recorder one;
    Recorder file;
    YSE::pHandle oneHandle(&one);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "");
    REQUIRE(mtr != nullptr);
    p.Connect(mtr, 1, &oneHandle, 0);
    p.Connect(mtr, 2, &fileHandle, 0);

    mtr->SetListData(0, "record");
    mtr->SetIntData(1, 1);
    mtr->SetIntData(1, 2);
    mtr->SetIntData(1, 3);
    mtr->SetListData(0, "stop");
    // Something for the read to interrupt: a play that has to wait.
    mtr->SetListData(0, "first 500");
    mtr->SetListData(0, "play");
    REQUIRE(p.Scheduler()->PendingCount() == 1);
    one.reset();

    mtr->SetListData(0, "read " + path);
    SettleFiles(p);
    REQUIRE(Joined(file.seen) == "!");

    // The armed step is gone with the tape that armed it, and nothing was
    // played out of the old recording.
    CHECK(p.Scheduler()->PendingCount() == 0);
    CHECK(one.seen.empty());

    // Only the file's one event is left, and the cursor is at its start.
    mtr->SetListData(0, "next");
    mtr->SetListData(0, "next");
    CHECK(Joined(one.seen) == "i9");

    Remove(path);
  }

  TEST_CASE("mtr: read and write in a track inlet address only that track (#691)") {
    // Max: "in other inlets: opens a file containing only the track that
    // corresponds to the inlet" — and the same for write. Both halves are here
    // because getting one of them object-wide by accident is the plausible bug,
    // and it is invisible on a one-track object.
    const std::string path = TempFile("yse_mtr_pertrack_691.txt");

    Recorder one;
    Recorder two;
    Recorder file;
    YSE::pHandle oneHandle(&one);
    YSE::pHandle twoHandle(&two);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "2");
    REQUIRE(mtr != nullptr);
    p.Connect(mtr, 1, &oneHandle, 0);
    p.Connect(mtr, 2, &twoHandle, 0);
    p.Connect(mtr, 3, &fileHandle, 0);

    mtr->SetListData(0, "record");
    mtr->SetIntData(1, 11);
    mtr->SetIntData(2, 22);
    mtr->SetListData(0, "stop");

    // A write in track 2's inlet writes track 2 and nothing else — and keeps its
    // own declaration, so the file says which track it came from.
    mtr->SetListData(2, "write " + path);
    SettleFiles(p);
    CHECK(ReadWholeFile(path) == "track 2;\n0 22;\nend;\n");

    // Read back into track 1's inlet: the block's declared number is track 2's,
    // and it is deliberately ignored — Max defines this form for a file holding
    // one track, so the inlet decides where it lands.
    mtr->SetListData(1, "read " + path);
    SettleFiles(p);
    REQUIRE(Joined(file.seen) == "!");

    mtr->SetListData(0, "next");
    // Track 1 now holds what track 2 had, and track 2 was not disturbed by
    // either message.
    CHECK(Joined(one.seen) == "i22");
    CHECK(Joined(two.seen) == "i22");

    Remove(path);
  }

  TEST_CASE("mtr: a per-track read takes the first track block only (#691)") {
    // Max defines the per-inlet form only for a file that holds one track, so a
    // multi-track file has no meaning there. Taking the first block is the one
    // reading that keeps a tape a tape; concatenating the blocks would jam three
    // recordings into one.
    const std::string path = TempFile("yse_mtr_firstblock_691.txt");
    WriteWholeFile(path, "track 1;\n0 11;\nend;\ntrack 2;\n0 22;\nend;\n");

    Recorder one;
    Recorder file;
    YSE::pHandle oneHandle(&one);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "");
    REQUIRE(mtr != nullptr);
    p.Connect(mtr, 1, &oneHandle, 0);
    p.Connect(mtr, 2, &fileHandle, 0);

    mtr->SetListData(1, "read " + path);
    SettleFiles(p);
    REQUIRE(Joined(file.seen) == "!");

    mtr->SetListData(0, "next");
    mtr->SetListData(0, "next");
    CHECK(Joined(one.seen) == "i11");

    Remove(path);
  }

  TEST_CASE("mtr: a file with no track line loads into the first track (#691)") {
    // A hand-written file that is just events is one track's worth of them, and
    // the only track it can mean is the first. Semicolons are optional on the
    // way in for the same reason `.qlist`'s are: this format carries no
    // punctuation inside a line for a newline to steal.
    const std::string path = TempFile("yse_mtr_headerless_691.txt");
    WriteWholeFile(path, "0 41\r\n0 42\r\n");

    Recorder one;
    Recorder file;
    YSE::pHandle oneHandle(&one);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "");
    REQUIRE(mtr != nullptr);
    p.Connect(mtr, 1, &oneHandle, 0);
    p.Connect(mtr, 2, &fileHandle, 0);

    mtr->SetListData(0, "read " + path);
    SettleFiles(p);
    REQUIRE(Joined(file.seen) == "!");

    mtr->SetListData(0, "next");
    mtr->SetListData(0, "next");
    CHECK(Joined(one.seen) == "i41,i42");

    Remove(path);
  }

  TEST_CASE("mtr: a track block naming a track the object has not got is dropped (#691)") {
    // A saved 4-track file read into a `.mtr 2` keeps the two tracks that still
    // have somewhere to live rather than folding the others onto the wrong tape,
    // which is the same reading `RestoreState` already gives a shrunk argument.
    const std::string path = TempFile("yse_mtr_overrange_691.txt");
    WriteWholeFile(path, "track 1;\n0 11;\nend;\ntrack 9;\n0 99;\nend;\ntrack 2;\n0 22;\nend;\n");

    Recorder one;
    Recorder two;
    Recorder file;
    YSE::pHandle oneHandle(&one);
    YSE::pHandle twoHandle(&two);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "2");
    REQUIRE(mtr != nullptr);
    p.Connect(mtr, 1, &oneHandle, 0);
    p.Connect(mtr, 2, &twoHandle, 0);
    p.Connect(mtr, 3, &fileHandle, 0);

    mtr->SetListData(0, "read " + path);
    SettleFiles(p);
    REQUIRE(Joined(file.seen) == "!");

    mtr->SetListData(0, "next");
    mtr->SetListData(0, "next");
    // The 99 landed nowhere: the block that held it named a track this object
    // has not got, so it closed rather than falling through to the last one
    // opened.
    CHECK(Joined(one.seen) == "i11");
    CHECK(Joined(two.seen) == "i22");

    Remove(path);
  }

  TEST_CASE("mtr: an over-long event is skipped and the rest of the file loads (#691)") {
    // Refused rather than truncated, with the rest still loading — the same
    // answer a recorded message past EVENT_CAPACITY gets, since half a message
    // is a different message.
    const std::string path = TempFile("yse_mtr_longevent_691.txt");
    WriteWholeFile(path, "track 1;\n0 " + std::string(gMtr::EVENT_CAPACITY + 1, 'x') + ";\n0 7;\n");

    Recorder one;
    Recorder file;
    YSE::pHandle oneHandle(&one);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "");
    REQUIRE(mtr != nullptr);
    p.Connect(mtr, 1, &oneHandle, 0);
    p.Connect(mtr, 2, &fileHandle, 0);

    mtr->SetListData(0, "read " + path);
    SettleFiles(p);
    REQUIRE(Joined(file.seen) == "!");

    mtr->SetListData(0, "next");
    mtr->SetListData(0, "next");
    CHECK(Joined(one.seen) == "i7");

    Remove(path);
  }

  TEST_CASE("mtr: events past the tape size are dropped (#691)") {
    // MAX_EVENTS is allocated whole at construction and never grows, so a longer
    // file loses its tail rather than allocating on the audio thread, where the
    // completion runs.
    std::string contents = "track 1;\n";
    for (std::size_t i = 0; i < gMtr::MAX_EVENTS + 5; i++)
      contents += "0 1;\n";
    const std::string path = TempFile("yse_mtr_tapefull_691.txt");
    WriteWholeFile(path, contents);

    Recorder file;
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "");
    REQUIRE(mtr != nullptr);
    p.Connect(mtr, 2, &fileHandle, 0);

    mtr->SetListData(0, "read " + path);
    SettleFiles(p);
    REQUIRE(Joined(file.seen) == "!");

    // Written straight back out: the tape holds exactly MAX_EVENTS events.
    const std::string out = TempFile("yse_mtr_tapefull_out_691.txt");
    mtr->SetListData(0, "write " + out);
    SettleFiles(p);

    std::string expected = "track 1;\n";
    for (std::size_t i = 0; i < gMtr::MAX_EVENTS; i++)
      expected += "0 1;\n";
    expected += "end;\n";
    CHECK(ReadWholeFile(out) == expected);

    Remove(path);
    Remove(out);
  }

  TEST_CASE("mtr: the bare forms reuse the last name given, each half its own (#691)") {
    // Max's bare `read` / `write` open a file dialog, which a headless patcher
    // has no equivalent of, so they reuse the last name their own half was
    // given. One name per half for the whole object, not one per track: a dialog
    // remembers nothing, so there is no Max behaviour a per-track name could
    // match.
    const std::string source = TempFile("yse_mtr_bare_src_691.txt");
    const std::string sink = TempFile("yse_mtr_bare_sink_691.txt");
    WriteWholeFile(source, "track 1;\n0 5;\nend;\n");

    Recorder file;
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "");
    REQUIRE(mtr != nullptr);
    p.Connect(mtr, 2, &fileHandle, 0);

    mtr->SetListData(0, "read " + source);
    SettleFiles(p);
    REQUIRE(Joined(file.seen) == "!");

    // Settled first on purpose: a write's payload is built at *request* time,
    // because the pool thread must never touch this object, so a write queued
    // behind a read writes the tapes as they were before the read landed.
    mtr->SetListData(0, "write " + sink);
    SettleFiles(p);
    CHECK(ReadWholeFile(sink) == "track 1;\n0 5;\nend;\n");

    // Now the bare forms. The write must not go to the file the read named, and
    // the read must not go to the file the write named — the failure mode of one
    // shared name, which would silently overwrite the source.
    WriteWholeFile(source, "track 1;\n0 6;\nend;\n");
    Remove(sink);
    file.reset();

    mtr->SetListData(0, "read");
    SettleFiles(p);
    REQUIRE(Joined(file.seen) == "!");

    mtr->SetListData(0, "write");
    SettleFiles(p);
    CHECK(ReadWholeFile(sink) == "track 1;\n0 6;\nend;\n");
    CHECK(ReadWholeFile(source) == "track 1;\n0 6;\nend;\n");

    Remove(source);
    Remove(sink);
  }

  TEST_CASE("mtr: a bare read with no name and no argument does nothing (#691)") {
    // There is nothing to fall back on and no dialog to ask with, so the message
    // is consumed and refused — silently, since it may be the audio thread.
    Recorder file;
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "");
    REQUIRE(mtr != nullptr);
    p.Connect(mtr, 2, &fileHandle, 0);

    mtr->SetListData(0, "read");
    mtr->SetListData(0, "write");
    REQUIRE(p.FileIO() != nullptr);
    CHECK(p.FileIO()->PendingCount() == 0);

    SettleFiles(p);
    CHECK(file.seen.empty());
  }

  TEST_CASE("mtr: a read of a missing file leaves the tapes alone (#691)") {
    // Every failure looks the same from here — no such file, a file too large,
    // a write while the host's read-only VFS is installed — and the report is
    // the outlet staying silent.
    const std::string path = TempFile("yse_mtr_missing_691.txt");

    Recorder one;
    Recorder file;
    YSE::pHandle oneHandle(&one);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "");
    REQUIRE(mtr != nullptr);
    p.Connect(mtr, 1, &oneHandle, 0);
    p.Connect(mtr, 2, &fileHandle, 0);

    mtr->SetListData(0, "record");
    mtr->SetIntData(1, 60);
    mtr->SetListData(0, "stop");

    mtr->SetListData(0, "read " + path);
    SettleFiles(p);

    CHECK(file.seen.empty());
    mtr->SetListData(0, "next");
    CHECK(Joined(one.seen) == "i60");
  }

  TEST_CASE("mtr: deleting the object with a read in flight is safe (#691)") {
    // The property that makes the whole design safe by construction rather than
    // by timing: the background job holds a scheduler-owned slot and never a
    // pObject, so there is no pointer for it to dangle on, and the completion is
    // re-resolved against the block's GraphState. An ASan build trips here if
    // the retired object is touched.
    const std::string path = TempFile("yse_mtr_deleted_691.txt");
    WriteWholeFile(path, "track 1;\n0 1;\nend;\n");

    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "");
    REQUIRE(mtr != nullptr);

    mtr->SetListData(0, "read " + path);
    REQUIRE(p.FileIO() != nullptr);
    REQUIRE(p.FileIO()->PendingCount() == 1);

    p.DeleteObject(mtr);
    SettleFiles(p);
    CHECK(p.FileIO()->PendingCount() == 0);

    Remove(path);
  }

  TEST_CASE("mtr: a tape loaded from a file plays on the clock (#691)") {
    // The whole point, end to end: a recording that was never made in this
    // session arrives from disk and plays back in the rhythm the file wrote
    // down. Deadlines through BlocksForMillis at the live SAMPLERATE, as the
    // rest of the clock suite is.
    const std::string path = TempFile("yse_mtr_play_691.txt");
    WriteWholeFile(path, "track 1;\n0 60;\n200 61;\nend;\n");

    Recorder one;
    Recorder file;
    YSE::pHandle oneHandle(&one);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* mtr = p.CreateObject(YSE::OBJ::G_MTR, "");
    REQUIRE(mtr != nullptr);
    p.Connect(mtr, 1, &oneHandle, 0);
    p.Connect(mtr, 2, &fileHandle, 0);

    mtr->SetListData(0, "read " + path);
    SettleFiles(p);
    REQUIRE(Joined(file.seen) == "!");

    mtr->SetListData(0, "play");
    // The first event's delta is 0, so it lands on the next block.
    p.Calculate(YSE::T_DSP);
    REQUIRE(Joined(one.seen) == "i60");

    const std::uint64_t due = messageScheduler::BlocksForMillis(200);
    for (std::uint64_t block = 1; block < due; ++block)
      p.Calculate(YSE::T_DSP);
    // Still waiting out the recorded gap.
    CHECK(Joined(one.seen) == "i60");

    p.Calculate(YSE::T_DSP);
    CHECK(Joined(one.seen) == "i60,i61");

    Remove(path);
  }

} // TEST_SUITE("patcher")
