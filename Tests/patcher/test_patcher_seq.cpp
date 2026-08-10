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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "headers/constants.hpp"
#include "patcher/genericObjects/gSeq.h"
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
#include "support/alloc_probe.hpp"

using TestHelpers::Wire;
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
        Wire(obj, i, out);
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

  // ─── file helpers (issue #692) ────────────────────────────────────────────

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

  // Hand a standalone .seq the bytes of a file, exactly as the scheduler's
  // completion would. The parser is grammar rather than timing, so it belongs in
  // the standalone layer with the rest of the grammar: no patcher, no background
  // job and no disk, and the tape directly readable afterwards. The tag is
  // .seq's private read tag, which is 0.
  void Deliver(Rig& rig, const std::string& contents) {
    YSE::PATCHER::fileResult result;
    result.tag = 0;
    result.op = YSE::PATCHER::FILE_OP::READ;
    result.ok = true;
    result.bytes = contents.data();
    result.byteCount = contents.size();
    rig.obj.DeliverFileResult(result, YSE::T_GUI);
  }

  // A .seq inside a real patcher, with one recorder on Max's three outlets — so
  // the relative order of a byte, a meta message and the end bang is visible —
  // and a second on the file outlet. Declared before the patcher so the patcher
  // is torn down first, while the recorders it is wired to still exist.
  struct FileRig {
    Recorder out;
    Recorder file;
    YSE::pHandle outHandle;
    YSE::pHandle fileHandle;
    patcherImplementation p;
    YSE::pHandle* seq = nullptr;

    explicit FileRig(const std::string& args = "")
      : outHandle(&out), fileHandle(&file), p(1, nullptr) {
      seq = p.CreateObject(YSE::OBJ::G_SEQ, args);
      REQUIRE(seq != nullptr);
      p.Connect(seq, 0, &outHandle, 0);
      p.Connect(seq, 1, &outHandle, 0);
      p.Connect(seq, 2, &outHandle, 0);
      p.Connect(seq, 3, &fileHandle, 0);
    }

    void List(const std::string& message) {
      seq->SetListData(0, message);
    }
    void Int(int value) {
      seq->SetIntData(0, value);
    }

    // Drive the patcher until its file requests have landed. The two halves are
    // deterministic for different reasons: WaitIdle joins the background pool's
    // jobs (so no sleep and no polling), and the single Calculate is the
    // dispatch frame the completions are handed out in — there is deliberately
    // no other way for them to arrive.
    void Settle() {
      YSE::PATCHER::fileScheduler* io = p.FileIO();
      REQUIRE(io != nullptr);
      io->WaitIdle();
      p.Calculate(YSE::T_DSP);
    }

    // The whole sequence as the outlets deliver it. Only meaningful where every
    // gap is zero — a positive one arms a step and the walk stops there, which
    // is what the clock case at the end of the file is for.
    std::string PlayOut() {
      out.reset();
      List("start");
      return Joined(out.seen);
    }
  };

  // ─── building standard MIDI files by hand ─────────────────────────────────
  //
  // The file cases assert against bytes rather than against the writer's own
  // output wherever they can, because a reader tested only against its own
  // writer proves the two agree and nothing else.

  void PutByte(std::string& out, unsigned char value) {
    out.push_back((char)value);
  }

  void PutU16(std::string& out, unsigned int value) {
    PutByte(out, (unsigned char)((value >> 8) & 0xFF));
    PutByte(out, (unsigned char)(value & 0xFF));
  }

  void PutU32(std::string& out, unsigned int value) {
    PutU16(out, (value >> 16) & 0xFFFF);
    PutU16(out, value & 0xFFFF);
  }

  // A variable-length quantity: seven bits per byte, high bit set on all but the
  // last.
  void PutVarLen(std::string& out, unsigned int value) {
    unsigned char buffer[4];
    std::size_t written = 0;
    buffer[written++] = (unsigned char)(value & 0x7F);
    value >>= 7;
    while (value != 0) {
      buffer[written++] = (unsigned char)((value & 0x7F) | 0x80);
      value >>= 7;
    }
    while (written > 0)
      PutByte(out, buffer[--written]);
  }

  std::string MidiHeader(unsigned int format, unsigned int tracks, unsigned int division) {
    std::string out;
    out += "MThd";
    PutU32(out, 6);
    PutU16(out, format);
    PutU16(out, tracks);
    PutU16(out, division);
    return out;
  }

  // Wrap an already-built event stream as an MTrk chunk, adding the
  // end-of-track event every chunk has to finish with.
  std::string MidiTrack(const std::string& events) {
    std::string body = events;
    PutVarLen(body, 0);
    PutByte(body, 0xFF);
    PutByte(body, 0x2F);
    PutByte(body, 0x00);

    std::string out;
    out += "MTrk";
    PutU32(out, (unsigned int)body.size());
    out += body;
    return out;
  }

  void PutEvent(std::string& out, unsigned int delta, std::initializer_list<int> bytes) {
    PutVarLen(out, delta);
    for (int byte : bytes)
      PutByte(out, (unsigned char)byte);
  }

  void PutMeta(std::string& out, unsigned int delta, unsigned char type, const std::string& data) {
    PutVarLen(out, delta);
    PutByte(out, 0xFF);
    PutByte(out, type);
    PutVarLen(out, (unsigned int)data.size());
    out += data;
  }

  // A sink that counts rather than records. The Recorder above builds a
  // std::string and pushes it onto a vector for every value it sees, which is
  // fine everywhere except inside the allocation probe at the end of this file —
  // there the sink's own bookkeeping would be indistinguishable from the parse's.
  struct Counter : YSE::PATCHER::pObject {
    int bangs = 0;
    int ints = 0;

    Counter() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { bangs++; });
      inputs.back().RegisterInt([this](int, int, YSE::THREAD) { ints++; });
    }
    const char* Type() const override {
      return "seq_counter";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A tempo meta's three bytes, from beats per minute.
  std::string TempoBytes(double bpm) {
    const unsigned int micros = (unsigned int)std::llround(60000000.0 / bpm);
    std::string out;
    PutByte(out, (unsigned char)((micros >> 16) & 0xFF));
    PutByte(out, (unsigned char)((micros >> 8) & 0xFF));
    PutByte(out, (unsigned char)(micros & 0xFF));
    return out;
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

  TEST_CASE("seq: Max's shape is one inlet and three outlets, plus the file one (#502, #692)") {
    // Max's left outlet carries the bytes, its middle one the end bang and its
    // right one the MIDI-file meta messages. The meta outlet landed with the
    // file half (#692) and is *appended*, which is the family's rule and here
    // also happens to be Max's own position for it. The file outlet after it is
    // a documented departure — Max's read is synchronous where this one cannot
    // be.
    gSeq obj;
    CHECK(obj.NumInputs() == 1);
    CHECK(obj.NumOutputs() == 4);
    CHECK(obj.GetCategory() == YSE::PATCHER::pCategory::MIDI);
  }

  TEST_CASE("seq: a fresh object is empty, stopped and at the recorded tempo (#502)") {
    gSeq obj;
    CHECK(obj.Count() == 0);
    CHECK(obj.MetaCount() == 0);
    CHECK(obj.Position() == 0);
    CHECK_FALSE(obj.IsRecording());
    CHECK_FALSE(obj.IsPlaying());
    CHECK(obj.Speed() == gSeq::NORMAL_SPEED);
    CHECK_FALSE(obj.IsTickDriven());
    CHECK(obj.Filename().empty());
    // Max's three tempo attributes, at their defaults with nothing read (#692).
    CHECK(obj.Tempo() == doctest::Approx(gSeq::DEFAULT_TEMPO));
    CHECK(obj.SequenceTempo() == doctest::Approx(gSeq::DEFAULT_TEMPO));
    CHECK_FALSE(obj.OverridesTempo());
  }

  TEST_CASE("seq: the creation argument is Max's filename (#502)") {
    // Max: "specifies the name of a file to be read into seq automatically when
    // the patch is loaded." Since #692 it also seeds both halves of the file
    // surface, so a bare `read` or `write` has a name to fall back on.
    gSeq obj;
    obj.SetParams("song.mid");
    CHECK(obj.Filename() == "song.mid");
    CHECK(obj.ReadFile() == "song.mid");
    CHECK(obj.WriteFile() == "song.mid");
    obj.SetParams("");
    CHECK(obj.Filename().empty());
    CHECK(obj.ReadFile().empty());
    CHECK(obj.WriteFile().empty());
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

  TEST_CASE("seq: 'dump' and 'print' are consumed and inert (#502, #692)") {
    // `dump` opens a file in an editing window this patcher is headless for, and
    // the patcher's log allocates and takes a lock, which a path that may be the
    // audio callback must not. Consumed rather than falling through, so neither
    // records a byte.
    Rig rig;
    rig.List("record");
    rig.List("dump");
    rig.List("print");
    CHECK(rig.obj.Count() == 0);
    CHECK(rig.Out().empty());
  }

  TEST_CASE("seq: a standalone object consumes read and write without doing anything (#692)") {
    // No patcher means no file scheduler, and the handler gives up quietly —
    // silently, because it may be the audio thread, where a log line would
    // allocate. Consumed rather than falling through, so neither records a byte.
    Rig rig;
    rig.List("record");
    rig.List("read song.mid");
    rig.List("write song.mid 1");
    CHECK(rig.obj.Count() == 0);
    CHECK(rig.Out().empty());
    // Nothing is remembered either: the handler gives up before it gets that
    // far, which is what the three siblings do.
    CHECK(rig.obj.ReadFile().empty());
    CHECK(rig.obj.WriteFile().empty());
  }

  TEST_CASE("seq: 'tempo' and 'overridetempo' are Max's attributes (#692)") {
    Rig rig;
    rig.List("tempo 90");
    CHECK(rig.obj.Tempo() == doctest::Approx(90.f));
    // A tempo that cannot convert a duration into anything is refused and the
    // previous one kept.
    rig.List("tempo 0");
    rig.List("tempo -60");
    CHECK(rig.obj.Tempo() == doctest::Approx(90.f));

    CHECK_FALSE(rig.obj.OverridesTempo());
    rig.List("overridetempo 1");
    CHECK(rig.obj.OverridesTempo());
    rig.List("overridetempo 0");
    CHECK_FALSE(rig.obj.OverridesTempo());

    // Max's sequencetempo is read-only, "a read-only value for convenience
    // purposes", so it is consumed and changes nothing.
    rig.List("sequencetempo 240");
    CHECK(rig.obj.SequenceTempo() == doctest::Approx(gSeq::DEFAULT_TEMPO));

    // And none of the three is data: a recording gets no bytes out of them.
    rig.List("record");
    rig.List("tempo 90");
    rig.List("overridetempo 1");
    rig.List("sequencetempo 240");
    CHECK(rig.obj.Count() == 0);
  }

  TEST_CASE("seq: a message that is neither a command nor a number does nothing (#502)") {
    Rig rig;
    rig.List("record");
    rig.List("wobble 3");
    rig.List("nonsense");
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
    // answer, for text's reason, and since #692 those two really do reach one.
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

  // ─── reading a sequence: the parser (issue #692) ────────────────────────────
  //
  // The parser is *grammar*, so it belongs in the standalone layer with the rest
  // of the grammar: a completion is handed to the object directly, exactly as
  // the scheduler's would be, with no patcher, no background job and no disk in
  // the way. That leaves the tape and the tempo attributes directly readable,
  // and it means a playback walk runs straight through — a standalone object has
  // no clock — so one `start` shows the whole sequence in the order a patch
  // would hear it.
  //
  // The files are built byte by byte rather than taken from this object's own
  // writer, because a reader tested only against its own writer proves that the
  // two agree and nothing else.

  TEST_CASE("seq: a hand-built format 0 MIDI file loads as bytes, metas and gaps (#692)") {
    // The acceptance criterion for the read half. 480 ticks at the 120 BPM the
    // file declares is one quarter note, which is 500 ms.
    std::string events;
    PutMeta(events, 0, 0x51, TempoBytes(120.0));
    PutEvent(events, 0, {0x90, 60, 100});
    PutEvent(events, 480, {0x80, 60, 0});

    Rig rig;
    Deliver(rig, MidiHeader(0, 1, 480) + MidiTrack(events));

    CHECK(Tape(rig.obj) == "0 meta tempo | 0 144 | 0 60 | 0 100 | 500 128 | 0 60 | 0 0 | "
                           "0 meta endoftrack");
    // The tempo meta and the chunk's own end-of-track, which Max names.
    CHECK(rig.obj.MetaCount() == 2);
    // Max: sequencetempo is "the unmodified tempo of the sequence", and tempo
    // "reflects the current tempo at the current playback time".
    CHECK(rig.obj.SequenceTempo() == doctest::Approx(120.f));
    CHECK(rig.obj.Tempo() == doctest::Approx(120.f));
    // The read bangs the file outlet, and only after the sequence is in place.
    CHECK(rig.Out() == "!");
  }

  TEST_CASE("seq: a loaded sequence plays bytes and metas out in order (#692)") {
    // What a patch actually hears. A standalone object has no clock, so the
    // whole sequence walks through in one dispatch and the relative order of a
    // byte, a meta message and the end bang is visible at once.
    std::string events;
    PutMeta(events, 0, 0x03, "Lead");
    PutEvent(events, 0, {0x90, 60, 100});
    PutMeta(events, 0, 0x06, "verse");
    PutEvent(events, 0, {0x80, 60, 0});
    PutMeta(events, 0, 0x58, std::string("\x04\x02\x18\x08", 4));

    Rig rig;
    Deliver(rig, MidiHeader(0, 1, 480) + MidiTrack(events));
    rig.reset();

    rig.List("start");
    // A text meta sends its text, a time signature its bytes as numbers, and
    // Max's end bang still comes immediately before the final event — which here
    // is the end-of-track meta.
    CHECK(rig.Out() == "smeta sequenceortrackname Lead,i144,i60,i100,smeta marker verse,"
                       "i128,i60,i0,smeta timesignature 4 2 24 8,!,smeta endoftrack");
  }

  TEST_CASE("seq: a meta type Max has no name for sends its type number (#692)") {
    // Swallowing it would leave a patch unable to tell two of them apart.
    std::string events;
    PutMeta(events, 0, 0x33, std::string("\x01\x02", 2));

    Rig rig;
    Deliver(rig, MidiHeader(0, 1, 480) + MidiTrack(events));
    rig.reset();

    rig.List("start");
    CHECK(rig.Out() == "smeta 51 1 2,!,smeta endoftrack");
  }

  TEST_CASE("seq: a format 1 file is merged into one sequence (#692)") {
    // Max reads "standard MIDI files (format 0 and format 1)", and a format 1
    // file is several chunks running at once. They are merged by absolute tick,
    // ties going to the lower-numbered chunk so the order is the file's own.
    std::string first;
    PutEvent(first, 0, {0x90, 60, 100});
    PutEvent(first, 960, {0x80, 60, 0});
    std::string second;
    PutEvent(second, 480, {0x91, 67, 90});

    Rig rig;
    Deliver(rig, MidiHeader(1, 2, 480) + MidiTrack(first) + MidiTrack(second));

    // At 120 BPM (the default the file does not override), 480 ticks is 500 ms
    // and 960 is 1000 — so the second chunk's note falls *between* the first
    // chunk's two, which is the whole point of the merge.
    CHECK(Tape(rig.obj) == "0 144 | 0 60 | 0 100 | 500 145 | 0 67 | 0 90 | 0 meta endoftrack | "
                           "500 128 | 0 60 | 0 0 | 0 meta endoftrack");
  }

  TEST_CASE("seq: running status and system exclusive both load (#692)") {
    // Max: seq "supports channel and system exclusive messages". Running status
    // is what a real file is full of — the status byte is written once and the
    // data bytes that follow belong to it — and the tape has to get it back with
    // the status byte spelled out, because that is what a MIDI port expects.
    std::string events;
    PutEvent(events, 0, {0x90, 60, 100});
    PutEvent(events, 0, {62, 100}); // running status: the same note-on
    PutVarLen(events, 0);
    PutByte(events, 0xF0);
    PutVarLen(events, 3);
    PutByte(events, 0x7E);
    PutByte(events, 0x09);
    PutByte(events, 0xF7);

    Rig rig;
    Deliver(rig, MidiHeader(0, 1, 480) + MidiTrack(events));

    CHECK(Tape(rig.obj) == "0 144 | 0 60 | 0 100 | 0 144 | 0 62 | 0 100 | 0 240 | 0 126 | 0 9 | "
                           "0 247 | 0 meta endoftrack");
  }

  TEST_CASE("seq: Max's text format loads with its absolute times as gaps (#692)") {
    // Max reads "a start time in milliseconds (the time elapsed since the
    // beginning of the sequence) followed by the (space-separated) bytes of a
    // MIDI message recorded at that start time". Absolute there, deltas here,
    // and the bytes of one line share one start time — which is exactly what
    // playback runs out together. A CRLF file loads as the lines it looks like.
    Rig rig;
    Deliver(rig, "0 144 60 100\r\n500 128 60 0\r\n");

    CHECK(Tape(rig.obj) == "0 144 | 0 60 | 0 100 | 500 128 | 0 60 | 0 0");
    CHECK(rig.obj.MetaCount() == 0);
  }

  TEST_CASE("seq: a text line without a leading time is not an event (#692)") {
    Rig rig;
    Deliver(rig, "\n0 144 60\nnot a line\n\n100 128 60\n");
    CHECK(Tape(rig.obj) == "0 144 | 0 60 | 100 128 | 0 60");
  }

  TEST_CASE("seq: the file's tempo map times the sequence, and overridetempo wins (#692)") {
    // The gap between the two notes is one quarter note. At the 60 BPM the file
    // declares that is 1000 ms; with `overridetempo 1` and `tempo 120` the
    // file's request is ignored and it is 500 ms.
    std::string events;
    PutMeta(events, 0, 0x51, TempoBytes(60.0));
    PutEvent(events, 0, {0x90, 60, 100});
    PutEvent(events, 480, {0x80, 60, 0});
    const std::string file = MidiHeader(0, 1, 480) + MidiTrack(events);

    Rig rig;
    Deliver(rig, file);
    CHECK(rig.obj.EventAt(4) == "1000 128");
    CHECK(rig.obj.SequenceTempo() == doctest::Approx(60.f));
    CHECK(rig.obj.Tempo() == doctest::Approx(60.f));

    // Max: "the value of the tempo attribute will override any tempo requested
    // by the sequence." It applies to the next read, the tape being
    // milliseconds rather than ticks — see the class documentation.
    rig.List("overridetempo 1");
    rig.List("tempo 120");
    Deliver(rig, file);
    CHECK(rig.obj.EventAt(4) == "500 128");
    // The sequence's own tempo is still reported unmodified, and the attribute
    // is not overwritten by the file's.
    CHECK(rig.obj.SequenceTempo() == doctest::Approx(60.f));
    CHECK(rig.obj.Tempo() == doctest::Approx(120.f));
  }

  TEST_CASE("seq: a tempo meta passing the playhead sets the tempo attribute (#692)") {
    // Max: "if the seq has read a MIDI file with tempo information, the tempo
    // attribute will reflect the current tempo at the current playback time."
    // Literally that — the tempo the playhead has just gone past.
    std::string events;
    PutMeta(events, 0, 0x51, TempoBytes(60.0));
    PutEvent(events, 0, {0x90, 60, 100});
    PutMeta(events, 0, 0x51, TempoBytes(240.0));
    PutEvent(events, 0, {0x80, 60, 0});
    const std::string file = MidiHeader(0, 1, 480) + MidiTrack(events);

    Rig rig;
    Deliver(rig, file);
    CHECK(rig.obj.Tempo() == doctest::Approx(60.f));

    // Every gap is zero, so one `start` walks the whole sequence and goes past
    // both tempo metas; the last one is what is left in force.
    rig.List("start");
    CHECK(rig.obj.Tempo() == doctest::Approx(240.f));
    CHECK(rig.obj.SequenceTempo() == doctest::Approx(60.f));

    // And with the attribute overriding, the sequence does not move it.
    rig.List("overridetempo 1");
    rig.List("tempo 90");
    Deliver(rig, file);
    rig.List("start");
    CHECK(rig.obj.Tempo() == doctest::Approx(90.f));
  }

  TEST_CASE("seq: an SMPTE division is absolute time, whatever the tempo says (#692)") {
    // A negative division is frames per second and ticks per frame, so 25 x 40
    // ticks is one second no matter what a tempo meta asks for.
    std::string events;
    PutMeta(events, 0, 0x51, TempoBytes(240.0));
    PutEvent(events, 0, {0x90, 60, 100});
    PutEvent(events, 1000, {0x80, 60, 0});

    Rig rig;
    // -25 frames per second, 40 ticks per frame.
    Deliver(rig, MidiHeader(0, 1, 0xE728) + MidiTrack(events));
    CHECK(rig.obj.EventAt(4) == "1000 128");
  }

  TEST_CASE("seq: a read replaces the sequence and stops the transport (#692)") {
    // Max's read loads a file into the object; it is not a merge. The transport
    // goes with the sequence, which is `.mtr`'s rule and where both part company
    // with `.qlist`: a sequence left playing would already have a step armed at
    // a delta belonging to a tape that no longer exists.
    Rig rig;
    rig.List("record");
    rig.List("144 60 112");
    rig.List("stop");
    REQUIRE(rig.obj.Count() == 3);

    Deliver(rig, "0 176 7 64\n");
    CHECK(Tape(rig.obj) == "0 176 | 0 7 | 0 64");
    CHECK_FALSE(rig.obj.IsPlaying());
    CHECK_FALSE(rig.obj.IsRecording());
    CHECK(rig.obj.Position() == 0);
  }

  TEST_CASE("seq: events past the tape size stop the read rather than growing it (#692)") {
    // The bound is the tape's, as it is for a recorded byte — growing it would
    // allocate on whichever thread the completion is delivered on, which is the
    // audio one.
    std::string events;
    for (std::size_t i = 0; i < (gSeq::MAX_EVENTS / 3) + 40; i++)
      PutEvent(events, 0, {0x90, 60, 100});

    Rig rig;
    Deliver(rig, MidiHeader(0, 1, 480) + MidiTrack(events));
    // A MIDI message is refused whole, so the tape never ends with a status byte
    // whose data bytes did not fit — 4096 is divisible by neither three nor one
    // more, so the last message that fits is the last one stored.
    CHECK(rig.obj.Count() == gSeq::MAX_EVENTS - (gSeq::MAX_EVENTS % 3));
  }

  TEST_CASE("seq: metas past the meta table are dropped and the sequence loads on (#692)") {
    // A meta is information *about* the music rather than part of it, so losing
    // one is not losing the take — unlike a byte, which stops the read.
    std::string events;
    for (std::size_t i = 0; i < gSeq::META_CAPACITY + 8; i++)
      PutMeta(events, 0, 0x06, "m");
    PutEvent(events, 0, {0x90, 60, 100});

    Rig rig;
    Deliver(rig, MidiHeader(0, 1, 480) + MidiTrack(events));
    CHECK(rig.obj.MetaCount() == gSeq::META_CAPACITY);
    // The note still made it, which is the point.
    CHECK(rig.obj.EventAt(gSeq::META_CAPACITY) == "0 144");
  }

  TEST_CASE("seq: an over-long meta payload is dropped, not truncated (#692)") {
    std::string events;
    PutMeta(events, 0, 0x02, std::string(gSeq::META_BYTES_CAPACITY + 1, 'x'));
    PutEvent(events, 0, {0x90, 60, 100});

    Rig rig;
    Deliver(rig, MidiHeader(0, 1, 480) + MidiTrack(events));
    // Only the end-of-track meta survives, and the note is unaffected.
    CHECK(Tape(rig.obj) == "0 144 | 0 60 | 0 100 | 0 meta endoftrack");
  }

  TEST_CASE("seq: a file that is not a sequence leaves the one loaded alone (#692)") {
    // The header is validated *before* anything is cleared, which is what makes
    // this possible at all.
    Rig rig;
    Deliver(rig, "0 144 60 112\n");
    REQUIRE(Tape(rig.obj) == "0 144 | 0 60 | 0 112");
    rig.reset();

    // A well-formed magic with a division of zero is not a division.
    Deliver(rig, MidiHeader(0, 1, 0));
    CHECK(Tape(rig.obj) == "0 144 | 0 60 | 0 112");
    // A header claiming a length that runs off the end.
    Deliver(rig, std::string("MThd\xFF\xFF\xFF\xFF\x00\x00\x00\x01\x01\xE0", 14));
    CHECK(Tape(rig.obj) == "0 144 | 0 60 | 0 112");
    // And a header with no track chunk behind it at all.
    Deliver(rig, MidiHeader(0, 1, 480));
    CHECK(Tape(rig.obj) == "0 144 | 0 60 | 0 112");
    // None of the three bangs the file outlet.
    CHECK(rig.Out().empty());
  }

  TEST_CASE("seq: a file declaring more chunks than the merge holds is refused whole (#692)") {
    // Refused rather than half read: a merge missing a chunk would silently be a
    // different sequence, and the refusal happens before anything is cleared.
    Rig rig;
    rig.List("record");
    rig.Int(144);
    rig.List("stop");
    rig.reset();

    const std::size_t chunks = gSeq::MAX_FILE_TRACKS + 1;
    std::string file = MidiHeader(1, (unsigned int)chunks, 480);
    for (std::size_t i = 0; i < chunks; i++) {
      std::string events;
      PutEvent(events, 0, {0x90, 60, 100});
      file += MidiTrack(events);
    }

    Deliver(rig, file);
    CHECK(Tape(rig.obj) == "0 144");
    CHECK(rig.Out().empty());
  }

  TEST_CASE("seq: 'clear' erases the metas with the bytes (#692)") {
    Rig rig;
    Deliver(rig, MidiHeader(0, 1, 480) + MidiTrack(std::string()));
    REQUIRE(rig.obj.MetaCount() == 1);
    rig.List("clear");
    CHECK(rig.obj.Count() == 0);
    CHECK(rig.obj.MetaCount() == 0);
  }

  // ─── sequence files on disk (issue #692) ────────────────────────────────────
  //
  // Every case here needs a real patcherImplementation and a real file: the
  // background job, the host-honouring read and the completion delivered into a
  // dispatch frame only exist there, and a standalone object deliberately has
  // none of them. The object is only reachable through its ports here, which is
  // the right level anyway — what has to come back is a sequence a patch can
  // *play*.

  TEST_CASE("seq: a write then a read round-trips the sequence (#692)") {
    // The acceptance criterion for the write half, end to end through a real
    // patcher, a real file on disk and real recorders.
    const std::string path = TempFile("yse_seq_roundtrip_692.mid");
    FileRig rig;

    // No block is rendered between these, so every gap is 0 and the file is
    // deterministic — the timing itself is pinned by the clock case below.
    rig.List("record");
    rig.List("144 60 112");
    rig.List("stop");

    rig.List("write " + path);
    rig.Settle();

    // A standard MIDI file, which is what Max's write produces: the header
    // chunk, format 0, one track chunk.
    const std::string written = ReadWholeFile(path);
    REQUIRE(written.size() > 18);
    CHECK(written.compare(0, 4, "MThd") == 0);
    CHECK((unsigned char)written[8] == 0);
    CHECK((unsigned char)written[9] == 0);
    CHECK((unsigned char)written[10] == 0);
    CHECK((unsigned char)written[11] == 1);
    CHECK(written.compare(14, 4, "MTrk") == 0);
    // Max has no outlet for a finished write anywhere in this family, and
    // neither does this.
    CHECK(rig.file.seen.empty());

    rig.List("clear");
    rig.List("read " + path);
    rig.Settle();
    // Exactly one bang, and only after the sequence is in place.
    REQUIRE(Joined(rig.file.seen) == "!");

    // The bytes come back as they went in. The two metas around them are the
    // file's own doing: a written file declares the tempo it was written at, and
    // every chunk ends with an end-of-track event.
    CHECK(rig.PlayOut() == "smeta tempo 120.,i144,i60,i112,!,smeta endoftrack");

    Remove(path);
  }

  TEST_CASE("seq: a second round trip changes nothing (#692)") {
    // The file the writer produces has to be a fixed point, or every save and
    // load cycle would add another tempo declaration. It is: a tape that already
    // opens with a tempo meta is written with that one rather than a fresh one.
    const std::string path = TempFile("yse_seq_fixedpoint_692.mid");
    FileRig rig;

    rig.List("record");
    rig.List("144 60 112");
    rig.List("stop");
    rig.List("write " + path);
    rig.Settle();
    rig.List("read " + path);
    rig.Settle();
    const std::string once = rig.PlayOut();
    CHECK(once == "smeta tempo 120.,i144,i60,i112,!,smeta endoftrack");
    const std::string first = ReadWholeFile(path);

    rig.List("write " + path);
    rig.Settle();
    CHECK(ReadWholeFile(path) == first);
    rig.List("read " + path);
    rig.Settle();
    CHECK(rig.PlayOut() == once);

    Remove(path);
  }

  TEST_CASE("seq: 'write <name> 1' writes Max's multi-track file, split by channel (#692)") {
    // Max: "a non-zero int argument creates a multi-track (format 1) MIDI file."
    // A flat byte tape has no track structure of its own, so the split it does
    // have is the MIDI channel — a conductor chunk for the metas and anything
    // with no channel, then one chunk per channel the sequence uses.
    const std::string path = TempFile("yse_seq_format1_692.mid");
    FileRig rig;

    rig.List("record");
    rig.List("144 60 100");
    rig.List("145 62 100");
    rig.List("stop");
    rig.List("write " + path + " 1");
    rig.Settle();

    const std::string written = ReadWholeFile(path);
    REQUIRE(written.size() > 14);
    // Format 1, and three chunks: the conductor plus one per channel.
    CHECK((unsigned char)written[9] == 1);
    CHECK((unsigned char)written[10] == 0);
    CHECK((unsigned char)written[11] == 3);

    // And it reads back as one sequence, which is what the merge is for. Every
    // chunk starts at tick 0, so ties go to the lower-numbered chunk and the
    // conductor's events come first.
    rig.List("clear");
    rig.List("read " + path);
    rig.Settle();
    CHECK(rig.PlayOut() == "smeta tempo 120.,smeta endoftrack,i144,i60,i100,smeta endoftrack,"
                           "i145,i62,i100,!,smeta endoftrack");

    Remove(path);
  }

  TEST_CASE("seq: a trailing bare integer is the format argument, not the name (#692)") {
    // The family's rule is that the whole remainder is the path, so a name with
    // spaces works. Max's optional format argument has to come off the end
    // without breaking that — and only when there is a name in front of it to
    // take it from, so a file whose whole name is a number can still be written.
    const std::string spaced = TempFile("yse seq spaced 692.mid");
    const std::string plain = TempFile("yse_seq_plain_692.mid");
    const std::string numeric = TempFile("692");
    FileRig rig;

    rig.List("record");
    rig.List("144 60 100");
    rig.List("stop");

    // A name with spaces in it, and Max's format argument off the end of it.
    rig.List("write " + spaced + " 1");
    rig.Settle();
    CHECK(ReadWholeFile(spaced).compare(0, 4, "MThd") == 0);
    CHECK((unsigned char)ReadWholeFile(spaced)[9] == 1);

    // No argument is format 0.
    rig.List("write " + plain);
    rig.Settle();
    CHECK((unsigned char)ReadWholeFile(plain)[9] == 0);

    // And a bare number with nothing in front of it is the name, not a format.
    rig.List("write " + numeric);
    rig.Settle();
    CHECK(ReadWholeFile(numeric).compare(0, 4, "MThd") == 0);

    Remove(spaced);
    Remove(plain);
    Remove(numeric);
  }

  TEST_CASE("seq: read does nothing in the message handler (#692)") {
    // The reason the plumbing exists. A `read` may be dispatched on the audio
    // callback, so the handler must not open anything — which is observable: the
    // sequence is still empty when the message returns, and only a rendered
    // block puts the file on it.
    const std::string path = TempFile("yse_seq_deferred_692.txt");
    WriteWholeFile(path, "0 144 60 100\n");
    FileRig rig;

    rig.List("read " + path);
    // Nothing yet: a `start` on an empty sequence plays nothing at all. The
    // request is a claim on a slot and the disk has not been touched here.
    rig.List("start");
    CHECK(rig.out.seen.empty());
    CHECK(rig.file.seen.empty());
    REQUIRE(rig.p.FileIO() != nullptr);
    CHECK(rig.p.FileIO()->PendingCount() == 1);

    rig.Settle();
    REQUIRE(Joined(rig.file.seen) == "!");
    CHECK(rig.p.FileIO()->PendingCount() == 0);
    CHECK(rig.PlayOut() == "i144,i60,!,i100");

    Remove(path);
  }

  TEST_CASE("seq: a read cancels the step a running sequence was waiting on (#692)") {
    // The half of "replace and stop the transport" that only a real scheduler
    // can show: the pending step goes with the tape it belonged to.
    const std::string path = TempFile("yse_seq_cancel_692.txt");
    WriteWholeFile(path, "0 176 7 64\n");
    FileRig rig;

    rig.List("record");
    rig.Int(144);
    rig.List("stop");
    rig.List("delay 500");
    rig.List("start");
    REQUIRE(rig.p.Scheduler()->PendingCount() == 1);

    rig.List("read " + path);
    rig.Settle();
    CHECK(rig.p.Scheduler()->PendingCount() == 0);
    CHECK(rig.PlayOut() == "i176,i7,!,i64");

    Remove(path);
  }

  TEST_CASE("seq: the bare forms reuse the last name given, each half its own (#692)") {
    // Max's bare `read` and `write` open a file dialog, which a headless patcher
    // has no equivalent of, so each half remembers the last name it was given.
    // Max documents no readagain / writeagain for seq, so neither is invented.
    const std::string source = TempFile("yse_seq_bare_source_692.txt");
    const std::string target = TempFile("yse_seq_bare_target_692.mid");
    WriteWholeFile(source, "0 144 62 90\n");
    FileRig rig;

    rig.List("read " + source);
    rig.List("write " + target);
    rig.Settle();
    REQUIRE(ReadWholeFile(target).compare(0, 4, "MThd") == 0);

    rig.List("clear");
    // A bare read goes back to the source, not to the file the write named.
    rig.List("read");
    rig.Settle();
    CHECK(rig.PlayOut() == "i144,i62,!,i90");

    Remove(source);
    Remove(target);
  }

  TEST_CASE("seq: a bare read with no name and no argument does nothing (#692)") {
    FileRig rig;
    rig.List("read");
    rig.List("write");
    REQUIRE(rig.p.FileIO() != nullptr);
    // Refused before the scheduler is reached at all, so nothing is even
    // counted as dropped.
    CHECK(rig.p.FileIO()->PendingCount() == 0);
    CHECK(rig.p.FileIO()->Dropped() == 0);
  }

  TEST_CASE("seq: a read of a missing file leaves the sequence alone (#692)") {
    const std::string path = TempFile("yse_seq_missing_692.mid");
    FileRig rig;

    rig.List("record");
    rig.List("144 60 112");
    rig.List("stop");

    rig.List("read " + path);
    rig.Settle();
    CHECK(rig.file.seen.empty());
    CHECK(rig.PlayOut() == "i144,i60,!,i112");
  }

  TEST_CASE("seq: the filename creation argument is read when the object joins a patcher (#692)") {
    // Max: "specifies the name of a file to be read into seq automatically when
    // the patch is loaded." This is where an object is loaded, and it is the
    // same deferred request a `read` message makes — so the sequence arrives
    // with the patcher's next block rather than during CreateObject.
    const std::string path = TempFile("yse_seq_autoload_692.txt");
    WriteWholeFile(path, "0 192 4\n");
    FileRig rig(path);

    // Claimed at construction, delivered by a block — nothing was opened on the
    // control thread either.
    REQUIRE(rig.p.FileIO() != nullptr);
    CHECK(rig.p.FileIO()->PendingCount() == 1);

    rig.Settle();
    REQUIRE(Joined(rig.file.seen) == "!");
    CHECK(rig.PlayOut() == "i192,!,i4");

    // And it seeded the write half too, so a bare write saves over the same
    // name — as a MIDI file, which is the only thing a write produces.
    rig.List("write");
    rig.Settle();
    CHECK(ReadWholeFile(path).compare(0, 4, "MThd") == 0);

    Remove(path);
  }

  TEST_CASE("seq: deleting the object with a read in flight is safe (#692)") {
    // The job never touches the requesting object, so a DeleteObject racing a
    // read is safe by construction rather than by timing. An ASan build trips
    // here if the retired object is touched.
    const std::string path = TempFile("yse_seq_delete_692.txt");
    WriteWholeFile(path, "0 144 60 100\n");

    patcherImplementation p(1, nullptr);
    YSE::pHandle* handle = p.CreateObject(YSE::OBJ::G_SEQ, "");
    REQUIRE(handle != nullptr);

    handle->SetListData(0, "read " + path);
    REQUIRE(p.FileIO() != nullptr);
    REQUIRE(p.FileIO()->PendingCount() == 1);

    p.DeleteObject(handle);
    p.FileIO()->WaitIdle();
    for (int block = 0; block < 4; block++)
      p.Calculate(YSE::T_DSP);
    CHECK(p.FileIO()->PendingCount() == 0);

    Remove(path);
  }

  TEST_CASE("seq: a sequence loaded from a MIDI file plays out its file timing (#692)") {
    // End to end and on the clock: the gap between the two notes exists only in
    // the file's ticks and the tempo it declares, and it has to come back out of
    // the patcher's block counter as the milliseconds it converted to. 480 ticks
    // at 120 BPM is 500 ms.
    const std::string path = TempFile("yse_seq_clock_692.mid");
    std::string events;
    PutMeta(events, 0, 0x51, TempoBytes(120.0));
    PutEvent(events, 0, {0x90, 60, 100});
    PutEvent(events, 480, {0x80, 60, 0});
    WriteWholeFile(path, MidiHeader(0, 1, 480) + MidiTrack(events));

    FileRig rig;
    rig.List("read " + path);
    rig.Settle();
    rig.out.reset();

    rig.List("start");
    // The whole zero-delta run at the head leaves in the dispatch that started
    // it — the three bytes of a note-on are one message.
    CHECK(Joined(rig.out.seen) == "smeta tempo 120.,i144,i60,i100");
    rig.out.reset();

    const std::uint64_t due = messageScheduler::BlocksForMillis(500);
    for (std::uint64_t block = 1; block < due; ++block)
      rig.p.Calculate(YSE::T_DSP);
    CHECK(rig.out.seen.empty());

    rig.p.Calculate(YSE::T_DSP);
    CHECK(Joined(rig.out.seen) == "i128,i60,i0,!,smeta endoftrack");

    Remove(path);
  }

  // ─── the parse on the audio thread ──────────────────────────────────────────

  TEST_CASE("seq: parsing a read completion allocates nothing (#692, #698)") {
    // The constraint the whole reader is shaped around, asserted rather than
    // argued. `fileScheduler` delivers a completion at the top of
    // `patcherImplementation::Calculate`, which is the audio callback, so
    // `LoadMidiFile` runs there — and that is why it walks fixed tables instead
    // of reusing `MIDI::fileImpl`'s vector-building, vector-sorting parser.
    //
    // It is also what bounds issue #698: the byte primitives the two now share
    // could only be lifted because they are pure and allocation-free. If a
    // future edit to midi/midiBytes.hpp ever puts a container, a std::string or
    // a log line behind one of them, `.seq` would start allocating on the audio
    // thread — and it would fail here rather than as an occasional glitch.
    //
    // The probe sees std::string and array allocations since issue #697, so a
    // zero here means something on Windows as well as on ELF. Under
    // ThreadSanitizer the overrides are compiled out and it holds trivially;
    // support/test_alloc_probe.cpp is where that is measured rather than assumed.
    const std::string midiPath = TempFile("yse_seq_alloc_698.mid");
    const std::string textPath = TempFile("yse_seq_alloc_698.txt");

    // A file with enough in it that any per-event allocation would show:
    // running status, a tempo map, metas and a few hundred channel messages.
    std::string events;
    PutMeta(events, 0, 0x51, TempoBytes(120.0));
    for (int i = 0; i < 200; i++) {
      PutEvent(events, (unsigned int)(i % 7), {0x90 + (i % 4), 60 + (i % 12), 100});
      PutEvent(events, 1, {60 + (i % 12), 0}); // running status: no status byte
      if (i % 32 == 0) PutMeta(events, 0, 0x51, TempoBytes(90.0 + i));
      if (i % 48 == 0) PutEvent(events, 0, {0xC0, 5}); // one data byte, not two
    }
    WriteWholeFile(midiPath, MidiHeader(0, 1, 480) + MidiTrack(events));

    // Max's other format, which goes down the text path instead — same
    // completion, same thread, same requirement.
    std::string text;
    for (int i = 0; i < 200; i++)
      text += std::to_string(i * 10) + " 144 " + std::to_string(60 + (i % 12)) + " 100\n";
    WriteWholeFile(textPath, text);

    Counter fileSink;
    Counter byteSink;
    YSE::pHandle fileHandle(&fileSink);
    YSE::pHandle byteHandle(&byteSink);
    patcherImplementation p(1, nullptr);
    YSE::pHandle* seq = p.CreateObject(YSE::OBJ::G_SEQ, "");
    REQUIRE(seq != nullptr);
    p.Connect(seq, 0, &byteHandle, 0);
    p.Connect(seq, 3, &fileHandle, 0);

    // Everything the request itself costs — building the message, claiming the
    // slot, the background read — happens out here, before the probe. Only the
    // completion is measured.
    seq->SetListData(0, "read " + midiPath);
    REQUIRE(p.FileIO() != nullptr);
    p.FileIO()->WaitIdle();

    {
      TestHelpers::ProbeScope probe;
      p.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    // Not a vacuous zero: the completion really was delivered inside the block
    // above, and it really did put a sequence on the tape. Without this the test
    // would pass just as well on a Calculate that did nothing at all.
    REQUIRE(fileSink.bangs == 1);

    seq->SetListData(0, "read " + textPath);
    p.FileIO()->WaitIdle();
    {
      TestHelpers::ProbeScope probe;
      p.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    REQUIRE(fileSink.bangs == 2);

    // And the sequence that arrived plays out: a parse that allocated nothing
    // because it quietly parsed nothing would be no use. Every gap in the text
    // file is positive, so this is only the run at time 0 — enough to show the
    // tape is not empty.
    byteSink.ints = 0;
    seq->SetListData(0, "start");
    CHECK(byteSink.ints == 3); // "0 144 60 100" — the first line's three bytes

    Remove(midiPath);
    Remove(textPath);
  }

} // TEST_SUITE
