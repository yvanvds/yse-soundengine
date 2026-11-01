// Tests for the ~seq patcher object (patcher/generatorObjects/dSeq), issue
// #1069. The sequencing is DSP::eventSequencer's and is tested in
// Tests/dsp/test_event_sequencer.cpp; these show that the commands and
// events reach it in the order they were sent, that the fired events, dump
// and the state go out only through the flush after the block (#1037), as
// the messages they were stored as, that read and write go through the file
// scheduler, and that it works in a rendered patch after a JSON round trip.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dSeq.h"
#include "patcher/io/fileScheduler.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::ScopedSampleRate;
using TestHelpers::Wire;
using YSE::DSP::buffer;
using YSE::PATCHER::dSeq;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // Every message that arrives, in order, as "i 60", "f 0.5", "b", "l text".
  struct Log : TestHelpers::SinkBase {
    std::vector<std::string> got;
    Log() {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { got.push_back("i " + std::to_string(v)); });
      inputs.back().RegisterFloat(
          [this](float v, int, YSE::THREAD) { got.push_back("f " + std::to_string(v)); });
      inputs.back().RegisterBang([this](int, YSE::THREAD) { got.push_back("b"); });
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { got.push_back("l " + v); });
    }
    const char* Type() const override {
      return "seq_log";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  using List = std::vector<std::string>;

  // ~seq with logs on its three outlets, all into one log so the order
  // between outlets shows. Block() runs one block on `phase` and the flush
  // the patcher runs after its traversal.
  struct Rig {
    Log log;
    dSeq seq;

    Rig() {
      Wire(seq, 0, log);
      Wire(seq, 1, log);
      Wire(seq, 2, log);
    }
    // One block of a ramp from `start` rising `step` per sample, wrapped.
    void Block(float start = 0.f, float step = 1.f / N) {
      buffer phase(N);
      for (unsigned int i = 0; i < N; ++i) {
        const float p = start + static_cast<float>(i) * step;
        phase.getPtr()[i] = p - static_cast<float>(static_cast<int>(p));
      }
      seq.ResetDSP();
      seq.GetInlet(0)->SetBuffer(&phase, YSE::T_DSP); // runs CALC()
      seq.FlushArmedOutlets(YSE::T_GUI);
    }
    void Send(const std::string& message) {
      seq.GetInlet(0)->SetMessage(message, YSE::T_GUI);
    }
    List Take() {
      List out;
      out.swap(log.got);
      return out;
    }
  };

  std::string Join(const List& list) {
    std::string out;
    for (const std::string& item : list) {
      if (!out.empty()) out += '|';
      out += item;
    }
    return out;
  }

  // The pieces of `text` between '|'.
  List Split(const std::string& text) {
    List out(1);
    for (char c : text) {
      if (c == '|') {
        out.emplace_back();
      } else {
        out.back().push_back(c);
      }
    }
    return out;
  }

  std::string TempFile(const char* name) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path.string();
  }

  std::string ReadWholeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }

  void SettleFiles(patcherImplementation& p) {
    YSE::PATCHER::fileScheduler* io = p.FileIO();
    REQUIRE(io != nullptr);
    io->WaitIdle();
    p.Calculate(YSE::T_DSP);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~seq: registry and shape (#1069)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_SEQ);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~seq");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 3);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::ANY);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::LIST);
    CHECK(obj->OutputDataType(2) == YSE::OUT_TYPE::LIST);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_SEQ));
    REQUIRE(o != nullptr);
    CHECK(o->IsDSPObject());
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::SEQUENCE);
  }

  TEST_CASE("~seq: added events go out as the messages they were stored as, in firing order, "
            "only after the block (#1069)") {
    Rig rig;
    rig.Send("add 0 0.5 hello world");
    rig.Send("add 0 0.25 60");
    rig.Send("add 0 0.5 0.5");
    rig.Send("add 0 0.75 bang");
    rig.Send("add 0 0.875 1 2 3");
    rig.Send("add 0 0.9 sym");
    rig.Send("add 1 0.5 other");
    rig.Send("play 1");
    CHECK(rig.seq.Dropped() == 0);
    rig.Block();
    CHECK(rig.Take() == List{"i 60", "l hello world", "f 0.500000", "b", "l 1 2 3", "l sym"});
    CHECK(rig.seq.Sequencer().size() == 7);

    // CALC() alone sends nothing: the events wait for the flush.
    buffer phase(N);
    for (unsigned int i = 0; i < N; ++i)
      phase.getPtr()[i] = static_cast<float>(i) / N;
    rig.seq.ResetDSP();
    rig.seq.GetInlet(0)->SetBuffer(&phase, YSE::T_DSP);
    CHECK(rig.log.got.empty());
    rig.seq.FlushArmedOutlets(YSE::T_GUI);
    CHECK(rig.Take().size() == 6);

    rig.Send("seq 1");
    rig.Block();
    CHECK(rig.Take() == List{"l other"});
    rig.Send("seqnum 0");
    rig.Send("play 0");
    rig.Block();
    CHECK(rig.Take().empty());
  }

  TEST_CASE("~seq: record and overdub store numbers, lists and messages at the current phase; "
            "commands are not recorded (#1069)") {
    Rig rig;
    rig.Send("add 0 0.5 old");
    rig.Block(0.f, 0.25f / N); // ends just under 0.25
    const float at = rig.seq.Sequencer().phase();

    // Not recording: events are ignored.
    rig.seq.GetInlet(0)->SetInt(9, YSE::T_GUI);
    rig.Block(at, 0.f);
    CHECK(rig.seq.Sequencer().size() == 1);

    rig.Send("overdub 1");
    rig.seq.GetInlet(0)->SetInt(60, YSE::T_GUI);
    rig.seq.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    rig.seq.GetInlet(0)->SetList("1  2   3", YSE::T_GUI);
    rig.Send("honk loud");
    rig.seq.GetInlet(0)->SetList("symbol word", YSE::T_GUI);
    rig.Block(at, 0.f);
    const auto& s = rig.seq.Sequencer();
    REQUIRE(s.size() == 6);
    const char* recorded[] = {"60", "0.5", "1 2 3", "honk loud", "word"};
    for (UInt i = 0; i < 5; ++i) {
      CAPTURE(i);
      CHECK(std::string(s.eventText(i)) == recorded[i]);
      CHECK(s.eventPhase(i) == at);
    }
    CHECK(std::string(s.eventText(5)) == "old");

    // record empties the sequence first.
    rig.Send("overdub 0");
    rig.Send("record 1");
    rig.Send("honk");
    rig.Block(at, 0.f);
    REQUIRE(s.size() == 1);
    CHECK(std::string(s.eventText(0)) == "honk");
    CHECK(s.recording());
    rig.Send("record 0");
    rig.Block(at, 0.f);
    CHECK_FALSE(s.recording());
    CHECK(rig.Take().empty());
  }

  TEST_CASE("~seq: delete, clear, erase, offset and dump (#1069)") {
    Rig rig;
    rig.Send("add 0 0.1 a");
    rig.Send("add 0 0.2 b");
    rig.Send("add 0 0.3 a");
    rig.Send("add 0 0.4 c d");
    rig.Send("add 2 0.5 x");
    rig.Send("add 3 0.5 y");
    rig.Send("dump");
    rig.Block();
    CHECK(Join(rig.Take()) == "l 0 0.1 a|l 0 0.2 b|l 0 0.3 a|l 0 0.4 c d|l 2 0.5 x|l 3 0.5 y");

    rig.Send("delete 0 0.2"); // one phase
    rig.Send("delete 0 0 1 a"); // a text anywhere in a range
    rig.Send("delete 0 0.35 0.45"); // a range
    rig.Send("dump 0");
    rig.Block();
    CHECK(rig.Take().empty());
    CHECK(rig.seq.Sequencer().count(0) == 0);

    // offset shifts the sequence numbers messages name.
    rig.Send("offset 2");
    rig.Send("add 1 0.5 shifted"); // into sequence 3
    rig.Send("clear 0"); // sequence 2
    rig.Send("dump 1"); // sequence 3
    rig.Block();
    CHECK(rig.Take() == List{"l 3 0.5 y", "l 3 0.5 shifted"});
    CHECK(rig.seq.Sequencer().count(2) == 0);
    rig.Send("offset 0");

    // clear without a number is the current sequence; erase all, everything.
    rig.Send("add 0 0.5 mine");
    rig.Send("clear");
    rig.Block();
    CHECK(rig.seq.Sequencer().count(0) == 0);
    CHECK(rig.seq.Sequencer().size() == 2);
    rig.Send("erase all");
    rig.Block();
    CHECK(rig.seq.Sequencer().size() == 0);
  }

  TEST_CASE("~seq: a bang reports the state, right outlet before the events (#1069)") {
    Rig rig;
    rig.Send("add 4 0 first");
    rig.Send("add 4 0.5 second");
    rig.Send("add 1 0.5 elsewhere");
    rig.Send("seq 4");
    rig.Send("play");
    rig.Send("overdub 1");
    rig.seq.GetInlet(0)->SetBang(YSE::T_GUI);
    rig.Send("dump 1");
    rig.Block();
    CHECK(rig.Take() == List{"l 4 1 0 1 2 3", "l 1 0.5 elsewhere", "l first", "l second"});
    rig.Send("bang");
    rig.Send("record 1");
    rig.Block(0.f, 0.f);
    CHECK(rig.Take() == List{"l 4 1 1 1 0 1"});
  }

  TEST_CASE("~seq: malformed messages are refused, the queue has a bound (#1069)") {
    Rig rig;
    rig.Send("add");
    rig.Send("add 0");
    rig.Send("add 0 0.5");
    rig.Send("add x 0.5 y");
    rig.Send("delete 0");
    rig.Send("clear x");
    rig.Send("dump x");
    rig.Send("play x");
    rig.Send("seq");
    rig.Send("offset");
    rig.Send("read somewhere"); // no patcher, so no file scheduler
    rig.Send("write");
    rig.seq.GetInlet(0)->SetFloat(NAN, YSE::T_GUI);
    rig.Send("add 0 0.5 " + std::string(YSE::DSP::eventSequencer::TEXT_CAPACITY, 'x'));
    CHECK(rig.seq.Dropped() == 14);
    rig.Block();
    CHECK(rig.seq.Sequencer().size() == 0);
    CHECK(rig.Take().empty());

    unsigned int sent = 0;
    while (rig.seq.Dropped() == 14) {
      rig.Send("add 0 0.5 e");
      sent++;
      REQUIRE(sent < 10000);
    }
    CHECK(sent == 513); // the 512-slot queue, then a refusal
    rig.Block();
    CHECK(rig.seq.Sequencer().size() == 512);
  }

  TEST_CASE("~seq: events past the block's fired capacity are counted (#1069)") {
    Rig rig;
    for (unsigned int i = 0; i < YSE::DSP::eventSequencer::DEFAULT_FIRED + 3; ++i)
      rig.Send("add 0 0.5 " + std::to_string(i));
    rig.Send("play 1");
    rig.Block();
    CHECK(rig.Take().size() == YSE::DSP::eventSequencer::DEFAULT_FIRED);
    CHECK(rig.seq.Missed() == 3);
  }

  TEST_CASE("~seq: write and read go through the file scheduler (#1069)") {
    const std::string path = TempFile("yse_seq_1069.txt");
    Log log;
    YSE::pHandle logHandle(&log);
    patcherImplementation p(1, nullptr);
    YSE::pHandle* seq = p.CreateObject(YSE::OBJ::D_SEQ, "");
    REQUIRE(seq != nullptr);
    p.Connect(seq, 1, &logHandle, 0);

    seq->SetListData(0, "add 0 0.25 60 100");
    seq->SetListData(0, "add 3 0.5 honk");
    seq->SetListData(0, "write " + path);
    REQUIRE(p.FileIO() != nullptr);
    p.Calculate(YSE::T_DSP); // the write is built after this block
    p.FileIO()->WaitIdle();
    CHECK(ReadWholeFile(path) == "0 0.25 60 100\n3 0.5 honk\n");

    // Read replaces every sequence, when the next block takes the result.
    {
      std::ofstream out(path, std::ios::binary);
      out << "1 0.125 from   file\r\n\n2 0.75 7\n";
    }
    seq->SetListData(0, "read " + path);
    SettleFiles(p);
    seq->SetListData(0, "dump");
    p.Calculate(YSE::T_DSP);
    CHECK(log.got == List{"l 1 0.125 from file", "l 2 0.75 7"});
    log.got.clear();

    // A malformed file changes nothing.
    {
      std::ofstream out(path, std::ios::binary);
      out << "1 0.5 fine\nnot a line\n";
    }
    seq->SetListData(0, "read " + path);
    SettleFiles(p);
    seq->SetListData(0, "dump");
    p.Calculate(YSE::T_DSP);
    CHECK(log.got == List{"l 1 0.125 from file", "l 2 0.75 7"});

    p.Disconnect(seq, 1, &logHandle, 0);
    std::error_code ec;
    std::filesystem::remove(path, ec);
  }

  TEST_CASE("~seq: survives a DumpJSON / ParseJSON round trip (#1069)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_SEQ, "");
      REQUIRE(a != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* a = dst.GetHandleFromID(0);
    REQUIRE(a != nullptr);
    CHECK(std::string(a->Type()) == "~seq");
    CHECK(a->GetParams() == "");
  }

  TEST_CASE("~seq: in a rendered patch, a phasor drives events added and recorded from message "
            "boxes into a number box, after a JSON round trip (#1069)") {
    // 1280 Hz and a 10 Hz phasor: one cycle per 128-sample block.
    ScopedSampleRate rate(1280);
    std::string dump;
    unsigned int addID = 0;
    unsigned int playID = 0;
    unsigned int dubID = 0;
    unsigned int noteID = 0;
    unsigned int seqID = 0;
    unsigned int numberID = 0;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* phasor = src.CreateObject(YSE::OBJ::D_PHASOR, "10");
      YSE::pHandle* add = src.CreateObject(YSE::OBJ::G_MESSAGE, "add 0 0.25 60");
      YSE::pHandle* play = src.CreateObject(YSE::OBJ::G_MESSAGE, "play 1");
      YSE::pHandle* dub = src.CreateObject(YSE::OBJ::G_MESSAGE, "overdub 1");
      YSE::pHandle* note = src.CreateObject(YSE::OBJ::G_MESSAGE, "72");
      YSE::pHandle* seq = src.CreateObject(YSE::OBJ::D_SEQ, "");
      YSE::pHandle* number = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      REQUIRE(phasor != nullptr);
      REQUIRE(add != nullptr);
      REQUIRE(play != nullptr);
      REQUIRE(dub != nullptr);
      REQUIRE(note != nullptr);
      REQUIRE(seq != nullptr);
      REQUIRE(number != nullptr);
      src.Connect(phasor, 0, seq, 0);
      src.Connect(add, 0, seq, 0);
      src.Connect(play, 0, seq, 0);
      src.Connect(dub, 0, seq, 0);
      src.Connect(note, 0, seq, 0);
      src.Connect(seq, 0, number, 1);
      addID = add->GetID();
      playID = play->GetID();
      dubID = dub->GetID();
      noteID = note->GetID();
      seqID = seq->GetID();
      numberID = number->GetID();
      dump = src.DumpJSON();
    }
    Log log;
    YSE::pHandle logHandle(&log);
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* add = dst.GetHandleFromID(addID);
    YSE::pHandle* play = dst.GetHandleFromID(playID);
    YSE::pHandle* dub = dst.GetHandleFromID(dubID);
    YSE::pHandle* note = dst.GetHandleFromID(noteID);
    YSE::pHandle* seq = dst.GetHandleFromID(seqID);
    YSE::pHandle* number = dst.GetHandleFromID(numberID);
    REQUIRE(add != nullptr);
    REQUIRE(play != nullptr);
    REQUIRE(dub != nullptr);
    REQUIRE(note != nullptr);
    REQUIRE(seq != nullptr);
    REQUIRE(number != nullptr);
    dst.Connect(seq, 0, &logHandle, 0);

    dst.Calculate(YSE::T_DSP);
    CHECK(log.got.empty());

    add->SetBang(0);
    play->SetBang(0);
    for (int block = 0; block < 3; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      CHECK(log.got == List{"i 60"});
      log.got.clear();
    }
    // The number box stored the event (its right inlet).
    CHECK(std::stof(number->GetGuiValue()) == 60.f);

    // Overdubbing a note from a message box: stored where the phasor is,
    // played from the next time round, with the first event.
    dub->SetBang(0);
    note->SetBang(0);
    dst.Calculate(YSE::T_DSP);
    log.got.clear();
    for (int block = 0; block < 3; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      std::vector<std::string> sorted = log.got;
      std::sort(sorted.begin(), sorted.end());
      CHECK(sorted == List{"i 60", "i 72"});
      log.got.clear();
    }
    dst.Disconnect(seq, 0, &logHandle, 0);
  }

  TEST_CASE("~seq: commands, events, blocks and the flush on the audio thread allocate nothing "
            "(#1069)") {
    // Unwired: the sends still run, with no sink of its own to allocate. The
    // messages are built before the probe.
    dSeq seq;
    const List messages = Split("add 0 0.25 60 100|add 0 0.5 honk|add 1 0.5 1|overdub 1|play 1|"
                                "seq 0|dump|dump 0|bang|delete 0 0.25|delete 0 0 1 honk|offset 1|"
                                "clear 0|offset 0|record 1|honk again|clear|erase all|"
                                "add 0 0.5 bang|add 0 0.5 0.25");
    buffer phase(N);
    for (unsigned int i = 0; i < N; ++i)
      phase.getPtr()[i] = static_cast<float>(i) / N;
    seq.ResetDSP();
    seq.GetInlet(0)->SetBuffer(&phase, YSE::T_DSP);
    seq.FlushArmedOutlets(YSE::T_GUI);
    // One message, then one block and its flush.
    auto run = [&seq, &phase](const std::string& message) {
      seq.GetInlet(0)->SetMessage(message, YSE::T_DSP);
      seq.ResetDSP();
      seq.GetInlet(0)->SetBuffer(&phase, YSE::T_DSP);
      seq.FlushArmedOutlets(YSE::T_GUI);
    };
    {
      TestHelpers::ProbeScope probe;
      seq.GetInlet(0)->SetInt(1, YSE::T_DSP);
      seq.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      seq.GetInlet(0)->SetList("1 2 3", YSE::T_DSP);
      seq.GetInlet(0)->SetBang(YSE::T_DSP);
      for (const std::string& message : messages)
        run(message);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(seq.Dropped() == 0);
  }

} // TEST_SUITE("patcher")
