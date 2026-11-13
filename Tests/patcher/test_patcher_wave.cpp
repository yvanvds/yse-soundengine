// Tests for the ~wave and ~2d.wave patcher objects (patcher/sampling/dWave),
// issue #1146. The sample math is DSP::waveReader's
// (Tests/dsp/test_wave_reader.cpp) and the reader is
// DSP::sampleBufferReader's (test_sampleBuffer.cpp). These show that pins,
// messages and arguments reach them, in rendered patches heard through a
// ~dac: a ~phasor scanning a cosine table into a wavetable oscillator, the
// number fallback, the region by argument, number, list, signal and set,
// channels, interpolation, rows and the row crossfade, set (#1256), round
// trips, allocation-free blocks and messages on the audio thread, and edits
// made while it renders.

#include <doctest/doctest.h>
#include <algorithm>
#include <atomic>
#include <cmath>
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
#include "patcher/sampling/dWave.h"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using YSE::STANDARD_BUFFERSIZE;
using YSE::DSP::sampleBuffer;
using YSE::DSP::sampleBufferRegistry;
using YSE::PATCHER::dWave;
using YSE::PATCHER::dWave2d;
using YSE::PATCHER::patcherImplementation;

namespace {

  constexpr double TWO_PI = 6.283185307179586;

  // A named buffer at 1000 Hz (one frame a millisecond) whose frame n of
  // channel c is (c + 1) * 10 * n.
  std::shared_ptr<sampleBuffer> Tens(const std::string& name, UInt frames, UInt channels = 1) {
    std::shared_ptr<sampleBuffer> b = sampleBufferRegistry::acquire(name);
    b->setSize(frames, static_cast<Int>(channels));
    b->setSampleRate(1000.f);
    for (UInt c = 0; c < channels; ++c) {
      std::vector<float> values(frames);
      for (UInt n = 0; n < frames; ++n)
        values[n] = static_cast<float>((c + 1) * 10 * n);
      b->setSamples(c, 0, values.data(), frames);
    }
    return b;
  }

  float Out(patcherImplementation& p, UInt i, UInt channel = 0) {
    return p.output[channel].getPtr()[i];
  }

