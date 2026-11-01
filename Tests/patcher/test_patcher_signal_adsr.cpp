// Tests for the ~adsr patcher object (patcher/generatorObjects/dAdsr), issue
// #1064. The envelope is DSP::adsr's and is tested in Tests/dsp/test_adsr.cpp;
// these show that the gate signal, the numbers, the lists, the messages and
// the arguments reach it, that the mute state goes out only through the
// armed outlet (#1037), and that it works in a rendered patch after a JSON
// round trip.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dAdsr.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using TestHelpers::IntSink;
using TestHelpers::ScopedSampleRate;
using TestHelpers::Wire;
using YSE::DSP::buffer;
using YSE::PATCHER::dAdsr;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // ~adsr with buffer sinks on the envelope and gate outlets and an int sink
  // on the mute outlet. Block() runs one block and the flush the patcher
  // runs after its traversal, and returns the mute value sent, or -1.
  struct Rig {
    BufferSink out;
    BufferSink gate;
    IntSink mute;
    dAdsr env;

    Rig() {
      Wire(env, 0, out);
      Wire(env, 1, gate);
      Wire(env, 2, mute);
    }
    int Flush() {
      env.FlushArmedOutlets(YSE::T_GUI);
      return mute.gotInt ? mute.received : -1;
    }
    int Block() {
      mute.gotInt = false;
      env.ResetDSP();
      env.Calculate(YSE::T_DSP);
      return Flush();
    }
    // A gate signal on the trigger inlet.
    int Block(buffer& g) {
      mute.gotInt = false;
      env.ResetDSP();
      env.GetInlet(0)->SetBuffer(&g, YSE::T_DSP); // the last input: runs CALC()
      return Flush();
    }
    float At(unsigned int i) const {
      return out.received->getPtr()[i];
    }
    float GateAt(unsigned int i) const {
      return gate.received->getPtr()[i];
    }
  };

  float Line(float a, float b, unsigned int n, unsigned int k) {
    return a + (b - a) * static_cast<float>(k + 1) / static_cast<float>(n);
  }

  float GuiFloat(YSE::pHandle* h) {
    return std::stof(h->GetGuiValue());
  }

  int GuiInt(YSE::pHandle* h) {
    return std::stoi(h->GetGuiValue());
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~adsr: registry and shape (#1064)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_ADSR);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~adsr");
    CHECK(obj->GetInputs() == 5);
    CHECK(obj->GetOutputs() == 3);
    for (unsigned int i = 0; i < 5; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(2) == YSE::OUT_TYPE::INT);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_ADSR));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 8);
  }

  TEST_CASE("~adsr: a number triggers at that amplitude and 0 releases; the mute state is "
            "armed, then sent (#1064, #1037)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    CHECK(rig.Block() == -1); // at rest: nothing to say
    REQUIRE(rig.out.received != nullptr);
    CHECK(rig.At(0) == 0.f);

    rig.env.GetInlet(0)->SetFloat(0.6f, YSE::T_GUI);
    rig.env.ResetDSP();
    rig.env.Calculate(YSE::T_DSP);
    CHECK_FALSE(rig.mute.gotInt); // CALC() only arms
    CHECK(rig.env.GetOutlet(2)->Armed());
    CHECK(rig.Flush() == 0);
    for (unsigned int i = 0; i < N; ++i) {
      REQUIRE(rig.At(i) == doctest::Approx(0.6f)); // the default is a plain gate
      REQUIRE(rig.GateAt(i) == 1.f);
    }
    CHECK(rig.Block() == -1); // sustaining: no change

    rig.env.GetInlet(0)->SetInt(0, YSE::T_GUI);
    CHECK(rig.Block() == 1);
    CHECK(rig.At(0) == 0.f);
    CHECK(rig.GateAt(0) == 0.f);
  }

  TEST_CASE("~adsr: the arguments set attack, decay, sustain, release, legato, retrigger, "
            "maxsustain and triggermode (#1064)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.env.SetParams("4 4 0.5 8");
    rig.env.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    CHECK(rig.Block() == 0);
    for (unsigned int k = 0; k < 4; ++k)
      REQUIRE(rig.At(k) == doctest::Approx(Line(0.f, 1.f, 4, k)));
    for (unsigned int k = 4; k < 8; ++k)
      REQUIRE(rig.At(k) == doctest::Approx(Line(1.f, 0.5f, 4, k - 4)));
    CHECK(rig.At(N - 1) == doctest::Approx(0.5f));
    rig.env.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    CHECK(rig.Block() == 1);
    for (unsigned int k = 0; k < 8; ++k)
      REQUIRE(rig.At(k) == doctest::Approx(Line(0.5f, 0.f, 8, k)));

    // legato 1: a restart from 0.5 rises without a ramp down.
    Rig legato;
    legato.env.SetParams("4 0 0.5 0 1");
    legato.env.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    legato.Block();
    legato.env.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    CHECK(legato.Block() == -1); // not from rest
    CHECK(legato.At(0) == doctest::Approx(0.625f));

    // retrigger 2: a ramp down over two samples first.
    Rig retrig;
    retrig.env.SetParams("0 0 1 0 0 2");
    retrig.env.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    retrig.Block();
    retrig.env.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    retrig.Block();
    CHECK(retrig.At(0) == doctest::Approx(0.5f));
    CHECK(retrig.At(1) == 0.f);
    CHECK(retrig.At(2) == doctest::Approx(1.f));
    CHECK(retrig.GateAt(1) == 0.f);
    CHECK(retrig.GateAt(2) == 1.f);

    // maxsustain 10 and triggermode 1: plays to the end on its own.
    Rig toEnd;
    toEnd.env.SetParams("0 0 1 0 0 5 10 1");
    toEnd.env.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    CHECK(toEnd.Block() == 1); // started and finished: at rest at the end
    CHECK(toEnd.At(11) == doctest::Approx(1.f));
    CHECK(toEnd.At(12) == 0.f);
  }

  TEST_CASE("~adsr: a gate signal on the left inlet triggers and releases (#1064, #1036)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.env.SetParams("4 0 0.5 4");
    buffer g(N);
    g = 0.f;
    for (unsigned int i = 10; i < 60; ++i)
      g.getPtr()[i] = 0.8f;
    CHECK(rig.Block(g) == 1); // started and finished in one block
    CHECK(rig.At(9) == 0.f);
    CHECK(rig.At(13) == doctest::Approx(0.8f));
    CHECK(rig.At(30) == doctest::Approx(0.4f));
    CHECK(rig.At(61) == doctest::Approx(0.2f));
    CHECK(rig.At(63) == 0.f);
    CHECK(rig.GateAt(10) == 1.f);
    CHECK(rig.GateAt(60) == 0.f);
  }

  TEST_CASE("~adsr: numbers at inlets 2-5 are attack, decay, sustain and release, and are "
            "not saved (#1064, #1036)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.env.GetInlet(1)->SetFloat(4.f, YSE::T_GUI);
    rig.env.GetInlet(2)->SetInt(2, YSE::T_GUI);
    rig.env.GetInlet(3)->SetFloat(0.5f, YSE::T_GUI);
    rig.env.GetInlet(4)->SetInt(8, YSE::T_GUI);
    CHECK(rig.env.GetParams() == "");
    rig.env.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    rig.Block();
    CHECK(rig.At(3) == doctest::Approx(1.f));
    CHECK(rig.At(4) == doctest::Approx(0.75f));
    CHECK(rig.At(5) == doctest::Approx(0.5f));
    rig.env.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    rig.Block();
    CHECK(rig.At(3) == doctest::Approx(0.25f));
    CHECK(rig.At(7) == 0.f);

    rig.env.GetInlet(3)->SetInt(1, YSE::T_GUI);
    rig.env.GetInlet(4)->SetFloat(0.f, YSE::T_GUI);
    rig.env.GetInlet(1)->SetInt(0, YSE::T_GUI);
    rig.env.GetInlet(2)->SetFloat(0.f, YSE::T_GUI);
    rig.env.GetInlet(0)->SetInt(1, YSE::T_GUI);
    rig.Block();
    CHECK(rig.At(0) == doctest::Approx(1.f));
    CHECK(rig.At(N - 1) == doctest::Approx(1.f));
  }

  TEST_CASE("~adsr: a list sets attack, decay, sustain and release; messages set every "
            "setting (#1064)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.env.GetInlet(0)->SetList("4 4 0.5 8", YSE::T_GUI);
    rig.env.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    rig.Block();
    CHECK(rig.At(3) == doctest::Approx(1.f));
    CHECK(rig.At(7) == doctest::Approx(0.5f));

    // A shorter list sets the leading ones; a word in it is skipped.
    rig.env.GetInlet(0)->SetList("2 x", YSE::T_GUI);
    rig.env.SetMessage("legato 1", 0.f);
    rig.env.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    rig.Block();
    CHECK(rig.At(0) == doctest::Approx(0.75f)); // from 0.5 to 1 in two samples
    CHECK(rig.At(1) == doctest::Approx(1.f));
    CHECK(rig.At(5) == doctest::Approx(0.5f)); // the 4 ms decay is kept

    Rig msg;
    msg.env.GetInlet(0)->SetMessage("attack 2", YSE::T_GUI);
    msg.env.SetMessage("decay 2", 0.f);
    msg.env.SetMessage("sustain 0.5", 0.f);
    msg.env.SetMessage("release 2", 0.f);
    msg.env.SetMessage("maxsustain 0", 0.f);
    msg.env.SetMessage("retrigger 0", 0.f);
    msg.env.SetMessage("triggermode 1", 0.f);
    // Malformed or unknown messages change nothing.
    msg.env.SetMessage("attack", 0.f);
    msg.env.SetMessage("decay abc", 0.f);
    msg.env.SetMessage("sustain nan", 0.f);
    msg.env.SetMessage("bogus 3", 0.f);
    msg.env.SetMessage("", 0.f);
    msg.env.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    CHECK(msg.Block() == 1);
    const float want[] = {0.5f, 1.f, 0.75f, 0.5f, 0.25f, 0.f};
    for (unsigned int k = 0; k < 6; ++k)
      REQUIRE(msg.At(k) == doctest::Approx(want[k]));
    CHECK(msg.env.GetParams() == "");

    // triggermode 1 ignores a release while it runs; triggermode 0 obeys.
    Rig mode;
    mode.env.SetParams("256");
    mode.env.SetMessage("triggermode 1", 0.f);
    mode.env.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    mode.Block();
    mode.env.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    mode.Block();
    CHECK(mode.At(N - 1) == doctest::Approx(1.f)); // the attack ran on
    mode.env.SetMessage("triggermode 0", 0.f);
    mode.env.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    CHECK(mode.Block() == 1);
  }

  TEST_CASE("~adsr: a connected time signal wins over the number (#1064, #1036)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.env.GetInlet(1)->SetFloat(100.f, YSE::T_GUI);
    buffer a(N);
    a = 4.f;
    rig.env.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    rig.mute.gotInt = false;
    rig.env.ResetDSP();
    rig.env.GetInlet(1)->SetBuffer(&a, YSE::T_DSP); // runs CALC()
    CHECK(rig.Flush() == 0);
    CHECK(rig.At(2) == doctest::Approx(0.75f)); // 4 ms, not 100
    CHECK(rig.At(3) == doctest::Approx(1.f));
  }

  TEST_CASE("~adsr: the arguments survive a DumpJSON / ParseJSON round trip (#1064)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_ADSR, "10 20 0.5 300 1 2.5 100 1");
      REQUIRE(a != nullptr);
      YSE::pHandle* b = src.CreateObject(YSE::OBJ::D_ADSR, "");
      REQUIRE(b != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* a = dst.GetHandleFromID(0);
    YSE::pHandle* b = dst.GetHandleFromID(1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(std::string(a->Type()) == "~adsr");
    CHECK(a->GetParams() == "10 20 0.5 300 1 2.5 100 1");
    CHECK(b->GetParams() == "");
  }

  TEST_CASE("~adsr: in a rendered patch, a gate and a number drive the envelope, its gate and "
            "its mute state, after a JSON round trip (#1064)") {
    ScopedSampleRate rate(1000);
    std::string dump;
    unsigned int sigID = 0;
    unsigned int gatedID = 0;
    unsigned int levelID = 0;
    unsigned int openID = 0;
    unsigned int muteID = 0;
    unsigned int eventID = 0;
    unsigned int eventLevelID = 0;
    {
      patcherImplementation src(1, nullptr);
      // A 256-sample attack to the gate's 0.5, sustain at half of it, a
      // 256-sample release. The snapshots report each block's last sample.
      YSE::pHandle* sig = src.CreateObject(YSE::OBJ::D_SIG, "0.5");
      YSE::pHandle* gated = src.CreateObject(YSE::OBJ::D_ADSR, "256 0 0.5 256");
      YSE::pHandle* levelSnap = src.CreateObject(YSE::OBJ::D_SNAPSHOT, "1 127");
      YSE::pHandle* openSnap = src.CreateObject(YSE::OBJ::D_SNAPSHOT, "1 127");
      YSE::pHandle* level = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* open = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* mute = src.CreateObject(YSE::OBJ::G_INT, "");
      // The same envelope with nothing on its trigger inlet: numbers drive it.
      YSE::pHandle* event = src.CreateObject(YSE::OBJ::D_ADSR, "256 0 0.5 256");
      YSE::pHandle* eventSnap = src.CreateObject(YSE::OBJ::D_SNAPSHOT, "1 127");
      YSE::pHandle* eventLevel = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      REQUIRE(sig != nullptr);
      REQUIRE(gated != nullptr);
      REQUIRE(levelSnap != nullptr);
      REQUIRE(openSnap != nullptr);
      REQUIRE(level != nullptr);
      REQUIRE(open != nullptr);
      REQUIRE(mute != nullptr);
      REQUIRE(event != nullptr);
      REQUIRE(eventSnap != nullptr);
      REQUIRE(eventLevel != nullptr);
      src.Connect(sig, 0, gated, 0);
      src.Connect(gated, 0, levelSnap, 0);
      src.Connect(gated, 1, openSnap, 0);
      src.Connect(levelSnap, 0, level, 1);
      src.Connect(openSnap, 0, open, 1);
      src.Connect(gated, 2, mute, 1);
      src.Connect(event, 0, eventSnap, 0);
      src.Connect(eventSnap, 0, eventLevel, 1);
      sigID = sig->GetID();
      gatedID = gated->GetID();
      levelID = level->GetID();
      openID = open->GetID();
      muteID = mute->GetID();
      eventID = event->GetID();
      eventLevelID = eventLevel->GetID();
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* sig = dst.GetHandleFromID(sigID);
    YSE::pHandle* gated = dst.GetHandleFromID(gatedID);
    YSE::pHandle* level = dst.GetHandleFromID(levelID);
    YSE::pHandle* open = dst.GetHandleFromID(openID);
    YSE::pHandle* mute = dst.GetHandleFromID(muteID);
    YSE::pHandle* event = dst.GetHandleFromID(eventID);
    YSE::pHandle* eventLevel = dst.GetHandleFromID(eventLevelID);
    REQUIRE(sig != nullptr);
    REQUIRE(gated != nullptr);
    REQUIRE(level != nullptr);
    REQUIRE(open != nullptr);
    REQUIRE(mute != nullptr);
    REQUIRE(event != nullptr);
    REQUIRE(eventLevel != nullptr);
    CHECK(gated->GetParams() == "256 0 0.5 256");

    mute->SetIntData(1, 7); // so that the first 0 shows
    event->SetFloatData(0, 1.f);
    const float levels[] = {0.25f, 0.5f, 0.25f};
    const float events[] = {0.5f, 1.f, 0.5f};
    for (int block = 0; block < 3; ++block) {
      CAPTURE(block);
      dst.Calculate(YSE::T_DSP);
      CHECK(GuiFloat(level) == doctest::Approx(levels[block]));
      CHECK(GuiFloat(open) == 1.f);
      CHECK(GuiInt(mute) == 0);
      CHECK(GuiFloat(eventLevel) == doctest::Approx(events[block]));
    }

    // The gate falls: a 256-sample release from 0.25.
    sig->SetFloatData(0, 0.f);
    event->SetFloatData(0, 0.f);
    dst.Calculate(YSE::T_DSP);
    CHECK(GuiFloat(level) == doctest::Approx(0.125f));
    CHECK(GuiFloat(open) == 0.f);
    CHECK(GuiInt(mute) == 0);
    CHECK(GuiFloat(eventLevel) == doctest::Approx(0.25f));
    dst.Calculate(YSE::T_DSP);
    CHECK(GuiFloat(level) == 0.f);
    CHECK(GuiInt(mute) == 1); // at rest: the voice can be muted
    CHECK(GuiFloat(eventLevel) == 0.f);
  }

  TEST_CASE("~adsr: numbers, lists, messages, blocks and the flush on the audio thread "
            "allocate nothing (#1064)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    buffer g(N);
    g = 0.f;
    for (unsigned int i = 10; i < 50; ++i)
      g.getPtr()[i] = 1.f;
    const std::string list = "3 4 0.5 6";
    const std::string legato = "legato 1";
    const std::string mode = "triggermode 1";
    rig.Block();
    {
      TestHelpers::ProbeScope probe;
      rig.env.GetInlet(0)->SetFloat(1.f, YSE::T_DSP);
      rig.env.GetInlet(0)->SetInt(0, YSE::T_DSP);
      rig.env.GetInlet(0)->SetList(list, YSE::T_DSP);
      rig.env.GetInlet(0)->SetMessage(legato, YSE::T_DSP);
      rig.env.GetInlet(1)->SetFloat(2.f, YSE::T_DSP);
      rig.env.GetInlet(2)->SetInt(3, YSE::T_DSP);
      rig.env.GetInlet(3)->SetFloat(0.4f, YSE::T_DSP);
      rig.env.GetInlet(4)->SetInt(5, YSE::T_DSP);
      rig.env.GetInlet(0)->SetFloat(1.f, YSE::T_DSP);
      rig.Block();
      rig.env.GetInlet(0)->SetMessage(mode, YSE::T_DSP);
      rig.Block(g);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(rig.mute.received == 1);
  }

} // TEST_SUITE("patcher")
