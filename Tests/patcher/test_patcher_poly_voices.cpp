// Tests for voice instancing, issue #1040: ~poly plays N copies of the
// subpatch inside it; .in / .out / ~in / ~out are their boundary; ~thispoly
// reports a voice's busy and mute state. Everything is shown in rendered
// patches built the way a patch author builds them: objects placed in a
// ~poly with SetContainer, the ~poly's own pins wired at the top level, and
// the ~dac output (or what a ~poly outlet sends) read back.
//
// What instancing must do, each with a control:
//  - every voice is its own copy, the contents themselves never run, and the
//    voices' ~out signals are summed;
//  - messages go to the target voice, `note` / `midinote` to a free voice,
//    and busy (from ~thispoly, or a note this block) drives that choice;
//  - a muted voice does not compute and resumes where it was;
//  - an edit inside rebuilds the voices; nothing crosses the boundary but the
//    ~poly's pins; nesting a ~poly in a ~poly or a ~pfft is refused;
//  - a saved patch loads back to the same voices, each with its loadbang;
//  - rendering and routing allocate nothing, and a control thread can route
//    and edit while the audio thread renders.

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <string>
#include <thread>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/patcherInsert.hpp"
#include "headers/constants.hpp"
#include "patcher/genericObjects/dPoly.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using YSE::STANDARD_BUFFERSIZE;
using YSE::PATCHER::patcherImplementation;

namespace {

  bool AllNear(YSE::DSP::buffer& b, float value) {
    const float* p = b.getPtr();
    for (UInt i = 0; i < b.getLength(); ++i)
      if (std::fabs(p[i] - value) > 1e-6f) return false;
    return true;
  }

  bool Listed(const char* name) {
    for (const auto& n : YSE::PATCHER::Register().AllNames())
      if (n == name) return true;
    return false;
  }

  MULTICHANNELBUFFER MonoBuffer() {
    MULTICHANNELBUFFER io;
    io.resize(1);
    io[0].resize(STANDARD_BUFFERSIZE);
    return io;
  }

  // Creates `type args` inside `container`.
  YSE::pHandle* Inside(patcherImplementation& p, YSE::pHandle* container, const char* type,
                       const std::string& args = "") {
    YSE::pHandle* h = p.CreateObject(type, args);
    REQUIRE(h != nullptr);
    p.SetObjectContainer(h, container);
    return h;
  }

