// ~sig (patcher/generatorObjects/dSig), MSP's sig~, issue #1062. The sample
// work is a DSP::buffer fill, tested in Tests/dsp/test_buffer.cpp; these show
// the sig~ surface reaches it:
//
//   - one outlet per creation argument (one with none), each a constant
//     signal starting at its argument (default 0);
//   - a float or int sets the leftmost outlet, a list the outlets from the
//     leftmost on, "set <index> <value>" one outlet;
//   - a signal on the inlet is ignored, and only orders the graph;
//   - the values are atomics a control thread can write while the audio
//     thread calculates (the concurrency case is a TSan gate).

#include <doctest/doctest.h>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/generatorObjects/dSig.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::PATCHER::dSig;
using YSE::PATCHER::patcherImplementation;

namespace {
  void Wire(YSE::PATCHER::pObject& sig, int outlet, BufferSink& sink) {
    sig.ConnectOutlet(sink.GetInlet(0), outlet);
    sink.ConnectInlet(sig.GetOutlet(outlet), 0);
  }

  bool AllEqual(YSE::DSP::buffer* b, float expected) {
    if (b == nullptr) return false;
    for (unsigned int i = 0; i < b->getLength(); ++i)
      if (b->getPtr()[i] != doctest::Approx(expected)) return false;
    return true;
  }

  bool AllEqual(YSE::DSP::buffer& b, float expected) {
    return AllEqual(&b, expected);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~sig: registry and shape (#1062)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* bare = p.CreateObject(YSE::OBJ::D_SIG);
    REQUIRE(bare != nullptr);
    CHECK(bare->GetInputs() == 1);
    CHECK(bare->GetOutputs() == 1);
    CHECK(bare->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    YSE::pHandle* three = p.CreateObject(YSE::OBJ::D_SIG, "1 2 3");
    REQUIRE(three != nullptr);
    CHECK(three->GetInputs() == 1);
    CHECK(three->GetOutputs() == 3);
    for (unsigned int i = 0; i < 3; ++i)
      CHECK(three->OutputDataType(i) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::D_SIG));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::OSC);
    CHECK(obj->IsDSPObject());
  }

  TEST_CASE("~sig: outputs 0 bare and its argument otherwise (#1062)") {
    dSig bare;
    BufferSink s0;
    Wire(bare, 0, s0);
    bare.Calculate(YSE::T_DSP);
    CHECK(AllEqual(s0.received, 0.f));

    dSig withArgs;
    withArgs.SetParams("0.25 -3 7");
    REQUIRE(withArgs.OutletCount() == 3);
    BufferSink a, b, c;
    Wire(withArgs, 0, a);
    Wire(withArgs, 1, b);
    Wire(withArgs, 2, c);
    withArgs.Calculate(YSE::T_DSP);
    CHECK(AllEqual(a.received, 0.25f));
    CHECK(AllEqual(b.received, -3.f));
    CHECK(AllEqual(c.received, 7.f));
  }

  TEST_CASE("~sig: a float or int sets the leftmost outlet (#1062)") {
    dSig sig;
    sig.SetParams("1 2");
    BufferSink a, b;
    Wire(sig, 0, a);
    Wire(sig, 1, b);

    sig.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    sig.Calculate(YSE::T_DSP);
    CHECK(AllEqual(a.received, 0.5f));
    CHECK(AllEqual(b.received, 2.f));

    sig.GetInlet(0)->SetInt(-4, YSE::T_GUI);
    sig.Calculate(YSE::T_DSP);
    CHECK(AllEqual(a.received, -4.f));
    CHECK(AllEqual(b.received, 2.f));

    // The value holds across blocks until changed.
    sig.Calculate(YSE::T_DSP);
    CHECK(AllEqual(a.received, -4.f));
  }

  TEST_CASE("~sig: a list sets the outlets by position, 'set' sets one (#1062)") {
    dSig sig;
    sig.SetParams("0 0 0");
    BufferSink a, b, c;
    Wire(sig, 0, a);
    Wire(sig, 1, b);
    Wire(sig, 2, c);

    sig.GetInlet(0)->SetList("1.5 2.5", YSE::T_GUI);
    sig.Calculate(YSE::T_DSP);
    CHECK(AllEqual(a.received, 1.5f));
    CHECK(AllEqual(b.received, 2.5f));
    CHECK(AllEqual(c.received, 0.f));

    // Numbers past the last outlet are ignored.
    sig.GetInlet(0)->SetList("4 5 6 7 8", YSE::T_GUI);
    sig.Calculate(YSE::T_DSP);
    CHECK(AllEqual(a.received, 4.f));
    CHECK(AllEqual(b.received, 5.f));
    CHECK(AllEqual(c.received, 6.f));

    // "set <index> <value>", as a message box would deliver it.
    sig.GetInlet(0)->SetMessage("set 2 9", YSE::T_GUI);
    sig.Calculate(YSE::T_DSP);
    CHECK(AllEqual(a.received, 4.f));
    CHECK(AllEqual(b.received, 5.f));
    CHECK(AllEqual(c.received, 9.f));
    sig.GetInlet(0)->SetMessage("set 0 -1", YSE::T_GUI);
    sig.Calculate(YSE::T_DSP);
    CHECK(AllEqual(a.received, -1.f));

    // An index out of range, fractional or missing changes nothing.
    for (const char* bad : {"set 3 100", "set -1 100", "set 1.5 100", "set 1", "set"}) {
      CAPTURE(bad);
      sig.GetInlet(0)->SetMessage(bad, YSE::T_GUI);
      sig.Calculate(YSE::T_DSP);
      CHECK(AllEqual(a.received, -1.f));
      CHECK(AllEqual(b.received, 5.f));
      CHECK(AllEqual(c.received, 9.f));
    }
  }

