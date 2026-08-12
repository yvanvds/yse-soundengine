// Tests for `.dial` — Max's dial, "output numbers in a settable range",
// rendered by a host as a knob (issue #552).
//
// The object exists because `.slider` is normalised: it stores a float in
// [0, 1], so every real parameter needs a `.scale` behind it before the number
// means anything. `.dial` carries the range itself, and that is the whole
// claim these tests have to pin down — that the *position* goes in and the
// *mapped value* comes out, that the curve bends the way an audio parameter
// wants, and that with no arguments at all the object is `.slider` again.
//
// Four layers, and they are not interchangeable:
//
//   - **standalone cases** pin the grammar: what the inlet accepts and clamps,
//     the mapping at both ends and in the middle, the exponent, a descending
//     range, a degenerate one, int mode, and the GUI value being the position
//     rather than the mapped value (the `.slider` protocol this object
//     deliberately reuses rather than pre-empting #551's redesign).
//
//   - **end-to-end cases** build a real patcher through the public API — real
//     `CreateObject` with real creation arguments, real `Connect`, driven
//     through `pHandle::SetFloatData` the way a host GUI drives a knob, read
//     back through the downstream object's own `pHandle::GetGuiValue` — and
//     ask the issue's question: does "cutoff, 20-20000 Hz, exponential" work
//     as one object? Plus the JSON round trip, because a patch that cannot
//     save its ranges has not exposed anything.
//
//   - **a live-re-range case**, because all four params are scalars: changing
//     the range on a running patcher must ride the wait-free scalar plan
//     (issue #234) rather than replacing the object.
//
//   - **real-time cases** measure every message path for allocation, in both
//     output modes.
//
// No audio device required.

#include <doctest/doctest.h>
#include <climits>
#include <memory>
#include <string>
#include <vector>

