// Tests for the ~fft and ~ifft patcher objects (patcher/spectral/dFft),
// issue #1160. The transforms are DSP::streamingFft's and
// DSP::streamingInverseFft's and are tested in
// Tests/dsp/test_streaming_fft.cpp; these show that the signals, the
// numbers, the phase / offset messages and the arguments reach them,
// bit-exact against the DSP classes on all three outlets, and that a
// rendered ~count -> ~fft -> ~ifft -> ~* -> ~dac patch gives its input back
// two frames later after a JSON round trip.

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
#include "patcher/spectral/dFft.h"
#include "dsp/spectral/streamingFft.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::DSP::buffer;
using YSE::DSP::streamingFft;
using YSE::DSP::streamingInverseFft;
using YSE::DSP::streamingTransform;
using YSE::PATCHER::dFft;
using YSE::PATCHER::dIfft;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  struct Sinks {
    BufferSink real, imaginary, sync;
  };

  void Wire(YSE::PATCHER::pObject& obj, Sinks& s) {
    BufferSink* sinks[3] = {&s.real, &s.imaginary, &s.sync};
    for (int i = 0; i < 3; ++i) {
      obj.ConnectOutlet(sinks[i]->GetInlet(0), i);
      sinks[i]->ConnectInlet(obj.GetOutlet(i), 0);
    }
  }

  // A different block of input every call.
  struct Stream {
    buffer real{N};
    buffer imaginary{N};
    unsigned int start = 0;

    void Next() {
      for (unsigned int i = 0; i < N; ++i) {
        const auto n = static_cast<float>(start + i);
        real.getPtr()[i] = std::sin(n * 0.37f) + 0.25f * std::cos(n * 1.9f);
        imaginary.getPtr()[i] = 0.5f * std::sin(n * 0.11f + 1.f);
      }
      start += N;
    }
  };

  enum class Pins { BOTH, REAL_ONLY, NONE };

  // One block through the object and the reference; all three outlets must
  // agree to the bit. Unconnected inlets read `numbers`.
  void Step(YSE::PATCHER::pObject& obj, Sinks& s, streamingTransform& reference, Stream& in,
            Pins pins, float realNumber = 0.f, float imaginaryNumber = 0.f) {
    in.Next();
    obj.ResetDSP(); // as the patcher does before every block
    if (pins != Pins::NONE) obj.GetInlet(0)->SetBuffer(&in.real, YSE::T_GUI);
    if (pins == Pins::BOTH) obj.GetInlet(1)->SetBuffer(&in.imaginary, YSE::T_GUI);
    obj.Calculate(YSE::T_DSP);

    buffer realConstant(N);
    realConstant = realNumber;
    buffer imaginaryConstant(N);
    imaginaryConstant = imaginaryNumber;
    buffer& re = pins == Pins::NONE ? realConstant : in.real;
    if (pins == Pins::BOTH)
      reference(re, in.imaginary);
    else
      reference(re, imaginaryConstant);

    REQUIRE(s.real.received != nullptr);
    REQUIRE(s.imaginary.received != nullptr);
    REQUIRE(s.sync.received != nullptr);
    CHECK(TestHelpers::buffersNearlyEqual(*s.real.received, reference.real(), 0.f));
    CHECK(TestHelpers::buffersNearlyEqual(*s.imaginary.received, reference.imaginary(), 0.f));
    CHECK(TestHelpers::buffersNearlyEqual(*s.sync.received, reference.sync(), 0.f));
  }

  template <class Object, class Transform>
  void CheckAgainstClass(const std::string& args, UInt size, UInt interval, UInt offset) {
    CAPTURE(args);
    Sinks s;
    Object obj;
    Wire(obj, s);
    obj.SetParams(args);
    CHECK(obj.GetParams() == args);
    Transform reference(size, interval, offset);
    Stream in;
    for (int block = 0; block < 12; ++block)
      Step(obj, s, reference, in, block < 6 ? Pins::BOTH : Pins::REAL_ONLY);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~fft / ~ifft: registry and shape (#1160)") {
    for (const auto* type : {YSE::OBJ::D_FFT, YSE::OBJ::D_IFFT}) {
      CAPTURE(type);
      YSE::patcher p;
      p.create(1);
      YSE::pHandle* obj = p.CreateObject(type);
      REQUIRE(obj != nullptr);
      CHECK(std::string(obj->Type()) == type);
      CHECK(obj->GetInputs() == 2);
      CHECK(obj->GetOutputs() == 3);
      for (unsigned int i = 0; i < 2; ++i)
        CHECK(obj->IsDSPInput(i));
      for (unsigned int i = 0; i < 3; ++i)
        CHECK(obj->OutputDataType(i) == YSE::OUT_TYPE::BUFFER);

      std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(type));
      REQUIRE(o != nullptr);
      CHECK(o->GetCategory() == YSE::PATCHER::pCategory::SPECTRAL);
      CHECK(o->IsDSPObject());
      CHECK(o->GetParamCount() == 3);
    }
    CHECK(std::string(YSE::OBJ::D_FFT) == "~fft");
    CHECK(std::string(YSE::OBJ::D_IFFT) == "~ifft");
  }

  TEST_CASE("~fft / ~ifft: the arguments reach the DSP class, fitted as it fits them (#1160)") {
    CheckAgainstClass<dFft, streamingFft>("", 512, 512, 0);
    CheckAgainstClass<dFft, streamingFft>("64", 64, 64, 0);
    CheckAgainstClass<dFft, streamingFft>("64 256 100", 64, 256, 100);
    CheckAgainstClass<dFft, streamingFft>("100 300 700", 128, 512, 188);
    CheckAgainstClass<dFft, streamingFft>("-5 -1 -3", 16, 16, 0);
    CheckAgainstClass<dFft, streamingFft>("1000000 0 0", 65536, 65536, 0);
    CheckAgainstClass<dIfft, streamingInverseFft>("", 512, 512, 0);
    CheckAgainstClass<dIfft, streamingInverseFft>("32 128 96", 32, 128, 96);
    CheckAgainstClass<dIfft, streamingInverseFft>("16 16 9", 16, 16, 9);

    // A reparse back to no arguments restores the defaults.
    Sinks s;
    dFft obj;
    Wire(obj, s);
    obj.SetParams("64 128 5");
    obj.SetParams("");
    streamingFft reference;
    Stream in;
    for (int block = 0; block < 10; ++block)
      Step(obj, s, reference, in, Pins::BOTH);
  }

  TEST_CASE("~fft / ~ifft: unconnected inlets read their numbers (#1160, #1036)") {
    for (const bool inverse : {false, true}) {
      CAPTURE(inverse);
      Sinks s;
      std::unique_ptr<YSE::PATCHER::pObject> obj;
      std::unique_ptr<streamingTransform> reference;
      if (inverse) {
        obj = std::make_unique<dIfft>();
        reference = std::make_unique<streamingInverseFft>(64, 64, 0);
      } else {
        obj = std::make_unique<dFft>();
        reference = std::make_unique<streamingFft>(64, 64, 0);
      }
      Wire(*obj, s);
      obj->SetParams("64");
      Stream in;
      // Nothing connected and no numbers: silence, the sync counting once
      // the first frame is out.
      Step(*obj, s, *reference, in, Pins::NONE);
      // Numbers on both inlets: a constant 0.5 + 0.25i, so bin 0 is 32 + 16i.
      obj->GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
      obj->GetInlet(1)->SetFloat(0.25f, YSE::T_GUI);
      Step(*obj, s, *reference, in, Pins::NONE, 0.5f, 0.25f);
      Step(*obj, s, *reference, in, Pins::NONE, 0.5f, 0.25f);
      if (!inverse) {
        // The second half of the last block is the first whole frame of the
        // constant: bin 0 at sample 64, the other bins 0.
        CHECK(s.real.received->getPtr()[64] == doctest::Approx(32.f));
        CHECK(s.imaginary.received->getPtr()[64] == doctest::Approx(16.f));
        CHECK(std::abs(s.real.received->getPtr()[65]) < 1e-5f);
      }
      // An int, and a signal on the real inlet with the imaginary number.
      obj->GetInlet(1)->SetInt(2, YSE::T_GUI);
      Step(*obj, s, *reference, in, Pins::REAL_ONLY, 0.f, 2.f);
      obj->GetInlet(1)->SetInt(0, YSE::T_GUI);
      Step(*obj, s, *reference, in, Pins::REAL_ONLY);
    }
  }

  TEST_CASE("~fft / ~ifft: phase and offset messages move the frames like the DSP class "
            "(#1160)") {
    for (const bool inverse : {false, true}) {
      CAPTURE(inverse);
      Sinks s;
      std::unique_ptr<YSE::PATCHER::pObject> obj;
      std::unique_ptr<streamingTransform> reference;
      if (inverse) {
        obj = std::make_unique<dIfft>();
        reference = std::make_unique<streamingInverseFft>(64, 256, 0);
      } else {
        obj = std::make_unique<dFft>();
        reference = std::make_unique<streamingFft>(64, 256, 0);
      }
      Wire(*obj, s);
      obj->SetParams("64 256");
      Stream in;
      Step(*obj, s, *reference, in, Pins::BOTH);
      Step(*obj, s, *reference, in, Pins::BOTH);

      obj->GetInlet(0)->SetMessage("phase 100", YSE::T_GUI);
      reference->offset(100);
      Step(*obj, s, *reference, in, Pins::BOTH);
      Step(*obj, s, *reference, in, Pins::BOTH);
      Step(*obj, s, *reference, in, Pins::BOTH);
      // offset is the same, modulo the interval; as a list from a receive.
      obj->GetInlet(0)->SetList("offset 300", YSE::T_GUI);
      reference->offset(300);
      Step(*obj, s, *reference, in, Pins::BOTH);
      Step(*obj, s, *reference, in, Pins::BOTH);
      obj->SetMessage("phase 7.9", 0.f);
      reference->offset(7);
      Step(*obj, s, *reference, in, Pins::BOTH);
      Step(*obj, s, *reference, in, Pins::BOTH);
      // Not saved: the arguments are.
      CHECK(obj->GetParams() == "64 256");

      // Malformed, negative or unknown messages change nothing.
      obj->SetMessage("phase", 0.f);
      obj->SetMessage("phase -3", 0.f);
      obj->SetMessage("phase nan", 0.f);
      obj->SetMessage("offset abc", 0.f);
      obj->SetMessage("fftsize 128", 0.f);
      obj->SetMessage("interval 1024", 0.f);
      obj->SetMessage("", 0.f);
      Step(*obj, s, *reference, in, Pins::BOTH);
      Step(*obj, s, *reference, in, Pins::BOTH);
      Step(*obj, s, *reference, in, Pins::BOTH);
    }
  }

  TEST_CASE("~fft / ~ifft: the arguments survive a DumpJSON / ParseJSON round trip (#1160)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_FFT, "1024 2048 512") != nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_IFFT, "256") != nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_FFT, "") != nullptr);
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
    CHECK(std::string(a->Type()) == "~fft");
    CHECK(a->GetParams() == "1024 2048 512");
    CHECK(std::string(b->Type()) == "~ifft");
    CHECK(b->GetParams() == "256");
    CHECK(c->GetParams() == "");
  }

  TEST_CASE("~fft / ~ifft: in a rendered patch, ~fft -> ~ifft gives the input back two frames "
            "later, with phase from a receive, after a JSON round trip (#1160)") {
    constexpr unsigned int SIZE = 256;
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      // 0, 0.001, ..., 0.999, over and over, through ~fft and ~ifft with
      // frames twice the size apart; 1 / 256 undoes the transforms' scale.
      YSE::pHandle* count = src.CreateObject(YSE::OBJ::D_COUNT, "0 1000 1");
      YSE::pHandle* scale = src.CreateObject(YSE::OBJ::D_MULTIPLY, "0.001");
      YSE::pHandle* fft = src.CreateObject(YSE::OBJ::D_FFT, "256 512");
      YSE::pHandle* ifft = src.CreateObject(YSE::OBJ::D_IFFT, "256 512 256");
      YSE::pHandle* norm = src.CreateObject(YSE::OBJ::D_MULTIPLY, "0.00390625");
      YSE::pHandle* control = src.CreateObject(YSE::OBJ::G_RECEIVE, "fft1160");
      YSE::pHandle* inverseControl = src.CreateObject(YSE::OBJ::G_RECEIVE, "ifft1160");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(count != nullptr);
      REQUIRE(scale != nullptr);
      REQUIRE(fft != nullptr);
      REQUIRE(ifft != nullptr);
      REQUIRE(norm != nullptr);
      REQUIRE(control != nullptr);
      REQUIRE(inverseControl != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(count, 0, scale, 0);
      src.Connect(scale, 0, fft, 0);
      src.Connect(fft, 0, ifft, 0);
      src.Connect(fft, 1, ifft, 1);
      src.Connect(ifft, 0, norm, 0);
      src.Connect(control, 0, fft, 0);
      src.Connect(inverseControl, 0, ifft, 0);
      src.Connect(norm, 0, dac, 0);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);

    std::vector<float> out;
    const auto render = [&](unsigned int blocks) {
      for (unsigned int block = 0; block < blocks; ++block) {
        dst.Calculate(YSE::T_DSP);
        for (unsigned int i = 0; i < N; ++i)
          out.push_back(dst.output[0].getPtr()[i]);
      }
    };
    const auto input = [](std::size_t n) { return 0.001f * static_cast<float>(n % 1000); };

    // Samples 0..255 of every 512 come back 512 samples later; the rest of
    // the input is not used, and the output there is 0.
    render(16);
    for (std::size_t n = 0; n < out.size(); ++n) {
      CAPTURE(n);
      const bool back = n >= 2 * SIZE && (n % (2 * SIZE)) < SIZE;
      const float expected = back ? input(n - 2 * SIZE) : 0.f;
      CHECK(std::abs(out[n] - expected) < 1e-4f);
    }

    // phase 256 moves the ~fft's frames to the other half of each period,
    // and offset 0 the ~ifft's to follow them: a period later, the samples
    // 256..511 of every 512 come back, still 512 samples later.
    REQUIRE(dst.PassData("phase 256", "fft1160", YSE::T_GUI));
    REQUIRE(dst.PassData("offset 0", "ifft1160", YSE::T_GUI));
    const std::size_t from = out.size();
    render(16);
    for (std::size_t n = from + 2 * SIZE; n < out.size(); ++n) {
      CAPTURE(n);
      const bool back = (n % (2 * SIZE)) >= SIZE;
      const float expected = back ? input(n - 2 * SIZE) : 0.f;
      CHECK(std::abs(out[n] - expected) < 1e-4f);
    }
  }

  TEST_CASE("~fft / ~ifft: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1160)") {
    Sinks s;
    Sinks t;
    dFft fft;
    dIfft ifft;
    Wire(fft, s);
    Wire(ifft, t);
    fft.SetParams("1024");
    ifft.SetParams("1024");
    Stream in;
    in.Next();
    const std::string phase = "phase 512";
    const std::string offset = "offset 100";
    const auto block = [&]() {
      fft.ResetDSP();
      fft.GetInlet(0)->SetBuffer(&in.real, YSE::T_DSP);
      fft.GetInlet(1)->SetBuffer(&in.imaginary, YSE::T_DSP);
      fft.Calculate(YSE::T_DSP);
      ifft.ResetDSP();
      ifft.GetInlet(0)->SetBuffer(&in.real, YSE::T_DSP);
      ifft.Calculate(YSE::T_DSP);
    };
    block();
    {
      TestHelpers::ProbeScope probe;
      fft.GetInlet(0)->SetFloat(3.f, YSE::T_DSP);
      ifft.GetInlet(1)->SetInt(2, YSE::T_DSP);
      for (int i = 0; i < 20; ++i) // frames complete inside these blocks
        block();
      fft.GetInlet(0)->SetMessage(phase, YSE::T_DSP);
      ifft.GetInlet(0)->SetMessage(offset, YSE::T_DSP);
      block();
      fft.ResetDSP();
      fft.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    REQUIRE(s.real.received != nullptr);
    REQUIRE(t.sync.received != nullptr);
  }

} // TEST_SUITE("patcher")
