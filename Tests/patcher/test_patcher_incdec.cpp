// Tests for `.incdec` — Max's incdec, "increment or decrement a value", the
// pair of arrows a host draws over a number (issue #558).
//
// The issue asks two questions and these tests answer both.
//
// The first is *whether this object should exist at all*, given `.counter`
// already holds a number and steps it. The answer is that a bang means the
// opposite thing on the two objects — `.counter` steps on a bang, `.incdec`
// reads on one — and there is a case below that pins exactly that, against the
// real `gCounter` rather than against hand-written expectations, so the
// decision cannot drift back into a merge without a test going red.
//
// The second is the object itself. Four layers, and they are not
// interchangeable:
//
//   - **standalone cases** pin the grammar: what each of the three inlets
//     accepts, which messages emit and which stay silent (Max is explicit that
//     a number arriving at the inlet "is not output directly"), the command
//     words, the step, the clamp, the wrap, and the arithmetic at the ends of
//     the int range where a naive implementation overflows.
//
//   - **end-to-end cases** build a real patcher through the public API — real
//     `CreateObject` with real creation arguments, real `Connect`, driven
//     through `pHandle::SetListData` and `SetIntData` the way a host clicks an
//     arrow, read back through the downstream object's own `pHandle` — and ask
//     the issue's use case: does "preset slot, 1-8, wrapping" work as one
//     object? Plus the JSON round trip, because a stepper that cannot save its
//     bounds has not exposed anything.
//
//   - **a live re-range case**, because all four params are scalars: changing
//     the range on a running patcher must ride the wait-free scalar plan
//     (issue #234) rather than replacing the object.
//
//   - **real-time cases** measure every message path for allocation.
//
// No audio device required.

#include <doctest/doctest.h>
#include <climits>
#include <memory>
#include <string>
#include <vector>

