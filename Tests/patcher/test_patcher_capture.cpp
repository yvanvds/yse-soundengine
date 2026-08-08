// Tests for .capture (issue #496) — the patcher's rolling record of the values
// that went past.
//
// Six things are worth pinning here, and every one of them is something an
// implementation can get wrong without ever crashing:
//
//   - **one item is one atom.** Max's list method stores "all numbers and/or
//     symbols in the list ... in order from first to last", so a three-element
//     list is *three* items and a dump hands them back one at a time. An object
//     that stored the list whole would pass every count-based test and produce a
//     useless trace.
//   - **the record wraps, it does not refuse.** Max: "once the maximum has been
//     exceeded, the earliest stored item is dropped as each new item is
//     received." This is the opposite of .coll and .bag, which refuse an insert
//     past their capacity, so the wrap is asserted rather than assumed.
//   - **`count` counts arrivals, not contents.** Max's "the number of items
//     collected since the last count message", reset on receipt "unless flag is
//     set". It is a since-you-last-asked meter, so it can report far more than
//     the object is holding — the exact thing a fill-level implementation gets
//     wrong and no small test would notice.
//   - **each item leaves as the kind it arrived as.** An int as an int, a float
//     as a float, a symbol as a one-element list. A trace that rewrote the type
//     of what it saw would be lying about the bug being hunted.
//   - **the six reserved words.** `clear`, `dump` and `count` do their jobs;
//     `open`, `wclose` and `write` are consumed and do nothing. All six are
//     consumed rather than *recorded*, because Max dispatches on the selector
//     and so cannot store them either — and a trace that differed from Max's for
//     the same patch is the one thing this object must not produce.
//   - **the capacity is a parameter and the contents are not.** The capacity
//     survives a save; the trace deliberately does not, which is where this
//     object sides with .bag against .coll.
//
// No audio device and no engine of its own, except where a real patcher graph is
// the point.

#include <doctest/doctest.h>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "patcher/genericObjects/gCapture.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::gCapture;

namespace {

  // Records every value it receives, in order and with its kind. MultiSink only
  // keeps the last of each kind, which cannot tell a dump of three items from a
  // dump of one — and both the order and the kinds are half of what this object
  // promises.
  struct Recorder : YSE::PATCHER::pObject {
    // "i60", "f60.5", "sfoo", "!" for a bang — one string per send, so a whole
    // dump reads back as the exact sequence it was sent in.
    std::vector<std::string> seen;

