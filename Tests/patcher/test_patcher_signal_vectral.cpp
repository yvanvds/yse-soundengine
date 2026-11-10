// Tests for the ~vectral patcher object (patcher/spectral/dVectral), issue
// #1132. The smoothing is DSP::vectral's and is tested in
// Tests/dsp/test_vectral.cpp; these show that the index and value signals,
// the numbers, the mode messages, size, clear and the size argument reach it,
// bit-exact against the DSP class, and that a rendered ~count -> ~vectral ->
// ~dac patch smooths its frames across frames after a JSON round trip.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/spectral/dVectral.h"
#include "dsp/spectral/vectral.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::vectral;
using YSE::PATCHER::dVectral;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  void Wire(dVectral& obj, BufferSink& out) {
    obj.ConnectOutlet(out.GetInlet(0), 0);
    out.ConnectInlet(obj.GetOutlet(0), 0);
  }

  // Frames of `frame` values: both indexes count 0 .. frame - 1 (the input
  // index `shift` ahead), the values change from frame to frame.
  struct Stream {
    buffer outIndex{N};
    buffer inIndex{N};
    buffer values{N};
    unsigned int start = 0;

    void Next(unsigned int frame, int shift = 0) {
      for (unsigned int i = 0; i < N; ++i) {
        const unsigned int n = start + i;
        const unsigned int k = n % frame;
        const unsigned int m = n / frame;
        outIndex.getPtr()[i] = static_cast<float>(k);
        inIndex.getPtr()[i] = static_cast<float>(static_cast<int>(k) + shift);
        values.getPtr()[i] = std::sin(static_cast<float>(k) * 0.3f) +
                             0.5f * std::sin(static_cast<float>(m) * 1.7f + static_cast<float>(k));
      }
      start += N;
    }
  };

  // One block through the object, with all three signals connected, and
  // through the reference; the outputs must agree to the bit.
  void Step(dVectral& obj, BufferSink& out, vectral& reference, Stream& s, unsigned int frame,
            int shift = 0) {
    s.Next(frame, shift);
    obj.ResetDSP(); // as the patcher does before every block
    obj.GetInlet(0)->SetBuffer(&s.outIndex, YSE::T_GUI);
    obj.GetInlet(1)->SetBuffer(&s.inIndex, YSE::T_GUI);
    obj.GetInlet(2)->SetBuffer(&s.values, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);
    REQUIRE(out.received != nullptr);
    CHECK(TestHelpers::buffersNearlyEqual(*out.received, reference(s.outIndex, s.inIndex, s.values),
                                          0.f));
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~vectral: registry and shape (#1132)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_VECTRAL);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~vectral");
    CHECK(obj->GetInputs() == 3);
    CHECK(obj->GetOutputs() == 1);
    for (unsigned int i = 0; i < 3; ++i)
      CHECK(obj->IsDSPInput(i));
    CHECK(obj->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_VECTRAL));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::SPECTRAL);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 1);
  }

  TEST_CASE("~vectral: signals and every message against the DSP class (#1132)") {
    BufferSink out;
    dVectral obj;
    Wire(obj, out);
    vectral reference; // 512, rampsmooth 1 1: the defaults
    Stream s;
    constexpr unsigned int FRAME = 64;

    // The defaults pass the frames through.
    Step(obj, out, reference, s, FRAME);
    CHECK(TestHelpers::buffersNearlyEqual(*out.received, s.values, 0.f));

    // size: the frame the indexes count over.
    obj.GetInlet(0)->SetMessage("size 64", YSE::T_GUI);
    reference.size(64);
    Step(obj, out, reference, s, FRAME);

    // rampsmooth with two lengths, and with one for both.
    obj.GetInlet(0)->SetMessage("rampsmooth 4 8", YSE::T_GUI);
    reference.rampsmooth(4.f, 8.f);
    Step(obj, out, reference, s, FRAME);
    Step(obj, out, reference, s, FRAME);
    obj.GetInlet(1)->SetMessage("rampsmooth 3", YSE::T_GUI);
    reference.rampsmooth(3.f, 3.f);
    Step(obj, out, reference, s, FRAME);

    // slide, in any inlet, also as a list through a receive.
    obj.GetInlet(2)->SetMessage("slide 2 6", YSE::T_GUI);
    reference.slide(2.f, 6.f);
    Step(obj, out, reference, s, FRAME);
    obj.GetInlet(0)->SetList("slide 5", YSE::T_GUI);
    reference.slide(5.f, 5.f);
    Step(obj, out, reference, s, FRAME);

    // deltaclip: lowest, then highest. One number is not enough.
    obj.GetInlet(0)->SetMessage("deltaclip -0.05 0.1", YSE::T_GUI);
    reference.deltaclip(-0.05f, 0.1f);
    Step(obj, out, reference, s, FRAME);
    obj.GetInlet(0)->SetMessage("deltaclip 0.3", YSE::T_GUI);
    Step(obj, out, reference, s, FRAME);

    // An input index ahead of the output index.
    Step(obj, out, reference, s, FRAME, 3);

    // clear, then back to an earlier mode with its own settings kept by the
    // message that chose it.
    obj.GetInlet(0)->SetMessage("clear", YSE::T_GUI);
    reference.clear();
    Step(obj, out, reference, s, FRAME);
    obj.GetInlet(0)->SetMessage("rampsmooth 6 2", YSE::T_GUI);
    reference.rampsmooth(6.f, 2.f);
    Step(obj, out, reference, s, FRAME);

    // A smaller size: the indexes past it store nothing and read 0.
    obj.GetInlet(0)->SetMessage("size 32", YSE::T_GUI);
    reference.size(32);
    Step(obj, out, reference, s, FRAME);
    CHECK(out.received->getPtr()[40] == 0.f);
    // Beyond the size argument clamps to it; below 1 to 1.
    obj.GetInlet(0)->SetMessage("size 100000", YSE::T_GUI);
    reference.size(100000);
    CHECK(reference.size() == 512);
    Step(obj, out, reference, s, FRAME);
    obj.GetInlet(0)->SetMessage("size 0", YSE::T_GUI);
    reference.size(0);
    Step(obj, out, reference, s, FRAME);
    obj.GetInlet(0)->SetMessage("size 64", YSE::T_GUI);
    reference.size(64);
    Step(obj, out, reference, s, FRAME);

    // Unknown or malformed messages and NaN change nothing.
    obj.SetMessage("smooth 3", 0.f);
    obj.SetMessage("slide", 0.f);
    obj.SetMessage("slide nan", 0.f);
    obj.SetMessage("rampsmooth abc", 0.f);
    obj.SetMessage("deltaclip nan nan", 0.f);
    obj.SetMessage("size", 0.f);
    obj.SetMessage("", 0.f);
    Step(obj, out, reference, s, FRAME);
    Step(obj, out, reference, s, FRAME);
  }

  TEST_CASE("~vectral: unconnected inlets read their numbers (#1132, #1036)") {
    BufferSink out;
    dVectral obj;
    Wire(obj, out);
    obj.SetParams("8");
    // Nothing connected: index 0, value 0.
    obj.ResetDSP();
    obj.Calculate(YSE::T_DSP);
    REQUIRE(out.received != nullptr);
    for (unsigned int i = 0; i < N; ++i)
      CHECK(out.received->getPtr()[i] == 0.f);

    // Numbers: store 2 at index 5 every sample, ramping over 4 samples,
    // since every sample is a frame of that one index; read index 5.
    obj.GetInlet(2)->SetFloat(2.f, YSE::T_GUI);
    obj.GetInlet(1)->SetInt(5, YSE::T_GUI);
    obj.GetInlet(0)->SetFloat(5.f, YSE::T_GUI);
    obj.SetMessage("rampsmooth 4", 0.f);
    obj.ResetDSP();
    obj.Calculate(YSE::T_DSP);
    CHECK(out.received->getPtr()[0] == 0.5f);
    CHECK(out.received->getPtr()[1] == 1.f);
    CHECK(out.received->getPtr()[3] == 2.f);
    CHECK(out.received->getPtr()[N - 1] == 2.f);
    // Another output index reads what is stored there: nothing.
    obj.GetInlet(0)->SetInt(4, YSE::T_GUI);
    obj.ResetDSP();
    obj.Calculate(YSE::T_DSP);
    CHECK(out.received->getPtr()[N - 1] == 0.f);
  }

  TEST_CASE("~vectral: the argument is the frame size and the largest size (#1132)") {
    const auto check = [](const std::string& args, UInt capacity) {
      CAPTURE(args);
      BufferSink out;
      dVectral obj;
      Wire(obj, out);
      obj.SetParams(args);
      CHECK(obj.GetParams() == args);
      vectral reference(capacity);
      Stream s;
      const unsigned int frame = std::min<unsigned int>(capacity + 3, 700);
      Step(obj, out, reference, s, frame);
      obj.SetMessage("slide 3", 0.f);
      reference.slide(3.f, 3.f);
      Step(obj, out, reference, s, frame);
      // The size message cannot go past it.
      obj.SetMessage("size 70000", 0.f);
      reference.size(70000);
      Step(obj, out, reference, s, frame);
    };
    check("", 512);
    check("16", 16);
    check("1024", 1024);
    check("0", 1);
    check("-4", 1);
    check("100000", vectral::MAXIMUM_SIZE);

    // A reparse back to no arguments restores the default.
    BufferSink out;
    dVectral obj;
    Wire(obj, out);
    obj.SetParams("8");
    obj.SetParams("");
    vectral reference;
    Stream s;
    Step(obj, out, reference, s, 512);
    Step(obj, out, reference, s, 600);
  }

  TEST_CASE("~vectral: the argument survives a DumpJSON / ParseJSON round trip (#1132)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_VECTRAL, "1024");
      REQUIRE(a != nullptr);
      YSE::pHandle* b = src.CreateObject(YSE::OBJ::D_VECTRAL, "");
      REQUIRE(b != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* a = dst.GetHandleFromID(0);
    YSE::pHandle* b = dst.GetHandleFromID(1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(std::string(a->Type()) == "~vectral");
    CHECK(a->GetParams() == "1024");
    CHECK(b->GetParams() == "");
  }

  TEST_CASE("~vectral: in a rendered patch, ~count frames smoothed across frames, with modes "
            "from a receive, after a JSON round trip (#1132)") {
    constexpr unsigned int FRAME = 64;
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      // Both indexes count 0 .. 63, over and over; the values are the index
      // times 0.5, the same every frame.
      YSE::pHandle* count = src.CreateObject(YSE::OBJ::D_COUNT, "0 64 1");
      YSE::pHandle* half = src.CreateObject(YSE::OBJ::D_MULTIPLY, "0.5");
      YSE::pHandle* vec = src.CreateObject(YSE::OBJ::D_VECTRAL, "64");
      YSE::pHandle* control = src.CreateObject(YSE::OBJ::G_RECEIVE, "vec1132");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(count != nullptr);
      REQUIRE(half != nullptr);
      REQUIRE(vec != nullptr);
      REQUIRE(control != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(count, 0, vec, 0);
      src.Connect(count, 0, vec, 1);
      src.Connect(count, 0, half, 0);
      src.Connect(half, 0, vec, 2);
      src.Connect(control, 0, vec, 0);
      src.Connect(vec, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);

    // Eight frames, over as many blocks as they take, checked against
    // `expected(m, k)` for frame m, bin k.
    constexpr unsigned int FRAMES = 8;
    static_assert((FRAMES * FRAME) % N == 0, "whole blocks");
    const auto render = [&](const auto& expected, double epsilon) {
      std::vector<float> out;
      for (unsigned int block = 0; block < FRAMES * FRAME / N; ++block) {
        dst.Calculate(YSE::T_DSP);
        for (unsigned int i = 0; i < N; ++i)
          out.push_back(dst.output[0].getPtr()[i]);
      }
      for (unsigned int m = 0; m < FRAMES; ++m)
        for (unsigned int k = 0; k < FRAME; ++k) {
          CAPTURE(m);
          CAPTURE(k);
          CHECK(out[m * FRAME + k] == doctest::Approx(expected(m, k)).epsilon(epsilon));
        }
    };

    // Bin k ramps from 0 to k / 2 over 4 frames, a quarter of the way each
    // frame, then holds: never smoothed into its neighbours.
    REQUIRE(dst.PassData("rampsmooth 4 2", "vec1132", YSE::T_GUI));
    render([](unsigned int m, unsigned int k) { return 0.5 * k * std::min(m + 1, 4u) / 4.0; },
           1e-6);

    // clear, then slide 2: each frame halves the distance to k / 2.
    REQUIRE(dst.PassData("clear", "vec1132", YSE::T_GUI));
    REQUIRE(dst.PassData("slide 2", "vec1132", YSE::T_GUI));
    render(
        [](unsigned int m, unsigned int k) {
          return 0.5 * k * (1.0 - std::pow(0.5, static_cast<double>(m + 1)));
        },
        1e-5);

    // clear, then deltaclip: at most 0.25 up per frame, so bin k reaches k / 2
    // after 2k frames.
    REQUIRE(dst.PassData("clear", "vec1132", YSE::T_GUI));
    REQUIRE(dst.PassData("deltaclip -1 0.25", "vec1132", YSE::T_GUI));
    render([](unsigned int m, unsigned int k) { return std::min(0.5 * k, 0.25 * (m + 1)); }, 1e-6);
  }

  TEST_CASE("~vectral: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1132)") {
    BufferSink out;
    dVectral obj;
    Wire(obj, out);
    obj.SetParams("1024");
    Stream s;
    s.Next(512);
    const std::string ramp = "rampsmooth 4 8";
    const std::string slide = "slide 2 3";
    const std::string clip = "deltaclip -0.1 0.1";
    const std::string size = "size 256";
    const std::string clear = "clear";
    const auto block = [&]() {
      obj.ResetDSP();
      obj.GetInlet(0)->SetBuffer(&s.outIndex, YSE::T_DSP);
      obj.GetInlet(1)->SetBuffer(&s.inIndex, YSE::T_DSP);
      obj.GetInlet(2)->SetBuffer(&s.values, YSE::T_DSP);
      obj.Calculate(YSE::T_DSP);
    };
    block();
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetFloat(3.f, YSE::T_DSP);
      obj.GetInlet(1)->SetInt(2, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(ramp, YSE::T_DSP);
      block();
      obj.GetInlet(0)->SetMessage(slide, YSE::T_DSP);
      block();
      obj.GetInlet(0)->SetMessage(clip, YSE::T_DSP);
      obj.GetInlet(0)->SetMessage(size, YSE::T_DSP);
      block();
      obj.GetInlet(0)->SetMessage(clear, YSE::T_DSP);
      block();
      obj.ResetDSP();
      obj.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    REQUIRE(out.received != nullptr);
  }

} // TEST_SUITE("patcher")