  // [source] -> [type args] -> [~dac], `source` made from `sourceType` and
  // `sourceArgs`, or no source at all when `sourceType` is empty.
  template <class Object> struct WaveRig {
    patcherImplementation p{2, nullptr};
    YSE::pHandle* source = nullptr;
    YSE::pHandle* handle = nullptr;
    YSE::pHandle* dac = nullptr;
    Object* wave = nullptr;

    WaveRig(const char* type, const std::string& args, const char* sourceType,
            const std::string& sourceArgs) {
      handle = p.CreateObject(type, args);
      REQUIRE(handle != nullptr);
      dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(dac != nullptr);
      if (sourceType != nullptr && *sourceType != '\0') {
        source = p.CreateObject(sourceType, sourceArgs);
        REQUIRE(source != nullptr);
        p.Connect(source, 0, handle, 0);
      }
      p.Connect(handle, 0, dac, 0);
      if (handle->GetOutputs() > 1) p.Connect(handle, 1, dac, 1);
      wave = static_cast<Object*>(p.GetObjectFromID(handle->GetID()));
    }

    void Message(const std::string& text, YSE::THREAD thread = YSE::T_GUI) {
      wave->GetInlet(0)->SetMessage(text, thread);
    }

    // What the next block reads, at its first sample.
    float Block(UInt channel = 0) {
      p.Calculate(YSE::T_DSP);
      return Out(p, 0, channel);
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

  using Wave = WaveRig<dWave>;
  using Wave2d = WaveRig<dWave2d>;

} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~wave / ~2d.wave: registry and shape (#1146)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_WAVE, "shape_1146");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~wave");
    CHECK(h->GetInputs() == 3);
    REQUIRE(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    YSE::pHandle* stereo = p.CreateObject(YSE::OBJ::D_WAVE, "shape_1146 0 0 2");
    REQUIRE(stereo != nullptr);
    CHECK(stereo->GetOutputs() == 2);

    YSE::pHandle* h2 = p.CreateObject(YSE::OBJ::D_WAVE2D, "shape_1146 0 0 3 8");
    REQUIRE(h2 != nullptr);
    CHECK(std::string(h2->Type()) == "~2d.wave");
    CHECK(h2->GetInputs() == 4);
    CHECK(h2->GetOutputs() == 3);

    bool wave = false;
    bool wave2d = false;
    for (const auto& name : YSE::PATCHER::Register().AllNames()) {
      if (name == YSE::OBJ::D_WAVE) wave = true;
      if (name == YSE::OBJ::D_WAVE2D) wave2d = true;
    }
    CHECK(wave);
    CHECK(wave2d);

    dWave standalone;
    CHECK(standalone.GetCategory() == YSE::PATCHER::pCategory::SAMPLING);
    CHECK(standalone.Channels() == 1);
    CHECK(standalone.CurrentStart() == 0.f);
    CHECK(standalone.CurrentEnd() == 0.f);
    CHECK(standalone.CurrentInterp() == 1);
    CHECK(standalone.Buffer() == nullptr);
    dWave2d standalone2d;
    CHECK(standalone2d.GetCategory() == YSE::PATCHER::pCategory::SAMPLING);
    CHECK(standalone2d.CurrentRows() == 1);
  }

  TEST_CASE("~wave: a ~phasor scanning a cosine table is a cosine oscillator (#1146)") {
    // The patch a user builds: [~phasor 441] -> [~wave cosine 0 0 1 5] ->
    // [~dac], the ~phasor heard on a second channel. The spline reads a
    // 256-point table within a few millionths of cos(2 pi phase).
    constexpr UInt POINTS = 256;
    auto table = sampleBufferRegistry::acquire("cos_1146");
    table->setSize(POINTS, 1);
    std::vector<float> values(POINTS);
    for (UInt n = 0; n < POINTS; ++n)
      values[n] = static_cast<float>(std::cos(TWO_PI * n / POINTS));
    table->setSamples(0, 0, values.data(), POINTS);

    patcherImplementation p(2, nullptr);
    YSE::pHandle* phasor = p.CreateObject(YSE::OBJ::D_PHASOR, "441");
    YSE::pHandle* wave = p.CreateObject(YSE::OBJ::D_WAVE, "cos_1146 0 0 1 5");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(phasor != nullptr);
    REQUIRE(wave != nullptr);
    REQUIRE(dac != nullptr);
    p.Connect(phasor, 0, wave, 0);
    p.Connect(wave, 0, dac, 0);
    p.Connect(phasor, 0, dac, 1);

    const double h = TWO_PI / POINTS;
    const double bound = h * h * h / 8.0 + 1e-5;
    double lowest = 1.0;
    double highest = 0.0;
    for (int block = 0; block < 4; ++block) {
      p.Calculate(YSE::T_DSP);
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
        CAPTURE(i);
        const double phase = Out(p, i, 1);
        lowest = std::min(lowest, phase);
        highest = std::max(highest, phase);
        CHECK(std::fabs(static_cast<double>(Out(p, i, 0)) - std::cos(TWO_PI * phase)) <= bound);
      }
    }
    CHECK(lowest < 0.05); // the ~phasor really swept the cycle
    CHECK(highest > 0.95);
  }

  TEST_CASE("~wave: a number while no signal is connected, a connected signal wins (#1146)") {
    auto b = Tens("num_1146", 8);
    Wave r(YSE::OBJ::D_WAVE, "num_1146", "", "");
    CHECK(r.Block() == 0.f); // phase 0
    r.wave->GetInlet(0)->SetFloat(0.25f, YSE::T_GUI);
    CHECK(r.Block() == 20.f);
    CHECK(Out(r.p, STANDARD_BUFFERSIZE - 1) == 20.f);
    r.wave->GetInlet(0)->SetInt(1, YSE::T_GUI); // wraps to 0
    CHECK(r.Block() == 0.f);
    r.wave->GetInlet(0)->SetFloat(1.5f, YSE::T_GUI);
    CHECK(r.Block() == 40.f);

    Wave s(YSE::OBJ::D_WAVE, "num_1146", YSE::OBJ::D_SIG, "0.75");
    s.wave->GetInlet(0)->SetFloat(0.f, YSE::T_GUI); // ignored while the cord is in
    CHECK(s.Block() == 60.f);
    s.source->SetFloatData(0, 0.125f);
    CHECK(s.Block() == 10.f);
    s.p.Disconnect(s.source, 0, s.handle, 0);
    CHECK(s.Block() == 0.f);
  }

  TEST_CASE("~wave: the region by argument, number, list, signal and set (#1146)") {
    auto b = Tens("region_1146", 20);
    // 4 to 8 ms: frames 4 .. 7.
    Wave r(YSE::OBJ::D_WAVE, "region_1146 4 8", YSE::OBJ::D_SIG, "0");
    CHECK(r.wave->CurrentStart() == 4.f);
    CHECK(r.wave->CurrentEnd() == 8.f);
    CHECK(r.Block() == 40.f);
    r.source->SetFloatData(0, 0.5f);
    CHECK(r.Block() == 60.f);

    // Numbers in the middle and right inlets.
    r.wave->GetInlet(1)->SetInt(10, YSE::T_GUI);
    r.wave->GetInlet(2)->SetFloat(14.f, YSE::T_GUI);
    CHECK(r.Block() == 120.f);
    // A list in the left inlet: start and end; one number is the start.
    r.wave->GetInlet(0)->SetList("2 6", YSE::T_GUI);
    CHECK(r.wave->CurrentStart() == 2.f);
    CHECK(r.wave->CurrentEnd() == 6.f);
    CHECK(r.Block() == 40.f);
    r.wave->GetInlet(0)->SetList("12", YSE::T_GUI);
    CHECK(r.wave->CurrentEnd() == 6.f);
    CHECK(r.Block() == 0.f); // the end before the start: silence
    // An end of 0 is the end of the buffer.
    r.wave->GetInlet(2)->SetInt(0, YSE::T_GUI);
    CHECK(r.Block() == 160.f); // frames 12 .. 19

    // Signals in the region inlets, from each block's first sample, win.
    YSE::pHandle* start = r.p.CreateObject(YSE::OBJ::D_SIG, "0");
    YSE::pHandle* end = r.p.CreateObject(YSE::OBJ::D_SIG, "10");
    r.p.Connect(start, 0, r.handle, 1);
    r.p.Connect(end, 0, r.handle, 2);
    CHECK(r.Block() == 50.f);
    start->SetFloatData(0, 6.f);
    end->SetFloatData(0, 8.f);
    CHECK(r.Block() == 70.f);
    CHECK(r.wave->CurrentStart() == 12.f); // the signal is not stored
    r.p.Disconnect(start, 0, r.handle, 1);
    r.p.Disconnect(end, 0, r.handle, 2);
    CHECK(r.Block() == 160.f);
  }

  TEST_CASE("~wave: channels, one outlet each (#1146)") {
    auto b = Tens("chan_1146", 8, 3);
    Wave r(YSE::OBJ::D_WAVE, "chan_1146 0 0 2", YSE::OBJ::D_SIG, "0.25");
    CHECK(r.wave->Channels() == 2);
    r.p.Calculate(YSE::T_DSP);
    CHECK(Out(r.p, 0, 0) == 20.f);
    CHECK(Out(r.p, 0, 1) == 40.f);
    // Channels past the buffer's are silent; the count is kept in 1 .. 64.
    Wave four(YSE::OBJ::D_WAVE, "chan_1146 0 0 4", YSE::OBJ::D_SIG, "0.25");
    CHECK(four.handle->GetOutputs() == 4);
    four.p.Connect(four.handle, 3, four.dac, 1);
    four.p.Disconnect(four.handle, 1, four.dac, 1);
    four.p.Calculate(YSE::T_DSP);
    CHECK(Out(four.p, 0, 1) == 0.f);
    dWave none;
    none.SetParams("chan_1146 0 0 0");
    CHECK(none.Channels() == 1);
    dWave many;
    many.SetParams("chan_1146 0 0 1000");
    CHECK(many.Channels() == 64);
  }

  TEST_CASE("~wave: interp by argument and message (#1146)") {
    // 0 1 4 9 ...: frame 1.5 reads 2.5 linearly, 1 without interpolation,
    // 2.25 through the spline (a parabola exactly).
    auto b = sampleBufferRegistry::acquire("interp_1146");
    b->setSize(8, 1);
    b->setSampleRate(1000.f);
    std::vector<float> squares{0.f, 1.f, 4.f, 9.f, 16.f, 25.f, 36.f, 49.f};
    b->setSamples(0, 0, squares.data(), 8);
    Wave r(YSE::OBJ::D_WAVE, "interp_1146", YSE::OBJ::D_SIG, "0.1875");
    CHECK(r.Block() == doctest::Approx(2.5f));
    r.Message("interp 0");
    CHECK(r.wave->CurrentInterp() == 0);
    CHECK(r.Block() == 1.f);
    r.Message("interp 5");
    CHECK(r.Block() == doctest::Approx(2.25f));
    // Hermite at tension 1: smoothstep, the midpoint of 1 and 4.
    r.Message("interp 6");
    r.Message("interp_tension 1");
    CHECK(r.wave->CurrentTension() == 1.f);
    CHECK(r.Block() == doctest::Approx(2.5f));
    r.Message("interp_tension 0");
    r.Message("interp_bias 1");
    CHECK(r.wave->CurrentBias() == 1.f);
    // m0 = 1, m1 = 3 at f = 0.5: 0.5 + 0.125 * 1 - 0.125 * 3 + 0.5 * 4.
    CHECK(r.Block() == doctest::Approx(0.5f + 0.125f - 0.375f + 2.f));
    // Kept in 0 .. 6; not a number is ignored.
    r.Message("interp 99");
    CHECK(r.wave->CurrentInterp() == 6);
    r.Message("interp -3");
    CHECK(r.wave->CurrentInterp() == 0);
    r.Message("interp none");
    CHECK(r.wave->CurrentInterp() == 0);

    Wave a(YSE::OBJ::D_WAVE, "interp_1146 0 0 1 6 0 1", YSE::OBJ::D_SIG, "0.1875");
    CHECK(a.wave->CurrentInterp() == 6);
    CHECK(a.wave->CurrentTension() == 1.f);
    CHECK(a.Block() == doctest::Approx(2.5f));
  }

  TEST_CASE("~2d.wave: the row phase crossfades between rows, by number, signal and rows "
            "(#1146)") {
    auto b = Tens("rows_1146", 8); // rows of 4: 0 10 20 30 | 40 50 60 70
    Wave2d r(YSE::OBJ::D_WAVE2D, "rows_1146 0 0 1 2", YSE::OBJ::D_SIG, "0.25");
    CHECK(r.wave->CurrentRows() == 2);
    CHECK(r.Block() == 10.f); // row phase 0: the first row
    r.wave->GetInlet(1)->SetFloat(1.f, YSE::T_GUI);
    CHECK(r.Block() == 50.f);
    r.wave->GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
    CHECK(r.Block() == 30.f);
    r.wave->GetInlet(1)->SetInt(3, YSE::T_GUI); // above 1: the last row
    CHECK(r.Block() == 50.f);

    // rows by message; 0 is one row.
    r.Message("rows 4"); // rows of 2: frame 2 * row + 2p
    r.wave->GetInlet(1)->SetFloat(1.f, YSE::T_GUI);
    CHECK(r.Block() == 65.f); // frame 6.5
    r.Message("rows 0");
    CHECK(r.wave->CurrentRows() == 0);
    CHECK(r.Block() == 20.f);
    r.Message("rows -2");
    CHECK(r.wave->CurrentRows() == 0);
    r.Message("rows 2");

    // A row signal, sample by sample, wins over the number.
    YSE::pHandle* row = r.p.CreateObject(YSE::OBJ::D_SIG, "0");
    r.p.Connect(row, 0, r.handle, 1);
    CHECK(r.Block() == 10.f);
    row->SetFloatData(0, 0.25f);
    CHECK(r.Block() == 20.f);

    // The region inlets are the third and fourth: 4 to 8 ms is one row of
    // two frames each.
    r.wave->GetInlet(2)->SetInt(4, YSE::T_GUI);
    r.wave->GetInlet(3)->SetInt(8, YSE::T_GUI);
    row->SetFloatData(0, 1.f);
    CHECK(r.Block() == 65.f);
    r.wave->GetInlet(0)->SetList("0 4", YSE::T_GUI);
    CHECK(r.wave->CurrentEnd() == 4.f);
    CHECK(r.Block() == 25.f); // rows 0 10 | 20 30, the second at 0.5
  }

  TEST_CASE("~2d.wave: a slow ramp morphs from the first cycle to the last (#1146)") {
    // Two rows, a cosine and its negation: a ~phasor scans the cycle, a
    // ~sig holds the row phase at 0.5, so the two cancel.
    constexpr UInt ROW = 128;
    auto b = sampleBufferRegistry::acquire("morph_1146");
    b->setSize(ROW * 2, 1);
    std::vector<float> values(ROW * 2);
    for (UInt n = 0; n < ROW; ++n) {
      values[n] = static_cast<float>(std::cos(TWO_PI * n / ROW));
      values[ROW + n] = -values[n];
    }
    b->setSamples(0, 0, values.data(), ROW * 2);
    patcherImplementation p(2, nullptr);
    YSE::pHandle* phasor = p.CreateObject(YSE::OBJ::D_PHASOR, "300");
    YSE::pHandle* row = p.CreateObject(YSE::OBJ::D_SIG, "0");
    YSE::pHandle* wave = p.CreateObject(YSE::OBJ::D_WAVE2D, "morph_1146 0 0 1 2");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    REQUIRE(wave != nullptr);
    p.Connect(phasor, 0, wave, 0);
    p.Connect(row, 0, wave, 1);
    p.Connect(wave, 0, dac, 0);
    p.Connect(phasor, 0, dac, 1);
    const double h = TWO_PI / ROW;
    for (float r : {0.f, 0.5f, 1.f, 0.25f}) {
      CAPTURE(r);
      row->SetFloatData(0, r);
      p.Calculate(YSE::T_DSP);
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
        const double expected = (1.0 - 2.0 * r) * std::cos(TWO_PI * Out(p, i, 1));
        CHECK(std::fabs(Out(p, i, 0) - expected) <= h * h / 8.0 + 1e-5);
      }
    }
  }

