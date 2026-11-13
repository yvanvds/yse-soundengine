// Tests for subpatch DSP enable, issue #1137: ~mute switches a subpatcher's
// signal processing off and on, ~pass forwards its input (silence while
// muted), ~begin is a constant 0. There is no DSP class (a graph feature, the
// two-layer rule is waived), so everything is shown in rendered patches built
// the way a patch author builds them: objects placed in a `patcher` object
// with SetContainer, a [~mute] cord drawn to one of its inlets by pin number,
// and the ~dac output read after patcherImplementation::Calculate.
//
// What muting must do, each with a control:
//  - a muted subpatcher is silent, and its siblings and the objects outside
//    it that mix it in keep playing;
//  - muted objects do not compute (a ~phasor resumes exactly where it was);
//  - an int mutes the direct contents only, a list <mute> 1 nested
//    subpatchers too, as separate bits;
//  - nothing travels down the ~mute cord into the subpatcher;
//  - an object moved into a muted subpatcher is muted from the next block;
//  - the state is not saved, messages and blocks allocate nothing, and a
//    control thread can mute while the audio thread renders.

#include <doctest/doctest.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/patcherInsert.hpp"
#include "headers/constants.hpp"
#include "patcher/genericObjects/gSubpatcher.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "support/alloc_probe.hpp"

using YSE::STANDARD_BUFFERSIZE;
using YSE::PATCHER::patcherImplementation;

namespace {

  bool AllEqual(YSE::DSP::buffer& b, float value) {
    const float* p = b.getPtr();
    for (UInt i = 0; i < b.getLength(); ++i)
      if (p[i] != value) return false;
    return true;
  }

  // One block through a YSE::patcher, the way a host renders it; returns the
  // first sample of channel 0 and leaves the block in `io`.
  float Render(YSE::DSP::patcherInsert& insert, MULTICHANNELBUFFER& io) {
    io[0] = 0.f;
    insert.process(io);
    return io[0].getPtr()[0];
  }

  MULTICHANNELBUFFER MonoBuffer() {
    MULTICHANNELBUFFER io;
    io.resize(1);
    io[0].resize(STANDARD_BUFFERSIZE);
    return io;
  }

  bool Listed(const char* name) {
    for (const auto& n : YSE::PATCHER::Register().AllNames())
      if (n == name) return true;
    return false;
  }

  // A subpatcher holding [~sig value] -> [~outlet 0], with a [.inlet 0] so a
  // [~mute] has a pin to connect to. Its outlet 0 goes to `dac` inlet 0.
  struct voice {
    YSE::pHandle* sub;
    YSE::pHandle* sig;
    YSE::pHandle* in;
  };

