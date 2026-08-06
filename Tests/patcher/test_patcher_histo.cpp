// Tests for .histo (issue #458) — a histogram of the numbers received.
//
// The object is small, which makes it easy to write a test suite that proves
// nothing: send three numbers, check a list came out. What actually
// characterises `histo` is
//
//   * per-value counting — the count is for *that* number, climbing once per
//     occurrence, and independent of every other bin. The classic error is one
//     running total that every number shares;
//   * the two inlets — inlet 0 counts and reports, inlet 1 only reports. A
//     query that silently counts turns a read of the histogram into a write of
//     it, which is the whole reason Max gives the object a right inlet;
//   * bang — re-reports the *most recently counted* number, not the most
//     recently touched one, and reports '0 0' before anything has arrived;
//   * range — the accepted values are [0, size-1] and anything else is
//     disregarded, which is Max's behaviour and this port's documented refusal
//     (silent on outlet 0, bang on outlet 1) rather than a clip;
//   * clear / dump — clear zeroes the bins but keeps the last number, and dump
//     replays the non-empty bins in ascending order;
//   * overflow — a bin saturates rather than wrapping, since a wrapped bin
//     turns the most-seen value into the least-seen one.
//
// The saturation case is asserted through the object's own accessors rather
// than by sending a billion messages: MAX_COUNT is 2^30, so a message-level
// test of it would take minutes. What the messages *can* prove — and do below —
// is that the count climbs exactly once per occurrence, which is the property
// saturation is the boundary of.
//
// Nothing here needs an audio device.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <vector>

#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/gHisto.h"
#include "patcher/sinks.hpp"

using TestHelpers::BangSink;
using TestHelpers::ListSink;

namespace {

