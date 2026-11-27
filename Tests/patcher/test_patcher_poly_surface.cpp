// Tests for the rest of ~poly's surface, issue #1294: the voice count at run
// time (`voices`), `busymap` / `mutemap`, `exclude`, `bypass`, and
// ~thispoly's `automute`. As in test_patcher_poly_voices.cpp, everything is
// shown in rendered patches built the way a patch author builds them: objects
// placed in a ~poly with SetContainer, its pins wired at the top level, and
// the ~dac output (or what a ~poly outlet sends) read back.
//
// What each must do, with a control:
//  - `voices N` adds or removes voices at the end; the voices that stay keep
//    their running state, mute and notes; it is not saved;
//  - the maps list every voice's busy / mute flag out of a message outlet;
//  - an excluded voice gets no note and is never stolen;
//  - a bypassed voice adds its inlet's signal instead of its own;
//  - an automuted voice is muted while not busy, and a note unmutes it;
//  - none of it allocates on the audio thread, and a control thread and the
//    audio thread can use all of it while the audio thread renders.

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <string>
#include <thread>
#include <vector>
#include "dsp/buffer.hpp"
#include "headers/constants.hpp"
#include "patcher/genericObjects/dPoly.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
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

  YSE::pHandle* Inside(patcherImplementation& p, YSE::pHandle* container, const char* type,
                       const std::string& args = "") {
    YSE::pHandle* h = p.CreateObject(type, args);
    REQUIRE(h != nullptr);
    p.SetObjectContainer(h, container);
    return h;
  }

  YSE::PATCHER::dPoly* PolyOf(patcherImplementation& p, YSE::pHandle* h) {
    return static_cast<YSE::PATCHER::dPoly*>(p.GetObjectFromID(h->GetID()));
  }

  // Records every message on its inlet, as "<tag>:<kind>:<value>".
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

  void Attach(patcherImplementation& p, YSE::pHandle* h, int outlet, LogSink& sink) {
    TestHelpers::Wire(*p.GetObjectFromID(h->GetID()), outlet, sink);
    p.CreateObject(YSE::OBJ::G_MESSAGE, "");
  }

  // A ~poly of `voices` voices, one inlet, no signal outlet, one message
  // outlet, holding [.in 0] -> [.out 0], and [.in 0] -> [~thispoly] (busy from
  // each message) when `busy`. Its message outlet logs "m", its voice outlet
  // "v".
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
    YSE::PATCHER::dPoly* Poly() {
      return PolyOf(p, poly);
    }
  };

  using strings = std::vector<std::string>;

  // A reference [~phasor 100] -> [~dac], rendered `count` blocks.
  std::vector<std::vector<float>> PhasorBlocks(int count) {
    patcherImplementation ref(1, nullptr);
    ref.Connect(ref.CreateObject(YSE::OBJ::D_PHASOR, "100"), 0,
                ref.CreateObject(YSE::OBJ::D_DAC, ""), 0);
    std::vector<std::vector<float>> blocks;
    for (int b = 0; b < count; ++b) {
      ref.Calculate(YSE::T_DSP);
      blocks.emplace_back(ref.output[0].getPtr(), ref.output[0].getPtr() + STANDARD_BUFFERSIZE);
    }
    return blocks;
  }

  // Whether `out` is the sum of the reference blocks `which`.
  bool SumOf(YSE::DSP::buffer& out, const std::vector<std::vector<float>>& blocks,
             const std::vector<int>& which) {
    for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
      float want = 0.f;
      for (int b : which)
        want += blocks[static_cast<std::size_t>(b)][i];
      if (std::fabs(out.getPtr()[i] - want) > 1e-5f) return false;
    }
    return true;
  }

} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~poly: voices changes the count, the voices that stay keep playing (#1294)") {
    // Voices of [~phasor 100]: each voice's ramp shows how many blocks it has
    // run, so a voice rebuilt by the change would start again at block 0.
    const auto blocks = PhasorBlocks(8);
    patcherImplementation p(1, nullptr);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::D_POLY, "v 2 1 1");
    YSE::pHandle* ramp = Inside(p, poly, YSE::OBJ::D_PHASOR, "100");
    YSE::pHandle* out = Inside(p, poly, YSE::OBJ::D_VOICE_OUT, "0");
    p.Connect(ramp, 0, out, 0);
    p.Connect(poly, 0, dac, 0);
    YSE::PATCHER::dPoly* host = PolyOf(p, poly);
    const std::string saved = p.DumpJSON();

    p.Calculate(YSE::T_DSP);
    p.Calculate(YSE::T_DSP);
    CHECK(SumOf(p.output[0], blocks, {1, 1}));

    // Grown: voices 1 and 2 go on at block 2, voice 3 starts at block 0.
    poly->SetListData(0, "voices 3");
    p.SettleSignalBus();
    CHECK(host->Voices() == 3);
    CHECK(host->WantedVoices() == 3);
    p.Calculate(YSE::T_DSP);
    CHECK(SumOf(p.output[0], blocks, {2, 2, 0}));

    // Shrunk: voice 1 goes on at block 3 alone.
    poly->SetListData(0, "voices 1");
    p.SettleSignalBus();
    CHECK(host->Voices() == 1);
    p.Calculate(YSE::T_DSP);
    CHECK(SumOf(p.output[0], blocks, {3}));

    // Below 1 is 1; above 128 is 128.
    poly->SetListData(0, "voices 0");
    p.SettleSignalBus();
    CHECK(host->Voices() == 1);
    p.Calculate(YSE::T_DSP);
    CHECK(SumOf(p.output[0], blocks, {4}));

    // The count is run-time state: the patch saves the argument.
    CHECK(p.DumpJSON() == saved);

    // An edit inside rebuilds the voices at the count asked for.
    poly->SetListData(0, "voices 2");
    p.SettleSignalBus();
    Inside(p, poly, YSE::OBJ::D_SIG, "0");
    CHECK(host->Voices() == 2);
    p.Calculate(YSE::T_DSP);
    CHECK(SumOf(p.output[0], blocks, {0, 0}));

    // From the audio thread too: the voices are made on the control side.
    host->GetInlet(0)->SetList("voices 4", YSE::T_DSP);
    p.SettleSignalBus();
    CHECK(host->Voices() == 4);
    p.Calculate(YSE::T_DSP);
    CHECK(SumOf(p.output[0], blocks, {1, 1, 0, 0}));

    poly->SetListData(0, "voices 500");
    p.SettleSignalBus();
    CHECK(host->Voices() == YSE::PATCHER::dPoly::MAX_VOICES);
  }

  TEST_CASE("~poly: voices keeps the state of the voices that stay (#1294)") {
    messageRig r(3, true);
    YSE::PATCHER::dPoly* host = r.Poly();
    r.p.Calculate(YSE::T_DSP);

    // Voice 1 and 3 hold notes, voice 2 is muted, voice 1 excluded.
    r.Send("midinote 60 100");
    r.Send("midinote 62 100");
    r.Send("midinote 64 100");
    r.Send("mute 2 1");
    r.Send("setvalue 2 0"); // voice 2 free again
    r.Send("exclude 1 1");
    r.Take();
    r.p.Calculate(YSE::T_DSP);
    CHECK(host->PitchOfVoice(2) == 62);

    // Grown to 5: what the three voices had is kept; 4 and 5 start new.
    r.Send("voices 5");
    r.p.SettleSignalBus();
    REQUIRE(host->Voices() == 5);
    r.Send("mutemap");
    CHECK(r.Take() == strings{"m:l:0 1 0 0 0"});
    r.Send("busymap");
    CHECK(r.Take() == strings{"m:l:1 0 1 0 0"});
    CHECK(host->Excluded(1));
    CHECK_FALSE(host->Excluded(4));
    CHECK(host->PitchOfVoice(1) == 60);
    CHECK(host->PitchOfVoice(3) == 64);
    // A note-off still finds its voice; the next note goes to voice 4 (1 is
    // excluded, 2 holds 62, 3 is busy).
    r.Send("midinote 64 0");
    r.Send("midinote 70 100");
    CHECK(r.Take() == strings{"v:i:3", "m:l:64 0", "v:i:4", "m:l:70 100"});

    // Shrunk to 2: voice 3 and 4 and their notes are gone.
    r.Send("voices 2");
    r.p.SettleSignalBus();
    REQUIRE(host->Voices() == 2);
    r.Send("midinote 70 0"); // was voice 4's: nobody holds it now
    CHECK(r.Take().empty());
    CHECK(host->PitchOfVoice(4) == -1);
    r.Send("mutemap");
    CHECK(r.Take() == strings{"m:l:0 1"});

    // Grown back: voice 3 and 4 are new voices, not the old ones.
    r.Send("voices 4");
    r.p.SettleSignalBus();
    r.Send("busymap");
    CHECK(r.Take() == strings{"m:l:1 0 0 0"});
    r.p.Calculate(YSE::T_DSP);
    r.Send("note 1");
    // Voice 2 holds 62 (held, though not busy), so voice 3.
    CHECK(r.Take() == strings{"v:i:3", "m:i:1"});

    // A target above the count becomes the count.
    r.Send("target 4");
    r.Send("voices 2");
    r.p.SettleSignalBus();
    CHECK(host->Target() == 2);
  }

  TEST_CASE("~poly: busymap and mutemap list every voice (#1294)") {
    messageRig r(3, true);
    YSE::PATCHER::dPoly* host = r.Poly();
    r.p.Calculate(YSE::T_DSP);
    r.Send("busymap");
    r.Send("mutemap 0");
    CHECK(r.Take() == strings{"m:l:0 0 0", "m:l:0 0 0"});

    r.Send("note 1"); // voice 1, made busy by its ~thispoly
    r.Send("mute 3 1");
    r.Take();
    r.Send("busymap");
    r.Send("mutemap");
    CHECK(r.Take() == strings{"m:l:1 0 0", "m:l:0 0 1"});
    // A voice given a note is busy until it has run once, ~thispoly or not.
    r.Send("setvalue 1 0");
    r.Take();
    r.p.Calculate(YSE::T_DSP);
    r.Send("note list hello");
    r.Take();
    r.Send("busymap");
    CHECK(r.Take() == strings{"m:l:1 0 0"});
    r.p.Calculate(YSE::T_DSP);
    r.Send("busymap");
    CHECK(r.Take() == strings{"m:l:0 0 0"});
    // An outlet the ~poly does not have: nothing.
    r.Send("busymap 1");
    r.Send("mutemap -1");
    CHECK(r.Take().empty());
    CHECK(host->Dropped() == 0);
  }

  TEST_CASE("~poly: an excluded voice gets no note and is not stolen (#1294)") {
    messageRig r(3, false);
    YSE::PATCHER::dPoly* host = r.Poly();
    r.p.Calculate(YSE::T_DSP);
    r.Send("exclude 1 1");
    r.Send("note 1");
    r.Send("note 2");
    r.Send("note 3"); // 2 and 3 are busy this block, 1 excluded
    CHECK(r.Take() == strings{"v:i:2", "m:i:1", "v:i:3", "m:i:2"});
    r.p.Calculate(YSE::T_DSP);
    // midinote and midievent skip it too; with stealing, the oldest voice
    // that is not excluded.
    r.Send("midinote 60 100");
    r.Send("midinote 62 100");
    r.Send("steal 1");
    r.Send("midinote 64 100");
    CHECK(r.Take() == strings{"v:i:2", "m:l:60 100", "v:i:3", "m:l:62 100", "v:i:2", "m:l:64 100"});
    // Its other messages still reach it.
    r.Send("setvalue 1 5");
    CHECK(r.Take() == strings{"v:i:1", "m:i:5"});
    // Every voice excluded: nothing, stealing or not.
    r.Send("exclude 0 1");
    r.p.Calculate(YSE::T_DSP);
    r.Send("note 9");
    CHECK(r.Take().empty());
    CHECK(host->Excluded(3));
    // Back in; the exclusion survives a rebuild of the voices.
    r.Send("exclude 0 0");
    r.Send("exclude 3");
    Inside(r.p, r.poly, YSE::OBJ::D_SIG, "0");
    r.Send("allnotesoff");
    r.Send("steal 0");
    r.Take();
    r.p.Calculate(YSE::T_DSP);
    r.Send("note 1");
    r.Send("note 2");
    r.Send("note 3");
    CHECK(r.Take() == strings{"v:i:1", "m:i:1", "v:i:2", "m:i:2"});
    // Out of range: ignored.
    r.Send("exclude 4 1");
    r.Send("exclude -1 1");
    CHECK_FALSE(host->Excluded(4));
    CHECK_FALSE(host->Excluded(1));
  }

  TEST_CASE("~poly: a bypassed voice passes its inlet through (#1294)") {
    // Two voices of [~in 0] -> [~* 2] -> [~out 0], fed 0.5: 2 in all.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* source = p.CreateObject(YSE::OBJ::D_SIG, "0.5");
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::D_POLY, "v 2 1 1");
    YSE::pHandle* in = Inside(p, poly, YSE::OBJ::D_VOICE_IN, "0");
    YSE::pHandle* gain = Inside(p, poly, YSE::OBJ::D_MULTIPLY, "2");
    YSE::pHandle* out = Inside(p, poly, YSE::OBJ::D_VOICE_OUT, "0");
    p.Connect(in, 0, gain, 0);
    p.Connect(gain, 0, out, 0);
    p.Connect(source, 0, poly, 0);
    p.Connect(poly, 0, dac, 0);
    YSE::PATCHER::dPoly* host = PolyOf(p, poly);
    p.Calculate(YSE::T_DSP);
    CHECK(AllNear(p.output[0], 2.f));

    // Voice 1 bypassed: its 0.5 unprocessed, plus voice 2's 1.
    poly->SetListData(0, "bypass 1 1");
    CHECK(host->Bypassed(1));
    p.Calculate(YSE::T_DSP);
    CHECK(AllNear(p.output[0], 1.5f));
    // Every voice: the input twice.
    poly->SetListData(0, "bypass 0 1");
    p.Calculate(YSE::T_DSP);
    CHECK(AllNear(p.output[0], 1.f));
    // A muted voice adds nothing, bypassed or not.
    poly->SetListData(0, "mute 2 1");
    p.Calculate(YSE::T_DSP);
    CHECK(AllNear(p.output[0], 0.5f));
    poly->SetListData(0, "mute 0 0");
    poly->SetListData(0, "bypass 0 0");
    p.Calculate(YSE::T_DSP);
    CHECK(AllNear(p.output[0], 2.f));

    // A bypassed voice does not run: a [~phasor] in it resumes where it
    // stopped, as a muted one does.
    const auto blocks = PhasorBlocks(6);
    patcherImplementation q(1, nullptr);
    YSE::pHandle* qdac = q.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* qpoly = q.CreateObject(YSE::OBJ::D_POLY, "v 1 1 1");
    YSE::pHandle* ramp = Inside(q, qpoly, YSE::OBJ::D_PHASOR, "100");
    YSE::pHandle* qout = Inside(q, qpoly, YSE::OBJ::D_VOICE_OUT, "0");
    q.Connect(ramp, 0, qout, 0);
    q.Connect(qpoly, 0, qdac, 0);
    q.Calculate(YSE::T_DSP);
    CHECK(SumOf(q.output[0], blocks, {0}));
    qpoly->SetListData(0, "bypass 1 1");
    q.Calculate(YSE::T_DSP);
    CHECK(AllNear(q.output[0], 0.f)); // its inlet is unconnected: silence
    qpoly->SetListData(0, "bypass 1 0");
    q.Calculate(YSE::T_DSP);
    CHECK(SumOf(q.output[0], blocks, {1}));
  }

  TEST_CASE("~thispoly: automute mutes a voice while it is not busy (#1294)") {
    // Two voices of [.in 0] -> [~sig] -> [~out 0] and [~sig] -> [~thispoly]
    // (busy while the signal is not 0), [.in 1] -> [~thispoly] for its
    // words, and the ~thispoly's mute outlet to [.out 0]. The rendered sum
    // and the mute reports show what is muted.
    std::vector<std::string> log;
    LogSink voiceSink(log, "v");
    LogSink messageSink(log, "m");
    patcherImplementation p(1, nullptr);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::D_POLY, "v 2 2 1 1");
    YSE::pHandle* in = Inside(p, poly, YSE::OBJ::G_VOICE_IN, "0");
    YSE::pHandle* words = Inside(p, poly, YSE::OBJ::G_VOICE_IN, "1");
    YSE::pHandle* level = Inside(p, poly, YSE::OBJ::D_SIG, "0");
    YSE::pHandle* state = Inside(p, poly, YSE::OBJ::D_THISPOLY);
    YSE::pHandle* out = Inside(p, poly, YSE::OBJ::D_VOICE_OUT, "0");
    YSE::pHandle* report = Inside(p, poly, YSE::OBJ::G_VOICE_OUT, "0");
    p.Connect(in, 0, level, 0);
    p.Connect(words, 0, state, 0);
    p.Connect(level, 0, out, 0);
    p.Connect(level, 0, state, 0);
    p.Connect(state, 1, report, 0);
    p.Connect(poly, 0, dac, 0);
    Attach(p, poly, 1, messageSink);
    Attach(p, poly, 2, voiceSink);
    YSE::PATCHER::dPoly* host = PolyOf(p, poly);
    p.Calculate(YSE::T_DSP);

    // Through the ~thispoly: neither voice is busy, so both are muted at
    // once.
    poly->SetListData(1, "list automute 1");
    CHECK(log == strings{"v:i:1", "m:i:1", "v:i:2", "m:i:1"});
    log.clear();
    CHECK(host->AutoMute(1));
    CHECK(host->AutoMute(2));

    // A note unmutes voice 1 before it arrives; its signal keeps it busy.
    poly->SetListData(0, "note 0.5");
    CHECK(log == strings{"v:i:1", "m:i:0"});
    log.clear();
    for (int b = 0; b < 3; ++b) {
      p.Calculate(YSE::T_DSP);
      CHECK(AllNear(p.output[0], 0.5f));
    }
    CHECK(log.empty());
    // The next note goes to voice 2 (1 is busy), unmuting it.
    poly->SetListData(0, "note 0.25");
    p.Calculate(YSE::T_DSP);
    CHECK(AllNear(p.output[0], 0.75f));
    CHECK(log == strings{"v:i:2", "m:i:0"});
    log.clear();

    // Voice 1's signal falls to 0: the block that sees it mutes the voice.
    poly->SetListData(0, "setvalue 1 0");
    p.Calculate(YSE::T_DSP);
    CHECK(log == strings{"v:i:1", "m:i:1"});
    log.clear();
    p.Calculate(YSE::T_DSP);
    CHECK(AllNear(p.output[0], 0.25f));
    CHECK(PolyOf(p, poly)->Busy(2));
    // ... and it is free: the next note gets it and unmutes it.
    poly->SetListData(0, "note 0.125");
    p.Calculate(YSE::T_DSP);
    CHECK(AllNear(p.output[0], 0.375f));
    CHECK(log == strings{"v:i:1", "m:i:0"});
    log.clear();

    // Off: the voice stays as it is, and a free voice is no longer muted.
    poly->SetListData(1, "list automute 0");
    poly->SetListData(0, "setvalue 2 0");
    p.Calculate(YSE::T_DSP);
    p.Calculate(YSE::T_DSP);
    CHECK(log.empty());
    CHECK(AllNear(p.output[0], 0.125f));
    CHECK_FALSE(host->AutoMute(1));
  }

  TEST_CASE("~thispoly: automute from a busy number (#1294)") {
    // [.in 0] -> [~thispoly]: each message's number is the busy flag.
    messageRig r(2, true);
    YSE::PATCHER::dPoly* host = r.Poly();
    r.p.Calculate(YSE::T_DSP);
    r.Send("list automute 1");
    r.Send("mutemap");
    CHECK(r.Take() == strings{"v:i:1", "m:l:automute 1", "v:i:2", "m:l:automute 1", "m:l:1 1"});
    r.Send("note 1"); // unmuted, then busy
    r.Send("mutemap");
    CHECK(r.Take() == strings{"v:i:1", "m:i:1", "m:l:0 1"});
    r.Send("setvalue 1 0"); // free: muted
    r.Send("mutemap");
    CHECK(r.Take() == strings{"v:i:1", "m:i:0", "m:l:1 1"});
    // Automute is the ~poly's state per voice: a rebuild keeps it.
    Inside(r.p, r.poly, YSE::OBJ::D_SIG, "0");
    CHECK(host->AutoMute(1));
    r.Send("setvalue 1 1");
    r.Send("mutemap");
    CHECK(r.Take() == strings{"v:i:1", "m:i:1", "m:l:0 1"});
  }

  TEST_CASE("~poly: the #1294 words allocate nothing on the audio thread (#1294)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::D_POLY, "v 4 1 1 1");
    YSE::pHandle* in = Inside(p, poly, YSE::OBJ::G_VOICE_IN, "0");
    YSE::pHandle* level = Inside(p, poly, YSE::OBJ::D_SIG, "0");
    YSE::pHandle* state = Inside(p, poly, YSE::OBJ::D_THISPOLY);
    YSE::pHandle* out = Inside(p, poly, YSE::OBJ::D_VOICE_OUT, "0");
    YSE::pHandle* report = Inside(p, poly, YSE::OBJ::G_VOICE_OUT, "0");
    YSE::pHandle* sin = Inside(p, poly, YSE::OBJ::D_VOICE_IN, "0");
    p.Connect(in, 0, level, 0);
    p.Connect(in, 0, state, 0);
    p.Connect(level, 0, out, 0);
    p.Connect(level, 0, state, 0);
    p.Connect(sin, 0, out, 0);
    p.Connect(state, 1, report, 0);
    p.Connect(poly, 0, dac, 0);
    YSE::PATCHER::pObject* polyObj = p.GetObjectFromID(poly->GetID());
    p.Calculate(YSE::T_DSP);
    // Grown and shrunk once first, so the allocator's resize under the
    // guard is part of what the probe sees.
    poly->SetListData(0, "voices 8");
    p.SettleSignalBus();

    const std::vector<std::string> messages = {
        "busymap",         "mutemap 0", "exclude 2 1",         "note 0.5",        "bypass 3 1",
        "midinote 60 100", "busymap",   "exclude 0 0",         "bypass 0 0",      "list automute 1",
        "note 0.25",       "mutemap",   "midievent 144 62 90", "list automute 0", "voices 3",
        "busymap"};
    {
      TestHelpers::ProbeScope probe;
      for (const std::string& m : messages) {
        polyObj->GetInlet(0)->SetList(m, YSE::T_DSP);
        p.Calculate(YSE::T_DSP);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    p.SettleSignalBus();
    CHECK(static_cast<YSE::PATCHER::dPoly*>(polyObj)->Voices() == 3);
    // The allocator follows the shrink on the next routing word, also without
    // allocating.
    {
      TestHelpers::ProbeScope probe;
      polyObj->GetInlet(0)->SetList("midinote 64 100", YSE::T_DSP);
      polyObj->GetInlet(0)->SetList("busymap", YSE::T_DSP);
      p.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  TEST_CASE("~poly: voices, maps, exclude and bypass while the audio thread renders (#1294)") {
    // Under TSan, the voice count, the allocator's resize, a flag or the
    // voice copies read and written without synchronisation show up here.
    // The render thread sends some of the words itself, as a cord on the
    // audio thread would.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::D_POLY, "v 4 1 1 1");
    YSE::pHandle* in = Inside(p, poly, YSE::OBJ::G_VOICE_IN, "0");
    YSE::pHandle* level = Inside(p, poly, YSE::OBJ::D_SIG, "0");
    YSE::pHandle* state = Inside(p, poly, YSE::OBJ::D_THISPOLY);
    YSE::pHandle* out = Inside(p, poly, YSE::OBJ::D_VOICE_OUT, "0");
    YSE::pHandle* report = Inside(p, poly, YSE::OBJ::G_VOICE_OUT, "0");
    p.Connect(in, 0, level, 0);
    p.Connect(in, 0, state, 0);
    p.Connect(level, 0, out, 0);
    p.Connect(level, 0, state, 0);
    p.Connect(state, 1, report, 0);
    p.Connect(poly, 0, dac, 0);
    YSE::PATCHER::pObject* polyObj = p.GetObjectFromID(poly->GetID());
    auto* host = static_cast<YSE::PATCHER::dPoly*>(polyObj);

    std::atomic<bool> done{false};
    std::atomic<bool> finite{true};
    std::atomic<int> blocks{0};
    std::thread render([&] {
      // At least one block before and after the control thread's work,
      // whatever the scheduling.
      int b = 0;
      while (!done.load() || blocks.load() == 0) {
        switch (b % 16) {
        case 3:
          polyObj->GetInlet(0)->SetList("voices " + std::to_string(1 + (b / 16) % 6), YSE::T_DSP);
          break;
        case 7:
          polyObj->GetInlet(0)->SetList("busymap", YSE::T_DSP);
          break;
        case 11:
          polyObj->GetInlet(0)->SetList("note 0.25", YSE::T_DSP);
          break;
        default:
          break;
        }
        p.Calculate(YSE::T_DSP);
        if (!std::isfinite(p.output[0].getPtr()[0])) finite.store(false);
        blocks.fetch_add(1);
        b++;
      }
    });
    while (blocks.load() == 0)
      std::this_thread::yield();
    for (int i = 0; i < 400; ++i) {
      switch (i % 10) {
      case 0:
        poly->SetListData(0, "voices " + std::to_string(1 + i % 7));
        break;
      case 1:
        poly->SetListData(0, "note 0.5");
        break;
      case 2:
        poly->SetListData(0, "midinote " + std::to_string(60 + i % 12) + " 100");
        break;
      case 3:
        poly->SetListData(0, "exclude " + std::to_string(i % 5) + " " + std::to_string(i % 2));
        break;
      case 4:
        poly->SetListData(0, "bypass " + std::to_string(i % 5) + " " + std::to_string(i % 2));
        break;
      case 5:
        poly->SetListData(0, "mutemap");
        break;
      case 6:
        poly->SetListData(0, "list automute " + std::to_string(i % 2));
        break;
      case 7:
        poly->SetListData(0, "allnotesoff");
        break;
      case 8:
        poly->SetListData(0, "setvalue 0 0");
        break;
      default:
        // An edit inside: the voices are rebuilt at the count asked for.
        if ((i % 50) == 9) level->SetParams((i % 100) == 9 ? "0.5" : "0");
        break;
      }
    }
    const int before = blocks.load();
    while (blocks.load() == before)
      std::this_thread::yield();
    done.store(true);
    render.join();
    // Settled with nothing else sending: the voices are what was asked for.
    p.SettleSignalBus();
    p.Calculate(YSE::T_DSP);
    CHECK(host->Voices() == host->WantedVoices());
    CHECK(finite.load());
    CHECK(blocks.load() > before);
  }

} // TEST_SUITE("patcher")
