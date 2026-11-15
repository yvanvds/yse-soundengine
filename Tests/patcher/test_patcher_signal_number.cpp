// Tests for the ~number patcher object (patcher/analysis/dNumber), MSP's
// number~, issue #1167. The ramp is DSP::rampSmooth's and is tested in
// Tests/dsp/test_ramp_smooth.cpp; these show that numbers, lists, the ramp
// time, 'set', bang, the limits and the modes reach it and the GUI value,
// that the monitored input goes out only through the armed outlet (#1037),
// one sample every interval at the live sample rate, and that a rendered
// ~number -> ~number patch ramps and monitors after a JSON round trip.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/analysis/dNumber.h"
#include "dsp/filters/rampSmooth.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using TestHelpers::FloatSink;
using TestHelpers::ScopedSampleRate;
using TestHelpers::Wire;
using YSE::DSP::buffer;
using YSE::DSP::rampSmooth;
using YSE::PATCHER::dNumber;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // A ~number with a sink on each outlet. Block() runs one block and the
  // flush the patcher runs after its traversal, and says whether the monitor
  // outlet sent anything.
  struct Rig {
    BufferSink signal;
    FloatSink monitor;
    dNumber number;

    Rig() {
      Wire(number, 0, signal);
      Wire(number, 1, monitor);
    }
    // One block with nothing connected: the monitor reads silence.
    bool Block() {
      monitor.gotFloat = false;
      number.ResetDSP();
      number.Calculate(YSE::T_DSP);
      number.FlushArmedOutlets(YSE::T_GUI);
      return monitor.gotFloat;
    }
    bool Block(buffer& in) {
      monitor.gotFloat = false;
      number.ResetDSP();
      number.GetInlet(0)->SetBuffer(&in, YSE::T_DSP); // the only input: runs CALC()
      number.FlushArmedOutlets(YSE::T_GUI);
      return monitor.gotFloat;
    }
    float Out(unsigned int i) const {
      REQUIRE(signal.received != nullptr);
      return signal.received->getPtr()[i];
    }
    void Send(const std::string& message) {
      number.GetInlet(0)->SetMessage(message, YSE::T_GUI);
    }
  };

  // The GUI value as numbers.
  std::vector<float> Cells(const std::string& value) {
    std::vector<float> all;
    std::istringstream in(value);
    std::string token;
    while (in >> token)
      all.push_back(std::stof(token));
    return all;
  }

  std::vector<float> Cells(YSE::PATCHER::pObject& o) {
    const std::vector<float> all = Cells(o.GetGuiValue());
    // The bulk read is every cell, in order.
    REQUIRE(all.size() == o.GetGuiValueCount());
    for (unsigned int i = 0; i < o.GetGuiValueCount(); ++i)
      CHECK(std::stof(o.GetGuiValueAt(i)) == all[i]);
    CHECK(o.GetGuiValueAt(o.GetGuiValueCount()).empty());
    return all;
  }

  float GuiFloat(YSE::pHandle* h) {
    return std::stof(h->GetGuiValue());
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~number: registry and shape (#1167)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_NUMBER);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~number");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 2);
    CHECK(obj->IsDSPInput(0));
    CHECK_FALSE(obj->IsDSPInput(1));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(obj->OutputDataType(1) == YSE::OUT_TYPE::FLOAT);
    CHECK(obj->GetGuiValueCount() == 3);
    CHECK_FALSE(obj->GuiValueIsSettable());

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_NUMBER));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ANALYSIS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 4);

    // At rest: 0 shown, monitor mode, both modes allowed.
    dNumber number;
    CHECK(Cells(number) == std::vector<float>{0.f, 2.f, 3.f});
    CHECK(number.Mode() == 2);
  }

  TEST_CASE("~number: a number is the output, ramped over the ramp time like DSP::rampSmooth "
            "(#1167)") {
    ScopedSampleRate rate(1000); // one sample per millisecond
    Rig rig;
    rampSmooth reference;

    // No ramp: a number jumps.
    rig.number.GetInlet(0)->SetFloat(0.25f, YSE::T_GUI);
    rig.Block();
    for (unsigned int i = 0; i < N; ++i)
      CHECK(rig.Out(i) == 0.25f);

    // 10 ms is 10 samples: a straight line that lands on the number, as
    // DSP::rampSmooth draws it.
    rig.number.GetInlet(1)->SetFloat(10.f, YSE::T_GUI);
    rig.number.GetInlet(0)->SetInt(1, YSE::T_GUI);
    rig.Block();
    reference.ramp(1.f);
    reference.process(0.25f);
    reference.ramp(10.f);
    for (unsigned int i = 0; i < N; ++i) {
      CAPTURE(i);
      CHECK(rig.Out(i) == reference.process(1.f));
    }
    CHECK(rig.Out(0) == doctest::Approx(0.325f));
    CHECK(rig.Out(9) == 1.f);

    // A list sets the ramp time first; the right inlet and 'ft1' set it.
    rig.number.GetInlet(0)->SetList("0 20", YSE::T_GUI);
    rig.Block();
    CHECK(rig.Out(9) == doctest::Approx(0.5f));
    CHECK(rig.Out(19) == 0.f);
    rig.Send("ft1 4");
    rig.number.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    rig.Block();
    CHECK(rig.Out(1) == doctest::Approx(0.5f));
    CHECK(rig.Out(3) == 1.f);
    rig.number.GetInlet(1)->SetInt(0, YSE::T_GUI);
    rig.number.GetInlet(0)->SetFloat(-1.f, YSE::T_GUI);
    rig.Block();
    CHECK(rig.Out(0) == -1.f);

    // NaN changes nothing.
    rig.number.GetInlet(0)->SetFloat(std::nanf(""), YSE::T_GUI);
    rig.number.GetInlet(1)->SetFloat(std::nanf(""), YSE::T_GUI);
    rig.Block();
    CHECK(rig.Out(N - 1) == -1.f);

    // Numbers are not saved.
    CHECK(rig.number.GetParams() == "");
  }

  TEST_CASE("~number: the ramp follows the live sample rate (#1167)") {
    Rig rig;
    rig.number.SetParams("10");
    {
      ScopedSampleRate rate(1000);
      rig.number.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
      rig.Block();
      CHECK(rig.Out(9) == 1.f);
      CHECK(rig.Out(8) < 1.f);
    }
    {
      // At twice the rate, 10 ms is twice the samples.
      ScopedSampleRate rate(2000);
      rig.number.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
      rig.Block();
      CHECK(rig.Out(9) == doctest::Approx(0.5f));
      CHECK(rig.Out(18) > 0.f);
      CHECK(rig.Out(19) == 0.f);
    }
  }

  TEST_CASE("~number: 'set' shows without outputting, a bang in output mode outputs, 'min' and "
            "'max' clamp (#1167)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.Send("mode 1");
    rig.number.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    rig.Send("set 0.75");
    rig.Block();
    CHECK(rig.Out(0) == 0.5f);
    CHECK(Cells(rig.number) == std::vector<float>{0.75f, 1.f, 3.f});

    // A bang in output mode outputs the shown number.
    rig.number.GetInlet(0)->SetBang(YSE::T_GUI);
    rig.Block();
    CHECK(rig.Out(0) == 0.75f);

    // In monitor mode a bang does nothing.
    rig.Send("set 0.25");
    rig.Send("mode 2");
    rig.number.GetInlet(0)->SetBang(YSE::T_GUI);
    rig.Block();
    CHECK(rig.Out(0) == 0.75f);

    // The limits clamp numbers, lists and 'set'.
    rig.Send("min -0.5");
    rig.Send("max 0.5");
    rig.number.GetInlet(0)->SetFloat(2.f, YSE::T_GUI);
    rig.Block();
    CHECK(rig.Out(0) == 0.5f);
    rig.number.GetInlet(0)->SetList("-3 0", YSE::T_GUI);
    rig.Block();
    CHECK(rig.Out(0) == -0.5f);
    rig.Send("mode 1");
    rig.Send("set 9");
    CHECK(Cells(rig.number)[0] == 0.5f);

    // A malformed limit changes nothing; no number removes it.
    rig.Send("max abc");
    rig.number.GetInlet(0)->SetInt(3, YSE::T_GUI);
    rig.Block();
    CHECK(rig.Out(0) == 0.5f);
    rig.Send("max");
    rig.number.GetInlet(0)->SetInt(3, YSE::T_GUI);
    rig.Block();
    CHECK(rig.Out(0) == 3.f);
    rig.Send("min");
    rig.number.GetInlet(0)->SetInt(-3, YSE::T_GUI);
    rig.Block();
    CHECK(rig.Out(0) == -3.f);
    CHECK(rig.number.GetParams() == "");
  }

  TEST_CASE("~number: the monitored input goes out the armed outlet every interval (#1167, "
            "#1037)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.number.SetParams("0 50"); // 50 samples: reports due at 0, 50 and 100
    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = static_cast<float>(i);
    rig.number.ResetDSP();
    rig.number.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
    CHECK_FALSE(rig.monitor.gotFloat); // CALC() only arms
    CHECK(rig.number.GetOutlet(1)->Armed());
    rig.number.FlushArmedOutlets(YSE::T_GUI);
    REQUIRE(rig.monitor.gotFloat);
    // Three due in one block are one report: the sample at the last.
    CHECK(rig.monitor.received == 100.f);
    CHECK(Cells(rig.number) == std::vector<float>{100.f, 2.f, 3.f});

    // The next are due at 150, 200 and 250: samples 22, 72 and 122 of the
    // second block.
    REQUIRE(rig.Block(in));
    CHECK(rig.monitor.received == 122.f);

    // 1000 ms. The report already due at 44 of the next block stays; then
    // one every 7.8 blocks: 44, 1044, ... 5044 in 40 blocks.
    rig.Send("interval 1000");
    int reports = 0;
    for (int k = 0; k < 40; ++k)
      reports += rig.Block(in) ? 1 : 0;
    CHECK(reports == 6);

    // Unconnected: silence. It reports in output mode too, and the GUI value
    // then shows the number instead. The next report is due 924 samples on.
    rig.Send("mode 1");
    rig.number.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    for (int k = 0; k < 7; ++k)
      CHECK_FALSE(rig.Block());
    REQUIRE(rig.Block());
    CHECK(rig.monitor.received == 0.f);
    CHECK(Cells(rig.number) == std::vector<float>{0.5f, 1.f, 3.f});
    CHECK(rig.number.GetParams() == "0 50");
  }

  TEST_CASE("~number: the interval is held to 10 - 2000 ms and follows the live sample rate "
            "(#1167)") {
    Rig rig;
    buffer in(N);
    in = 0.f;
    {
      ScopedSampleRate rate(1000);
      rig.number.SetParams("0 1"); // 1 ms is 10: reports at 0, 10, ... 120
      for (unsigned int i = 0; i < N; ++i)
        in.getPtr()[i] = static_cast<float>(i);
      REQUIRE(rig.Block(in));
      CHECK(rig.monitor.received == 120.f);

      // 5000 ms is 2000. The report already due at sample 2 stays; the
      // next is 2000 samples on, in the 15th block after it.
      rig.Send("interval 5000");
      REQUIRE(rig.Block(in));
      CHECK(rig.monitor.received == 2.f);
      for (int k = 0; k < 14; ++k)
        CHECK_FALSE(rig.Block(in));
      REQUIRE(rig.Block(in));
      CHECK(rig.monitor.received == 82.f);
    }
    {
      // At 2000 Hz the default 100 ms is 200 samples: due at 0, then at 72
      // of the second block. At 1000 Hz the first block would report 100.
      ScopedSampleRate rate(2000);
      Rig fresh;
      REQUIRE(fresh.Block(in));
      CHECK(fresh.monitor.received == 0.f);
      REQUIRE(fresh.Block(in));
      CHECK(fresh.monitor.received == 72.f);
    }
  }

  TEST_CASE("~number: 'mode', 'allow', 'monitormode' and 'sigoutmode' (#1167)") {
    Rig rig;
    rig.number.SetParams("0 100 1");
    CHECK(rig.number.Mode() == 1);
    rig.Send("mode 2");
    CHECK(rig.number.Mode() == 2);
    rig.Send("mode 3"); // not a mode
    CHECK(rig.number.Mode() == 2);

    // Output only: the mode gives way and 'mode 2' is refused.
    rig.Send("allow 1");
    CHECK(Cells(rig.number) == std::vector<float>{0.f, 1.f, 1.f});
    rig.Send("mode 2");
    CHECK(rig.number.Mode() == 1);

    // Monitor only, through the attribute halves.
    rig.Send("monitormode 1");
    CHECK(Cells(rig.number)[2] == 3.f);
    rig.Send("sigoutmode 0");
    CHECK(Cells(rig.number) == std::vector<float>{0.f, 2.f, 2.f});
    rig.Send("monitormode 0"); // would leave no mode
    CHECK(Cells(rig.number)[2] == 2.f);
    rig.Send("mode 1");
    CHECK(rig.number.Mode() == 2);
    rig.Send("allow 0");
    rig.Send("allow 4");
    CHECK(Cells(rig.number)[2] == 2.f);
    rig.Send("allow 3");
    rig.Send("mode 1");
    CHECK(rig.number.Mode() == 1);

    // Arguments: 'allow 2' overrides 'mode 1'.
    dNumber monitorOnly;
    monitorOnly.SetParams("0 100 1 2");
    CHECK(monitorOnly.Mode() == 2);
    // Unknown words, 'flags' and 'mouse' change nothing.
    rig.Send("flags 4");
    rig.Send("mouse");
    rig.Send("");
    CHECK(Cells(rig.number) == std::vector<float>{0.f, 1.f, 3.f});
  }

  TEST_CASE("~number: the arguments survive a DumpJSON / ParseJSON round trip (#1167)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_NUMBER, "25 300 1 1") != nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_NUMBER, "") != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    const char* params[] = {"25 300 1 1", ""};
    const std::vector<float> cells[] = {{0.f, 1.f, 1.f}, {0.f, 2.f, 3.f}};
    for (unsigned int i = 0; i < 2; ++i) {
      CAPTURE(i);
      YSE::pHandle* h = dst.GetHandleFromID(i);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == "~number");
      CHECK(h->GetParams() == params[i]);
      CHECK(Cells(h->GetGuiValue()) == cells[i]);
    }
  }

  TEST_CASE("~number: in a rendered patch one ramps and the other monitors it, after a JSON "
            "round trip (#1167)") {
    ScopedSampleRate rate(48000);
    std::string dump;
    unsigned int sourceID = 0;
    unsigned int monitorID = 0;
    unsigned int shownID = 0;
    {
      patcherImplementation src(1, nullptr);
      // 100 ms ramp, output mode: what the patch sends through the receive
      // ramps; the second ~number monitors it every 10 ms.
      YSE::pHandle* control = src.CreateObject(YSE::OBJ::G_RECEIVE, "number1167");
      YSE::pHandle* source = src.CreateObject(YSE::OBJ::D_NUMBER, "100 100 1");
      YSE::pHandle* monitor = src.CreateObject(YSE::OBJ::D_NUMBER, "0 10");
      YSE::pHandle* shown = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      REQUIRE(control != nullptr);
      REQUIRE(source != nullptr);
      REQUIRE(monitor != nullptr);
      REQUIRE(shown != nullptr);
      src.Connect(control, 0, source, 0);
      src.Connect(source, 0, monitor, 0);
      src.Connect(monitor, 1, shown, 0);
      sourceID = source->GetID();
      monitorID = monitor->GetID();
      shownID = shown->GetID();
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* source = dst.GetHandleFromID(sourceID);
    YSE::pHandle* monitor = dst.GetHandleFromID(monitorID);
    YSE::pHandle* shown = dst.GetHandleFromID(shownID);
    REQUIRE(source != nullptr);
    REQUIRE(monitor != nullptr);
    REQUIRE(shown != nullptr);

    dst.Calculate(YSE::T_DSP);
    CHECK(GuiFloat(shown) == 0.f);
    REQUIRE(dst.PassData(0.5f, "number1167", YSE::T_GUI));
    // Halfway: 2400 samples is 18.75 blocks; the report in block 19 is at
    // most a block away from the middle of the ramp.
    std::vector<float> seen;
    for (int k = 0; k < 19; ++k) {
      dst.Calculate(YSE::T_DSP);
      seen.push_back(GuiFloat(shown));
    }
    CHECK(Cells(source->GetGuiValue()) == std::vector<float>{0.5f, 1.f, 3.f});
    CHECK(seen.back() > 0.2f);
    CHECK(seen.back() < 0.3f);
    for (std::size_t i = 1; i < seen.size(); ++i)
      CHECK(seen[i] >= seen[i - 1]);
    for (int k = 0; k < 40; ++k)
      dst.Calculate(YSE::T_DSP);
    CHECK(GuiFloat(shown) == 0.5f);
    CHECK(Cells(monitor->GetGuiValue()) == std::vector<float>{0.5f, 2.f, 3.f});
  }

  TEST_CASE("~number: numbers, messages, blocks and the flush on the audio thread allocate "
            "nothing (#1167)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    buffer in(N);
    in = 0.5f;
    const std::string list = "0.25 10";
    const std::string set = "set 0.5";
    const std::string mode = "mode 1";
    const std::string allow = "allow 3";
    const std::string monitormode = "monitormode 1";
    const std::string min = "min -1";
    const std::string max = "max";
    const std::string interval = "interval 16";
    const std::string ft1 = "ft1 5";
    rig.Block(in);
    {
      TestHelpers::ProbeScope probe;
      dNumber& n = rig.number;
      n.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      n.GetInlet(0)->SetInt(2, YSE::T_DSP);
      n.GetInlet(0)->SetList(list, YSE::T_DSP);
      n.GetInlet(0)->SetMessage(set, YSE::T_DSP);
      n.GetInlet(0)->SetMessage(mode, YSE::T_DSP);
      n.GetInlet(0)->SetMessage(allow, YSE::T_DSP);
      n.GetInlet(0)->SetMessage(monitormode, YSE::T_DSP);
      n.GetInlet(0)->SetMessage(min, YSE::T_DSP);
      n.GetInlet(0)->SetMessage(max, YSE::T_DSP);
      n.GetInlet(0)->SetMessage(interval, YSE::T_DSP);
      n.GetInlet(0)->SetMessage(ft1, YSE::T_DSP);
      n.GetInlet(0)->SetBang(YSE::T_DSP);
      n.GetInlet(1)->SetFloat(20.f, YSE::T_DSP);
      n.GetInlet(1)->SetInt(20, YSE::T_DSP);
      n.ResetDSP();
      n.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      n.FlushArmedOutlets(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(rig.monitor.received == 0.5f);
  }

} // TEST_SUITE("patcher")