  // Collects every list an outlet produces, in order.
  struct LineCollector : YSE::PATCHER::pObject {
    std::vector<std::string> lines;
    LineCollector() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { lines.push_back(v); });
    }
    const char* Type() const override {
      return "line_collector";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A .histo with both outlets wired: the report on outlet 0, the
  // out-of-range refusal on outlet 1.
  struct HistoRig {
    YSE::PATCHER::gHisto histo;
    ListSink reports;
    BangSink rejects;

    explicit HistoRig(const char* params = nullptr) {
      histo.ConnectOutlet(reports.GetInlet(0), 0);
      reports.ConnectInlet(histo.GetOutlet(0), 0);
      histo.ConnectOutlet(rejects.GetInlet(0), 1);
      rejects.ConnectInlet(histo.GetOutlet(1), 0);
      if (params != nullptr) histo.SetParams(params);
    }

    // Sends one number to inlet 0. Returns the list it produced, or "" when it
    // produced none — a real outcome here, not an error.
    std::string Send(int value) {
      reports.gotList = false;
      reports.received.clear();
      histo.GetInlet(0)->SetInt(value, YSE::T_GUI);
      return reports.gotList ? reports.received : std::string();
    }

    std::string SendFloat(float value) {
      reports.gotList = false;
      reports.received.clear();
      histo.GetInlet(0)->SetFloat(value, YSE::T_GUI);
      return reports.gotList ? reports.received : std::string();
    }

    // Asks inlet 1 for a count without counting it.
    std::string Query(int value) {
      reports.gotList = false;
      reports.received.clear();
      histo.GetInlet(1)->SetInt(value, YSE::T_GUI);
      return reports.gotList ? reports.received : std::string();
    }

    std::string Bang() {
      reports.gotList = false;
      reports.received.clear();
      histo.GetInlet(0)->SetBang(YSE::T_GUI);
      return reports.gotList ? reports.received : std::string();
    }

    void Message(const std::string& text) {
      histo.GetInlet(0)->SetList(text, YSE::T_GUI);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("histo: creatable through the registry with the documented shape (#458)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_HISTO);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".histo"));
    CHECK(std::string(YSE::OBJ::G_HISTO) == std::string(".histo"));
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 2);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::LIST);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::BANG);
  }

  TEST_CASE("histo: is listed by the registry (#458)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& n : names) {
      if (n == std::string(YSE::OBJ::G_HISTO)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("histo: names its parameter (#458)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_HISTO));
    REQUIRE(obj != nullptr);
    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == std::string("size"));
  }

  TEST_CASE("histo: params survive a DumpJSON / ParseJSON round trip (#458)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_HISTO, "8") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".histo") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".histo"));
    CHECK(h->GetParams() == std::string("8"));

    // ...and the size reached the object, not just the parameter string. With
    // eight bins only 0-7 are countable, so of these four numbers exactly two
    // land; at the default size of 128 all four would. The GUI value is the
    // total counted.
    h->SetIntData(0, 3);
    h->SetIntData(0, 7);
    h->SetIntData(0, 8);
    h->SetIntData(0, 100);
    CHECK(h->GetGuiValue() == std::string("2"));
  }

  // ─── counting ───────────────────────────────────────────────────────────────

  TEST_CASE("histo: the first occurrence of a number reports a count of one (#458)") {
    HistoRig rig;
    CHECK(rig.Send(60) == std::string("60 1"));
    CHECK(rig.histo.CountOf(60) == 1);
    CHECK(rig.rejects.bangCount == 0);
  }

  TEST_CASE("histo: the count climbs once per occurrence (#458)") {
    HistoRig rig;
    for (int expected = 1; expected <= 25; ++expected) {
      CAPTURE(expected);
      CHECK(rig.Send(60) == "60 " + std::to_string(expected));
    }
    CHECK(rig.histo.CountOf(60) == 25);
    CHECK(rig.histo.Total() == 25);
  }

  TEST_CASE("histo: counts are per value, not one running total (#458)") {
    // The classic implementation error: one counter every number shares.
    HistoRig rig;
    CHECK(rig.Send(1) == std::string("1 1"));
    CHECK(rig.Send(2) == std::string("2 1"));
    CHECK(rig.Send(3) == std::string("3 1"));
    CHECK(rig.Send(1) == std::string("1 2")); // 1's own second occurrence, not 4
    CHECK(rig.Send(2) == std::string("2 2"));
    CHECK(rig.Send(1) == std::string("1 3"));

    CHECK(rig.histo.CountOf(1) == 3);
    CHECK(rig.histo.CountOf(2) == 2);
    CHECK(rig.histo.CountOf(3) == 1);
    CHECK(rig.histo.CountOf(4) == 0); // never seen
    CHECK(rig.histo.Total() == 6);
  }

  TEST_CASE("histo: a float counts as the same number, truncated (#458)") {
    HistoRig rig;
    CHECK(rig.SendFloat(60.9f) == std::string("60 1"));
    CHECK(rig.SendFloat(60.1f) == std::string("60 2"));
    CHECK(rig.histo.CountOf(61) == 0);
  }

  TEST_CASE("histo: zero is a countable value like any other (#458)") {
    // Worth pinning because 0 is also the initial `last`, so a report of
    // "0 <n>" has two possible sources and the bounds check touches it.
    HistoRig rig;
    CHECK(rig.Send(0) == std::string("0 1"));
    CHECK(rig.Send(0) == std::string("0 2"));
    CHECK(rig.rejects.bangCount == 0);
  }

  // ─── the query inlet ────────────────────────────────────────────────────────

  TEST_CASE("histo: inlet 1 reports a count without counting it (#458)") {
    // Max: the right inlet "functions identically to left inlet integers but
    // the number is not counted".
    HistoRig rig;
    rig.Send(5);
    rig.Send(5);
    CHECK(rig.Query(5) == std::string("5 2"));
    // Asking twice more must not have moved it.
    CHECK(rig.Query(5) == std::string("5 2"));
    CHECK(rig.Query(5) == std::string("5 2"));
    CHECK(rig.histo.CountOf(5) == 2);
    CHECK(rig.histo.Total() == 2);
  }

  TEST_CASE("histo: querying a value never counted reports zero (#458)") {
    HistoRig rig;
    rig.Send(5);
    CHECK(rig.Query(6) == std::string("6 0"));
    CHECK(rig.histo.CountOf(6) == 0);
    CHECK(rig.histo.Total() == 1);
  }

  TEST_CASE("histo: a float query truncates and still does not count (#458)") {
    HistoRig rig;
    rig.Send(9);
    rig.reports.gotList = false;
    rig.histo.GetInlet(1)->SetFloat(9.8f, YSE::T_GUI);
    CHECK(rig.reports.received == std::string("9 1"));
    CHECK(rig.histo.CountOf(9) == 1);
  }

  // ─── bang ───────────────────────────────────────────────────────────────────

  TEST_CASE("histo: bang re-reports the most recently counted number (#458)") {
    HistoRig rig;
    rig.Send(3);
    rig.Send(7);
    rig.Send(7);
    CHECK(rig.Bang() == std::string("7 2"));
    // ...and banging does not count anything itself.
    CHECK(rig.Bang() == std::string("7 2"));
    CHECK(rig.histo.CountOf(7) == 2);
    CHECK(rig.histo.Total() == 3);
  }

  TEST_CASE("histo: bang before any number reports zero (#458)") {
    // Max: "outputs 0 if no number has been received previously."
    HistoRig rig;
    CHECK(rig.Bang() == std::string("0 0"));
    CHECK(rig.histo.Total() == 0);
  }

  TEST_CASE("histo: a query does not become what the next bang reports (#458)") {
    // A read of the histogram must not move the object's idea of where the
    // stream is — otherwise polling it rewrites what it says.
    HistoRig rig;
    rig.Send(4);
    rig.Send(4);
    rig.Query(9);
    CHECK(rig.Bang() == std::string("4 2"));
  }

  TEST_CASE("histo: a refused number does not become what the next bang reports (#458)") {
    HistoRig rig("16");
    rig.Send(5);
    rig.Send(900); // out of range
    CHECK(rig.Bang() == std::string("5 1"));
  }

  // ─── range ──────────────────────────────────────────────────────────────────

  TEST_CASE("histo: a number at or above the size is disregarded (#458)") {
    // Max: "numbers outside this range are disregarded". Size 16 means the
    // accepted values are 0-15, so 16 is already one too far.
    HistoRig rig("16");
    CHECK(rig.histo.Size() == 16);
    CHECK(rig.Send(15) == std::string("15 1"));
    CHECK(rig.Send(16) == std::string(""));
    CHECK(rig.rejects.bangCount == 1);
    CHECK(rig.Send(9999) == std::string(""));
    CHECK(rig.rejects.bangCount == 2);
    // Nothing was counted, and nothing was piled onto the boundary bin — which
    // is what clipping instead of refusing would have done.
    CHECK(rig.histo.CountOf(15) == 1);
    CHECK(rig.histo.Total() == 1);
  }

  TEST_CASE("histo: a negative number is disregarded (#458)") {
    HistoRig rig("16");
    CHECK(rig.Send(-1) == std::string(""));
    CHECK(rig.Send(-9000) == std::string(""));
    CHECK(rig.rejects.bangCount == 2);
    CHECK(rig.histo.CountOf(0) == 0); // not clipped to the bottom bin either
    CHECK(rig.histo.Total() == 0);
  }

  TEST_CASE("histo: an out-of-range query is refused the same way (#458)") {
    HistoRig rig("16");
    CHECK(rig.Query(16) == std::string(""));
    CHECK(rig.rejects.bangCount == 1);
    CHECK(rig.Query(-3) == std::string(""));
    CHECK(rig.rejects.bangCount == 2);
    CHECK(rig.histo.CountOf(16) == 0);
  }

  TEST_CASE("histo: the default size covers a MIDI note range (#458)") {
    // The reason the default is 128 rather than something rounder.
    HistoRig rig;
    CHECK(rig.histo.Size() == 128);
    CHECK(rig.Send(0) == std::string("0 1"));
    CHECK(rig.Send(127) == std::string("127 1"));
    CHECK(rig.Send(128) == std::string("")); // one past the top note
    CHECK(rig.rejects.bangCount == 1);
  }

  TEST_CASE("histo: the size is clamped to the fixed bin array (#458)") {
    YSE::PATCHER::gHisto a;
    a.SetParams("99999");
    CHECK(a.Size() == YSE::PATCHER::gHisto::CAPACITY);

    YSE::PATCHER::gHisto b;
    b.SetParams("0");
    CHECK(b.Size() == 1);

    YSE::PATCHER::gHisto c;
    c.SetParams("-40");
    CHECK(c.Size() == 1);

    YSE::PATCHER::gHisto d;
    d.SetParams("500");
    CHECK(d.Size() == 500);
  }

  TEST_CASE("histo: the top bin of a full-capacity histogram is reachable (#458)") {
    // The bounds check and the array bound have to agree at the very last slot,
    // which is where an off-by-one would write past the end.
    constexpr int CAP = YSE::PATCHER::gHisto::CAPACITY;
    HistoRig rig("99999");
    REQUIRE(rig.histo.Size() == CAP);
    CHECK(rig.Send(CAP - 1) == std::to_string(CAP - 1) + " 1");
    CHECK(rig.Send(CAP) == std::string(""));
    CHECK(rig.rejects.bangCount == 1);
  }

  TEST_CASE("histo: a size of one accepts only zero (#458)") {
    HistoRig rig("1");
    CHECK(rig.Send(0) == std::string("0 1"));
    CHECK(rig.Send(1) == std::string(""));
    CHECK(rig.rejects.bangCount == 1);
  }

  // ─── clear ──────────────────────────────────────────────────────────────────

  TEST_CASE("histo: `clear` zeroes every bin (#458)") {
    HistoRig rig;
    rig.Send(1);
    rig.Send(1);
    rig.Send(2);
    REQUIRE(rig.histo.Total() == 3);

    rig.Message("clear");
    CHECK(rig.histo.CountOf(1) == 0);
    CHECK(rig.histo.CountOf(2) == 0);
    CHECK(rig.histo.Total() == 0);
    // ...and counting starts again from one.
    CHECK(rig.Send(1) == std::string("1 1"));
  }

  TEST_CASE("histo: `clear` keeps the last number so a bang reports its zero (#458)") {
    HistoRig rig;
    rig.Send(42);
    rig.Send(42);
    rig.Message("clear");
    CHECK(rig.Bang() == std::string("42 0"));
  }

  TEST_CASE("histo: an unknown message is ignored (#458)") {
    HistoRig rig;
    rig.Send(1);
    rig.Send(1);
    for (const char* junk : {"", " ", "banana", "clear 3", "clearing", "dumps", "1 2", "-"}) {
      CAPTURE(junk);
      rig.Message(junk);
    }
    CHECK(rig.histo.CountOf(1) == 2);
    CHECK(rig.histo.Total() == 2);
    CHECK(rig.rejects.bangCount == 0);
  }

  // ─── dump ───────────────────────────────────────────────────────────────────

  TEST_CASE("histo: `dump` reports every non-empty bin in ascending order (#458)") {
    YSE::PATCHER::gHisto histo;
    LineCollector collector;
    histo.ConnectOutlet(collector.GetInlet(0), 0);
    collector.ConnectInlet(histo.GetOutlet(0), 0);

    // Sent out of order, so the ascending order of the dump is the object's
    // doing rather than the input's.
    for (int value : {7, 2, 7, 4, 2, 7})
      histo.GetInlet(0)->SetInt(value, YSE::T_GUI);
    collector.lines.clear(); // drop the live reports; only the dump matters

    histo.GetInlet(0)->SetList("dump", YSE::T_GUI);
    REQUIRE(collector.lines.size() == 3u);
    CHECK(collector.lines[0] == std::string("2 2"));
    CHECK(collector.lines[1] == std::string("4 1"));
    CHECK(collector.lines[2] == std::string("7 3"));
  }

  TEST_CASE("histo: `dump` says nothing about an empty histogram (#458)") {
    HistoRig rig;
    rig.reports.gotList = false;
    rig.Message("dump");
    CHECK_FALSE(rig.reports.gotList);

    // ...and after `clear` too, which is the same emptiness by another route.
    rig.Send(1);
    rig.Send(2);
    rig.Message("clear");
    rig.reports.gotList = false;
    rig.Message("dump");
    CHECK_FALSE(rig.reports.gotList);
  }

  TEST_CASE("histo: `dump` does not disturb the histogram (#458)") {
    HistoRig rig;
    rig.Send(3);
    rig.Send(3);
    rig.Message("dump");
    CHECK(rig.histo.CountOf(3) == 2);
    CHECK(rig.histo.Total() == 2);
    CHECK(rig.Bang() == std::string("3 2"));
  }

  // ─── overflow ───────────────────────────────────────────────────────────────

  TEST_CASE("histo: a bin saturates rather than wrapping (#458)") {
    // A wrapped bin turns the most-seen value into the least-seen one, which is
    // the single worst answer a histogram can give. MAX_COUNT is 2^30, so the
    // ceiling is asserted on the arithmetic the bins climb by rather than by
    // sending a billion messages.
    using YSE::PATCHER::gHisto;
    constexpr int MAX = gHisto::MAX_COUNT;

    CHECK(gHisto::AddSaturating(0, 1) == 1);
    CHECK(gHisto::AddSaturating(41, 1) == 42);
    CHECK(gHisto::AddSaturating(MAX - 2, 1) == MAX - 1);
    CHECK(gHisto::AddSaturating(MAX - 1, 1) == MAX);
    // At the ceiling it stops, and it stops there rather than going negative —
    // which is what an int add would eventually do.
    CHECK(gHisto::AddSaturating(MAX, 1) == MAX);
    CHECK(gHisto::AddSaturating(MAX, MAX) == MAX);
    CHECK(gHisto::AddSaturating(0x7FFFFFFF, 1) == MAX);
    CHECK(gHisto::AddSaturating(MAX, 1) > 0);
  }

  TEST_CASE("histo: a bin that climbs normally is not affected by the ceiling (#458)") {
    // The other side of the same boundary: ordinary counting is exact.
    HistoRig rig;
    for (int i = 0; i < 1000; ++i)
      rig.Send(11);
    CHECK(rig.histo.CountOf(11) == 1000);
    CHECK(rig.histo.Total() == 1000);
  }

  // ─── GUI value ──────────────────────────────────────────────────────────────

  TEST_CASE("histo: the GUI value follows the size of the sample (#458)") {
    HistoRig rig("16");
    CHECK(rig.histo.GetGuiValue() == std::string("0"));
    rig.Send(1);
    CHECK(rig.histo.GetGuiValue() == std::string("1"));
    rig.Send(1);
    CHECK(rig.histo.GetGuiValue() == std::string("2"));
    rig.Send(2);
    CHECK(rig.histo.GetGuiValue() == std::string("3"));
    rig.Send(900); // refused, so it is not part of the sample
    CHECK(rig.histo.GetGuiValue() == std::string("3"));
    rig.Query(1); // read, not counted
    CHECK(rig.histo.GetGuiValue() == std::string("3"));
    rig.Message("clear");
    CHECK(rig.histo.GetGuiValue() == std::string("0"));
  }

  // ─── a stream, end to end ───────────────────────────────────────────────────

  TEST_CASE("histo: a whole stream leaves the distribution it had (#458)") {
    // The object's actual job: after a phrase has been played through it, the
    // bins hold the phrase's value distribution and the dump reads it back.
    YSE::PATCHER::gHisto histo;
    LineCollector collector;
    histo.ConnectOutlet(collector.GetInlet(0), 0);
    collector.ConnectInlet(histo.GetOutlet(0), 0);

    const int phrase[] = {60, 62, 64, 62, 60, 62, 64, 65, 64, 62};
    constexpr int REPEATS = 40;
    for (int repeat = 0; repeat < REPEATS; ++repeat) {
      for (int note : phrase)
        histo.GetInlet(0)->SetInt(note, YSE::T_GUI);
    }

    // 60 twice per repeat, 62 four times, 64 three times, 65 once.
    CHECK(histo.CountOf(60) == 2 * REPEATS);
    CHECK(histo.CountOf(62) == 4 * REPEATS);
    CHECK(histo.CountOf(64) == 3 * REPEATS);
    CHECK(histo.CountOf(65) == 1 * REPEATS);
    CHECK(histo.CountOf(61) == 0);
    CHECK(histo.Total() == REPEATS * static_cast<int>(sizeof(phrase) / sizeof(phrase[0])));

    collector.lines.clear();
    histo.GetInlet(0)->SetList("dump", YSE::T_GUI);
    REQUIRE(collector.lines.size() == 4u);
    CHECK(collector.lines[0] == "60 " + std::to_string(2 * REPEATS));
    CHECK(collector.lines[1] == "62 " + std::to_string(4 * REPEATS));
    CHECK(collector.lines[2] == "64 " + std::to_string(3 * REPEATS));
    CHECK(collector.lines[3] == "65 " + std::to_string(1 * REPEATS));
  }

} // TEST_SUITE
