// Tests for `.polymidiin` and the MIDI half of `~poly`, issue #756: the
// ~poly's `midievent` and `mpeevent` messages reach the voices without a cord,
// and `.polymidiin` inside a voice outputs them as MIDI bytes; `.mpeparse`'s
// rightmost outlet sends the `mpeevent` the ~poly reads.
//
// What must hold, each with a control:
//  - `midievent` allocates by key as `midinote` does (note-on to a free voice,
//    the note-off to the voice holding it), poly pressure goes to the voice
//    holding the key and other channel messages to the target;
//  - `allnotesoff` ends a `midievent` note with a note-off on its channel;
//  - `mpeevent` goes straight to the voice it names (-1: every voice), takes
//    no voice from the allocator, and a voice the ~poly lacks gets nothing;
//  - `.polymidiin` sends only inside a voice;
//  - `.mpeparse`'s `mpeevent` carries the zone, the voice the channel is and
//    the bytes, and nothing for a channel outside the zone;
//  - end to end, `.mpeformat` -> `.mpeparse` -> `~poly` -> `.polymidiin` ->
//    `.mpeparse` in each voice gives every note its own voice, audibly;
//  - routing and rendering allocate nothing on the audio thread, and a control
//    thread can route MPE while the audio thread renders.

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>
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
      if (std::fabs(p[i] - value) > 1e-4f) return false;
    return true;
  }

  YSE::pHandle* Inside(patcherImplementation& p, YSE::pHandle* container, const char* type,
                       const std::string& args = "") {
    YSE::pHandle* h = p.CreateObject(type, args);
    REQUIRE(h != nullptr);
    p.SetObjectContainer(h, container);
    return h;
  }

  // Records every message on its inlet as "<tag>:<kind>:<value>" into one
  // shared log, so the order across outlets shows.
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
    // Publishes, so the snapshot the sends read has the cord.
    p.CreateObject(YSE::OBJ::G_MESSAGE, "");
  }

  // A ~poly of `voices` voices with one inlet and one message outlet, each
  // voice [.polymidiin] -> [.out 0] and [.in 0] -> [.out 0]. The message
  // outlet logs "m", the voice outlet "v".
  struct midiRig {
    std::vector<std::string> log;
    LogSink voice{log, "v"};
    LogSink message{log, "m"};
    patcherImplementation p{1, nullptr};
    YSE::pHandle* poly = nullptr;

    explicit midiRig(int voices) {
      poly = p.CreateObject(YSE::OBJ::D_POLY, "v " + std::to_string(voices) + " 1 0 1");
      REQUIRE(poly != nullptr);
      YSE::pHandle* midi = Inside(p, poly, YSE::OBJ::M_POLYMIDIIN);
      YSE::pHandle* in = Inside(p, poly, YSE::OBJ::G_VOICE_IN, "0");
      YSE::pHandle* out = Inside(p, poly, YSE::OBJ::G_VOICE_OUT, "0");
      p.Connect(midi, 0, out, 0);
      p.Connect(in, 0, out, 0);
      Attach(p, poly, 0, message);
      Attach(p, poly, 1, voice);
    }
    YSE::PATCHER::dPoly* Poly() {
      return static_cast<YSE::PATCHER::dPoly*>(p.GetObjectFromID(poly->GetID()));
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

  TEST_CASE(".polymidiin: registry, category and shape (#756)") {
    bool listed = false;
    for (const auto& n : YSE::PATCHER::Register().AllNames())
      if (n == YSE::OBJ::M_POLYMIDIIN) listed = true;
    CHECK(listed);
    CHECK(std::string(YSE::OBJ::M_POLYMIDIIN) == ".polymidiin");

    patcherImplementation p(1, nullptr);
    YSE::pHandle* midi = p.CreateObject(YSE::OBJ::M_POLYMIDIIN, "");
    REQUIRE(midi != nullptr);
    CHECK(midi->GetInputs() == 0);
    REQUIRE(midi->GetOutputs() == 1);
    CHECK(midi->OutputDataType(0) == YSE::OUT_TYPE::LIST);
    YSE::PATCHER::pObject* object = p.GetObjectFromID(midi->GetID());
    CHECK(object->GetCategory() == YSE::PATCHER::pCategory::MIDI);
    CHECK_FALSE(object->IsDSPObject());
    CHECK(object->VoiceEventSink());

    // .mpeparse grew the mpeevent outlet on the right, so the six it had keep
    // their numbers.
    YSE::pHandle* parse = p.CreateObject(YSE::OBJ::M_MPEPARSE, "");
    REQUIRE(parse != nullptr);
    REQUIRE(parse->GetOutputs() == 7);
    CHECK(parse->OutputDataType(6) == YSE::OUT_TYPE::LIST);
  }

  TEST_CASE("~poly: midievent allocates a note-on by key, its note-off finds the voice (#756)") {
    midiRig r(2);
    r.p.Calculate(YSE::T_DSP);
    r.Send("midievent 145 60 100");
    r.Send("midievent 146 64 90");
    r.Send("midievent 147 67 80"); // no free voice, steal off
    CHECK(r.Take() == strings{"v:i:1", "m:l:145 60 100", "v:i:2", "m:l:146 64 90"});
    CHECK(r.Poly()->PitchOfVoice(1) == 60);
    CHECK(r.Poly()->PitchOfVoice(2) == 64);

    r.p.Calculate(YSE::T_DSP);
    // Both spellings of a release find the voice, and pass as they came.
    r.Send("midievent 129 60 64");
    r.Send("midievent 146 64 0");
    r.Send("midievent 128 67 0"); // held by nobody
    CHECK(r.Take() == strings{"v:i:1", "m:l:129 60 64", "v:i:2", "m:l:146 64 0"});
    CHECK(r.Poly()->PitchOfVoice(1) == -1);
    CHECK(r.Poly()->PitchOfVoice(2) == -1);

    // The ~poly's `steal` applies, as for midinote.
    r.p.Calculate(YSE::T_DSP);
    r.Send("steal 1");
    r.Send("midievent 144 50 100");
    r.Send("midievent 144 52 100");
    r.p.Calculate(YSE::T_DSP);
    r.Send("midievent 144 55 100");
    CHECK(r.Take() ==
          strings{"v:i:1", "m:l:144 50 100", "v:i:2", "m:l:144 52 100", "v:i:1", "m:l:144 55 100"});
    CHECK(r.Poly()->Dropped() == 0);
  }

  TEST_CASE("~poly: midievent poly pressure follows the key, the rest goes to the target (#756)") {
    midiRig r(3);
    r.p.Calculate(YSE::T_DSP);
    r.Send("midievent 144 60 100");
    r.Send("midievent 144 62 100");
    r.Take();

    r.Send("midievent 160 62 33"); // poly pressure on key 62: voice 2
    r.Send("midievent 160 70 33"); // a key nobody holds
    CHECK(r.Take() == strings{"v:i:2", "m:l:160 62 33"});

    // Control change, program change (one data byte), bend: every voice.
    r.Send("midievent 176 64 127");
    r.Send("midievent 192 5");
    CHECK(r.Take() == strings{"v:i:1", "m:l:176 64 127", "v:i:2", "m:l:176 64 127", "v:i:3",
                              "m:l:176 64 127", "v:i:1", "m:l:192 5", "v:i:2", "m:l:192 5", "v:i:3",
                              "m:l:192 5"});
    r.Send("target 3");
    r.Send("midievent 224 0 80 0.5"); // the fourth number, detune, is not used
    CHECK(r.Take() == strings{"v:i:3", "m:l:224 0 80"});
    r.Send("target -1");
    r.Send("midievent 224 0 64");
    CHECK(r.Take().empty());

    // Not channel messages, or short of data: nothing, and no voice taken.
    r.Send("target 0");
    r.Send("midievent 60 100");
    r.Send("midievent 240 1 2");
    r.Send("midievent 144 61");
    r.Send("midievent");
    CHECK(r.Take().empty());
    CHECK(r.Poly()->PitchOfVoice(3) == -1);
    // A data byte is clamped to 0-127.
    r.Send("midievent 176 300 -4");
    CHECK(r.Take() ==
          strings{"v:i:1", "m:l:176 127 0", "v:i:2", "m:l:176 127 0", "v:i:3", "m:l:176 127 0"});
  }

  TEST_CASE("~poly: allnotesoff ends a midievent note on its channel, a midinote as before "
            "(#756)") {
    midiRig r(3);
    r.p.Calculate(YSE::T_DSP);
    r.Send("midievent 147 60 100"); // channel 4
    r.Send("midinote 62 90");
    r.Send("midievent 144 64 80"); // channel 1
    r.Take();
    r.Send("allnotesoff");
    CHECK(r.Take() ==
          strings{"v:i:1", "m:l:131 60 0", "v:i:2", "m:l:62 0", "v:i:3", "m:l:128 64 0"});
    // A voice that held a midievent note and then a midinote ends as a midinote.
    r.p.Calculate(YSE::T_DSP);
    r.Send("midievent 147 60 100");
    r.Send("midinote 60 0");
    r.p.Calculate(YSE::T_DSP);
    r.Send("midinote 61 100");
    r.Take();
    r.Send("allnotesoff");
    CHECK(r.Take() == strings{"v:i:1", "m:l:61 0"});
  }

  TEST_CASE("~poly: mpeevent goes straight to the voice it names (#756)") {
    midiRig r(4);
    r.p.Calculate(YSE::T_DSP);
    // Voice 3 is channel 4 in the lower zone.
    r.Send("mpeevent 1 1 3 4 147 60 100");
    CHECK(r.Take() == strings{"v:i:3", "m:l:147 60 100"});
    // No allocation: the voice holds nothing, and a second note-on for the
    // same voice goes there too, busy or not.
    CHECK(r.Poly()->PitchOfVoice(3) == -1);
    r.Send("mpeevent 1 1 3 4 147 61 100");
    CHECK(r.Take() == strings{"v:i:3", "m:l:147 61 100"});
    // A message number without its channel takes the channel field's.
    r.Send("mpeevent 1 1 2 3 208 77"); // channel pressure, one data byte
    CHECK(r.Take() == strings{"v:i:2", "m:l:210 77"});
    // -1, the master channel: every voice.
    r.Send("mpeevent 1 1 -1 1 224 0 72");
    CHECK(r.Take() == strings{"v:i:1", "m:l:224 0 72", "v:i:2", "m:l:224 0 72", "v:i:3",
                              "m:l:224 0 72", "v:i:4", "m:l:224 0 72"});
    // A voice this ~poly does not have, voice 0, short data, a system status.
    r.Send("mpeevent 1 1 5 6 149 60 100");
    r.Send("mpeevent 1 1 0 1 144 60 100");
    r.Send("mpeevent 1 1 1 2 145 60");
    r.Send("mpeevent 1 1 1 2 240 1 2");
    CHECK(r.Take().empty());
    // The target does not apply: mpeevent names its voice.
    r.Send("target -1");
    r.Send("mpeevent 1 1 1 2 145 60 0");
    CHECK(r.Take() == strings{"v:i:1", "m:l:145 60 0"});
    CHECK(r.Poly()->Dropped() == 0);
  }

  TEST_CASE(".polymidiin: sends only inside a voice, never from the definition (#756)") {
    std::vector<std::string> log;
    LogSink outside(log, "x");
    patcherImplementation p(1, nullptr);
    YSE::pHandle* top = p.CreateObject(YSE::OBJ::M_POLYMIDIIN, "");
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::D_POLY, "v 2 1 0 1");
    YSE::pHandle* inner = Inside(p, poly, YSE::OBJ::M_POLYMIDIIN);
    (void)inner;
    Attach(p, top, 0, outside);
    p.Calculate(YSE::T_DSP);
    poly->SetListData(0, "mpeevent 1 1 -1 1 176 7 100");
    poly->SetListData(0, "midievent 144 60 100");
    CHECK(log.empty());
    // The template copy inside the ~poly never runs: nothing reaches a sink
    // wired to it either (no cord can leave the ~poly, so a .out in the
    // definition would be the only way; covered by ~poly's own tests).
  }

  TEST_CASE(".mpeparse: the mpeevent outlet carries the zone, the voice and the bytes (#756)") {
    std::vector<std::string> log;
    std::vector<std::unique_ptr<LogSink>> taps;
    patcherImplementation p(1, nullptr);
    YSE::pHandle* parse = p.CreateObject(YSE::OBJ::M_MPEPARSE, "0 15");
    REQUIRE(parse != nullptr);
    for (int o = 0; o < 7; o++) {
      taps.push_back(std::make_unique<LogSink>(log, "o" + std::to_string(o)));
      Attach(p, parse, o, *taps.back());
    }
    auto take = [&log] {
      strings taken;
      taken.swap(log);
      return taken;
    };

    // Channel 3 is voice 2 of the lower zone; the event comes first, then
    // the role, the channel and the value as before.
    parse->SetListData(0, "146 60 100");
    CHECK(take() == strings{"o6:l:mpeevent 1 1 2 3 146 60 100", "o5:i:1", "o4:i:3", "o0:l:60 100"});
    // A note-off keeps its spelling in the event; the note outlet folds it.
    parse->SetListData(0, "130 60 64");
    CHECK(take() == strings{"o6:l:mpeevent 1 1 2 3 130 60 64", "o5:i:1", "o4:i:3", "o0:l:60 0"});
    // A bend keeps its two bytes; channel pressure has one.
    parse->SetListData(0, "226 40 70");
    CHECK(take().front() == "o6:l:mpeevent 1 1 2 3 226 40 70");
    parse->SetListData(0, "210 90");
    CHECK(take().front() == "o6:l:mpeevent 1 1 2 3 210 90");
    // The master channel is every voice's: voice -1.
    parse->SetListData(0, "224 0 72");
    CHECK(take().front() == "o6:l:mpeevent 1 1 -1 1 224 0 72");
    // Messages the four expression outlets leave to .midiparse still reach
    // a voice: a sustain pedal on the master, a program change on a member.
    parse->SetListData(0, "176 64 127");
    CHECK(take() == strings{"o6:l:mpeevent 1 1 -1 1 176 64 127"});
    parse->SetListData(0, "194 5");
    CHECK(take() == strings{"o6:l:mpeevent 1 1 2 3 194 5"});
    // Running status: each message of a chord is its own event.
    parse->SetListData(0, "147 60 100 62 90");
    strings chord = take();
    REQUIRE(chord.size() == 8);
    CHECK(chord[0] == "o6:l:mpeevent 1 1 3 4 147 60 100");
    CHECK(chord[4] == "o6:l:mpeevent 1 1 3 4 147 62 90");

    // A four-member zone: channel 6 is outside it and sends no event.
    parse->SetListData(0, "176 100 6");
    parse->SetListData(0, "176 101 0");
    parse->SetListData(0, "176 6 4");
    CHECK(take() == strings{"o6:l:mpeevent 1 1 -1 1 176 100 6", "o6:l:mpeevent 1 1 -1 1 176 101 0",
                            "o6:l:mpeevent 1 1 -1 1 176 6 4"});
    parse->SetListData(0, "149 60 100");
    CHECK(take() == strings{"o5:i:0", "o4:i:6", "o0:l:60 100"});
  }

  TEST_CASE(".mpeparse: in the upper zone the voice counts down from channel 15 (#756)") {
    std::vector<std::string> log;
    LogSink tap(log, "e");
    patcherImplementation p(1, nullptr);
    YSE::pHandle* parse = p.CreateObject(YSE::OBJ::M_MPEPARSE, "1 4");
    REQUIRE(parse != nullptr);
    Attach(p, parse, 6, tap);
    parse->SetListData(0, "158 60 100"); // channel 15: voice 1
    parse->SetListData(0, "155 60 100"); // channel 12: voice 4
    parse->SetListData(0, "159 60 100"); // channel 16: the master
    parse->SetListData(0, "154 60 100"); // channel 11: outside
    CHECK(log == strings{"e:l:mpeevent 16 2 1 15 158 60 100", "e:l:mpeevent 16 2 4 12 155 60 100",
                         "e:l:mpeevent 16 2 -1 16 159 60 100"});
  }

  TEST_CASE("mpe: .mpeformat -> .mpeparse -> ~poly -> .polymidiin gives each note a voice "
            "(#756)") {
    // The whole path a patch author wires, and heard at the ~dac. Each voice
    // reads its MIDI with its own .mpeparse: the note goes out of the ~poly's
    // message outlet, the pressure drives a ~sig the voice sums into the
    // ~poly's signal outlet. One voice per note means one pressure per note.
    std::vector<std::string> log;
    LogSink voice(log, "v");
    LogSink message(log, "m");
    patcherImplementation p(1, nullptr);
    YSE::pHandle* format = p.CreateObject(YSE::OBJ::M_MPEFORMAT, "");
    YSE::pHandle* parse = p.CreateObject(YSE::OBJ::M_MPEPARSE, "0 4");
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::D_POLY, "mpe 4 1 1 1");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(format != nullptr);
    REQUIRE(parse != nullptr);
    REQUIRE(poly != nullptr);
    YSE::pHandle* midi = Inside(p, poly, YSE::OBJ::M_POLYMIDIIN);
    YSE::pHandle* inner = Inside(p, poly, YSE::OBJ::M_MPEPARSE, "0 4");
    YSE::pHandle* sig = Inside(p, poly, YSE::OBJ::D_SIG, "0");
    YSE::pHandle* out = Inside(p, poly, YSE::OBJ::D_VOICE_OUT, "0");
    YSE::pHandle* report = Inside(p, poly, YSE::OBJ::G_VOICE_OUT, "0");
    p.Connect(format, 0, parse, 0);
    p.Connect(parse, 6, poly, 0);
    p.Connect(poly, 0, dac, 0);
    p.Connect(midi, 0, inner, 0);
    p.Connect(inner, 2, sig, 0); // pressure
    p.Connect(inner, 0, report, 0); // note
    p.Connect(sig, 0, out, 0);
    Attach(p, poly, 1, message);
    Attach(p, poly, 2, voice);
    p.Calculate(YSE::T_DSP);
    CHECK(AllNear(p.output[0], 0.f));

    // Two notes, on member channels 2 and 3, each pressed differently.
    format->SetIntData(4, 2);
    format->SetListData(0, "60 100");
    format->SetIntData(2, 30);
    format->SetIntData(4, 3);
    format->SetListData(0, "64 90");
    format->SetIntData(2, 50);
    CHECK(log == strings{"v:i:1", "m:l:60 100", "v:i:2", "m:l:64 90"});
    p.Calculate(YSE::T_DSP);
    CHECK(AllNear(p.output[0], 80.f));

    // Pressure on the master channel reaches every voice.
    format->SetIntData(4, 1);
    format->SetIntData(2, 10);
    p.Calculate(YSE::T_DSP);
    CHECK(AllNear(p.output[0], 40.f));

    // One note's pressure changes that voice alone.
    format->SetIntData(4, 3);
    format->SetIntData(2, 25);
    p.Calculate(YSE::T_DSP);
    CHECK(AllNear(p.output[0], 55.f));

    // A channel outside the four-member zone reaches no voice.
    format->SetIntData(4, 9);
    format->SetIntData(2, 100);
    p.Calculate(YSE::T_DSP);
    CHECK(AllNear(p.output[0], 55.f));

    // The release reaches the voice that played the note.
    log.clear();
    format->SetIntData(4, 3);
    format->SetListData(0, "64 0");
    CHECK(log == strings{"v:i:2", "m:l:64 0"});
    CHECK(static_cast<YSE::PATCHER::dPoly*>(p.GetObjectFromID(poly->GetID()))->Dropped() == 0);
  }

  TEST_CASE("~poly: MIDI routing and .polymidiin allocate nothing on the audio thread (#756)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* parse = p.CreateObject(YSE::OBJ::M_MPEPARSE, "0 15");
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::D_POLY, "v 15 1 1 0");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* midi = Inside(p, poly, YSE::OBJ::M_POLYMIDIIN);
    YSE::pHandle* inner = Inside(p, poly, YSE::OBJ::M_MPEPARSE, "0 15");
    YSE::pHandle* sig = Inside(p, poly, YSE::OBJ::D_SIG, "0");
    YSE::pHandle* out = Inside(p, poly, YSE::OBJ::D_VOICE_OUT, "0");
    p.Connect(parse, 6, poly, 0);
    p.Connect(poly, 0, dac, 0);
    p.Connect(midi, 0, inner, 0);
    p.Connect(inner, 2, sig, 0);
    p.Connect(sig, 0, out, 0);
    YSE::PATCHER::pObject* parseObj = p.GetObjectFromID(parse->GetID());
    YSE::PATCHER::pObject* polyObj = p.GetObjectFromID(poly->GetID());
    p.Calculate(YSE::T_DSP);

    const std::vector<std::string> bytes = {"145 60 100", "209 40",     "225 0 80",
                                            "177 74 30",  "144 64 127", "208 12",
                                            "159 60 100", "129 60 0",   "128 64 0"};
    const std::vector<std::string> words = {"midievent 144 60 100",
                                            "midievent 160 60 20",
                                            "midievent 176 64 127",
                                            "midievent 192 3",
                                            "midievent 128 60 0",
                                            "midievent 145 61 90",
                                            "allnotesoff",
                                            "mpeevent 1 1 15 16 159 60 100",
                                            "mpeevent 1 1 -1 1 224 0 80"};
    {
      TestHelpers::ProbeScope probe;
      for (const std::string& m : bytes) {
        parseObj->GetInlet(0)->SetList(m, YSE::T_DSP);
        p.Calculate(YSE::T_DSP);
      }
      for (const std::string& m : words) {
        polyObj->GetInlet(0)->SetList(m, YSE::T_DSP);
        p.Calculate(YSE::T_DSP);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK_FALSE(AllNear(p.output[0], 0.f));
  }

  TEST_CASE("~poly: a control thread routes MIDI and MPE while the audio thread renders (#756)") {
    // Under TSan, an unsynchronised write the cordless delivery makes into a
    // voice while that voice renders shows up here.
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* parse = p.CreateObject(YSE::OBJ::M_MPEPARSE, "0 15");
    YSE::pHandle* poly = p.CreateObject(YSE::OBJ::D_POLY, "v 8 1 1 0");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* midi = p.CreateObject(YSE::OBJ::M_POLYMIDIIN, "");
    YSE::pHandle* inner = p.CreateObject(YSE::OBJ::M_MPEPARSE, "0 15");
    YSE::pHandle* sig = p.CreateObject(YSE::OBJ::D_SIG, "0");
    YSE::pHandle* out = p.CreateObject(YSE::OBJ::D_VOICE_OUT, "0");
    for (YSE::pHandle* h : {midi, inner, sig, out})
      p.SetContainer(h, poly);
    p.Connect(parse, 6, poly, 0);
    p.Connect(poly, 0, dac, 0);
    p.Connect(midi, 0, inner, 0);
    p.Connect(inner, 2, sig, 0);
    p.Connect(sig, 0, out, 0);
    YSE::DSP::patcherInsert insert(p);

    std::atomic<bool> done{false};
    std::atomic<bool> finite{true};
    std::atomic<int> blocks{0};
    std::atomic<int> routed{0};
    std::thread render([&] {
      MULTICHANNELBUFFER io;
      io.resize(1);
      io[0].resize(STANDARD_BUFFERSIZE);
      while (!done.load() || blocks.load() == 0) {
        io[0] = 0.f;
        insert.process(io);
        if (!std::isfinite(io[0].getPtr()[0])) finite.store(false);
        blocks.fetch_add(1);
      }
    });
    // The render has run before the first message, whatever the scheduling.
    while (blocks.load() == 0)
      std::this_thread::yield();
    for (int i = 0; i < 400; ++i) {
      const int channel = 2 + i % 8;
      switch (i % 6) {
      case 0:
        parse->SetListData(0, std::to_string(143 + channel) + " " + std::to_string(60 + i % 12) +
                                  " 100");
        break;
      case 1:
        parse->SetListData(0, std::to_string(207 + channel) + " " + std::to_string(i % 128));
        break;
      case 2:
        parse->SetListData(0, "208 " + std::to_string(i % 64)); // master: every voice
        break;
      case 3:
        poly->SetListData(0, "midievent 144 " + std::to_string(40 + i % 20) + " 90");
        break;
      case 4:
        poly->SetListData(0, "allnotesoff");
        break;
      default:
        // An edit inside: every voice is rebuilt under the render.
        if ((i % 60) == 5) sig->SetParams((i % 120) == 5 ? "0.5" : "0");
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
