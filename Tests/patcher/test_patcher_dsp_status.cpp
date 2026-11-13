// Tests for the DSP status objects, issue #1172: ~dspstate reports the
// patcher's DSP switch (#1043), sample rate and block size, on a bang and by
// itself when the switch changes; ~dsptime reports the milliseconds of audio
// processed since the DSP was last turned on; ~dspstress reports the
// patcher's render load in percent, at an interval and on change. There is no
// DSP class (engine queries, the two-layer rule is waived), so everything is
// shown in rendered patches with a sink on every outlet. The render load is
// wall-clock time and differs between runs, so its tests check the range and
// when values arrive, never an exact value; the interval / on-change logic is
// shown with known loads through pDspstress::Feed.

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "dsp/patcherInsert.hpp"
#include "headers/constants.hpp"
#include "patcher/genericObjects/pDspStatus.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using TestHelpers::OrderSink;
using YSE::STANDARD_BUFFERSIZE;
using YSE::PATCHER::patcherImplementation;
using YSE::PATCHER::pDspstress;

namespace {

  bool Listed(const char* name) {
    for (const auto& n : YSE::PATCHER::Register().AllNames())
      if (n == name) return true;
    return false;
  }

  double BlockMs(unsigned int rate) {
    return static_cast<double>(STANDARD_BUFFERSIZE) * 1000.0 / static_cast<double>(rate);
  }

  // Sets YSE::SAMPLERATE for the scope, as a device change does.
  struct SampleRateScope {
    unsigned int saved;
    explicit SampleRateScope(unsigned int rate) : saved(YSE::SAMPLERATE) {
      YSE::SAMPLERATE = rate;
    }
    ~SampleRateScope() {
      YSE::SAMPLERATE = saved;
    }
    SampleRateScope(const SampleRateScope&) = delete;
    SampleRateScope& operator=(const SampleRateScope&) = delete;
  };

  // A patcher with [~phasor] -> [~dac] playing, [.m stop] / [.m start] into
  // the ~dac (the switch, #1043) and the object under test with an
  // order-recording sink on each outlet (tags '0', '1', ...). The sinks come
  // first so they outlive the patcher (issue #967).
  struct Rig {
    std::vector<char> log;
    OrderSink sinks[4];
    std::vector<std::unique_ptr<YSE::pHandle>> sinkHandles;
    patcherImplementation p{1, nullptr};
    YSE::pHandle* stop = nullptr;
    YSE::pHandle* start = nullptr;
    YSE::pHandle* handle = nullptr;

    explicit Rig(const char* type, const std::string& args = "") {
      YSE::pHandle* ramp = p.CreateObject(YSE::OBJ::D_PHASOR, "100");
      YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      stop = p.CreateObject(YSE::OBJ::G_MESSAGE, "stop");
      start = p.CreateObject(YSE::OBJ::G_MESSAGE, "start");
      p.Connect(ramp, 0, dac, 0);
      p.Connect(stop, 0, dac, 0);
      p.Connect(start, 0, dac, 0);
      if (type != nullptr) Add(type, args);
    }

    void Add(const char* type, const std::string& args = "") {
      handle = p.CreateObject(type, args);
      REQUIRE(handle != nullptr);
      for (int i = 0; i < handle->GetOutputs(); ++i) {
        sinks[i].log = &log;
        sinks[i].tag = static_cast<char>('0' + i);
        sinkHandles.push_back(std::make_unique<YSE::pHandle>(&sinks[i]));
        p.Connect(handle, i, sinkHandles.back().get(), 0);
      }
    }

    YSE::PATCHER::pObject* Object() {
      return p.GetObjectFromID(handle->GetID());
    }

    void Blocks(int n) {
      for (int i = 0; i < n; ++i)
        p.Calculate(YSE::T_DSP);
    }

    void Reset() {
      log.clear();
      for (OrderSink& s : sinks) {
        s.count = 0;
        s.lastKind = OrderSink::NONE;
      }
    }

