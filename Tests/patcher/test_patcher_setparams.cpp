// Regression tests for RT-safe live SetParams (issue #234).
//
// Before this change, pHandle::SetParams re-parsed a live object's params in
// place on the control thread — writing scalar fields through void* and
// letting PARM_PARSE grow/shrink the very inputs/outputs vectors the audio
// thread indexes while rendering the pinned snapshot (a reallocation UAF).
//
// Now (see docs/design/patcher_live_params.md):
//  - scalar params are pre-parsed into a POD plan and applied by the audio
//    thread at the top of the next Calculate (deferred, allocation-free);
//  - pin-count / string re-parses build a replacement object off the live
//    path and publish it with the usual GraphState swap, preserving handle
//    identity, storage ID, GUI properties, and surviving connections.
//
// These tests drive patcherImplementation directly (Calculate is the
// audio-thread entry point). No audio device required.

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <climits>
#include <string>
#include <thread>
#include <vector>
#include "patcher/parameters.h"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "sinks.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::patcherImplementation;

TEST_SUITE("patcher") {

  // ---- Scalar path: deferred apply on the audio thread ----

  TEST_CASE("setparams: scalar params defer to the next Calculate") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* f = p.CreateObject(YSE::OBJ::G_FLOAT, "1.5");
    REQUIRE(f != nullptr);
    CHECK(std::stof(f->GetGuiValue()) == doctest::Approx(1.5f));

    // The stored param string updates eagerly (GetParams/DumpJSON coherence),
    // but the live field must not move until the audio thread applies it.
    f->SetParams("42.5");
    CHECK(f->GetParams() == "42.5");
    CHECK(std::stof(f->GetGuiValue()) == doctest::Approx(1.5f));

    p.Calculate(YSE::T_DSP);
    CHECK(std::stof(f->GetGuiValue()) == doctest::Approx(42.5f));
  }

  TEST_CASE("setparams: a scalar re-parse does not republish or retire anything") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* sine = p.CreateObject(YSE::OBJ::D_SINE, "440");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(sine, 0, dac, 0);
    p.Calculate(YSE::T_DSP);
    REQUIRE_FALSE(p.output[0].isSilent());

    // Scalar params ride the queue; no GraphState is rebuilt and no object is
    // replaced, so the retire lists must not grow — that is the observable
    // difference from the structural path (and it is why in-flight DSP state
    // survives a frequency tweak).
    const std::size_t retiredBefore = p.PendingRetired();
    const unsigned int idBefore = sine->GetID();
    sine->SetParams("880");
    CHECK(p.PendingRetired() == retiredBefore);
    CHECK(sine->GetID() == idBefore);
    CHECK(sine->GetParams() == "880");

    p.Calculate(YSE::T_DSP);
    CHECK_FALSE(p.output[0].isSilent());
  }

  TEST_CASE("setparams: a queued scalar plan for a deleted object is dropped safely") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* f = p.CreateObject(YSE::OBJ::G_FLOAT, "0");
    REQUIRE(f != nullptr);

    // Enqueue, then delete before the audio thread drains. The plan's target
    // is absent from the pinned snapshot, so the ops must be dropped without
    // touching the retired object (an ASan build trips otherwise).
    f->SetParams("7");
    p.DeleteObject(f);
    p.Calculate(YSE::T_DSP);
    p.Calculate(YSE::T_DSP);
  }

  TEST_CASE("setparams: empty args are a no-op on the scalar path") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* f = p.CreateObject(YSE::OBJ::G_FLOAT, "3");
    f->SetParams("");
    p.Calculate(YSE::T_DSP);
    CHECK(f->GetParams() == "3");
    CHECK(std::stof(f->GetGuiValue()) == doctest::Approx(3.f));
  }

  // ---- Structural path: replacement object + swap ----

  TEST_CASE("setparams: gGate pin growth keeps identity and surviving connections") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* gate = p.CreateObject(YSE::OBJ::G_GATE, "2");
    REQUIRE(gate != nullptr);
    REQUIRE(gate->GetOutputs() == 2);

    MultiSink sinkA, sinkB;
    YSE::pHandle hA(&sinkA), hB(&sinkB);
    p.Connect(gate, 0, &hA, 0);
    p.Connect(gate, 1, &hB, 0);

    const unsigned int idBefore = gate->GetID();
    gate->SetParams("4");

    // The re-parse is structural: visible immediately, same handle, same
    // storage ID, params updated, both wired outlets preserved.
    CHECK(gate->GetOutputs() == 4);
    CHECK(gate->GetID() == idBefore);
    CHECK(gate->GetParams() == "4");
    CHECK(gate->GetConnections(0) == 1);
    CHECK(gate->GetConnections(1) == 1);

    // The replacement routes like a freshly created gGate: select outlet 1,
    // send a value, and the preserved edge must deliver it.
    gate->SetIntData(0, 1);
    gate->SetIntData(1, 99);
    CHECK(sinkA.gotInt);
    CHECK(sinkA.intValue == 99);
    CHECK_FALSE(sinkB.gotInt);

    gate->SetIntData(0, 2);
    gate->SetIntData(1, 55);
    CHECK(sinkB.gotInt);
    CHECK(sinkB.intValue == 55);
  }

  TEST_CASE("setparams: gGate pin shrink drops the removed outlets' edges") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* gate = p.CreateObject(YSE::OBJ::G_GATE, "4");
    REQUIRE(gate->GetOutputs() == 4);

    MultiSink sinkKept, sinkDropped;
    YSE::pHandle hKept(&sinkKept), hDropped(&sinkDropped);
    p.Connect(gate, 0, &hKept, 0);
    p.Connect(gate, 3, &hDropped, 0);

    gate->SetParams("2");
    CHECK(gate->GetOutputs() == 2);
    CHECK(gate->GetConnections(0) == 1);

    gate->SetIntData(0, 1);
    gate->SetIntData(1, 12);
    CHECK(sinkKept.gotInt);
    CHECK_FALSE(sinkDropped.gotInt);
    p.Calculate(YSE::T_DSP);
  }

  // Regression for issue #737, on the route that actually reaches it: an
  // editor or binding that walked 0..GetOutputs() before the re-parse holds
  // outlet numbers the object no longer has. Asking about them used to index
  // outputs[] past the end (a heap-buffer-overflow under ASan); it now answers.
  TEST_CASE("setparams: an outlet number cached across a gGate shrink is answered, not read "
            "(issue #737)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* gate = p.CreateObject(YSE::OBJ::G_GATE, "4");
    REQUIRE(gate->GetOutputs() == 4);

    MultiSink sinkKept, sinkDropped;
    YSE::pHandle hKept(&sinkKept), hDropped(&sinkDropped);
    p.Connect(gate, 0, &hKept, 0);
    p.Connect(gate, 3, &hDropped, 0);
    REQUIRE(gate->GetConnections(3) == 1);

    gate->SetParams("2");
    REQUIRE(gate->GetOutputs() == 2);

    // Outlet 3 was real a moment ago and is gone now.
    CHECK(gate->GetConnections(3) == 0u);
    CHECK(gate->GetConnectionTarget(3, 0) == UINT_MAX);
    CHECK(gate->GetConnectionTargetInlet(3, 0) == UINT_MAX);
    // The surviving outlet is untouched.
    CHECK(gate->GetConnections(0) == 1u);
    p.Calculate(YSE::T_DSP);
  }

  TEST_CASE("setparams: gSwitch inlet growth preserves incoming edges") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* noise = p.CreateObject(YSE::OBJ::D_NOISE, "");
    YSE::pHandle* sw = p.CreateObject(YSE::OBJ::G_SWITCH, "2");
    REQUIRE(sw->GetInputs() == 3); // selector + 2 value inlets
    p.Connect(noise, 0, sw, 0);
    REQUIRE(noise->GetConnections(0) == 1);

    sw->SetParams("4");
    CHECK(sw->GetInputs() == 5);
    // The buffer edge into the surviving inlet was rewired onto the
    // replacement — the source outlet still has exactly one live target.
    CHECK(noise->GetConnections(0) == 1);
    p.Calculate(YSE::T_DSP);
  }

  TEST_CASE("setparams: gRoute list re-parse rebuilds the outlet set") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* route = p.CreateObject(YSE::OBJ::G_ROUTE, "a b");
    REQUIRE(route->GetOutputs() == 3); // one per token + fall-through

    route->SetParams("x y z");
    CHECK(route->GetOutputs() == 4);
    CHECK(route->GetParams() == "x y z");
  }

  TEST_CASE("setparams: gReceive dataName re-parse redirects value delivery") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "alpha");
    MultiSink sink;
    YSE::pHandle hSink(&sink);
    p.Connect(recv, 0, &hSink, 0);

    CHECK(p.PassData(1, "alpha", YSE::T_GUI));
    p.Calculate(YSE::T_DSP);
    CHECK(sink.intValue == 1);

    // Replacing the receiver re-registers it under the new name; the outlet
    // connection to the sink survives the swap.
    recv->SetParams("beta 0");
    CHECK_FALSE(p.PassData(2, "alpha", YSE::T_GUI));
    CHECK(p.PassData(3, "beta", YSE::T_GUI));
    p.Calculate(YSE::T_DSP);
    CHECK(sink.intValue == 3);
  }

  TEST_CASE("setparams: DumpJSON round-trips a live re-parse") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* gate = p.CreateObject(YSE::OBJ::G_GATE, "2");
    YSE::pHandle* sine = p.CreateObject(YSE::OBJ::D_SINE, "440");
    gate->SetParams("4");
    sine->SetParams("880");

    patcherImplementation copy(1, nullptr);
    copy.ParseJSON(p.DumpJSON());
    REQUIRE(copy.Objects() == 2);
    for (unsigned int i = 0; i < copy.Objects(); i++) {
      YSE::pHandle* h = copy.GetHandleFromList(i);
      if (std::string(h->Type()) == YSE::OBJ::G_GATE) {
        CHECK(h->GetParams() == "4");
        CHECK(h->GetOutputs() == 4);
      } else {
        CHECK(h->GetParams() == "880");
      }
    }
  }

  TEST_CASE("setparams: replacements retire through the background pool") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* gate = p.CreateObject(YSE::OBJ::G_GATE, "2");

    for (int i = 0; i < 50; ++i) {
      gate->SetParams(i % 2 == 0 ? "3" : "2");
      p.Calculate(YSE::T_DSP);
    }

    // Same drain pattern as the #227 reclaim test: keep the epoch moving so
    // the pool can cross the +2 grace for the retired objects and graphs.
    for (int spins = 0; spins < 2000 && p.PendingRetired() > 4; ++spins) {
      gate->SetParams(spins % 2 == 0 ? "3" : "2");
      p.Calculate(YSE::T_DSP);
      p.Calculate(YSE::T_DSP);
      p.Calculate(YSE::T_DSP);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(p.PendingRetired() <= 4);
  }

  // ---- Concurrency: live SetParams against a rendering patcher ----
  //
  // The #234 acceptance: concurrent pHandle::SetParams — including the
  // gGate/gSwitch pin re-parse — against a rendering patcher must be clean
  // under TSan and ASan. This is the seed for the #229 harness extension.

  TEST_CASE("setparams: concurrent re-parse against a rendering patcher") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* sine = p.CreateObject(YSE::OBJ::D_SINE, "440");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* gate = p.CreateObject(YSE::OBJ::G_GATE, "2");
    YSE::pHandle* sw = p.CreateObject(YSE::OBJ::G_SWITCH, "2");
    // Two stable receivers hammer the gate through the RT-safe value path:
    // "sel" re-selects outlet 1 (a structural replacement resets the
    // selection) and "val" makes the gate index outputs[activeOutlet - 1] on
    // the audio thread — the exact read the old in-place re-parse raced with
    // its pop_back/emplace_back reallocation. A third receiver has its own
    // dataName churned to cover the string-param replacement.
    YSE::pHandle* recvSel = p.CreateObject(YSE::OBJ::G_RECEIVE, "sel");
    YSE::pHandle* recvVal = p.CreateObject(YSE::OBJ::G_RECEIVE, "val");
    YSE::pHandle* recvRep = p.CreateObject(YSE::OBJ::G_RECEIVE, "tgt");
    p.Connect(sine, 0, dac, 0);
    p.Connect(recvSel, 0, gate, 0);
    p.Connect(recvVal, 0, gate, 1);

    std::atomic<bool> stop{false};
    std::thread render([&] {
      while (!stop.load(std::memory_order_acquire)) {
        p.Calculate(YSE::T_DSP);
      }
    });

    // Control thread: scalar re-parses on the DSP object interleaved with
    // structural re-parses (pin growth/shrink on gGate/gSwitch, dataName
    // re-parse on gReceive) and value traffic that fans out into the
    // just-replaced objects during the block.
    for (int i = 0; i < 400; ++i) {
      sine->SetParams(i % 2 == 0 ? "880" : "440");
      gate->SetParams(i % 2 == 0 ? "8" : "2");
      sw->SetParams(i % 2 == 0 ? "5" : "2");
      recvRep->SetParams(i % 2 == 0 ? "tgtB 0" : "tgt 0");
      p.PassData(1, "sel", YSE::T_GUI);
      p.PassData(i, "val", YSE::T_GUI);
      p.PassData(i, "tgt", YSE::T_GUI);
    }

    stop.store(true, std::memory_order_release);
    render.join();

    // The patcher is still coherent: the last re-parse won, and a final
    // block renders the sine through the preserved wiring.
    CHECK(gate->GetOutputs() == 2);
    CHECK(sw->GetInputs() == 3);
    p.Calculate(YSE::T_DSP);
    p.Calculate(YSE::T_DSP);
    CHECK_FALSE(p.output[0].isSilent());
  }

  // ---- Objects that register no parameters (issue #627) ----
  //
  // Parameters::Set read `parms.back()` to decide whether trailing arguments
  // should be appended to a list parameter, without first checking that any
  // parameter is registered at all. For an object with no ADD_PARAM the
  // tokenizer's `currentArg < parms.size()` guard is `0 < 0`, so the very first
  // token took that branch and `back()` dereferenced an empty vector — a
  // segfault on `.mean`, `.togedge`, the trigonometric family, `.abs`/`.sqrt`,
  // `.atodb`/`.dbtoa`, `.mtof`/`.ftom`, `~noise` and the GUI objects.
  //
  // The pinned behaviour: **surplus arguments are ignored**, exactly as they
  // already were for an object that registers *some* parameters and is handed
  // more than it can take. The argument string is still stored verbatim, so
  // GetParams()/DumpJSON round-trip a patch file unchanged rather than
  // silently rewriting it. This is what the parented path (BuildPlan) has
  // always done; the two now agree.

  TEST_CASE("setparams: a parameter set with nothing registered ignores arguments (#627)") {
    // The defect at its narrowest: no Register() call at all, then a non-empty
    // argument string. This is what every no-ADD_PARAM object hands to
    // Parameters::Set from CreateObjectUnlocked.
    YSE::PATCHER::Parameters parms;
    REQUIRE(parms.Count() == 0);

    parms.Set("5");
    CHECK(parms.Get() == "5");

    // More than one token, and a trailing token — the list-append branch that
    // owned the bad read — must be just as harmless.
    parms.Set("5 6 7");
    CHECK(parms.Get() == "5 6 7");

    // A plan built for the same object is the parented equivalent: no scalar
    // writes, nothing applied, the string still stored.
    YSE::PATCHER::ParamOp ops[4];
    CHECK(parms.BuildPlan("8 9", ops, 4) == 0);
    CHECK(parms.Get() == "8 9");
  }

  // Class-level failsafe rather than a list of the objects known to be
  // affected today: every registered type, created through the public
  // CreateObject entry point with an argument it may or may not want. A new
  // object that registers no parameters is covered the day it is added.
  TEST_CASE("setparams: every registered object survives a stray creation argument (#627)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    REQUIRE(names.size() > 0);

    patcherImplementation p(1, nullptr);
    for (const auto& name : names) {
      CAPTURE(name);
      YSE::pHandle* h = p.CreateObject(name, "5");
      REQUIRE(h != nullptr);
      // ~dac / ~adc are built channel-matched and never see SetParams at all
      // (patcherImplementation::CreateObjectUnlocked), so they keep an empty
      // param string; every other type stores what it was handed.
      if (name != YSE::OBJ::D_DAC && name != YSE::OBJ::D_ADC) {
        CHECK(h->GetParams() == "5");
      }
    }
  }

  TEST_CASE("setparams: a no-parameter object round-trips a patch file carrying arguments (#627)") {
    // ParseJSON restores every object through CreateObjectUnlocked with the
    // saved `parms` string, so a patch file whose no-parameter object carries a
    // non-empty value took the crash down the file-loading path — reachable
    // from any hand-edited or older patch.
    const std::vector<const char*> noParamTypes = {
        YSE::OBJ::G_MEAN,          YSE::OBJ::G_TOGEDGE,       YSE::OBJ::G_ABS,
        YSE::OBJ::G_SQRT,          YSE::OBJ::G_ATODB,         YSE::OBJ::G_DBTOA,
        YSE::OBJ::MIDITOFREQUENCY, YSE::OBJ::FREQUENCYTOMIDI, YSE::OBJ::G_SIN,
        YSE::OBJ::G_COSH,
    };

    patcherImplementation p(1, nullptr);
    for (const char* type : noParamTypes) {
      CAPTURE(type);
      REQUIRE(p.CreateObject(type, "5") != nullptr);
    }
    REQUIRE(p.Objects() == (unsigned int)noParamTypes.size());

    patcherImplementation loaded(1, nullptr);
    loaded.ParseJSON(p.DumpJSON());
    REQUIRE(loaded.Objects() == (unsigned int)noParamTypes.size());
    for (unsigned int i = 0; i < loaded.Objects(); i++) {
      YSE::pHandle* h = loaded.GetHandleFromList(i);
      CAPTURE(h->Type());
      // The stray argument survives the round trip verbatim; loading a patch
      // must not quietly rewrite it.
      CHECK(h->GetParams() == "5");
    }
  }

  TEST_CASE("setparams: a stray argument does not disturb a no-parameter object (#627)") {
    // Ignored means ignored: the object still behaves like the bare one. .mean
    // averages from scratch, and a live re-parse on the parented object is the
    // no-op the scalar path already made it.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* mean = p.CreateObject(YSE::OBJ::G_MEAN, "5");
    REQUIRE(mean != nullptr);

    MultiSink sink;
    YSE::pHandle hSink(&sink);
    p.Connect(mean, 0, &hSink, 0);

    mean->SetFloatData(0, 2.f);
    CHECK(sink.floatValue == doctest::Approx(2.f)); // mean of {2}, not of {5, 2}
    mean->SetFloatData(0, 4.f);
    CHECK(sink.floatValue == doctest::Approx(3.f)); // mean of {2, 4}

    mean->SetParams("11 12");
    CHECK(mean->GetParams() == "11 12");
    p.Calculate(YSE::T_DSP);
    mean->SetFloatData(0, 6.f);
    CHECK(sink.floatValue == doctest::Approx(4.f)); // still {2, 4, 6}
  }

} // TEST_SUITE("patcher")
