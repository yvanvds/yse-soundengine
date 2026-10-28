// Tests for the ~filtergraph patcher object (patcher/filters/dFiltergraph),
// issue #1102. The bands and their coefficients are DSP::filterBands' and are
// tested in Tests/dsp/test_filter_bands.cpp; these show that the messages,
// inlets, outlets, GUI value, arguments and saved state reach it, and that
// the bands drive ~cascade in a rendered patch after a JSON round trip.

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
#include "patcher/filters/dFiltergraph.h"
#include "patcher/math/gExprEval.h"
#include "dsp/filters/biquadCascade.hpp"
#include "dsp/filters/filterBands.hpp"
#include "dsp/generators/phasor.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "utils/json.hpp"
#include "sinks.hpp"

using YSE::DSP::biquadCascade;
using YSE::DSP::buffer;
using YSE::DSP::filterBands;
using YSE::DSP::phasor;
using YSE::PATCHER::dFiltergraph;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  std::string Text(float v) {
    char text[YSE::PATCHER::kExprValueTextMax];
    const int n = YSE::PATCHER::ExprFormatValue(YSE::PATCHER::ExprValue::Float(v), text,
                                                YSE::PATCHER::kExprValueTextMax);
    return std::string(text, static_cast<std::size_t>(n));
  }

  std::string Render(const std::vector<float>& values) {
    std::string out;
    for (const float v : values) {
      if (!out.empty()) out += ' ';
      out += Text(v);
    }
    return out;
  }

  std::vector<float> Sections(const filterBands& bands) {
    std::vector<float> c(filterBands::MAX_BANDS * 5);
    c.resize(bands.sections(c.data(), static_cast<UInt>(c.size())));
    return c;
  }

  std::string SectionsOf(const filterBands& bands) {
    return Render(Sections(bands));
  }

  // The seven outlets, each logging into one shared order.
  struct Rig {
    std::vector<char> order;
    TestHelpers::OrderSink outs[7];
    explicit Rig(dFiltergraph& obj) {
      for (int i = 0; i < 7; ++i) {
        outs[i].log = &order;
        outs[i].tag = static_cast<char>('0' + i);
        TestHelpers::Wire(obj, i, outs[i], 0);
      }
    }
    const std::string& Coefficients() const {
      return outs[0].lastList;
    }
    int Sent() const {
      return outs[0].count;
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

  void Send(dFiltergraph& obj, const std::string& text, int inlet = 0) {
    obj.GetInlet(inlet)->SetList(text, YSE::T_GUI);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~filtergraph: registry and shape (#1102)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_FILTERGRAPH);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~filtergraph");
    CHECK(h->GetInputs() == 8);
    CHECK(h->GetOutputs() == 7);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::LIST);
    for (int i = 1; i <= 4; ++i)
      CHECK(h->OutputDataType(i) == YSE::OUT_TYPE::FLOAT);
    CHECK(h->OutputDataType(5) == YSE::OUT_TYPE::LIST);
    CHECK(h->OutputDataType(6) == YSE::OUT_TYPE::INT);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_FILTERGRAPH));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::FILTER);
    CHECK(o->GetParamCount() == 1);
    CHECK(o->GetParamDocs()[0].name == "settings");
    CHECK(o->GetOutlet(0)->GetDocLabel() == "coefficients");
    CHECK(o->GetOutlet(6)->GetDocLabel() == "band");
    CHECK_FALSE(o->GuiValueIsSettable());
    for (int i = 0; i < 8; ++i) {
      CAPTURE(i);
      const unsigned int accepted = o->GetInlet(i)->GetAcceptedTypes();
      CHECK((accepted & YSE::PATCHER::IT_FLOAT) != 0);
      CHECK((accepted & YSE::PATCHER::IT_LIST) != 0);
    }
    CHECK((o->GetInlet(0)->GetAcceptedTypes() & YSE::PATCHER::IT_BANG) != 0);
  }

  TEST_CASE("~filtergraph: mode words and parameters send the coefficients; set... forms do "
            "not (#1102)") {
    TestHelpers::ScopedSampleRate rate(48000);
    dFiltergraph obj;
    Rig rig(obj);
    filterBands ref;

    Send(obj, "peaknotch 2000 4 3");
    ref.mode(0, filterBands::PEAKNOTCH).parameters(0, 2000.f, 4.f, 3.f);
    REQUIRE(rig.Sent() == 1);
    CHECK(rig.Coefficients() == SectionsOf(ref));

    // Every mode word, by name and by number.
    const char* words[] = {"display",   "lowpass",  "highpass",  "bandpass", "bandstop",
                           "peaknotch", "lowshelf", "highshelf", "resonant", "allpass"};
    for (int m = 9; m >= 0; --m) {
      CAPTURE(m);
      Send(obj, words[m]);
      CHECK(obj.Bands().mode(0) == m);
      Send(obj, "mode 1");
      CHECK(obj.Bands().mode(0) == filterBands::LOWPASS);
    }
    ref.mode(0, filterBands::LOWPASS);
    CHECK(rig.Coefficients() == SectionsOf(ref));
    const int sent = rig.Sent();
    CHECK(sent == 21);

    // setparams, edit_* and set on the frequency inlet change without sending.
    Send(obj, "setparams 500 1 2");
    Send(obj, "edit_amp 3");
    Send(obj, "edit_gainmode 1");
    Send(obj, "set 700 2 5", 5);
    CHECK(rig.Sent() == sent);
    ref.parameters(0, 700.f, 2.f, 5.f).gainEnabled(0, true);
    Send(obj, "bang");
    CHECK(rig.Coefficients() == SectionsOf(ref));

    // The parameter inlets send; a list of three on inlet 5 is params; ints
    // are floats.
    obj.GetInlet(5)->SetFloat(1500.f, YSE::T_GUI);
    obj.GetInlet(6)->SetInt(3, YSE::T_GUI);
    obj.GetInlet(7)->SetFloat(0.5f, YSE::T_GUI);
    ref.parameters(0, 1500.f, 3.f, 0.5f);
    CHECK(rig.Coefficients() == SectionsOf(ref));
    Send(obj, "800 0.5 2", 5);
    ref.parameters(0, 800.f, 0.5f, 2.f);
    CHECK(rig.Coefficients() == SectionsOf(ref));
    Send(obj, "2", 7);
    ref.q(0, 2.f);
    CHECK(rig.Coefficients() == SectionsOf(ref));
    Send(obj, "gainmode 0");
    ref.gainEnabled(0, false);
    CHECK(rig.Coefficients() == SectionsOf(ref));

    // A coefficient inlet does nothing outside display mode.
    const int before = rig.Sent();
    obj.GetInlet(1)->SetFloat(0.3f, YSE::T_GUI);
    obj.GetInlet(0)->SetFloat(0.3f, YSE::T_GUI);
    CHECK(rig.Sent() == before);

    // Unknown words, words without their numbers and dropped messages are
    // ignored.
    Send(obj, "analog");
    Send(obj, "markers 100 1000");
    Send(obj, "highorder 1 0 0 1 0");
    Send(obj, "params 1 2");
    Send(obj, "mode 12");
    Send(obj, "options 0 1");
    Send(obj, "lowpass abc");
    CHECK(rig.Sent() == before);
    CHECK(obj.Bands().mode(0) == filterBands::LOWPASS);
    CHECK(obj.Dropped() == 0);
  }

  TEST_CASE("~filtergraph: a bang sends the band's index, bandwidth, Q, gain and frequency, "
            "right to left, then the coefficients (#1102)") {
    TestHelpers::ScopedSampleRate rate(44100);
    dFiltergraph obj;
    Rig rig(obj);
    Send(obj, "nfilters 3");
    Send(obj, "setfilter 2");
    Send(obj, "highshelf 4000 2 1");
    rig.order.clear();
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.order == std::vector<char>{'6', '4', '3', '2', '1', '0'});
    CHECK(rig.outs[6].lastInt == 2);
    CHECK(rig.outs[4].lastFloat == doctest::Approx(4000.f));
    CHECK(rig.outs[3].lastFloat == 1.f);
    CHECK(rig.outs[2].lastFloat == 2.f);
    CHECK(rig.outs[1].lastFloat == 4000.f);
    filterBands ref;
    ref.bands(3).mode(2, filterBands::HIGHSHELF).parameters(2, 4000.f, 2.f, 1.f);
    CHECK(rig.Coefficients() == SectionsOf(ref));

    // In display mode only the coefficients; the word bang through a
    // receive is a bang.
    Send(obj, "display");
    rig.order.clear();
    Send(obj, "bang");
    CHECK(rig.order == std::vector<char>{'0'});
  }

  TEST_CASE("~filtergraph: coefficient lists, cascades and display mode (#1102)") {
    TestHelpers::ScopedSampleRate rate(48000);
    dFiltergraph obj;
    Rig rig(obj);

    // A list of numbers is a cascade, sent back as given.
    Send(obj, "1 0.5 0.25 -0.5 0.125 0.5 0 0 0 0");
    CHECK(rig.Coefficients() ==
          Render({1.f, 0.5f, 0.25f, -0.5f, 0.125f, 0.5f, 0.f, 0.f, 0.f, 0.f}));
    CHECK(obj.Bands().bands() == 2);
    CHECK(obj.Bands().mode(1) == filterBands::DISPLAY);
    Send(obj, "cascade 0.25 0 0 0 0");
    CHECK(rig.Coefficients() == Render({0.25f, 0.f, 0.f, 0.f, 0.f}));
    CHECK(obj.Bands().bands() == 1);
    // set: the same without sending.
    const int sent = rig.Sent();
    Send(obj, "set 2 0 0 0 0");
    CHECK(rig.Sent() == sent);

    // In display mode each coefficient inlet sets its coefficient and sends.
    obj.GetInlet(0)->SetFloat(0.75f, YSE::T_GUI);
    CHECK(rig.Coefficients() == Render({0.75f, 0.f, 0.f, 0.f, 0.f}));
    obj.GetInlet(1)->SetFloat(0.1f, YSE::T_GUI);
    obj.GetInlet(2)->SetInt(0, YSE::T_GUI);
    obj.GetInlet(3)->SetFloat(-0.2f, YSE::T_GUI);
    Send(obj, "0.3", 4);
    CHECK(rig.Coefficients() == Render({0.75f, 0.1f, 0.f, -0.2f, 0.3f}));
    Send(obj, "2");
    CHECK(rig.Coefficients() == Render({2.f, 0.1f, 0.f, -0.2f, 0.3f}));
    // Lists that are not whole sets of numbers are not read.
    Send(obj, "1 2 3");
    Send(obj, "1 2 3 4 five");
    CHECK(rig.Coefficients() == Render({2.f, 0.1f, 0.f, -0.2f, 0.3f}));

    // Into display from a filter mode keeps the coefficients it had.
    Send(obj, "bandpass 1000 1 4");
    // A copy on purpose: the sink's list is overwritten by the next send.
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    const std::string bandpass = rig.Coefficients();
    Send(obj, "display");
    CHECK(rig.Coefficients() == bandpass);
    Send(obj, "1500", 5);
    CHECK(rig.Coefficients() == bandpass);
  }

  TEST_CASE("~filtergraph: bands, options, constraints, flat, selection and query (#1102)") {
    TestHelpers::ScopedSampleRate rate(48000);
    dFiltergraph obj;
    Rig rig(obj);
    filterBands ref;

    Send(obj, "nfilters 4");
    ref.bands(4);
    Send(obj, "params 2 300 2 1");
    ref.parameters(2, 300.f, 2.f, 1.f);
    CHECK(rig.Coefficients() == SectionsOf(ref));
    Send(obj, "options 1 6 1 1 1");
    ref.mode(1, filterBands::LOWSHELF).gainEnabled(1, true).handle(1, true);
    CHECK(rig.Coefficients() == SectionsOf(ref));
    CHECK(obj.Bands().handle(1));
    const int sent = rig.Sent();
    Send(obj, "setoptions 3 5 0 0 0");
    Send(obj, "setconstraints 3 100 200 0 0 0 0");
    CHECK(rig.Sent() == sent);
    ref.mode(3, filterBands::PEAKNOTCH).constraints(3, 100.f, 200.f, 0.f, 0.f, 0.f, 0.f);
    Send(obj, "constraints 0 0 0 0.5 2 0 0");
    ref.constraints(0, 0.f, 0.f, 0.5f, 2.f, 0.f, 0.f);
    CHECK(rig.Coefficients() == SectionsOf(ref));
    CHECK(obj.Bands().frequency(3) == 200.f);
    // An index past the bands in use is not a band.
    Send(obj, "params 4 300 2 1");
    Send(obj, "constraints 9 0 0 0 0 0 0");
    CHECK(rig.Sent() == sent + 1);

    // Edit attributes go to the edited band, clamped by its constraints.
    Send(obj, "whichfilt 3");
    CHECK(obj.Edited() == 3);
    Send(obj, "edit_freq 5000");
    CHECK(obj.Bands().frequency(3) == 200.f);
    Send(obj, "edit_maxfreq 6000");
    Send(obj, "edit_freq 5000");
    CHECK(obj.Bands().frequency(3) == 5000.f);
    Send(obj, "edit_Q 7");
    Send(obj, "edit_minQ 8");
    CHECK(obj.Bands().q(3) == 8.f);
    Send(obj, "edit_mode 2");
    Send(obj, "edit_displaydot 1");
    CHECK(obj.Bands().mode(3) == filterBands::HIGHPASS);
    CHECK(obj.Bands().handle(3));
    Send(obj, "edit_filter 0");
    CHECK(obj.Edited() == 0);
    Send(obj, "setfilter 4");
    CHECK(obj.Edited() == 0);

    // flat, on the bands listed.
    Send(obj, "params 0 1000 1.5 9");
    Send(obj, "flat 0 2 17");
    CHECK(obj.Bands().q(0) == filterBands::FLAT_Q);
    CHECK(obj.Bands().gain(0) == 1.f);
    CHECK(obj.Bands().gain(2) == 1.f);

    // selectfilt is the highlight; nfilters shrinking clamps both.
    Send(obj, "selectfilt 3");
    CHECK(obj.Highlighted() == 3);
    Send(obj, "setfilter 3");
    Send(obj, "nfilters 2");
    CHECK(obj.Edited() == 1);
    CHECK(obj.Highlighted() == -1);
    Send(obj, "selectfilt 1");
    Send(obj, "selectfilt -1");
    CHECK(obj.Highlighted() == -1);

    // query: the gain and phase of all bands together at a frequency.
    Send(obj, "query 1000");
    float amplitude = 0.f;
    float phase = 0.f;
    REQUIRE(obj.Bands().responseAt(1000.f, amplitude, phase));
    CHECK(rig.outs[5].lastList == Text(amplitude) + " " + Text(phase));
    CHECK(obj.Dropped() == 0);
  }

  TEST_CASE("~filtergraph: the GUI value is the bands, cell by cell (#1102)") {
    TestHelpers::ScopedSampleRate rate(48000);
    dFiltergraph obj;
    Send(obj, "nfilters 2");
    Send(obj, "setparams 0 800 2 3");
    Send(obj, "options 1 5 0 1 1");
    Send(obj, "params 1 3000 0.5 2");
    Send(obj, "setfilter 1");
    Send(obj, "selectfilt 0");

    CHECK(obj.GetGuiValueCount() == 3);
    CHECK(obj.GetGuiValueAt(0) == "2 1 0");
    const filterBands bands = obj.Bands();
    for (unsigned int b = 0; b < 2; ++b) {
      CAPTURE(b);
      const filterBands::coefficientSet c = bands.coefficients(b);
      const std::string expect =
          std::to_string(static_cast<int>(bands.mode(b))) + " " +
          Render({bands.frequency(b), bands.gain(b), bands.q(b)}) + " " +
          (bands.gainEnabled(b) ? "1 " : "0 ") + (bands.handle(b) ? "1 " : "0 ") +
          Render({c.a0, c.a1, c.a2, c.b1, c.b2}) + (bands.analog(b) ? " 1" : " 0");
      CHECK(obj.GetGuiValueAt(b + 1) == expect);
    }
    CHECK(obj.GetGuiValueAt(1).rfind("1 " + Render({800.f, 2.f, 3.f}) + " 0 0 ", 0) == 0);
    CHECK(obj.GetGuiValueAt(2).rfind("5 " + Render({3000.f, 0.5f, 2.f}) + " 0 1 ", 0) == 0);
    // The analog flag is the last value of a band's cell (#1209).
    CHECK(bands.analog(1));
    CHECK(obj.GetGuiValueAt(1).substr(obj.GetGuiValueAt(1).size() - 2) == " 0");
    CHECK(obj.GetGuiValueAt(2).substr(obj.GetGuiValueAt(2).size() - 2) == " 1");
    CHECK(obj.GetGuiValueAt(3).empty());
    CHECK(obj.GetGuiValue() ==
          obj.GetGuiValueAt(0) + " " + obj.GetGuiValueAt(1) + " " + obj.GetGuiValueAt(2));
  }

  TEST_CASE("~filtergraph: the arguments are nfilters and autoout, saved as given (#1102)") {
    dFiltergraph obj;
    const std::string args = "nfilters 5 autoout 1";
    obj.SetParams(args);
    CHECK(obj.GetParams() == args);
    CHECK(obj.Bands().bands() == 5);
    CHECK(obj.AutoOut());
    Send(obj, "nfilters 2");
    CHECK(obj.GetParams() == args);
    obj.SetParams("");
    CHECK(obj.Bands().bands() == 1);
    CHECK_FALSE(obj.AutoOut());
    obj.SetParams("nfilters 99 color red autoout");
    CHECK(obj.Bands().bands() == filterBands::MAX_BANDS);
    CHECK_FALSE(obj.AutoOut());
  }

  TEST_CASE("~filtergraph -> ~cascade: in a rendered patch the bands survive a JSON round trip, "
            "autoout sends them at load, edits arrive through a receive (#1102)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    std::string gui;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "graph1102");
      YSE::pHandle* graph = src.CreateObject(YSE::OBJ::D_FILTERGRAPH, "nfilters 2 autoout 1");
      YSE::pHandle* filter = src.CreateObject(YSE::OBJ::D_CASCADE, "8");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(graph != nullptr);
      REQUIRE(filter != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, filter, 0);
      src.Connect(cmd, 0, graph, 0);
      src.Connect(graph, 0, filter, 1);
      src.Connect(filter, 0, dac, 0);
      // The bands as a host would edit them, through the receive.
      REQUIRE(src.PassData("lowshelf 300 2 1", "graph1102", YSE::T_GUI));
      REQUIRE(src.PassData("setfilter 1", "graph1102", YSE::T_GUI));
      REQUIRE(src.PassData("peaknotch 2500 0.25 4", "graph1102", YSE::T_GUI));
      REQUIRE(src.PassData("setconstraints 1 0 3000 0 0 0 0", "graph1102", YSE::T_GUI));
      // Values through a receive are delivered at the next block.
      src.Calculate(YSE::T_DSP);
      CHECK(graph->GetGuiValueAt(0) == "2 1 -1");
      gui = graph->GetGuiValue();
      dump = src.DumpJSON();
    }

    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* graph = nullptr;
    for (unsigned int i = 0; i < 5; ++i) {
      YSE::pHandle* h = dst.GetHandleFromList(i);
      if (h != nullptr && std::string(h->Type()) == "~filtergraph") graph = h;
    }
    REQUIRE(graph != nullptr);
    CHECK(graph->GetGuiValue() == gui);
    CHECK(graph->GetGuiValueAt(0) == "2 1 -1");

    filterBands bands;
    bands.bands(2)
        .mode(0, filterBands::LOWSHELF)
        .parameters(0, 300.f, 2.f, 1.f)
        .mode(1, filterBands::PEAKNOTCH)
        .parameters(1, 2500.f, 0.25f, 4.f);
    phasor rampRef;
    biquadCascade reference(8);
    // autoout: the coefficients reached ~cascade when the patch loaded.
    std::vector<float> c = Sections(bands);
    reference.coefficients(c.data(), static_cast<UInt>(c.size()));
    for (int block = 0; block < 8; ++block) {
      CAPTURE(block);
      if (block == 4) {
        // An edit of the restored edited band (1), clamped by its saved
        // constraint, through the receive.
        REQUIRE(dst.PassData("params 4000 4 2", "graph1102", YSE::T_GUI));
        bands.parameters(1, 3000.f, 4.f, 2.f);
        c = Sections(bands);
        reference.coefficients(c.data(), static_cast<UInt>(c.size()));
      }
      buffer& ramped = rampRef(375.f, N);
      dst.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(ramped), 0.f));
    }
    // The filter is not the identity: the shelf doubles the ramp's DC.
    double sum = 0.0;
    for (unsigned int i = 0; i < N; ++i)
      sum += dst.output[0].getPtr()[i];
    CHECK(sum / N > 0.75);
  }

  TEST_CASE("~filtergraph: messages on the audio thread allocate nothing (#1102)") {
    TestHelpers::ScopedSampleRate rate(48000);
    dFiltergraph obj;
    CountingSink outs[7];
    for (int i = 0; i < 7; ++i)
      TestHelpers::Wire(obj, i, outs[i], 0);
    obj.SetParams("nfilters 24");
    std::string cascade;
    for (unsigned int i = 0; i < filterBands::MAX_BANDS; ++i)
      cascade += "0.5 0.1 0.2 -0.3 0.04 ";
    const std::string mode = "peaknotch 1234.5 2.5 3.5";
    const std::string params = "params 12 400 2 1";
    const std::string constraints = "constraints 3 100 2000 0.5 4 0.1 10";
    const std::string query = "query 1000";
    const std::string flat = "flat 0 1 2 3";
    const std::string triple = "800 2 3";
    const std::string analog = "analog 1";
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetBang(YSE::T_DSP);
      obj.GetInlet(0)->SetList(mode, YSE::T_DSP);
      obj.GetInlet(0)->SetList(analog, YSE::T_DSP);
      obj.GetInlet(0)->SetList(params, YSE::T_DSP);
      obj.GetInlet(0)->SetList(constraints, YSE::T_DSP);
      obj.GetInlet(0)->SetList(query, YSE::T_DSP);
      obj.GetInlet(0)->SetList(flat, YSE::T_DSP);
      obj.GetInlet(5)->SetList(triple, YSE::T_DSP);
      obj.GetInlet(6)->SetFloat(2.f, YSE::T_DSP);
      obj.GetInlet(0)->SetList(cascade, YSE::T_DSP);
      obj.GetInlet(3)->SetFloat(0.1f, YSE::T_DSP);
      obj.GetInlet(0)->SetBang(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(outs[0].count == 11);
    CHECK(outs[5].count == 1);
    CHECK(obj.Dropped() == 0);
  }

  TEST_CASE("~filtergraph: analog, edit_analog and the options flag set the band's analog "
            "design (#1209)") {
    TestHelpers::ScopedSampleRate rate(48000);
    dFiltergraph obj;
    Rig rig(obj);
    filterBands ref;

    Send(obj, "bandpass 12000 1 2");
    ref.mode(0, filterBands::BANDPASS).parameters(0, 12000.f, 1.f, 2.f);
    CHECK_FALSE(obj.Bands().analog(0));
    const std::string cookbook = SectionsOf(ref);
    CHECK(rig.Coefficients() == cookbook);

    // analog sends; the coefficients are the matched design's.
    Send(obj, "analog 1");
    ref.analog(0, true);
    CHECK(obj.Bands().analog(0));
    CHECK(rig.Coefficients() == SectionsOf(ref));
    CHECK(rig.Coefficients() != cookbook);
    Send(obj, "analog 0");
    CHECK_FALSE(obj.Bands().analog(0));
    CHECK(rig.Coefficients() == cookbook);

    // edit_analog sets without sending.
    const int sent = rig.Sent();
    Send(obj, "edit_analog 1");
    CHECK(rig.Sent() == sent);
    CHECK(obj.Bands().analog(0));
    Send(obj, "bang");
    CHECK(rig.Coefficients() == SectionsOf(ref));

    // The fourth number of options / setoptions.
    Send(obj, "nfilters 2");
    ref.bands(2);
    Send(obj, "setoptions 1 5 0 1 0");
    CHECK(rig.Sent() == sent + 1);
    CHECK(obj.Bands().analog(1));
    ref.mode(1, filterBands::PEAKNOTCH).analog(1, true);
    Send(obj, "options 0 3 0 0 0");
    ref.analog(0, false);
    CHECK_FALSE(obj.Bands().analog(0));
    CHECK(rig.Coefficients() == SectionsOf(ref));
    Send(obj, "options 0 3 0 1 0");
    ref.analog(0, true);
    CHECK(rig.Coefficients() == SectionsOf(ref));
    CHECK(obj.Dropped() == 0);
  }

  TEST_CASE("~filtergraph -> ~cascade: analog bands survive a JSON round trip and are rendered "
            "with the matched design; a patch saved without the flag loads with it off "
            "(#1209)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "graph1209");
      YSE::pHandle* graph = src.CreateObject(YSE::OBJ::D_FILTERGRAPH, "nfilters 2 autoout 1");
      YSE::pHandle* filter = src.CreateObject(YSE::OBJ::D_CASCADE, "8");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(ramp != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(graph != nullptr);
      REQUIRE(filter != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(ramp, 0, filter, 0);
      src.Connect(cmd, 0, graph, 0);
      src.Connect(graph, 0, filter, 1);
      src.Connect(filter, 0, dac, 0);
      REQUIRE(src.PassData("bandpass 15000 2 2", "graph1209", YSE::T_GUI));
      REQUIRE(src.PassData("analog 1", "graph1209", YSE::T_GUI));
      // The other two flags are saved as booleans too and must come back.
      REQUIRE(src.PassData("gainmode 1", "graph1209", YSE::T_GUI));
      REQUIRE(src.PassData("setfilter 1", "graph1209", YSE::T_GUI));
      REQUIRE(src.PassData("peaknotch 9000 4 1", "graph1209", YSE::T_GUI));
      REQUIRE(src.PassData("analog 1", "graph1209", YSE::T_GUI));
      REQUIRE(src.PassData("displaydot 1", "graph1209", YSE::T_GUI));
      src.Calculate(YSE::T_DSP);
      dump = src.DumpJSON();
    }

    filterBands analog;
    analog.bands(2)
        .mode(0, filterBands::BANDPASS)
        .parameters(0, 15000.f, 2.f, 2.f)
        .analog(0, true)
        .gainEnabled(0, true)
        .mode(1, filterBands::PEAKNOTCH)
        .parameters(1, 9000.f, 4.f, 1.f)
        .analog(1, true)
        .handle(1, true);
    filterBands cookbook = analog;
    cookbook.analog(0, false).analog(1, false);

    // A patch from before #1209: the same dump without the flag.
    nlohmann::json old = nlohmann::json::parse(dump);
    int removed = 0;
    const auto strip = [&removed](auto& self, nlohmann::json& node) -> void {
      if (node.is_object()) removed += static_cast<int>(node.erase("analog"));
      if (node.is_structured())
        for (auto& child : node)
          self(self, child);
    };
    strip(strip, old);
    CHECK(removed == 2);

    struct expectation {
      std::string json;
      filterBands bands;
    };
    const expectation cases[] = {{dump, analog}, {old.dump(), cookbook}};
    for (const expectation& e : cases) {
      patcherImplementation dst(1, nullptr);
      dst.ParseJSON(e.json);
      YSE::pHandle* graph = nullptr;
      for (unsigned int i = 0; i < 5; ++i) {
        YSE::pHandle* h = dst.GetHandleFromList(i);
        if (h != nullptr && std::string(h->Type()) == "~filtergraph") graph = h;
      }
      REQUIRE(graph != nullptr);
      const bool on = e.bands.analog(0);
      CAPTURE(on);
      const std::string cell = graph->GetGuiValueAt(1);
      CHECK(cell.substr(cell.size() - 2) == (on ? " 1" : " 0"));
      // gainmode on band 0, displaydot on band 1.
      CHECK(cell.rfind("3 " + Render({15000.f, 2.f, 2.f}) + " 1 0 ", 0) == 0);
      CHECK(graph->GetGuiValueAt(2).rfind("5 " + Render({9000.f, 4.f, 1.f}) + " 0 1 ", 0) == 0);

      phasor rampRef;
      biquadCascade reference(8);
      std::vector<float> c = Sections(e.bands);
      reference.coefficients(c.data(), static_cast<UInt>(c.size()));
      for (int block = 0; block < 4; ++block) {
        CAPTURE(block);
        buffer& ramped = rampRef(375.f, N);
        dst.Calculate(YSE::T_DSP);
        CHECK(TestHelpers::buffersNearlyEqual(dst.output[0], reference(ramped), 0.f));
      }
    }
    // The two designs are not the same filter.
    CHECK(SectionsOf(analog) != SectionsOf(cookbook));
  }

} // TEST_SUITE("patcher")
