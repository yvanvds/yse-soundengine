// Tests for ~frameaverage and ~framesmooth (patcher/spectral/dFrameAverage),
// issue #1162. The per-bin math is DSP::frameAverage / DSP::frameSmooth's
// and is tested in Tests/dsp/test_frame_average.cpp; these show:
//  - outside a ~pfft a block is a frame, or with framesize a stream of
//    frames across blocks, bit-exact against the classes, with numbers on
//    the unconnected inlet, clear, and the arguments;
//  - inside a rendered ~pfft the objects see the spectral frame bin by bin
//    across passes: a ~fftin -> ~frameaverage / ~framesmooth -> ~fftout
//    patch matches DSP::stft with the closed-form average over frames and
//    trailing mean along the frame, at a frame of four passes, at a hop
//    below the block, with a capacity below the frame and across a clear;
//  - a magnitude smoother of ~cartopol -> ~frameaverage -> ~poltocar after
//    a JSON round trip leaves a steady tone alone;
//  - a clear waits for the start of a frame, the arguments survive a JSON
//    round trip, and rendering allocates nothing.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/spectral/frameAverage.hpp"
#include "dsp/spectral/stft.hpp"
#include "headers/constants.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/spectral/dFrameAverage.h"
#include "patcher/spectral/spectralPass.h"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::buffer;
using YSE::DSP::frameAverage;
using YSE::DSP::frameSmooth;
using YSE::DSP::stft;
using YSE::DSP::stftFrame;
using YSE::PATCHER::dFrameaverage;
using YSE::PATCHER::dFramesmooth;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = STANDARD_BUFFERSIZE;

  template <class Obj> void Wire(Obj& obj, BufferSink& out) {
    obj.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(obj.GetOutlet(0), 0);
  }

  // One block through the object, with `in` on its inlet (or nothing:
  // unconnected), as the patcher runs it.
  template <class Obj> buffer& Step(Obj& obj, BufferSink& out, buffer* in) {
    obj.ResetDSP();
    if (in != nullptr) obj.GetInlet(0)->SetBuffer(in, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    REQUIRE(out.received != nullptr);
    return *out.received;
  }

  void Fill(buffer& b, unsigned int block) {
    for (unsigned int i = 0; i < b.getLength(); ++i)
      b.getPtr()[i] = std::sin(0.3f * static_cast<float>(i) + 1.1f * static_cast<float>(block)) +
                      0.01f * static_cast<float>(i);
  }

  // A rich signal: band content plus deterministic noise.
  float Rich(std::size_t n) {
    unsigned int s = static_cast<unsigned int>(n) * 2654435761u + 12345u;
    s ^= s >> 13;
    s *= 2246822519u;
    s ^= s >> 16;
    return 0.5f * static_cast<float>(std::sin(0.07 * static_cast<double>(n))) +
           0.3f * (static_cast<float>(s >> 8) / 16777216.f - 0.5f);
  }

  // One block of `signal` through the patcher as an insert.
  template <class Signal>
  std::vector<float> RenderBlock(patcherImplementation& p, std::size_t& at, Signal&& signal) {
    MULTICHANNELBUFFER io(1);
    io[0].resize(N);
    for (unsigned int i = 0; i < N; ++i)
      io[0].getPtr()[i] = signal(at + i);
    p.ProcessAsInsert(io);
    at += N;
    return std::vector<float>(io[0].getPtr(), io[0].getPtr() + N);
  }

  // [~adc] -> [~pfft fx size overlap] -> [~dac]; inside, the real part of
  // ~fftin through [~frameaverage averageArgs] and the imaginary part
  // through [~framesmooth smoothArgs] into ~fftout.
  struct spectral {
    YSE::pHandle* pfft;
    YSE::pHandle* average;
    YSE::pHandle* smooth;
  };

  spectral BuildSpectral(patcherImplementation& p, UInt size, UInt overlap,
                         const std::string& averageArgs, const std::string& smoothArgs) {
    YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_ADC, "");
    YSE::pHandle* pfft = p.CreateObject(YSE::OBJ::D_PFFT, "fx " + std::to_string(size) + " " +
                                                              std::to_string(overlap));
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* in = p.CreateObject(YSE::OBJ::D_FFTIN, "0");
    YSE::pHandle* average = p.CreateObject(YSE::OBJ::D_FRAMEAVERAGE, averageArgs);
    YSE::pHandle* smooth = p.CreateObject(YSE::OBJ::D_FRAMESMOOTH, smoothArgs);
    YSE::pHandle* out = p.CreateObject(YSE::OBJ::D_FFTOUT, "0");
    for (YSE::pHandle* h : {adc, pfft, dac, in, average, smooth, out})
      REQUIRE(h != nullptr);
    for (YSE::pHandle* h : {in, average, smooth, out})
      p.SetObjectContainer(h, pfft);
    p.Connect(adc, 0, pfft, 0);
    p.Connect(in, 0, average, 0);
    p.Connect(in, 1, smooth, 0);
    p.Connect(average, 0, out, 0);
    p.Connect(smooth, 0, out, 1);
    p.Connect(pfft, 0, dac, 0);
    return {pfft, average, smooth};
  }

  // What that patch must put out: DSP::stft, its callback replacing the real
  // parts by their mean over the last `frames` frames (0 before the first
  // and after a clear) and the imaginary parts by their mean with the
  // `smoothness - 1` bins before them in the frame, in closed form, for the
  // bins of the half-spectrum frame (size / 2; the Nyquist bin, which a
  // ~pfft frame does not hold, zeroed), bins at or past `averageBins` of the
  // average at 0.
  struct reference {
    stft fx;
    std::vector<std::vector<float>> history;
    std::vector<float> imaginary;
    UInt frames;
    UInt averageBins;
    UInt smoothness;
    std::size_t made = 0;

    reference(UInt size, UInt overlap, UInt count, UInt bins, UInt values)
      : fx(size, overlap),
        history(count, std::vector<float>(size, 0.f)),
        imaginary(size),
        frames(count),
        averageBins(bins),
        smoothness(values) {}

    void clear() {
      for (auto& f : history)
        std::fill(f.begin(), f.end(), 0.f);
      made = 0;
    }

    std::vector<float> Block(std::size_t at) {
      buffer b(N);
      for (unsigned int i = 0; i < N; ++i)
        b.getPtr()[i] = Rich(at + i);
      auto callback = [this](stftFrame& f) {
        const UInt half = f.size / 2;
        std::vector<float>& now = history[made % frames];
        made++;
        std::copy(f.imaginary, f.imaginary + f.count, imaginary.begin());
        for (UInt k = 0; k < f.count; ++k) {
          if (k >= half) {
            f.real[k] = 0.f;
            f.imaginary[k] = 0.f;
            continue;
          }
          now[k] = f.real[k];
          double sum = 0.0;
          for (const auto& h : history)
            sum += h[k];
          f.real[k] = k < averageBins ? static_cast<float>(sum / frames) : 0.f;
          const UInt from = k + 1 >= smoothness ? k + 1 - smoothness : 0;
          double window = 0.0;
          for (UInt j = from; j <= k; ++j)
            window += imaginary[j];
          f.imaginary[k] = static_cast<float>(window / (k + 1 - from));
        }
      };
      buffer& o = fx(b, callback);
      return std::vector<float>(o.getPtr(), o.getPtr() + N);
    }
  };

  float Worst(const std::vector<float>& a, const std::vector<float>& b) {
    REQUIRE(a.size() == b.size());
    float worst = 0.f;
    for (std::size_t n = 0; n < a.size(); ++n)
      worst = std::max(worst, std::abs(a[n] - b[n]));
    return worst;
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~frameaverage / ~framesmooth: registry, category and shape (#1162)") {
    YSE::patcher p;
    p.create(1);
    for (const char* type : {YSE::OBJ::D_FRAMEAVERAGE, YSE::OBJ::D_FRAMESMOOTH}) {
      CAPTURE(type);
      YSE::pHandle* obj = p.CreateObject(type);
      REQUIRE(obj != nullptr);
      CHECK(std::string(obj->Type()) == type);
      CHECK(obj->GetInputs() == 1);
      CHECK(obj->GetOutputs() == 1);
      CHECK(obj->IsDSPInput(0));
      CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
      std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(type));
      REQUIRE(o != nullptr);
      CHECK(o->GetCategory() == YSE::PATCHER::pCategory::SPECTRAL);
      CHECK(o->IsDSPObject());
    }
    CHECK(std::string(YSE::OBJ::D_FRAMEAVERAGE) == "~frameaverage");
    CHECK(std::string(YSE::OBJ::D_FRAMESMOOTH) == "~framesmooth");
    std::unique_ptr<YSE::PATCHER::pObject> a(
        YSE::PATCHER::Register().Get(YSE::OBJ::D_FRAMEAVERAGE));
    std::unique_ptr<YSE::PATCHER::pObject> s(YSE::PATCHER::Register().Get(YSE::OBJ::D_FRAMESMOOTH));
    CHECK(a->GetParamCount() == 3);
    CHECK(s->GetParamCount() == 2);
  }

  TEST_CASE("~frameaverage / ~framesmooth: outside a ~pfft a block is a frame, against the "
            "classes; numbers and clear (#1162)") {
    BufferSink averageOut;
    BufferSink smoothOut;
    dFrameaverage average;
    dFramesmooth smooth;
    Wire(average, averageOut);
    Wire(smooth, smoothOut);
    frameAverage a;
    frameSmooth s;
    buffer in(N);
    for (unsigned int block = 0; block < 8; ++block) {
      CAPTURE(block);
      Fill(in, block);
      if (block == 5) {
        average.GetInlet(0)->SetMessage("clear", YSE::T_GUI);
        smooth.GetInlet(0)->SetMessage("clear", YSE::T_GUI);
        a.clear();
        s.clear();
      }
      CHECK(TestHelpers::buffersNearlyEqual(Step(average, averageOut, &in), a(in), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(Step(smooth, smoothOut, &in), s(in), 0.f));
    }
    // Right after the clear, the average starts again from a quarter.
    // Unconnected, the inlet reads the last number: a constant frame.
    BufferSink out2;
    dFrameaverage unconnected;
    Wire(unconnected, out2);
    unconnected.GetInlet(0)->SetFloat(2.f, YSE::T_GUI);
    CHECK(Step(unconnected, out2, nullptr).getPtr()[0] == 0.5f);
    buffer& twice = Step(unconnected, out2, nullptr);
    CHECK(twice.getPtr()[0] == 1.f);
    CHECK(twice.getPtr()[N - 1] == 1.f);
    unconnected.GetInlet(0)->SetInt(6, YSE::T_GUI);
    CHECK(Step(unconnected, out2, nullptr).getPtr()[7] == 2.5f);
  }

  TEST_CASE("~frameaverage / ~framesmooth: the arguments are frame count, capacity, "
            "smoothness and frame size (#1162)") {
    // ~frameaverage 2 64: two frames, bins past 64 put out 0.
    BufferSink out;
    dFrameaverage average;
    Wire(average, out);
    average.SetParams("2 64");
    CHECK(average.GetParams() == "2 64");
    buffer in(N);
    buffer before(N);
    frameAverage a(2, 64);
    for (unsigned int block = 0; block < 3; ++block) {
      Fill(in, block);
      buffer& o = Step(average, out, &in);
      CHECK(TestHelpers::buffersNearlyEqual(o, a(in), 0.f));
      CHECK(o.getPtr()[64] == 0.f);
      if (block > 0) {
        CHECK(o.getPtr()[3] == doctest::Approx((in.getPtr()[3] + before.getPtr()[3]) / 2.f));
      }
      before = in;
    }

    // ~frameaverage 3 4096 200 and ~framesmooth 5 200: frames of 200 samples
    // across blocks.
    BufferSink out2;
    BufferSink out3;
    dFrameaverage streamed;
    dFramesmooth smooth;
    Wire(streamed, out2);
    Wire(smooth, out3);
    streamed.SetParams("3 4096 200");
    smooth.SetParams("5 200");
    CHECK(streamed.GetParams() == "3 4096 200");
    CHECK(smooth.GetParams() == "5 200");
    frameAverage sa(3);
    frameSmooth ss(5);
    sa.frameSize(200);
    ss.frameSize(200);
    for (unsigned int block = 0; block < 9; ++block) {
      CAPTURE(block);
      Fill(in, block);
      CHECK(TestHelpers::buffersNearlyEqual(Step(streamed, out2, &in), sa(in), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(Step(smooth, out3, &in), ss(in), 0.f));
    }
    // Frame 0 is samples 0..199: sample 200 (block 1, sample 72) starts
    // frame 1, so ~framesmooth puts it out as it went in.
    Fill(in, 0);
    smooth.SetParams("5 200");
    Step(smooth, out3, &in);
    Fill(in, 1);
    buffer& next = Step(smooth, out3, &in);
    CHECK(next.getPtr()[72] == in.getPtr()[72]);
    CHECK(next.getPtr()[73] == doctest::Approx((in.getPtr()[72] + in.getPtr()[73]) / 2.f));
    CHECK(next.getPtr()[71] != in.getPtr()[71]);

    // A reparse to no arguments: the defaults.
    smooth.SetParams("");
    frameSmooth plain;
    for (unsigned int block = 0; block < 2; ++block) {
      Fill(in, block);
      CHECK(TestHelpers::buffersNearlyEqual(Step(smooth, out3, &in), plain(in), 0.f));
    }
    streamed.SetParams("");
    frameAverage plainAverage;
    for (unsigned int block = 0; block < 2; ++block) {
      Fill(in, block);
      CHECK(TestHelpers::buffersNearlyEqual(Step(streamed, out2, &in), plainAverage(in), 0.f));
    }
  }

  TEST_CASE("~frameaverage / ~framesmooth: a clear inside a ~pfft waits for the start of a "
            "frame (#1162)") {
    BufferSink out;
    dFrameaverage average;
    Wire(average, out);
    average.SetParams("2");
    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = 1.f;
    YSE::PATCHER::spectralPass pass;
    pass.fftSize = 512;
    pass.bins = 256;
    pass.hop = 128;
    pass.overlap = 4;
    pass.count = N;
    pass.passes = 2;
    const auto run = [&](UInt p, U64 frame) {
      pass.pass = p;
      pass.firstBin = p * N;
      pass.frame = frame;
      const YSE::PATCHER::spectralPassScope scope(&pass);
      return Step(average, out, &in).getPtr()[0];
    };
    CHECK(run(0, 0) == 0.5f);
    CHECK(run(1, 0) == 0.5f);
    CHECK(run(0, 1) == 1.f);
    // A clear arriving mid-frame leaves the frame's second pass alone ...
    average.GetInlet(0)->SetMessage("clear", YSE::T_GUI);
    CHECK(run(1, 1) == 1.f);
    // ... and starts the next frame from 0, every pass of it.
    CHECK(run(0, 2) == 0.5f);
    CHECK(run(1, 2) == 0.5f);
    CHECK(run(0, 3) == 1.f);
    CHECK(YSE::PATCHER::CurrentSpectralPass() == nullptr);

    // ~framesmooth carries its window from pass to pass of a frame.
    BufferSink out2;
    dFramesmooth smooth;
    Wire(smooth, out2);
    smooth.SetParams("4");
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = static_cast<float>(i);
    pass.pass = 0;
    pass.firstBin = 0;
    {
      const YSE::PATCHER::spectralPassScope scope(&pass);
      CHECK(Step(smooth, out2, &in).getPtr()[0] == 0.f);
    }
    pass.pass = 1;
    pass.firstBin = N;
    {
      const YSE::PATCHER::spectralPassScope scope(&pass);
      // Bin 128 (0 again in this pass's block) with bins 125..127.
      CHECK(Step(smooth, out2, &in).getPtr()[0] == doctest::Approx((125.0 + 126 + 127 + 0) / 4));
    }
  }

  TEST_CASE("~frameaverage / ~framesmooth: in a rendered ~pfft, bin by bin across passes, "
            "against DSP::stft in closed form (#1162)") {
    struct setting {
      UInt size;
      UInt overlap;
      std::string averageArgs;
      UInt frames;
      UInt averageBins;
      std::string smoothArgs;
      UInt smoothness;
    };
    // 1024 at overlap 4: 512 bins in four passes, a frame every two blocks.
    // 64 at overlap 4: 32 bins in one pass, eight frames a block. 1024 with a
    // capacity of 300: the average stops in the third pass. 256 at 2.
    const setting settings[] = {{1024, 4, "", 4, 4096, "", 3},
                                {64, 4, "3", 3, 4096, "6", 6},
                                {1024, 4, "5 300", 5, 300, "40", 40},
                                {256, 2, "1", 1, 4096, "1", 1}};
    for (const setting& s : settings) {
      CAPTURE(s.size);
      CAPTURE(s.overlap);
      CAPTURE(s.averageArgs);
      CAPTURE(s.smoothArgs);
      patcherImplementation p(1, nullptr);
      const spectral patch = BuildSpectral(p, s.size, s.overlap, s.averageArgs, s.smoothArgs);
      reference ref(s.size, s.overlap, s.frames, s.averageBins, s.smoothness);
      std::size_t at = 0;
      std::vector<float> out;
      std::vector<float> expected;
      constexpr unsigned int BLOCKS = 48;
      constexpr unsigned int CLEAR_AT = 30;
      for (unsigned int block = 0; block < BLOCKS; ++block) {
        if (block == CLEAR_AT) {
          patch.average->SetListData(0, "clear");
          patch.smooth->SetListData(0, "clear");
          ref.clear();
        }
        const std::size_t from = at;
        const std::vector<float> o = RenderBlock(p, at, Rich);
        const std::vector<float> e = ref.Block(from);
        out.insert(out.end(), o.begin(), o.end());
        expected.insert(expected.end(), e.begin(), e.end());
      }
      CHECK(Worst(out, expected) < 1e-5f);
      // And it is not the identity, but at frame count 1 and smoothness 1
      // (where only the Nyquist bin, which a half spectrum drops, is lost).
      float changed = 0.f;
      for (std::size_t n = s.size; n < out.size(); ++n)
        changed = std::max(changed, std::abs(out[n] - Rich(n - s.size)));
      if (s.frames == 1 && s.smoothness == 1)
        CHECK(changed < 0.05f);
      else
        CHECK(changed > 0.1f);
    }
  }

  TEST_CASE("~frameaverage: a magnitude smoother of ~cartopol, ~frameaverage and ~poltocar "
            "inside a ~pfft leaves a steady tone alone (#1162)") {
    constexpr UInt SIZE = 1024;
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* adc = src.CreateObject(YSE::OBJ::D_ADC, "");
      YSE::pHandle* pfft = src.CreateObject(YSE::OBJ::D_PFFT, "smooth 1024 4");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      YSE::pHandle* in = src.CreateObject(YSE::OBJ::D_FFTIN, "0");
      YSE::pHandle* polar = src.CreateObject(YSE::OBJ::D_CARTOPOL, "");
      YSE::pHandle* average = src.CreateObject(YSE::OBJ::D_FRAMEAVERAGE, "8");
      YSE::pHandle* cart = src.CreateObject(YSE::OBJ::D_POLTOCAR, "");
      YSE::pHandle* out = src.CreateObject(YSE::OBJ::D_FFTOUT, "0");
      for (YSE::pHandle* h : {adc, pfft, dac, in, polar, average, cart, out})
        REQUIRE(h != nullptr);
      for (YSE::pHandle* h : {in, polar, average, cart, out})
        src.SetObjectContainer(h, pfft);
      src.Connect(adc, 0, pfft, 0);
      src.Connect(in, 0, polar, 0);
      src.Connect(in, 1, polar, 1);
      src.Connect(polar, 0, average, 0);
      src.Connect(average, 0, cart, 0);
      src.Connect(polar, 1, cart, 1);
      src.Connect(cart, 0, out, 0);
      src.Connect(cart, 1, out, 1);
      src.Connect(pfft, 0, dac, 0);
      dump = src.DumpJSON();
    }
    // Rendered after a JSON round trip: the argument comes back too.
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    // A steady tone, bin-aligned: every frame's magnitudes are the same, so their average
    // over eight frames is too, once eight frames are in; before, the tone
    // fades in.
    const auto tone = [](std::size_t n) {
      return static_cast<float>(
          0.5 * std::sin(2.0 * 3.14159265358979323846 * 8.0 * static_cast<double>(n) / SIZE));
    };
    std::size_t at = 0;
    std::vector<float> out;
    for (unsigned int block = 0; block < 96; ++block) {
      const std::vector<float> o = RenderBlock(dst, at, tone);
      out.insert(out.end(), o.begin(), o.end());
    }
    // Eight frames at a hop of 256, plus the latency.
    const std::size_t settled = SIZE + 8 * SIZE / 4 + SIZE;
    float worst = 0.f;
    for (std::size_t n = settled; n < out.size(); ++n)
      worst = std::max(worst, std::abs(out[n] - tone(n - SIZE)));
    CHECK(worst < 1e-3f);
    // At the start, the first frame is an eighth of the tone.
    float early = 0.f;
    for (std::size_t n = SIZE; n < SIZE + SIZE / 4; ++n)
      early = std::max(early, std::abs(out[n]));
    CHECK(early < 0.2f);
  }

  TEST_CASE("~frameaverage / ~framesmooth: the arguments survive a DumpJSON / ParseJSON round "
            "trip (#1162)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_FRAMEAVERAGE, "6 2048 512") != nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_FRAMESMOOTH, "9 64") != nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_FRAMEAVERAGE, "") != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* a = dst.GetHandleFromID(0);
    YSE::pHandle* b = dst.GetHandleFromID(1);
    YSE::pHandle* c = dst.GetHandleFromID(2);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    CHECK(std::string(a->Type()) == "~frameaverage");
    CHECK(a->GetParams() == "6 2048 512");
    CHECK(std::string(b->Type()) == "~framesmooth");
    CHECK(b->GetParams() == "9 64");
    CHECK(c->GetParams() == "");
  }

  TEST_CASE("~frameaverage / ~framesmooth: rendering, numbers and clear on the audio thread "
            "allocate nothing (#1162)") {
    patcherImplementation p(1, nullptr);
    const spectral patch = BuildSpectral(p, 1024, 4, "", "7");
    std::size_t at = 0;
    for (int i = 0; i < 16; ++i)
      RenderBlock(p, at, Rich); // warm up
    MULTICHANNELBUFFER io(1);
    io[0].resize(N);
    io[0] = 0.1f;
    BufferSink out;
    BufferSink out2;
    dFrameaverage average;
    dFramesmooth smooth;
    Wire(average, out);
    Wire(smooth, out2);
    smooth.SetParams("5 200");
    buffer in(N);
    Step(average, out, &in);
    Step(smooth, out2, &in);
    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 32; ++i)
        p.ProcessAsInsert(io);
      average.GetInlet(0)->SetMessage("clear", YSE::T_DSP);
      average.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      smooth.GetInlet(0)->SetMessage("clear", YSE::T_DSP);
      Step(average, out, &in);
      Step(average, out, nullptr);
      Step(smooth, out2, &in);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    (void)patch;
  }
}