    Recorder() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { seen.emplace_back("!"); });
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { seen.push_back("i" + std::to_string(v)); });
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
        // One decimal is enough to tell 60.5 from 60 and keeps the expected
        // strings readable; nothing here is tested at finer resolution.
        char text[32];
        std::snprintf(text, sizeof(text), "f%.1f", (double)v);
        seen.emplace_back(text);
      });
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { seen.push_back("s" + v); });
    }
    const char* Type() const override {
      return "recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    void reset() {
      seen.clear();
    }
  };

  // A standalone .capture with a recorder on each outlet. Standalone where the
  // patcher is not the point: a test that needed one could not tell a refused
  // message from a message the patcher never delivered.
  struct Rig {
    Recorder dump;
    Recorder count;
    gCapture obj;

    Rig() {
      obj.ConnectOutlet(dump.GetInlet(0), 0);
      obj.ConnectOutlet(count.GetInlet(0), 1);
    }

    void List(const std::string& message) {
      obj.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Int(int value) {
      obj.GetInlet(0)->SetInt(value, YSE::T_GUI);
    }
    void Float(float value) {
      obj.GetInlet(0)->SetFloat(value, YSE::T_GUI);
    }
    void Bang() {
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
    }

    // What a `dump` sends, as the sequence of tagged strings above.
    std::vector<std::string> Dumped() {
      dump.reset();
      List("dump");
      return dump.seen;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("capture: registered, one inlet and two outlets (#496)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::G_CAPTURE);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == ".capture");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 2);
  }

  TEST_CASE("capture: appears in the registry's name list (#496)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_CAPTURE)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("capture: the inlet takes int, float and list but not bang (#496)") {
    gCapture obj;
    const unsigned int accepted = obj.GetInlet(0)->GetAcceptedTypes();
    CHECK((accepted & YSE::PATCHER::IT_INT) != 0);
    CHECK((accepted & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((accepted & YSE::PATCHER::IT_LIST) != 0);
    // Max documents no bang method for capture, and one that dumped would give
    // the object two spellings of `dump` on the same inlet.
    CHECK((accepted & YSE::PATCHER::IT_BANG) == 0);
  }

  TEST_CASE("capture: a fresh record is empty and a dump sends nothing (#496)") {
    Rig rig;
    CHECK(rig.obj.Count() == 0);
    CHECK(rig.Dumped().empty());
  }

  TEST_CASE("capture: Calculate() sends nothing (#496)") {
    // The rule .route, .sel, .value, .bucket, .coll and .bag establish: an object
    // driven by its inlet must not re-emit on every DSP tick.
    Rig rig;
    rig.Int(60);
    rig.dump.reset();
    rig.obj.Calculate(YSE::T_DSP);
    rig.obj.Calculate(YSE::T_DSP);
    CHECK(rig.dump.seen.empty());
    CHECK(rig.count.seen.empty());
  }

  TEST_CASE("capture: a bang does nothing at all (#496)") {
    Rig rig;
    rig.Int(60);
    rig.dump.reset();
    rig.Bang();
    CHECK(rig.dump.seen.empty());
    CHECK(rig.obj.Count() == 1);
  }

  // ─── recording ──────────────────────────────────────────────────────────────

  TEST_CASE("capture: recording sends nothing (#496)") {
    Rig rig;
    rig.Int(60);
    rig.Float(60.5f);
    rig.List("foo 1 2");
    CHECK(rig.dump.seen.empty());
    CHECK(rig.count.seen.empty());
  }

  TEST_CASE("capture: one item is one atom, so a list is stored element by element (#496)") {
    // Max's list method: "all numbers and/or symbols in the list are stored in
    // order from first to last". An object that stored the list whole would pass
    // every count-based test and produce an unreadable trace.
    Rig rig;
    rig.List("60 100 note");
    CHECK(rig.obj.Count() == 3);
    CHECK(rig.obj.NumberAt(0) == doctest::Approx(60.f));
    CHECK(rig.obj.NumberAt(1) == doctest::Approx(100.f));
    CHECK(rig.obj.IsSymbolAt(2));
    CHECK(rig.obj.SymbolAt(2) == "note");
  }

  TEST_CASE("capture: each item leaves as the kind it arrived as (#496)") {
    Rig rig;
    rig.Int(60);
    rig.Float(60.5f);
    rig.List("foo");

    const std::vector<std::string> out = rig.Dumped();
    REQUIRE(out.size() == 3);
    CHECK(out[0] == "i60");
    CHECK(out[1] == "f60.5");
    // A one-element list: the patcher has no symbol atom, and Max's `symbol`
    // prefix would put a token in the trace that nothing put into it.
    CHECK(out[2] == "sfoo");
  }

  TEST_CASE("capture: an int spelled as a float stays a float through the trace (#496)") {
    // The spelling travels with the value, the test .trigger, .match, .route and
    // .coll already share: a trace that rewrote `60.` as `60` would be lying
    // about the type of the thing being hunted.
    Rig rig;
    rig.List("60 60.");
    const std::vector<std::string> out = rig.Dumped();
    REQUIRE(out.size() == 2);
    CHECK(out[0] == "i60");
    CHECK(out[1] == "f60.0");
  }

  TEST_CASE("capture: a dump sends the record oldest first (#496)") {
    Rig rig;
    rig.Int(1);
    rig.Int(2);
    rig.Int(3);
    const std::vector<std::string> out = rig.Dumped();
    REQUIRE(out.size() == 3);
    CHECK(out[0] == "i1");
    CHECK(out[1] == "i2");
    CHECK(out[2] == "i3");
  }

  TEST_CASE("capture: a dump leaves the record standing, so it can be read twice (#496)") {
    Rig rig;
    rig.Int(1);
    rig.Int(2);
    CHECK(rig.Dumped().size() == 2);
    CHECK(rig.Dumped().size() == 2);
    CHECK(rig.obj.Count() == 2);
  }

  TEST_CASE("capture: a symbol longer than the item capacity is refused whole (#496)") {
    // Refused rather than truncated: half a symbol is a different symbol, and a
    // trace that quietly rewrote what it saw would be worse than one with a hole.
    Rig rig;
    const std::string tooLong(gCapture::ITEM_CAPACITY + 1, 'x');
    const std::string atLimit(gCapture::ITEM_CAPACITY, 'y');
    rig.List(tooLong);
    CHECK(rig.obj.Count() == 0);
    rig.List(atLimit);
    REQUIRE(rig.obj.Count() == 1);
    CHECK(rig.obj.SymbolAt(0) == atLimit);
  }

  // ─── the ring ───────────────────────────────────────────────────────────────

  TEST_CASE("capture: past the capacity the oldest item is dropped, not the newest (#496)") {
    // Max: "once the maximum has been exceeded, the earliest stored item is
    // dropped as each new item is received." The opposite of .coll and .bag,
    // which refuse an insert past their capacity.
    Rig rig;
    rig.obj.SetParams("3");
    REQUIRE(rig.obj.Capacity() == 3);

    for (int i = 1; i <= 5; i++)
      rig.Int(i);
    CHECK(rig.obj.Count() == 3);

    const std::vector<std::string> out = rig.Dumped();
    REQUIRE(out.size() == 3);
    CHECK(out[0] == "i3");
    CHECK(out[1] == "i4");
    CHECK(out[2] == "i5");
  }

  TEST_CASE("capture: the ring keeps wrapping past a whole lap of the table (#496)") {
    // A ring whose read index is computed rather than tracked breaks on the
    // *second* lap, not the first, so one lap is not enough to pin it.
    Rig rig;
    rig.obj.SetParams("4");
    for (int i = 0; i < 30; i++)
      rig.Int(i);

    const std::vector<std::string> out = rig.Dumped();
    REQUIRE(out.size() == 4);
    CHECK(out[0] == "i26");
    CHECK(out[3] == "i29");
  }

  TEST_CASE("capture: with no argument it holds Max's 512 items (#496)") {
    gCapture obj;
    CHECK(obj.Capacity() == gCapture::DEFAULT_ITEMS);
    CHECK(gCapture::DEFAULT_ITEMS == 512);

    for (std::size_t i = 0; i < gCapture::MAX_ITEMS + 10; i++)
      obj.GetInlet(0)->SetInt((int)i, YSE::T_GUI);
    CHECK(obj.Count() == gCapture::MAX_ITEMS);
    // Wrapped rather than refused: the oldest ten are the ones that went.
    CHECK(obj.NumberAt(0) == doctest::Approx(10.f));
  }

  TEST_CASE("capture: the capacity argument is clamped rather than honoured (#496)") {
    gCapture obj;
    obj.SetParams("100000");
    CHECK(obj.Capacity() == gCapture::MAX_ITEMS);

    // A zero-item record would silently discard everything, so 1 is the floor.
    obj.SetParams("0");
    CHECK(obj.Capacity() == gCapture::MIN_ITEMS);
    obj.SetParams("-5");
    CHECK(obj.Capacity() == gCapture::MIN_ITEMS);
  }

  TEST_CASE("capture: clearing the argument returns it to Max's default (#496)") {
    gCapture obj;
    obj.SetParams("8");
    REQUIRE(obj.Capacity() == 8);
    obj.SetParams("");
    CHECK(obj.Capacity() == gCapture::DEFAULT_ITEMS);
  }

  TEST_CASE("capture: Max's display-format argument is accepted and ignored (#496)") {
    // Max's second argument chooses how the editing window draws numbers; there
    // is no window here. Accepted rather than refused so that a `.capture 512 x`
    // brought across from Max still builds the object it names.
    gCapture obj;
    obj.SetParams("16 x");
    CHECK(obj.Capacity() == 16);

    // A leading non-number is not a capacity either, and leaves the default.
    gCapture other;
    other.SetParams("a");
    CHECK(other.Capacity() == gCapture::DEFAULT_ITEMS);
  }

  // ─── the commands ───────────────────────────────────────────────────────────

  TEST_CASE("capture: clear erases the contents silently (#496)") {
    Rig rig;
    rig.Int(60);
    rig.Int(62);
    rig.List("clear");
    CHECK(rig.dump.seen.empty());
    CHECK(rig.obj.Count() == 0);
    CHECK(rig.Dumped().empty());
  }

  TEST_CASE("capture: count reports arrivals since the last count and zeroes the meter (#496)") {
    // Max: "sends the number of items collected since the last count message out
    // the right outlet ... upon receipt of the count message, the object's
    // internal count will be reset to 0 unless flag is set."
    Rig rig;
    rig.Int(1);
    rig.List("2 3 4"); // three atoms, not one
    CHECK(rig.obj.Received() == 4);

    rig.List("count");
    REQUIRE(rig.count.seen.size() == 1);
    CHECK(rig.count.seen[0] == "i4");
    CHECK(rig.obj.Received() == 0);
    // Nothing left the dump outlet: count is not a dump.
    CHECK(rig.dump.seen.empty());

    rig.count.reset();
    rig.Int(9);
    rig.List("count");
    REQUIRE(rig.count.seen.size() == 1);
    CHECK(rig.count.seen[0] == "i1");
  }

  TEST_CASE("capture: count with a non-zero flag reads the meter without zeroing it (#496)") {
    Rig rig;
    rig.Int(1);
    rig.Int(2);

    rig.List("count 1");
    REQUIRE(rig.count.seen.size() == 1);
    CHECK(rig.count.seen[0] == "i2");
    CHECK(rig.obj.Received() == 2);

    // A zero flag is Max's "unless flag is set" not being met, so it resets.
    rig.count.reset();
    rig.List("count 0");
    REQUIRE(rig.count.seen.size() == 1);
    CHECK(rig.count.seen[0] == "i2");
    CHECK(rig.obj.Received() == 0);
  }

  TEST_CASE("capture: count is an arrival meter, so it outruns the contents (#496)") {
    // The distinction the object's name invites getting wrong: `count` is not a
    // fill level, and it counts the items the ring has already dropped.
    Rig rig;
    rig.obj.SetParams("2");
    for (int i = 0; i < 10; i++)
      rig.Int(i);

    CHECK(rig.obj.Count() == 2);
    rig.List("count");
    REQUIRE(rig.count.seen.size() == 1);
    CHECK(rig.count.seen[0] == "i10");
  }

  TEST_CASE("capture: clear erases the contents but leaves the arrival meter standing (#496)") {
    // Max documents `clear` as erasing the contents and says nothing about the
    // counter; the two answer different questions.
    Rig rig;
    rig.Int(1);
    rig.Int(2);
    rig.List("clear");
    CHECK(rig.obj.Count() == 0);
    CHECK(rig.obj.Received() == 2);
  }

  TEST_CASE("capture: open, wclose and write are consumed rather than recorded (#496)") {
    // Max dispatches on the selector, so a `capture` in Max cannot store these
    // symbols either — and a trace that differed from Max's for the same patch is
    // the one thing a debugging instrument must not produce. There is no window
    // here and no file I/O on a path that may be the audio thread, so they do
    // nothing at all.
    Rig rig;
    rig.List("open");
    rig.List("wclose");
    rig.List("write trace.txt");
    CHECK(rig.obj.Count() == 0);
    CHECK(rig.obj.Received() == 0);
    CHECK(rig.dump.seen.empty());
    CHECK(rig.count.seen.empty());
  }

  TEST_CASE("capture: a message whose first word is a command word is never recorded (#496)") {
    // The mirror of the case above, for the three that do work: `dump` in the
    // record would make the trace unreadable exactly when it is being read.
    Rig rig;
    rig.List("clear 1 2");
    rig.List("dump");
    rig.List("count 1");
    CHECK(rig.obj.Count() == 0);
  }

  TEST_CASE("capture: a command word after the first item is ordinary data (#496)") {
    // Only the leading token is a selector, in Max as here.
    Rig rig;
    rig.List("60 dump");
    REQUIRE(rig.obj.Count() == 2);
    CHECK(rig.obj.NumberAt(0) == doctest::Approx(60.f));
    CHECK(rig.obj.IsSymbolAt(1));
    CHECK(rig.obj.SymbolAt(1) == "dump");
  }

  // ─── what a save carries ────────────────────────────────────────────────────

  TEST_CASE("capture: the capacity survives a DumpJSON / ParseJSON round trip (#496)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_CAPTURE, "3") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".capture") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".capture");

    Recorder out;
    YSE::pHandle outHandle(&out);
    loaded.Connect(copy, 0, &outHandle, 0);

    // Read the capacity back through the object's own behaviour rather than an
    // accessor: what has to survive is the record a patch can use.
    for (int i = 1; i <= 5; i++)
      copy->SetIntData(0, i);
    copy->SetListData(0, "dump");
    REQUIRE(out.seen.size() == 3);
    CHECK(out.seen[0] == "i3");
    CHECK(out.seen[2] == "i5");
  }

  TEST_CASE("capture: the contents deliberately do not survive a save (#496)") {
    // Max gives `coll` a "save data with patcher" flag — the feature that made
    // pObject grow a state hook — and gives `capture` none: its `write` message
    // is how a capture's contents are saved, to a text file, on demand. A trace
    // is the record of a run, so a reloaded patch holding the values from the
    // session it was saved in would be answering a question nobody had asked.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* cap = src.CreateObject(YSE::OBJ::G_CAPTURE);
    REQUIRE(cap != nullptr);
    cap->SetIntData(0, 60);
    cap->SetListData(0, "62 64");

    const std::string json = src.DumpJSON();
    CHECK(json.find("\"state\"") == std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);

    Recorder out;
    MultiSink meter;
    YSE::pHandle outHandle(&out);
    YSE::pHandle meterHandle(&meter);
    loaded.Connect(copy, 0, &outHandle, 0);
    loaded.Connect(copy, 1, &meterHandle, 0);

    copy->SetListData(0, "dump");
    CHECK(out.seen.empty());
    // The arrival meter is contents too, and starts a reloaded patch at zero.
    copy->SetListData(0, "count");
    CHECK(meter.gotInt);
    CHECK(meter.intValue == 0);
  }

  // ─── end to end, through a real patcher graph ───────────────────────────────

  TEST_CASE("capture: records what a real graph actually sent, and hands it back (#496)") {
    // The use case the object exists for, run through a real graph rather than a
    // directly wired outlet: a .capture sits on the wire between two objects,
    // the patch is driven, and only afterwards is the object asked what went
    // past. What is pinned is that the trace is the *transformed* values in
    // arrival order — the question a patcher debugging session actually asks.
    Recorder trace;
    YSE::pHandle traceHandle(&trace);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::G_ADD, "12");
    YSE::pHandle* cap = p.CreateObject(YSE::OBJ::G_CAPTURE, "4");
    REQUIRE(add != nullptr);
    REQUIRE(cap != nullptr);
    p.Connect(add, 0, cap, 0);
    p.Connect(cap, 0, &traceHandle, 0);

    // Five notes through the transposer, into a four-item record.
    for (int note = 60; note < 65; note++)
      add->SetIntData(0, note);

    // Nothing has left the capture yet: recording is silent.
    CHECK(trace.seen.empty());

    cap->SetListData(0, "dump");
    REQUIRE(trace.seen.size() == 4);
    // `.+` answers in floats whatever it was given, so the trace carries what
    // the wire carried — which is the point. The oldest of the five is gone.
    CHECK(trace.seen[0] == "f73.0");
    CHECK(trace.seen[3] == "f76.0");
  }

  TEST_CASE("capture: a .trigger drives count and dump round a real graph (#496)") {
    // The two questions asked in one gesture, with the messages arriving the way
    // a patch would send them: .trigger fires right to left, so `count` reaches
    // the capture before `dump` does.
    Recorder items;
    Recorder meter;
    YSE::pHandle itemsHandle(&items);
    YSE::pHandle meterHandle(&meter);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* cap = p.CreateObject(YSE::OBJ::G_CAPTURE, "2");
    YSE::pHandle* trig = p.CreateObject(YSE::OBJ::G_TRIGGER, "dump count");
    REQUIRE(cap != nullptr);
    REQUIRE(trig != nullptr);
    p.Connect(trig, 0, cap, 0);
    p.Connect(trig, 1, cap, 0);
    p.Connect(cap, 0, &itemsHandle, 0);
    p.Connect(cap, 1, &meterHandle, 0);

    cap->SetListData(0, "10 20 30");

    trig->SetBang(0);
    // `count` first: three atoms arrived even though only two are held.
    REQUIRE(meter.seen.size() == 1);
    CHECK(meter.seen[0] == "i3");
    // then `dump`: the two survivors, oldest first.
    REQUIRE(items.seen.size() == 2);
    CHECK(items.seen[0] == "i20");
    CHECK(items.seen[1] == "i30");
  }

} // TEST_SUITE
