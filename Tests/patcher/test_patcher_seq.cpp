// Tests for .seq (issue #502) — the patcher's raw-MIDI-byte sequencer.
//
// .seq is .mtr's neighbour (Max's own "See Also" says so) and the suite is
// shaped the way .mtr's is, for the same reason: half of what the object does
// only exists on a clock, so there are two layers and they are not
// interchangeable.
//
//   - **standalone cases** pin the grammar and the tape: what is a command and
//     what is data, what counts as a MIDI byte, what `record` / `append` /
//     `stop` / `clear` do, what `delay` / `addeventdelay` / `hook` edit, in
//     what order the end bang and the last byte leave, and what a save carries.
//     A standalone object has no patcher and so no clock at all, which makes
//     every recorded delta 0 and every playback walk straight through — perfect
//     for testing everything *except* the timing.
//
//   - **patcher cases** pin the clock, which cannot exist standalone: that a
//     gap really is measured off the block counter, that playback really waits
//     it out again, that Max's tempo multiplier really divides the wait, that
//     `stop` really cancels a pending step, that `start -1` really ignores the
//     clock and advances on `tick` instead — and, above all, that the three
//     bytes of one MIDI message really arrive in one audio block. Deadlines are
//     asserted through messageScheduler::BlocksForMillis / MillisForBlocks at
//     the live SAMPLERATE rather than through hard-coded block counts, so the
//     suite holds at any negotiated rate.
//
// Six rules carry the file, each of them something a plausible implementation
// gets backwards without ever crashing:
//
//   - **bytes recorded together play back together.** This is the one rule that
//     separates a MIDI sequencer from a message sequencer. .qlist treats the
//     scheduler's one-block deadline floor as a feature; reproducing it here
//     would spread a note-on's status byte and its two data bytes over three
//     audio blocks, which is not that note-on. A run of zero-delta events has
//     to finish inside one dispatch.
//   - **the end bang comes *before* the last byte.** Max: "the bang is sent out
//     immediately before the final event of the sequence is played." Every
//     instinct says afterwards.
//   - **`record` erases and `append` does not.** That is the whole difference
//     between the two, and Max states it only in `append`'s entry.
//   - **the multiplier divides.** Max: "start 2048 plays it back at twice the
//     original speed", so a bigger number is a shorter wait.
//   - **`start -1` stops using the clock entirely.** A patcher that kept
//     ticking underneath would play the sequence twice.
//   - **nothing is saved with the patcher.** Max gives `seq` no save flag —
//     its contents live in a file — so the family rule leaves the tape out and
//     keeps only the filename parameter.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "headers/constants.hpp"
#include "patcher/genericObjects/gSeq.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/time/messageScheduler.h"

using YSE::PATCHER::gSeq;
using YSE::PATCHER::messageScheduler;
using YSE::PATCHER::patcherImplementation;

namespace {

  // Records every value it receives, in order and with its kind. Both outlets
  // feed one of these, so the *relative order* of a byte and the end bang is
  // visible — which is the only way to test Max's "immediately before the final
  // event" at all.
  struct Recorder : YSE::PATCHER::pObject {
    // "i144" for an int, "!" for a bang — one string per send.
    std::vector<std::string> seen;

