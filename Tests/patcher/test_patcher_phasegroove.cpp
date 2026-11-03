// Tests for the ~phasegroove patcher object (patcher/sampling/dPhaseGroove),
// issue #1142. The reading itself is DSP::groovePlayer::phase's
// (Tests/dsp/test_groove_player.cpp) and the reader is
// DSP::sampleBufferReader's (test_sampleBuffer.cpp). These show that pins,
// messages and arguments reach them, in rendered patches heard through a
// ~dac: a ~phasor playing a ~buffer's loop, with the ramp itself on the second
// ~dac channel; a ~sig phase and the sync outlet; numbers on an unconnected
// inlet; the loop points by inlet, signal, word and argument; the crossfade;
// several channels; set (#1256); a round trip; allocation-free blocks and
// messages on the audio thread; and playback while other threads edit and
// rebind. The buffers hold a ramp (sample n is n) at 1000 Hz and the engine
// runs at 1000 Hz, so a millisecond is a frame and a sample.

#include <doctest/doctest.h>
#include <atomic>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "dsp/sampling/sampleBuffer.hpp"
#include "headers/constants.hpp"
#include "patcher/io/fileScheduler.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sampling/dPhaseGroove.h"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferRegistry;
using YSE::PATCHER::dPhaseGroove;
using YSE::PATCHER::patcherImplementation;

namespace {

  constexpr float RATE = 1000.f; // a frame per millisecond
  constexpr UInt N = STANDARD_BUFFERSIZE;

  // A named buffer of `frames` frames at 1000 Hz: channel c's sample n is
  // n + 1000 c.
  std::shared_ptr<sampleBuffer> Ramp(const std::string& name, UInt channels, UInt frames) {
    std::shared_ptr<sampleBuffer> b = sampleBufferRegistry::acquire(name);
    b->setSize(frames, static_cast<Int>(channels));
    b->setSampleRate(RATE);
    std::vector<float> values(frames);
    for (UInt c = 0; c < channels; ++c) {
      for (UInt n = 0; n < frames; ++n)
        values[n] = static_cast<float>(n) + 1000.f * static_cast<float>(c);
      b->setSamples(c, 0, values.data(), frames);
    }
    return b;
  }

  // [~sig phase] -> [~phasegroove args] -> [~dac]: the first sample outlet on
  // ~dac channel 0, the sync outlet on channel 1 (or, with two channels, the
  // second sample outlet there). No `phaseArgs`: the inlet stays unconnected.
  struct PhaseRig {
    ScopedSampleRate rate{1000};
    patcherImplementation p{2, nullptr};
    YSE::pHandle* phase = nullptr;
    YSE::pHandle* handle = nullptr;
    YSE::pHandle* dac = nullptr;
    dPhaseGroove* groove = nullptr;

    explicit PhaseRig(const std::string& args, const char* phaseArgs = "0") {
      handle = p.CreateObject(YSE::OBJ::D_PHASEGROOVE, args);
      REQUIRE(handle != nullptr);
      dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(dac != nullptr);
      groove = static_cast<dPhaseGroove*>(p.GetObjectFromID(handle->GetID()));
      if (phaseArgs != nullptr) {
        phase = p.CreateObject(YSE::OBJ::D_SIG, phaseArgs);
        REQUIRE(phase != nullptr);
        p.Connect(phase, 0, handle, 0);
      }
      p.Connect(handle, 0, dac, 0);
      p.Connect(handle, groove->Channels() > 1 ? 1 : groove->Channels(), dac, 1);
    }

    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      groove->GetInlet(0)->SetMessage(text, thread);
    }

    void Phase(float value) {
      phase->SetFloatData(0, value);
    }

    float Out(UInt i, UInt channel = 0) {
      return p.output[channel].getPtr()[i];
    }

    float Sync(UInt i) {
      return Out(i, 1);
    }

    bool Silent(UInt from = 0) {
      for (UInt i = from; i < N; ++i)
        if (Out(i) != 0.f) return false;
      return true;
    }

    // One block; the value it held throughout (the phase is constant).
    float Held() {
      p.Calculate(YSE::T_DSP);
      return Out(N - 1);
    }

