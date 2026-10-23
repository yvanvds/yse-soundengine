// Tests for the bitwise signal objects ~bitand ~bitor ~bitxor ~bitnot
// ~bitshift ~bitsafe (patcher/math/dBitwise), issue #1051. The sample math is
// DSP::bitwise / bitnot / bitshift / bitsafe (Tests/dsp/test_bitwise.cpp);
// these show that pins, messages and parameters reach it, in isolation and in
// a running patch.

#include <doctest/doctest.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/dBitwise.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using YSE::PATCHER::patcherImplementation;

namespace {
  void Wire(YSE::PATCHER::pObject& op, BufferSink& sink) {
    op.ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(op.GetOutlet(0), 0);
  }

  std::uint32_t Bits(float f) {
    std::uint32_t u = 0;
    std::memcpy(&u, &f, sizeof u);
    return u;
  }

  float FromBits(std::uint32_t u) {
    float f = 0.f;
    std::memcpy(&f, &u, sizeof f);
    return f;
  }

  bool AllEqual(YSE::DSP::buffer* b, float expected) {
    if (b == nullptr) return false;
    for (unsigned int i = 0; i < b->getLength(); ++i)
      if (b->getPtr()[i] != expected) return false;
    return true;
  }

  // A number into inlet 0 of a fresh T (with creation arguments `args` and
  // an optional message), one block out, its first sample. The sink is
  // declared first so it outlives the object.
  template <class T> float Number(float in, const char* args = "", const char* message = "") {
    BufferSink sink;
    T op;
    Wire(op, sink);
    if (args[0] != '\0') op.SetParams(args);
    if (message[0] != '\0') op.GetInlet(0)->SetMessage(message, YSE::T_GUI);
    op.GetInlet(0)->SetFloat(in, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    REQUIRE(sink.received->getLength() == YSE::STANDARD_BUFFERSIZE);
    REQUIRE(AllEqual(sink.received, sink.received->getPtr()[0]));
    return sink.received->getPtr()[0];
  }

  // 100 samples a cycle at whatever rate the test binary runs.
  std::string HundredSampleFrequency() {
    return std::to_string(static_cast<float>(YSE::SAMPLERATE) / 100.f);
  }

  struct Family {
    const char* type;
    const char* name;
    int inputs;
  };

  const Family kFamily[] = {
      {YSE::OBJ::D_BITAND, "~bitand", 2},     {YSE::OBJ::D_BITOR, "~bitor", 2},
      {YSE::OBJ::D_BITXOR, "~bitxor", 2},     {YSE::OBJ::D_BITNOT, "~bitnot", 1},
      {YSE::OBJ::D_BITSHIFT, "~bitshift", 1}, {YSE::OBJ::D_BITSAFE, "~bitsafe", 1},
  };
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("signal bitwise: registry, names and shape (#1051)") {
    YSE::patcher p;
    p.create(1);
    for (const Family& f : kFamily) {
      CAPTURE(f.name);
      CHECK(std::string(f.type) == f.name);
      YSE::pHandle* h = p.CreateObject(f.type);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == f.name);
      CHECK(h->GetInputs() == f.inputs);
      CHECK(h->GetOutputs() == 1);
      CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);

      std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(f.type));
      REQUIRE(obj != nullptr);
      CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
      CHECK(obj->IsDSPObject());
      CHECK(std::string(obj->Type()) == f.name);
    }
  }

  TEST_CASE("signal bitwise: and / or / xor take the mask from arguments, ints and floats "
            "(#1051)") {
    using namespace YSE::PATCHER;
    // Raw mode (default): a mask on the float bit pattern.
    CHECK(Number<dBitAnd>(3.f, "-8388608") == 2.f); // 0xFF800000 drops the mantissa
    CHECK(Number<dBitXor>(1.5f, "-2147483648") == -1.5f); // the sign bit
    CHECK(Number<dBitOr>(2.f, "4194304") == 3.f); // 0x00400000: top mantissa bit
    CHECK(Number<dBitAnd>(3.f) == 0.f); // default mask 0
    // Integer mode: creation argument 2.
    CHECK(Number<dBitAnd>(7.9f, "5 1") == 5.f);
    CHECK(Number<dBitOr>(8.f, "3 1") == 11.f);
    CHECK(Number<dBitXor>(6.f, "3 1") == 5.f);
    // Never NaN: or-ing the exponent bits in gives 0.
    CHECK(Number<dBitOr>(1.f, "2139095040") == 0.f);

    BufferSink sink;
    dBitAnd op;
    Wire(op, sink);
    op.GetInlet(0)->SetFloat(3.f, YSE::T_GUI);
    // An int on the right is the bit pattern as it is.
    op.GetInlet(1)->SetInt(static_cast<int>(0xFF800000u), YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 2.f));
    // A float in raw mode is its own bit pattern: 3 & 3 = 3.
    op.GetInlet(1)->SetFloat(3.f, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 3.f));
    // In integer mode the float converts to an integer: 7 & 2.9 -> 7 & 2.
    op.GetInlet(0)->SetMessage("mode 1", YSE::T_GUI);
    op.GetInlet(0)->SetFloat(7.f, YSE::T_GUI);
    op.GetInlet(1)->SetFloat(2.9f, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 2.f));
    // Mode 2: input raw, mask integer; mode 3: input integer, mask raw.
    op.GetInlet(0)->SetMessage("mode 2", YSE::T_GUI);
    op.GetInlet(0)->SetFloat(3.f, YSE::T_GUI);
    op.GetInlet(1)->SetFloat(-8388608.f, YSE::T_GUI); // integer 0xFF800000
    op.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 2.f));
    op.GetInlet(0)->SetMessage("mode 3", YSE::T_GUI);
    op.GetInlet(1)->SetInt(6, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 2.f)); // integer 3 & 6
    // Nonsense is ignored.
    op.GetInlet(0)->SetMessage("mode banana", YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 2.f));
  }

  TEST_CASE("signal bitwise: the 'bits' message sets the mask, most significant first "
            "(#1051)") {
    using namespace YSE::PATCHER;
    CHECK(Number<dBitAnd>(7.f, "0 1", "bits 1 0 1") == 5.f);
    CHECK(Number<dBitOr>(8.f, "0 1", "bits 1 1") == 11.f);
    // 32 bits: the sign bit and nothing else.
    std::string sign = "bits 1";
    for (int i = 0; i < 31; ++i)
      sign += " 0";
    CHECK(Number<dBitXor>(2.5f, "", sign.c_str()) == -2.5f);
    // A token that is not a number drops the message: the mask stays 3.
    CHECK(Number<dBitAnd>(7.f, "3 1", "bits 1 x 0") == 3.f);
  }

  TEST_CASE("signal bitwise: a right-hand signal wins over the mask (#1051)") {
    using namespace YSE::PATCHER;
    BufferSink sink;
    dBitXor op;
    Wire(op, sink);
    op.SetParams("0 1"); // integer mode
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE), right(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < in.getLength(); ++i) {
      in.getPtr()[i] = static_cast<float>(i);
      right.getPtr()[i] = static_cast<float>(i % 5);
    }
    op.GetInlet(1)->SetBuffer(&right, YSE::T_GUI);
    op.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    for (unsigned int i = 0; i < sink.received->getLength(); ++i) {
      CAPTURE(i);
      CHECK(sink.received->getPtr()[i] == static_cast<float>(static_cast<int>(i) ^ (i % 5)));
    }
  }

  TEST_CASE("signal bitwise: ~bitnot and ~bitshift modes and messages (#1051)") {
    using namespace YSE::PATCHER;
    CHECK(Number<dBitNot>(1.f) == FromBits(~Bits(1.f)));
    CHECK(Number<dBitNot>(0.f) == 0.f); // all ones is NaN: 0
    CHECK(Number<dBitNot>(5.f, "1") == -6.f);
    CHECK(Number<dBitNot>(5.f, "", "mode 1") == -6.f);
    CHECK(Number<dBitNot>(5.f, "1", "mode 0") == FromBits(~Bits(5.f)));

    CHECK(Number<dBitShift>(3.f, "2 1") == 12.f);
    CHECK(Number<dBitShift>(-12.f, "-2 1") == -3.f);
    CHECK(Number<dBitShift>(3.f, "", "shift -1") == FromBits(Bits(3.f) >> 1));
    CHECK(Number<dBitShift>(1.f, "-23") == FromBits(0x7Fu));
    CHECK(Number<dBitShift>(3.f) == 3.f); // shift 0

    BufferSink sink;
    dBitShift op;
    Wire(op, sink);
    op.GetInlet(0)->SetInt(5, YSE::T_GUI);
    op.GetInlet(0)->SetMessage("mode 1", YSE::T_GUI);
    op.GetInlet(0)->SetMessage("shift 3", YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 40.f));
    op.GetInlet(0)->SetMessage("shift nonsense", YSE::T_GUI); // ignored
    op.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 40.f));
    op.GetInlet(0)->SetMessage("shift -1", YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    CHECK(AllEqual(sink.received, 2.f));
  }

  TEST_CASE("signal bitwise: ~bitsafe replaces NaN and infinity with 0 (#1051)") {
    using namespace YSE::PATCHER;
    BufferSink sink;
    dBitSafe op;
    Wire(op, sink);
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    for (unsigned int i = 0; i < in.getLength(); ++i)
      in.getPtr()[i] = static_cast<float>(i) - 10.f;
    in.getPtr()[1] = std::numeric_limits<float>::quiet_NaN();
    in.getPtr()[2] = std::numeric_limits<float>::infinity();
    in.getPtr()[3] = -std::numeric_limits<float>::infinity();
    op.GetInlet(0)->SetBuffer(&in, YSE::T_GUI);
    op.Calculate(YSE::T_DSP);
    REQUIRE(sink.received != nullptr);
    CHECK(sink.received->getPtr()[0] == -10.f);
    CHECK(sink.received->getPtr()[1] == 0.f);
    CHECK(sink.received->getPtr()[2] == 0.f);
    CHECK(sink.received->getPtr()[3] == 0.f);
    for (unsigned int i = 4; i < sink.received->getLength(); ++i)
      CHECK(sink.received->getPtr()[i] == in.getPtr()[i]);
    CHECK(Number<dBitSafe>(0.5f) == 0.5f);
  }

  TEST_CASE("signal bitwise: parameters survive a DumpJSON / ParseJSON round trip (#1051)") {
    std::string dump;
    {
      patcherImplementation src(3, nullptr);
      YSE::pHandle* andObj = src.CreateObject(YSE::OBJ::D_BITAND, "6 1");
      YSE::pHandle* notObj = src.CreateObject(YSE::OBJ::D_BITNOT, "1");
      YSE::pHandle* shiftObj = src.CreateObject(YSE::OBJ::D_BITSHIFT, "2 1");
      // A constant 7 in: ~+ with an unconnected left inlet outputs its argument.
      YSE::pHandle* seven = src.CreateObject(YSE::OBJ::D_ADD, "7");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      src.Connect(seven, 0, andObj, 0);
      src.Connect(seven, 0, notObj, 0);
      src.Connect(seven, 0, shiftObj, 0);
      src.Connect(andObj, 0, dac, 0);
      src.Connect(notObj, 0, dac, 1);
      src.Connect(shiftObj, 0, dac, 2);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(3, nullptr);
    dst.ParseJSON(dump);
    dst.Calculate(YSE::T_DSP);
    CHECK(dst.output[0].getPtr()[0] == 6.f); // integer 7 & 6
    CHECK(dst.output[1].getPtr()[0] == -8.f); // integer ~7
    CHECK(dst.output[2].getPtr()[0] == 28.f); // integer 7 << 2
  }

  TEST_CASE("signal bitwise: in a patch, a sine through the six objects (#1051)") {
    patcherImplementation p(6, nullptr);
    YSE::pHandle* ramp = p.CreateObject(YSE::OBJ::D_PHASOR, HundredSampleFrequency());
    YSE::pHandle* cos = p.CreateObject(YSE::OBJ::D_COS, "");
    YSE::pHandle* gain = p.CreateObject(YSE::OBJ::D_MULTIPLY, "100");
    // Raw and with 0xFFFF0000: drop the low 16 mantissa bits.
    YSE::pHandle* crush = p.CreateObject(YSE::OBJ::D_BITAND, "-65536");
    YSE::pHandle* ior = p.CreateObject(YSE::OBJ::D_BITOR, "1 1");
    YSE::pHandle* sign = p.CreateObject(YSE::OBJ::D_BITXOR, "-2147483648");
    YSE::pHandle* inv = p.CreateObject(YSE::OBJ::D_BITNOT, "1");
    YSE::pHandle* half = p.CreateObject(YSE::OBJ::D_BITSHIFT, "-1 1");
    YSE::pHandle* safe = p.CreateObject(YSE::OBJ::D_BITSAFE, "");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");

    p.Connect(ramp, 0, cos, 0);
    p.Connect(cos, 0, gain, 0);
    for (YSE::pHandle* h : {crush, ior, sign, inv, half, safe})
      p.Connect(gain, 0, h, 0);
    p.Connect(crush, 0, dac, 0);
    p.Connect(ior, 0, dac, 1);
    p.Connect(sign, 0, dac, 2);
    p.Connect(inv, 0, dac, 3);
    p.Connect(half, 0, dac, 4);
    p.Connect(safe, 0, dac, 5);
    p.Calculate(YSE::T_DSP);

    // The objects see the same block ~* produced; rebuild it from ~bitsafe,
    // which passes a finite signal through bit for bit.
    const float pi = 3.14159265f;
    for (unsigned int i : {0u, 10u, 20u, 25u, 40u, 50u, 60u, 75u, 99u}) {
      CAPTURE(i);
      const float x = p.output[5].getPtr()[i];
      CHECK(x == doctest::Approx(100.f * std::cos(2.f * pi * static_cast<float>(i) / 100.f))
                     .epsilon(1e-2)
                     .scale(1.f));
      const int n = static_cast<int>(x);
      CHECK(p.output[0].getPtr()[i] == FromBits(Bits(x) & 0xFFFF0000u));
      CHECK(p.output[1].getPtr()[i] == static_cast<float>(n | 1));
      CHECK(p.output[2].getPtr()[i] == -x);
      CHECK(p.output[3].getPtr()[i] == static_cast<float>(-n - 1));
      CHECK(p.output[4].getPtr()[i] == std::floor(static_cast<float>(n) / 2.f));
    }
    for (unsigned int ch = 0; ch < 6; ++ch)
      for (unsigned int i = 0; i < p.output[ch].getLength(); ++i)
        CHECK(std::isfinite(p.output[ch].getPtr()[i]));
  }

  TEST_CASE("signal bitwise: numbers, messages and blocks on the audio thread allocate nothing "
            "(#1051)") {
    using namespace YSE::PATCHER;
    BufferSink sinks[6]; // declared first so they outlive the objects
    dBitAnd an;
    dBitOr ob;
    dBitXor xb;
    dBitNot nb;
    dBitShift sb;
    dBitSafe safe;
    pObject* all[] = {&an, &ob, &xb, &nb, &sb, &safe};
    pObject* twoInlets[] = {&an, &ob, &xb};
    for (unsigned int i = 0; i < 6; ++i) {
      Wire(*all[i], sinks[i]);
      all[i]->Calculate(YSE::T_DSP);
    }
    YSE::DSP::buffer in(YSE::STANDARD_BUFFERSIZE);
    in = 0.5f;
    {
      TestHelpers::ProbeScope probe;
      for (pObject* op : all) {
        op->GetInlet(0)->SetFloat(0.3f, YSE::T_DSP);
        op->GetInlet(0)->SetInt(2, YSE::T_DSP);
        op->Calculate(YSE::T_DSP);
        op->GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
        op->Calculate(YSE::T_DSP);
      }
      an.GetInlet(0)->SetMessage("mode 1", YSE::T_DSP);
      ob.GetInlet(0)->SetMessage("bits 1 0 1 1", YSE::T_DSP);
      nb.GetInlet(0)->SetMessage("mode 1", YSE::T_DSP);
      sb.GetInlet(0)->SetMessage("shift -2", YSE::T_DSP);
      for (pObject* op : twoInlets) {
        op->GetInlet(1)->SetFloat(0.7f, YSE::T_DSP);
        op->GetInlet(1)->SetInt(3, YSE::T_DSP);
        op->Calculate(YSE::T_DSP);
        op->GetInlet(1)->SetBuffer(&in, YSE::T_DSP);
        op->GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
        op->Calculate(YSE::T_DSP);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
