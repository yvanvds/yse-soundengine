// Tests for the ~snapshot patcher object (patcher/analysis/dSnapshot), issue
// #1125: the reference object for signal-to-control outlets (#1037). It has
// no DSP class; these drive it standalone, one block at a time, flushing its
// outlet the way the patcher does after the traversal.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/analysis/dSnapshot.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using TestHelpers::FloatSink;
using TestHelpers::Wire;
using YSE::PATCHER::dSnapshot;
using YSE::PATCHER::patcherImplementation;

namespace {
  // One block: a ramp offset by `base` (sample i is base + i), then the flush
  // the patcher runs after its traversal.
  struct Rig {
    FloatSink sink;
    dSnapshot snap;
    YSE::DSP::buffer in{YSE::STANDARD_BUFFERSIZE};
    int reports = 0;

    Rig() {
      Wire(snap, 0, sink);
    }
    // Returns whether the block reported.
    bool Block(float base) {
      for (unsigned int i = 0; i < in.getLength(); i++)
        in.getPtr()[i] = base + static_cast<float>(i);
      sink.gotFloat = false;
      snap.ResetDSP();
      snap.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      snap.FlushArmedOutlets(YSE::T_GUI);
      if (sink.gotFloat) reports++;
      return sink.gotFloat;
    }
  };

