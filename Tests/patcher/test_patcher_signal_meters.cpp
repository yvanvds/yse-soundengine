// Tests for the ~meter, ~levelmeter and ~gridmeter patcher objects
// (patcher/analysis/dMeter, dLevelmeter, dGridmeter), issue #1166. The
// measurement is DSP::levelMeter's and is tested in
// Tests/dsp/test_level_meter.cpp; these show that the signals, the numbers,
// the messages and the arguments reach it, that the reports go out only
// through the armed outlets (#1037), that the GUI values carry what a host
// draws, and that a rendered ~cycle -> ~* -> meters patch reads the sine's
// level after a JSON round trip.

#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/analysis/dMeter.h"
#include "patcher/analysis/dLevelmeter.h"
#include "patcher/analysis/dGridmeter.h"
#include "dsp/analysis/levelMeter.hpp"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "support/audio_helpers.hpp"
#include "sinks.hpp"

using TestHelpers::FloatSink;
using TestHelpers::ListSink;
using TestHelpers::ScopedSampleRate;
using TestHelpers::Wire;
using YSE::DSP::buffer;
using YSE::DSP::levelMeter;
using YSE::PATCHER::dGridmeter;
using YSE::PATCHER::dLevelmeter;
using YSE::PATCHER::dMeter;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  // A meter with a sink on its outlet. Block() runs one block and the flush
  // the patcher runs after its traversal, and says whether anything went out.
  template <typename Meter, typename Sink> struct Rig {
    Sink out;
    Meter meter;

    Rig() {
      Wire(meter, 0, out);
    }
    bool Got() const;
    void Clear();
    // One block with nothing connected: silence.
    bool Block() {
      Clear();
      meter.ResetDSP();
      meter.Calculate(YSE::T_DSP);
      meter.FlushArmedOutlets(YSE::T_GUI);
      return Got();
    }
    bool Block(buffer& in) {
      Clear();
      meter.ResetDSP();
      meter.GetInlet(0)->SetBuffer(&in, YSE::T_DSP); // the last input: runs CALC()
      meter.FlushArmedOutlets(YSE::T_GUI);
      return Got();
    }
  };

  template <> bool Rig<dMeter, FloatSink>::Got() const {
    return out.gotFloat;
  }
  template <> void Rig<dMeter, FloatSink>::Clear() {
    out.gotFloat = false;
  }
  template <> bool Rig<dLevelmeter, FloatSink>::Got() const {
    return out.gotFloat;
  }
  template <> void Rig<dLevelmeter, FloatSink>::Clear() {
    out.gotFloat = false;
  }
  template <> bool Rig<dGridmeter, ListSink>::Got() const {
    return out.gotList;
  }
  template <> void Rig<dGridmeter, ListSink>::Clear() {
    out.gotList = false;
  }

  using MeterRig = Rig<dMeter, FloatSink>;
  using LevelRig = Rig<dLevelmeter, FloatSink>;
  using GridRig = Rig<dGridmeter, ListSink>;

  // The GUI value as numbers.
  std::vector<float> Cells(const std::string& value) {
    std::vector<float> out;
    std::istringstream in(value);
    std::string token;
    while (in >> token)
      out.push_back(std::stof(token));
    return out;
  }

  std::vector<float> Cells(YSE::PATCHER::pObject& o) {
    const std::vector<float> all = Cells(o.GetGuiValue());
    // The bulk read is every cell, in order.
    REQUIRE(all.size() == o.GetGuiValueCount());
    for (unsigned int i = 0; i < o.GetGuiValueCount(); ++i)
      CHECK(std::stof(o.GetGuiValueAt(i)) == all[i]);
    CHECK(o.GetGuiValueAt(o.GetGuiValueCount()).empty());
    return all;
  }

  float Db(double amplitude) {
    return static_cast<float>(20.0 * std::log10(amplitude));
  }

  float GuiFloat(YSE::pHandle* h) {
    return std::stof(h->GetGuiValue());
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("meters: registry and shape (#1166)") {
    YSE::patcher p;
    p.create(1);
    struct Row {
      const char* type;
      std::size_t params;
      unsigned int cells;
      YSE::OUT_TYPE out;
    };
    const Row rows[] = {
        {YSE::OBJ::D_METER, 2, 3, YSE::OUT_TYPE::FLOAT},
        {YSE::OBJ::D_LEVELMETER, 3, 3, YSE::OUT_TYPE::FLOAT},
        {YSE::OBJ::D_GRIDMETER, 1, 2, YSE::OUT_TYPE::LIST},
    };
    for (const Row& row : rows) {
      CAPTURE(row.type);
      YSE::pHandle* obj = p.CreateObject(row.type);
      REQUIRE(obj != nullptr);
      CHECK(std::string(obj->Type()) == row.type);
      CHECK(obj->GetInputs() == 1);
      CHECK(obj->GetOutputs() == 1);
      CHECK(obj->IsDSPInput(0));
      CHECK(obj->OutputDataType(0) == row.out);
      CHECK(obj->GetGuiValueCount() == row.cells);
      CHECK_FALSE(obj->GuiValueIsSettable());

      std::unique_ptr<YSE::PATCHER::pObject> o(YSE::PATCHER::Register().Get(row.type));
      REQUIRE(o != nullptr);
      CHECK(o->GetCategory() == YSE::PATCHER::pCategory::ANALYSIS);
      CHECK(o->IsDSPObject());
      CHECK(o->GetParamCount() == row.params);
      const std::vector<float> cells = Cells(*o);
      CHECK(cells.size() == row.cells);
    }

    // A meter at rest: silence, nothing held, no clip.
    dMeter meter;
    CHECK(Cells(meter) == std::vector<float>{0.f, 0.f, 0.f});
    dLevelmeter level;
    CHECK(Cells(level) == std::vector<float>{levelMeter::SILENCE, levelMeter::SILENCE, 0.f});
    dGridmeter grid;
    CHECK(Cells(grid) == std::vector<float>{0.f, 0.f});
  }

  TEST_CASE("~meter: the peak of every interval goes out the armed outlet (#1166, #1037)") {
    ScopedSampleRate rate(1000); // one sample per millisecond
    MeterRig rig;
    rig.meter.SetParams("64"); // two windows a block
    buffer in(N);
    in = 0.f;
    in.getPtr()[10] = -0.25f;
    in.getPtr()[70] = 0.5f;
    rig.meter.ResetDSP();
    rig.meter.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
    CHECK_FALSE(rig.out.gotFloat); // CALC() only arms
    CHECK(rig.meter.GetOutlet(0)->Armed());
    rig.meter.FlushArmedOutlets(YSE::T_GUI);
    REQUIRE(rig.out.gotFloat);
    CHECK(rig.out.received == 0.5f); // the last window of the block
    CHECK(Cells(rig.meter) == std::vector<float>{0.5f, 0.5f, 0.f});

    // Unconnected: silence.
    REQUIRE(rig.Block());
    CHECK(rig.out.received == 0.f);

    // The default interval is 50 ms.
    MeterRig fresh;
    CHECK(fresh.meter.GetParams() == "");
    in = 0.f;
    in.getPtr()[60] = 0.75f; // window 50-99
    in.getPtr()[120] = 0.875f; // window 100-149, still open
    REQUIRE(fresh.Block(in));
    CHECK(fresh.out.received == 0.75f);
  }

  TEST_CASE("~meter: 'interval' is held to 10 - 2000 ms; numbers and messages are not saved "
            "(#1166)") {
    ScopedSampleRate rate(1000);
    MeterRig rig;
    rig.meter.SetParams("64");
    buffer in(N);
    in = 0.f;
    in.getPtr()[N - 1] = 0.5f;
    // 1 ms is 10: the window holding the last sample, 120-129, is still
    // open, where a 1 ms window would have ended on it.
    rig.meter.GetInlet(0)->SetMessage("interval 1", YSE::T_GUI);
    REQUIRE(rig.Block(in));
    CHECK(rig.out.received == 0.f);
    CHECK(rig.meter.GetParams() == "64");

    // 5000 ms is 2000: 15 blocks of 128 pass without a report.
    rig.meter.SetMessage("interval 5000", 0.f);
    in = 0.f;
    for (int k = 0; k < 15; ++k)
      CHECK_FALSE(rig.Block(in));
    CHECK(rig.Block(in));

    // Malformed or unknown messages change nothing.
    rig.meter.SetMessage("interval", 0.f);
    rig.meter.SetMessage("interval abc", 0.f);
    rig.meter.SetMessage("hold nan", 0.f);
    rig.meter.SetMessage("set 0", 0.f);
    rig.meter.SetMessage("", 0.f);
    for (int k = 0; k < 14; ++k)
      CHECK_FALSE(rig.Block(in));
  }

  TEST_CASE("~meter: a number is shown and sent out; clip, hold and 'clear' (#1166)") {
    ScopedSampleRate rate(1000);
    MeterRig rig;
    rig.meter.SetParams("1000 2000"); // no window ends in this test
    // A number goes straight out, as in Max, and is shown at the next block.
    rig.meter.GetInlet(0)->SetFloat(0.625f, YSE::T_GUI);
    REQUIRE(rig.out.gotFloat);
    CHECK(rig.out.received == 0.625f);
    CHECK_FALSE(rig.Block()); // no window ended
    CHECK(Cells(rig.meter) == std::vector<float>{0.625f, 0.625f, 0.f});

    rig.meter.GetInlet(0)->SetInt(2, YSE::T_GUI); // above 1: the clip lights
    REQUIRE(rig.out.gotFloat);
    CHECK(rig.out.received == 2.f);
    rig.Block();
    CHECK(Cells(rig.meter) == std::vector<float>{2.f, 2.f, 1.f});
    rig.meter.GetInlet(0)->SetFloat(0.25f, YSE::T_GUI);
    rig.Block();
    // A number counts as one interval, 1000 ms of the 2000 the hold keeps
    // 2 for; the clip stays on.
    CHECK(Cells(rig.meter) == std::vector<float>{0.25f, 2.f, 1.f});

    rig.meter.GetInlet(0)->SetMessage("clear", YSE::T_GUI);
    rig.Block();
    CHECK(Cells(rig.meter) == std::vector<float>{0.25f, 0.25f, 0.f});

    // A signal above 1 clips too; 'hold 0' makes the hold the level.
    rig.meter.SetMessage("hold 0", 0.f);
    rig.meter.SetMessage("interval 64", 0.f);
    buffer in(N);
    in = 0.f;
    in.getPtr()[3] = -1.5f;
    REQUIRE(rig.Block(in));
    CHECK(rig.out.received == 0.f);
    CHECK(Cells(rig.meter) == std::vector<float>{0.f, 0.f, 1.f});
  }

  TEST_CASE("~levelmeter: the RMS in dB every interval, a bang and a number (#1166, #1037)") {
    ScopedSampleRate rate(1000);
    LevelRig rig;
    rig.meter.SetParams("0 64"); // fast, two windows a block
    buffer in(N);
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = i % 2 == 0 ? 0.5f : -0.5f;
    REQUIRE(rig.Block(in));
    CHECK(rig.out.received == doctest::Approx(Db(0.5)).epsilon(1e-5));
    CHECK(Cells(rig.meter)[0] == doctest::Approx(Db(0.5)).epsilon(1e-5));

    // RMS, not peak: a window with one sample at 1 among 63 zeros.
    in = 0.f;
    in.getPtr()[100] = 1.f;
    REQUIRE(rig.Block(in));
    CHECK(rig.out.received == doctest::Approx(Db(1.0 / 8.0)).epsilon(1e-5));
    REQUIRE(rig.Block());
    CHECK(rig.out.received == levelMeter::SILENCE);

    // A bang sends the reading at the next block; so does every window.
    LevelRig slow;
    slow.meter.SetParams("0 1000");
    for (unsigned int i = 0; i < N; ++i)
      in.getPtr()[i] = 0.25f;
    CHECK_FALSE(slow.Block(in));
    slow.meter.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(slow.out.gotFloat); // a bang only asks
    REQUIRE(slow.Block(in));
    CHECK(slow.out.received == levelMeter::SILENCE); // no window has ended

    // A number is a reading in dB: sent out, and shown.
    slow.meter.GetInlet(0)->SetInt(-12, YSE::T_GUI);
    REQUIRE(slow.out.gotFloat);
    CHECK(slow.out.received == -12.f);
    slow.Block(in);
    CHECK(Cells(slow.meter)[0] == doctest::Approx(-12.f).epsilon(1e-5));
    slow.meter.GetInlet(0)->SetFloat(-30.5f, YSE::T_GUI);
    CHECK(slow.out.received == -30.5f);
    slow.Block(in);
    CHECK(Cells(slow.meter)[0] == doctest::Approx(-30.5f).epsilon(1e-5));
    slow.meter.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(slow.Block(in));
    CHECK(slow.out.received == doctest::Approx(-30.5f).epsilon(1e-5));
  }

  TEST_CASE("~levelmeter: 'mode' sets Max's ballistics; attack, release and offset "
            "(#1166)") {
    ScopedSampleRate rate(1000);
    LevelRig rig;
    rig.meter.SetParams("1 64"); // VU: 330 ms, 330 ms, -20 dB
    buffer in(N);
    in = 0.5f;
    REQUIRE(rig.Block(in));
    // Two windows of 64 ms toward 0.5.
    const double vu = 0.5 * (1.0 - std::exp(-128.0 / 330.0));
    CHECK(rig.out.received == doctest::Approx(Db(vu) + 20.f).epsilon(1e-4));
    for (int k = 0; k < 40; ++k) // 5 s: settled
      rig.Block(in);
    CHECK(rig.out.received == doctest::Approx(Db(0.5) + 20.f).epsilon(1e-4));

    // 'mode 2': DIN, -14 dB; the reading moves at once.
    rig.meter.GetInlet(0)->SetMessage("mode 2", YSE::T_GUI);
    REQUIRE(rig.Block(in));
    CHECK(rig.out.received == doctest::Approx(Db(0.5) + 14.f).epsilon(1e-4));
    CHECK(rig.meter.GetParams() == "1 64"); // not saved

    // Out of range modes are the nearest; 9 is EBU, -1 fast.
    rig.meter.SetMessage("mode 9", 0.f);
    REQUIRE(rig.Block(in));
    CHECK(rig.out.received == doctest::Approx(Db(0.5) + 14.f).epsilon(1e-4));
    rig.meter.SetMessage("mode -1", 0.f);
    REQUIRE(rig.Block(in));
    CHECK(rig.out.received == doctest::Approx(Db(0.5)).epsilon(1e-4));

    // 'attack', 'release' and 'offset' one by one.
    rig.meter.SetMessage("offset 6", 0.f);
    rig.meter.SetMessage("release 64", 0.f);
    in = 0.f;
    REQUIRE(rig.Block(in));
    CHECK(rig.out.received == doctest::Approx(Db(0.5 * std::exp(-2.0)) - 6.f).epsilon(1e-4));
    rig.meter.SetMessage("attack 128", 0.f);
    in = 0.5f;
    const double down = 0.5 * std::exp(-2.0);
    const double up = down + (0.5 - down) * (1.0 - std::exp(-0.5));
    REQUIRE(rig.Block(in));
    CHECK(rig.out.received ==
          doctest::Approx(Db(up + (0.5 - up) * (1.0 - std::exp(-0.5))) - 6.f).epsilon(1e-4));
    rig.meter.SetMessage("attack", 0.f); // malformed: nothing
    rig.meter.SetMessage("offset x", 0.f);
    rig.meter.SetMessage("modes 1", 0.f);
  }

  TEST_CASE("~levelmeter: hold, clip and 'clear' in the GUI value (#1166)") {
    ScopedSampleRate rate(1000);
    LevelRig rig;
    rig.meter.SetParams("0 64 200");
    buffer in(N);
    in = 0.f;
    in.getPtr()[100] = 1.5f; // clips; window 64-127: sqrt(2.25 / 64)
    rig.Block(in);
    const float top = Db(1.5 / 8.0);
    std::vector<float> cells = Cells(rig.meter);
    CHECK(cells[0] == doctest::Approx(top).epsilon(1e-5));
    CHECK(cells[1] == doctest::Approx(top).epsilon(1e-5));
    CHECK(cells[2] == 1.f);
    rig.Block();
    cells = Cells(rig.meter);
    CHECK(cells[0] == levelMeter::SILENCE);
    CHECK(cells[1] == doctest::Approx(top).epsilon(1e-5)); // held for 200 ms
    CHECK(cells[2] == 1.f);
    rig.meter.GetInlet(0)->SetMessage("clear", YSE::T_GUI);
    rig.Block();
    CHECK(Cells(rig.meter) == std::vector<float>{levelMeter::SILENCE, levelMeter::SILENCE, 0.f});
  }

  TEST_CASE("~gridmeter: the cell count shapes the inlets and the GUI value (#1166)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* four = p.CreateObject(YSE::OBJ::D_GRIDMETER, "4");
    REQUIRE(four != nullptr);
    CHECK(four->GetInputs() == 4);
    for (unsigned int i = 0; i < 4; ++i)
      CHECK(four->IsDSPInput(i));
    CHECK(four->GetGuiValueCount() == 8);
    CHECK(four->GetGuiValueAt(8).empty());

    dGridmeter grid;
    grid.SetParams("0");
    CHECK(grid.NumInputs() == 1);
    grid.SetParams("100");
    CHECK(grid.NumInputs() == dGridmeter::MAX_CELLS);
    CHECK(grid.GetGuiValueCount() == 2 * dGridmeter::MAX_CELLS);
    grid.SetParams("");
    CHECK(grid.NumInputs() == 1);
  }

  TEST_CASE("~gridmeter: brightness from -range dB to 0 dB, with ballistics (#1166)") {
    ScopedSampleRate rate(1000);
    GridRig rig;
    rig.meter.SetParams("1 60 0 0 64"); // no ballistics
    buffer in(N);
    in = 0.5f;
    CHECK_FALSE(rig.Block(in)); // levels are not messages
    std::vector<float> cells = Cells(rig.meter);
    REQUIRE(cells.size() == 2);
    CHECK(cells[0] == doctest::Approx((Db(0.5) + 60.f) / 60.f).epsilon(1e-5));
    CHECK(cells[1] == 0.f);
    in = 2.f; // past 0 dB: above 1, the hot colour
    rig.Block(in);
    CHECK(Cells(rig.meter)[0] == doctest::Approx((Db(2.0) + 60.f) / 60.f).epsilon(1e-5));
    rig.meter.GetInlet(0)->SetMessage("range 30", YSE::T_GUI);
    CHECK(Cells(rig.meter)[0] == doctest::Approx((Db(2.0) + 30.f) / 30.f).epsilon(1e-5));
    rig.meter.SetMessage("range -5", 0.f); // not a range: 60
    CHECK(Cells(rig.meter)[0] == doctest::Approx((Db(2.0) + 60.f) / 60.f).epsilon(1e-5));
    in = 0.0001f; // -80 dB: below the range
    rig.Block(in);
    CHECK(Cells(rig.meter)[0] == 0.f);

    // The default attack and release are 300 ms.
    GridRig slow;
    slow.meter.SetParams("1 60");
    in = 1.f;
    slow.Block(in); // windows end at 19, 39, ..., 119: six of 20 ms
    const double level = 1.0 - std::exp(-120.0 / 300.0);
    CHECK(Cells(slow.meter)[0] == doctest::Approx((Db(level) + 60.f) / 60.f).epsilon(1e-4));
    slow.meter.SetMessage("attack 0", 0.f);
    slow.Block(in);
    CHECK(Cells(slow.meter)[0] == doctest::Approx(1.f).epsilon(1e-6));
    slow.meter.SetMessage("release 0", 0.f);
    slow.meter.SetMessage("interval 64", 0.f);
    in = 0.f;
    slow.Block(in);
    CHECK(Cells(slow.meter)[0] == 0.f);
  }

  TEST_CASE("~gridmeter: mute, muteall, solo and a list go out after the block (#1166, "
            "#1037)") {
    GridRig rig;
    rig.meter.SetParams("3");
    rig.meter.GetInlet(0)->SetMessage("mute 2 1", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList); // sent after the next block
    REQUIRE(rig.Block());
    CHECK(rig.out.received == "0 1 0");
    CHECK(Cells(rig.meter) == std::vector<float>{0.f, 0.f, 0.f, 0.f, 1.f, 0.f});
    CHECK_FALSE(rig.Block()); // once

    rig.meter.SetMessage("solo 3", 0.f);
    REQUIRE(rig.Block());
    CHECK(rig.out.received == "1 1 0");
    rig.meter.SetMessage("solo 0", 0.f);
    REQUIRE(rig.Block());
    CHECK(rig.out.received == "0 0 0");
    rig.meter.SetMessage("muteall 1", 0.f);
    REQUIRE(rig.Block());
    CHECK(rig.out.received == "1 1 1");
    rig.meter.GetInlet(0)->SetList("0 1", YSE::T_GUI); // cells past the list keep theirs
    REQUIRE(rig.Block());
    CHECK(rig.out.received == "0 1 1");
    rig.meter.SetMessage("mute 3 0", 0.f);
    rig.meter.SetMessage("mute 1 2", 0.f); // any non-zero mutes
    REQUIRE(rig.Block());
    CHECK(rig.out.received == "1 1 0");
    CHECK(Cells(rig.meter) == std::vector<float>{0.f, 0.f, 0.f, 1.f, 1.f, 0.f});

    // Cells that do not exist, and malformed messages, change nothing.
    rig.meter.SetMessage("mute 4 1", 0.f);
    rig.meter.SetMessage("mute 0 1", 0.f);
    rig.meter.SetMessage("mute 1", 0.f);
    rig.meter.SetMessage("mute", 0.f);
    rig.meter.SetMessage("solo 9", 0.f);
    rig.meter.SetMessage("muteall", 0.f);
    rig.meter.SetMessage("range 20", 0.f); // a setting, not a mute
    rig.meter.SetMessage("", 0.f);
    CHECK_FALSE(rig.Block());
    CHECK(Cells(rig.meter) == std::vector<float>{0.f, 0.f, 0.f, 1.f, 1.f, 0.f});
  }

  TEST_CASE("meters: the arguments survive a DumpJSON / ParseJSON round trip (#1166)") {
    std::string dump;
    {
      patcherImplementation src(1, nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_METER, "25 300") != nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_METER, "") != nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_LEVELMETER, "3 50 1500") != nullptr);
      REQUIRE(src.CreateObject(YSE::OBJ::D_GRIDMETER, "4 48 10 500 40") != nullptr);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    const char* types[] = {"~meter", "~meter", "~levelmeter", "~gridmeter"};
    const char* params[] = {"25 300", "", "3 50 1500", "4 48 10 500 40"};
    for (unsigned int i = 0; i < 4; ++i) {
      CAPTURE(i);
      YSE::pHandle* h = dst.GetHandleFromID(i);
      REQUIRE(h != nullptr);
      CHECK(std::string(h->Type()) == types[i]);
      CHECK(h->GetParams() == params[i]);
    }
    CHECK(dst.GetHandleFromID(3)->GetInputs() == 4);
  }

  TEST_CASE("meters: in a rendered patch they read a 1 kHz sine, after a JSON round trip "
            "(#1166)") {
    ScopedSampleRate rate(48000);
    std::string dump;
    unsigned int meterID = 0;
    unsigned int levelID = 0;
    unsigned int gridID = 0;
    unsigned int peakID = 0;
    unsigned int dbID = 0;
    {
      patcherImplementation src(1, nullptr);
      // 48 samples per cycle; ~cycle starts on its crest.
      YSE::pHandle* osc = src.CreateObject(YSE::OBJ::D_CYCLE, "1000");
      YSE::pHandle* gain = src.CreateObject(YSE::OBJ::D_MULTIPLY, "0.5");
      YSE::pHandle* quiet = src.CreateObject(YSE::OBJ::D_MULTIPLY, "0.01");
      YSE::pHandle* meter = src.CreateObject(YSE::OBJ::D_METER, "10");
      YSE::pHandle* level = src.CreateObject(YSE::OBJ::D_LEVELMETER, "1");
      YSE::pHandle* grid = src.CreateObject(YSE::OBJ::D_GRIDMETER, "3 60");
      YSE::pHandle* peakOut = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* dbOut = src.CreateObject(YSE::OBJ::G_FLOAT, "");
      YSE::pHandle* control = src.CreateObject(YSE::OBJ::G_RECEIVE, "grid1166");
      REQUIRE(osc != nullptr);
      REQUIRE(gain != nullptr);
      REQUIRE(quiet != nullptr);
      REQUIRE(meter != nullptr);
      REQUIRE(level != nullptr);
      REQUIRE(grid != nullptr);
      REQUIRE(peakOut != nullptr);
      REQUIRE(dbOut != nullptr);
      REQUIRE(control != nullptr);
      src.Connect(osc, 0, gain, 0);
      src.Connect(osc, 0, quiet, 0);
      src.Connect(gain, 0, meter, 0);
      src.Connect(gain, 0, level, 0);
      src.Connect(gain, 0, grid, 0);
      src.Connect(quiet, 0, grid, 1);
      src.Connect(control, 0, grid, 0);
      src.Connect(meter, 0, peakOut, 0);
      src.Connect(level, 0, dbOut, 0);
      meterID = meter->GetID();
      levelID = level->GetID();
      gridID = grid->GetID();
      peakID = peakOut->GetID();
      dbID = dbOut->GetID();
      dump = src.DumpJSON();
    }
    patcherImplementation dst(1, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* meter = dst.GetHandleFromID(meterID);
    YSE::pHandle* level = dst.GetHandleFromID(levelID);
    YSE::pHandle* grid = dst.GetHandleFromID(gridID);
    YSE::pHandle* peakOut = dst.GetHandleFromID(peakID);
    YSE::pHandle* dbOut = dst.GetHandleFromID(dbID);
    REQUIRE(meter != nullptr);
    REQUIRE(level != nullptr);
    REQUIRE(grid != nullptr);
    REQUIRE(peakOut != nullptr);
    REQUIRE(dbOut != nullptr);
    REQUIRE(grid->GetInputs() == 3);

    for (int k = 0; k < 375 * 3; ++k) // 3 s: the VU and the grid have settled
      dst.Calculate(YSE::T_DSP);
    CHECK(GuiFloat(peakOut) == doctest::Approx(0.5f).epsilon(1e-4));
    // The RMS of the sine against the VU's reference level, -20 dBFS.
    CHECK(GuiFloat(dbOut) == doctest::Approx(Db(0.5 / std::sqrt(2.0)) + 20.f).epsilon(1e-3));
    std::vector<float> cells = Cells(meter->GetGuiValue());
    REQUIRE(cells.size() == 3);
    CHECK(cells[0] == doctest::Approx(0.5f).epsilon(1e-4));
    CHECK(cells[2] == 0.f);
    cells = Cells(level->GetGuiValue());
    REQUIRE(cells.size() == 3);
    CHECK(cells[0] == doctest::Approx(GuiFloat(dbOut)).epsilon(1e-4));
    cells = Cells(grid->GetGuiValue());
    REQUIRE(cells.size() == 6);
    CHECK(cells[0] == doctest::Approx((Db(0.5) + 60.f) / 60.f).epsilon(1e-3));
    CHECK(cells[1] == doctest::Approx((Db(0.01) + 60.f) / 60.f).epsilon(1e-3)); // -40 dB
    CHECK(cells[2] == 0.f); // unconnected: silence
    CHECK(cells[3] == 0.f);

    // A mute through the receive shows in the GUI value.
    REQUIRE(dst.PassData(std::string("mute 3 1"), "grid1166", YSE::T_GUI));
    dst.Calculate(YSE::T_DSP);
    cells = Cells(grid->GetGuiValue());
    CHECK(cells[3] == 0.f);
    CHECK(cells[4] == 0.f);
    CHECK(cells[5] == 1.f);
  }

  TEST_CASE("meters: numbers, messages, blocks and the flush on the audio thread allocate "
            "nothing (#1166)") {
    ScopedSampleRate rate(1000);
    MeterRig meter;
    LevelRig level;
    GridRig grid;
    grid.meter.SetParams("4");
    buffer in(N);
    in = 0.5f;
    const std::string interval = "interval 16";
    const std::string hold = "hold 100";
    const std::string clear = "clear";
    const std::string mode = "mode 1";
    const std::string offset = "offset -3";
    const std::string mute = "mute 2 1";
    const std::string solo = "solo 3";
    const std::string all = "muteall 0";
    const std::string list = "1 0 1 0";
    meter.Block(in);
    level.Block(in);
    grid.Block(in);
    {
      TestHelpers::ProbeScope probe;
      meter.meter.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      meter.meter.GetInlet(0)->SetInt(2, YSE::T_DSP);
      meter.meter.GetInlet(0)->SetMessage(interval, YSE::T_DSP);
      meter.meter.GetInlet(0)->SetMessage(hold, YSE::T_DSP);
      meter.meter.GetInlet(0)->SetMessage(clear, YSE::T_DSP);
      meter.Block(in);
      level.meter.GetInlet(0)->SetBang(YSE::T_DSP);
      level.meter.GetInlet(0)->SetFloat(-6.f, YSE::T_DSP);
      level.meter.GetInlet(0)->SetMessage(mode, YSE::T_DSP);
      level.meter.GetInlet(0)->SetMessage(offset, YSE::T_DSP);
      level.meter.GetInlet(0)->SetMessage(clear, YSE::T_DSP);
      level.Block(in);
      grid.meter.GetInlet(0)->SetMessage(mute, YSE::T_DSP);
      grid.meter.GetInlet(0)->SetMessage(solo, YSE::T_DSP);
      grid.meter.GetInlet(0)->SetMessage(all, YSE::T_DSP);
      grid.meter.GetInlet(0)->SetMessage(interval, YSE::T_DSP);
      grid.meter.GetInlet(0)->SetList(list, YSE::T_DSP);
      grid.meter.ResetDSP();
      grid.meter.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
      grid.meter.FlushArmedOutlets(YSE::T_DSP);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(grid.out.received == "1 0 1 0");
  }

} // TEST_SUITE("patcher")
