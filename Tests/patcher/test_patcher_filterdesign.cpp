// Tests for the .filterdesign and .filterdetail patcher objects
// (patcher/filters/gFilterdesign, gFilterdetail), issue #1101. The design and
// the analysis are DSP::filterDesign's and DSP::filterResponse's and are
// tested in Tests/dsp/test_filter_design.cpp; these show that the settings,
// arguments and outputs reach them, and that a designed filter drives
// ~cascade and .filterdetail in a rendered patch after a JSON round trip.

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
#include "patcher/filters/gFilterdesign.h"
#include "patcher/filters/gFilterdetail.h"
#include "patcher/math/gExprEval.h"
#include "dsp/filters/biquadCascade.hpp"
#include "dsp/filters/filterDesign.hpp"
#include "dsp/filters/filterResponse.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using YSE::DSP::biquadCascade;
using YSE::DSP::buffer;
using YSE::DSP::filterDesign;
using YSE::DSP::filterResponse;
using YSE::DSP::phasor;
using YSE::PATCHER::gFilterdesign;
using YSE::PATCHER::gFilterdetail;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // Every list that arrives, in order, with a shared log of which sink fired.
  struct ListLog : TestHelpers::SinkBase {
    std::vector<std::string> lists;
    std::vector<char>* order = nullptr;
    char tag = '?';
    ListLog() {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        lists.push_back(v);
        if (order != nullptr) order->push_back(tag);
      });
    }
    const char* Type() const override {
      return "list_log";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // Counts lists and their length without allocating.
  struct CountingSink : TestHelpers::SinkBase {
    int count = 0;
    std::size_t length = 0;
    CountingSink() {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        ++count;
        length = v.size();
      });
    }
    const char* Type() const override {
      return "counting_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // Values as the patcher writes them.
  std::string Render(const std::vector<double>& values, const std::string& word = "") {
    std::string out = word;
    for (const double v : values) {
      char text[YSE::PATCHER::kExprValueTextMax];
      const int n =
          YSE::PATCHER::ExprFormatValue(YSE::PATCHER::ExprValue::Float(static_cast<float>(v)), text,
                                        YSE::PATCHER::kExprValueTextMax);
      if (!out.empty()) out += ' ';
      out.append(text, static_cast<std::size_t>(n));
    }
    return out;
  }

  std::string Render(const float* values, unsigned int count) {
    return Render(std::vector<double>(values, values + count));
  }

  std::string SectionsOf(const filterDesign& d) {
    float c[filterDesign::MAX_SECTIONS * 5];
    const UInt n = d.coefficients(c, filterDesign::MAX_SECTIONS * 5);
    return Render(c, n);
  }

  std::vector<float> Floats(const filterDesign& d) {
    std::vector<float> c(filterDesign::MAX_SECTIONS * 5);
    c.resize(d.coefficients(c.data(), static_cast<UInt>(c.size())));
    return c;
  }

  void Wire(gFilterdesign& obj, ListLog& sections, ListLog& details) {
    TestHelpers::Wire(obj, 0, sections, 0);
    TestHelpers::Wire(obj, 1, details, 0);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE(".filterdesign / .filterdetail: registry and shape (#1101)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* design = p.CreateObject(YSE::OBJ::G_FILTERDESIGN);
    REQUIRE(design != nullptr);
    CHECK(std::string(design->Type()) == ".filterdesign");
    CHECK(design->GetInputs() == 1);
    CHECK(design->GetOutputs() == 2);
    CHECK(design->OutputDataType(0) == YSE::OUT_TYPE::LIST);
    CHECK(design->OutputDataType(1) == YSE::OUT_TYPE::LIST);
    YSE::pHandle* detail = p.CreateObject(YSE::OBJ::G_FILTERDETAIL);
    REQUIRE(detail != nullptr);
    CHECK(std::string(detail->Type()) == ".filterdetail");
    CHECK(detail->GetInputs() == 1);
    CHECK(detail->GetOutputs() == 6);
    for (int i = 0; i < 6; ++i)
      CHECK(detail->OutputDataType(i) == YSE::OUT_TYPE::LIST);

    for (const char* name : {YSE::OBJ::G_FILTERDESIGN, YSE::OBJ::G_FILTERDETAIL}) {
      CAPTURE(name);
      std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(name));
      REQUIRE(o != nullptr);
      CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
      CHECK_FALSE(o->IsDSPObject());
      CHECK(o->GetParamCount() == 1);
    }
    std::unique_ptr<YSE::PATCHER::pObject> d(
        YSE::PATCHER::Register().Get(YSE::OBJ::G_FILTERDESIGN));
    const unsigned int accepted = d->GetInlet(0)->GetAcceptedTypes();
    CHECK((accepted & YSE::PATCHER::IT_BANG) != 0);
    CHECK((accepted & YSE::PATCHER::IT_LIST) != 0);
    CHECK(d->GetOutlet(0)->GetDocLabel() == "sections");
    CHECK(d->GetOutlet(1)->GetDocLabel() == "details");
    CHECK(d->GetParamDocs()[0].name == "settings");
    std::unique_ptr<YSE::PATCHER::pObject> t(
        YSE::PATCHER::Register().Get(YSE::OBJ::G_FILTERDETAIL));
    CHECK(t->GetOutlet(0)->GetDocLabel() == "magnitude");
    CHECK(t->GetOutlet(5)->GetDocLabel() == "step");
    CHECK(t->GetParamDocs()[0].name == "numpoints");
  }

  TEST_CASE(".filterdesign: a bang sends the design's sections; settings set it without "
            "sending (#1101)") {
    TestHelpers::ScopedSampleRate rate(48000);
    ListLog sections;
    ListLog details;
    gFilterdesign obj;
    Wire(obj, sections, details);

    // The defaults: a second-order Butterworth lowpass at 1000 Hz.
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    filterDesign reference;
    REQUIRE(reference.design());
    REQUIRE(sections.lists.size() == 1);
    CHECK(sections.lists[0] == SectionsOf(reference));
    CHECK(details.lists.empty());

    // Every setting, in one list and one at a time, sends nothing.
    obj.GetInlet(0)->SetList("response bandstop topology chebyshev-1 order 5 frequency 300 "
                             "3000 passband_ripple 0.5",
                             YSE::T_GUI);
    obj.GetInlet(0)->SetList("stopband_attenuation 70", YSE::T_GUI);
    CHECK(sections.lists.size() == 1);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    reference.response(filterDesign::RESPONSE::BANDSTOP)
        .topology(filterDesign::TOPOLOGY::CHEBYSHEV_1)
        .order(5)
        .frequency(300.f, 3000.f)
        .passbandRipple(0.5f)
        .stopbandAttenuation(70.f);
    REQUIRE(reference.design());
    REQUIRE(sections.lists.size() == 2);
    CHECK(sections.lists[1] == SectionsOf(reference));
    CHECK(reference.sections() == 3);

    // Units, an explicit sample rate, the other names.
    obj.GetInlet(0)->SetMessage("units normalized", YSE::T_GUI);
    obj.GetInlet(0)->SetMessage("frequency 0.25 response highpass topology chebyshev-2",
                                YSE::T_GUI);
    obj.GetInlet(0)->SetMessage("bang", YSE::T_GUI);
    reference.units(filterDesign::UNITS::NORMALIZED)
        .frequency(0.25f)
        .response(filterDesign::RESPONSE::HIGHPASS)
        .topology(filterDesign::TOPOLOGY::CHEBYSHEV_2);
    REQUIRE(reference.design());
    REQUIRE(sections.lists.size() == 3);
    CHECK(sections.lists[2] == SectionsOf(reference));
    obj.GetInlet(0)->SetList("units radians frequency 1.5 response lowpass topology butterworth",
                             YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    reference.units(filterDesign::UNITS::RADIANS)
        .frequency(1.5f)
        .response(filterDesign::RESPONSE::LOWPASS)
        .topology(filterDesign::TOPOLOGY::BUTTERWORTH);
    REQUIRE(reference.design());
    CHECK(sections.lists.back() == SectionsOf(reference));
    obj.GetInlet(0)->SetList("units hertz samplerate 8000 frequency 1000 response bandpass "
                             "frequency 1000 2000",
                             YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    reference.units(filterDesign::UNITS::HERTZ)
        .sampleRate(8000.f)
        .response(filterDesign::RESPONSE::BANDPASS)
        .frequency(1000.f, 2000.f);
    REQUIRE(reference.design());
    CHECK(sections.lists.back() == SectionsOf(reference));
    CHECK(obj.Settings().sampleRate() == 8000.f);

    // Unknown words and values that do not fit are skipped; the settings
    // around them still apply.
    const std::size_t before = sections.lists.size();
    obj.GetInlet(0)->SetList("color red order 2 response sideways topology elliptic units "
                             "furlongs order abc samplerate -5 passband_ripple 0 frequency",
                             YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    reference.order(2);
    REQUIRE(reference.design());
    REQUIRE(sections.lists.size() == before + 1);
    CHECK(sections.lists.back() == SectionsOf(reference));
    CHECK(obj.Settings().response() == filterDesign::RESPONSE::BANDPASS);
    CHECK(obj.Settings().passbandRipple() == 0.5f);

    // A design that fails sends nothing: 5000 Hz is above 4000.
    obj.GetInlet(0)->SetList("frequency 5000 6000", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(sections.lists.size() == before + 1);
    CHECK(details.lists.empty());
    CHECK(obj.Dropped() == 0);
  }

  TEST_CASE(".filterdesign: zeroes, poles, gain and the transfer function go out the right "
            "outlet, before the sections (#1101)") {
    std::vector<char> order;
    ListLog sections;
    ListLog details;
    gFilterdesign obj;
    Wire(obj, sections, details);
    sections.order = &order;
    sections.tag = 's';
    details.order = &order;
    details.tag = 'd';
    obj.GetInlet(0)->SetList("units normalized frequency 0.2 order 3 zpk_output 1 tf_output 1",
                             YSE::T_GUI);
    CHECK(obj.ZpkOutput());
    CHECK(obj.TfOutput());
    obj.GetInlet(0)->SetBang(YSE::T_GUI);

    filterDesign reference;
    reference.units(filterDesign::UNITS::NORMALIZED).frequency(0.2f).order(3);
    REQUIRE(reference.design());
    std::vector<double> zeros;
    std::vector<double> poles;
    std::vector<double> numerator;
    std::vector<double> denominator;
    for (UInt i = 0; i < reference.poles(); ++i) {
      zeros.push_back(reference.zero(i).real());
      zeros.push_back(reference.zero(i).imag());
      poles.push_back(reference.pole(i).real());
      poles.push_back(reference.pole(i).imag());
    }
    for (UInt i = 0; i <= reference.poles(); ++i) {
      numerator.push_back(reference.numerator(i));
      denominator.push_back(reference.denominator(i));
    }
    REQUIRE(details.lists.size() == 5);
    CHECK(details.lists[0] == Render(zeros, "zeros"));
    CHECK(details.lists[1] == Render(poles, "poles"));
    CHECK(details.lists[2] == Render({reference.gain()}, "gain"));
    CHECK(details.lists[3] == Render(numerator, "numerator"));
    CHECK(details.lists[4] == Render(denominator, "denominator"));
    REQUIRE(sections.lists.size() == 1);
    CHECK(sections.lists[0] == SectionsOf(reference));
    CHECK(order == std::vector<char>{'d', 'd', 'd', 'd', 'd', 's'});

    // Each output on its own; all off sends nothing.
    obj.GetInlet(0)->SetList("sos_output 0 tf_output 0", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(details.lists.size() == 8);
    CHECK(details.lists[7].rfind("gain ", 0) == 0);
    CHECK(sections.lists.size() == 1);
    obj.GetInlet(0)->SetList("zpk_output 0", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(details.lists.size() == 8);
    CHECK(sections.lists.size() == 1);
  }

  TEST_CASE(".filterdesign: the arguments are the starting settings, saved as given (#1101)") {
    TestHelpers::ScopedSampleRate rate(44100);
    ListLog sections;
    ListLog details;
    gFilterdesign obj;
    Wire(obj, sections, details);
    const std::string args = "response highpass order 4 frequency 200 topology chebyshev-1 "
                             "passband_ripple 3 zpk_output 1";
    obj.SetParams(args);
    CHECK(obj.GetParams() == args);
    CHECK(obj.ZpkOutput());
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    filterDesign reference;
    reference.response(filterDesign::RESPONSE::HIGHPASS)
        .order(4)
        .frequency(200.f)
        .topology(filterDesign::TOPOLOGY::CHEBYSHEV_1)
        .passbandRipple(3.f);
    REQUIRE(reference.design());
    REQUIRE(sections.lists.size() == 1);
    CHECK(sections.lists[0] == SectionsOf(reference));
    CHECK(details.lists.size() == 3);

    // A later message is not saved; empty arguments are the defaults.
    obj.GetInlet(0)->SetList("order 8", YSE::T_GUI);
    CHECK(obj.GetParams() == args);
    obj.SetParams("");
    CHECK_FALSE(obj.ZpkOutput());
    CHECK(obj.Settings().order() == 2);
    CHECK(obj.Settings().response() == filterDesign::RESPONSE::LOWPASS);
    // The word bang as a list (text through a receive) is a bang.
    const std::size_t sent = sections.lists.size();
    obj.GetInlet(0)->SetList("bang", YSE::T_GUI);
    CHECK(sections.lists.size() == sent + 1);
    filterDesign defaults;
    REQUIRE(defaults.design());
    CHECK(sections.lists.back() == SectionsOf(defaults));
  }

  TEST_CASE(".filterdetail: a coefficient list sends six curves, right to left (#1101)") {
    std::vector<char> order;
    ListLog outs[6];
    gFilterdetail obj;
    for (int i = 0; i < 6; ++i) {
      outs[i].order = &order;
      outs[i].tag = static_cast<char>('0' + i);
      TestHelpers::Wire(obj, i, outs[i], 0);
    }
    CHECK(obj.Points() == gFilterdetail::MAX_POINTS);

    filterDesign d;
    d.units(filterDesign::UNITS::NORMALIZED).frequency(0.3f).order(4);
    REQUIRE(d.design());
    const std::vector<float> c = Floats(d);
    obj.GetInlet(0)->SetList(SectionsOf(d), YSE::T_GUI);

    filterResponse reference(gFilterdetail::MAX_POINTS);
    REQUIRE(reference.analyse(c.data(), static_cast<UInt>(c.size())));
    const unsigned int n = reference.points();
    CHECK(order == std::vector<char>{'5', '4', '3', '2', '1', '0'});
    REQUIRE(outs[0].lists.size() == 1);
    CHECK(outs[0].lists[0] == Render(reference.magnitude(), n));
    CHECK(outs[1].lists[0] == Render(reference.phase(), n));
    CHECK(outs[2].lists[0] == Render(reference.phaseDelay(), n));
    CHECK(outs[3].lists[0] == Render(reference.groupDelay(), n));
    CHECK(outs[4].lists[0] == Render(reference.impulse(), n));
    CHECK(outs[5].lists[0] == Render(reference.step(), n));

    // numpoints, as a message and through a receive; one biquad.
    obj.GetInlet(0)->SetMessage("numpoints 16", YSE::T_GUI);
    CHECK(obj.Points() == 16);
    CHECK(outs[0].lists.size() == 1);
    obj.GetInlet(0)->SetList("0 1 0 0 0", YSE::T_GUI);
    REQUIRE(outs[0].lists.size() == 2);
    CHECK(outs[0].lists[1] == Render(std::vector<double>(16, 1.0)));
    std::vector<double> impulse(16, 0.0);
    impulse[1] = 1.0;
    CHECK(outs[4].lists[1] == Render(impulse));
    obj.GetInlet(0)->SetList("numpoints 0", YSE::T_GUI);
    CHECK(obj.Points() == 1);
    obj.GetInlet(0)->SetList("numpoints 100000", YSE::T_GUI);
    CHECK(obj.Points() == gFilterdetail::MAX_POINTS);
    obj.GetInlet(0)->SetList("numpoints nan", YSE::T_GUI);
    CHECK(obj.Points() == gFilterdetail::MAX_POINTS);

    // Nothing for a list without one complete set, a non-finite value or a
    // word; an incomplete trailing set is ignored.
    obj.GetInlet(0)->SetList("1 0 0 0", YSE::T_GUI);
    obj.GetInlet(0)->SetList("1 0 0 nan 0", YSE::T_GUI);
    obj.GetInlet(0)->SetList("dictionary design", YSE::T_GUI);
    CHECK(outs[0].lists.size() == 2);
    obj.GetInlet(0)->SetList("0 1 0 0 0 7 7", YSE::T_GUI);
    CHECK(outs[0].lists.size() == 3);
    CHECK(obj.Dropped() == 0);
  }

  TEST_CASE(".filterdetail: the argument is numpoints, saved as given (#1101)") {
    const auto check = [](const std::string& args, unsigned int points) {
      CAPTURE(args);
      ListLog magnitude;
      gFilterdetail obj;
      TestHelpers::Wire(obj, 0, magnitude, 0);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      CHECK(obj.Points() == points);
      obj.GetInlet(0)->SetList("0.5 0 0 0 0", YSE::T_GUI);
      REQUIRE(magnitude.lists.size() == 1);
      CHECK(magnitude.lists[0] == Render(std::vector<double>(points, 0.5)));
    };
    check("", gFilterdetail::MAX_POINTS);
    check("12", 12);
    check("numpoints 7", 7);
    check("0", 1);
    check("100000", gFilterdetail::MAX_POINTS);
    check("numpoints", gFilterdetail::MAX_POINTS);

    gFilterdetail obj;
    obj.SetParams("9");
    CHECK(obj.Points() == 9);
    obj.SetParams("");
    CHECK(obj.Points() == gFilterdetail::MAX_POINTS);
  }

  TEST_CASE(".filterdesign -> ~cascade and .filterdetail: in a rendered patch, with settings "
            "and bangs from a receive, after a JSON round trip (#1101)") {
    TestHelpers::ScopedSampleRate rate(48000);
    ListLog magnitude;
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "design1101");
      YSE::pHandle* design =
          src.CreateObject(YSE::OBJ::G_FILTERDESIGN, "order 4 frequency 2000 topology chebyshev-1");
      YSE::pHandle* filter = src.CreateObject(YSE::OBJ::D_CASCADE, "8");
      YSE::pHandle* detail = src.CreateObject(YSE::OBJ::G_FILTERDETAIL, "32");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(design != nullptr);
      REQUIRE(filter != nullptr);
      REQUIRE(detail != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, filter, 0);
      src.Connect(cmd, 0, design, 0);
      src.Connect(design, 0, filter, 1);
      src.Connect(design, 0, detail, 0);
      src.Connect(filter, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* detail = nullptr;
    for (unsigned int i = 0; i < 6; ++i) {
      YSE::pHandle* h = dst.GetHandleFromList(i);
      if (h != nullptr && std::string(h->Type()) == ".filterdetail") detail = h;
    }
    REQUIRE(detail != nullptr);
    YSE::pHandle sink(&magnitude);
    dst.Connect(detail, 0, &sink, 0);

    phasor rampRef;
    biquadCascade reference(8);
    filterDesign design;
    design.order(4).frequency(2000.f).topology(filterDesign::TOPOLOGY::CHEBYSHEV_1);
    filterResponse response(32);
    for (int block = 0; block < 10; ++block) {
      CAPTURE(block);
      if (block == 2) {
        // The saved settings, designed.
        REQUIRE(dst.PassData("bang", "design1101", YSE::T_GUI));
        REQUIRE(design.design());
        const std::vector<float> c = Floats(design);
        reference.coefficients(c.data(), static_cast<UInt>(c.size()));
        REQUIRE(response.analyse(c.data(), static_cast<UInt>(c.size())));
      }
      if (block == 5) {
        // New settings, then a bang: a highpass of order 6.
        REQUIRE(dst.PassData("response highpass order 6 frequency 500", "design1101", YSE::T_GUI));
        REQUIRE(dst.PassData("bang", "design1101", YSE::T_GUI));
        design.response(filterDesign::RESPONSE::HIGHPASS).order(6).frequency(500.f);
        REQUIRE(design.design());
        const std::vector<float> c = Floats(design);
        reference.coefficients(c.data(), static_cast<UInt>(c.size()));
        REQUIRE(response.analyse(c.data(), static_cast<UInt>(c.size())));
      }
      buffer& ramped = rampRef(375.f, N);
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(ramped), 0.f));
      if (block == 2 || block == 5) {
        REQUIRE(!magnitude.lists.empty());
        CHECK(magnitude.lists.back() == Render(response.magnitude(), 32));
      }
    }
    REQUIRE(magnitude.lists.size() == 2);
    // A highpass removes the ramp's DC (0.5): the output averages near 0
    // (not exactly in one block, while the transient of the switch decays).
    double sum = 0.0;
    for (unsigned int i = 0; i < N; ++i)
      sum += dst.output[0].getPtr()[i];
    CHECK(std::fabs(sum / N) < 0.1);
  }

  TEST_CASE(".filterdesign / .filterdetail: bangs, settings and lists on the audio thread "
            "allocate nothing (#1101)") {
    CountingSink sections;
    CountingSink details;
    CountingSink curves[6];
    gFilterdesign design;
    gFilterdetail detail;
    TestHelpers::Wire(design, 0, sections, 0);
    TestHelpers::Wire(design, 1, details, 0);
    for (int i = 0; i < 6; ++i)
      TestHelpers::Wire(detail, i, curves[i], 0);
    design.SetParams("zpk_output 1 tf_output 1 order 32 units normalized frequency 0.4");
    filterDesign big;
    big.order(32).units(filterDesign::UNITS::NORMALIZED).frequency(0.4f);
    REQUIRE(big.design());
    const std::string list = SectionsOf(big);
    const std::string settings = "response bandpass frequency 0.1 0.3 topology chebyshev-2";
    const std::string points = "numpoints 200";
    {
      TestHelpers::ProbeScope probe;
      design.GetInlet(0)->SetBang(YSE::T_DSP);
      design.GetInlet(0)->SetList(settings, YSE::T_DSP);
      design.GetInlet(0)->SetBang(YSE::T_DSP);
      detail.GetInlet(0)->SetList(list, YSE::T_DSP);
      detail.GetInlet(0)->SetList(points, YSE::T_DSP);
      detail.GetInlet(0)->SetList(list, YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(sections.count == 2);
    CHECK(details.count == 10);
    CHECK(curves[0].count == 2);
    CHECK(curves[5].count == 2);
    CHECK(sections.length > 0);
  }

} // TEST_SUITE("patcher")