  TEST_CASE("~wave / ~2d.wave: set reads another buffer from a later block, with its region "
            "(#1146)") {
    auto a = Tens("set_1146_a", 8);
    auto b = Tens("set_1146_b", 16);
    b->applyOffset(1000.f);
    Wave r(YSE::OBJ::D_WAVE, "set_1146_a 2 6", YSE::OBJ::D_SIG, "0");
    CHECK(r.Block() == 20.f);

    // set alone: the whole buffer.
    r.Message("set set_1146_b");
    CHECK(r.wave->CurrentStart() == 0.f);
    CHECK(r.wave->CurrentEnd() == 0.f);
    r.Settle();
    CHECK(r.wave->Buffer() == b);
    CHECK_FALSE(r.wave->ReleasePending());
    CHECK(r.Block() == 1000.f);
    // set with a region.
    r.Message("set set_1146_a 4 8");
    r.Settle();
    CHECK(r.wave->Buffer() == a);
    CHECK(r.wave->CurrentStart() == 4.f);
    CHECK(r.wave->CurrentEnd() == 8.f);
    CHECK(r.Block() == 40.f);
    // set with no name: unbound, silence.
    r.Message("set");
    r.Settle();
    CHECK(r.wave->Buffer() == nullptr);
    CHECK(r.Block() == 0.f);
    CHECK(r.p.FileIO()->Dropped() == 0);

    Wave2d r2(YSE::OBJ::D_WAVE2D, "set_1146_a", YSE::OBJ::D_SIG, "0");
    r2.Message("set set_1146_b 3");
    r2.Settle();
    CHECK(r2.wave->Buffer() == b);
    CHECK(r2.wave->CurrentStart() == 3.f);
    CHECK(r2.Block() == 1030.f);

    // Outside a patcher a set does nothing.
    dWave standalone;
    standalone.SetParams("set_1146_a");
    standalone.GetInlet(0)->SetMessage("set set_1146_b", YSE::T_GUI);
    CHECK(standalone.Buffer() == a);
  }