    Recorder() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { seen.emplace_back("!"); });
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { seen.push_back("i" + std::to_string(v)); });
      inputs.back().RegisterFloat(
          [this](float v, int, YSE::THREAD) { seen.push_back("f" + std::to_string((int)v)); });
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { seen.push_back("s" + v); });
    }
    const char* Type() const override {
      return "seq_recorder";
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

  // A standalone .seq with one recorder wired to *both* outlets. Standalone
  // means no patcher, so no scheduler and no clock: every delta records as 0
  // and playback walks straight through, which is exactly what makes this rig
  // the right place to test everything that is not timing.
  struct Rig {
    gSeq obj;
    Recorder out;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) obj.SetParams(args);
      for (int i = 0; i < obj.NumOutputs(); i++)
        obj.ConnectOutlet(out.GetInlet(0), i);
    }

    void List(const std::string& message) {
      obj.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Int(int value) {
      obj.GetInlet(0)->SetInt(value, YSE::T_GUI);
    }
    void Float(float value) {
      obj.GetInlet(0)->SetFloat(value, YSE::T_GUI);
    }
    void Bang() {
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
    }
    std::string Out() {
      return Joined(out.seen);
    }
    void reset() {
      out.reset();
    }
  };

  // The whole tape as one string, so an assertion reads as the recording it
  // made rather than as a pile of index lookups.
  std::string Tape(const gSeq& obj) {
    std::string all;
    for (std::size_t i = 0; i < obj.Count(); i++) {
      if (i > 0) all += " | ";
      all += obj.EventAt(i);
    }
    return all;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("seq: creatable through the registry (#502)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SEQ);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".seq");
  }

  TEST_CASE("seq: listed by pRegistry::AllNames (#502)") {
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".seq")) != names.end());
  }

  TEST_CASE("seq: Max's shape is one inlet and two outlets (#502)") {
    // Max's left outlet carries the bytes and its middle one the end bang. The
    // right outlet carries MIDI-file meta messages and is deliberately absent —
    // a meta event cannot appear in a recorded byte stream, so it would be
    // permanently silent, and appending it at Max's own position when the file
    // half lands shifts no saved patch's cords.
    gSeq obj;
    CHECK(obj.NumInputs() == 1);
    CHECK(obj.NumOutputs() == 2);
    CHECK(obj.GetCategory() == YSE::PATCHER::pCategory::MIDI);
  }

  TEST_CASE("seq: a fresh object is empty, stopped and at the recorded tempo (#502)") {
    gSeq obj;
    CHECK(obj.Count() == 0);
    CHECK(obj.Position() == 0);
    CHECK_FALSE(obj.IsRecording());
    CHECK_FALSE(obj.IsPlaying());
    CHECK(obj.Speed() == gSeq::NORMAL_SPEED);
    CHECK_FALSE(obj.IsTickDriven());
    CHECK(obj.Filename().empty());
  }

  TEST_CASE("seq: the creation argument is Max's filename (#502)") {
    // Max: "specifies the name of a file to be read into seq automatically when
    // the patch is loaded." Stored, and inert until the file half lands (#692).
    gSeq obj;
    obj.SetParams("song.mid");
    CHECK(obj.Filename() == "song.mid");
    obj.SetParams("");
    CHECK(obj.Filename().empty());
  }

  // ─── recording ──────────────────────────────────────────────────────────────

  TEST_CASE("seq: 'record' stores the bytes that arrive in the inlet (#502)") {
    // Max: "when seq is recording, numbers received in its inlet are
    // interpreted as bytes of MIDI messages."
    Rig rig;
    rig.List("record");
    CHECK(rig.obj.IsRecording());
    rig.Int(144);
    rig.Int(60);
    rig.Int(112);
    CHECK(rig.obj.Count() == 3);
    // No patcher means no clock, so every gap measures as 0.
    CHECK(Tape(rig.obj) == "0 144 | 0 60 | 0 112");
  }

  TEST_CASE("seq: a float is converted to an int (#502)") {
    // Max's documented float method, in one line: "converted to int."
    Rig rig;
    rig.List("record");
    rig.Float(144.7f);
    CHECK(Tape(rig.obj) == "0 144");
  }

  TEST_CASE("seq: a list of numbers records each of them as a byte (#502)") {
    // The patcher's message model has no separate "list" and "message"
    // selectors, so this is the shape a MIDI message actually arrives in here.
    Rig rig;
    rig.List("record");
    rig.List("144 60 112");
    CHECK(rig.obj.Count() == 3);
    CHECK(Tape(rig.obj) == "0 144 | 0 60 | 0 112");
  }

  TEST_CASE("seq: a number outside a byte is refused rather than folded (#502)") {
    // The family's rule everywhere else: something that does not fit is refused
    // whole. A byte clamped or masked into range would be a *different* MIDI
    // message rather than a rejected one.
    Rig rig;
    rig.List("record");
    rig.Int(-1);
    rig.Int(256);
    rig.Int(1000);
    CHECK(rig.obj.Count() == 0);
    rig.Int(0);
    rig.Int(255);
    CHECK(rig.obj.Count() == 2);
  }

  TEST_CASE("seq: nothing is stored while the object is not recording (#502)") {
    Rig rig;
    rig.Int(144);
    rig.List("60 112");
    CHECK(rig.obj.Count() == 0);
  }

  TEST_CASE("seq: 'record' starts a fresh take and 'append' does not (#502)") {
    // Max states the difference only in append's entry: "starts recording at
    // the end of the stored sequence, without erasing the existing sequence."
    Rig rig;
    rig.List("record");
    rig.Int(144);
    rig.Int(60);
    rig.List("stop");
    REQUIRE(rig.obj.Count() == 2);

    rig.List("append");
    CHECK(rig.obj.IsRecording());
    rig.Int(128);
    CHECK(rig.obj.Count() == 3);
    CHECK(Tape(rig.obj) == "0 144 | 0 60 | 0 128");

    rig.List("record");
    rig.Int(176);
    CHECK(rig.obj.Count() == 1);
    CHECK(Tape(rig.obj) == "0 176");
  }

  TEST_CASE("seq: 'stop' ends recording (#502)") {
    // Max: "stops the sequencer if it is recording or playing."
    Rig rig;
    rig.List("record");
    rig.Int(144);
    rig.List("stop");
    CHECK_FALSE(rig.obj.IsRecording());
    rig.Int(60);
    CHECK(rig.obj.Count() == 1);
  }

  TEST_CASE("seq: 'clear' erases the sequence (#502)") {
    Rig rig;
    rig.List("record");
    rig.Int(144);
    rig.Int(60);
    rig.List("clear");
    CHECK(rig.obj.Count() == 0);
    CHECK(rig.obj.Position() == 0);
    CHECK_FALSE(rig.obj.IsRecording());
  }

  TEST_CASE("seq: bytes past the tape size are dropped (#502)") {
    // Refused rather than growing the table, which would allocate on whichever
    // thread the byte arrived on.
    Rig rig;
    rig.List("record");
    for (std::size_t i = 0; i < gSeq::MAX_EVENTS + 8; i++)
      rig.Int(64);
    CHECK(rig.obj.Count() == gSeq::MAX_EVENTS);
  }

  // ─── playing back ───────────────────────────────────────────────────────────

  TEST_CASE("seq: a bang plays the sequence out as individual bytes (#502)") {
    // Max: "the sequence stored in seq is sent out the outlet in the form of
    // individual MIDI bytes." A standalone object has no clock to wait on, so
    // the whole sequence walks out at once.
    Rig rig;
    rig.List("record");
    rig.List("144 60 112");
    rig.List("stop");
    rig.reset();

    rig.Bang();
    // The end bang lands *before* the final byte — see the next case.
    CHECK(rig.Out() == "i144,i60,!,i112");
    CHECK_FALSE(rig.obj.IsPlaying());
    CHECK(rig.obj.Position() == 3);
  }

  TEST_CASE("seq: the end bang is sent immediately before the final byte (#502)") {
    // Max, parenthetically and against every instinct: "(the bang is sent out
    // immediately before the final event of the sequence is played.)" Only a
    // test keeps this, and it is genuinely useful — a patch learns that the
    // byte about to arrive is the last one rather than finding out afterwards.
    Rig rig;
    rig.List("record");
    rig.Int(192);
    rig.Int(31);
    rig.List("stop");
    rig.reset();

    rig.List("start");
    CHECK(rig.Out() == "i192,!,i31");
  }

  TEST_CASE("seq: 'start' on an empty sequence plays and bangs nothing (#502)") {
    // There is no final event for the end bang to precede.
    Rig rig;
    rig.List("start");
    CHECK(rig.Out().empty());
    CHECK_FALSE(rig.obj.IsPlaying());
  }

  TEST_CASE("seq: 'start' rewinds rather than resuming (#502)") {
    // Max's bang "starts playing the sequence stored in seq" — the whole of it.
    Rig rig;
    rig.List("record");
    rig.List("144 60");
    rig.List("stop");
    rig.Bang();
    REQUIRE(rig.obj.Position() == 2);
    rig.reset();

    rig.Bang();
    CHECK(rig.Out() == "i144,!,i60");
  }

  TEST_CASE("seq: 'record' after playing takes over without a 'stop' (#502)") {
    // Max: "a stop message need not be received when switching directly from
    // playing to recording, or vice-versa."
    Rig rig;
    rig.List("record");
    rig.List("144 60");
    rig.List("record");
    CHECK(rig.obj.IsRecording());
    CHECK_FALSE(rig.obj.IsPlaying());
    CHECK(rig.obj.Count() == 0);
  }

  // ─── the speed multiplier ───────────────────────────────────────────────────

  TEST_CASE("seq: 'start <n>' stores Max's tempo multiplier (#502)") {
    // Max: "the message start 1024 indicates normal tempo. If the number is
    // 512, seq plays the sequence at half the original recorded speed, start
    // 2048 plays it back at twice the original speed."
    Rig rig;
    rig.List("record");
    rig.Int(144);
    rig.List("stop");

    rig.List("start 2048");
    CHECK(rig.obj.Speed() == 2048);
    rig.List("start 512");
    CHECK(rig.obj.Speed() == 512);
    rig.List("start");
    CHECK(rig.obj.Speed() == gSeq::NORMAL_SPEED);
  }

  TEST_CASE("seq: a multiplier that cannot scale a duration is refused (#502)") {
    // Zero and any negative other than Max's -1 would divide a wait into
    // nothing a clock can hold, so the recorded tempo is used instead.
    Rig rig;
    rig.List("record");
    rig.Int(144);
    rig.List("stop");

    rig.List("start 0");
    CHECK(rig.obj.Speed() == gSeq::NORMAL_SPEED);
    rig.List("start -7");
    CHECK(rig.obj.Speed() == gSeq::NORMAL_SPEED);
  }

  TEST_CASE("seq: 'start -1' selects Max's tick-driven mode (#502)") {
    Rig rig;
    rig.List("record");
    rig.Int(144);
    rig.List("stop");
    rig.reset();

    rig.List("start -1");
    CHECK(rig.obj.IsTickDriven());
    CHECK(rig.obj.IsPlaying());
    // Max: seq "waits for a tick message to advance its clock", so nothing has
    // happened yet — not even a first event recorded at delta 0.
    CHECK(rig.Out().empty());
  }

  TEST_CASE("seq: in tick mode a 'tick' advances the sequence (#502)") {
    // 48 ticks per second at the recorded tempo. With no patcher every delta is
    // 0, so the first tick is enough to run the whole sequence out.
    Rig rig;
    rig.List("record");
    rig.List("144 60 112");
    rig.List("stop");
    rig.reset();

    rig.List("start -1");
    rig.List("tick");
    CHECK(rig.Out() == "i144,i60,!,i112");
    CHECK_FALSE(rig.obj.IsPlaying());
  }

  TEST_CASE("seq: a 'tick' outside tick mode does nothing (#502)") {
    // The millisecond clock is already running the sequence there.
    Rig rig;
    rig.List("record");
    rig.List("144 60");
    rig.List("stop");
    rig.reset();

    rig.List("tick");
    CHECK(rig.Out().empty());
  }

  // ─── editing the tape ───────────────────────────────────────────────────────

  TEST_CASE("seq: 'delay' sets the onset of the first event (#502)") {
    // Max: "sets the onset time, in milliseconds, of the first event in the
    // recorded sequence. All events in the sequence are shifted so that the
    // first event occurs at the specified onset time." With deltas stored
    // rather than absolute times, that shift *is* writing the first delta.
    Rig rig;
    rig.List("record");
    rig.List("144 60");
    rig.List("stop");

    rig.List("delay 250");
    CHECK(Tape(rig.obj) == "250 144 | 0 60");
    // A negative onset is not a time; it floors at zero.
    rig.List("delay -5");
    CHECK(Tape(rig.obj) == "0 144 | 0 60");
  }

  TEST_CASE("seq: 'addeventdelay' adds to that onset (#502)") {
    // Max: "adds to the delay onset time, in milliseconds, of the first event
    // in the recorded sequence" — the same edit as delay, made relative.
    Rig rig;
    rig.List("record");
    rig.List("144 60");
    rig.List("stop");

    rig.List("delay 100");
    rig.List("addeventdelay 50");
    CHECK(Tape(rig.obj) == "150 144 | 0 60");
    rig.List("addeventdelay -200");
    CHECK(Tape(rig.obj) == "0 144 | 0 60");
  }

  TEST_CASE("seq: 'hook' multiplies every event time (#502)") {
    // Max: "multiplies all the event times in the stored sequence by that
    // number. For example, if the number is 2.0, all event times will be
    // doubled, and the sequence will play back twice as slowly."
    Rig rig;
    rig.List("record");
    rig.List("144 60");
    rig.List("stop");
    rig.List("delay 100");

    rig.List("hook 2.");
    CHECK(Tape(rig.obj) == "200 144 | 0 60");
    rig.List("hook 0.5");
    CHECK(Tape(rig.obj) == "100 144 | 0 60");
    // A multiplier of zero or less cannot scale a duration into anything a
    // clock can wait for, so the tape is left alone.
    rig.List("hook 0");
    CHECK(Tape(rig.obj) == "100 144 | 0 60");
    rig.List("hook -2");
    CHECK(Tape(rig.obj) == "100 144 | 0 60");
  }

  // ─── the words that do nothing ──────────────────────────────────────────────

  TEST_CASE("seq: 'read', 'write', 'dump' and 'print' are consumed and inert (#502)") {
    // File I/O cannot be done from a message handler at all today (#683 / #692)
    // and the patcher's log allocates and locks, which the same path must not.
    // Consumed rather than falling through, so none of them records a byte.
    Rig rig;
    rig.List("record");
    rig.List("read song.mid");
    rig.List("write song.mid 1");
    rig.List("dump");
    rig.List("print");
    CHECK(rig.obj.Count() == 0);
    CHECK(rig.Out().empty());
  }

  TEST_CASE("seq: a message that is neither a command nor a number does nothing (#502)") {
    Rig rig;
    rig.List("record");
    rig.List("wobble 3");
    rig.List("tempo 240");
    CHECK(rig.obj.Count() == 0);
  }

  TEST_CASE("seq: Calculate sends nothing (#502)") {
    // The object is driven by its inlet and by its own clock; one that emitted
    // would restart itself on every DSP tick.
    Rig rig;
    rig.List("record");
    rig.List("144 60");
    rig.List("stop");
    rig.reset();

    for (int i = 0; i < 8; i++)
      rig.obj.Calculate(YSE::T_DSP);
    CHECK(rig.Out().empty());
  }

  // ─── what a save carries ────────────────────────────────────────────────────

  TEST_CASE("seq: the filename survives a DumpJSON / ParseJSON round trip (#502)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::G_SEQ, "song.mid");
    REQUIRE(obj != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".seq") != std::string::npos);
    CHECK(json.find("song.mid") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".seq");
    CHECK(std::string(copy->GetParams()) == "song.mid");
  }

  TEST_CASE("seq: the sequence deliberately does not survive a save (#502)") {
    // The family rule is to save exactly where Max has a save flag: qlist saves
    // its cue list with the patcher and mtr has Max 8's embed. seq has neither,
    // because its contents live in a *file* reached by read / write — text's
    // answer, for text's reason. When the file half lands (#683 / #692) they
    // will live there too.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::G_SEQ);
    REQUIRE(obj != nullptr);
    obj->SetListData(0, "record");
    obj->SetListData(0, "144 60 112");
    obj->SetListData(0, "stop");

    const std::string json = src.DumpJSON();
    CHECK(json.find("\"state\"") == std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);

    Recorder out;
    YSE::pHandle outHandle(&out);
    loaded.Connect(copy, 0, &outHandle, 0);

    copy->SetListData(0, "start");
    CHECK(out.seen.empty());
  }

  // ─── the clock, which needs a real patcher ──────────────────────────────────

  TEST_CASE("seq: 'start' waits out each recorded gap (#502)") {
    // The whole round trip, and the property no standalone test can show: a gap
    // is measured off the patcher's block counter while recording and waited
    // out again on the same counter while playing. Both halves are asserted
    // through MillisForBlocks / BlocksForMillis at the live SAMPLERATE, so an
    // implementation that measured on a different clock than it waits on comes
    // back at the wrong speed here. The first gap is measured from the `record`
    // message itself, which is what gives Max's `delay` something to overwrite —
    // hence the wait before the *first* byte.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* seq = p.CreateObject(YSE::OBJ::G_SEQ, "");
    REQUIRE(seq != nullptr);

    Recorder data;
    YSE::pHandle dataHandle(&data);
    p.Connect(seq, 0, &dataHandle, 0);

    const std::uint64_t gap = 10;
    seq->SetListData(0, "record");
    for (std::uint64_t block = 0; block < gap; ++block)
      p.Calculate(YSE::T_DSP);
    seq->SetIntData(0, 144);
    for (std::uint64_t block = 0; block < gap; ++block)
      p.Calculate(YSE::T_DSP);
    seq->SetIntData(0, 60);
    seq->SetListData(0, "stop");
    data.reset();

    const int recorded = messageScheduler::MillisForBlocks(gap);
    const std::uint64_t due = messageScheduler::BlocksForMillis(recorded);

    seq->SetListData(0, "start");
    // Nothing goes out in the arming dispatch: the first byte has a gap of its
    // own and is waited out like every other one.
    CHECK(data.seen.empty());
    CHECK(p.Scheduler()->PendingCount() == 1);

    for (std::uint64_t block = 1; block < due; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(data.seen.empty());
    p.Calculate(YSE::T_DSP);
    CHECK(Joined(data.seen) == "i144");

    for (std::uint64_t block = 1; block <= due; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(Joined(data.seen) == "i144,i60");
    // The sequence finished rather than stalling on a step whose delivery never
    // came.
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  TEST_CASE("seq: the bytes of one MIDI message arrive in one audio block (#502)") {
    // The rule that separates a MIDI sequencer from a message sequencer, and
    // the one place this object deliberately departs from .qlist. The
    // scheduler's deadline floor is one block, so arming every event — even one
    // recorded at the same instant as its predecessor — would spread a note-on
    // across three blocks, which is not that note-on. A run of zero-delta
    // events has to finish inside the dispatch that reached it.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* seq = p.CreateObject(YSE::OBJ::G_SEQ, "");
    REQUIRE(seq != nullptr);

    Recorder data;
    YSE::pHandle dataHandle(&data);
    p.Connect(seq, 0, &dataHandle, 0);
    p.Connect(seq, 1, &dataHandle, 0);

    const std::uint64_t gap = 8;
    seq->SetListData(0, "record");
    for (std::uint64_t block = 0; block < gap; ++block)
      p.Calculate(YSE::T_DSP);
    // One dispatch, so all three bytes share a block: the first carries the gap
    // and the other two carry nothing.
    seq->SetListData(0, "144 60 112");
    seq->SetListData(0, "stop");

    const int recorded = messageScheduler::MillisForBlocks(gap);
    REQUIRE(recorded > 0);
    const std::uint64_t due = messageScheduler::BlocksForMillis(recorded);
    data.reset();

    seq->SetListData(0, "start");
    // One pending step for the whole message rather than three.
    CHECK(p.Scheduler()->PendingCount() == 1);

    for (std::uint64_t block = 1; block < due; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(data.seen.empty());

    // The single block that carries the message: all three bytes, with the end
    // bang in Max's position immediately before the last one.
    p.Calculate(YSE::T_DSP);
    CHECK(Joined(data.seen) == "i144,i60,!,i112");
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  TEST_CASE("seq: 'start 2048' halves every wait (#502)") {
    // Max: "start 2048 plays it back at twice the original speed", so the
    // multiplier divides. The sign of this is a coin flip if you do not read
    // the reference, and only a test pins it.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* seq = p.CreateObject(YSE::OBJ::G_SEQ, "");
    REQUIRE(seq != nullptr);

    Recorder data;
    YSE::pHandle dataHandle(&data);
    p.Connect(seq, 0, &dataHandle, 0);

    const std::uint64_t gap = 20;
    seq->SetListData(0, "record");
    for (std::uint64_t block = 0; block < gap; ++block)
      p.Calculate(YSE::T_DSP);
    seq->SetIntData(0, 144);
    seq->SetListData(0, "stop");
    data.reset();

    const int recorded = messageScheduler::MillisForBlocks(gap);
    const std::uint64_t scaled = messageScheduler::BlocksForMillis(recorded / 2);
    const std::uint64_t full = messageScheduler::BlocksForMillis(recorded);
    REQUIRE(scaled < full);

    seq->SetListData(0, "start 2048");
    for (std::uint64_t block = 1; block < scaled; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(data.seen.empty());
    p.Calculate(YSE::T_DSP);
    CHECK(Joined(data.seen) == "i144");
  }

  TEST_CASE("seq: 'stop' cancels the pending step (#502)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* seq = p.CreateObject(YSE::OBJ::G_SEQ, "");
    REQUIRE(seq != nullptr);

    Recorder data;
    YSE::pHandle dataHandle(&data);
    p.Connect(seq, 0, &dataHandle, 0);

    const std::uint64_t gap = 16;
    seq->SetListData(0, "record");
    for (std::uint64_t block = 0; block < gap; ++block)
      p.Calculate(YSE::T_DSP);
    seq->SetIntData(0, 144);
    seq->SetListData(0, "stop");
    data.reset();

    seq->SetListData(0, "start");
    REQUIRE(p.Scheduler()->PendingCount() == 1);
    seq->SetListData(0, "stop");
    CHECK(p.Scheduler()->PendingCount() == 0);

    const std::uint64_t due =
        messageScheduler::BlocksForMillis(messageScheduler::MillisForBlocks(gap));
    for (std::uint64_t block = 0; block <= due + 2; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(data.seen.empty());
  }

  TEST_CASE("seq: 'start -1' ignores the clock and advances on 'tick' (#502)") {
    // Max: "starts the sequencer, but rather than follow Max's millisecond
    // clock, seq waits for a tick message to advance its clock", and "in order
    // to play the sequence at its original recorded tempo, seq must receive 48
    // tick messages per second." A patcher still rendering underneath must not
    // move the sequence at all.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* seq = p.CreateObject(YSE::OBJ::G_SEQ, "");
    REQUIRE(seq != nullptr);

    Recorder data;
    YSE::pHandle dataHandle(&data);
    p.Connect(seq, 0, &dataHandle, 0);

    const std::uint64_t gap = 24;
    seq->SetListData(0, "record");
    for (std::uint64_t block = 0; block < gap; ++block)
      p.Calculate(YSE::T_DSP);
    seq->SetIntData(0, 144);
    seq->SetListData(0, "stop");
    data.reset();

    const int recorded = messageScheduler::MillisForBlocks(gap);
    REQUIRE(recorded > 0);

    seq->SetListData(0, "start -1");
    // No scheduler slot is taken and no amount of rendering advances it.
    CHECK(p.Scheduler()->PendingCount() == 0);
    for (std::uint64_t block = 0; block < 4 * gap; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(data.seen.empty());

    // 48 ticks is one second, so the byte falls due on the first tick whose
    // elapsed time reaches its recorded onset.
    const int ticksNeeded = ((recorded * gSeq::TICKS_PER_SECOND) + 999) / 1000;
    REQUIRE(ticksNeeded > 0);
    for (int tick = 0; tick < ticksNeeded - 1; tick++)
      seq->SetListData(0, "tick");
    CHECK(data.seen.empty());
    seq->SetListData(0, "tick");
    CHECK(Joined(data.seen) == "i144");
  }

  TEST_CASE("seq: deleting the object drops its pending steps safely (#502)") {
    // Armed possibly long before it is due — far outside the reclaimer's
    // two-block grace — so delivery must re-resolve the target and find
    // nothing. An ASan build trips here if the retired object is touched.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* seq = p.CreateObject(YSE::OBJ::G_SEQ, "");
    REQUIRE(seq != nullptr);

    Recorder data;
    YSE::pHandle dataHandle(&data);
    p.Connect(seq, 0, &dataHandle, 0);

    seq->SetListData(0, "record");
    seq->SetIntData(0, 144);
    seq->SetListData(0, "stop");
    seq->SetListData(0, "delay 200");
    seq->SetListData(0, "start");
    REQUIRE(p.Scheduler()->PendingCount() == 1);

    p.DeleteObject(seq);
    const std::uint64_t due = messageScheduler::BlocksForMillis(200);
    for (std::uint64_t block = 1; block <= due + 2; ++block)
      p.Calculate(YSE::T_DSP);
    CHECK(data.seen.empty());
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

} // TEST_SUITE
