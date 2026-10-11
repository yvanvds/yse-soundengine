// Signal-to-control outlets (issue #1037): a signal object arms a control
// outlet in CALC(), and the patcher sends what was armed after the block's DSP
// traversal. These pin the slot (coalescing, disarm, buffer outlets), the
// per-object flush (right to left) and the patcher's flush (storage-ID order,
// once per block). ~snapshot (#1125) is the reference object and has its own
// file.

#include <doctest/doctest.h>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using TestHelpers::BangSink;
using TestHelpers::FloatSink;
using TestHelpers::OrderSink;
using TestHelpers::Wire;
using YSE::PATCHER::patcherImplementation;

namespace {
  // A signal object with a bang, a float and an int outlet, whose CALC() arms
  // whatever the test queued.
  struct Armer : TestHelpers::SinkBase {
    int bangs = 0;
    std::vector<float> floats;
    std::vector<int> ints;

    Armer() : SinkBase(true) {
      outputs.emplace_back(this, YSE::OUT_TYPE::BANG);
      outputs.emplace_back(this, YSE::OUT_TYPE::FLOAT);
      outputs.emplace_back(this, YSE::OUT_TYPE::INT);
      outputs.emplace_back(this, YSE::OUT_TYPE::BUFFER);
    }
    const char* Type() const override {
      return "armer";
    }
    void SetMessage(const std::string&, float) override {}
    void Calculate(YSE::THREAD) override {
      for (int i = 0; i < bangs; i++)
        outputs[0].ArmBang();
      for (float f : floats)
        outputs[1].ArmFloat(f);
      for (int v : ints)
        outputs[2].ArmInt(v);
    }
  };

  float GuiFloat(YSE::pHandle* h) {
    return std::stof(h->GetGuiValue());
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("armed outlets: CALC() arms, only the flush sends (#1037)") {
    FloatSink sink;
    Armer armer;
    Wire(armer, 1, sink);
    armer.floats = {0.5f};
    armer.Calculate(YSE::T_DSP);
    CHECK(armer.GetOutlet(1)->Armed());
    CHECK_FALSE(sink.gotFloat);

    armer.FlushArmedOutlets(YSE::T_GUI);
    CHECK(sink.gotFloat);
    CHECK(sink.received == doctest::Approx(0.5f));
    CHECK_FALSE(armer.GetOutlet(1)->Armed());
  }

  TEST_CASE("armed outlets: the latest value wins and bangs coalesce (#1037)") {
    BangSink bang;
    FloatSink value;
    OrderSink number;
    Armer armer;
    Wire(armer, 0, bang);
    Wire(armer, 1, value);
    Wire(armer, 2, number);
    armer.bangs = 3;
    armer.floats = {0.1f, 0.2f, 0.3f};
    armer.ints = {7, 8};
    armer.Calculate(YSE::T_DSP);
    armer.FlushArmedOutlets(YSE::T_GUI);
    CHECK(bang.bangCount == 1);
    CHECK(value.received == doctest::Approx(0.3f));
    CHECK(number.count == 1);
    CHECK(number.lastInt == 8);
  }

  TEST_CASE("armed outlets: a flush with nothing armed sends nothing, and disarms (#1037)") {
    BangSink bang;
    Armer armer;
    Wire(armer, 0, bang);
    armer.FlushArmedOutlets(YSE::T_GUI);
    CHECK(bang.bangCount == 0);

    armer.bangs = 1;
    armer.Calculate(YSE::T_DSP);
    armer.FlushArmedOutlets(YSE::T_GUI);
    armer.FlushArmedOutlets(YSE::T_GUI); // the second has nothing left
    CHECK(bang.bangCount == 1);
  }

  TEST_CASE("armed outlets: one object's outlets flush right to left (#1037)") {
    std::vector<char> log;
    OrderSink a;
    OrderSink b;
    OrderSink c;
    a.log = b.log = c.log = &log;
    a.tag = 'b';
    b.tag = 'f';
    c.tag = 'i';
    Armer armer;
    Wire(armer, 0, a);
    Wire(armer, 1, b);
    Wire(armer, 2, c);
    armer.bangs = 1;
    armer.floats = {1.f};
    armer.ints = {1};
    armer.Calculate(YSE::T_DSP);
    armer.FlushArmedOutlets(YSE::T_GUI);
    CHECK(std::string(log.begin(), log.end()) == "ifb");
  }

  TEST_CASE("armed outlets: a buffer outlet cannot be armed (#1037)") {
    Armer armer;
    armer.GetOutlet(3)->ArmFloat(1.f);
    armer.GetOutlet(3)->ArmBang();
    CHECK_FALSE(armer.GetOutlet(3)->Armed());
  }

  TEST_CASE("armed outlets: arming and flushing allocate nothing (#1037)") {
    FloatSink sink;
    BangSink bang;
    Armer armer;
    Wire(armer, 0, bang);
    Wire(armer, 1, sink);
    armer.floats = {0.25f};
    armer.bangs = 2;
    {
      TestHelpers::ProbeScope probe;
      armer.Calculate(YSE::T_DSP);
      armer.FlushArmedOutlets(YSE::T_GUI);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(bang.bangCount == 1);
  }

  TEST_CASE("armed outlets: the patcher flushes after the block, in storage-ID order (#1037)") {
    // Two ~snapshots report every block into one .f; the one flushed last is
    // what .f holds. ~phasor at frequency 0 is a constant signal at its
    // phaseoffset.
    for (int lowFirst = 0; lowFirst < 2; lowFirst++) {
      CAPTURE(lowFirst);
      patcherImplementation p(1, nullptr);
      const float first = lowFirst ? 0.25f : 0.75f;
      const float second = lowFirst ? 0.75f : 0.25f;
      YSE::pHandle* snapA = p.CreateObject(YSE::OBJ::D_SNAPSHOT, "1");
      YSE::pHandle* snapB = p.CreateObject(YSE::OBJ::D_SNAPSHOT, "1");
      YSE::pHandle* srcA = p.CreateObject(YSE::OBJ::D_PHASOR, "0 " + std::to_string(first));
      YSE::pHandle* srcB = p.CreateObject(YSE::OBJ::D_PHASOR, "0 " + std::to_string(second));
      YSE::pHandle* f = p.CreateObject(YSE::OBJ::G_FLOAT, "");
      REQUIRE(snapA->GetID() < snapB->GetID());
      p.Connect(srcA, 0, snapA, 0);
      p.Connect(srcB, 0, snapB, 0);
      p.Connect(snapA, 0, f, 1);
      p.Connect(snapB, 0, f, 1);

      p.Calculate(YSE::T_DSP);
      // snapB has the higher storage ID, so it is flushed last.
      CHECK(GuiFloat(f) == doctest::Approx(second));
    }
  }

  TEST_CASE("armed outlets: a patcher block with emitters allocates nothing (#1037)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* src = p.CreateObject(YSE::OBJ::D_PHASOR, "0 0.5");
    YSE::pHandle* snap = p.CreateObject(YSE::OBJ::D_SNAPSHOT, "1");
    YSE::pHandle* f = p.CreateObject(YSE::OBJ::G_FLOAT, "");
    p.Connect(src, 0, snap, 0);
    p.Connect(snap, 0, f, 1);
    p.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      p.Calculate(YSE::T_DSP);
      p.Calculate(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(GuiFloat(f) == doctest::Approx(0.5f));
  }

} // TEST_SUITE("patcher")