#include "patcher/guiObjects/gIncDec.h"
#include "patcher/inlet.h"
#include "patcher/math/gCounter.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::gIncDec;
using YSE::PATCHER::patcherImplementation;
using YSE::PATCHER::Register;

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("incdec: type name, port counts and outlet type (#558)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_INCDEC);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".incdec");
    CHECK(h->GetInputs() == 3);
    CHECK(h->GetOutputs() == 1);
    // INT, not ANY: a stepper over whole numbers has nothing else to send.
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::INT);
  }

  TEST_CASE("incdec: registry name and validity (#558)") {
    CHECK(YSE::patcher::IsValidObject(".incdec"));
    auto names = Register().AllNames();
    bool found = false;
    for (const auto& n : names) {
      if (n == ".incdec") found = true;
    }
    CHECK(found);
  }

  // ─── the decision the issue asked for ───────────────────────────────────────

  TEST_CASE("incdec: a bang reads here and steps on .counter (#558)") {
    // Issue #558 asks whether `.counter` should have gained bounds and a
    // direction instead of a second object existing. It should not, and this is
    // the reason, checked against the real object rather than asserted in a
    // comment: the same stimulus means opposite things on the two. `.counter`
    // is the thing behind a `.metro` that numbers events; `.incdec` is a
    // control whose arrows move it and whose bang merely reads it. One object
    // cannot hold both meanings.
    MultiSink stepperSink, counterSink;
    gIncDec stepper;
    YSE::PATCHER::gCounter counter;
    TestHelpers::Wire(stepper, 0, stepperSink);
    TestHelpers::Wire(counter, 0, counterSink);

    // Both start at 0 and both are banged three times.
    for (int i = 0; i < 3; i++) {
      stepper.GetInlet(0)->SetBang(YSE::T_GUI);
      counter.GetInlet(0)->SetBang(YSE::T_GUI);
    }

    // Both emitted on every bang — that much they share.
    REQUIRE(counterSink.gotInt);
    REQUIRE(stepperSink.gotInt);
    // But the counter has walked and the stepper has not moved at all.
    CHECK(counterSink.intValue == 3);
    CHECK(stepperSink.intValue == 0);
    CHECK(stepper.GetGuiValue() == std::to_string(0));
  }

  // ─── the grammar ────────────────────────────────────────────────────────────

  TEST_CASE("incdec: inc and dec move by the step and emit (#558)") {
    MultiSink sink;
    gIncDec stepper;
    stepper.SetParams("5");
    TestHelpers::Wire(stepper, 0, sink);

    stepper.GetInlet(0)->SetList("inc", YSE::T_GUI);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 5);

    stepper.GetInlet(0)->SetList("inc", YSE::T_GUI);
    CHECK(sink.intValue == 10);

    stepper.GetInlet(0)->SetList("dec", YSE::T_GUI);
    CHECK(sink.intValue == 5);

    stepper.GetInlet(0)->SetList("dec", YSE::T_GUI);
    stepper.GetInlet(0)->SetList("dec", YSE::T_GUI);
    CHECK(sink.intValue == -5);
  }

  TEST_CASE("incdec: an int or float on the left inlet sets without emitting (#558)") {
    // Max: the number arriving at the inlet "is not output directly". That is
    // what lets a `.loadmess` or a preset place the stepper without firing
    // everything downstream, and it is the one place this object's grammar
    // would be easy to get wrong by copying `.i`.
    MultiSink sink;
    gIncDec stepper;
    TestHelpers::Wire(stepper, 0, sink);

    stepper.GetInlet(0)->SetInt(42, YSE::T_GUI);
    CHECK_FALSE(sink.gotInt);
    CHECK(stepper.GetGuiValue() == std::to_string(42));

    stepper.GetInlet(0)->SetFloat(-7.9f, YSE::T_GUI);
    CHECK_FALSE(sink.gotInt);
    // Truncated toward zero, as Max truncates a float in every inlet.
    CHECK(stepper.GetGuiValue() == std::to_string(-7));

    // ... and a bang is how you read it back.
    stepper.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == -7);
  }

  TEST_CASE("incdec: 'set <n>' is the int message spelled out (#558)") {
    MultiSink sink;
    gIncDec stepper;
    TestHelpers::Wire(stepper, 0, sink);

    stepper.GetInlet(0)->SetList("set 12", YSE::T_GUI);
    CHECK_FALSE(sink.gotInt);
    CHECK(stepper.GetGuiValue() == std::to_string(12));

    // A bare `set` names no value, so it changes nothing rather than guessing.
    stepper.GetInlet(0)->SetList("set", YSE::T_GUI);
    CHECK(stepper.GetGuiValue() == std::to_string(12));

    // And a token that is not a number at all is ignored.
    stepper.GetInlet(0)->SetList("wobble", YSE::T_GUI);
    CHECK(stepper.GetGuiValue() == std::to_string(12));
  }

  TEST_CASE("incdec: 'set' reads the whole int range, which a float could not (#558)") {
    // The list argument is read as a decimal integer rather than through a
    // float: 2000000001 needs 31 bits and a float carries 24, so a float-routed
    // reader would hand back 2000000000 here. The object is integer-valued from
    // end to end, so the reader is too.
    gIncDec stepper;
    stepper.GetInlet(0)->SetList("set 2000000001", YSE::T_GUI);
    CHECK(stepper.GetGuiValue() == std::to_string(2000000001));

    stepper.GetInlet(0)->SetList("set -2000000001", YSE::T_GUI);
    CHECK(stepper.GetGuiValue() == std::to_string(-2000000001));
  }

  TEST_CASE("incdec: a bare numeric list is the number it spells (#558)") {
    MultiSink sink;
    gIncDec stepper;
    TestHelpers::Wire(stepper, 0, sink);

    stepper.GetInlet(0)->SetList("9", YSE::T_GUI);
    CHECK_FALSE(sink.gotInt);
    CHECK(stepper.GetGuiValue() == std::to_string(9));
  }

  // ─── the nudge inlet ────────────────────────────────────────────────────────

  TEST_CASE("incdec: the middle inlet moves signed steps and emits (#558)") {
    // The arrows on a cord: a MIDI encoder sends +1 and -1 and drives the
    // stepper with one connection.
    MultiSink sink;
    gIncDec stepper;
    stepper.SetParams("3");
    TestHelpers::Wire(stepper, 0, sink);

    stepper.GetInlet(1)->SetInt(1, YSE::T_GUI);
    CHECK(sink.intValue == 3);

    stepper.GetInlet(1)->SetInt(-1, YSE::T_GUI);
    CHECK(sink.intValue == 0);

    // More than one step at a time.
    stepper.GetInlet(1)->SetInt(4, YSE::T_GUI);
    CHECK(sink.intValue == 12);

    // 0 moves nothing and still emits — a read, exactly like a bang.
    sink.reset();
    stepper.GetInlet(1)->SetInt(0, YSE::T_GUI);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 12);
  }

  // ─── the step inlet ─────────────────────────────────────────────────────────

  TEST_CASE("incdec: the right inlet sets the step size live (#558)") {
    MultiSink sink;
    gIncDec stepper;
    TestHelpers::Wire(stepper, 0, sink);

    stepper.GetInlet(0)->SetList("inc", YSE::T_GUI);
    CHECK(sink.intValue == 1);

    // Coarse. Setting the step emits nothing by itself.
    sink.reset();
    stepper.GetInlet(2)->SetInt(12, YSE::T_GUI);
    CHECK_FALSE(sink.gotInt);
    stepper.GetInlet(0)->SetList("inc", YSE::T_GUI);
    CHECK(sink.intValue == 13);

    // A negative step swaps the arrows.
    stepper.GetInlet(2)->SetInt(-1, YSE::T_GUI);
    stepper.GetInlet(0)->SetList("inc", YSE::T_GUI);
    CHECK(sink.intValue == 12);
    stepper.GetInlet(0)->SetList("dec", YSE::T_GUI);
    CHECK(sink.intValue == 13);

    // A step of 0 leaves the value alone and still emits.
    stepper.GetInlet(2)->SetInt(0, YSE::T_GUI);
    stepper.GetInlet(0)->SetList("inc", YSE::T_GUI);
    CHECK(sink.intValue == 13);
  }

  // ─── the bounds ─────────────────────────────────────────────────────────────

  TEST_CASE("incdec: clamping is the default at both ends (#558)") {
    MultiSink sink;
    gIncDec stepper;
    stepper.SetParams("1 0 11");
    TestHelpers::Wire(stepper, 0, sink);

    stepper.GetInlet(0)->SetList("set 11", YSE::T_GUI);
    stepper.GetInlet(0)->SetList("inc", YSE::T_GUI);
    CHECK(sink.intValue == 11);

    stepper.GetInlet(0)->SetList("set 0", YSE::T_GUI);
    stepper.GetInlet(0)->SetList("dec", YSE::T_GUI);
    CHECK(sink.intValue == 0);

    // A set from outside the range lands on the boundary too.
    stepper.GetInlet(0)->SetInt(100, YSE::T_GUI);
    CHECK(stepper.GetGuiValue() == std::to_string(11));
    stepper.GetInlet(0)->SetInt(-100, YSE::T_GUI);
    CHECK(stepper.GetGuiValue() == std::to_string(0));
  }

  TEST_CASE("incdec: wrap carries around, inclusive at both ends (#558)") {
    // The span is maximum - minimum + 1, so one past the top is the bottom.
    // Twelve pitch classes, which is the case that decides the question: 11
    // stepping to 0 must be a step of 1, not of 12.
    MultiSink sink;
    gIncDec stepper;
    stepper.SetParams("1 0 11 1");
    TestHelpers::Wire(stepper, 0, sink);

    stepper.GetInlet(0)->SetList("set 11", YSE::T_GUI);
    stepper.GetInlet(0)->SetList("inc", YSE::T_GUI);
    CHECK(sink.intValue == 0);

    stepper.GetInlet(0)->SetList("dec", YSE::T_GUI);
    CHECK(sink.intValue == 11);

    // Several spans out in one move, in both directions.
    stepper.GetInlet(0)->SetList("set 0", YSE::T_GUI);
    stepper.GetInlet(1)->SetInt(25, YSE::T_GUI);
    CHECK(sink.intValue == 1);
    stepper.GetInlet(0)->SetList("set 0", YSE::T_GUI);
    stepper.GetInlet(1)->SetInt(-25, YSE::T_GUI);
    CHECK(sink.intValue == 11);
  }

  TEST_CASE("incdec: limits given the wrong way round still bound correctly (#558)") {
    // `.pong`'s rule: the pair is ordered, not trusted.
    MultiSink sink;
    gIncDec stepper;
    stepper.SetParams("1 11 0");
    TestHelpers::Wire(stepper, 0, sink);

    stepper.GetInlet(0)->SetInt(50, YSE::T_GUI);
    CHECK(stepper.GetGuiValue() == std::to_string(11));
    stepper.GetInlet(0)->SetInt(-50, YSE::T_GUI);
    CHECK(stepper.GetGuiValue() == std::to_string(0));
  }

  TEST_CASE("incdec: a collapsed range has exactly one legal value (#558)") {
    gIncDec clamping;
    clamping.SetParams("1 7 7");
    clamping.GetInlet(0)->SetInt(100, YSE::T_GUI);
    CHECK(clamping.GetGuiValue() == std::to_string(7));

    // The wrapping branch divides by the span, so a collapsed range is also the
    // case that would divide by zero if the span were maximum - minimum.
    gIncDec wrapping;
    wrapping.SetParams("1 7 7 1");
    wrapping.GetInlet(0)->SetInt(100, YSE::T_GUI);
    CHECK(wrapping.GetGuiValue() == std::to_string(7));
    wrapping.GetInlet(0)->SetList("inc", YSE::T_GUI);
    CHECK(wrapping.GetGuiValue() == std::to_string(7));
  }

  TEST_CASE("incdec: the value is bounded on the way out, not only on the way in (#558)") {
    // A stepper created over 60-72 is born holding 0, which is outside its own
    // range. It must not report it — and a live re-range must show up at once
    // rather than at the next step.
    gIncDec stepper;
    stepper.SetParams("1 60 72");
    CHECK(stepper.GetGuiValue() == std::to_string(60));

    stepper.GetInlet(0)->SetInt(66, YSE::T_GUI);
    CHECK(stepper.GetGuiValue() == std::to_string(66));

    stepper.SetParams("1 0 10");
    CHECK(stepper.GetGuiValue() == std::to_string(10));
  }

  // ─── the ends of the int range ──────────────────────────────────────────────

  TEST_CASE("incdec: the default range is unbounded and saturates (#558)") {
    // With no arguments the range is the whole int range, so clamping is a
    // no-op and the object is a two-directional `.counter` — but stepping past
    // the end must saturate rather than overflow, which is undefined behaviour
    // done in ints.
    MultiSink sink;
    gIncDec stepper;
    TestHelpers::Wire(stepper, 0, sink);

    stepper.GetInlet(0)->SetInt(INT_MAX, YSE::T_GUI);
    stepper.GetInlet(0)->SetList("inc", YSE::T_GUI);
    CHECK(sink.intValue == INT_MAX);

    stepper.GetInlet(0)->SetInt(INT_MIN, YSE::T_GUI);
    stepper.GetInlet(0)->SetList("dec", YSE::T_GUI);
    CHECK(sink.intValue == INT_MIN);
  }

  TEST_CASE("incdec: the unbounded range wraps at the int boundary (#558)") {
    // wrap over the default range spans 2^32, which only closes because the
    // arithmetic is done in 64 bits.
    MultiSink sink;
    gIncDec stepper;
    stepper.SetParams("1 -2147483648 2147483647 1");
    TestHelpers::Wire(stepper, 0, sink);

    stepper.GetInlet(0)->SetInt(INT_MAX, YSE::T_GUI);
    stepper.GetInlet(0)->SetList("inc", YSE::T_GUI);
    CHECK(sink.intValue == INT_MIN);

    stepper.GetInlet(0)->SetList("dec", YSE::T_GUI);
    CHECK(sink.intValue == INT_MAX);
  }

  TEST_CASE("incdec: a step of INT_MIN does not overflow (#558)") {
    // Negating INT_MIN as an int is undefined and its magnitude is not
    // representable as one, so `dec` with this step is the arithmetic corner
    // that has to be done in 64 bits.
    MultiSink sink;
    gIncDec stepper;
    stepper.SetParams("-2147483648");
    TestHelpers::Wire(stepper, 0, sink);

    stepper.GetInlet(0)->SetList("inc", YSE::T_GUI);
    CHECK(sink.intValue == INT_MIN);

    // Back the other way: -1 * INT_MIN is +2147483648, which is not an int at
    // all. In 64 bits it lands exactly on 0.
    stepper.GetInlet(0)->SetList("dec", YSE::T_GUI);
    CHECK(sink.intValue == 0);

    // And a nudge of many such steps, which is the widest product the object
    // can be asked for.
    stepper.GetInlet(1)->SetInt(INT_MAX, YSE::T_GUI);
    CHECK(sink.intValue == INT_MIN);
  }

  // ─── the GUI value ──────────────────────────────────────────────────────────

  TEST_CASE("incdec: the GUI value is the stored value (#558)") {
    // The single-string scalar protocol `.slider`, `.dial` and `.counter`
    // already use. A stepper is one number and needs nothing structured, so
    // issue #551's redesign is not pre-empted here — pinned so the choice
    // cannot drift silently.
    gIncDec stepper;
    stepper.SetParams("2 0 10");
    CHECK(stepper.GetGuiValue() == std::to_string(0));

    stepper.GetInlet(0)->SetList("inc", YSE::T_GUI);
    CHECK(stepper.GetGuiValue() == std::to_string(2));

    stepper.GetInlet(0)->SetInt(9, YSE::T_GUI);
    CHECK(stepper.GetGuiValue() == std::to_string(9));
  }

  // ─── end to end, through the public patcher API ─────────────────────────────

  TEST_CASE("incdec: a host clicks the arrows on a real patch (#558)") {
    // The issue's use case built the way a host builds it: create the object
    // with its bounds as creation arguments, wire it to something, click the
    // arrows through the public handle API, and read the number that arrived on
    // the other side through that object's own handle. No internal pokes.
    //
    // "Preset slot, 1-8, wrapping" — the eighth slot's next is the first.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* stepper = p.CreateObject(YSE::OBJ::G_INCDEC, "1 1 8 1");
    YSE::pHandle* readout = p.CreateObject(YSE::OBJ::G_INT);
    REQUIRE(stepper != nullptr);
    REQUIRE(readout != nullptr);
    p.Connect(stepper, 0, readout, 0);

    stepper->SetListData(0, "set 8");
    // A set is silent, so nothing has reached the readout yet.
    CHECK(readout->GetGuiValue() == std::to_string(0));

    stepper->SetListData(0, "inc");
    CHECK(readout->GetGuiValue() == std::to_string(1));

    stepper->SetListData(0, "dec");
    CHECK(readout->GetGuiValue() == std::to_string(8));

    // The encoder route: the middle inlet, signed.
    stepper->SetIntData(1, -3);
    CHECK(readout->GetGuiValue() == std::to_string(5));

    // And a bang reads the stepper without moving it.
    stepper->SetBang(0);
    CHECK(readout->GetGuiValue() == std::to_string(5));
    CHECK(stepper->GetGuiValue() == std::to_string(5));
  }

  TEST_CASE("incdec: params survive a DumpJSON / ParseJSON round trip (#558)") {
    // A stepper that cannot save its bounds has not exposed anything; the
    // loaded object has to *behave* the same, not merely carry the same string.
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_INCDEC, "2 0 11 1") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".incdec") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".incdec"));
    CHECK(h->GetParams() == std::string("2 0 11 1"));

    YSE::pHandle* readout = loaded.CreateObject(YSE::OBJ::G_INT);
    REQUIRE(readout != nullptr);
    loaded.Connect(h, 0, readout, 0);

    // The step survived: 5 + 2 is 7, which is a number the readout did not
    // already hold.
    h->SetListData(0, "set 5");
    h->SetListData(0, "inc");
    CHECK(readout->GetGuiValue() == std::to_string(7));

    // And so did the bounds and the wrap: a step of 2 from 10 in a wrapping
    // 0-11 range lands on 12, which is one past the top.
    h->SetListData(0, "set 10");
    h->SetListData(0, "inc");
    CHECK(readout->GetGuiValue() == std::to_string(0));
  }

  TEST_CASE("incdec: a live re-range rides the scalar plan, not a rebuild (#558)") {
    // All four params are scalars and the object registers no clear/parse
    // callbacks, so SetParams on a running patcher must defer to the audio
    // thread rather than replace the object (issue #234). The retire count is
    // the observable difference; the identity and the new bounds are the
    // consequences a patch cares about.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* stepper = p.CreateObject(YSE::OBJ::G_INCDEC, "1 0 3");
    REQUIRE(stepper != nullptr);

    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    p.Connect(stepper, 0, &sinkHandle, 0);

    stepper->SetListData(0, "set 3");
    stepper->SetListData(0, "inc");
    CHECK(sink.intValue == 3);

    const std::size_t retiredBefore = p.PendingRetired();
    const unsigned int idBefore = stepper->GetID();
    stepper->SetParams("1 0 100");
    CHECK(p.PendingRetired() == retiredBefore);
    CHECK(stepper->GetID() == idBefore);
    CHECK(stepper->GetParams() == "1 0 100");

    // Deferred: not visible until the audio thread has drained the plan.
    stepper->SetListData(0, "inc");
    CHECK(sink.intValue == 3);

    p.Calculate(YSE::T_DSP);
    stepper->SetListData(0, "inc");
    CHECK(sink.intValue == 4);
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the parameter order, which is what a
  // binding generator and a saved patch both key on.

  TEST_CASE("incdec: documents itself as GUI with four ordered params (#558)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::G_INCDEC));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GUI);
    CHECK(obj->NumInputs() == 3);
    CHECK(obj->NumOutputs() == 1);

    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 4);
    const std::vector<std::string> expected = {"step", "minimum", "maximum", "wrap"};
    for (size_t i = 0; i < expected.size(); ++i) {
      CAPTURE(i);
      CHECK(docs[i].name == expected[i]);
    }
  }

  // ─── real time ──────────────────────────────────────────────────────────────

  TEST_CASE("incdec: no message path allocates (#558)") {
    // The counter is read inside the scope and asserted outside it: doctest's
    // assertion machinery allocates on its first run, which would otherwise be
    // charged to the object under test. The command strings are built before
    // the scope opens for the same reason — a std::string built at the call
    // site is the caller's allocation, not the object's.
    MultiSink sink;
    gIncDec stepper;
    stepper.SetParams("1 0 11 1");
    TestHelpers::Wire(stepper, 0, sink);

    const std::string inc("inc");
    const std::string dec("dec");
    const std::string set("set 4");
    const std::string bare("7");

    // Warm every path (and doctest) before arming.
    stepper.GetInlet(0)->SetList(inc, YSE::T_GUI);
    stepper.GetInlet(0)->SetList(dec, YSE::T_GUI);
    stepper.GetInlet(0)->SetList(set, YSE::T_GUI);
    stepper.GetInlet(0)->SetList(bare, YSE::T_GUI);
    stepper.GetInlet(0)->SetBang(YSE::T_GUI);
    stepper.GetInlet(1)->SetInt(2, YSE::T_GUI);
    REQUIRE(sink.gotInt);

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      stepper.GetInlet(0)->SetList(inc, YSE::T_GUI);
      stepper.GetInlet(0)->SetList(dec, YSE::T_GUI);
      stepper.GetInlet(0)->SetList(set, YSE::T_GUI);
      stepper.GetInlet(0)->SetList(bare, YSE::T_GUI);
      stepper.GetInlet(0)->SetBang(YSE::T_GUI);
      stepper.GetInlet(0)->SetInt(3, YSE::T_GUI);
      stepper.GetInlet(0)->SetFloat(3.5f, YSE::T_GUI);
      stepper.GetInlet(1)->SetInt(2, YSE::T_GUI);
      stepper.GetInlet(1)->SetFloat(-2.f, YSE::T_GUI);
      stepper.GetInlet(2)->SetInt(3, YSE::T_GUI);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);
  }

} // TEST_SUITE("patcher")