#include "patcher/guiObjects/gDial.h"
#include "patcher/guiObjects/gSlider.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::gDial;
using YSE::PATCHER::patcherImplementation;
using YSE::PATCHER::Register;

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("dial: type name, port counts, and the ANY outlet (#552)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DIAL);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".dial");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
    // ANY, not FLOAT: intMode decides the type of what leaves this outlet and
    // it is a creation argument, read after the outlet has been built.
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::ANY);
  }

  TEST_CASE("dial: registry name and validity (#552)") {
    CHECK(YSE::patcher::IsValidObject(".dial"));
    auto names = Register().AllNames();
    bool found = false;
    for (const auto& n : names) {
      if (n == ".dial") found = true;
    }
    CHECK(found);
  }

  // ─── the default is .slider ─────────────────────────────────────────────────

  TEST_CASE("dial: with no arguments it maps like .slider does (#552)") {
    // The claim in the header comment, checked against the object it claims to
    // match rather than against hand-written expectations: same input, same
    // number out, for the whole range plus both clamps.
    MultiSink dialSink, sliderSink;
    gDial dial;
    YSE::PATCHER::gSlider slider;
    TestHelpers::Wire(dial, 0, dialSink);
    TestHelpers::Wire(slider, 0, sliderSink);

    const float inputs[] = {-0.5f, 0.f, 0.25f, 0.5f, 0.75f, 1.f, 1.5f};
    for (float in : inputs) {
      CAPTURE(in);
      dial.GetInlet(0)->SetFloat(in, YSE::T_GUI);
      slider.GetInlet(0)->SetFloat(in, YSE::T_GUI);
      REQUIRE(dialSink.gotFloat);
      REQUIRE(sliderSink.gotFloat);
      CHECK(dialSink.floatValue == doctest::Approx(sliderSink.floatValue));
      CHECK(dial.GetGuiValue() == slider.GetGuiValue());
    }
  }

  // ─── the range ──────────────────────────────────────────────────────────────

  TEST_CASE("dial: the position is mapped onto the minimum-maximum range (#552)") {
    MultiSink sink;
    gDial dial;
    dial.SetParams("20 20000");
    TestHelpers::Wire(dial, 0, sink);

    dial.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    CHECK(sink.floatValue == doctest::Approx(20.f));

    dial.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    CHECK(sink.floatValue == doctest::Approx(20000.f));

    dial.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(sink.floatValue == doctest::Approx(10010.f));
  }

  TEST_CASE("dial: a position outside [0, 1] is clamped before it is mapped (#552)") {
    // Clamping the *position* rather than the mapped value is what keeps the
    // exponent well-behaved: a negative normalised value is the one input
    // MapRange has to mirror the curve for, and it can never reach it here.
    MultiSink sink;
    gDial dial;
    dial.SetParams("20 20000 2");
    TestHelpers::Wire(dial, 0, sink);

    dial.GetInlet(0)->SetFloat(-3.f, YSE::T_GUI);
    CHECK(sink.floatValue == doctest::Approx(20.f));

    dial.GetInlet(0)->SetFloat(7.5f, YSE::T_GUI);
    CHECK(sink.floatValue == doctest::Approx(20000.f));
  }

  TEST_CASE("dial: the exponent bends the mapping into a curve (#552)") {
    // The issue's own example: a cutoff over 20-20000 Hz should not spend half
    // its travel above 10 kHz. With exponent 2 the midpoint sits at a quarter
    // of the span: 20 + 19980 * 0.25 = 5015.
    MultiSink sink;
    gDial dial;
    dial.SetParams("20 20000 2");
    TestHelpers::Wire(dial, 0, sink);

    dial.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(sink.floatValue == doctest::Approx(5015.f));

    // The ends are fixed points of any exponent — the curve bends the travel,
    // it does not move the range.
    dial.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    CHECK(sink.floatValue == doctest::Approx(20.f));
    dial.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    CHECK(sink.floatValue == doctest::Approx(20000.f));
  }

  TEST_CASE("dial: a descending range runs the other way (#552)") {
    MultiSink sink;
    gDial dial;
    dial.SetParams("100 0");
    TestHelpers::Wire(dial, 0, sink);

    dial.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    CHECK(sink.floatValue == doctest::Approx(100.f));
    dial.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    CHECK(sink.floatValue == doctest::Approx(0.f));
    dial.GetInlet(0)->SetFloat(0.25f, YSE::T_GUI);
    CHECK(sink.floatValue == doctest::Approx(75.f));
  }

  TEST_CASE("dial: a degenerate range emits the minimum, never an infinity (#552)") {
    MultiSink sink;
    gDial dial;
    dial.SetParams("440 440");
    TestHelpers::Wire(dial, 0, sink);

    dial.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    CHECK(sink.floatValue == doctest::Approx(440.f));
    dial.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    CHECK(sink.floatValue == doctest::Approx(440.f));
  }

  // ─── int mode ───────────────────────────────────────────────────────────────

  TEST_CASE("dial: intMode rounds and sends an int rather than a float (#552)") {
    // MultiSink records which handler fired, which is the half of this a sink
    // that only kept a number could not tell: an int-shaped float is still a
    // float to everything downstream.
    MultiSink sink;
    gDial dial;
    dial.SetParams("0 127 1 1");
    TestHelpers::Wire(dial, 0, sink);

    dial.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(sink.gotInt);
    CHECK_FALSE(sink.gotFloat);
    // 63.5 rounds away from zero.
    CHECK(sink.intValue == 64);

    sink.reset();
    dial.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    CHECK(sink.intValue == 0);

    sink.reset();
    dial.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    CHECK(sink.intValue == 127);
  }

  TEST_CASE("dial: float mode is the default and stays a float (#552)") {
    MultiSink sink;
    gDial dial;
    dial.SetParams("0 127");
    TestHelpers::Wire(dial, 0, sink);

    dial.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(sink.gotFloat);
    CHECK_FALSE(sink.gotInt);
    CHECK(sink.floatValue == doctest::Approx(63.5f));
  }

  TEST_CASE("dial: int mode saturates rather than wrapping on an absurd range (#552)") {
    // A range wider than an int is a user error, but the answer still has to be
    // inside the range rather than at the bottom of it — MapRange's own reason
    // for returning the minimum on a degenerate range. The float-to-int cast is
    // undefined for these values, so this is the guard that makes it defined.
    MultiSink sink;
    gDial dial;
    dial.SetParams("0 1e12 1 1");
    TestHelpers::Wire(dial, 0, sink);

    dial.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == INT_MAX);

    sink.reset();
    dial.SetParams("0 -1e12 1 1");
    dial.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    CHECK(sink.intValue == INT_MIN);
  }

  // ─── the inlet grammar ──────────────────────────────────────────────────────

  TEST_CASE("dial: an int on the inlet is a widened position, not a value (#552)") {
    // .slider's convention, kept deliberately: the inlet carries the knob
    // position, so an int is a float that happens to have no fraction and gets
    // clamped like any other. Anything else would make the same inlet mean two
    // things depending on the message type.
    MultiSink sink;
    gDial dial;
    dial.SetParams("10 20");
    TestHelpers::Wire(dial, 0, sink);

    dial.GetInlet(0)->SetInt(0, YSE::T_GUI);
    CHECK(sink.floatValue == doctest::Approx(10.f));

    dial.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(sink.floatValue == doctest::Approx(20.f));

    // Out of range in both directions.
    dial.GetInlet(0)->SetInt(9, YSE::T_GUI);
    CHECK(sink.floatValue == doctest::Approx(20.f));
    dial.GetInlet(0)->SetInt(-4, YSE::T_GUI);
    CHECK(sink.floatValue == doctest::Approx(10.f));
  }

  TEST_CASE("dial: a bang re-emits without moving the knob (#552)") {
    MultiSink sink;
    gDial dial;
    dial.SetParams("0 100");
    TestHelpers::Wire(dial, 0, sink);

    dial.GetInlet(0)->SetFloat(0.4f, YSE::T_GUI);
    CHECK(sink.floatValue == doctest::Approx(40.f));

    sink.reset();
    dial.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(sink.gotFloat);
    CHECK(sink.floatValue == doctest::Approx(40.f));
    CHECK(dial.GetGuiValue() == std::to_string(0.4f));
  }

  // ─── the GUI value ──────────────────────────────────────────────────────────

  TEST_CASE("dial: the GUI value is the position, not the mapped value (#552)") {
    // Deliberate, and the reason is in the header: the single-string GUI
    // protocol carries one scalar, and the one a host needs to *draw* the knob
    // is where it sits. Displaying "5015 Hz" needs the range too, which is
    // issue #551's redesign and not this object's business. Pinning it here so
    // the choice cannot drift silently.
    gDial dial;
    dial.SetParams("20 20000 2");
    CHECK(dial.GetGuiValue() == std::to_string(0.f));

    dial.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(dial.GetGuiValue() == std::to_string(0.5f));

    // Clamped on the way in, so the GUI value is always a legal position.
    dial.GetInlet(0)->SetFloat(4.f, YSE::T_GUI);
    CHECK(dial.GetGuiValue() == std::to_string(1.f));
  }

  // ─── end to end, through the public patcher API ─────────────────────────────

  TEST_CASE("dial: a host knob drives a real patch through pHandle (#552)") {
    // The issue's use case built the way a host builds it: create the object
    // with its range as a creation argument, wire it to something, move the
    // knob through the public handle API, and read the number that arrived on
    // the other side through that object's own handle. No internal pokes.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* dial = p.CreateObject(YSE::OBJ::G_DIAL, "20 20000 2");
    YSE::pHandle* readout = p.CreateObject(YSE::OBJ::G_FLOAT);
    REQUIRE(dial != nullptr);
    REQUIRE(readout != nullptr);
    p.Connect(dial, 0, readout, 0);

    dial->SetFloatData(0, 0.f);
    CHECK(std::stof(readout->GetGuiValue()) == doctest::Approx(20.f));

    dial->SetFloatData(0, 0.5f);
    CHECK(std::stof(readout->GetGuiValue()) == doctest::Approx(5015.f));

    dial->SetFloatData(0, 1.f);
    CHECK(std::stof(readout->GetGuiValue()) == doctest::Approx(20000.f));

    // And the knob itself reports where it sits, which is what the host draws.
    CHECK(std::stof(dial->GetGuiValue()) == doctest::Approx(1.f));
  }

  TEST_CASE("dial: an int-mode knob reaches a .i through a real patch (#552)") {
    // The int half of the same journey: an int-mode dial has to land in an
    // object that only accepts ints, which is what makes "MIDI controller, 0-127"
    // one object instead of a dial, a scale and a round.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* dial = p.CreateObject(YSE::OBJ::G_DIAL, "0 127 1 1");
    YSE::pHandle* readout = p.CreateObject(YSE::OBJ::G_INT);
    REQUIRE(dial != nullptr);
    REQUIRE(readout != nullptr);
    p.Connect(dial, 0, readout, 0);

    dial->SetFloatData(0, 1.f);
    CHECK(readout->GetGuiValue() == std::to_string(127));

    dial->SetFloatData(0, 0.5f);
    CHECK(readout->GetGuiValue() == std::to_string(64));
  }

  TEST_CASE("dial: params survive a DumpJSON / ParseJSON round trip (#552)") {
    // A patch that cannot save its ranges has not exposed a parameter; the
    // loaded object has to map the same way, not merely carry the same string.
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_DIAL, "20 20000 2 0") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".dial") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".dial"));
    CHECK(h->GetParams() == std::string("20 20000 2 0"));

    YSE::pHandle* readout = loaded.CreateObject(YSE::OBJ::G_FLOAT);
    REQUIRE(readout != nullptr);
    loaded.Connect(h, 0, readout, 0);
    h->SetFloatData(0, 0.5f);
    CHECK(std::stof(readout->GetGuiValue()) == doctest::Approx(5015.f));
  }

  TEST_CASE("dial: a live re-range rides the scalar plan, not a rebuild (#552)") {
    // All four params are scalars and the object registers no clear/parse
    // callbacks, so SetParams on a running patcher must defer to the audio
    // thread rather than replace the object (issue #234). The retire count is
    // the observable difference; the identity and the new mapping are the
    // consequences a patch cares about.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* dial = p.CreateObject(YSE::OBJ::G_DIAL, "0 100");
    REQUIRE(dial != nullptr);

    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    p.Connect(dial, 0, &sinkHandle, 0);

    dial->SetFloatData(0, 0.5f);
    CHECK(sink.floatValue == doctest::Approx(50.f));

    const std::size_t retiredBefore = p.PendingRetired();
    const unsigned int idBefore = dial->GetID();
    dial->SetParams("0 1000");
    CHECK(p.PendingRetired() == retiredBefore);
    CHECK(dial->GetID() == idBefore);
    CHECK(dial->GetParams() == "0 1000");

    // Deferred: not visible until the audio thread has drained the plan.
    dial->SetFloatData(0, 0.5f);
    CHECK(sink.floatValue == doctest::Approx(50.f));

    p.Calculate(YSE::T_DSP);
    dial->SetFloatData(0, 0.5f);
    CHECK(sink.floatValue == doctest::Approx(500.f));
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the parameter order, which is what a
  // binding generator and a saved patch both key on.

  TEST_CASE("dial: documents itself as GUI with four ordered params (#552)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::G_DIAL));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GUI);
    CHECK(obj->NumInputs() == 1);
    CHECK(obj->NumOutputs() == 1);

    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 4);
    const std::vector<std::string> expected = {"minimum", "maximum", "exponent", "intMode"};
    for (size_t i = 0; i < expected.size(); ++i) {
      CAPTURE(i);
      CHECK(docs[i].name == expected[i]);
    }
  }

  // ─── real time ──────────────────────────────────────────────────────────────

  TEST_CASE("dial: no message path allocates, in either output mode (#552)") {
    // The counter is read inside the scope and asserted outside it: doctest's
    // assertion machinery allocates on its first run, which would otherwise be
    // charged to the object under test.
    MultiSink floatSink, intSink;
    gDial floatDial, intDial;
    floatDial.SetParams("20 20000 2");
    intDial.SetParams("0 127 1 1");
    TestHelpers::Wire(floatDial, 0, floatSink);
    TestHelpers::Wire(intDial, 0, intSink);

    // Warm both paths (and doctest) before arming.
    floatDial.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    intDial.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    REQUIRE(floatSink.gotFloat);
    REQUIRE(intSink.gotInt);

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      floatDial.GetInlet(0)->SetFloat(0.25f, YSE::T_GUI);
      floatDial.GetInlet(0)->SetInt(1, YSE::T_GUI);
      floatDial.GetInlet(0)->SetBang(YSE::T_GUI);
      intDial.GetInlet(0)->SetFloat(0.75f, YSE::T_GUI);
      intDial.GetInlet(0)->SetInt(0, YSE::T_GUI);
      intDial.GetInlet(0)->SetBang(YSE::T_GUI);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);
  }

} // TEST_SUITE("patcher")