  TEST_CASE("~wave / ~2d.wave: arguments survive a DumpJSON / ParseJSON round trip; messages "
            "are not saved (#1146)") {
    auto a = Tens("json_1146_a", 20, 2);
    auto b = Tens("json_1146_b", 20, 2);
    std::string dump;
    {
      patcherImplementation p(2, nullptr);
      YSE::pHandle* sig = p.CreateObject(YSE::OBJ::D_SIG, "0.5");
      YSE::pHandle* w = p.CreateObject(YSE::OBJ::D_WAVE, "json_1146_a 2 6 2 0 0.5 0.25");
      YSE::pHandle* w2 = p.CreateObject(YSE::OBJ::D_WAVE2D, "json_1146_a 0 8 1 2");
      YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
      p.Connect(sig, 0, w, 0);
      p.Connect(sig, 0, w2, 0);
      p.Connect(w, 1, dac, 0);
      p.Connect(w2, 0, dac, 1);
      dump = p.DumpJSON();
      CHECK(dump.find("json_1146_a 2 6 2 0 0.5 0.25") != std::string::npos);
      CHECK(dump.find("json_1146_a 0 8 1 2") != std::string::npos);
      auto* wave = static_cast<dWave*>(p.GetObjectFromID(w->GetID()));
      auto* wave2 = static_cast<dWave2d*>(p.GetObjectFromID(w2->GetID()));
      CHECK(wave->CurrentBias() == 0.5f);
      CHECK(wave->CurrentTension() == 0.25f);
      wave->GetInlet(0)->SetMessage("set json_1146_b 1 2", YSE::T_GUI);
      wave->GetInlet(0)->SetMessage("interp 4", YSE::T_GUI);
      wave->GetInlet(1)->SetInt(3, YSE::T_GUI);
      wave->GetInlet(0)->SetList("5 9", YSE::T_GUI);
      wave2->GetInlet(0)->SetMessage("rows 3", YSE::T_GUI);
      wave2->GetInlet(1)->SetFloat(0.5f, YSE::T_GUI);
      p.FileIO()->WaitIdle();
      p.Calculate(YSE::T_DSP);
      p.FileIO()->WaitIdle();
      CHECK(p.DumpJSON() == dump);
    }
    CHECK(dump.find("json_1146_b") == std::string::npos);
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);
    CHECK(dst.DumpJSON() == dump);
    // The reloaded objects read a again: ~wave frames 2 .. 5 of channel 2
    // at 0.5 without interpolation (frame 4: 80), ~2d.wave the first of two
    // rows of 4 (frame 2: 20).
    dst.Calculate(YSE::T_DSP);
    CHECK(Out(dst, 0, 0) == 80.f);
    CHECK(Out(dst, 0, 1) == 20.f);
  }

  TEST_CASE("~wave / ~2d.wave: blocks, numbers, lists and messages on the audio thread "
            "allocate nothing (#1146)") {
    auto a = Tens("rt_1146_a", 1024, 2);
    auto b = Tens("rt_1146_b", 512, 2);
    Wave r(YSE::OBJ::D_WAVE, "rt_1146_a 0 0 2", YSE::OBJ::D_PHASOR, "100");
    Wave2d r2(YSE::OBJ::D_WAVE2D, "rt_1146_a 0 0 2 4", YSE::OBJ::D_PHASOR, "100");
    r.Settle();
    r2.Settle();
    // Built before the probe: the strings themselves allocate.
    const std::string toB = "set rt_1146_b 1 400";
    const std::string toA = "set rt_1146_a";
    const std::string interp = "interp 4";
    const std::string bias = "interp_bias 0.2";
    const std::string tension = "interp_tension -0.5";
    const std::string rows = "rows 3";
    const std::string list = "10 300";
    for (const std::string* m : {&toB, &toA, &toB}) {
      CAPTURE(*m);
      {
        TestHelpers::ProbeScope probe;
        r.Message(*m, YSE::T_DSP);
        r.Message(interp, YSE::T_DSP);
        r.Message(bias, YSE::T_DSP);
        r.Message(tension, YSE::T_DSP);
        r.wave->GetInlet(0)->SetList(list, YSE::T_DSP);
        r.wave->GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
        r.wave->GetInlet(1)->SetFloat(1.f, YSE::T_DSP);
        r.wave->GetInlet(2)->SetInt(500, YSE::T_DSP);
        r2.Message(*m, YSE::T_DSP);
        r2.Message(rows, YSE::T_DSP);
        r2.wave->GetInlet(0)->SetList(list, YSE::T_DSP);
        r2.wave->GetInlet(1)->SetFloat(0.4f, YSE::T_DSP);
        r2.wave->GetInlet(2)->SetInt(2, YSE::T_DSP);
        r2.wave->GetInlet(3)->SetFloat(450.f, YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      r.p.FileIO()->WaitIdle();
      r2.p.FileIO()->WaitIdle();
      {
        // The block that takes the new reader and asks for the old one to go.
        TestHelpers::ProbeScope probe;
        r.p.Calculate(YSE::T_DSP);
        r.p.Calculate(YSE::T_DSP);
        r2.p.Calculate(YSE::T_DSP);
        r2.p.Calculate(YSE::T_DSP);
        CHECK(TestHelpers::g_alloc_count.load() == 0);
      }
      r.p.FileIO()->WaitIdle();
      r2.p.FileIO()->WaitIdle();
      CHECK_FALSE(r.wave->ReleasePending());
      CHECK_FALSE(r2.wave->ReleasePending());
      r.p.Calculate(YSE::T_DSP); // hands the slots back
      r2.p.Calculate(YSE::T_DSP);
    }
    CHECK(r.wave->Buffer() == b);
    CHECK(r2.wave->Buffer() == b);
    CHECK(r.p.FileIO()->Dropped() == 0);
  }

  TEST_CASE("~wave renders while control threads edit and set its buffers (#1146)") {
    // Two buffers of different lengths filled 1 and 2, edited and switched
    // from control threads while the audio thread scans them with a
    // ~phasor through the cubic mode. Every sample heard is in reach of what
    // was written: 0 to 3, with the cubic's overshoot across a step. Under
    // ASan or TSan a freed storage or a broken handoff shows up here.
    auto ones = sampleBufferRegistry::acquire("race_1146_1");
    ones->setSize(STANDARD_BUFFERSIZE);
    ones->fill(1.f);
    auto twos = sampleBufferRegistry::acquire("race_1146_2");
    twos->setSize(STANDARD_BUFFERSIZE * 3);
    twos->fill(2.f);
    Wave r(YSE::OBJ::D_WAVE, "race_1146_1 0 0 1 4", YSE::OBJ::D_PHASOR, "300");
    const std::string toOnes = "set race_1146_1";
    const std::string toTwos = "set race_1146_2";

    std::atomic<bool> stop{false};
    std::thread setter([&] {
      int n = 0;
      while (!stop.load()) {
        r.Message((n++ % 2) != 0 ? toOnes : toTwos, YSE::T_GUI);
        std::this_thread::yield();
      }
    });
    std::thread editor([&] {
      int n = 0;
      while (!stop.load()) {
        ones->setSize(STANDARD_BUFFERSIZE + static_cast<UInt>(n % 7));
        ones->fill(n % 2 != 0 ? 1.f : 3.f);
        n++;
        std::this_thread::yield();
      }
    });

    int bad = 0;
    int switches = 0;
    const sampleBuffer* last = r.wave->Buffer().get();
    for (int block = 0; block < 20000 && (block < 2000 || switches == 0); ++block) {
      r.p.Calculate(YSE::T_DSP);
      for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
        const float v = Out(r.p, i);
        if (!(v >= -1.f && v <= 4.f)) bad++;
      }
      const sampleBuffer* now = r.wave->Buffer().get();
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