    std::string Order() const {
      return std::string(log.begin(), log.end());
    }
  };

} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("dsp status: registry, category and shape (#1172)") {
    for (const char* name : {YSE::OBJ::D_DSPSTATE, YSE::OBJ::D_DSPTIME, YSE::OBJ::D_DSPSTRESS}) {
      CAPTURE(name);
      CHECK(Listed(name));
    }
    patcherImplementation p(1, nullptr);
    YSE::pHandle* state = p.CreateObject(YSE::OBJ::D_DSPSTATE, "");
    YSE::pHandle* timer = p.CreateObject(YSE::OBJ::D_DSPTIME, "");
    YSE::pHandle* stress = p.CreateObject(YSE::OBJ::D_DSPSTRESS, "");
    REQUIRE(state != nullptr);
    REQUIRE(timer != nullptr);
    REQUIRE(stress != nullptr);
    CHECK(std::string(state->Type()) == "~dspstate");
    CHECK(std::string(timer->Type()) == "~dsptime");
    CHECK(std::string(stress->Type()) == "~dspstress");

    CHECK(state->GetInputs() == 1);
    REQUIRE(state->GetOutputs() == 4);
    CHECK(state->OutputDataType(0) == YSE::OUT_TYPE::INT);
    CHECK(state->OutputDataType(1) == YSE::OUT_TYPE::FLOAT);
    CHECK(state->OutputDataType(2) == YSE::OUT_TYPE::INT);
    CHECK(state->OutputDataType(3) == YSE::OUT_TYPE::INT);
    CHECK(timer->GetInputs() == 1);
    REQUIRE(timer->GetOutputs() == 1);
    CHECK(timer->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);
    CHECK(stress->GetInputs() == 1);
    REQUIRE(stress->GetOutputs() == 1);
    CHECK(stress->OutputDataType(0) == YSE::OUT_TYPE::FLOAT);

    for (YSE::pHandle* h : {state, timer, stress}) {
      CAPTURE(h->Type());
      // Control objects: no signal pins.
      CHECK_FALSE(h->IsDSPInput(0));
      CHECK(p.GetObjectFromID(h->GetID())->GetCategory() == YSE::PATCHER::pCategory::IO);
    }
  }

  TEST_CASE("~dspstate: a bang reports the switch, rate and block size right to left (#1172)") {
    SampleRateScope rate(44100);
    Rig r(YSE::OBJ::D_DSPSTATE);
    r.Blocks(2);
    CHECK(r.Order().empty()); // nothing by itself while the switch stays put

    r.handle->SetBang(0);
    CHECK(r.Order() == "3210");
    CHECK(r.sinks[3].lastInt == static_cast<int>(STANDARD_BUFFERSIZE));
    CHECK(r.sinks[2].lastInt == static_cast<int>(STANDARD_BUFFERSIZE));
    CHECK(r.sinks[1].lastFloat == 44100.f);
    CHECK(r.sinks[0].lastInt == 1);

    // The rate is read when the report goes out, not when the object was
    // built.
    r.Reset();
    YSE::SAMPLERATE = 96000;
    r.Object()->GetInlet(0)->SetMessage("bang", YSE::T_GUI);
    CHECK(r.Order() == "3210");
    CHECK(r.sinks[1].lastFloat == 96000.f);

    // Off: the switch outlet says so.
    r.stop->SetBang(0);
    r.Reset();
    r.handle->SetBang(0);
    CHECK(r.Order() == "3210");
    CHECK(r.sinks[0].lastInt == 0);
  }

  TEST_CASE("~dspstate: reports by itself once when the DSP turns off and on (#1172)") {
    Rig r(YSE::OBJ::D_DSPSTATE);
    r.Blocks(3);
    CHECK(r.Order().empty());

    // The switch flips on the control thread; the report goes out at the
    // top of the next block, once.
    r.stop->SetBang(0);
    CHECK(r.Order().empty());
    r.Blocks(1);
    CHECK(r.Order() == "3210");
    CHECK(r.sinks[0].lastInt == 0);
    r.Reset();
    r.Blocks(4);
    CHECK(r.Order().empty());

    r.start->SetBang(0);
    r.Blocks(1);
    CHECK(r.Order() == "3210");
    CHECK(r.sinks[0].lastInt == 1);
    r.Reset();
    r.Blocks(4);
    CHECK(r.Order().empty());

    // Off and on again between two blocks: no change to report.
    r.stop->SetBang(0);
    r.start->SetBang(0);
    r.Blocks(2);
    CHECK(r.Order().empty());
  }

  TEST_CASE("~dspstate: joining a stopped patcher reports nothing by itself (#1172)") {
    // The poll compares with the switch as it was when the object joined,
    // not with "on".
    Rig r(nullptr);
    r.stop->SetBang(0);
    r.Blocks(1);
    r.Add(YSE::OBJ::D_DSPSTATE);
    r.Blocks(3);
    CHECK(r.Order().empty());
    r.start->SetBang(0);
    r.Blocks(1);
    CHECK(r.Order() == "3210");
    CHECK(r.sinks[0].lastInt == 1);
  }

  TEST_CASE("~dsptime: counts the audio processed since the DSP was turned on (#1172)") {
    SampleRateScope rate(48000);
    Rig r(YSE::OBJ::D_DSPTIME);
    r.handle->SetBang(0);
    REQUIRE(r.sinks[0].lastKind == OrderSink::FLOAT);
    CHECK(r.sinks[0].lastFloat == 0.f);

    r.Blocks(10);
    r.handle->SetBang(0);
    CHECK(r.sinks[0].lastFloat == doctest::Approx(10.0 * BlockMs(48000)));

    // Samples, not the wall clock: a block at another rate adds its own
    // length (the live sample rate).
    YSE::SAMPLERATE = 44100;
    r.Blocks(4);
    r.handle->SetBang(0);
    CHECK(r.sinks[0].lastFloat == doctest::Approx(10.0 * BlockMs(48000) + 4.0 * BlockMs(44100)));
    YSE::SAMPLERATE = 48000;

    // Stopped, nothing is processed and the count holds.
    const float held = r.sinks[0].lastFloat;
    r.stop->SetBang(0);
    r.Blocks(5);
    r.handle->SetBang(0);
    CHECK(r.sinks[0].lastFloat == held);

    // On again, it counts from 0.
    r.start->SetBang(0);
    r.Blocks(3);
    r.handle->SetBang(0);
    CHECK(r.sinks[0].lastFloat == doctest::Approx(3.0 * BlockMs(48000)));
    CHECK(r.p.DspTimeMs() == doctest::Approx(3.0 * BlockMs(48000)));

    // No other report: only bangs send.
    CHECK(r.sinks[0].count == 5);

    // Outside a patcher, nothing has been processed.
    OrderSink sink;
    YSE::PATCHER::pDsptime lone;
    TestHelpers::Wire(lone, 0, sink);
    lone.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(sink.lastKind == OrderSink::FLOAT);
    CHECK(sink.lastFloat == 0.f);
  }

  TEST_CASE("~dspstress: the mean of an interval goes out once it is over, on change (#1172)") {
    OrderSink sink;
    pDspstress stress;
    TestHelpers::Wire(stress, 0, sink);
    CHECK(stress.IntervalMs() == pDspstress::DEFAULT_INTERVAL_MS);

    // 10 ms windows of 2.5 ms blocks: four blocks each.
    stress.GetInlet(0)->SetInt(10, YSE::T_GUI);
    CHECK(stress.IntervalMs() == 10.f);
    stress.Feed(0.1f, 2.5, YSE::T_GUI);
    stress.Feed(0.2f, 2.5, YSE::T_GUI);
    stress.Feed(0.3f, 2.5, YSE::T_GUI);
    CHECK(sink.count == 0);
    stress.Feed(0.4f, 2.5, YSE::T_GUI);
    REQUIRE(sink.count == 1);
    CHECK(sink.lastFloat == doctest::Approx(25.0)); // percent

    // The same mean again is not sent; a different one is.
    for (int i = 0; i < 4; ++i)
      stress.Feed(0.25f, 2.5, YSE::T_GUI);
    CHECK(sink.count == 1);
    for (int i = 0; i < 4; ++i)
      stress.Feed(1.5f, 2.5, YSE::T_GUI);
    REQUIRE(sink.count == 2);
    CHECK(sink.lastFloat == doctest::Approx(150.0)); // cannot keep up: above 100

    // 0 stops reporting, and so does a negative number.
    stress.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    for (int i = 0; i < 20; ++i)
      stress.Feed(0.5f, 2.5, YSE::T_GUI);
    CHECK(sink.count == 2);
    stress.GetInlet(0)->SetFloat(-5.f, YSE::T_GUI);
    CHECK(stress.IntervalMs() == 0.f);
    for (int i = 0; i < 20; ++i)
      stress.Feed(0.5f, 2.5, YSE::T_GUI);
    CHECK(sink.count == 2);

    // On again: a fresh window, none of the blocks fed while off.
    stress.GetInlet(0)->SetFloat(5.f, YSE::T_GUI);
    stress.Feed(0.5f, 2.5, YSE::T_GUI);
    CHECK(sink.count == 2);
    stress.Feed(0.7f, 2.5, YSE::T_GUI);
    REQUIRE(sink.count == 3);
    CHECK(sink.lastFloat == doctest::Approx(60.0));

    // An interval shorter than a block reports every block that changes.
    stress.GetInlet(0)->SetFloat(1.f, YSE::T_GUI);
    stress.Feed(0.1f, 2.5, YSE::T_GUI);
    stress.Feed(0.2f, 2.5, YSE::T_GUI);
    stress.Feed(0.2f, 2.5, YSE::T_GUI);
    CHECK(sink.count == 5);

    // Outside a patcher the poll has nothing to measure.
    stress.Calculate(YSE::T_GUI);
    CHECK(sink.count == 5);
  }

  TEST_CASE("~dspstress: reports the patcher's render load at its interval (#1172)") {
    SampleRateScope rate(48000);
    // A patch without a ~dspstress is not timed.
    Rig r(nullptr);
    r.Blocks(5);
    CHECK(r.p.RenderLoad() == 0.f);

    // Default 250 ms: the first report after the block that reaches it.
    const int perReport = static_cast<int>(std::ceil(250.0 / BlockMs(48000)));
    r.Add(YSE::OBJ::D_DSPSTRESS);
    r.Blocks(perReport - 1);
    CHECK(r.sinks[0].count == 0);
    r.Blocks(1);
    REQUIRE(r.sinks[0].count == 1);
    REQUIRE(r.sinks[0].lastKind == OrderSink::FLOAT);
    // Every timed block takes some time, and a phasor into a ~dac keeps up
    // with real time easily, whatever the machine.
    CHECK(r.sinks[0].lastFloat > 0.f);
    CHECK(r.sinks[0].lastFloat < 100.f);
    CHECK(r.p.RenderLoad() > 0.f);

    // Never more than one report per interval.
    r.Blocks(perReport * 3);
    CHECK(r.sinks[0].count >= 1);
    CHECK(r.sinks[0].count <= 4);

    // A short interval reports (almost) every block: wall-clock time
    // rarely gives the same mean twice, and an unchanged mean is skipped.
    const int before = r.sinks[0].count;
    r.handle->SetFloatData(0, 1.f);
    r.Blocks(20);
    CHECK(r.sinks[0].count > before);
    CHECK(r.sinks[0].count <= before + 20);
    CHECK(r.sinks[0].lastFloat >= 0.f);
    CHECK(std::isfinite(r.sinks[0].lastFloat));

    // 0: no more reports. The DSP switch off: still measured and reported
    // (the drains and polls run), within range.
    r.handle->SetIntData(0, 0);
    const int stopped = r.sinks[0].count;
    r.Blocks(perReport * 2);
    CHECK(r.sinks[0].count == stopped);
    r.stop->SetBang(0);
    r.handle->SetIntData(0, 10);
    r.Blocks(20);
    CHECK(r.sinks[0].count > stopped);
    CHECK(r.sinks[0].lastFloat >= 0.f);
    CHECK(r.sinks[0].lastFloat < 100.f);
  }

  TEST_CASE("dsp status: arguments survive a round trip, numbers sent are not saved (#1172)") {
    YSE::patcher src;
    src.create(1);
    REQUIRE(src.CreateObject(YSE::OBJ::D_DSPSTATE, "") != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::D_DSPTIME, "") != nullptr);
    YSE::pHandle* stress = src.CreateObject(YSE::OBJ::D_DSPSTRESS, "100");
    REQUIRE(stress != nullptr);
    YSE::pHandle* plain = src.CreateObject(YSE::OBJ::D_DSPSTRESS, "");
    REQUIRE(plain != nullptr);
    plain->SetIntData(0, 40);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(1);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 4);
    int states = 0;
    int timers = 0;
    std::vector<std::string> stressParams;
    for (unsigned int i = 0; i < loaded.Objects(); ++i) {
      YSE::pHandle* h = loaded.GetHandleFromList(i);
      const std::string type = h->Type();
      if (type == "~dspstate") states++;
      if (type == "~dsptime") timers++;
      if (type == "~dspstress") stressParams.push_back(h->GetParams());
    }
    CHECK(states == 1);
    CHECK(timers == 1);
    REQUIRE(stressParams.size() == 2);
    const bool hundred = stressParams[0] == "100" || stressParams[1] == "100";
    CHECK(hundred);
    // The number sent to the plain one is runtime state: it loads with the
    // default interval, and its argument stays empty.
    const bool empty = stressParams[0].empty() || stressParams[1].empty();
    CHECK(empty);

    // The argument sets the live interval; no argument is the default.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* set = p.CreateObject(YSE::OBJ::D_DSPSTRESS, "100");
    YSE::pHandle* none = p.CreateObject(YSE::OBJ::D_DSPSTRESS, "");
    CHECK(static_cast<pDspstress*>(p.GetObjectFromID(set->GetID()))->IntervalMs() == 100.f);
    CHECK(static_cast<pDspstress*>(p.GetObjectFromID(none->GetID()))->IntervalMs() ==
          pDspstress::DEFAULT_INTERVAL_MS);
  }

  TEST_CASE("dsp status: bangs, numbers and polled blocks allocate nothing (#1172)") {
    // The sinks come first so they outlive the patcher (issue #967).
    OrderSink stateSinks[4];
    OrderSink timeSink;
    OrderSink stressSink;
    std::vector<std::unique_ptr<YSE::pHandle>> sinkHandles;
    patcherImplementation p(1, nullptr);
    YSE::pHandle* ramp = p.CreateObject(YSE::OBJ::D_PHASOR, "100");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(ramp, 0, dac, 0);
    YSE::pHandle* state = p.CreateObject(YSE::OBJ::D_DSPSTATE, "");
    YSE::pHandle* timer = p.CreateObject(YSE::OBJ::D_DSPTIME, "");
    YSE::pHandle* stress = p.CreateObject(YSE::OBJ::D_DSPSTRESS, "1");
    for (int i = 0; i < 4; ++i) {
      sinkHandles.push_back(std::make_unique<YSE::pHandle>(&stateSinks[i]));
      p.Connect(state, i, sinkHandles.back().get(), 0);
    }
    sinkHandles.push_back(std::make_unique<YSE::pHandle>(&timeSink));
    p.Connect(timer, 0, sinkHandles.back().get(), 0);
    sinkHandles.push_back(std::make_unique<YSE::pHandle>(&stressSink));
    p.Connect(stress, 0, sinkHandles.back().get(), 0);
    YSE::PATCHER::pObject* stateObj = p.GetObjectFromID(state->GetID());
    YSE::PATCHER::pObject* timeObj = p.GetObjectFromID(timer->GetID());
    YSE::PATCHER::pObject* stressObj = p.GetObjectFromID(stress->GetID());
    p.Calculate(YSE::T_DSP); // first block: anything lazily sized is sized now

    const int stateBefore = stateSinks[0].count;
    {
      TestHelpers::ProbeScope probe;
      stateObj->GetInlet(0)->SetBang(YSE::T_DSP);
      timeObj->GetInlet(0)->SetBang(YSE::T_DSP);
      stressObj->GetInlet(0)->SetInt(2, YSE::T_DSP);
      stressObj->GetInlet(0)->SetFloat(1.f, YSE::T_DSP);
      for (int i = 0; i < 8; ++i)
        p.Calculate(YSE::T_DSP);
      // The switch flips: ~dspstate reports from the poll.
      p.SetDspOn(false);
      p.Calculate(YSE::T_DSP);
      p.SetDspOn(true);
      p.Calculate(YSE::T_DSP);
      timeObj->GetInlet(0)->SetBang(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(stateSinks[0].count == stateBefore + 3);
    CHECK(timeSink.count == 2);
    CHECK(stressSink.count >= 1);
  }

  TEST_CASE("dsp status: control threads bang and switch while it renders (#1172)") {
    // Under TSan a plain (non-atomic) count, load or switch shows up here.
    OrderSink timeSink;
    OrderSink stressSink;
    std::vector<std::unique_ptr<YSE::pHandle>> sinkHandles;
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* ramp = p.CreateObject(YSE::OBJ::D_PHASOR, "100");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(ramp, 0, dac, 0);
    REQUIRE(p.CreateObject(YSE::OBJ::D_DSPSTATE, "") != nullptr);
    YSE::pHandle* timer = p.CreateObject(YSE::OBJ::D_DSPTIME, "");
    YSE::pHandle* stress = p.CreateObject(YSE::OBJ::D_DSPSTRESS, "1");
    sinkHandles.push_back(std::make_unique<YSE::pHandle>(&timeSink));
    p.Connect(timer, 0, sinkHandles.back().get(), 0);
    sinkHandles.push_back(std::make_unique<YSE::pHandle>(&stressSink));
    p.Connect(stress, 0, sinkHandles.back().get(), 0);

    std::atomic<bool> done{false};
    std::thread control([&] {
      int i = 0;
      while (!done.load()) {
        dac->SetListData(0, (i % 3) != 0 ? "start" : "stop");
        stress->SetIntData(0, (i % 2) != 0 ? 1 : 3);
        ++i;
      }
    });
    YSE::DSP::patcherInsert insert(p);
    MULTICHANNELBUFFER io;
    io.resize(1);
    io[0].resize(STANDARD_BUFFERSIZE);
    for (int block = 0; block < 2000; ++block) {
      io[0] = 0.f;
      insert.process(io);
    }
    done.store(true);
    control.join();
    dac->SetListData(0, "start");
    // Banged on this thread once the renders are done: the count is a whole
    // number of blocks.
    timer->SetBang(0);
    REQUIRE(timeSink.lastKind == OrderSink::FLOAT);
    const double blocks = static_cast<double>(timeSink.lastFloat) / BlockMs(YSE::SAMPLERATE);
    CHECK(blocks >= 0.0);
    CHECK(std::fabs(blocks - std::round(blocks)) < 1e-3);
    CHECK(stressSink.lastFloat >= 0.f);
  }

} // TEST_SUITE("patcher")
