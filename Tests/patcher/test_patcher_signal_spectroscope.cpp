// Tests for the ~spectroscope patcher object (patcher/analysis/dSpectroscope),
// MSP's spectroscope~, issue #1170. The analysis is DSP::spectrumAnalyzer's
// and is tested in Tests/dsp/test_spectrum_analyzer.cpp; these show that the
// messages and arguments reach it, that inside a ~pfft it takes the pass's
// bins from both inlets, that the GUI value is the newest finished frame in
// the cell shape the header documents, that a host polling from its own
// threads only ever sees whole frames, and that a rendered patch, a plain
// one and one inside a ~pfft, analyses after a JSON round trip.

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
#include "patcher/analysis/dSpectroscope.h"
#include "patcher/generatorObjects/dSig.h"
#include "patcher/spectral/spectralPass.h"
#include "dsp/analysis/spectrumAnalyzer.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::ScopedSampleRate;
using TestHelpers::Wire;
using YSE::DSP::buffer;
using YSE::DSP::spectrumAnalyzer;
using YSE::PATCHER::dSig;
using YSE::PATCHER::dSpectroscope;
using YSE::PATCHER::patcherImplementation;
using YSE::PATCHER::spectralPass;
using YSE::PATCHER::spectralPassScope;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  std::vector<float> Cells(const std::string& value) {
    std::vector<float> all;
    std::istringstream in(value);
    std::string token;
    while (in >> token)
      all.push_back(std::stof(token));
    return all;
  }

  // The bulk read, checked against the per-cell reads of the same frame:
  // cell 0 takes the newest, which the bulk read already took, so they agree.
  std::vector<float> Cells(YSE::PATCHER::pObject& o) {
    const std::vector<float> all = Cells(o.GetGuiValue());
    REQUIRE(all.size() >= 5);
    CHECK(std::stof(o.GetGuiValueAt(0)) == all[0]);
    REQUIRE(all.size() == o.GetGuiValueCount());
    for (unsigned int i = 1; i < o.GetGuiValueCount(); ++i)
      CHECK(std::stof(o.GetGuiValueAt(i)) == all[i]);
    CHECK(o.GetGuiValueAt(o.GetGuiValueCount()).empty());
    return all;
  }

  // A ~spectroscope driven a block at a time. With `both` a ~sig is wired
  // into the left inlet, so it waits for its buffer and the right inlet's
  // arrives first; the ~sig itself never runs.
  struct Rig {
    dSig source;
    dSpectroscope scope;
    bool both;

    explicit Rig(bool withRight = false) : both(withRight) {
      if (both) Wire(source, 0, scope, 0);
    }
    void Block(buffer& x) {
      REQUIRE_FALSE(both);
      scope.ResetDSP();
      scope.GetInlet(0)->SetBuffer(&x, YSE::T_DSP); // the only input: runs CALC()
    }
    void Block(buffer& x, buffer& y) {
      REQUIRE(both);
      scope.ResetDSP();
      scope.GetInlet(1)->SetBuffer(&y, YSE::T_DSP);
      scope.GetInlet(0)->SetBuffer(&x, YSE::T_DSP); // the last input: runs CALC()
    }
    void Send(const std::string& message) {
      scope.GetInlet(0)->SetMessage(message, YSE::T_GUI);
    }
  };

  float Rich(unsigned int n) {
    unsigned int s = n * 2654435761u + 12345u;
    s ^= s >> 13;
    s *= 2246822519u;
    s ^= s >> 16;
    return 0.5f * static_cast<float>(std::sin(0.07 * static_cast<double>(n))) +
           0.3f * (static_cast<float>(s >> 8) / 16777216.f - 0.5f);
  }

  void Fill(buffer& b, unsigned int from) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = Rich(from + i);
  }

  // The bin of the largest value past `from`.
  unsigned int Peak(const std::vector<float>& cells, unsigned int from = 1) {
    unsigned int best = from;
    for (unsigned int j = from; j + 5 < cells.size(); ++j)
      if (cells[5 + j] > cells[5 + best]) best = j;
    return best;
  }

  // One block of `signal` through the patcher as an insert.
  template <class Signal>
  void RenderBlock(patcherImplementation& p, std::size_t& at, Signal&& signal) {
    MULTICHANNELBUFFER io(1);
    io[0].resize(N);
    for (unsigned int i = 0; i < N; ++i)
      io[0].getPtr()[i] = signal(at + i);
    p.ProcessAsInsert(io);
    at += N;
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~spectroscope: registry and shape (#1170)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_SPECTROSCOPE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~spectroscope");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 0);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->IsDSPInput(1));
    CHECK(obj->GetGuiValueCount() == 5);
    CHECK_FALSE(obj->GuiValueIsSettable());

    std::unique_ptr<YSE::PATCHER::pObject> o(
        YSE::PATCHER::Register().Get(YSE::OBJ::D_SPECTROSCOPE));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ANALYSIS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 3);

    // No frame yet.
    Rig rig;
    CHECK(Cells(rig.scope) == std::vector<float>{0.f, 0.f, 0.f, 0.f, 0.f});
  }

  TEST_CASE("~spectroscope: the GUI value is the newest frame, as DSP::spectrumAnalyzer "
            "analyses it (#1170)") {
    ScopedSampleRate rate(6400); // 20 ms is 128 samples: a frame a block
    Rig rig;
    rig.scope.SetParams("20 0 64");
    spectrumAnalyzer reference;
    reference.size(64);
    buffer in(N);
    for (unsigned int k = 0; k < 6; ++k) {
      Fill(in, k * N);
      rig.Block(in);
      reference(in);
      const spectrumAnalyzer::frame f = reference.acquire();
      const std::vector<float> cells = Cells(rig.scope);
      REQUIRE(cells.size() == 5 + 33);
      CHECK(cells[0] == 0.f);
      CHECK(cells[1] == 64.f);
      CHECK(cells[2] == 33.f);
      CHECK(cells[3] == static_cast<float>(k + 1));
      CHECK(cells[4] == 6400.f);
      for (unsigned int j = 0; j < 33; ++j) {
        CAPTURE(j);
        CHECK(cells[5 + j] == doctest::Approx(f.values[j]).epsilon(1e-5));
      }
    }

    // Per-cell reads: cell 0 moves to the newest frame, and the count and the
    // other cells follow it.
    Fill(in, 6 * N);
    rig.Block(in);
    reference(in);
    CHECK(rig.scope.GetGuiValueAt(3) == "6"); // still the frame cell 0 last took
    CHECK(rig.scope.GetGuiValueAt(0) == "0");
    CHECK(rig.scope.GetGuiValueAt(3) == "7");
    const spectrumAnalyzer::frame f = reference.acquire();
    CHECK(std::stof(rig.scope.GetGuiValueAt(5 + 4)) == doctest::Approx(f.values[4]).epsilon(1e-5));
    CHECK(rig.scope.GetGuiValueCount() == 5 + 33);
    CHECK(rig.scope.GetGuiValueAt(5 + 33).empty());
  }

  TEST_CASE("~spectroscope: the messages set the interval, the phase spectrum and the size; "
            "the interval at the live sample rate (#1170)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    rig.scope.SetParams("10 0 16");
    buffer in(N);
    unsigned int n = 0;
    auto block = [&]() {
      Fill(in, n);
      n += N;
      rig.Block(in);
      return Cells(rig.scope);
    };
    // 10 ms at 1000 Hz: 12 frames in the first 128 samples.
    std::vector<float> cells = block();
    CHECK(cells[3] == 12.f);
    CHECK(cells[1] == 16.f);
    // 'interval 64': two more frames a block, at samples 184 and 248; the
    // wait to 184 began at 1000 Hz with the old interval (120 + 10 = 130),
    // so frames at 130, 194.
    rig.Send("interval 64");
    cells = block();
    CHECK(cells[3] == 14.f);
    // At 2000 Hz the interval is 128 samples (from the next wait).
    YSE::SAMPLERATE = 2000;
    cells = block(); // 256..383: the frame at 258 (194 + 64), next wait 128
    CHECK(cells[3] == 15.f);
    CHECK(cells[4] == 2000.f);
    cells = block(); // 384..511: the frame at 386
    CHECK(cells[3] == 16.f);
    YSE::SAMPLERATE = 1000;

    // The phase spectrum and a larger size; a missing or malformed number is
    // ignored.
    rig.Send("interval 10");
    rig.Send("phasespect 1");
    rig.Send("size 33");
    rig.Send("size");
    rig.Send("phasespect x");
    rig.Send("bogus 3");
    cells = block();
    CHECK(cells[0] == 1.f);
    CHECK(cells[1] == 64.f);
    CHECK(cells[2] == 33.f);
    for (unsigned int j = 0; j < 33; ++j) {
      CHECK(cells[5 + j] >= -3.1416f);
      CHECK(cells[5 + j] <= 3.1416f);
    }
    rig.Send("phasespect 0");
    CHECK(block()[0] == 0.f);
  }

  TEST_CASE("~spectroscope: inside a ~pfft it takes the pass's bins from both inlets and "
            "publishes at the frame's last pass (#1170)") {
    ScopedSampleRate rate(1000);
    Rig rig(true);
    rig.scope.SetParams("10"); // 10 samples: every frame of a hop of 128
    static constexpr UInt SIZE = 512;
    static constexpr UInt BINS = SIZE / 2;
    spectralPass pass;
    pass.fftSize = SIZE;
    pass.bins = BINS;
    pass.hop = 128;
    pass.overlap = 4;
    pass.count = N;
    pass.passes = BINS / N;
    std::vector<float> re(BINS);
    std::vector<float> im(BINS);
    for (UInt b = 0; b < BINS; ++b) {
      re[b] = 0.5f * static_cast<float>(b) - 3.f;
      im[b] = 2.f - 0.25f * static_cast<float>(b);
    }
    for (UInt p = 0; p < pass.passes; ++p) {
      pass.pass = p;
      pass.firstBin = p * N;
      buffer x(N);
      buffer y(N);
      for (UInt i = 0; i < N; ++i) {
        x.getPtr()[i] = re[p * N + i];
        y.getPtr()[i] = im[p * N + i];
      }
      spectralPassScope scope(&pass);
      rig.Block(x, y);
      // Published at the last pass only.
      (void)rig.scope.GetGuiValueAt(0); // takes the newest
      CHECK(std::stof(rig.scope.GetGuiValueAt(3)) == (p + 1 == pass.passes ? 1.f : 0.f));
    }
    const std::vector<float> cells = Cells(rig.scope);
    REQUIRE(cells.size() == 5 + BINS);
    CHECK(cells[1] == static_cast<float>(SIZE));
    CHECK(cells[2] == static_cast<float>(BINS));
    for (UInt b = 0; b < BINS; ++b) {
      CAPTURE(b);
      const double edge = b == 0 ? 1.0 : 2.0;
      CHECK(cells[5 + b] == doctest::Approx(std::hypot(re[b], im[b]) * edge / SIZE).epsilon(1e-5));
    }

    // Unconnected, the right inlet reads as silence: the real parts alone.
    Rig left;
    left.scope.SetParams("10");
    for (UInt p = 0; p < pass.passes; ++p) {
      pass.pass = p;
      pass.firstBin = p * N;
      buffer x(N);
      for (UInt i = 0; i < N; ++i)
        x.getPtr()[i] = re[p * N + i];
      spectralPassScope scope(&pass);
      left.Block(x);
    }
    const std::vector<float> real = Cells(left.scope);
    REQUIRE(real.size() == 5 + BINS);
    CHECK(real[5 + 10] == doctest::Approx(std::fabs(re[10]) * 2.0 / SIZE).epsilon(1e-5));
  }

  TEST_CASE("~spectroscope: the arguments survive a DumpJSON / ParseJSON round trip (#1170)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_SPECTROSCOPE, "50 1 1024") != nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_SPECTROSCOPE, "") != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    const char* params[] = {"50 1 1024", ""};
    for (unsigned int i = 0; i < 2; ++i) {
      CAPTURE(i);
      YSE::pHandle* h = dst.GetHandleFromID(i);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == "~spectroscope");
      CHECK(h->GetParams() == params[i]);
      CHECK(h->GetGuiValue() == "0 0 0 0 0");
    }
  }

  TEST_CASE("~spectroscope: in a rendered patch a tone shows in its bin, on a signal and "
            "inside a ~pfft, after a JSON round trip (#1170)") {
    ScopedSampleRate rate(44100);
    std::string dump;
    unsigned int plainID = 0;
    unsigned int spectralID = 0;
    {
      patcherImplementation src(1, nullptr);
      // [~adc] -> [~spectroscope 20 0 1024], and [~adc] -> [~pfft view 512 4]
      // whose ~fftin feeds a ~spectroscope (both parts) and ~fftout; a
      // receive turns the plain one to the phase spectrum.
      YSE::pHandle* adc = src.CreateObject(YSE::OBJ::D_ADC, "");
      YSE::pHandle* control = src.CreateObject(YSE::OBJ::G_RECEIVE, "spectro1170");
      YSE::pHandle* plain = src.CreateObject(YSE::OBJ::D_SPECTROSCOPE, "20 0 1024");
      YSE::pHandle* pfft = src.CreateObject(YSE::OBJ::D_PFFT, "view 512 4");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      YSE::pHandle* in = src.CreateObject(YSE::OBJ::D_FFTIN, "0");
      YSE::pHandle* spectral = src.CreateObject(YSE::OBJ::D_SPECTROSCOPE, "10");
      YSE::pHandle* out = src.CreateObject(YSE::OBJ::D_FFTOUT, "0");
      for (YSE::pHandle* h : {adc, control, plain, pfft, dac, in, spectral, out})
        REQUIRE(h != nullptr);
      for (YSE::pHandle* h : {in, spectral, out})
        src.SetObjectContainer(h, pfft);
      src.Connect(adc, 0, plain, 0);
      src.Connect(control, 0, plain, 0);
      src.Connect(adc, 0, pfft, 0);
      src.Connect(in, 0, spectral, 0);
      src.Connect(in, 1, spectral, 1);
      src.Connect(in, 0, out, 0);
      src.Connect(in, 1, out, 1);
      src.Connect(pfft, 0, dac, 0);
      plainID = plain->GetID();
      spectralID = spectral->GetID();
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    YSE::pHandle* plain = dst.GetHandleFromID(plainID);
    YSE::pHandle* spectral = dst.GetHandleFromID(spectralID);
    REQUIRE(plain != nullptr);
    REQUIRE(spectral != nullptr);
    CHECK(plain->GetParams() == "20 0 1024");
    CHECK(spectral->GetParams() == "10");

    // A tone of 0.6 at bin 64 of 1024 (bin 32 of 512): 2756.25 Hz.
    const auto tone = [](std::size_t n) {
      return static_cast<float>(0.6 * std::sin(2.0 * PI * 64.0 * static_cast<double>(n) / 1024.0));
    };
    std::size_t at = 0;
    for (unsigned int block = 0; block < 40; ++block)
      RenderBlock(dst, at, tone);

    std::vector<float> cells = Cells(plain->GetGuiValue());
    REQUIRE(cells.size() == 5 + 513);
    CHECK(cells[0] == 0.f);
    CHECK(cells[1] == 1024.f);
    CHECK(cells[3] >= 5.f); // 40 blocks of 128 at 882 samples a frame
    CHECK(cells[4] == 44100.f);
    CHECK(Peak(cells) == 64);
    CHECK(cells[5 + 64] == doctest::Approx(0.6f).epsilon(1e-3));
    CHECK(cells[5 + 63] == doctest::Approx(0.3f).epsilon(1e-3));

    // Inside the ~pfft: the ~pfft's half spectrum, a Hann window's mean
    // halving the tone.
    cells = Cells(spectral->GetGuiValue());
    REQUIRE(cells.size() == 5 + 256);
    CHECK(cells[1] == 512.f);
    CHECK(cells[2] == 256.f);
    CHECK(cells[3] >= 5.f); // a frame every 441 samples, in hops of 128
    CHECK(Peak(cells) == 32);
    CHECK(cells[5 + 32] == doctest::Approx(0.3f).epsilon(1e-3));

    // The receive turns the plain one to phases.
    REQUIRE(dst.PassData(std::string("phasespect 1"), "spectro1170", YSE::T_GUI));
    for (unsigned int block = 0; block < 8; ++block)
      RenderBlock(dst, at, tone);
    cells = Cells(plain->GetGuiValue());
    CHECK(cells[0] == 1.f);
    REQUIRE(cells.size() == 5 + 513);
  }

  TEST_CASE("~spectroscope: host threads polling while the audio thread analyses only ever "
            "see whole frames (#1170)") {
    // One frame at the end of every block of the block's number: DC and
    // bin 1 (the Hann window's lobe, scaled as a sine's) read the number,
    // the rest 0. Two host threads poll at
    // once, one by the bulk read and one cell by cell.
    ScopedSampleRate rate(N * 100); // 10 ms is one block
    Rig rig;
    rig.scope.SetParams("10 0 16");
    std::atomic<bool> done{false};
    std::atomic<unsigned int> torn{0};
    std::atomic<unsigned int> reads{0};
    // The blocks start once the bulk poller polls, and it polls once more
    // after the last block: it reads at least once, however the threads are
    // scheduled (#1292).
    std::atomic<bool> polling{false};
    std::thread bulk([&] {
      for (bool finished = false; !finished;) {
        finished = done.load(std::memory_order_acquire);
        polling.store(true, std::memory_order_release);
        const std::vector<float> cells = Cells(rig.scope.GetGuiValue());
        if (cells.size() < 5) {
          torn.fetch_add(1);
          continue;
        }
        if (cells[3] == 0.f) continue;
        if (cells.size() != 5 + 9) {
          torn.fetch_add(1);
          continue;
        }
        const float k = cells[3];
        if (std::fabs(cells[5] - k) > 1e-3f * k) torn.fetch_add(1);
        if (std::fabs(cells[6] - k) > 1e-3f * k) torn.fetch_add(1);
        for (std::size_t i = 7; i < cells.size(); ++i)
          if (std::fabs(cells[i]) > 1e-3f * k) torn.fetch_add(1);
        reads.fetch_add(1);
      }
    });
    std::thread cellwise([&] {
      while (!done.load(std::memory_order_acquire)) {
        (void)rig.scope.GetGuiValueAt(0);
        const unsigned int count = rig.scope.GetGuiValueCount();
        for (unsigned int i = 1; i < count; ++i)
          (void)rig.scope.GetGuiValueAt(i);
      }
    });
    buffer in(N);
    while (!polling.load(std::memory_order_acquire))
      std::this_thread::yield();
    for (unsigned int k = 1; k <= 3000; ++k) {
      in = static_cast<float>(k);
      rig.Block(in);
    }
    done.store(true, std::memory_order_release);
    bulk.join();
    cellwise.join();
    CHECK(torn.load() == 0);
    CHECK(reads.load() > 0);
    CHECK(Cells(rig.scope)[3] == 3000.f);
  }

  TEST_CASE("~spectroscope: messages and blocks on the audio thread allocate nothing, inside "
            "a ~pfft too (#1170)") {
    ScopedSampleRate rate(1000);
    Rig rig;
    Rig spectral(true);
    const std::string interval = "interval 12";
    const std::string phasespect = "phasespect 1";
    const std::string size = "size 1024";
    buffer in(N);
    Fill(in, 0);
    buffer y(N);
    y = -0.5f;
    rig.Block(in);
    spectralPass pass;
    pass.fftSize = 2 * N;
    pass.bins = N;
    pass.hop = N;
    pass.count = N;
    pass.passes = 1;
    {
      spectralPassScope scope(&pass);
      spectral.Block(in, y);
    }
    {
      TestHelpers::ProbeScope probe;
      dSpectroscope& s = rig.scope;
      s.GetInlet(0)->SetMessage(interval, YSE::T_DSP);
      s.GetInlet(0)->SetMessage(phasespect, YSE::T_DSP);
      s.GetInlet(0)->SetMessage(size, YSE::T_DSP);
      for (int k = 0; k < 4; ++k) {
        rig.Block(in);
        spectralPassScope scope(&pass);
        spectral.Block(in, y);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