  voice BuildVoice(patcherImplementation& p, float value, YSE::pHandle* dac,
                   YSE::pHandle* container = nullptr) {
    voice v{};
    v.sub = p.CreateObject(YSE::OBJ::PATCHER, "voice");
    v.sig = p.CreateObject(YSE::OBJ::D_SIG, std::to_string(value));
    v.in = p.CreateObject(YSE::OBJ::G_INLET, "0");
    YSE::pHandle* out = p.CreateObject(YSE::OBJ::D_OUTLET, "0");
    REQUIRE(v.sub != nullptr);
    if (container != nullptr) p.SetObjectContainer(v.sub, container);
    p.SetObjectContainer(v.sig, v.sub);
    p.SetObjectContainer(v.in, v.sub);
    p.SetObjectContainer(out, v.sub);
    p.Connect(v.sig, 0, out, 0);
    if (dac != nullptr) p.Connect(v.sub, 0, dac, 0);
    return v;
  }

} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("subpatch mute: registry, category and shape (#1137)") {
    for (const char* name : {YSE::OBJ::D_MUTE, YSE::OBJ::D_PASS, YSE::OBJ::D_BEGIN}) {
      CAPTURE(name);
      CHECK(Listed(name));
    }
    CHECK(std::string(YSE::OBJ::D_MUTE) == "~mute");
    CHECK(std::string(YSE::OBJ::D_PASS) == "~pass");
    CHECK(std::string(YSE::OBJ::D_BEGIN) == "~begin");

    patcherImplementation p(1, nullptr);
    YSE::pHandle* mute = p.CreateObject(YSE::OBJ::D_MUTE, "");
    YSE::pHandle* pass = p.CreateObject(YSE::OBJ::D_PASS, "");
    YSE::pHandle* begin = p.CreateObject(YSE::OBJ::D_BEGIN, "");
    REQUIRE(mute != nullptr);
    REQUIRE(pass != nullptr);
    REQUIRE(begin != nullptr);
    CHECK(mute->GetInputs() == 1);
    CHECK_FALSE(mute->IsDSPInput(0));
    CHECK(mute->GetOutputs() == 1);
    CHECK(mute->OutputDataType(0) != YSE::OUT_TYPE::BUFFER);
    CHECK(pass->GetInputs() == 1);
    CHECK(pass->IsDSPInput(0));
    CHECK(pass->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(begin->GetInputs() == 0);
    CHECK(begin->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    for (YSE::pHandle* h : {mute, pass, begin}) {
      CHECK(p.GetObjectFromID(h->GetID())->GetCategory() == YSE::PATCHER::pCategory::ENCAPSULATION);
    }
  }

  TEST_CASE("~mute: a muted subpatcher is silent, its sibling keeps playing (#1137)") {
    // Two voices into one ~dac inlet, the fan-in a mixer is. [.m 1] / [.m 0]
    // -> [~mute] -> voice A's inlet. Muting A must not stop the ~dac: B is
    // still heard.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    voice a = BuildVoice(p, 0.5f, dac);
    voice b = BuildVoice(p, 0.25f, dac);
    (void)b;
    YSE::pHandle* mute = p.CreateObject(YSE::OBJ::D_MUTE, "");
    YSE::pHandle* on = p.CreateObject(YSE::OBJ::G_MESSAGE, "1");
    YSE::pHandle* off = p.CreateObject(YSE::OBJ::G_MESSAGE, "0");
    p.Connect(on, 0, mute, 0);
    p.Connect(off, 0, mute, 0);
    p.Connect(mute, 0, a.sub, 0);

    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.75f));

    on->SetBang(0);
    for (int i = 0; i < 3; ++i) {
      p.Calculate(YSE::T_DSP);
      CHECK(AllEqual(p.output[0], 0.25f));
    }

    off->SetBang(0);
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.75f));
  }

  TEST_CASE("~mute: muted objects do not compute and resume where they were (#1137)") {
    // [~phasor 100] in a subpatcher -> ~dac, and the same ramp rendered
    // without interruption in a second patcher. The muted ramp does not
    // advance, so its first block after three muted ones is the reference's
    // second block.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* sub = p.CreateObject(YSE::OBJ::PATCHER, "");
    YSE::pHandle* ramp = p.CreateObject(YSE::OBJ::D_PHASOR, "100");
    YSE::pHandle* out = p.CreateObject(YSE::OBJ::D_OUTLET, "0");
    YSE::pHandle* in = p.CreateObject(YSE::OBJ::D_INLET, "0");
    p.SetObjectContainer(ramp, sub);
    p.SetObjectContainer(out, sub);
    p.SetObjectContainer(in, sub);
    p.Connect(ramp, 0, out, 0);
    p.Connect(sub, 0, dac, 0);
    YSE::pHandle* mute = p.CreateObject(YSE::OBJ::D_MUTE, "");
    // A signal inlet is a pin like any other for ~mute.
    p.Connect(mute, 0, sub, 0);

    patcherImplementation ref(1, nullptr);
    YSE::pHandle* refRamp = ref.CreateObject(YSE::OBJ::D_PHASOR, "100");
    ref.Connect(refRamp, 0, ref.CreateObject(YSE::OBJ::D_DAC, ""), 0);

    p.Calculate(YSE::T_DSP);
    ref.Calculate(YSE::T_DSP);
    CHECK_FALSE(AllEqual(p.output[0], 0.f));

    mute->SetIntData(0, 1);
    for (int i = 0; i < 3; ++i) {
      p.Calculate(YSE::T_DSP);
      CHECK(AllEqual(p.output[0], 0.f));
    }
    mute->SetIntData(0, 0);
    p.Calculate(YSE::T_DSP);
    ref.Calculate(YSE::T_DSP);
    for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
      CAPTURE(i);
      CHECK(p.output[0].getPtr()[i] == ref.output[0].getPtr()[i]);
    }
  }

  TEST_CASE("~mute: an int mutes the direct contents, a list nested ones too (#1137)") {
    // outer: [~sig 0.5] -> [~dac 1]; inner (inside outer): [~sig 0.25] ->
    // [~dac 2]. Each subpatcher reaches the output on its own channel, so
    // what each one does is seen separately.
    patcherImplementation p(2, nullptr);
    YSE::pHandle* outer = p.CreateObject(YSE::OBJ::PATCHER, "outer");
    YSE::pHandle* outerIn = p.CreateObject(YSE::OBJ::G_INLET, "0");
    YSE::pHandle* outerSig = p.CreateObject(YSE::OBJ::D_SIG, "0.5");
    YSE::pHandle* outerDac = p.CreateObject(YSE::OBJ::D_DAC, "1");
    YSE::pHandle* inner = p.CreateObject(YSE::OBJ::PATCHER, "inner");
    YSE::pHandle* innerIn = p.CreateObject(YSE::OBJ::G_INLET, "0");
    YSE::pHandle* innerSig = p.CreateObject(YSE::OBJ::D_SIG, "0.25");
    YSE::pHandle* innerDac = p.CreateObject(YSE::OBJ::D_DAC, "2");
    for (YSE::pHandle* h : {outerIn, outerSig, outerDac, inner})
      p.SetObjectContainer(h, outer);
    for (YSE::pHandle* h : {innerIn, innerSig, innerDac})
      p.SetObjectContainer(h, inner);
    p.Connect(outerSig, 0, outerDac, 0);
    p.Connect(innerSig, 0, innerDac, 0);
    YSE::pHandle* mute = p.CreateObject(YSE::OBJ::D_MUTE, "");
    YSE::pHandle* innerMute = p.CreateObject(YSE::OBJ::D_MUTE, "");
    p.SetObjectContainer(innerMute, outer);
    p.Connect(mute, 0, outer, 0);
    p.Connect(innerMute, 0, inner, 0);

    const auto heard = [&p](float first, float second) {
      p.Calculate(YSE::T_DSP);
      CHECK(AllEqual(p.output[0], first));
      CHECK(AllEqual(p.output[1], second));
    };
    heard(0.5f, 0.25f);

    mute->SetIntData(0, 1); // the outer contents only
    heard(0.f, 0.25f);
    mute->SetListData(0, "1 1"); // and everything nested
    heard(0.f, 0.f);
    mute->SetIntData(0, 0); // the contents back; nested still muted
    heard(0.5f, 0.f);
    mute->SetListData(0, "0 1"); // nested back
    heard(0.5f, 0.25f);

    // A nested subpatcher's own mute is its own: muting it leaves the outer
    // one playing, and the outer "0 1" does not clear it.
    innerMute->SetFloatData(0, 1.f);
    heard(0.5f, 0.f);
    mute->SetListData(0, "0 1");
    heard(0.5f, 0.f);
    innerMute->SetFloatData(0, 0.f);
    heard(0.5f, 0.25f);

    // A one-number list is the int; a word is ignored.
    mute->SetListData(0, "1");
    heard(0.f, 0.25f);
    mute->SetListData(0, "mute 0");
    heard(0.f, 0.25f);
    mute->SetListData(0, "0.0 0");
    heard(0.5f, 0.25f);
  }

  TEST_CASE("~mute: nothing travels down the cord into the subpatcher (#1137)") {
    // [.inlet 0] -> [~sig 0.5]'s value inlet: a number arriving there would
    // change the level. The mute must not be delivered as one.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    voice v = BuildVoice(p, 0.5f, dac);
    p.Connect(v.in, 0, v.sig, 0);
    YSE::pHandle* mute = p.CreateObject(YSE::OBJ::D_MUTE, "");
    p.Connect(mute, 0, v.sub, 0);
    mute->SetIntData(0, 1);
    mute->SetIntData(0, 0);
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.5f));

    // Control: the same pin does carry a number sent to it directly.
    YSE::pHandle* number = p.CreateObject(YSE::OBJ::G_MESSAGE, "0.125");
    p.Connect(number, 0, v.sub, 0);
    number->SetBang(0);
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.125f));

    // A ~mute connected to something that is not a subpatcher inlet does
    // nothing at all.
    YSE::pHandle* stray = p.CreateObject(YSE::OBJ::D_MUTE, "");
    p.Connect(stray, 0, v.sig, 0);
    stray->SetIntData(0, 1);
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.125f));
  }

  TEST_CASE("~mute: a ~dac inside a muted subpatcher adds nothing (#1137)") {
    // [~sig 0.5] at the top level drawn straight to a ~dac inside the
    // subpatcher, across the boundary: muting the subpatcher silences it.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* sub = p.CreateObject(YSE::OBJ::PATCHER, "");
    YSE::pHandle* in = p.CreateObject(YSE::OBJ::G_INLET, "0");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* sig = p.CreateObject(YSE::OBJ::D_SIG, "0.5");
    p.SetObjectContainer(in, sub);
    p.SetObjectContainer(dac, sub);
    p.Connect(sig, 0, dac, 0);
    YSE::pHandle* mute = p.CreateObject(YSE::OBJ::D_MUTE, "");
    p.Connect(mute, 0, sub, 0);
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.5f));
    mute->SetIntData(0, 1);
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.f));
  }

  TEST_CASE("~mute: an object moved into a muted subpatcher is muted (#1137)") {
    // SetContainer after the mute: the move publishes, so the next block
    // already knows the object is inside.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    voice v = BuildVoice(p, 0.f, nullptr);
    YSE::pHandle* sig = p.CreateObject(YSE::OBJ::D_SIG, "0.5");
    p.Connect(sig, 0, dac, 0);
    YSE::pHandle* mute = p.CreateObject(YSE::OBJ::D_MUTE, "");
    p.Connect(mute, 0, v.sub, 0);
    mute->SetIntData(0, 1);
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.5f));

    p.SetObjectContainer(sig, v.sub);
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.f));

    p.SetObjectContainer(sig, nullptr);
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.5f));
  }

  TEST_CASE("~pass: passes its input, and silence while muted (#1137)") {
    // [~sig 0.5] -> [~pass] -> [~outlet] inside a subpatcher.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* sub = p.CreateObject(YSE::OBJ::PATCHER, "");
    YSE::pHandle* in = p.CreateObject(YSE::OBJ::G_INLET, "0");
    YSE::pHandle* sig = p.CreateObject(YSE::OBJ::D_SIG, "0.5");
    YSE::pHandle* pass = p.CreateObject(YSE::OBJ::D_PASS, "");
    YSE::pHandle* out = p.CreateObject(YSE::OBJ::D_OUTLET, "0");
    for (YSE::pHandle* h : {in, sig, pass, out})
      p.SetObjectContainer(h, sub);
    p.Connect(sig, 0, pass, 0);
    p.Connect(pass, 0, out, 0);
    p.Connect(sub, 0, dac, 0);
    YSE::pHandle* mute = p.CreateObject(YSE::OBJ::D_MUTE, "");
    p.Connect(mute, 0, sub, 0);

    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.5f));
    mute->SetIntData(0, 1);
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.f));
    mute->SetIntData(0, 0);
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.5f));

    // Unconnected, it reads 0.
    YSE::pHandle* lone = p.CreateObject(YSE::OBJ::D_PASS, "");
    p.Connect(lone, 0, dac, 0);
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.5f));
  }

  TEST_CASE("~begin: a constant 0 that adds nothing (#1137)") {
    // [~begin] and [~sig 0.25] into one ~dac inlet: 0.25. [~begin] alone:
    // silence.
    patcherImplementation p(2, nullptr);
    YSE::pHandle* begin = p.CreateObject(YSE::OBJ::D_BEGIN, "");
    YSE::pHandle* sig = p.CreateObject(YSE::OBJ::D_SIG, "0.25");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(begin, 0, dac, 0);
    p.Connect(sig, 0, dac, 0);
    p.Connect(begin, 0, dac, 1);
    for (int i = 0; i < 2; ++i) {
      p.Calculate(YSE::T_DSP);
      CHECK(AllEqual(p.output[0], 0.25f));
      CHECK(AllEqual(p.output[1], 0.f));
    }
  }

  TEST_CASE("~mute: saved and loaded, the mute state is not (#1137)") {
    YSE::patcher src;
    src.create(1);
    YSE::pHandle* sub = src.CreateObject(YSE::OBJ::PATCHER, "voice");
    YSE::pHandle* in = src.CreateObject(YSE::OBJ::G_INLET, "0");
    YSE::pHandle* begin = src.CreateObject(YSE::OBJ::D_BEGIN, "");
    YSE::pHandle* sig = src.CreateObject(YSE::OBJ::D_SIG, "0.5");
    YSE::pHandle* pass = src.CreateObject(YSE::OBJ::D_PASS, "");
    YSE::pHandle* out = src.CreateObject(YSE::OBJ::D_OUTLET, "0");
    YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* mute = src.CreateObject(YSE::OBJ::D_MUTE, "");
    for (YSE::pHandle* h : {in, begin, sig, pass, out})
      src.SetContainer(h, sub);
    src.Connect(begin, 0, pass, 0);
    src.Connect(sig, 0, pass, 0);
    src.Connect(pass, 0, out, 0);
    src.Connect(sub, 0, dac, 0);
    src.Connect(mute, 0, sub, 0);
    mute->SetIntData(0, 1);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(1);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 8);
    YSE::pHandle* lmute = nullptr;
    for (unsigned int i = 0; i < loaded.Objects(); ++i) {
      YSE::pHandle* h = loaded.GetHandleFromList(i);
      if (std::string(h->Type()) == "~mute") lmute = h;
      if (std::string(h->Type()) == "~pass" || std::string(h->Type()) == "~begin") {
        CHECK(loaded.GetContainer(h) != nullptr);
      }
    }
    REQUIRE(lmute != nullptr);
    if (lmute == nullptr) return; // REQUIRE throws; this tells the analyzer
    CHECK(lmute->GetConnections(0) == 1);

    // Loaded unmuted, and the reloaded cord still mutes.
    YSE::DSP::patcherInsert insert(loaded);
    MULTICHANNELBUFFER io = MonoBuffer();
    Render(insert, io);
    CHECK(AllEqual(io[0], 0.5f));
    lmute->SetIntData(0, 1);
    Render(insert, io);
    CHECK(AllEqual(io[0], 0.f));
  }

  TEST_CASE("~mute: messages and muted blocks allocate nothing on the audio thread (#1137)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    voice a = BuildVoice(p, 0.5f, dac);
    voice b = BuildVoice(p, 0.25f, dac, a.sub);
    YSE::pHandle* mute = p.CreateObject(YSE::OBJ::D_MUTE, "");
    p.Connect(mute, 0, a.sub, 0);
    p.Connect(mute, 0, b.sub, 0);
    YSE::PATCHER::pObject* muteObj = p.GetObjectFromID(mute->GetID());
    p.Calculate(YSE::T_DSP); // anything lazily sized is sized now

    const std::vector<std::string> messages = {"1 1", "0 1", "1", "0", "1 0", "bang"};
    {
      TestHelpers::ProbeScope probe;
      for (const std::string& m : messages) {
        muteObj->GetInlet(0)->SetMessage(m, YSE::T_DSP);
        p.Calculate(YSE::T_DSP);
        muteObj->GetInlet(0)->SetList(m, YSE::T_DSP);
        p.Calculate(YSE::T_DSP);
      }
      muteObj->GetInlet(0)->SetInt(1, YSE::T_DSP);
      p.Calculate(YSE::T_DSP);
      muteObj->GetInlet(0)->SetFloat(0.f, YSE::T_DSP);
      p.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    // All unmuted again: voice a, and voice b nested in it.
    CHECK(AllEqual(p.output[0], 0.75f));
  }

  TEST_CASE("~mute: a control thread mutes while the audio thread renders (#1137)") {
    // Under TSan a plain (non-atomic) mute word shows up here.
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* sub = p.CreateObject(YSE::OBJ::PATCHER, "");
    YSE::pHandle* in = p.CreateObject(YSE::OBJ::G_INLET, "0");
    YSE::pHandle* sig = p.CreateObject(YSE::OBJ::D_SIG, "0.5");
    YSE::pHandle* out = p.CreateObject(YSE::OBJ::D_OUTLET, "0");
    for (YSE::pHandle* h : {in, sig, out})
      p.SetContainer(h, sub);
    p.Connect(sig, 0, out, 0);
    p.Connect(sub, 0, dac, 0);
    YSE::pHandle* mute = p.CreateObject(YSE::OBJ::D_MUTE, "");
    p.Connect(mute, 0, sub, 0);
    YSE::DSP::patcherInsert insert(p);
    MULTICHANNELBUFFER io = MonoBuffer();

    std::atomic<bool> done{false};
    std::thread control([&] {
      int i = 0;
      while (!done.load()) {
        if ((i % 3) == 0) {
          mute->SetListData(0, (i % 2) != 0 ? "1 1" : "0 1");
        } else {
          mute->SetIntData(0, i % 2);
        }
        ++i;
      }
    });
    for (int block = 0; block < 2000; ++block) {
      const float v = Render(insert, io);
      CHECK((v == 0.f || v == 0.5f));
    }
    done.store(true);
    control.join();
    mute->SetIntData(0, 0);
    Render(insert, io);
    CHECK(AllEqual(io[0], 0.5f));
  }

} // TEST_SUITE("patcher")
