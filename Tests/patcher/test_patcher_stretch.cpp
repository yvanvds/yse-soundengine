// Tests for the ~stretch patcher object (patcher/sampling/dStretch), issue
// #1152. The stretching is DSP::timeStretch's and is tested in
// Tests/dsp/test_time_stretch.cpp; these run ~stretch in a patcher on a
// named buffer: the shape, the attribute words reaching the DSP class (the
// result bit for bit against it), the done bang and the progress outlet,
// readagain, set, followglobaltempo on a domain clock, cancel (waiting and
// running, buffer untouched), the settings round trip, nothing allocating on
// the audio thread, and, in a rendered patch after a JSON round trip, a sine
// buffer stretched to twice its length at its pitch and then shifted an
// octave at its length, played by ~play into ~dac.

#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "clock/clockManager.h"
#include "dsp/sampling/sampleBuffer.hpp"
#include "dsp/sampling/timeStretch.hpp"
#include "headers/constants.hpp"
#include "patcher/io/fileScheduler.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcherImplementation.h"
#include "patcher/sampling/dStretch.h"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "pool_blocker.hpp"
#include "sinks.hpp"

using TestHelpers::OrderSink;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferRegistry;
using YSE::DSP::timeStretch;
using YSE::PATCHER::dStretch;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;
  constexpr double PI = 3.14159265358979323846;

  // Every float it is sent, in a fixed store, so recording allocates nothing.
  struct ProgressSink : TestHelpers::SinkBase {
    std::array<float, 512> values{};
    std::size_t count = 0;
    ProgressSink() {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
        if (count < values.size()) values[count] = v;
        count++;
      });
    }
    const char* Type() const override {
      return "progress_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
    std::vector<float> Seen() const {
      return std::vector<float>(values.begin(),
                                values.begin() +
                                    static_cast<std::ptrdiff_t>(std::min(count, values.size())));
    }
  };

  // A ~stretch in a patcher, with a sink on each outlet. The sinks come
  // first so they outlive the patcher (issue #967).
  struct Rig {
    OrderSink done;
    ProgressSink progress;
    std::vector<std::unique_ptr<YSE::pHandle>> sinkHandles;
    patcherImplementation p{1, nullptr};
    YSE::pHandle* handle = nullptr;
    dStretch* obj = nullptr;

    explicit Rig(const std::string& args) {
      handle = p.CreateObject(YSE::OBJ::D_STRETCH, args);
      REQUIRE(handle != nullptr);
      obj = static_cast<dStretch*>(p.GetObjectFromID(handle->GetID()));
      sinkHandles.push_back(std::make_unique<YSE::pHandle>(&done));
      p.Connect(handle, 0, sinkHandles.back().get(), 0);
      sinkHandles.push_back(std::make_unique<YSE::pHandle>(&progress));
      p.Connect(handle, 1, sinkHandles.back().get(), 0);
    }

    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      obj->GetInlet(0)->SetMessage(text, thread);
    }

    void Bang(YSE::THREAD thread = YSE::T_GUI) {
      obj->GetInlet(0)->SetBang(thread);
    }

    // The jobs run on the background pool (WaitIdle joins them); their
    // results are handed out at the top of the next block.
    void Settle() {
      YSE::PATCHER::fileScheduler* io = p.FileIO();
      REQUIRE(io != nullptr);
      io->WaitIdle();
      p.Calculate(YSE::T_DSP);
    }

    // Blocks until the done bang has come `bangs` times, or 60 s.
    bool RenderUntilDone(int bangs) {
      const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
      while (done.count < bangs) {
        if (std::chrono::steady_clock::now() > until) return false;
        p.Calculate(YSE::T_DSP);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      return true;
    }
  };

  std::vector<float> Sine(double hz, std::size_t frames, double rate = 48000.0) {
    std::vector<float> v(frames);
    for (std::size_t i = 0; i < frames; ++i)
      v[i] = static_cast<float>(0.5 * std::sin(2.0 * PI * hz * static_cast<double>(i) / rate));
    return v;
  }

  std::shared_ptr<sampleBuffer> SineBuffer(const std::string& name, std::size_t frames,
                                           double hz = 440.0) {
    auto b = sampleBufferRegistry::acquire(name);
    const std::vector<float> s = Sine(hz, frames);
    b->setContents(1, static_cast<UInt>(frames), s.data());
    b->setSampleRate(48000.f);
    return b;
  }

  std::vector<float> Samples(const sampleBuffer& b, UInt channel = 0) {
    std::vector<float> v(b.frames());
    v.resize(b.getSamples(channel, 0, v.data(), b.frames()));
    return v;
  }

  // The amplitude of `v` at `hz`, Hann-windowed.
  double Amplitude(const std::vector<float>& v, double hz, double rate = 48000.0) {
    double re = 0.0;
    double im = 0.0;
    double sum = 0.0;
    const double w = 2.0 * PI * hz / rate;
    for (std::size_t n = 0; n < v.size(); ++n) {
      const double h =
          0.5 - 0.5 * std::cos(2.0 * PI * static_cast<double>(n) / static_cast<double>(v.size()));
      re += h * v[n] * std::cos(w * static_cast<double>(n));
      im -= h * v[n] * std::sin(w * static_cast<double>(n));
      sum += h;
    }
    return 2.0 * std::hypot(re, im) / sum;
  }

  // The loudest frequency of `v` between `low` and `high` Hz, to 0.05 Hz.
  double Loudest(const std::vector<float>& v, double low, double high) {
    double best = low;
    double level = -1.0;
    const auto steps = static_cast<int>((high - low) / 2.0);
    for (int i = 0; i <= steps; ++i) {
      const double hz = low + 2.0 * i;
      const double a = Amplitude(v, hz);
      if (a > level) {
        level = a;
        best = hz;
      }
    }
    const double centre = best;
    for (int i = 0; i <= 80; ++i) {
      const double hz = centre - 2.0 + 0.05 * i;
      const double a = Amplitude(v, hz);
      if (a > level) {
        level = a;
        best = hz;
      }
    }
    return best;
  }

  std::vector<float> Middle(const std::vector<float>& v) {
    const std::size_t quarter = v.size() / 4;
    return std::vector<float>(v.begin() + static_cast<std::ptrdiff_t>(quarter),
                              v.end() - static_cast<std::ptrdiff_t>(quarter));
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~stretch: registry and shape (#1152)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_STRETCH, "shape_1152");
    REQUIRE(h != nullptr);
    CHECK(h->GetInputs() == 1);
    REQUIRE(h->GetOutputs() == 2);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BANG);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::FLOAT);

    bool listed = false;
    for (const auto& name : YSE::PATCHER::Register().AllNames())
      if (name == YSE::OBJ::D_STRETCH) listed = true;
    CHECK(listed);

    dStretch standalone;
    CHECK(standalone.GetCategory() == YSE::PATCHER::pCategory::SAMPLING);
    CHECK(standalone.WantsBlockPoll());
  }

  TEST_CASE("~stretch: the attribute words reach the DSP class, by name and by number "
            "(#1152)") {
    dStretch obj;
    timeStretch ts = obj.Stretcher();
    CHECK(ts.stretch() == 1.f);
    CHECK(ts.pitch() == 1.f);
    CHECK(ts.quality() == timeStretch::BETTER);
    CHECK(ts.mode() == timeStretch::MODE_BASIC);
    CHECK_FALSE(ts.pitchCorrection());
    CHECK(ts.standardPitch() == 440.f);

    obj.SetParams("named_1152 stretch 1.5 pitchshift 0.5 quality best mode rhythmic "
                  "pitchcorrection 1 basictuning 442");
    CHECK(std::string(obj.BufferName()) == "named_1152");
    ts = obj.Stretcher();
    CHECK(ts.stretch() == 1.5f);
    CHECK(ts.pitch() == 0.5f);
    CHECK(ts.quality() == timeStretch::BEST);
    CHECK(ts.mode() == timeStretch::RHYTHMIC);
    CHECK(ts.pitchCorrection());
    CHECK(ts.standardPitch() == 442.f);

    // Messages; pitchshiftcent is the same factor in cents.
    obj.GetInlet(0)->SetMessage("pitchshiftcent 700", YSE::T_GUI);
    CHECK(obj.Stretcher().pitch() == doctest::Approx(std::pow(2.0, 7.0 / 12.0)));
    obj.GetInlet(0)->SetMessage("quality 0", YSE::T_GUI);
    CHECK(obj.Stretcher().quality() == timeStretch::BASIC);
    const char* modes[] = {"basic",      "monophonic", "rhythmic",       "general",
                           "polyphonic", "pads",       "extremestretch", "efficient"};
    for (int m = 0; m < 8; ++m) {
      obj.GetInlet(0)->SetMessage(std::string("mode ") + modes[m], YSE::T_GUI);
      CHECK(obj.Stretcher().mode() == m);
    }
    obj.GetInlet(0)->SetMessage("mode 3", YSE::T_GUI);
    CHECK(obj.Stretcher().mode() == timeStretch::GENERAL);
    // Bad values change nothing; the ignored words are accepted.
    obj.GetInlet(0)->SetMessage("mode nonsense", YSE::T_GUI);
    obj.GetInlet(0)->SetMessage("quality 9", YSE::T_GUI);
    obj.GetInlet(0)->SetMessage("stretch x", YSE::T_GUI);
    obj.GetInlet(0)->SetMessage("formant 1.2", YSE::T_GUI);
    obj.GetInlet(0)->SetMessage("formantcorrection 1", YSE::T_GUI);
    obj.GetInlet(0)->SetMessage("slurtime 50", YSE::T_GUI);
    obj.GetInlet(0)->SetMessage("dictionary markers", YSE::T_GUI);
    CHECK(obj.Stretcher().mode() == timeStretch::GENERAL);
    CHECK(obj.Stretcher().quality() == timeStretch::BASIC);
    CHECK(obj.Stretcher().stretch() == 1.5f);
    // Set names another buffer; set alone none.
    obj.GetInlet(0)->SetMessage("set other_1152", YSE::T_GUI);
    CHECK(std::string(obj.BufferName()) == "other_1152");
    obj.GetInlet(0)->SetMessage("set", YSE::T_GUI);
    CHECK(std::string(obj.BufferName()).empty());

    // Empty settings are the defaults again.
    obj.SetParams("");
    CHECK(obj.Stretcher().stretch() == 1.f);
    CHECK(obj.Stretcher().quality() == timeStretch::BETTER);
  }

  TEST_CASE("~stretch: a bang stretches the buffer in the background, bit for bit as the DSP "
            "class does, then reports 1 and bangs (#1152)") {
    auto buffer = SineBuffer("bang_1152", 24000);
    auto expected = sampleBuffer::create();
    expected->copyFrom(*buffer);

    Rig r("bang_1152 stretch 1.5 pitchshiftcent 300 quality good mode general");
    r.p.Calculate(YSE::T_DSP);
    r.Bang();
    // Nothing yet: the result comes at the top of a later block.
    CHECK(r.done.count == 0);
    r.Settle();
    CHECK(r.done.count == 1);
    CHECK(r.done.lastKind == OrderSink::BANG);
    REQUIRE(r.progress.count >= 1);
    CHECK(r.progress.Seen().back() == 1.f);
    CHECK(buffer->frames() == 36000);

    REQUIRE(r.obj->Stretcher().process(*expected, *expected));
    CHECK(Samples(*buffer) == Samples(*expected));
    CHECK(r.obj->FailedTasks() == 0);
    CHECK(r.obj->CancelledTasks() == 0);

    // `apply` is the same as bang.
    r.Message("apply");
    r.Settle();
    CHECK(r.done.count == 2);
  }

  TEST_CASE("~stretch: progress rises to 1 while a long job runs (#1152)") {
    auto buffer = SineBuffer("progress_1152", 48000 * 10);
    Rig r("progress_1152 stretch 4 quality best");
    r.p.Calculate(YSE::T_DSP);
    r.Bang();
    REQUIRE(r.RenderUntilDone(1));
    const std::vector<float> seen = r.progress.Seen();
    REQUIRE(seen.size() >= 2);
    CHECK(std::is_sorted(seen.begin(), seen.end()));
    CHECK(seen.front() < 1.f);
    CHECK(seen.back() == 1.f);
    CHECK(buffer->frames() == 48000 * 40);

    // progress_enable 0: the bang alone.
    r.Message("progress_enable 0");
    r.Message("stretch 0.5");
    const std::size_t before = r.progress.count;
    r.Bang();
    REQUIRE(r.RenderUntilDone(2));
    CHECK(r.progress.count == before);
    CHECK(buffer->frames() == 48000 * 5); // from the original (readagain)
  }

  TEST_CASE("~stretch: readagain processes the original; a buffer changed since is the new "
            "original; readagain 0 processes what is there (#1152)") {
    auto buffer = SineBuffer("again_1152", 10000);
    Rig r("again_1152 stretch 2");
    r.Bang();
    r.Settle();
    CHECK(buffer->frames() == 20000);
    r.Bang();
    r.Settle();
    CHECK(buffer->frames() == 20000); // not a stretch of the stretch
    r.Message("stretch 3");
    r.Bang();
    r.Settle();
    CHECK(buffer->frames() == 30000);

    // Changed by someone else: that is the original now.
    const std::vector<float> other = Sine(220.0, 1000);
    buffer->setContents(1, 1000, other.data());
    r.Bang();
    r.Settle();
    CHECK(buffer->frames() == 3000);

    r.Message("readagain 0");
    r.Message("stretch 2");
    r.Bang();
    r.Settle();
    CHECK(buffer->frames() == 6000);
    r.Bang();
    r.Settle();
    CHECK(buffer->frames() == 12000);
    CHECK(r.done.count == 6);
  }

  TEST_CASE("~stretch: set picks another buffer; an unknown or no name stretches nothing "
            "(#1152)") {
    auto a = SineBuffer("set_a_1152", 1000);
    auto b = SineBuffer("set_b_1152", 2000);
    Rig r("set_a_1152 stretch 2");
    r.Message("set set_b_1152");
    r.Bang();
    r.Settle();
    CHECK(a->frames() == 1000);
    CHECK(b->frames() == 4000);

    r.Message("set no_such_buffer_1152");
    r.Bang();
    r.Settle();
    CHECK(r.obj->FailedTasks() == 1);
    CHECK(r.done.count == 1);

    r.Message("set");
    r.Bang();
    r.Settle();
    CHECK(r.obj->RefusedRequests() == 1);
    CHECK(r.done.count == 1);

    // Without a patcher a bang does nothing.
    dStretch standalone;
    standalone.SetParams("set_a_1152");
    standalone.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(standalone.RefusedRequests() == 1);
    CHECK(a->frames() == 1000);
  }

  TEST_CASE("~stretch: followglobaltempo fits the buffer to a domain clock's tempo, from "
            "originaltempo or originallength (#1152)") {
    const std::string clock = "stretch_1152.clock";
    REQUIRE(YSE::CLOCK::Manager().createClock(clock, 60.f));
    auto buffer = SineBuffer("tempo_1152", 48000); // one second at 48 kHz
    Rig r("tempo_1152 stretch 3 followglobaltempo 1 transport " + clock + " originaltempo 120");
    r.Bang();
    r.Settle();
    CHECK(buffer->frames() == 96000); // 120 BPM played at 60: twice as long

    // Four beats in the second: 240 BPM, at 120 twice as long.
    const std::string faster = "stretch_1152.faster";
    REQUIRE(YSE::CLOCK::Manager().createClock(faster, 120.f));
    r.Message("transport " + faster);
    r.Message("originallength 4");
    r.Bang();
    r.Settle();
    CHECK(buffer->frames() == 96000);
    // At 60, four times.
    r.Message("transport " + clock);
    r.Bang();
    r.Settle();
    CHECK(buffer->frames() == 192000);
    // The same in ticks and as a note value: a whole note is four beats.
    r.Message("transport " + faster);
    r.Message("originallength 1920 ticks");
    r.Bang();
    r.Settle();
    CHECK(buffer->frames() == 96000);
    r.Message("originallength 1n");
    r.Bang();
    r.Settle();
    CHECK(buffer->frames() == 96000);
    // originaltempo set after it wins again: 120 at 120.
    r.Message("originaltempo 120");
    r.Bang();
    r.Settle();
    CHECK(buffer->frames() == 48000);

    // No clock of that name, or followglobaltempo 0: the stretch attribute.
    r.Message("transport no_such_clock_1152");
    r.Bang();
    r.Settle();
    CHECK(buffer->frames() == 144000);
    r.Message("transport " + clock);
    r.Message("followglobaltempo 0");
    r.Bang();
    r.Settle();
    CHECK(buffer->frames() == 144000);
    YSE::CLOCK::Manager().destroyClock(clock);
    YSE::CLOCK::Manager().destroyClock(faster);
  }

  TEST_CASE("~stretch: cancel stops a waiting job and leaves the buffer untouched (#1152)") {
    auto buffer = SineBuffer("cancel_wait_1152", 24000);
    const std::uint64_t version = buffer->version();
    const std::vector<float> before = Samples(*buffer);
    Rig r("cancel_wait_1152 stretch 2");
    r.p.Calculate(YSE::T_DSP);
    {
      TestHelpers::PoolBlocker blocker;
      REQUIRE(blocker.Park());
      r.Bang();
      r.Bang();
      r.Message("cancel");
      blocker.Unpark();
    }
    r.Settle();
    CHECK(buffer->version() == version);
    CHECK(Samples(*buffer) == before);
    CHECK(r.done.count == 0);
    CHECK(r.progress.count == 0);
    CHECK(r.obj->CancelledTasks() == 2);

    // A bang after the cancel runs.
    r.Bang();
    r.Settle();
    CHECK(r.done.count == 1);
    CHECK(buffer->frames() == 48000);
  }

  TEST_CASE("~stretch: cancel stops a running job between two frames, buffer untouched "
            "(#1152)") {
    auto buffer = SineBuffer("cancel_run_1152", 48000 * 20);
    const std::uint64_t version = buffer->version();
    Rig r("cancel_run_1152 stretch 8 quality best");
    r.p.Calculate(YSE::T_DSP);
    r.Bang();
    // Wait until it is running: a progress value has come out.
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (r.progress.count == 0 && std::chrono::steady_clock::now() < until) {
      r.p.Calculate(YSE::T_DSP);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(r.progress.count > 0);
    REQUIRE(r.done.count == 0);
    r.Message("cancel");
    r.Settle();
    CHECK(r.done.count == 0);
    CHECK(r.obj->CancelledTasks() == 1);
    CHECK(buffer->version() == version);
    CHECK(buffer->frames() == 48000 * 20);
    CHECK(r.progress.Seen().back() < 1.f);
  }

  TEST_CASE("~stretch: deleting the object cancels its waiting job (#1152)") {
    auto buffer = SineBuffer("delete_1152", 24000);
    const std::uint64_t version = buffer->version();
    {
      patcherImplementation p(1, nullptr);
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_STRETCH, "delete_1152 stretch 2");
      REQUIRE(h != nullptr);
      p.Calculate(YSE::T_DSP);
      TestHelpers::PoolBlocker blocker;
      REQUIRE(blocker.Park());
      h->SetBang(0);
      p.DeleteObject(h);
      p.Calculate(YSE::T_DSP);
      p.Calculate(YSE::T_DSP);
      blocker.Unpark();
      p.FileIO()->WaitIdle();
    }
    CHECK(buffer->version() == version);
    CHECK(buffer->frames() == 24000);
  }

  TEST_CASE("~stretch: the settings survive GetParams and a DumpJSON / ParseJSON round trip "
            "(#1152)") {
    const std::string args = "rt_1152 stretch 2 pitchshiftcent -500 quality best mode pads "
                             "readagain 0 pitchcorrection 1 basictuning 441 followglobaltempo 1 "
                             "originaltempo 96 transport rt_clock progress_enable 0";
    dStretch obj;
    obj.SetParams(args);
    CHECK(obj.GetParams() == args);
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_STRETCH, args) != nullptr);
      dump = src.DumpJSON();
    }
    CHECK(dump.find("~stretch") != std::string::npos);
    CHECK(dump.find("mode pads") != std::string::npos);
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
  }

  TEST_CASE("~stretch: messages, the progress poll and the done delivery on the audio thread "
            "allocate nothing (#1152)") {
    auto buffer = SineBuffer("rt_alloc_1152", 48000 * 10);
    Rig r("rt_alloc_1152 quality best");
    const std::string stretch = "stretch 4";
    const std::string cents = "pitchshiftcent 200";
    const std::string mode = "mode rhythmic";
    const std::string quality = "quality good";
    const std::string set = "set rt_alloc_1152";
    const std::string transport = "transport some_clock";
    const std::string length = "originallength 1920 ticks";
    const std::string cancel = "cancel";
    const std::string bang = "bang";
    r.done.lastList.reserve(64);
    r.p.Calculate(YSE::T_DSP);
    {
      TestHelpers::ProbeScope probe;
      r.Message(stretch, YSE::T_DSP);
      r.Message(cents, YSE::T_DSP);
      r.Message(mode, YSE::T_DSP);
      r.Message(quality, YSE::T_DSP);
      r.Message(set, YSE::T_DSP);
      r.Message(transport, YSE::T_DSP);
      r.Message(length, YSE::T_DSP);
      r.Message(bang, YSE::T_DSP);
      r.Message(cancel, YSE::T_DSP);
      r.Bang(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    // The job runs while blocks poll its progress and then deliver the bang.
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    int blocks = 0;
    while (r.done.count < 1 && std::chrono::steady_clock::now() < until) {
      {
        TestHelpers::ProbeScope probe;
        r.p.Calculate(YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      blocks++;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(r.done.count == 1);
    CHECK(r.progress.count >= 1);
    CHECK(r.obj->CancelledTasks() == 1);
    CHECK(blocks > 0);
  }

  TEST_CASE("~stretch: in a rendered patch after a JSON round trip, a sine buffer is stretched "
            "to twice its length at its pitch, then shifted an octave at its own length, and "
            "~play plays each result as it is done (#1152)") {
    TestHelpers::ScopedSampleRate rate(48000);
    std::string dump;
    unsigned int progressID = 0;
    {
      patcherImplementation src(1, nullptr);
      YSE::pHandle* bufferCmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "st1152buf");
      YSE::pHandle* buf = src.CreateObject(YSE::OBJ::D_BUFFER, "st1152 500");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "st1152cmd");
      YSE::pHandle* stretch =
          src.CreateObject(YSE::OBJ::D_STRETCH, "st1152 stretch 2 quality good");
      YSE::pHandle* start = src.CreateObject(YSE::OBJ::G_MESSAGE, "1");
      YSE::pHandle* play = src.CreateObject(YSE::OBJ::D_PLAY, "st1152");
      YSE::pHandle* progress = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      for (YSE::pHandle* h : {bufferCmd, buf, cmd, stretch, start, play, progress, dac})
        REQUIRE(h != nullptr);
      src.Connect(bufferCmd, 0, buf, 0);
      src.Connect(cmd, 0, stretch, 0);
      src.Connect(stretch, 0, start, 0);
      src.Connect(start, 0, play, 0);
      src.Connect(stretch, 1, progress, 0);
      src.Connect(play, 0, dac, 0);
      progressID = progress->GetID();
      dump = src.DumpJSON();
    }
    CHECK(dump.find("st1152 stretch 2 quality good") != std::string::npos);
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* progress = dst.GetHandleFromID(progressID);
    REQUIRE(progress != nullptr);
    YSE::PATCHER::fileScheduler* io = dst.FileIO();
    REQUIRE(io != nullptr);

    // 500 ms of a 440 Hz sine: 220 cycles.
    dst.Calculate(YSE::T_DSP);
    REQUIRE(dst.PassData(std::string("fill sin 220"), "st1152buf", YSE::T_GUI));
    io->WaitIdle();
    dst.Calculate(YSE::T_DSP);
    auto buffer = sampleBufferRegistry::find("st1152");
    REQUIRE(buffer != nullptr);
    REQUIRE(buffer->frames() == 24000);

    // Render until the job is done and ~play has played the result out.
    const auto run = [&](int blocks) {
      std::vector<float> out;
      for (int k = 0; k < blocks; ++k) {
        io->WaitIdle();
        dst.Calculate(YSE::T_DSP);
        out.insert(out.end(), dst.output[0].getPtr(), dst.output[0].getPtr() + N);
      }
      return out;
    };
    const auto sounding = [](const std::vector<float>& out) {
      std::size_t first = out.size();
      std::size_t last = 0;
      for (std::size_t i = 0; i < out.size(); ++i) {
        if (std::abs(out[i]) > 0.05f) {
          first = std::min(first, i);
          last = i;
        }
      }
      return first < out.size()
                 ? std::vector<float>(out.begin() + static_cast<std::ptrdiff_t>(first),
                                      out.begin() + static_cast<std::ptrdiff_t>(last + 1))
                 : std::vector<float>();
    };

    REQUIRE(dst.PassData(std::string("bang"), "st1152cmd", YSE::T_GUI));
    std::vector<float> played = sounding(run(600)); // 1.6 s
    CHECK(std::stof(progress->GetGuiValue()) == 1.f);
    CHECK(buffer->frames() == 48000);
    // Twice as long, as played: a second.
    CHECK(std::abs(static_cast<double>(played.size()) - 48000.0) < 400.0);
    CHECK(std::abs(Loudest(Middle(played), 350.0, 530.0) - 440.0) < 0.002 * 440.0);

    // An octave up at the original length: readagain works from the 500 ms.
    REQUIRE(dst.PassData(std::string("stretch 1"), "st1152cmd", YSE::T_GUI));
    REQUIRE(dst.PassData(std::string("pitchshiftcent 1200"), "st1152cmd", YSE::T_GUI));
    REQUIRE(dst.PassData(std::string("bang"), "st1152cmd", YSE::T_GUI));
    played = sounding(run(400));
    CHECK(buffer->frames() == 24000);
    CHECK(std::abs(static_cast<double>(played.size()) - 24000.0) < 400.0);
    CHECK(std::abs(Loudest(Middle(played), 700.0, 1060.0) - 880.0) < 0.002 * 880.0);
    CHECK(Amplitude(Middle(played), 440.0) < 0.01);
  }

} // TEST_SUITE("patcher")