  // The interval, in ms, that is exactly `blocks` blocks at the current rate.
  std::string BlocksInMs(double blocks) {
    return std::to_string(blocks * static_cast<double>(YSE::STANDARD_BUFFERSIZE) * 1000.0 /
                          YSE::SAMPLERATE);
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("snapshot: registry and shape (#1125)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_SNAPSHOT);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~snapshot");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);

    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::D_SNAPSHOT));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::ANALYSIS);
    CHECK(obj->IsDSPObject());
  }

  TEST_CASE("snapshot: with no interval only a bang reports, from the last block (#1125)") {
    Rig rig;
    CHECK_FALSE(rig.Block(10.f));
    CHECK_FALSE(rig.Block(20.f));
    rig.snap.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.gotFloat);
    CHECK(rig.sink.received == doctest::Approx(20.f)); // sample 0 of the last block
  }

  TEST_CASE("snapshot: offset picks the sample in the block (#1125)") {
    Rig rig;
    rig.snap.SetParams("0 5");
    rig.Block(100.f);
    rig.snap.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.received == doctest::Approx(105.f));

    rig.snap.GetInlet(0)->SetMessage("offset 9", YSE::T_GUI);
    rig.Block(100.f);
    rig.snap.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.received == doctest::Approx(109.f));

    // Past the block: the last sample.
    rig.snap.GetInlet(0)->SetMessage("offset 100000", YSE::T_GUI);
    rig.Block(0.f);
    rig.snap.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.received == doctest::Approx(YSE::STANDARD_BUFFERSIZE - 1.f));
  }

  TEST_CASE("snapshot: an interval reports through the armed outlet, every period (#1125)") {
    Rig rig;
    // 2.5 blocks, so no report lands exactly on a block boundary where the
    // ms -> samples rounding could decide it.
    rig.snap.SetParams(BlocksInMs(2.5));
    CHECK(rig.Block(1.f)); // the first block after reporting starts
    CHECK(rig.sink.received == doctest::Approx(1.f));
    CHECK_FALSE(rig.Block(2.f));
    CHECK(rig.Block(3.f));
    CHECK(rig.sink.received == doctest::Approx(3.f));
    CHECK_FALSE(rig.Block(4.f));
  }

  TEST_CASE("snapshot: CALC() only arms; nothing is sent before the flush (#1125, #1037)") {
    FloatSink sink;
    dSnapshot snap;
    Wire(snap, 0, sink);
    snap.SetParams(BlocksInMs(1));
    snap.Calculate(YSE::T_DSP);
    CHECK_FALSE(sink.gotFloat);
    CHECK(snap.GetOutlet(0)->Armed());
    snap.FlushArmedOutlets(YSE::T_GUI);
    CHECK(sink.gotFloat);
  }

  TEST_CASE("snapshot: an interval shorter than a block reports once per block (#1125)") {
    Rig rig;
    rig.snap.GetInlet(0)->SetMessage("sampleinterval 1", YSE::T_GUI);
    for (int i = 0; i < 4; i++)
      CHECK(rig.Block(static_cast<float>(i)));
    CHECK(rig.reports == 4);
  }

  TEST_CASE("snapshot: sampleinterval counts samples, interval clears it (#1125)") {
    Rig rig;
    rig.snap.GetInlet(0)->SetMessage(
        "sampleinterval " + std::to_string(3 * YSE::STANDARD_BUFFERSIZE), YSE::T_GUI);
    CHECK(rig.Block(0.f));
    CHECK_FALSE(rig.Block(0.f));
    CHECK_FALSE(rig.Block(0.f));
    CHECK(rig.Block(0.f));

    rig.snap.GetInlet(0)->SetMessage("interval 0", YSE::T_GUI);
    CHECK_FALSE(rig.Block(0.f));
    CHECK_FALSE(rig.Block(0.f));
  }

  TEST_CASE("snapshot: start/stop, numbers on the left and the right inlet (#1125)") {
    Rig rig;
    rig.snap.SetParams(BlocksInMs(1));
    CHECK(rig.Block(0.f)); // on by default once an interval is set

    rig.snap.GetInlet(0)->SetMessage("stop", YSE::T_GUI);
    CHECK_FALSE(rig.Block(0.f));
    rig.snap.GetInlet(0)->SetMessage("start", YSE::T_GUI);
    CHECK(rig.Block(0.f));

    rig.snap.GetInlet(0)->SetInt(0, YSE::T_GUI);
    CHECK_FALSE(rig.Block(0.f));
    rig.snap.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    CHECK(rig.Block(0.f));

    rig.snap.GetInlet(1)->SetFloat(0.f, YSE::T_GUI); // interval 0 stops
    CHECK_FALSE(rig.Block(0.f));
    rig.snap.GetInlet(1)->SetInt(1, YSE::T_GUI); // 1 ms is under a block
    CHECK(rig.Block(0.f));
  }

  TEST_CASE("snapshot: the interval follows the live sample rate (#1125)") {
    const unsigned int savedRate = YSE::SAMPLERATE;
    struct Restore {
      unsigned int rate;
      ~Restore() {
        YSE::SAMPLERATE = rate;
      }
    } restore{savedRate};

    Rig rig;
    rig.snap.SetParams(BlocksInMs(2.5)); // 2.5 blocks at the current rate
    YSE::SAMPLERATE = savedRate * 2; // now the same ms is 5 blocks
    CHECK(rig.Block(0.f));
    CHECK_FALSE(rig.Block(0.f));
    CHECK_FALSE(rig.Block(0.f)); // would report at the old rate
    CHECK_FALSE(rig.Block(0.f));
  }

  TEST_CASE("snapshot: parameters survive a DumpJSON / ParseJSON round trip (#1125)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      src.CreateObject(YSE::OBJ::D_SNAPSHOT, "250 7");
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* h = dst.GetHandleFromID(0);
    REQUIRE(h != nullptr);
    CHECK(h->GetParams() == "250 7");
  }

  TEST_CASE("snapshot: messages, blocks and the flush allocate nothing (#1125)") {
    Rig rig;
    rig.snap.SetParams("1");
    rig.Block(0.f);
    // Built outside the probe: a message longer than the library's small-string
    // buffer (15 chars on libstdc++) allocates when the std::string is made,
    // which is the caller's cost, not the object's.
    const std::string offsetMessage = "offset 3";
    const std::string intervalMessage = "sampleinterval 64";
    {
      TestHelpers::ProbeScope probe;
      rig.snap.GetInlet(0)->SetMessage(offsetMessage, YSE::T_DSP);
      rig.snap.GetInlet(0)->SetMessage(intervalMessage, YSE::T_DSP);
      rig.snap.GetInlet(1)->SetFloat(2.f, YSE::T_DSP);
      rig.Block(1.f);
      rig.snap.GetInlet(0)->SetBang(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