  TEST_CASE("~sig: a connected signal is ignored and only orders the graph (#1062)") {
    dSig sig;
    sig.SetParams("3");
    BufferSink sink;
    Wire(sig, 0, sink);
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    in = 100.f;
    sig.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
    sig.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 3.f));

    // In a patch: ~phasor -> ~sig -> ~dac renders ~sig's value, not the ramp.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* ramp = p.CreateObject(YSE::OBJ::D_PHASOR, "100");
    YSE::pHandle* s = p.CreateObject(YSE::OBJ::D_SIG, "0.75");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(ramp, 0, s, 0);
    p.Connect(s, 0, dac, 0);
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.75f));
  }

  TEST_CASE("~sig: in a patch, feeds a signal inlet and follows numbers (#1062)") {
    patcherImplementation p(3, nullptr);
    YSE::pHandle* s = p.CreateObject(YSE::OBJ::D_SIG, "0.5 2");
    YSE::pHandle* mul = p.CreateObject(YSE::OBJ::D_MULTIPLY, "100"); // the 100 is overridden
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(s, 0, mul, 0);
    p.Connect(s, 1, mul, 1);
    p.Connect(mul, 0, dac, 0);
    p.Connect(s, 0, dac, 1);
    p.Connect(s, 1, dac, 2);
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 1.f));
    CHECK(AllEqual(p.output[1], 0.5f));
    CHECK(AllEqual(p.output[2], 2.f));

    s->SetFloatData(0, 3.f);
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 6.f));
    CHECK(AllEqual(p.output[1], 3.f));

    s->SetListData(0, "set 1 -1");
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], -3.f));
    CHECK(AllEqual(p.output[2], -1.f));
  }

  TEST_CASE("~sig: the arguments survive a DumpJSON / ParseJSON round trip (#1062)") {
    std::string dump;
    {
      patcherImplementation src(3, nullptr);
      YSE::pHandle* s = src.CreateObject(YSE::OBJ::D_SIG, "1.5 -2 0.125");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      for (int i = 0; i < 3; ++i)
        src.Connect(s, i, dac, i);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(3, nullptr);
    dst.ParseJSON(dump);
    bool found = false;
    for (unsigned int i = 0; i < dst.Objects(); ++i) {
      YSE::pHandle* h = dst.GetHandleFromList(i);
      if (std::string(h->Type()) == YSE::OBJ::D_SIG) {
        found = true;
        CHECK(h->GetOutputs() == 3);
      }
    }
    CHECK(found);
    dst.Calculate(YSE::T_DSP);
    CHECK(AllEqual(dst.output[0], 1.5f));
    CHECK(AllEqual(dst.output[1], -2.f));
    CHECK(AllEqual(dst.output[2], 0.125f));
  }

  TEST_CASE("~sig: numbers, lists and blocks on the audio thread allocate nothing (#1062)") {
    dSig sig;
    sig.SetParams("0 0");
    BufferSink a, b;
    Wire(sig, 0, a);
    Wire(sig, 1, b);
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    in = 0.5f;
    const std::string list = "0.25 0.75";
    const std::string set = "set 1 0.5";
    sig.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      sig.GetInlet(0)->SetFloat(0.3f, YSE::T_DSP);
      sig.GetInlet(0)->SetInt(2, YSE::T_DSP);
      sig.GetInlet(0)->SetList(list, YSE::T_DSP);
      sig.GetInlet(0)->SetList(set, YSE::T_DSP);
      sig.Calculate(YSE::T_DSP);
      sig.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      sig.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(AllEqual(a.received, 0.25f));
    CHECK(AllEqual(b.received, 0.5f));
  }

  TEST_CASE("~sig: a control thread sets the values while the audio thread calculates (#1062)") {
    // Each block must come out whole, from one value; a TSan build flags a
    // non-atomic handoff.
    dSig sig;
    sig.SetParams("1 1");
    BufferSink a, b;
    Wire(sig, 0, a);
    Wire(sig, 1, b);

    std::atomic<bool> stop{false};
    std::thread control([&] {
      int n = 0;
      while (!stop.load()) {
        const bool odd = (n++ % 2) != 0;
        sig.GetInlet(0)->SetFloat(odd ? 1.f : 2.f, YSE::T_GUI);
        sig.GetInlet(0)->SetList(odd ? "set 1 1" : "set 1 2", YSE::T_GUI);
      }
    });

    bool whole = true;
    for (int block = 0; block < 2000; ++block) {
      sig.Calculate(YSE::T_DSP);
      const float va = a.received->getPtr()[0];
      const float vb = b.received->getPtr()[0];
      if (!(va == 1.f || va == 2.f) || !AllEqual(a.received, va)) whole = false;
      if (!(vb == 1.f || vb == 2.f) || !AllEqual(b.received, vb)) whole = false;
    }
    stop.store(true);
    control.join();
    CHECK(whole);
  }

} // TEST_SUITE("patcher")