    // The bind task runs on the background pool, the next block takes the
    // reader, and the task that frees the old one runs after it.
    void Settle() {
      YSE::PATCHER::fileScheduler* io = p.FileIO();
      REQUIRE(io != nullptr);
      io->WaitIdle();
      p.Calculate(YSE::T_DSP);
      io->WaitIdle();
    }
  };

} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~phasegroove: registry and shape (#1142)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_PHASEGROOVE, "shape_1142");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~phasegroove");
    CHECK(h->GetInputs() == 3); // phase, loop start, loop end
    REQUIRE(h->GetOutputs() == 2); // the signal, sync
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::BUFFER);

    // The channel count sets the sample outlets; sync stays rightmost.
    YSE::pHandle* three = p.CreateObject(YSE::OBJ::D_PHASEGROOVE, "shape_1142 3");
    REQUIRE(three != nullptr);
    CHECK(three->GetOutputs() == 4);
    YSE::pHandle* many = p.CreateObject(YSE::OBJ::D_PHASEGROOVE, "shape_1142 500");
    REQUIRE(many != nullptr);
    CHECK(many->GetOutputs() == 65);
  }

  TEST_CASE("~phasegroove: a ~phasor plays a ~buffer's loop once per cycle (#1142)") {
    // The patch a user builds: [~phasor 5] -> [~phasegroove tape] -> [~dac],
    // with [setloop 100 200( into the left inlet and the ramp itself on the
    // second ~dac channel to compare with.
    ScopedSampleRate rate(1000);
    patcherImplementation p(2, nullptr);
    YSE::pHandle* buffer = p.CreateObject(YSE::OBJ::D_BUFFER, "tape_1142");
    YSE::pHandle* phasor = p.CreateObject(YSE::OBJ::D_PHASOR, "5"); // 200 samples a cycle
    YSE::pHandle* setloop = p.CreateObject(YSE::OBJ::G_MESSAGE, "setloop 100 200");
    YSE::pHandle* groove = p.CreateObject(YSE::OBJ::D_PHASEGROOVE, "tape_1142");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(buffer != nullptr);
    REQUIRE(phasor != nullptr);
    REQUIRE(setloop != nullptr);
    REQUIRE(groove != nullptr);
    p.Connect(phasor, 0, groove, 0);
    p.Connect(setloop, 0, groove, 0);
    p.Connect(groove, 0, dac, 0);
    p.Connect(phasor, 0, dac, 1);
    auto tape = Ramp("tape_1142", 1, 1000);
    REQUIRE(sampleBufferRegistry::find("tape_1142") == tape);

    // The whole buffer first: the default loop.
    p.Calculate(YSE::T_DSP);
    for (UInt i = 0; i < N; ++i) {
      CAPTURE(i);
      CHECK(p.output[0].getPtr()[i] ==
            doctest::Approx(1000.f * p.output[1].getPtr()[i]).epsilon(0.001));
    }

    setloop->SetBang(0);
    int wraps = 0;
    float last = 0.f;
    for (int block = 0; block < 8; ++block) {
      p.Calculate(YSE::T_DSP);
      for (UInt i = 0; i < N; ++i) {
        CAPTURE(block);
        CAPTURE(i);
        const float phase = p.output[1].getPtr()[i];
        CHECK(p.output[0].getPtr()[i] == doctest::Approx(100.f + 100.f * phase).epsilon(0.001));
        if (phase < last) wraps++;
        last = phase;
      }
    }
    CHECK(wraps >= 4); // 1024 samples at 200 a cycle
  }

  TEST_CASE("~phasegroove: a ~phasor running down plays it backward; the sync is the phase "
            "(#1142)") {
    auto b = Ramp("down_1142", 1, 1000);
    ScopedSampleRate rate(1000);
    patcherImplementation p(2, nullptr);
    YSE::pHandle* phasor = p.CreateObject(YSE::OBJ::D_PHASOR, "-5");
    YSE::pHandle* groove = p.CreateObject(YSE::OBJ::D_PHASEGROOVE, "down_1142 1 0 50 100 200");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(phasor != nullptr);
    REQUIRE(groove != nullptr);
    p.Connect(phasor, 0, groove, 0);
    p.Connect(groove, 0, dac, 0);
    p.Connect(groove, 1, dac, 1);
    int falling = 0;
    float last = 0.f;
    for (int block = 0; block < 3; ++block) {
      p.Calculate(YSE::T_DSP);
      for (UInt i = 0; i < N; ++i) {
        const float sync = p.output[1].getPtr()[i];
        CHECK(sync >= 0.f);
        CHECK(sync <= 1.f);
        CHECK(p.output[0].getPtr()[i] == doctest::Approx(100.f + 100.f * sync).epsilon(0.001));
        if (sync < last) falling++;
        last = sync;
      }
    }
    CHECK(falling > 300);
  }

  TEST_CASE("~phasegroove: a ~sig phase, wrapped, with the sync outlet (#1142)") {
    auto b = Ramp("sig_1142", 1, 1000);
    PhaseRig r("sig_1142 1 0 50 100 200", "0.25");
    CHECK(r.Held() == doctest::Approx(125.f));
    CHECK(r.Sync(0) == doctest::Approx(0.25f));
    r.Phase(1.5f); // an offset ramp wraps round the loop
    CHECK(r.Held() == doctest::Approx(150.f));
    CHECK(r.Sync(0) == doctest::Approx(0.5f));
    r.Phase(-0.25f);
    CHECK(r.Held() == doctest::Approx(175.f));
    r.Phase(1.f);
    CHECK(r.Held() == doctest::Approx(200.f));
    CHECK(r.Sync(0) == 1.f);
  }

  TEST_CASE("~phasegroove: an unconnected phase is the last number; a connected signal wins, and "
            "the number comes back with the cord (#1142)") {
    auto b = Ramp("const_1142", 1, 1000);
    PhaseRig r("const_1142 1 0 50 100 200", nullptr);
    CHECK(r.Held() == doctest::Approx(100.f)); // nothing yet: 0
    r.groove->GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(r.Held() == doctest::Approx(150.f));
    r.groove->GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(r.Held() == doctest::Approx(200.f));
    r.groove->GetInlet(0)->SetFloat(std::numeric_limits<float>::quiet_NaN(), YSE::T_GUI);
    CHECK(r.Held() == doctest::Approx(200.f));

    r.groove->GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    YSE::pHandle* sig = r.p.CreateObject(YSE::OBJ::D_SIG, "0.25");
    REQUIRE(sig != nullptr);
    r.p.Connect(sig, 0, r.handle, 0);
    CHECK(r.Held() == doctest::Approx(125.f));
    r.groove->GetInlet(0)->SetFloat(0.75f, YSE::T_GUI); // stored, not heard
    CHECK(r.Held() == doctest::Approx(125.f));
    r.p.Disconnect(sig, 0, r.handle, 0);
    CHECK(r.Held() == doctest::Approx(175.f));
  }

  TEST_CASE("~phasegroove: loop points by inlet, by signal, by word and by argument (#1142)") {
    auto b = Ramp("loop_1142", 1, 1000);
    PhaseRig r("loop_1142 1 0 50 100 200", "0.5");
    CHECK(r.Held() == doctest::Approx(150.f));
    CHECK(r.groove->Player().loopStart() == 100.f);
    CHECK(r.groove->Player().loopEnd() == 200.f);

    r.groove->GetInlet(1)->SetFloat(300.f, YSE::T_GUI);
    r.groove->GetInlet(2)->SetFloat(500.f, YSE::T_GUI);
    CHECK(r.Held() == doctest::Approx(400.f));
    r.groove->GetInlet(1)->SetInt(0, YSE::T_GUI);
    r.groove->GetInlet(2)->SetInt(100, YSE::T_GUI);
    CHECK(r.Held() == doctest::Approx(50.f));

    r.Message("setloop 600 700");
    CHECK(r.Held() == doctest::Approx(650.f));
    r.Message("setloop 1"); // needs both: nothing happens
    CHECK(r.Held() == doctest::Approx(650.f));
    r.Message("loopstart 500");
    CHECK(r.Held() == doctest::Approx(600.f));
    r.Message("loopend 900");
    CHECK(r.Held() == doctest::Approx(700.f));
    r.Message("loopend"); // needs its number
    CHECK(r.Held() == doctest::Approx(700.f));
    r.Message("reset");
    CHECK(r.Held() == doctest::Approx(500.f));

    // A loop that holds nothing: silence, sync 0.
    r.Message("setloop 300 300");
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Silent());
    CHECK(r.Sync(0) == 0.f);
    r.Message("setloop 100 200");

    // A connected signal on a loop point wins over its number; once per
    // block.
    YSE::pHandle* start = r.p.CreateObject(YSE::OBJ::D_SIG, "140");
    REQUIRE(start != nullptr);
    r.p.Connect(start, 0, r.handle, 1);
    r.groove->GetInlet(1)->SetFloat(0.f, YSE::T_GUI); // stored, not heard
    CHECK(r.Held() == doctest::Approx(170.f));
    CHECK(r.groove->Player().loopStart() == 140.f);
    YSE::pHandle* end = r.p.CreateObject(YSE::OBJ::D_SIG, "160");
    REQUIRE(end != nullptr);
    r.p.Connect(end, 0, r.handle, 2);
    CHECK(r.Held() == doctest::Approx(150.f));
    CHECK(r.groove->Player().loopEnd() == 160.f);
  }

  TEST_CASE("~phasegroove: loopinterp and interptime by word and by argument (#1142)") {
    auto b = Ramp("fade_1142", 1, 1000);
    // Frame 196, rising, 0.6 into a fade of 10: 196 + 0.6 (96 - 196) = 136.
    {
      PhaseRig r("fade_1142 1 0 10 100 200", "0.5");
      r.p.Calculate(YSE::T_DSP); // rising from here
      r.Phase(0.96f);
      CHECK(r.Held() == doctest::Approx(196.f));
      r.Message("loopinterp 1");
      CHECK(r.Held() == doctest::Approx(136.f));
      CHECK(r.groove->Player().loopInterp());
      r.Message("interptime 20"); // 0.8 into it: 196 + 0.8 (96 - 196) = 116
      CHECK(r.Held() == doctest::Approx(116.f));
      CHECK(r.groove->Player().interpTime() == 20.f);
      r.Message("interptime -3"); // no fade
      CHECK(r.Held() == doctest::Approx(196.f));
      r.Message("loopinterp 0");
      r.Message("interptime 20");
      CHECK(r.Held() == doctest::Approx(196.f));
    }
    {
      PhaseRig r("fade_1142 1 1 10 100 200", "0.5");
      r.p.Calculate(YSE::T_DSP);
      r.Phase(0.96f);
      CHECK(r.Held() == doctest::Approx(136.f));
    }
  }

  TEST_CASE("~phasegroove: several channels, one outlet each; sync is the rightmost (#1142)") {
    auto b = Ramp("chans_1142", 3, 1000);
    ScopedSampleRate rate(1000);
    patcherImplementation p(2, nullptr);
    YSE::pHandle* sig = p.CreateObject(YSE::OBJ::D_SIG, "0.5");
    YSE::pHandle* groove = p.CreateObject(YSE::OBJ::D_PHASEGROOVE, "chans_1142 2 0 50 100 200");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(sig != nullptr);
    REQUIRE(groove != nullptr);
    REQUIRE(groove->GetOutputs() == 3);
    p.Connect(sig, 0, groove, 0);
    p.Connect(groove, 0, dac, 0);
    p.Connect(groove, 1, dac, 1);
    p.Calculate(YSE::T_DSP);
    // Channels 0 and 2 go to the first outlet, channel 1 to the second.
    CHECK(p.output[0].getPtr()[0] == doctest::Approx(150.f + 2150.f));
    CHECK(p.output[1].getPtr()[0] == doctest::Approx(1150.f));
    p.Disconnect(groove, 1, dac, 1);
    p.Connect(groove, 2, dac, 1);
    p.Calculate(YSE::T_DSP);
    CHECK(p.output[1].getPtr()[0] == doctest::Approx(0.5f));
  }

  TEST_CASE("~phasegroove: no name, or a buffer sized later (#1142)") {
    {
      PhaseRig r("", "0.5");
      r.p.Calculate(YSE::T_DSP);
      CHECK(r.Silent());
      CHECK(r.Sync(0) == 0.f);
    }
    std::shared_ptr<sampleBuffer> later = sampleBufferRegistry::acquire("later_1142");
    PhaseRig r("later_1142", "0.5");
    r.p.Calculate(YSE::T_DSP); // acquired, but empty: silence
    CHECK(r.Silent());
    auto b = Ramp("later_1142", 1, 256);
    CHECK(r.Held() == doctest::Approx(128.f));
  }

  TEST_CASE("~phasegroove: set plays another buffer from a later block (#1142)") {
    auto a = Ramp("set_1142_a", 1, 1000);
    auto b = Ramp("set_1142_b", 1, 500);
    PhaseRig r("set_1142_a", "0.5");
    CHECK(r.Held() == doctest::Approx(500.f));

    r.Message("set set_1142_b");
    r.Settle();
    CHECK(r.groove->Buffer() == b);
    CHECK_FALSE(r.groove->ReleasePending());
    CHECK(r.Held() == doctest::Approx(250.f)); // its own loop: the whole of it

    // set with no name: silence.
    r.Message("set");
    r.Settle();
    CHECK(r.groove->Buffer() == nullptr);
    r.p.Calculate(YSE::T_DSP);
    CHECK(r.Silent());
    CHECK(r.p.FileIO()->Dropped() == 0);

    // Outside a patcher a set does nothing.
    dPhaseGroove standalone;
    standalone.SetParams("set_1142_a");
    standalone.GetInlet(0)->SetMessage("set set_1142_b", YSE::T_GUI);
    CHECK(standalone.Buffer() == a);
  }

  TEST_CASE("~phasegroove: arguments survive a DumpJSON / ParseJSON round trip; messages are not "
            "saved (#1142)") {
    ScopedSampleRate rate(1000);
    auto a = Ramp("json_1142_a", 2, 1000);
    auto b = Ramp("json_1142_b", 2, 1000);
    std::string dump;
    {
      PhaseRig r("json_1142_a 2 1 4 100 110");
      dump = r.p.DumpJSON();
      r.Message("set json_1142_b");
      r.Message("loopinterp 0");
      r.Message("interptime 9");
      r.Message("setloop 5 6");
      r.groove->GetInlet(1)->SetFloat(7.f, YSE::T_GUI);
      r.Settle();
      CHECK(r.groove->Buffer() == b);
      CHECK(r.p.DumpJSON() == dump);
    }
    CHECK(dump.find("json_1142_a 2 1 4 100 110") != std::string::npos);
    CHECK(dump.find("json_1142_b") == std::string::npos);
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    dPhaseGroove* object = nullptr;
    for (unsigned int i = 0; i < dst.Objects(); ++i) {
      YSE::pHandle* h = dst.GetHandleFromList(i);
      if (h != nullptr && std::string(h->Type()) == YSE::OBJ::D_PHASEGROOVE)
        object = static_cast<dPhaseGroove*>(dst.GetObjectFromID(h->GetID()));
    }
    REQUIRE(object != nullptr);
    CHECK(object->Channels() == 2);
    CHECK(object->Buffer() == a);
    dst.Calculate(YSE::T_DSP);
    CHECK(object->Player().loopInterp());
    CHECK(object->Player().interpTime() == 4.f);
    CHECK(object->Player().loopStart() == 100.f);
    CHECK(object->Player().loopEnd() == 110.f);
  }

  TEST_CASE("~phasegroove: blocks, numbers, words and set on the audio thread allocate nothing "
            "(#1142)") {
    auto a = Ramp("rt_1142_a", 2, 1000);
    auto b = Ramp("rt_1142_b", 2, 300);
    PhaseRig r("rt_1142_a 2", "0.5");
    r.Settle();
    // Built before the probe: the strings themselves allocate.
    const std::string toB = "set rt_1142_b";
    const std::string toA = "set rt_1142_a";
    const std::vector<std::string> words = {"loopinterp 1", "interptime 5", "setloop 10 200",
                                            "loopstart 20", "loopend 250",  "reset"};
    for (const std::string* m : {&toB, &toA, &toB}) {
      CAPTURE(*m);
      {
        // Lock-free too: the request is a CAS into the scheduler's table.
        TestHelpers::ProbeScope probe;
        r.Message(*m, YSE::T_DSP);
        for (const std::string& w : words)
          r.Message(w, YSE::T_DSP);
        r.groove->GetInlet(0)->SetFloat(0.25f, YSE::T_DSP);
        r.groove->GetInlet(0)->SetInt(1, YSE::T_DSP);
        r.groove->GetInlet(1)->SetFloat(5.f, YSE::T_DSP);
        r.groove->GetInlet(2)->SetInt(90, YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      r.p.FileIO()->WaitIdle();
      {
        // The blocks that take the new reader and play.
        TestHelpers::ProbeScope probe;
        for (int block = 0; block < 5; ++block)
          r.p.Calculate(YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      r.p.FileIO()->WaitIdle();
      CHECK_FALSE(r.groove->ReleasePending());
      r.p.Calculate(YSE::T_DSP); // hands the slots back
    }
    CHECK(r.groove->Buffer() == b);
    CHECK(r.p.FileIO()->Dropped() == 0);
  }

  TEST_CASE("~phasegroove plays while other threads edit and set its buffers (#1142)") {
    // A ~phasor reads two buffers of different lengths, switched from a
    // control thread, while an editor resizes one and fills it with 3 and the
    // other with 2, and the loop points move. Every sample heard comes from
    // what the buffers held: 0 (a resize's new frames), 1, 2 or 3, through
    // the cubic kernel (which overshoots a step a little) and the crossfade.
    // Under ASan or TSan a freed storage or a broken handoff shows up here.
    auto ones = sampleBufferRegistry::acquire("race_1142_1");
    ones->setSize(N * 2);
    ones->setSampleRate(RATE);
    ones->fill(1.f);
    auto twos = sampleBufferRegistry::acquire("race_1142_2");
    twos->setSize(N * 5);
    twos->setSampleRate(RATE);
    twos->fill(2.f);
    ScopedSampleRate rate(1000);
    patcherImplementation p(2, nullptr);
    YSE::pHandle* phasor = p.CreateObject(YSE::OBJ::D_PHASOR, "7");
    YSE::pHandle* handle = p.CreateObject(YSE::OBJ::D_PHASEGROOVE, "race_1142_1 1 1 10");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(phasor != nullptr);
    REQUIRE(handle != nullptr);
    REQUIRE(dac != nullptr);
    p.Connect(phasor, 0, handle, 0);
    p.Connect(handle, 0, dac, 0);
    p.Connect(handle, 1, dac, 1);
    auto* groove = static_cast<dPhaseGroove*>(p.GetObjectFromID(handle->GetID()));
    const std::string toOnes = "set race_1142_1";
    const std::string toTwos = "set race_1142_2";

    std::atomic<bool> stop{false};
    std::thread setter([&] {
      int n = 0;
      while (!stop.load()) {
        groove->GetInlet(0)->SetMessage((n % 2) != 0 ? toOnes : toTwos, YSE::T_GUI);
        groove->GetInlet(1)->SetFloat(static_cast<float>(n % 50), YSE::T_GUI);
        groove->GetInlet(2)->SetFloat(static_cast<float>(100 + n % 300), YSE::T_GUI);
        n++;
        std::this_thread::yield();
      }
    });
    std::thread editor([&] {
      int n = 0;
      while (!stop.load()) {
        ones->setSize(N * 2 + static_cast<UInt>(n % 7));
        if (n % 3 == 0) ones->fill(3.f);
        if (n % 3 == 1) ones->fill(1.f);
        n++;
        std::this_thread::yield();
      }
    });

    int bad = 0;
    int switches = 0;
    const sampleBuffer* last = groove->Buffer().get();
    for (int block = 0; block < 40000 && (block < 5000 || switches == 0); ++block) {
      p.Calculate(YSE::T_DSP);
      for (UInt i = 0; i < N; ++i) {
        // A step from 3 to 0 overshoots by under 0.2 either way.
        const float v = p.output[0].getPtr()[i];
        if (!(v >= -0.5f && v <= 3.5f)) bad++;
        const float s = p.output[1].getPtr()[i];
        if (!(s >= 0.f && s <= 1.f)) bad++;
      }
      const sampleBuffer* now = groove->Buffer().get();
      if (now != last) switches++;
      last = now;
    }
    stop.store(true);
    setter.join();
    editor.join();
    CHECK(bad == 0);
    CHECK(switches > 0);
  }

} // TEST_SUITE
