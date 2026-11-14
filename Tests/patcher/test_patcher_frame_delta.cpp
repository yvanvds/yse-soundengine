// Tests for ~framedelta and ~frameaccum (patcher/spectral/dFrameDelta),
// issue #1161. The per-bin math is DSP::frameDelta / DSP::frameAccumulate's
// and is tested in Tests/dsp/test_frame_delta.cpp; these show:
//  - outside a ~pfft a block is a frame, bit-exact against the classes, with
//    numbers on the unconnected inlet, clear, and the arguments;
//  - inside a rendered ~pfft the objects see the spectral frame bin by bin
//    across passes: a ~fftin -> ~framedelta / ~frameaccum -> ~fftout patch
//    matches DSP::stft with the closed-form difference and sum, bit for bit,
//    at a frame of four passes, at a hop below the block, with a capacity
//    below the frame and across a clear;
//  - a phase vocoder made of ~cartopol -> ~framedelta -> ~frameaccum 1 ->
//    ~poltocar gives its input back;
//  - a clear waits for the start of a frame, the arguments survive a JSON
//    round trip, and rendering allocates nothing.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/spectral/frameDelta.hpp"
#include "dsp/spectral/stft.hpp"
#include "headers/constants.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/spectral/dFrameDelta.h"
#include "patcher/spectral/spectralPass.h"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::buffer;
using YSE::DSP::frameAccumulate;
using YSE::DSP::frameDelta;
using YSE::DSP::stft;
using YSE::DSP::stftFrame;
using YSE::PATCHER::dFrameaccum;
using YSE::PATCHER::dFramedelta;
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

  // One block of Rich through the patcher as an insert.
  std::vector<float> RenderBlock(patcherImplementation& p, std::size_t& at) {
    MULTICHANNELBUFFER io(1);
    io[0].resize(N);
    for (unsigned int i = 0; i < N; ++i)
      io[0].getPtr()[i] = Rich(at + i);
    p.ProcessAsInsert(io);
    at += N;
    return std::vector<float>(io[0].getPtr(), io[0].getPtr() + N);
  }

  // [~adc] -> [~pfft fx size overlap] -> [~dac]; inside, the real part of
  // ~fftin through [~framedelta deltaArgs] and the imaginary part through
  // [~frameaccum accumArgs] into ~fftout.
  struct spectral {
    YSE::pHandle* pfft;
    YSE::pHandle* delta;
    YSE::pHandle* accum;
  };

  spectral BuildSpectral(patcherImplementation& p, UInt size, UInt overlap,
                         const std::string& deltaArgs, const std::string& accumArgs) {
    YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_ADC, "");
    YSE::pHandle* pfft = p.CreateObject(YSE::OBJ::D_PFFT, "fx " + std::to_string(size) + " " +
                                                              std::to_string(overlap));
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* in = p.CreateObject(YSE::OBJ::D_FFTIN, "0");
    YSE::pHandle* delta = p.CreateObject(YSE::OBJ::D_FRAMEDELTA, deltaArgs);
    YSE::pHandle* accum = p.CreateObject(YSE::OBJ::D_FRAMEACCUM, accumArgs);
    YSE::pHandle* out = p.CreateObject(YSE::OBJ::D_FFTOUT, "0");
    for (YSE::pHandle* h : {adc, pfft, dac, in, delta, accum, out})
      REQUIRE(h != nullptr);
    for (YSE::pHandle* h : {in, delta, accum, out})
      p.SetObjectContainer(h, pfft);
    p.Connect(adc, 0, pfft, 0);
    p.Connect(in, 0, delta, 0);
    p.Connect(in, 1, accum, 0);
    p.Connect(delta, 0, out, 0);
    p.Connect(accum, 0, out, 1);
    p.Connect(pfft, 0, dac, 0);
    return {pfft, delta, accum};
  }

  // What that patch must put out: DSP::stft, its callback taking the
  // difference of the real parts and the sum of the imaginary parts in
  // closed form, for the bins of the half-spectrum frame (size / 2; the
  // Nyquist bin, which a ~pfft frame does not hold, zeroed), bins at or past
  // `deltaBins` of the difference at 0. The state is cleared before block
  // `clearAt`.
  struct reference {
    stft fx;
    std::vector<float> lastReal;
    std::vector<float> sumImaginary;
    UInt deltaBins;

    reference(UInt size, UInt overlap, UInt bins)
      : fx(size, overlap), lastReal(size, 0.f), sumImaginary(size, 0.f), deltaBins(bins) {}

    void clear() {
      std::fill(lastReal.begin(), lastReal.end(), 0.f);
      std::fill(sumImaginary.begin(), sumImaginary.end(), 0.f);
    }

    std::vector<float> Block(std::size_t at) {
      buffer b(N);
      for (unsigned int i = 0; i < N; ++i)
        b.getPtr()[i] = Rich(at + i);
      auto callback = [this](stftFrame& f) {
        const UInt half = f.size / 2;
        for (UInt k = 0; k < f.count; ++k) {
          if (k >= half) {
            f.real[k] = 0.f;
            f.imaginary[k] = 0.f;
            continue;
          }
          const float re = f.real[k];
          f.real[k] = k < deltaBins ? re - lastReal[k] : 0.f;
          if (k < deltaBins) lastReal[k] = re;
          sumImaginary[k] += f.imaginary[k];
          f.imaginary[k] = sumImaginary[k];
        }
      };
      buffer& o = fx(b, callback);
      return std::vector<float>(o.getPtr(), o.getPtr() + N);
    }
  };

  std::size_t Differences(const std::vector<float>& a, const std::vector<float>& b) {
    REQUIRE(a.size() == b.size());
    std::size_t differ = 0;
    for (std::size_t n = 0; n < a.size(); ++n)
      if (a[n] != b[n]) differ++;
    return differ;
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~framedelta / ~frameaccum: registry, category and shape (#1161)") {
    YSE::patcher p;
    p.create(1);
    for (const char* type : {YSE::OBJ::D_FRAMEDELTA, YSE::OBJ::D_FRAMEACCUM}) {
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
    CHECK(std::string(YSE::OBJ::D_FRAMEDELTA) == "~framedelta");
    CHECK(std::string(YSE::OBJ::D_FRAMEACCUM) == "~frameaccum");
    std::unique_ptr<YSE::PATCHER::pObject> d(YSE::PATCHER::Register().Get(YSE::OBJ::D_FRAMEDELTA));
    std::unique_ptr<YSE::PATCHER::pObject> a(YSE::PATCHER::Register().Get(YSE::OBJ::D_FRAMEACCUM));
    CHECK(d->GetParamCount() == 1);
    CHECK(a->GetParamCount() == 2);
  }

  TEST_CASE("~framedelta / ~frameaccum: outside a ~pfft a block is a frame, against the "
            "classes; numbers and clear (#1161)") {
    BufferSink deltaOut;
    BufferSink accumOut;
    dFramedelta delta;
    dFrameaccum accum;
    Wire(delta, deltaOut);
    Wire(accum, accumOut);
    frameDelta d;
    frameAccumulate a;
    buffer in(N);
    for (unsigned int block = 0; block < 6; ++block) {
      CAPTURE(block);
      Fill(in, block);
      if (block == 4) {
        delta.GetInlet(0)->SetMessage("clear", YSE::T_GUI);
        accum.GetInlet(0)->SetMessage("clear", YSE::T_GUI);
        d.clear();
        a.clear();
      }
      CHECK(TestHelpers::buffersNearlyEqual(Step(delta, deltaOut, &in), d(in), 0.f));
      CHECK(TestHelpers::buffersNearlyEqual(Step(accum, accumOut, &in), a(in), 0.f));
    }
    // Right after the clear, a block came out as it went in.
    // Unconnected, the inlet reads the last number: a constant frame.
    BufferSink out2;
    dFrameaccum unconnected;
    Wire(unconnected, out2);
    unconnected.GetInlet(0)->SetFloat(0.25f, YSE::T_GUI);
    Step(unconnected, out2, nullptr);
    buffer& twice = Step(unconnected, out2, nullptr);
    CHECK(twice.getPtr()[0] == 0.5f);
    CHECK(twice.getPtr()[N - 1] == 0.5f);
    unconnected.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(Step(unconnected, out2, nullptr).getPtr()[7] == 1.5f);
  }

  TEST_CASE("~framedelta / ~frameaccum: the arguments are capacity and phase wrap (#1161)") {
    // ~framedelta 64: bins past 64 of a block put out 0.
    BufferSink out;
    dFramedelta delta;
    Wire(delta, out);
    delta.SetParams("64");
    CHECK(delta.GetParams() == "64");
    buffer in(N);
    Fill(in, 0);
    frameDelta d(64);
    buffer& o = Step(delta, out, &in);
    CHECK(TestHelpers::buffersNearlyEqual(o, d(in), 0.f));
    CHECK(o.getPtr()[63] == in.getPtr()[63]);
    CHECK(o.getPtr()[64] == 0.f);

    // ~frameaccum 1: wrapped into -pi .. pi.
    BufferSink out2;
    dFrameaccum accum;
    Wire(accum, out2);
    accum.SetParams("1 256");
    CHECK(accum.GetParams() == "1 256");
    frameAccumulate a(256, true);
    for (unsigned int block = 0; block < 5; ++block) {
      for (unsigned int i = 0; i < N; ++i)
        in.getPtr()[i] = 0.9f + 0.05f * static_cast<float>(i);
      buffer& w = Step(accum, out2, &in);
      CHECK(TestHelpers::buffersNearlyEqual(w, a(in), 0.f));
      for (unsigned int i = 0; i < N; ++i) {
        REQUIRE(w.getPtr()[i] >= -3.1416f);
        REQUIRE(w.getPtr()[i] <= 3.1416f);
      }
    }
    // A reparse to no arguments: no wrap, the default capacity.
    accum.SetParams("");
    frameAccumulate plain;
    for (unsigned int block = 0; block < 3; ++block) {
      Fill(in, block);
      in.getPtr()[0] = 3.f;
      CHECK(TestHelpers::buffersNearlyEqual(Step(accum, out2, &in), plain(in), 0.f));
    }
    CHECK(out2.received->getPtr()[0] == 9.f);
  }

  TEST_CASE("~framedelta / ~frameaccum: a clear inside a ~pfft waits for the start of a "
            "frame (#1161)") {
    BufferSink out;
    dFrameaccum accum;
    Wire(accum, out);
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
    const auto run = [&](UInt p) {
      pass.pass = p;
      pass.firstBin = p * N;
      const YSE::PATCHER::spectralPassScope scope(&pass);
      return Step(accum, out, &in).getPtr()[0];
    };
    CHECK(run(0) == 1.f);
    CHECK(run(1) == 1.f);
    CHECK(run(0) == 2.f);
    // A clear arriving mid-frame leaves the frame's second pass alone ...
    accum.GetInlet(0)->SetMessage("clear", YSE::T_GUI);
    CHECK(run(1) == 2.f);
    // ... and starts the next frame from 0, every pass of it.
    CHECK(run(0) == 1.f);
    CHECK(run(1) == 1.f);
    CHECK(YSE::PATCHER::CurrentSpectralPass() == nullptr);
  }

  TEST_CASE("~framedelta / ~frameaccum: in a rendered ~pfft, bin by bin across passes, "
            "bit-exact against DSP::stft (#1161)") {
    struct setting {
      UInt size;
      UInt overlap;
      std::string deltaArgs;
      UInt deltaBins;
    };
    // 1024 at overlap 4: 512 bins in four passes, a frame every two blocks.
    // 64 at overlap 4: 32 bins in one pass, eight frames a block. 1024 with a
    // capacity of 300: the difference stops in the third pass. 256 at 2.
    const setting settings[] = {
        {1024, 4, "", 4096}, {64, 4, "", 4096}, {1024, 4, "300", 300}, {256, 2, "", 4096}};
    for (const setting& s : settings) {
      CAPTURE(s.size);
      CAPTURE(s.overlap);
      CAPTURE(s.deltaBins);
      patcherImplementation p(1, nullptr);
      const spectral patch = BuildSpectral(p, s.size, s.overlap, s.deltaArgs, "");
      reference ref(s.size, s.overlap, s.deltaBins);
      std::size_t at = 0;
      std::vector<float> out;
      std::vector<float> expected;
      constexpr unsigned int BLOCKS = 48;
      constexpr unsigned int CLEAR_AT = 30;
      for (unsigned int block = 0; block < BLOCKS; ++block) {
        if (block == CLEAR_AT) {
          patch.delta->SetListData(0, "clear");
          patch.accum->SetListData(0, "clear");
          ref.clear();
        }
        const std::size_t from = at;
        const std::vector<float> o = RenderBlock(p, at);
        const std::vector<float> e = ref.Block(from);
        out.insert(out.end(), o.begin(), o.end());
        expected.insert(expected.end(), e.begin(), e.end());
      }
      CHECK(Differences(out, expected) == 0);
      // And it is not the identity: the spectrum was changed.
      float changed = 0.f;
      for (std::size_t n = s.size; n < out.size(); ++n)
        changed = std::max(changed, std::abs(out[n] - Rich(n - s.size)));
      CHECK(changed > 0.01f);
    }
  }

  TEST_CASE("~framedelta / ~frameaccum: a phase vocoder of ~cartopol, ~framedelta, "
            "~frameaccum 1 and ~poltocar inside a ~pfft gives its input back (#1161)") {
    constexpr UInt SIZE = 1024;
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* adc = src.CreateObject(YSE::OBJ::D_ADC, "");
      YSE::pHandle* pfft = src.CreateObject(YSE::OBJ::D_PFFT, "pv 1024 4");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      YSE::pHandle* in = src.CreateObject(YSE::OBJ::D_FFTIN, "0");
      YSE::pHandle* polar = src.CreateObject(YSE::OBJ::D_CARTOPOL, "");
      YSE::pHandle* delta = src.CreateObject(YSE::OBJ::D_FRAMEDELTA, "");
      YSE::pHandle* accum = src.CreateObject(YSE::OBJ::D_FRAMEACCUM, "1");
      YSE::pHandle* cart = src.CreateObject(YSE::OBJ::D_POLTOCAR, "");
      YSE::pHandle* out = src.CreateObject(YSE::OBJ::D_FFTOUT, "0");
      for (YSE::pHandle* h : {adc, pfft, dac, in, polar, delta, accum, cart, out})
        REQUIRE(h != nullptr);
      for (YSE::pHandle* h : {in, polar, delta, accum, cart, out})
        src.SetObjectContainer(h, pfft);
      src.Connect(adc, 0, pfft, 0);
      src.Connect(in, 0, polar, 0);
      src.Connect(in, 1, polar, 1);
      src.Connect(polar, 0, cart, 0);
      src.Connect(polar, 1, delta, 0);
      src.Connect(delta, 0, accum, 0);
      src.Connect(accum, 0, cart, 1);
      src.Connect(cart, 0, out, 0);
      src.Connect(cart, 1, out, 1);
      src.Connect(pfft, 0, dac, 0);
      dump = src.DumpJSON();
    }
    // Rendered after a JSON round trip: the arguments come back too.
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    std::size_t at = 0;
    std::vector<float> out;
    for (unsigned int block = 0; block < 64; ++block) {
      const std::vector<float> o = RenderBlock(dst, at);
      out.insert(out.end(), o.begin(), o.end());
    }
    // Delayed by the size; a half spectrum drops the Nyquist bin, which a
    // rich signal fills a little.
    float worst = 0.f;
    for (std::size_t n = 2 * SIZE; n < out.size(); ++n)
      worst = std::max(worst, std::abs(out[n] - Rich(n - SIZE)));
    CHECK(worst < 0.05f);
    float signal = 0.f;
    for (std::size_t n = 2 * SIZE; n < out.size(); ++n)
      signal = std::max(signal, std::abs(out[n]));
    CHECK(signal > 0.3f);
  }

  TEST_CASE("~framedelta / ~frameaccum: the arguments survive a DumpJSON / ParseJSON round "
            "trip (#1161)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_FRAMEDELTA, "2048") != nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_FRAMEACCUM, "1 512") != nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_FRAMEACCUM, "") != nullptr);
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
    CHECK(std::string(a->Type()) == "~framedelta");
    CHECK(a->GetParams() == "2048");
    CHECK(std::string(b->Type()) == "~frameaccum");
    CHECK(b->GetParams() == "1 512");
    CHECK(c->GetParams() == "");
  }

  TEST_CASE("~framedelta / ~frameaccum: rendering, numbers and clear on the audio thread "
            "allocate nothing (#1161)") {
    patcherImplementation p(1, nullptr);
    const spectral patch = BuildSpectral(p, 1024, 4, "", "1");
    std::size_t at = 0;
    for (int i = 0; i < 16; ++i)
      RenderBlock(p, at); // warm up
    MULTICHANNELBUFFER io(1);
    io[0].resize(N);
    io[0] = 0.1f;
    BufferSink out;
    dFramedelta delta;
    Wire(delta, out);
    buffer in(N);
    Step(delta, out, &in);
    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 32; ++i)
        p.ProcessAsInsert(io);
      delta.GetInlet(0)->SetMessage("clear", YSE::T_DSP);
      delta.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      Step(delta, out, &in);
      Step(delta, out, nullptr);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    (void)patch;
  }
}
