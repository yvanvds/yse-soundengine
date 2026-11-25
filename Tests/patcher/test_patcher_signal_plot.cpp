// Tests for the ~plot patcher object (patcher/analysis/dPlot), MSP's plot~,
// issue #1171. The plot is DSP::plotData's and is tested in
// Tests/dsp/test_plot_data.cpp; these show that the inlets, numbers, lists,
// messages and arguments reach it subplot by subplot, that the GUI value is
// the newest frame in the cell shape the header documents, that a signal cord
// and a refer to a ~buffer feed a subplot, that messages from a host thread
// while the audio thread renders arrive whole and in order, and that a
// rendered patch shows a signal, lists through a receive and a buffer after a
// JSON round trip.

#include <doctest/doctest.h>
#include <atomic>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/io/fileScheduler.h"
#include "patcher/analysis/dPlot.h"
#include "dsp/analysis/plotData.hpp"
#include "dsp/operators/units.hpp"
#include "dsp/sampling/sampleBuffer.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::ScopedSampleRate;
using YSE::DSP::buffer;
using YSE::DSP::plotData;
using YSE::PATCHER::dPlot;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  std::vector<float> Cells(const std::string& value) {
    std::vector<float> all;
    std::istringstream in(value);
    std::string token;
    while (in >> token)
      all.push_back(std::stof(token));
    return all;
  }

  // The bulk read, checked against the per-cell reads of the same frame:
  // cell 0 takes the newest, which the bulk read already took, so they agree.
  std::vector<float> Cells(YSE::PATCHER::pObject& o) {
    const std::vector<float> all = Cells(o.GetGuiValue());
    REQUIRE(all.size() >= 3);
    CHECK(std::stof(o.GetGuiValueAt(0)) == all[0]);
    REQUIRE(all.size() == o.GetGuiValueCount());
    for (unsigned int i = 1; i < o.GetGuiValueCount(); ++i)
      CHECK(std::stof(o.GetGuiValueAt(i)) == all[i]);
    CHECK(o.GetGuiValueAt(o.GetGuiValueCount()).empty());
    return all;
  }

  // The cells a frame of DSP::plotData shows as.
  std::vector<float> Expected(const plotData::frame& f) {
    std::vector<float> all{static_cast<float>(f.plots), static_cast<float>(f.points),
                           static_cast<float>(f.serial)};
    for (UInt p = 0; p < f.plots; ++p) {
      const plotData::subplot& s = f.subplots[p];
      const float axes[] = {
          s.domain.start, s.domain.end, s.domain.scale == plotData::LOG ? 1.f : 0.f,
          s.range.start,  s.range.end,  s.range.scale == plotData::LOG ? 1.f : 0.f,
          s.xOrigin,      s.yOrigin,    s.filter == plotData::ATODB ? 1.f : 0.f};
      all.insert(all.end(), std::begin(axes), std::end(axes));
      all.insert(all.end(), s.values, s.values + f.points);
    }
    return all;
  }

  // The values of subplot `plot` in a GUI value of `points` points.
  std::vector<float> Values(const std::vector<float>& cells, unsigned int plot,
                            unsigned int points) {
    const std::size_t at = 3 + static_cast<std::size_t>(plot) * (9 + points) + 9;
    REQUIRE(cells.size() >= at + points);
    return std::vector<float>(cells.begin() + static_cast<std::ptrdiff_t>(at),
                              cells.begin() + static_cast<std::ptrdiff_t>(at + points));
  }

  // The nine axis cells of subplot `plot`.
  std::vector<float> Axes(const std::vector<float>& cells, unsigned int plot, unsigned int points) {
    const std::size_t at = 3 + static_cast<std::size_t>(plot) * (9 + points);
    REQUIRE(cells.size() >= at + 9);
    return std::vector<float>(cells.begin() + static_cast<std::ptrdiff_t>(at),
                              cells.begin() + static_cast<std::ptrdiff_t>(at + 9));
  }

  // A standalone ~plot. Block() runs CALC() with every inlet unconnected;
  // Signal() hands inlet 0 a buffer, the only input, which runs it.
  struct Rig {
    dPlot plot;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) plot.SetParams(args);
    }
    void Block() {
      plot.ResetDSP();
      plot.Calculate(YSE::T_DSP);
    }
    void Signal(buffer& in) {
      plot.ResetDSP();
      plot.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
    }
    void Send(unsigned int inlet, const std::string& message, YSE::THREAD thread = YSE::T_GUI) {
      plot.GetInlet(static_cast<int>(inlet))->SetMessage(message, thread);
    }
  };
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~plot: registry and shape (#1171)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::D_PLOT);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == "~plot");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 0);
    CHECK(obj->IsDSPInput(0));
    CHECK(obj->GetGuiValueCount() == 3 + 9 + 4);
    CHECK_FALSE(obj->GuiValueIsSettable());

    YSE::pHandle* three = p.CreateObject(YSE::OBJ::D_PLOT, "8 3");
    REQUIRE(three != nullptr);
    CHECK(three->GetInputs() == 3);
    for (unsigned int i = 0; i < 3; ++i)
      CHECK(three->IsDSPInput(i));
    CHECK(three->GetGuiValueCount() == 3 + 3 * (9 + 8));

    std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(YSE::OBJ::D_PLOT));
    REQUIRE(o != nullptr);
    CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ANALYSIS);
    CHECK(o->IsDSPObject());
    CHECK(o->GetParamCount() == 4);

    // The defaults, before any block: one subplot of four zeros, domain 0 to
    // 1, range -1 to 1, linear, origins 0, no filter.
    Rig rig;
    CHECK(Cells(rig.plot) == std::vector<float>{1.f, 4.f, 1.f, 0.f, 1.f, 0.f, -1.f, 1.f, 0.f, 0.f,
                                                0.f, 0.f, 0.f, 0.f, 0.f, 0.f});
  }

  TEST_CASE("~plot: numbers and lists set a subplot's values from the next block, as "
            "DSP::plotData holds them (#1171)") {
    Rig rig("6 2");
    plotData reference(2, plotData::MAX_POINTS);
    reference.points(6).publish(); // as the arguments did

    rig.plot.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    rig.plot.GetInlet(1)->SetList("1 2 3 4 5 6 7 8", YSE::T_GUI);
    // Nothing shows before the block drains the messages.
    CHECK(Values(Cells(rig.plot), 1, 6) == std::vector<float>(6, 0.f));
    rig.Block();
    const float half = 0.5f;
    const float list[] = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f};
    reference.set(0, &half, 1).set(1, list, 8).publish();
    std::vector<float> cells = Cells(rig.plot);
    CHECK(cells == Expected(reference.acquire()));
    CHECK(Values(cells, 0, 6) == std::vector<float>{0.5f, 0.f, 0.f, 0.f, 0.f, 0.f});
    CHECK(Values(cells, 1, 6) == std::vector<float>{1.f, 2.f, 3.f, 4.f, 5.f, 6.f});

    // An int sets the first value only; the rest stay. Later lists come in
    // order: the last one wins where they overlap.
    rig.plot.GetInlet(1)->SetInt(-3, YSE::T_GUI);
    rig.plot.GetInlet(0)->SetList("9 9 9", YSE::T_GUI);
    rig.plot.GetInlet(0)->SetList("8", YSE::T_GUI);
    rig.plot.GetInlet(0)->SetFloat(std::nanf(""), YSE::T_GUI); // ignored
    rig.Block();
    cells = Cells(rig.plot);
    CHECK(Values(cells, 0, 6) == std::vector<float>{8.f, 9.f, 9.f, 0.f, 0.f, 0.f});
    CHECK(Values(cells, 1, 6) == std::vector<float>{-3.f, 2.f, 3.f, 4.f, 5.f, 6.f});
    const float serial = cells[2];

    // The values past numpoints were kept: numpoints 8 shows them.
    rig.Send(0, "numpoints 8");
    rig.Block();
    cells = Cells(rig.plot);
    REQUIRE(cells.size() == 3 + 2 * (9 + 8));
    CHECK(cells[2] == serial + 1.f);
    CHECK(Values(cells, 1, 8) == std::vector<float>{-3.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f});

    // Nothing new, no new frame.
    rig.Block();
    CHECK(Cells(rig.plot)[2] == serial + 1.f);

    // Per-cell reads: cell 0 moves to the newest frame; the count and the
    // other cells follow it.
    rig.plot.GetInlet(0)->SetInt(42, YSE::T_GUI);
    rig.Block();
    CHECK(rig.plot.GetGuiValueAt(2) == std::to_string(static_cast<int>(serial) + 1));
    CHECK(rig.plot.GetGuiValueAt(0) == "2");
    CHECK(rig.plot.GetGuiValueAt(1) == "8");
    CHECK(rig.plot.GetGuiValueAt(2) == std::to_string(static_cast<int>(serial) + 2));
    CHECK(std::stof(rig.plot.GetGuiValueAt(3 + 9)) == 42.f);
    CHECK(rig.plot.GetGuiValueCount() == 3 + 2 * 17);
    CHECK(rig.plot.GetGuiValueAt(3 + 2 * 17).empty());
  }

  TEST_CASE("~plot: the define messages set each subplot's axes and filter; the drawing "
            "messages are the host's (#1171)") {
    Rig rig("4 2");
    rig.Send(1, "definedomain 20 20000 log");
    rig.Send(1, "definerange -60 0");
    rig.Send(1, "definexorigin 1000");
    rig.Send(1, "defineyorigin -6");
    rig.Send(1, "definefilter atodb");
    rig.Send(0, "definedomain 0 8 linear");
    rig.Send(0, "definerange 0 1 log");
    // Ignored: the drawing, a malformed or missing number, an unknown filter.
    rig.Send(0, "definecolor 1 0 0 1");
    rig.Send(0, "defineline curve");
    rig.Send(0, "definepoint circle");
    rig.Send(0, "definethickness 3");
    rig.Send(0, "definenumber above");
    rig.Send(0, "definexgrid 1 2 3");
    rig.Send(0, "defineylabels 0 zero");
    rig.Send(0, "definexorigin x");
    rig.Send(0, "defineyorigin");
    rig.Send(0, "definefilter loud");
    rig.Send(0, "edit");
    rig.Send(0, "dictionary d");
    rig.Send(0, "getdictionary 0");
    rig.Send(0, "mouse 1");
    rig.Send(0, "numplots 5");
    rig.plot.GetInlet(1)->SetList("1 0.5 0 -2", YSE::T_GUI);
    rig.Block();
    std::vector<float> cells = Cells(rig.plot);
    REQUIRE(cells.size() == 3 + 2 * 13);
    CHECK(Axes(cells, 0, 4) == std::vector<float>{0.f, 8.f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 0.f});
    CHECK(Axes(cells, 1, 4) ==
          std::vector<float>{20.f, 20000.f, 1.f, -60.f, 0.f, 0.f, 1000.f, -6.f, 1.f});
    const std::vector<float> db = Values(cells, 1, 4);
    CHECK(db[0] == doctest::Approx(0.f));
    CHECK(db[1] == doctest::Approx(YSE::DSP::AmpToDb(0.5f)).epsilon(1e-4));
    CHECK(db[2] == YSE::DSP::DB_FLOOR);
    CHECK(db[3] == doctest::Approx(YSE::DSP::AmpToDb(2.f)).epsilon(1e-4));
    CHECK(rig.plot.Data().value(1, 1) == 0.5f); // stored as it came
    CHECK(rig.plot.GetInlet(2) == nullptr); // numplots is not a message

    // A range with one number keeps the other end; no scale is linear.
    rig.Send(1, "definerange -20");
    rig.Send(1, "definefilter none");
    rig.Block();
    cells = Cells(rig.plot);
    CHECK(Axes(cells, 1, 4)[3] == -20.f);
    CHECK(Axes(cells, 1, 4)[4] == 0.f);
    CHECK(Axes(cells, 1, 4)[8] == 0.f);
    CHECK(Values(cells, 1, 4) == std::vector<float>{1.f, 0.5f, 0.f, -2.f});
  }

  TEST_CASE("~plot: a signal cord shows frames of the signal at audioframerate, from the "
            "live sample rate (#1171)") {
    ScopedSampleRate rate(1000);
    Rig rig("4 1 0 10"); // a frame every 100 samples
    plotData reference(1, plotData::MAX_POINTS);
    reference.frameRate(10.f);
    buffer in(N);
    for (unsigned int k = 0; k < 6; ++k) {
      CAPTURE(k);
      for (unsigned int i = 0; i < N; ++i)
        in.getPtr()[i] = static_cast<float>(k * N + i);
      rig.Signal(in);
      reference(0, in);
      reference.publish();
      CHECK(Cells(rig.plot) == Expected(reference.acquire()));
    }
    // The last whole frame started on the last multiple of 100.
    const float last = std::floor((6.f * N - 4.f) / 100.f) * 100.f;
    CHECK(Values(Cells(rig.plot), 0, 4) ==
          std::vector<float>{last, last + 1.f, last + 2.f, last + 3.f});

    // audioframesize 200 and 20 frames a second at 2000 Hz: a wait of 100
    // samples, shorter than a frame, so frames of 200 follow each other, each
    // showing its first four. The new size starts one on the next sample.
    YSE::SAMPLERATE = 2000;
    rig.Send(0, "audioframesize 200");
    rig.Send(0, "audioframerate 20");
    rig.Send(0, "audioframesize"); // ignored
    for (unsigned int k = 0; k * N < 200; ++k) { // the frame on sample 0 ends on 199
      for (unsigned int i = 0; i < N; ++i)
        in.getPtr()[i] = static_cast<float>(10000 + k * N + i);
      rig.Signal(in);
    }
    CHECK(Values(Cells(rig.plot), 0, 4) == std::vector<float>{10000.f, 10001.f, 10002.f, 10003.f});
    // Lists still write the values the next frame replaces.
    rig.plot.GetInlet(0)->SetList("7 7 7 7", YSE::T_GUI);
    rig.Block();
    CHECK(Values(Cells(rig.plot), 0, 4) == std::vector<float>(4, 7.f));
  }

  TEST_CASE("~plot: refer shows a region of a ~buffer, from a later block; refer alone stops "
            "(#1171)") {
    ScopedSampleRate rate(48000);
    patcherImplementation p(1, nullptr);
    YSE::pHandle* store = p.CreateObject(YSE::OBJ::D_BUFFER, "plot_refer_1171 10");
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_PLOT, "8 2 0 1000");
    REQUIRE(store != nullptr);
    REQUIRE(h != nullptr);
    std::shared_ptr<YSE::DSP::sampleBuffer> b =
        YSE::DSP::sampleBufferRegistry::find("plot_refer_1171");
    REQUIRE(b != nullptr);
    REQUIRE(b->frames() == 480);
    std::vector<float> ramp(480);
    for (unsigned int n = 0; n < 480; ++n)
      ramp[n] = static_cast<float>(n);
    b->setSamples(0, 0, ramp.data(), 480);

    YSE::PATCHER::fileScheduler* io = p.FileIO();
    REQUIRE(io != nullptr);
    auto settle = [&]() {
      io->WaitIdle();
      p.Calculate(YSE::T_DSP);
      io->WaitIdle();
      p.Calculate(YSE::T_DSP);
    };

    // The whole buffer on subplot 2: point i is sample 60 * i.
    h->SetListData(1, "refer plot_refer_1171");
    settle();
    std::vector<float> cells = Cells(h->GetGuiValue());
    CHECK(Values(cells, 1, 8) ==
          std::vector<float>{0.f, 60.f, 120.f, 180.f, 240.f, 300.f, 360.f, 420.f});
    CHECK(Values(cells, 0, 8) == std::vector<float>(8, 0.f));

    // Channel 1 from 2 ms to 4 ms: samples 96 to 192, 12 a point. The buffer
    // changing shows at the next frame.
    h->SetListData(1, "refer plot_refer_1171 1 2 4");
    settle();
    cells = Cells(h->GetGuiValue());
    CHECK(Values(cells, 1, 8) ==
          std::vector<float>{96.f, 108.f, 120.f, 132.f, 144.f, 156.f, 168.f, 180.f});
    b->setSamples(0, 96, std::vector<float>(1, -1.f).data(), 1);
    p.Calculate(YSE::T_DSP);
    p.Calculate(YSE::T_DSP);
    CHECK(Values(Cells(h->GetGuiValue()), 1, 8)[0] == -1.f);

    // A list is overwritten by the next frame while the buffer is referred
    // to; after 'refer' alone it stays.
    h->SetListData(1, "5 5");
    p.Calculate(YSE::T_DSP);
    p.Calculate(YSE::T_DSP);
    CHECK(Values(Cells(h->GetGuiValue()), 1, 8)[0] == -1.f);
    h->SetListData(1, "refer");
    settle();
    h->SetListData(1, "5 5");
    settle();
    cells = Cells(h->GetGuiValue());
    CHECK(Values(cells, 1, 8)[0] == 5.f);
    CHECK(Values(cells, 1, 8)[1] == 5.f);
    CHECK(Values(cells, 1, 8)[2] == 120.f);
  }

  TEST_CASE("~plot: the arguments survive a DumpJSON / ParseJSON round trip; numplots "
            "rebuilds the inlets (#1171)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_PLOT, "16 3 1024 30") != nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_PLOT, "") != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* a = dst.GetHandleFromID(0);
    YSE::pHandle* b = dst.GetHandleFromID(1);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(std::string(a->Type()) == "~plot");
    CHECK(a->GetParams() == "16 3 1024 30");
    CHECK(a->GetInputs() == 3);
    CHECK(a->GetGuiValueCount() == 3 + 3 * (9 + 16));
    CHECK(b->GetParams() == "");
    CHECK(b->GetInputs() == 1);
    CHECK(b->GetGuiValueCount() == 3 + 9 + 4);
    const auto* plot = static_cast<const dPlot*>(dst.GetObjectFromID(0));
    CHECK(plot->Data().points() == 16);
    CHECK(plot->Data().frameSize() == 1024);
    CHECK(plot->Data().frameRate() == 30.f);

    // A live change of numplots rebuilds the object with its new inlets.
    a->SetParams("16 5");
    CHECK(a->GetInputs() == 5);
    CHECK(a->GetParams() == "16 5");
    a->SetParams("");
    CHECK(a->GetInputs() == 1);
  }

  TEST_CASE("~plot: in a rendered patch a signal, lists through a receive and a ~buffer are "
            "shown, after a JSON round trip (#1171)") {
    ScopedSampleRate rate(48000);
    std::string dump;
    unsigned int plotID = 0;
    {
      patcherImplementation src(1, nullptr);
      // ~phasor 375 (a ramp of 128 samples) into subplot 1 of a ~plot of 32
      // points and 3 subplots, 1000 frames a second (one every 48 samples);
      // a receive into subplot 2; a ~buffer for subplot 3 to refer to.
      YSE::pHandle* ramp = src.CreateObject(YSE::OBJ::D_PHASOR, "375");
      YSE::pHandle* control = src.CreateObject(YSE::OBJ::G_RECEIVE, "plot1171");
      YSE::pHandle* refer = src.CreateObject(YSE::OBJ::G_RECEIVE, "plot1171refer");
      YSE::pHandle* store = src.CreateObject(YSE::OBJ::D_BUFFER, "plot_patch_1171 1");
      YSE::pHandle* plot = src.CreateObject(YSE::OBJ::D_PLOT, "32 3 0 1000");
      REQUIRE(ramp != nullptr);
      REQUIRE(control != nullptr);
      REQUIRE(refer != nullptr);
      REQUIRE(store != nullptr);
      REQUIRE(plot != nullptr);
      src.Connect(ramp, 0, plot, 0);
      src.Connect(control, 0, plot, 1);
      src.Connect(refer, 0, plot, 2);
      plotID = plot->GetID();
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* plot = dst.GetHandleFromID(plotID);
    REQUIRE(plot != nullptr);
    CHECK(plot->GetParams() == "32 3 0 1000");
    REQUIRE(plot->GetInputs() == 3);

    std::shared_ptr<YSE::DSP::sampleBuffer> b =
        YSE::DSP::sampleBufferRegistry::find("plot_patch_1171");
    REQUIRE(b != nullptr);
    REQUIRE(b->frames() == 48);
    std::vector<float> ramp(48);
    for (unsigned int n = 0; n < 48; ++n)
      ramp[n] = static_cast<float>(n) / 48.f;
    b->setSamples(0, 0, ramp.data(), 48);

    REQUIRE(dst.PassData("0.25 0.5 0.75", "plot1171", YSE::T_GUI));
    REQUIRE(dst.PassData("definerange 0 2", "plot1171", YSE::T_GUI));
    REQUIRE(dst.PassData("refer plot_patch_1171", "plot1171refer", YSE::T_GUI));
    YSE::PATCHER::fileScheduler* io = dst.FileIO();
    REQUIRE(io != nullptr);
    for (int k = 0; k < 4; ++k) {
      io->WaitIdle();
      dst.Calculate(YSE::T_DSP);
    }

    const std::vector<float> cells = Cells(plot->GetGuiValue());
    REQUIRE(cells.size() == 3 + 3 * (9 + 32));
    CHECK(cells[0] == 3.f);
    CHECK(cells[1] == 32.f);

    // Subplot 1: 32 consecutive samples of the ramp, rising 1/128 a sample
    // (but where it wraps).
    const std::vector<float> wave = Values(cells, 0, 32);
    unsigned int rising = 0;
    for (unsigned int j = 1; j < 32; ++j)
      if (std::fabs(wave[j] - wave[j - 1] - 1.f / 128.f) < 1e-4f) ++rising;
    CHECK(rising >= 30);

    // Subplot 2: the list and the range, through the receive.
    CHECK(Values(cells, 1, 32)[0] == 0.25f);
    CHECK(Values(cells, 1, 32)[2] == 0.75f);
    CHECK(Values(cells, 1, 32)[3] == 0.f);
    CHECK(Axes(cells, 1, 32)[3] == 0.f);
    CHECK(Axes(cells, 1, 32)[4] == 2.f);

    // Subplot 3: the buffer, 48 samples over 32 points.
    const std::vector<float> region = Values(cells, 2, 32);
    for (unsigned int i = 0; i < 32; ++i) {
      CAPTURE(i);
      const unsigned int sample = i * 48 / 32; // floor(i * length / points)
      CHECK(region[i] == static_cast<float>(sample) / 48.f);
    }
  }

  TEST_CASE("~plot: lists from a host thread while the audio thread renders arrive whole "
            "and in order; host pollers only see whole frames (#1171)") {
    // Every list sets all eight values to its own number, and the numbers
    // only grow: a frame with unequal values was torn, one going down was
    // reordered.
    Rig rig("8");
    std::atomic<bool> done{false};
    std::atomic<unsigned int> torn{0};
    std::atomic<unsigned int> reads{0};
    // The sender starts once the poller polls, and the poller reads once more
    // after the sender is done: it reads at least once, however the threads
    // are scheduled (#1292).
    std::atomic<bool> polling{false};
    std::thread sender([&] {
      while (!polling.load(std::memory_order_acquire))
        std::this_thread::yield();
      for (unsigned int k = 1; k <= 3000; ++k) {
        std::string list;
        for (int i = 0; i < 8; ++i)
          list += std::to_string(k) + " ";
        rig.plot.GetInlet(0)->SetList(list, YSE::T_GUI);
      }
      done.store(true, std::memory_order_release);
    });
    std::thread poller([&] {
      float last = 0.f;
      for (bool finished = false; !finished;) {
        finished = done.load(std::memory_order_acquire);
        polling.store(true, std::memory_order_release);
        const std::vector<float> cells = Cells(rig.plot.GetGuiValue());
        if (cells.size() != 3 + 9 + 8) {
          torn.fetch_add(1);
          continue;
        }
        const std::vector<float> v = Values(cells, 0, 8);
        for (float x : v)
          if (x != v[0]) torn.fetch_add(1);
        if (v[0] < last) torn.fetch_add(1);
        last = v[0];
        reads.fetch_add(1);
      }
    });
    std::thread cellwise([&] {
      while (!done.load(std::memory_order_acquire)) {
        (void)rig.plot.GetGuiValueAt(0);
        const unsigned int count = rig.plot.GetGuiValueCount();
        for (unsigned int i = 1; i < count; ++i)
          (void)rig.plot.GetGuiValueAt(i);
      }
    });
    while (!done.load(std::memory_order_acquire))
      rig.Block();
    sender.join();
    poller.join();
    cellwise.join();
    rig.Block();
    CHECK(torn.load() == 0);
    CHECK(reads.load() > 0);
    // Whatever was dropped (two senders never meet here; a full ring might),
    // the last list sent was taken or a later one.
    const std::vector<float> v = Values(Cells(rig.plot), 0, 8);
    CHECK(v[0] > 0.f);
    CHECK(v[0] <= 3000.f);
    if (rig.plot.Dropped() == 0) CHECK(v[0] == 3000.f);
  }

  TEST_CASE("~plot: a ring too full for a list drops it and counts it; the next block makes "
            "room (#1171)") {
    Rig rig("4");
    std::string list;
    for (unsigned int i = 0; i < plotData::MAX_POINTS; ++i)
      list += "1 ";
    // Two full lists fit, with room for small messages; the third does not.
    rig.plot.GetInlet(0)->SetList(list, YSE::T_GUI);
    rig.plot.GetInlet(0)->SetList(list, YSE::T_GUI);
    CHECK(rig.plot.Dropped() == 0);
    rig.plot.GetInlet(0)->SetList(list, YSE::T_GUI);
    CHECK(rig.plot.Dropped() == 1);
    rig.Block();
    rig.plot.GetInlet(0)->SetList("2 2", YSE::T_GUI);
    rig.Block();
    CHECK(rig.plot.Dropped() == 1);
    CHECK(Values(Cells(rig.plot), 0, 4) == std::vector<float>{2.f, 2.f, 1.f, 1.f});
  }

  TEST_CASE("~plot: numbers, lists, messages and blocks on the audio thread allocate nothing "
            "(#1171)") {
    ScopedSampleRate rate(1000);
    Rig rig("16 2 0 100");
    const std::string list = "1 2 3 4 5 6 7 8";
    const std::string domain = "definedomain 0 10 log";
    const std::string range = "definerange -1 1";
    const std::string xorigin = "definexorigin 2";
    const std::string yorigin = "defineyorigin 0.5";
    const std::string filter = "definefilter atodb";
    const std::string refer = "refer nothing 1 0 10";
    const std::string points = "numpoints 32";
    const std::string size = "audioframesize 64";
    const std::string framerate = "audioframerate 50";
    const std::string color = "definecolor 1 0 0 1";
    buffer in(N);
    in = 0.5f;
    rig.Signal(in);
    rig.Block();
    {
      TestHelpers::ProbeScope probe;
      dPlot& p = rig.plot;
      for (int k = 0; k < 4; ++k) {
        p.GetInlet(0)->SetInt(4, YSE::T_DSP);
        p.GetInlet(1)->SetFloat(6.f, YSE::T_DSP);
        p.GetInlet(1)->SetList(list, YSE::T_DSP);
        p.GetInlet(0)->SetMessage(domain, YSE::T_DSP);
        p.GetInlet(1)->SetMessage(range, YSE::T_DSP);
        p.GetInlet(1)->SetMessage(xorigin, YSE::T_DSP);
        p.GetInlet(1)->SetMessage(yorigin, YSE::T_DSP);
        p.GetInlet(1)->SetMessage(filter, YSE::T_DSP);
        p.GetInlet(1)->SetMessage(refer, YSE::T_DSP);
        p.GetInlet(0)->SetMessage(points, YSE::T_DSP);
        p.GetInlet(0)->SetMessage(size, YSE::T_DSP);
        p.GetInlet(0)->SetMessage(framerate, YSE::T_DSP);
        p.GetInlet(0)->SetMessage(color, YSE::T_DSP);
        rig.Signal(in);
        rig.Block();
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

} // TEST_SUITE("patcher")
