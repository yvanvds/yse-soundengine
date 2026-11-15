// Tests for the ~scope patcher object (patcher/analysis/dScope), MSP's scope~,
// issue #1169. The capture is DSP::scopeCapture's and is tested in
// Tests/dsp/test_scope_capture.cpp; these show that the inlets, numbers,
// messages and arguments reach it, that a cord on the right inlet makes it
// X/Y, that the GUI value is the newest finished trace in the cell shape the
// header documents, that a host polling from its own threads only ever sees
// whole traces, and that a rendered patch captures after a JSON round trip.

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/analysis/dScope.h"
#include "patcher/generatorObjects/dSig.h"
#include "dsp/analysis/scopeCapture.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::ScopedSampleRate;
using TestHelpers::Wire;
using YSE::DSP::buffer;
using YSE::DSP::scopeCapture;
using YSE::PATCHER::dScope;
using YSE::PATCHER::dSig;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  std::vector<float> Cells(const std::string& value) {
    std::vector<float> all;
    std::istringstream in(value);
    std::string token;
    while (in >> token)
      all.push_back(std::stof(token));
    return all;
  }

  // The bulk read, checked against the per-cell reads of the same trace: cell
  // 0 takes the newest, which the bulk read already took, so they agree.
  std::vector<float> Cells(YSE::PATCHER::pObject& o) {
    const std::vector<float> all = Cells(o.GetGuiValue());
    REQUIRE(all.size() >= 3);
    CHECK(std::stof(o.GetGuiValueAt(0)) == all[0]);
    REQUIRE(all.size() == o.GetGuiValueCount());
    for (unsigned int i = 1; i < o.GetGuiValueCount(); ++i)
      CHECK(std::stof(o.GetGuiValueAt(i)) == all[i]);
    CHECK(o.GetGuiValueAt(o.GetGuiValueCount()).empty());
    return all;
  }

  // The cells a trace of DSP::scopeCapture shows as.
  std::vector<float> Expected(const scopeCapture::trace& t) {
    std::vector<float> all{t.mode == scopeCapture::XY ? 1.f : 0.f, static_cast<float>(t.size),
                           static_cast<float>(t.serial)};
    for (UInt i = 0; i < t.size; ++i) {
      all.push_back(t.first[i]);
      all.push_back(t.second[i]);
    }
    return all;
  }

  // A ~scope driven a block at a time. With `xy` a ~sig is wired into the
  // left inlet, so it waits for its buffer and the right inlet's arrives
  // first; the ~sig itself never runs.
  struct Rig {
    dSig source;
    dScope scope;
    bool xy;

    explicit Rig(bool withY = false) : xy(withY) {
      if (xy) Wire(source, 0, scope, 0);
    }
    void Block(buffer& x) {
      REQUIRE_FALSE(xy);
      scope.ResetDSP();
      scope.GetInlet(0)->SetBuffer(&x, YSE::T_DSP); // the only input: runs CALC()
    }
    void Block(buffer& x, buffer& y) {
      REQUIRE(xy);
      scope.ResetDSP();
      scope.GetInlet(1)->SetBuffer(&y, YSE::T_DSP);
      scope.GetInlet(0)->SetBuffer(&x, YSE::T_DSP); // the last input: runs CALC()
    }
    void Send(const std::string& message) {
      scope.GetInlet(0)->SetMessage(message, YSE::T_GUI);
    }
  };

  // Blocks of the running sample index, from `from`.
  void Index(buffer& b, unsigned int from) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = static_cast<float>(from + i);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~scope: registry and shape (#1169)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_SCOPE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~scope");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 0);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->IsDSPInput(1));
    CHECK(obj->GetGuiValueCount() == 3);
    CHECK_FALSE(obj->GuiValueIsSettable());

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_SCOPE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ANALYSIS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 5);

    // No trace yet.
    Rig rig;
    CHECK(Cells(rig.scope) == std::vector<float>{0.f, 0.f, 0.f});
  }

  TEST_CASE("~scope: the GUI value is the newest waveform trace, as DSP::scopeCapture "
            "captures it (#1169)") {
    ScopedSampleRate rate(48000);
    Rig rig;
    rig.scope.SetParams("8 4"); // 32 samples a trace
    scopeCapture reference;
    reference.size(4).period(8);
    buffer in(N);
    for (unsigned int k = 0; k < 6; ++k) {
      for (unsigned int i = 0; i < N; ++i)
        in.getPtr()[i] = static_cast<float>(
            0.7 * std::sin(2.0 * 3.14159265358979323846 * 1000.0 * (k * N + i) / 48000.0));
      rig.Block(in);
      reference(in);
      const std::vector<float> cells = Cells(rig.scope);
      CHECK(cells == Expected(reference.acquire()));
      CHECK(cells[2] == static_cast<float>(4 * (k + 1))); // four traces a block
      CHECK(cells.size() == 3 + 8);
    }

    // Per-cell reads: cell 0 moves to the newest trace, and the count and the
    // other cells follow it.
    Index(in, 0);
    rig.Block(in);
    CHECK(rig.scope.GetGuiValueCount() == 3 + 8);
    CHECK(rig.scope.GetGuiValueAt(2) == "24"); // still the trace cell 0 last took
    CHECK(rig.scope.GetGuiValueAt(0) == "0");
    CHECK(rig.scope.GetGuiValueAt(2) == "28");
    CHECK(std::stof(rig.scope.GetGuiValueAt(3)) == 96.f); // the min of 96..103
    CHECK(std::stof(rig.scope.GetGuiValueAt(4)) == 103.f);
    CHECK(rig.scope.GetGuiValueAt(11).empty());
  }

  TEST_CASE("~scope: a cord on the right inlet makes it X/Y (#1169)") {
    Rig rig(true);
    rig.scope.SetParams("16 4"); // 64 samples a trace
    buffer x(N);
    buffer y(N);
    Index(x, 0);
    for (unsigned int i = 0; i < N; ++i)
      y.getPtr()[i] = 0.5f * x.getPtr()[i];
    rig.Block(x, y);
    const std::vector<float> cells = Cells(rig.scope);
    CHECK(cells ==
          std::vector<float>{1.f, 4.f, 2.f, 64.f, 32.f, 80.f, 40.f, 96.f, 48.f, 112.f, 56.f});

    // Without the cord, a waveform again.
    Rig wave;
    wave.scope.SetParams("16 4");
    wave.Block(x);
    CHECK(Cells(wave.scope) ==
          std::vector<float>{0.f, 4.f, 2.f, 64.f, 79.f, 80.f, 95.f, 96.f, 111.f, 112.f, 127.f});
  }

  TEST_CASE("~scope: numbers set calccount and bufsize; the messages set every setting, "
            "the delay at the live sample rate (#1169)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    buffer in(N);
    unsigned int n = 0; // the running sample index fed in
    auto block = [&rig, &in, &n]() {
      Index(in, n);
      n += N;
      rig.Block(in);
      return Cells(rig.scope);
    };

    // Left int: calccount; right int: bufsize. 2 * 8 = 16 samples a trace.
    rig.scope.GetInlet(0)->SetInt(2, YSE::T_GUI);
    rig.scope.GetInlet(1)->SetInt(8, YSE::T_GUI);
    std::vector<float> cells = block(); // 0..127
    REQUIRE(cells.size() == 3 + 16);
    CHECK(cells[2] == 8.f);
    CHECK(cells[3] == 112.f); // the newest trace: 112..127
    CHECK(cells[4] == 113.f);

    // Floats count by their whole part; NaN is ignored. 8 samples a trace.
    rig.scope.GetInlet(0)->SetFloat(4.9f, YSE::T_GUI);
    rig.scope.GetInlet(0)->SetFloat(std::nanf(""), YSE::T_GUI);
    rig.scope.GetInlet(1)->SetFloat(2.5f, YSE::T_GUI);
    rig.scope.GetInlet(1)->SetFloat(std::nanf(""), YSE::T_GUI);
    cells = block(); // 128..255
    REQUIRE(cells.size() == 3 + 4);
    CHECK(cells[3] == 248.f);
    CHECK(cells[4] == 251.f);
    CHECK(cells[6] == 255.f);

    // calccount is held to 2 or more; 'bufsize' sets the points.
    rig.scope.GetInlet(0)->SetInt(1, YSE::T_GUI);
    rig.Send("bufsize 3");
    cells = block(); // 256..383, 6 samples a trace
    REQUIRE(cells.size() == 3 + 6);
    CHECK(cells[3] == 376.f);
    CHECK(cells[4] == 377.f);

    // The messages; a missing or malformed number is ignored.
    rig.Send("calccount 4");
    rig.Send("bufsize 2");
    rig.Send("bufsize");
    rig.Send("calccount x");
    cells = block(); // 384..511, 8 samples a trace
    REQUIRE(cells.size() == 3 + 4);
    CHECK(cells[3] == 504.f);

    // A delay of 8 ms is 8 samples at 1000 Hz: a trace every 16 samples,
    // from 512.
    rig.Send("delay 8");
    CHECK(block()[3] == 624.f); // 512..639
    CHECK(block()[3] == 752.f); // 640..767
    // At 2000 Hz it is 16 samples: a trace every 24, from 768.
    YSE::SAMPLERATE = 2000;
    CHECK(block()[3] == 888.f); // 768..895
    YSE::SAMPLERATE = 1000;

    // The trigger: rising through 1000.5 starts the trace at 1001. A new
    // bufsize drops the wait in progress.
    rig.Send("delay 0");
    rig.Send("trigger 1");
    rig.Send("triglevel 1000.5");
    rig.Send("bufsize 4"); // 16 samples a trace
    cells = block(); // 896..1023
    REQUIRE(cells.size() == 3 + 8);
    CHECK(cells[3] == 1001.f);
    const float serial = cells[2];
    CHECK(block()[2] == serial); // 1024..1151: above the level all along
    rig.Send("triglevel 1200.5");
    cells = block(); // 1152..1279
    CHECK(cells[2] == serial + 1.f);
    CHECK(cells[3] == 1201.f);
    // Down never triggers on a rising ramp.
    rig.Send("trigger 2");
    rig.Send("triglevel 1300.5");
    CHECK(block()[2] == serial + 1.f);
  }

  TEST_CASE("~scope: the arguments survive a DumpJSON / ParseJSON round trip (#1169)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_SCOPE, "64 256 5 1 0.25") != nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_SCOPE, "") != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    const char* params[] = {"64 256 5 1 0.25", ""};
    for (unsigned int i = 0; i < 2; ++i) {
      CAPTURE(i);
      YSE::pHandle* h = dst.GetHandleFromID(i);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == "~scope");
      CHECK(h->GetParams() == params[i]);
      CHECK(h->GetGuiValue() == "0 0 0");
    }
  }

  TEST_CASE("~scope: in a rendered patch a triggered waveform and an X/Y figure are "
            "captured, after a JSON round trip (#1169)") {
    ScopedSampleRate rate(48000);
    std::string dump;
    unsigned int waveID = 0;
    unsigned int figureID = 0;
    {
      patcherImplementation src(1, nullptr);
      // ~phasor 375 (128 samples a ramp) -> ~scope, 2 samples a point, 16
      // points, triggered rising through 0.5; a receive sets calccount.
      // ~cycle 375 into both inlets of a second ~scope: a diagonal line.
      YSE::pHandle* control = src.CreateObject(YSE::OBJ::G_RECEIVE, "scope1169");
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* wave = src.CreateObject(YSE::OBJ::D_SCOPE, "2 16 0 1 0.5");
      YSE::pHandle* tone = src.CreateObject(YSE::OBJ::D_CYCLE, "375");
      YSE::pHandle* figure = src.CreateObject(YSE::OBJ::D_SCOPE, "4 32");
      REQUIRE(control != nullptr);
      REQUIRE(ramp != nullptr);
      REQUIRE(wave != nullptr);
      REQUIRE(tone != nullptr);
      REQUIRE(figure != nullptr);
      src.Connect(control, 0, wave, 0);
      src.Connect(ramp, 0, wave, 0);
      src.Connect(tone, 0, figure, 0);
      src.Connect(tone, 0, figure, 1);
      waveID = wave->GetID();
      figureID = figure->GetID();
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* wave = dst.GetHandleFromID(waveID);
    YSE::pHandle* figure = dst.GetHandleFromID(figureID);
    REQUIRE(wave != nullptr);
    REQUIRE(figure != nullptr);
    CHECK(wave->GetParams() == "2 16 0 1 0.5");
    CHECK(figure->GetParams() == "4 32");

    for (int k = 0; k < 8; ++k)
      dst.Calculate(YSE::T_DSP);

    // The waveform starts at the crossing and climbs 2/128 a point.
    std::vector<float> cells = Cells(wave->GetGuiValue());
    REQUIRE(cells.size() == 3 + 32);
    CHECK(cells[0] == 0.f);
    CHECK(cells[2] > 0.f);
    CHECK(cells[3] >= 0.5f);
    CHECK(cells[3] < 0.5f + 1.f / 128.f + 1e-4f);
    for (unsigned int j = 0; j < 16; ++j) {
      CAPTURE(j);
      CHECK(cells[4 + 2 * j] - cells[3 + 2 * j] == doctest::Approx(1.f / 128.f).epsilon(1e-3));
      if (j > 0)
        CHECK(cells[3 + 2 * j] - cells[1 + 2 * j] == doctest::Approx(2.f / 128.f).epsilon(1e-3));
    }

    // The receive sets calccount 4: a point is four samples.
    REQUIRE(dst.PassData(4, "scope1169", YSE::T_GUI));
    for (int k = 0; k < 8; ++k)
      dst.Calculate(YSE::T_DSP);
    cells = Cells(wave->GetGuiValue());
    REQUIRE(cells.size() == 3 + 32);
    CHECK(cells[3] >= 0.5f);
    CHECK(cells[5] - cells[3] == doctest::Approx(4.f / 128.f).epsilon(1e-3));
    CHECK(cells[4] - cells[3] == doctest::Approx(3.f / 128.f).epsilon(1e-3));

    // X against the same Y: every point on the diagonal, the cosine's range.
    cells = Cells(figure->GetGuiValue());
    REQUIRE(cells.size() == 3 + 64);
    CHECK(cells[0] == 1.f);
    CHECK(cells[2] > 0.f);
    float top = -2.f;
    float bottom = 2.f;
    for (unsigned int j = 0; j < 32; ++j) {
      CHECK(cells[3 + 2 * j] == cells[4 + 2 * j]);
      top = std::fmax(top, cells[3 + 2 * j]);
      bottom = std::fmin(bottom, cells[3 + 2 * j]);
    }
    CHECK(top > 0.99f);
    CHECK(bottom < -0.99f);
  }

  TEST_CASE("~scope: host threads polling while the audio thread captures only ever see "
            "whole traces (#1169)") {
    // One trace a block, of the block's number: a trace whose points are not
    // all its serial was torn. Two host threads poll at once, one by the bulk
    // read and one cell by cell.
    Rig rig;
    rig.scope.SetParams("32 4");
    std::atomic<bool> done{false};
    std::atomic<unsigned int> torn{0};
    std::atomic<unsigned int> reads{0};
    std::thread bulk([&] {
      while (!done.load(std::memory_order_acquire)) {
        const std::vector<float> cells = Cells(rig.scope.GetGuiValue());
        if (cells.size() < 3) {
          torn.fetch_add(1);
          continue;
        }
        if (cells[2] == 0.f) continue;
        if (cells.size() != 3 + 8) torn.fetch_add(1);
        for (std::size_t i = 3; i < cells.size(); ++i)
          if (cells[i] != cells[2]) torn.fetch_add(1);
        reads.fetch_add(1);
      }
    });
    std::thread cellwise([&] {
      while (!done.load(std::memory_order_acquire)) {
        (void)rig.scope.GetGuiValueAt(0);
        const unsigned int count = rig.scope.GetGuiValueCount();
        const std::string serial = rig.scope.GetGuiValueAt(2);
        for (unsigned int i = 3; i < count; ++i)
          (void)rig.scope.GetGuiValueAt(i);
        (void)serial;
      }
    });
    buffer in(N);
    for (unsigned int k = 1; k <= 3000; ++k) {
      in = static_cast<float>(k);
      rig.Block(in);
    }
    done.store(true, std::memory_order_release);
    bulk.join();
    cellwise.join();
    CHECK(torn.load() == 0);
    CHECK(reads.load() > 0);
    CHECK(Cells(rig.scope)[2] == 3000.f);
  }

  TEST_CASE("~scope: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1169)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    Rig figure(true);
    const std::string calccount = "calccount 8";
    const std::string bufsize = "bufsize 16";
    const std::string delay = "delay 3";
    const std::string trigger = "trigger 1";
    const std::string triglevel = "triglevel 0.25";
    buffer in(N);
    in = 0.5f;
    buffer y(N);
    y = -0.5f;
    rig.Block(in);
    figure.Block(in, y);
    {
      TestHelpers::ProbeScope probe;
      dScope& s = rig.scope;
      s.GetInlet(0)->SetInt(4, YSE::T_DSP);
      s.GetInlet(0)->SetFloat(6.f, YSE::T_DSP);
      s.GetInlet(1)->SetInt(32, YSE::T_DSP);
      s.GetInlet(1)->SetFloat(8.f, YSE::T_DSP);
      s.GetInlet(0)->SetMessage(calccount, YSE::T_DSP);
      s.GetInlet(0)->SetMessage(bufsize, YSE::T_DSP);
      s.GetInlet(0)->SetMessage(delay, YSE::T_DSP);
      s.GetInlet(0)->SetMessage(trigger, YSE::T_DSP);
      s.GetInlet(0)->SetMessage(triglevel, YSE::T_DSP);
      for (int k = 0; k < 4; ++k) {
        rig.Block(in);
        figure.Block(in, y);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
