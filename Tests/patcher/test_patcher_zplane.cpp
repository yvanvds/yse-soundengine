// Tests for the ~zplane patcher object (patcher/filters/dZplane), issue
// #1103. The conversions and editing rules are DSP::zPlane's and
// DSP::filterDesign's and are tested in Tests/dsp/test_zplane.cpp; these
// show that the messages, inlets, outlets, GUI value, arguments and saved
// state reach them, and that edits drive ~cascade in a rendered patch after
// a JSON round trip.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/filters/dZplane.h"
#include "patcher/math/gExprEval.h"
#include "patcher/pListArgs.h"
#include "dsp/filters/biquadCascade.hpp"
#include "dsp/filters/zPlane.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using YSE::DSP::biquadCascade;
using YSE::DSP::buffer;
using YSE::DSP::phasor;
using YSE::DSP::zPlane;
using YSE::PATCHER::dZplane;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr UInt CAPACITY = zPlane::MAX_SECTIONS * 5;

  std::string Text(float v) {
    char text[YSE::PATCHER::kExprValueTextMax];
    const int n = YSE::PATCHER::ExprFormatValue(YSE::PATCHER::ExprValue::Float(v), text,
                                                YSE::PATCHER::kExprValueTextMax);
    return std::string(text, static_cast<std::size_t>(n));
  }

  std::string Render(const float* values, UInt n) {
    std::string out;
    for (UInt i = 0; i < n; ++i) {
      if (!out.empty()) out += ' ';
      out += Text(values[i]);
    }
    return out;
  }

  std::vector<float> Coefficients(const zPlane& plane) {
    std::vector<float> c(CAPACITY);
    c.resize(plane.coefficients(c.data(), CAPACITY));
    return c;
  }

  std::string CoefficientsOf(const zPlane& plane) {
    const std::vector<float> c = Coefficients(plane);
    return Render(c.data(), static_cast<UInt>(c.size()));
  }

  // The coefficients as ~cascade reads them: a list travels as text, at the
  // precision ExprFormatValue writes.
  std::vector<float> Transported(const zPlane& plane) {
    const std::string text = CoefficientsOf(plane);
    std::vector<float> out;
    std::size_t begin = 0;
    while (begin < text.size()) {
      std::size_t end = text.find(' ', begin);
      if (end == std::string::npos) end = text.size();
      float v = 0.f;
      REQUIRE(YSE::PATCHER::ReadNumericToken(text.data() + begin, end - begin, v));
      out.push_back(v);
      begin = end + 1;
    }
    return out;
  }

  std::string ZeroesOf(const zPlane& plane) {
    float v[CAPACITY];
    return Render(v, plane.zeroes(v, CAPACITY));
  }

  std::string PolesOf(const zPlane& plane) {
    float v[CAPACITY];
    return Render(v, plane.poles(v, CAPACITY));
  }

  std::string GainsOf(const zPlane& plane) {
    float v[CAPACITY];
    return Render(v, plane.gains(v, CAPACITY));
  }

  // The four outlets, each logging into one shared order.
  struct Rig {
    std::vector<char> order;
    TestHelpers::OrderSink outs[4];
    explicit Rig(dZplane& obj) {
      for (int i = 0; i < 4; ++i) {
        outs[i].log = &order;
        outs[i].tag = static_cast<char>('0' + i);
        TestHelpers::Wire(obj, i, outs[i], 0);
      }
    }
    // Every outlet against the plane.
    void Check(const zPlane& plane) const {
      CHECK(outs[0].lastList == CoefficientsOf(plane));
      CHECK(outs[1].lastList == ZeroesOf(plane));
      CHECK(outs[2].lastList == PolesOf(plane));
      CHECK(outs[3].lastList == GainsOf(plane));
    }
  };

  // Counts every message without allocating.
  struct CountingSink : TestHelpers::SinkBase {
    int count = 0;
    CountingSink() {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList([this](const std::string&, int, YSE::THREAD) { ++count; });
      inputs.back().RegisterFloat([this](float, int, YSE::THREAD) { ++count; });
      inputs.back().RegisterInt([this](int, int, YSE::THREAD) { ++count; });
    }
    const char* Type() const override {
      return "counting_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  void Send(dZplane& obj, const std::string& text, int inlet = 0) {
    obj.GetInlet(inlet)->SetList(text, YSE::T_GUI);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~zplane: registry and shape (#1103)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_ZPLANE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~zplane");
    CHECK(h->GetInputs() == 5);
    CHECK(h->GetOutputs() == 4);
    for (int i = 0; i < 4; ++i)
      CHECK(h->OutputDataType(i) == YSE::OUT_TYPE::LIST);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_ZPLANE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
    CHECK(o->GetParamCount() == 1);
    CHECK(o->GetParamDocs()[0].name == "settings");
    CHECK(o->GetOutlet(0)->GetDocLabel() == "coefficients");
    CHECK(o->GetOutlet(1)->GetDocLabel() == "zeroes");
    CHECK(o->GetOutlet(2)->GetDocLabel() == "poles");
    CHECK(o->GetOutlet(3)->GetDocLabel() == "gains");
    CHECK_FALSE(o->GuiValueIsSettable());
    for (int i = 0; i < 5; ++i) {
      CAPTURE(i);
      const unsigned int accepted = o->GetInlet(i)->GetAcceptedTypes();
      CHECK((accepted & YSE::PATCHER::IT_FLOAT) != 0);
      CHECK((accepted & YSE::PATCHER::IT_LIST) != 0);
    }
    CHECK((o->GetInlet(0)->GetAcceptedTypes() & YSE::PATCHER::IT_BANG) != 0);
  }

  TEST_CASE("~zplane: a cascade sends gains, poles and zeroes; inlets and bang send the "
            "coefficients too, right to left (#1103)") {
    dZplane obj;
    Rig rig(obj);
    zPlane ref;

    const float list[] = {2.f, 0.f, -0.5f, 0.f, 0.25f, 1.f, 0.5f, 0.f, -0.5f, 0.f};
    Send(obj, Render(list, 10));
    ref.cascade(list, 10);
    CHECK(rig.order == std::vector<char>{'3', '2', '1'});
    CHECK(rig.outs[0].count == 0);
    CHECK(rig.outs[1].lastList == ZeroesOf(ref));
    CHECK(rig.outs[2].lastList == PolesOf(ref));
    CHECK(rig.outs[3].lastList == GainsOf(ref));
    const float gains[] = {2.f, 1.f};
    CHECK(rig.outs[3].lastList == Render(gains, 2));
    CHECK(obj.Plane().sections() == 2);

    // A bang: everything, the coefficients last.
    rig.order.clear();
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.order == std::vector<char>{'3', '2', '1', '0'});
    rig.Check(ref);
    rig.order.clear();
    Send(obj, "bang");
    CHECK(rig.order == std::vector<char>{'3', '2', '1', '0'});

    // The five coefficient inlets set the first section and send everything.
    rig.order.clear();
    obj.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(rig.order == std::vector<char>{'3', '2', '1', '0'});
    obj.GetInlet(1)->SetFloat(-0.2f, YSE::T_GUI);
    obj.GetInlet(2)->SetInt(0, YSE::T_GUI);
    obj.GetInlet(3)->SetFloat(-1.f, YSE::T_GUI);
    Send(obj, "0.64", 4);
    Send(obj, "3");
    ref.coefficients(0, {3.f, -0.2f, 0.f, -1.f, 0.64f});
    CHECK(rig.outs[0].count == 8);
    rig.Check(ref);

    // Lists that are not whole sets of numbers are not read; neither are a
    // coefficient inlet's lists of more than one number.
    const int sent = rig.outs[1].count;
    Send(obj, "1 2 3");
    Send(obj, "1 2 3 4 five");
    Send(obj, "1 2", 3);
    CHECK(rig.outs[1].count == sent);
    rig.Check(ref);
  }

  TEST_CASE("~zplane: zero and pole move roots and send the new coefficients; order and "
            "pconstrain set the attributes (#1103)") {
    dZplane obj;
    Rig rig(obj);
    zPlane ref;

    Send(obj, "pole 0 0.5 0.5");
    ref.movePole(0, 0.5, 0.5);
    CHECK(rig.order == std::vector<char>{'3', '2', '1', '0'});
    rig.Check(ref);
    CHECK(rig.outs[0].lastList == Render(Coefficients(ref).data(), 5));
    CHECK(obj.Plane().coefficients(0).b2 == doctest::Approx(0.5));

    Send(obj, "zero 1 -1 0");
    ref.moveZero(1, -1.0, 0.0);
    rig.Check(ref);

    // order and pconstrain send nothing.
    const int sent = rig.outs[0].count;
    Send(obj, "order 4");
    Send(obj, "pconstrain 1");
    CHECK(rig.outs[0].count == sent);
    ref.order(4).constrainPoles(true);
    CHECK(obj.Plane().order() == 4);
    CHECK(obj.Plane().constrainPoles());

    // The second section's pole, pulled inside the unit circle.
    Send(obj, "pole 3 0 1.5");
    ref.movePole(3, 0.0, 1.5);
    rig.Check(ref);
    CHECK(std::abs(obj.Plane().pole(2)) == doctest::Approx(zPlane::MAX_POLE_RADIUS));

    // Indices past the order, missing or extra numbers, unknown words and the
    // display messages are ignored.
    const int now = rig.outs[0].count;
    Send(obj, "pole 4 0 0");
    Send(obj, "pole -1 0 0");
    Send(obj, "zero 0 1");
    Send(obj, "zero 0 1 2 3");
    Send(obj, "pole 0 a b");
    Send(obj, "logscale 1");
    Send(obj, "range 2");
    Send(obj, "dictionary design1");
    Send(obj, "order");
    CHECK(rig.outs[0].count == now);
    CHECK(obj.Plane().order() == 4);
    CHECK(obj.Dropped() == 0);
  }

  TEST_CASE("~zplane: the GUI value is the sections with their roots, cell by cell (#1103)") {
    dZplane obj;
    Send(obj, "1 -1 0.5 -0.5 0.25 2 0 0 0.9 0");
    Send(obj, "pconstrain 1");
    CHECK(obj.GetGuiValueCount() == 3);
    CHECK(obj.GetGuiValueAt(0) == "2 1");
    const zPlane plane = obj.Plane();
    for (unsigned int s = 0; s < 2; ++s) {
      CAPTURE(s);
      const zPlane::coefficientSet c = plane.coefficients(s);
      const float values[] = {c.a0,
                              c.a1,
                              c.a2,
                              c.b1,
                              c.b2,
                              static_cast<float>(plane.zero(2 * s).real()),
                              static_cast<float>(plane.zero(2 * s).imag()),
                              static_cast<float>(plane.zero(2 * s + 1).real()),
                              static_cast<float>(plane.zero(2 * s + 1).imag()),
                              static_cast<float>(plane.pole(2 * s).real()),
                              static_cast<float>(plane.pole(2 * s).imag()),
                              static_cast<float>(plane.pole(2 * s + 1).real()),
                              static_cast<float>(plane.pole(2 * s + 1).imag())};
      CHECK(obj.GetGuiValueAt(s + 1) == Render(values, 13));
    }
    // Section 0's zeroes 0.5 +- 0.5i and poles 0.25 +- 0.433i.
    const float prefix[] = {1.f, -1.f, 0.5f, -0.5f, 0.25f, 0.5f, 0.5f, 0.5f, -0.5f, 0.25f};
    CHECK(obj.GetGuiValueAt(1).rfind(Render(prefix, 10) + " ", 0) == 0);
    CHECK(obj.GetGuiValueAt(3).empty());
    CHECK(obj.GetGuiValue() ==
          obj.GetGuiValueAt(0) + " " + obj.GetGuiValueAt(1) + " " + obj.GetGuiValueAt(2));
  }

  TEST_CASE("~zplane: the arguments are order and pconstrain, saved as given (#1103)") {
    dZplane obj;
    const std::string args = "order 6 pconstrain 1";
    obj.SetParams(args);
    CHECK(obj.GetParams() == args);
    CHECK(obj.Plane().order() == 6);
    CHECK(obj.Plane().constrainPoles());
    Send(obj, "order 2");
    CHECK(obj.GetParams() == args);
    obj.SetParams("");
    CHECK(obj.Plane().order() == 2);
    CHECK_FALSE(obj.Plane().constrainPoles());
    obj.SetParams("order 99 color red pconstrain");
    CHECK(obj.Plane().sections() == zPlane::MAX_SECTIONS);
    CHECK_FALSE(obj.Plane().constrainPoles());
  }

  TEST_CASE("~zplane -> ~cascade: in a rendered patch, roots moved through a receive filter the "
            "signal, and the sections and pconstrain survive a JSON round trip (#1103)") {
    std::string dump;
    std::string gui;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "plane1103");
      YSE::pHandle* plane = src.CreateObject(YSE::OBJ::D_ZPLANE, "order 4");
      YSE::pHandle* filter = src.CreateObject(YSE::OBJ::D_CASCADE, "8");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(plane != nullptr);
      REQUIRE(filter != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, filter, 0);
      src.Connect(cmd, 0, plane, 0);
      src.Connect(plane, 0, filter, 1);
      src.Connect(filter, 0, dac, 0);
      // The roots as a host would drag them, through the receive.
      REQUIRE(src.PassData("pole 0 0.6 0.3", "plane1103", YSE::T_GUI));
      REQUIRE(src.PassData("zero 2 -1 0", "plane1103", YSE::T_GUI));
      REQUIRE(src.PassData("pole 2 0.5 0", "plane1103", YSE::T_GUI));
      REQUIRE(src.PassData("pconstrain 1", "plane1103", YSE::T_GUI));
      // Values through a receive are delivered at the next block.
      src.Calculate(YSE::T_DSP);
      CHECK(plane->GetGuiValueAt(0) == "2 1");
      gui = plane->GetGuiValue();
      dump = src.DumpJSON();
    }

    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* plane = nullptr;
    for (unsigned int i = 0; i < 5; ++i) {
      YSE::pHandle* h = dst.GetHandleFromList(i);
      if (h != nullptr && std::string(h->Type()) == "~zplane") plane = h;
    }
    REQUIRE(plane != nullptr);
    CHECK(plane->GetGuiValue() == gui);
    CHECK(plane->GetGuiValueAt(0) == "2 1");

    zPlane ref;
    ref.order(4);
    // The numbers as the message carried them, in float.
    ref.movePole(0, 0.6f, 0.3f);
    ref.moveZero(2, -1.0, 0.0);
    ref.movePole(2, 0.5, 0.0);
    ref.constrainPoles(true);
    phasor rampRef;
    biquadCascade reference(8);
    // A bang through the receive sends the restored sections to ~cascade.
    REQUIRE(dst.PassData("bang", "plane1103", YSE::T_GUI));
    std::vector<float> c = Transported(ref);
    reference.coefficients(c.data(), static_cast<UInt>(c.size()));
    for (int block = 0; block < 8; ++block) {
      CAPTURE(block);
      if (block == 4) {
        // A drag past the unit circle, held inside it by the restored
        // pconstrain.
        REQUIRE(dst.PassData("pole 1 0 -2", "plane1103", YSE::T_GUI));
        ref.movePole(1, 0.0, -2.0);
        c = Transported(ref);
        reference.coefficients(c.data(), static_cast<UInt>(c.size()));
      }
      buffer& ramped = rampRef(375.f, N);
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(ramped), 0.f));
    }
    CHECK(std::abs(ref.pole(0)) == doctest::Approx(zPlane::MAX_POLE_RADIUS));
    // The filter is not the identity: the output is not the ramp.
    buffer& ramped = rampRef(375.f, N);
    dst.Calculate(YSE::T_DSP);
    CHECK_FALSE(TestHelpers::buffersNearlyEqual(dst.output[0], ramped, 1e-3f));
  }

  TEST_CASE("~zplane: messages on the audio thread allocate nothing (#1103)") {
    dZplane obj;
    CountingSink outs[4];
    for (int i = 0; i < 4; ++i)
      TestHelpers::Wire(obj, i, outs[i], 0);
    std::string cascade;
    for (unsigned int i = 0; i < zPlane::MAX_SECTIONS; ++i)
      cascade += "0.5 0.1 0.2 -0.3 0.04 ";
    const std::string pole = "pole 17 0.3 0.4";
    const std::string zero = "zero 46 -1 0";
    const std::string order = "order 48";
    const std::string constrain = "pconstrain 1";
    const std::string one = "0.25";
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetList(order, YSE::T_DSP);
      obj.GetInlet(0)->SetList(constrain, YSE::T_DSP);
      obj.GetInlet(0)->SetList(cascade, YSE::T_DSP);
      obj.GetInlet(0)->SetList(pole, YSE::T_DSP);
      obj.GetInlet(0)->SetList(zero, YSE::T_DSP);
      obj.GetInlet(3)->SetFloat(0.1f, YSE::T_DSP);
      obj.GetInlet(4)->SetList(one, YSE::T_DSP);
      obj.GetInlet(0)->SetBang(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(outs[0].count == 5);
    CHECK(outs[1].count == 6);
    CHECK(obj.Dropped() == 0);
  }

} // TEST_SUITE("patcher")