  // Records every message on its inlet, as "<tag>:<kind>:<value>", in one
  // log several sinks share, so the order across outlets shows.
  struct LogSink : TestHelpers::SinkBase {
    LogSink(std::vector<std::string>& to, std::string label) : log(to), tag(std::move(label)) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { log.push_back(tag + ":b"); });
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { log.push_back(tag + ":i:" + std::to_string(v)); });
      inputs.back().RegisterFloat(
          [this](float v, int, YSE::THREAD) { log.push_back(tag + ":f:" + std::to_string(v)); });
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { log.push_back(tag + ":l:" + v); });
    }
    const char* Type() const override {
      return "log_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    std::vector<std::string>& log;
    std::string tag;
  };

  // Wires `sink` to outlet `outlet` of `h`, and publishes so the snapshot
  // the sends read has the cord.
  void Attach(patcherImplementation& p, YSE::pHandle* h, int outlet, LogSink& sink) {
    TestHelpers::Wire(*p.GetObjectFromID(h->GetID()), outlet, sink);
    p.CreateObject(YSE::OBJ::G_MESSAGE, "");
  }

  // A ~poly of `voices` voices with one inlet and one message outlet, holding
  // [.in 0] -> [.out 0] (and [.in 0] -> [~thispoly], busy from each message,
  // when `busy`). Its message outlet logs "m", its voice outlet "v".
  struct messageRig {
    std::vector<std::string> log;
    LogSink voice{log, "v"};
    LogSink message{log, "m"};
    patcherImplementation p{1, nullptr};
    YSE::pHandle* poly = nullptr;

    messageRig(int voices, bool busy) {
      poly = p.CreateObject(YSE::OBJ::D_POLY, "v " + std::to_string(voices) + " 1 0 1");
      REQUIRE(poly != nullptr);
      YSE::pHandle* in = Inside(p, poly, YSE::OBJ::G_VOICE_IN, "0");
      if (busy) {
        YSE::pHandle* state = Inside(p, poly, YSE::OBJ::D_THISPOLY);
        p.Connect(in, 0, state, 0);
      }
      YSE::pHandle* out = Inside(p, poly, YSE::OBJ::G_VOICE_OUT, "0");
      p.Connect(in, 0, out, 0);
      Attach(p, poly, 0, message);
      Attach(p, poly, 1, voice);
    }
    void Send(const std::string& text) {
      poly->SetListData(0, text);
    }
    std::vector<std::string> Take() {
      std::vector<std::string> taken;
      taken.swap(log);
      return taken;
    }
  };

  using strings = std::vector<std::string>;

} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~poly: registry, category and shape (#1040)") {
    for (const char* name : {YSE::OBJ::D_POLY, YSE::OBJ::G_VOICE_IN, YSE::OBJ::G_VOICE_OUT,
                             YSE::OBJ::D_VOICE_IN, YSE::OBJ::D_VOICE_OUT, YSE::OBJ::D_THISPOLY}) {
      CAPTURE(name);
      CHECK(Listed(name));
    }
    CHECK(std::string(YSE::OBJ::D_POLY) == "~poly");
    CHECK(std::string(YSE::OBJ::G_VOICE_IN) == ".in");
    CHECK(std::string(YSE::OBJ::G_VOICE_OUT) == ".out");
    CHECK(std::string(YSE::OBJ::D_VOICE_IN) == "~in");
    CHECK(std::string(YSE::OBJ::D_VOICE_OUT) == "~out");
    CHECK(std::string(YSE::OBJ::D_THISPOLY) == "~thispoly");

    patcherImplementation p(1, nullptr);
    YSE::pHandle* plain = p.CreateObject(YSE::OBJ::D_POLY, "");
    REQUIRE(plain != nullptr);
    CHECK(plain->GetInputs() == 1);
    CHECK(plain->IsDSPInput(0));
    REQUIRE(plain->GetOutputs() == 2);
    CHECK(plain->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(plain->OutputDataType(1) == YSE::OUT_TYPE::INT);

    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::D_POLY, "synth 4 2 2 1");
    REQUIRE(poly != nullptr);
    CHECK(poly->GetInputs() == 2);
    REQUIRE(poly->GetOutputs() == 4);
    CHECK(poly->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(poly->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);
    CHECK(poly->OutputDataType(2) == YSE::OUT_TYPE::ANY);
    CHECK(poly->OutputDataType(3) == YSE::OUT_TYPE::INT);
    CHECK(static_cast<YSE::PATCHER::dPoly*>(p.GetObjectFromID(poly->GetID()))->Voices() == 4);

    YSE::pHandle* in = p.CreateObject(YSE::OBJ::G_VOICE_IN, "0");
    YSE::pHandle* out = p.CreateObject(YSE::OBJ::G_VOICE_OUT, "0");
    YSE::pHandle* sin = p.CreateObject(YSE::OBJ::D_VOICE_IN, "0");
    YSE::pHandle* sout = p.CreateObject(YSE::OBJ::D_VOICE_OUT, "0");
    YSE::pHandle* state = p.CreateObject(YSE::OBJ::D_THISPOLY, "");
    CHECK(in->GetInputs() == 0);
    CHECK(in->GetOutputs() == 1);
    CHECK(out->GetInputs() == 1);
    CHECK(out->GetOutputs() == 0);
    CHECK(sin->GetInputs() == 0);
    CHECK(sin->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(sout->IsDSPInput(0));
    CHECK(sout->GetOutputs() == 0);
    CHECK(state->IsDSPInput(0));
    CHECK(state->GetOutputs() == 3);
    for (YSE::pHandle* h : {poly, in, out, sin, sout, state}) {
      CHECK(p.GetObjectFromID(h->GetID())->GetCategory() == YSE::PATCHER::pCategory::ENCAPSULATION);
    }
  }

  TEST_CASE("~poly: every voice is a copy, the contents never run, ~out sums (#1040)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::D_POLY, "v 4 0 1");
    YSE::pHandle* sig = Inside(p, poly, YSE::OBJ::D_SIG, "0.25");
    YSE::pHandle* out = Inside(p, poly, YSE::OBJ::D_VOICE_OUT, "0");
    p.Connect(sig, 0, out, 0);
    p.Connect(poly, 0, dac, 0);
    p.Calculate(YSE::T_DSP);
    // Four voices, not five: the ~sig placed inside is the definition.
    CHECK(AllNear(p.output[0], 1.f));
    // Only the objects the user made are the patch's.
    CHECK(p.Objects() == 4);

    // An edit inside rebuilds every voice.
    sig->SetParams("0.5");
    p.Calculate(YSE::T_DSP);
    CHECK(AllNear(p.output[0], 2.f));
    YSE::pHandle* more = Inside(p, poly, YSE::OBJ::D_SIG, "0.125");
    p.Connect(more, 0, out, 0);
    p.Calculate(YSE::T_DSP);
    CHECK(AllNear(p.output[0], 2.5f));
    // So does a new voice count (the ~poly is rebuilt, its contents kept).
    poly->SetParams("v 2 0 1");
    p.Calculate(YSE::T_DSP);
    CHECK(AllNear(p.output[0], 1.25f));
    CHECK(p.GetObjectContainer(sig) == poly);
    p.DeleteObject(more);
    p.Calculate(YSE::T_DSP);
    CHECK(AllNear(p.output[0], 1.f));
    // Deleting the ~poly takes its contents and voices with it.
    p.DeleteObject(poly);
    CHECK(p.Objects() == 1);
    p.Calculate(YSE::T_DSP);
    CHECK(AllNear(p.output[0], 0.f));
  }

  TEST_CASE("~poly: ~in hands every voice the inlet's signal (#1040)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* source = p.CreateObject(YSE::OBJ::D_SIG, "0.5");
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::D_POLY, "v 3 2 2");
    YSE::pHandle* in = Inside(p, poly, YSE::OBJ::D_VOICE_IN, "0");
    YSE::pHandle* gain = Inside(p, poly, YSE::OBJ::D_MULTIPLY, "2");
    YSE::pHandle* out = Inside(p, poly, YSE::OBJ::D_VOICE_OUT, "1");
    // An unconnected inlet is silence: ~in 1 adds nothing.
    YSE::pHandle* in1 = Inside(p, poly, YSE::OBJ::D_VOICE_IN, "1");
    p.Connect(in, 0, gain, 0);
    p.Connect(gain, 0, out, 0);
    p.Connect(in1, 0, out, 0);
    p.Connect(source, 0, poly, 0);
    p.Connect(poly, 1, dac, 0);
    p.Calculate(YSE::T_DSP);
    CHECK(AllNear(p.output[0], 3.f));
    // Outlet 0 has no ~out: silence.
    p.Disconnect(poly, 1, dac, 0);
    p.Connect(poly, 0, dac, 0);
    p.Calculate(YSE::T_DSP);
    CHECK(AllNear(p.output[0], 0.f));
  }

  TEST_CASE("~poly: a subpatcher inside a voice is copied whole (#1040)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::D_POLY, "v 2 0 1");
    YSE::pHandle* sub = Inside(p, poly, YSE::OBJ::PATCHER, "inner");
    YSE::pHandle* sig = Inside(p, sub, YSE::OBJ::D_SIG, "0.25");
    YSE::pHandle* outlet = Inside(p, sub, YSE::OBJ::D_OUTLET, "0");
    YSE::pHandle* out = Inside(p, poly, YSE::OBJ::D_VOICE_OUT, "0");
    // A ~dac inside a voice adds nothing to the patch's output.
    YSE::pHandle* innerDac = Inside(p, poly, YSE::OBJ::D_DAC, "");
    p.Connect(sig, 0, outlet, 0);
    p.Connect(sub, 0, out, 0);
    p.Connect(sub, 0, innerDac, 0);
    p.Connect(poly, 0, dac, 0);
    p.Calculate(YSE::T_DSP);
    CHECK(AllNear(p.output[0], 0.5f));
  }

  TEST_CASE("~poly: target sends messages to one voice, all, or none (#1040)") {
    messageRig r(3, false);
    r.p.Calculate(YSE::T_DSP);
    r.poly->SetIntData(0, 7);
    CHECK(r.Take() == strings{"v:i:1", "m:i:7", "v:i:2", "m:i:7", "v:i:3", "m:i:7"});
    r.Send("target 2");
    r.poly->SetFloatData(0, 0.5f);
    r.Send("hello 1 2");
    r.poly->SetBang(0);
    CHECK(r.Take() == strings{"v:i:2", "m:f:0.500000", "v:i:2", "m:l:hello 1 2", "v:i:2", "m:b"});
    // `list` sends a routing word as data.
    r.Send("list target 5");
    CHECK(r.Take() == strings{"v:i:2", "m:l:target 5"});
    // Above the voice count: the highest voice. Negative: none.
    r.Send("target 99");
    r.poly->SetIntData(0, 1);
    CHECK(r.Take() == strings{"v:i:3", "m:i:1"});
    r.Send("target -1");
    r.poly->SetIntData(0, 1);
    CHECK(r.Take().empty());
    // setvalue reaches one voice without moving the target.
    r.Send("setvalue 1 9");
    r.Send("setvalue 0 4.5");
    CHECK(r.Take() == strings{"v:i:1", "m:i:9", "v:i:1", "m:f:4.500000", "v:i:2", "m:f:4.500000",
                              "v:i:3", "m:f:4.500000"});
    auto* poly = static_cast<YSE::PATCHER::dPoly*>(r.p.GetObjectFromID(r.poly->GetID()));
    CHECK(poly->Target() == -1);
  }

  TEST_CASE("~poly: note goes to a voice ~thispoly has not made busy (#1040)") {
    messageRig r(3, true);
    r.p.Calculate(YSE::T_DSP);
    auto* poly = static_cast<YSE::PATCHER::dPoly*>(r.p.GetObjectFromID(r.poly->GetID()));
    r.Send("note 1");
    r.Send("note 1");
    r.Send("note 1");
    CHECK(r.Take() == strings{"v:i:1", "m:i:1", "v:i:2", "m:i:1", "v:i:3", "m:i:1"});
    CHECK(poly->Busy(1));
    CHECK(poly->Busy(3));
    // Every voice busy, and no stealing: dropped.
    r.p.Calculate(YSE::T_DSP);
    r.Send("note 1");
    CHECK(r.Take().empty());
    // Voice 2 says it is free again: it gets the next note.
    r.Send("setvalue 2 0");
    CHECK_FALSE(poly->Busy(2));
    r.Take();
    r.p.Calculate(YSE::T_DSP);
    r.Send("note 5");
    CHECK(r.Take() == strings{"v:i:2", "m:i:5"});
    // Stealing: the voice started longest ago, voice 1.
    r.Send("steal 1");
    r.Send("note 6");
    CHECK(r.Take() == strings{"v:i:1", "m:i:6"});
    r.Send("note 7");
    CHECK(r.Take() == strings{"v:i:3", "m:i:7"});
  }

  TEST_CASE("~poly: a voice given a note is busy until it has run once (#1040)") {
    // No ~thispoly: busy only for the rest of the block, so a chord spreads
    // and the next block starts again at voice 1.
    messageRig r(2, false);
    r.p.Calculate(YSE::T_DSP);
    r.Send("note 7");
    r.Send("note 8");
    r.Send("note 9");
    CHECK(r.Take() == strings{"v:i:1", "m:i:7", "v:i:2", "m:i:8"});
    r.p.Calculate(YSE::T_DSP);
    r.Send("note 9 10");
    CHECK(r.Take() == strings{"v:i:1", "m:l:9 10"});
  }

  TEST_CASE("~poly: midinote holds a pitch, its note-off finds the voice (#1040)") {
    messageRig r(2, false);
    auto* poly = static_cast<YSE::PATCHER::dPoly*>(r.p.GetObjectFromID(r.poly->GetID()));
    r.p.Calculate(YSE::T_DSP);
    r.Send("midinote 60 100");
    r.Send("midinote 64 90");
    r.Send("midinote 67 80"); // no free voice
    CHECK(r.Take() == strings{"v:i:1", "m:l:60 100", "v:i:2", "m:l:64 90"});
    CHECK(poly->PitchOfVoice(1) == 60);
    CHECK(poly->PitchOfVoice(2) == 64);
    r.p.Calculate(YSE::T_DSP);
    r.Send("midinote 60 0");
    r.Send("midinote 67 0"); // held by nobody
    CHECK(r.Take() == strings{"v:i:1", "m:l:60 0"});
    CHECK(poly->PitchOfVoice(1) == -1);
    r.p.Calculate(YSE::T_DSP);
    r.Send("midinote 67 70");
    CHECK(r.Take() == strings{"v:i:1", "m:l:67 70"});
    r.Send("allnotesoff");
    CHECK(r.Take() == strings{"v:i:1", "m:l:67 0", "v:i:2", "m:l:64 0"});
    CHECK(poly->PitchOfVoice(2) == -1);
    CHECK(poly->Dropped() == 0);
  }

  TEST_CASE("~poly: a muted voice does not compute and resumes where it was (#1040)") {
    // Two voices of [~phasor 100]; a reference ramp rendered without a stop.
    std::vector<std::string> log;
    LogSink voiceSink(log, "v");
    LogSink messageSink(log, "m");
    patcherImplementation p(1, nullptr);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::D_POLY, "v 2 1 1 1");
    YSE::pHandle* ramp = Inside(p, poly, YSE::OBJ::D_PHASOR, "100");
    YSE::pHandle* out = Inside(p, poly, YSE::OBJ::D_VOICE_OUT, "0");
    YSE::pHandle* in = Inside(p, poly, YSE::OBJ::G_VOICE_IN, "0");
    YSE::pHandle* state = Inside(p, poly, YSE::OBJ::D_THISPOLY);
    YSE::pHandle* report = Inside(p, poly, YSE::OBJ::G_VOICE_OUT, "0");
    p.Connect(ramp, 0, out, 0);
    p.Connect(in, 0, state, 0);
    p.Connect(state, 1, report, 0);
    p.Connect(poly, 0, dac, 0);
    Attach(p, poly, 1, messageSink);
    Attach(p, poly, 2, voiceSink);

    patcherImplementation ref(1, nullptr);
    ref.Connect(ref.CreateObject(YSE::OBJ::D_PHASOR, "100"), 0,
                ref.CreateObject(YSE::OBJ::D_DAC, ""), 0);
    std::vector<std::vector<float>> blocks;
    for (int b = 0; b < 8; ++b) {
      ref.Calculate(YSE::T_DSP);
      blocks.emplace_back(ref.output[0].getPtr(), ref.output[0].getPtr() + STANDARD_BUFFERSIZE);
    }
    const auto expect = [&](const std::vector<float>& a, const std::vector<float>* b) {
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
        CAPTURE(i);
        const float want = a[i] + (b != nullptr ? (*b)[i] : 0.f);
        CHECK(p.output[0].getPtr()[i] == doctest::Approx(want).epsilon(1e-6));
      }
    };

    p.Calculate(YSE::T_DSP); // both voices: block 0 twice
    expect(blocks[0], &blocks[0]);
    poly->SetListData(0, "mute 1 1");
    CHECK(log == strings{"v:i:1", "m:i:1"});
    log.clear();
    for (int b = 1; b <= 3; ++b) {
      p.Calculate(YSE::T_DSP); // voice 2 alone
      expect(blocks[static_cast<std::size_t>(b)], nullptr);
    }
    poly->SetListData(0, "mute 0 0");
    CHECK(log == strings{"v:i:1", "m:i:0", "v:i:2", "m:i:0"});
    log.clear();
    p.Calculate(YSE::T_DSP); // voice 1 resumes at block 1, voice 2 is at block 4
    expect(blocks[4], &blocks[1]);

    // A voice can mute itself through its ~thispoly.
    poly->SetListData(0, "setvalue 2 mute 1");
    CHECK(log == strings{"v:i:2", "m:i:1"});
    p.Calculate(YSE::T_DSP); // voice 1 at block 2 alone
    expect(blocks[2], nullptr);
    // The mute survives a rebuild of the voices (an object added inside).
    Inside(p, poly, YSE::OBJ::D_SIG, "0");
    p.Calculate(YSE::T_DSP);
    expect(blocks[0], nullptr);
  }

  TEST_CASE("~thispoly: a bang reports the voice count and the voice number (#1040)") {
    std::vector<std::string> log;
    LogSink voiceSink(log, "v");
    LogSink m0(log, "m0");
    LogSink m1(log, "m1");
    patcherImplementation p(1, nullptr);
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::D_POLY, "v 3 1 0 2");
    YSE::pHandle* in = Inside(p, poly, YSE::OBJ::G_VOICE_IN, "0");
    YSE::pHandle* state = Inside(p, poly, YSE::OBJ::D_THISPOLY);
    YSE::pHandle* number = Inside(p, poly, YSE::OBJ::G_VOICE_OUT, "0");
    YSE::pHandle* count = Inside(p, poly, YSE::OBJ::G_VOICE_OUT, "1");
    p.Connect(in, 0, state, 0);
    p.Connect(state, 0, number, 0);
    p.Connect(state, 2, count, 0);
    Attach(p, poly, 0, m0);
    Attach(p, poly, 1, m1);
    Attach(p, poly, 2, voiceSink);
    poly->SetListData(0, "target 2");
    poly->SetBang(0);
    CHECK(log == strings{"v:i:2", "m1:i:3", "v:i:2", "m0:i:2"});

    // Outside a ~poly: voice 0 of 1.
    std::vector<std::string> alone;
    LogSink first(alone, "0");
    LogSink last(alone, "2");
    patcherImplementation q(1, nullptr);
    YSE::pHandle* lone = q.CreateObject(YSE::OBJ::D_THISPOLY, "");
    Attach(q, lone, 0, first);
    Attach(q, lone, 2, last);
    lone->SetBang(0);
    CHECK(alone == strings{"2:i:1", "0:i:0"});
  }

  TEST_CASE("~poly: cords cannot cross its boundary, a ~poly cannot nest (#1040)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::D_POLY, "v 2 1 1 1");
    YSE::pHandle* sig = Inside(p, poly, YSE::OBJ::D_SIG, "0.5");
    YSE::pHandle* out = Inside(p, poly, YSE::OBJ::G_VOICE_OUT, "0");
    YSE::pHandle* outside = p.CreateObject(YSE::OBJ::D_SIG, "0.25");
    YSE::pHandle* box = p.CreateObject(YSE::OBJ::G_MESSAGE, "1");
    p.Connect(outside, 0, poly, 0); // the ~poly's own pin: fine
    CHECK(outside->GetConnections(0) == 1);
    p.Connect(box, 0, out, 0); // into the contents: refused
    p.Connect(sig, 0, outside, 0); // out of the contents: refused
    p.Connect(sig, 0, poly, 0); // a content to its own ~poly: refused
    CHECK(box->GetConnections(0) == 0);
    CHECK(sig->GetConnections(0) == 0);

    YSE::pHandle* inner = p.CreateObject(YSE::OBJ::D_POLY, "");
    p.SetObjectContainer(inner, poly);
    CHECK(p.GetObjectContainer(inner) == nullptr);
    // Nor through a subpatcher, nor into a ~pfft.
    YSE::pHandle* group = p.CreateObject(YSE::OBJ::PATCHER, "");
    p.SetObjectContainer(inner, group);
    CHECK(p.GetObjectContainer(inner) == group);
    p.SetObjectContainer(group, poly);
    CHECK(p.GetObjectContainer(group) == nullptr);
    YSE::pHandle* pfft = p.CreateObject(YSE::OBJ::D_PFFT, "");
    p.SetObjectContainer(group, pfft);
    CHECK(p.GetObjectContainer(group) == nullptr);
    // A plain subpatcher goes in.
    YSE::pHandle* plain = p.CreateObject(YSE::OBJ::PATCHER, "");
    p.SetObjectContainer(plain, poly);
    CHECK(p.GetObjectContainer(plain) == poly);
  }

  TEST_CASE("~poly: a ~pfft inside a voice runs in every voice (#1040)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* source = p.CreateObject(YSE::OBJ::D_SIG, "0.5");
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::D_POLY, "v 2 1 1");
    YSE::pHandle* in = Inside(p, poly, YSE::OBJ::D_VOICE_IN, "0");
    YSE::pHandle* fx = Inside(p, poly, YSE::OBJ::D_PFFT, "fx 64 4");
    YSE::pHandle* fftin = Inside(p, fx, YSE::OBJ::D_FFTIN, "0");
    YSE::pHandle* fftout = Inside(p, fx, YSE::OBJ::D_FFTOUT, "0");
    YSE::pHandle* out = Inside(p, poly, YSE::OBJ::D_VOICE_OUT, "0");
    p.Connect(fftin, 0, fftout, 0);
    p.Connect(fftin, 1, fftout, 1);
    p.Connect(in, 0, fx, 0);
    p.Connect(fx, 0, out, 0);
    p.Connect(source, 0, poly, 0);
    p.Connect(poly, 0, dac, 0);
    // Past the ~pfft's latency (64 samples), each voice passes 0.5 back
    // (the constant's spectrum, unchanged): 1 in all.
    for (int b = 0; b < 4; ++b)
      p.Calculate(YSE::T_DSP);
    const float* o = p.output[0].getPtr();
    for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
      CAPTURE(i);
      CHECK(o[i] == doctest::Approx(1.0).epsilon(1e-4));
    }
  }

  TEST_CASE("~poly: saved and loaded, each voice takes its loadbang (#1040)") {
    YSE::patcher src;
    src.create(1);
    YSE::pHandle* poly = src.CreateObject(YSE::OBJ::D_POLY, "voices 3 0 1");
    YSE::pHandle* load = src.CreateObject(YSE::OBJ::G_LOADBANG, "");
    YSE::pHandle* box = src.CreateObject(YSE::OBJ::G_MESSAGE, "0.25");
    YSE::pHandle* sig = src.CreateObject(YSE::OBJ::D_SIG, "0");
    YSE::pHandle* out = src.CreateObject(YSE::OBJ::D_VOICE_OUT, "0");
    YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
    for (YSE::pHandle* h : {load, box, sig, out})
      src.SetContainer(h, poly);
    src.Connect(load, 0, box, 0);
    src.Connect(box, 0, sig, 0);
    src.Connect(sig, 0, out, 0);
    src.Connect(poly, 0, dac, 0);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(1);
    loaded.ParseJSON(json);
    CHECK(loaded.Objects() == 6);
    CHECK(loaded.DumpJSON() == json);
    YSE::DSP::patcherInsert insert(loaded);
    MULTICHANNELBUFFER io = MonoBuffer();
    io[0] = 0.f;
    insert.process(io);
    CHECK(AllNear(io[0], 0.75f));

    // Built live, nothing fired a loadbang: silence.
    YSE::DSP::patcherInsert live(src);
    io[0] = 0.f;
    live.process(io);
    CHECK(AllNear(io[0], 0.f));
  }

  TEST_CASE("~poly: rendering and routing allocate nothing on the audio thread (#1040)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::D_POLY, "v 4 1 1 1");
    YSE::pHandle* in = Inside(p, poly, YSE::OBJ::G_VOICE_IN, "0");
    YSE::pHandle* state = Inside(p, poly, YSE::OBJ::D_THISPOLY);
    YSE::pHandle* report = Inside(p, poly, YSE::OBJ::G_VOICE_OUT, "0");
    YSE::pHandle* sig = Inside(p, poly, YSE::OBJ::D_SIG, "0");
    YSE::pHandle* ramp = Inside(p, poly, YSE::OBJ::D_PHASOR, "100");
    YSE::pHandle* mix = Inside(p, poly, YSE::OBJ::D_MULTIPLY, "");
    YSE::pHandle* sin = Inside(p, poly, YSE::OBJ::D_VOICE_IN, "0");
    YSE::pHandle* out = Inside(p, poly, YSE::OBJ::D_VOICE_OUT, "0");
    p.Connect(in, 0, state, 0);
    p.Connect(in, 0, report, 0);
    p.Connect(in, 0, sig, 0);
    p.Connect(state, 1, report, 0);
    p.Connect(ramp, 0, mix, 0);
    p.Connect(sig, 0, mix, 1);
    p.Connect(mix, 0, out, 0);
    p.Connect(sin, 0, out, 0);
    p.Connect(poly, 0, dac, 0);
    YSE::PATCHER::pObject* polyObj = p.GetObjectFromID(poly->GetID());
    p.Calculate(YSE::T_DSP);

    const std::vector<std::string> messages = {"note 1",
                                               "note 0.5",
                                               "midinote 60 100",
                                               "midinote 62 90",
                                               "midinote 60 0",
                                               "target 2",
                                               "0.25",
                                               "1 2 3",
                                               "word 4",
                                               "mute 2 1",
                                               "mute 0 0",
                                               "setvalue 3 0.125",
                                               "list note 3",
                                               "allnotesoff",
                                               "steal 1",
                                               "target 0",
                                               "bang",
                                               "setvalue 1 mute 1",
                                               "setvalue 1 mute 0"};
    {
      TestHelpers::ProbeScope probe;
      for (const std::string& m : messages) {
        polyObj->GetInlet(0)->SetMessage(m, YSE::T_DSP);
        p.Calculate(YSE::T_DSP);
        polyObj->GetInlet(0)->SetList(m, YSE::T_DSP);
        p.Calculate(YSE::T_DSP);
      }
      polyObj->GetInlet(0)->SetInt(1, YSE::T_DSP);
      polyObj->GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      polyObj->GetInlet(0)->SetBang(YSE::T_DSP);
      p.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK_FALSE(AllNear(p.output[0], 0.f));
  }

  TEST_CASE("~poly: a control thread routes and edits while the audio thread renders (#1040)") {
    // Under TSan, a busy flag, a mute word, the target or the voice copies
    // read and written without synchronisation show up here.
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::D_POLY, "v 4 1 1 1");
    YSE::pHandle* in = p.CreateObject(YSE::OBJ::G_VOICE_IN, "0");
    YSE::pHandle* state = p.CreateObject(YSE::OBJ::D_THISPOLY, "");
    YSE::pHandle* sig = p.CreateObject(YSE::OBJ::D_SIG, "0");
    YSE::pHandle* out = p.CreateObject(YSE::OBJ::D_VOICE_OUT, "0");
    YSE::pHandle* report = p.CreateObject(YSE::OBJ::G_VOICE_OUT, "0");
    for (YSE::pHandle* h : {in, state, sig, out, report})
      p.SetContainer(h, poly);
    p.Connect(in, 0, state, 0);
    p.Connect(in, 0, sig, 0);
    p.Connect(sig, 0, out, 0);
    p.Connect(sig, 0, state, 0); // busy from the signal too
    p.Connect(state, 1, report, 0);
    p.Connect(poly, 0, dac, 0);
    YSE::DSP::patcherInsert insert(p);

    std::atomic<bool> done{false};
    std::atomic<bool> finite{true};
    std::atomic<int> blocks{0};
    std::atomic<int> routed{0};
    std::thread render([&] {
      MULTICHANNELBUFFER io = MonoBuffer();
      // At least one block before and after the control thread's work,
      // whatever the scheduling.
      while (!done.load() || blocks.load() == 0) {
        io[0] = 0.f;
        insert.process(io);
        if (!std::isfinite(io[0].getPtr()[0])) finite.store(false);
        blocks.fetch_add(1);
      }
    });
    while (blocks.load() == 0)
      std::this_thread::yield();
    for (int i = 0; i < 400; ++i) {
      switch (i % 8) {
      case 0:
        poly->SetListData(0, "note 0.25");
        break;
      case 1:
        poly->SetListData(0, "midinote " + std::to_string(60 + i % 12) + " 100");
        break;
      case 2:
        poly->SetListData(0, "target " + std::to_string(i % 5));
        break;
      case 3:
        poly->SetFloatData(0, 0.f);
        break;
      case 4:
        poly->SetListData(0, "mute " + std::to_string(i % 5) + " " + std::to_string(i % 2));
        break;
      case 5:
        poly->SetListData(0, "allnotesoff");
        break;
      case 6:
        poly->SetListData(0, "setvalue 2 mute 0");
        break;
      default:
        // An edit inside: every voice is rebuilt under the render.
        if ((i % 40) == 7) sig->SetParams((i % 80) == 7 ? "0.5" : "0");
        break;
      }
      routed.fetch_add(1);
    }
    const int before = blocks.load();
    while (blocks.load() == before)
      std::this_thread::yield();
    done.store(true);
    render.join();
    CHECK(routed.load() == 400);
    CHECK(finite.load());
    CHECK(blocks.load() > before);
  }

} // TEST_SUITE("patcher")
