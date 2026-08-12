// Tests for `.print` — Max's print, which prints "any message in the Max
// window" (issue #546). There is no Max window here, so the destination is the
// engine log.
//
// The trap this object sets is not in its grammar — it prints what it is given,
// and that is the whole of it — but in *where it runs*. A message handler runs
// on whichever thread dispatched the message, and in-patcher delivery
// dispatches on `T_DSP`, so a `.print` in a patch is routinely being driven by
// the audio callback. Every obvious implementation of "write this to the log"
// allocates a std::string and then writes a file or calls into host code, which
// is precisely what the audio thread may not do. An implementation that did the
// obvious thing would pass every functional test in this file and glitch the
// audio of the patch it was added to debug.
//
// So the layers here are:
//
//   - **the log, end to end** — the assertion that matters to a user is not
//     "the object accepted the message" but "the line came out of the engine
//     log", so the cases that check what a message prints install a real
//     `YSE::logHandler` through the public `YSE::Log()` API and read what
//     arrives there, exactly as a host would.
//
//   - **creation arguments** — Max's name, and the per-tick budget that is this
//     patcher's and not Max's. Including the arguments that are wrong, since a
//     creation argument arriving from a saved patch must never be able to break
//     loading it.
//
//   - **flood behaviour** — both bounds, and that each one *reports* rather than
//     silently swallowing. A debugging instrument that lies about the patch it
//     is debugging is worse than no instrument.
//
//   - **end-to-end, in a real patcher** — a real graph, real cords, and the
//     object created the way a user creates it.
//
//   - **real-time discipline** — the message paths measured for allocation on
//     the thread that runs them.
//
// No audio device required.

#include <doctest/doctest.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "internal/rtLogQueue.h"
#include "log.hpp"
#include "patcher/genericObjects/gPrint.h"
#include "patcher/inlet.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "support/alloc_probe.hpp"

using YSE::PATCHER::gPrint;
using YSE::PATCHER::Register;

namespace {

  // The engine log, watched the way a host watches it: a real logHandler
  // installed through the public API. Everything the engine logs while this is
  // alive arrives here, the object under test's lines among them, so the
  // accessors search rather than compare — the surrounding debug chatter from
  // `Parameters::Set` is part of an honest reading of this channel.
  struct LogCapture : YSE::logHandler {
    std::vector<std::string> lines;

    LogCapture() {
      lines.reserve(4096);
      // Whatever is still queued from an earlier test belongs to that test.
      YSE::INTERNAL::RtLog().drain();
      YSE::Log().setHandler(this);
    }
    ~LogCapture() override {
      YSE::Log().setHandler(nullptr);
    }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;
    LogCapture(LogCapture&&) = delete;
    LogCapture& operator=(LogCapture&&) = delete;

    void AddMessage(const std::string& message) override {
      lines.push_back(message);
    }

    // Run the control-thread half of the object: this is what `system::update()`
    // does once per tick, and until it happens a printed line is still sitting
    // in the queue.
    std::size_t Drain() {
      return YSE::INTERNAL::RtLog().drain();
    }

    bool Saw(const std::string& needle) const {
      for (const std::string& line : lines) {
        if (line.find(needle) != std::string::npos) return true;
      }
      return false;
    }

    std::size_t Count(const std::string& needle) const {
      std::size_t found = 0;
      for (const std::string& line : lines) {
        if (line.find(needle) != std::string::npos) found++;
      }
      return found;
    }

    void Clear() {
      lines.clear();
    }
  };

  // A standalone `.print`, driven through its inlet the way a cord drives it.
  // The object needs no patcher and no clock, so standalone is the whole of its
  // message behaviour.
  struct Rig {
    gPrint obj;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) obj.SetParams(args);
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    Rig(Rig&&) = delete;
    Rig& operator=(Rig&&) = delete;

    void Bang() {
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
    }
    void Int(int value) {
      obj.GetInlet(0)->SetInt(value, YSE::T_GUI);
    }
    void Float(float value) {
      obj.GetInlet(0)->SetFloat(value, YSE::T_GUI);
    }
    void List(const std::string& value) {
      obj.GetInlet(0)->SetList(value, YSE::T_GUI);
    }
    void Message(const std::string& value) {
      obj.GetInlet(0)->SetMessage(value, YSE::T_GUI);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("print: it is registered and has Max's shape (#546)") {
    auto names = Register().AllNames();
    bool registered = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_PRINT)) registered = true;
    }
    CHECK(registered);

    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::G_PRINT));
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == ".print");

    // One inlet and no outlet, which is Max's shape. The missing outlet is not
    // an omission: an outlet would turn a probe into a participant, and a patch
    // that had to wire this object's output somewhere could no longer leave it
    // in place once it was working.
    CHECK(obj->NumInputs() == 1);
    CHECK(obj->NumOutputs() == 0);
  }

  // ─── creation arguments ─────────────────────────────────────────────────────

  TEST_CASE("print: the name is Max's first argument (#546)") {
    // Max: "the first argument sets the name that appears in the Max window
    // when the object prints something. The default name is print."
    {
      Rig rig;
      CHECK(rig.obj.Name() == "print");
      CHECK(rig.obj.LinesPerTick() == gPrint::DEFAULT_LINES_PER_TICK);
    }
    {
      Rig rig("bass");
      CHECK(rig.obj.Name() == "bass");
      CHECK(rig.obj.LinesPerTick() == gPrint::DEFAULT_LINES_PER_TICK);
    }
    {
      // A name longer than the cap is cut rather than refused: a name is a
      // label, and a label that was too long is still the label the author
      // meant. Refusing it would leave the line unlabelled, which is worse.
      Rig rig(std::string(gPrint::NAME_CAPACITY + 20, 'x'));
      CHECK(rig.obj.Name().size() == gPrint::NAME_CAPACITY);
    }
    {
      // `SetParams("")` has to leave Max's no-argument object behind rather
      // than one still holding the previous name — that is what the clear
      // callback is for.
      Rig rig("bass 4");
      REQUIRE(rig.obj.Name() == "bass");
      rig.obj.SetParams("");
      CHECK(rig.obj.Name() == "print");
      CHECK(rig.obj.LinesPerTick() == gPrint::DEFAULT_LINES_PER_TICK);
    }
  }

  TEST_CASE("print: the budget is the second argument, clamped (#546)") {
    {
      Rig rig("dbg 4");
      CHECK(rig.obj.LinesPerTick() == 4);
    }
    {
      // Zero is not offered: an object that printed nothing is a deleted
      // object, spelled confusingly.
      Rig rig("dbg 0");
      CHECK(rig.obj.LinesPerTick() == gPrint::MIN_LINES_PER_TICK);
    }
    {
      Rig rig("dbg -7");
      CHECK(rig.obj.LinesPerTick() == gPrint::MIN_LINES_PER_TICK);
    }
    {
      // Past the queue's own capacity there is nothing more to buy.
      Rig rig("dbg 99999");
      CHECK(rig.obj.LinesPerTick() == gPrint::MAX_LINES_PER_TICK);
    }
    {
      // A float is truncated, as everywhere else a creation argument is read as
      // a count.
      Rig rig("dbg 5.9");
      CHECK(rig.obj.LinesPerTick() == 5);
    }
    {
      // The case that must not throw. A second argument that is not a number
      // leaves the default standing, because a creation argument arriving from
      // a hand-edited or newer saved patch must never be able to break loading
      // it — and `Parameters::Set` would have thrown out of `std::stoi` had the
      // budget been registered as a plain int parameter.
      Rig rig("dbg wibble");
      CHECK(rig.obj.Name() == "dbg");
      CHECK(rig.obj.LinesPerTick() == gPrint::DEFAULT_LINES_PER_TICK);
    }
  }

  // ─── what reaches the log ───────────────────────────────────────────────────

  TEST_CASE("print: every message kind reaches the engine log (#546)") {
    LogCapture log;
    Rig rig("dbg");

    rig.Bang();
    rig.Int(60);
    rig.Float(60.5f);
    rig.List("60 100 wibble");
    rig.Message("hello world");

    // Nothing has reached the log yet: the object queued five lines and the
    // control thread has not been past. That delay is the design, not a defect
    // — the writing happens where a std::string and a file are affordable.
    CHECK_FALSE(log.Saw("dbg: 60"));

    CHECK(log.Drain() == 5);

    CHECK(log.Saw("dbg: bang")); // Max's print "prints the word bang"
    CHECK(log.Saw("dbg: 60"));
    CHECK(log.Saw("dbg: 60.5")); // a float stays visibly a float
    CHECK(log.Saw("dbg: 60 100 wibble")); // a list, verbatim
    CHECK(log.Saw("dbg: hello world")); // Max's `anything` method
  }

  TEST_CASE("print: the name is what tells two of them apart (#546)") {
    LogCapture log;
    Rig left("left");
    Rig right("right");

    left.Int(1);
    right.Int(2);
    log.Drain();

    CHECK(log.Saw("left: 1"));
    CHECK(log.Saw("right: 2"));
    CHECK_FALSE(log.Saw("left: 2"));
    CHECK_FALSE(log.Saw("right: 1"));
  }

  TEST_CASE("print: a line too long for the record is cut and marked (#546)") {
    LogCapture log;
    Rig rig("dbg");

    // Deliberately the opposite of `.capture`'s refuse-whole rule: `.capture`
    // stores a symbol, where half of one is a different symbol, while this
    // produces a line to read, where the first couple of hundred characters are
    // almost all of the information and silence is none of it.
    const std::string huge(YSE::INTERNAL::rtLogQueue::kLineCapacity * 2, 'z');
    rig.List(huge);
    CHECK(log.Drain() == 1);

    CHECK(log.Saw("dbg: zzz"));
    CHECK(log.Saw("...")); // the cut is marked rather than silent

    for (const std::string& line : log.lines) {
      // The record's bound really is a bound. The log line carries the engine's
      // own "(App Message)" tag in front of it, so it is longer than the record
      // by that tag and no more.
      CHECK(line.size() <= YSE::INTERNAL::rtLogQueue::kLineCapacity + 32);
    }
  }

  // ─── flooding ───────────────────────────────────────────────────────────────

  TEST_CASE("print: the budget refuses past its limit, and says so (#546)") {
    LogCapture log;
    Rig rig("dbg 2");

    rig.Int(1);
    rig.Int(2);
    rig.Int(3);
    rig.Int(4);
    rig.Int(5);

    // Two lines got through, and the drops are counted rather than forgotten.
    CHECK(rig.obj.Dropped() == 3);

    log.Drain();
    CHECK(log.Saw("dbg: 1"));
    CHECK(log.Saw("dbg: 2"));
    CHECK_FALSE(log.Saw("dbg: 3"));

    // ... and the gap is *marked*. A rate-limited `.print` that simply went
    // quiet would read as a working object that had stopped seeing messages,
    // which is the one thing a debugging instrument must never look like.
    CHECK(log.Saw("dbg: rate limit reached (2 per tick)"));

    // One notice for the whole burst, not one per refused message.
    CHECK(log.Count("rate limit reached") == 1);
  }

  TEST_CASE("print: a drain tick refills the budget (#546)") {
    LogCapture log;
    Rig rig("dbg 1");

    rig.Int(1);
    rig.Int(2); // refused: this tick's one line is spent
    log.Drain();
    CHECK(log.Saw("dbg: 1"));
    CHECK_FALSE(log.Saw("dbg: 2"));

    log.Clear();

    // The drain has been past, so the budget is whole again. This is the whole
    // of the object's clock: it has none of its own, and asking the queue "has
    // the control thread been past since I last looked?" costs one relaxed
    // load on a path that must stay cheap.
    rig.Int(3);
    log.Drain();
    CHECK(log.Saw("dbg: 3"));
  }

  TEST_CASE("print: a full queue is refused and the loss is reported (#546)") {
    LogCapture log;
    // The queue is shared by every producer in the process, so it takes two
    // objects to reach it: one object's budget tops out at exactly the queue's
    // capacity, which is the point of that clamp — a single `.print` can spend
    // the whole queue but cannot overrun it. Two of them can, and that is the
    // case a patch actually produces.
    Rig loud("loud 256");
    Rig louder("louder 256");

    const std::size_t capacity = YSE::INTERNAL::RtLog().capacity();
    const std::uint64_t before = YSE::INTERNAL::RtLog().dropped();

    for (std::size_t i = 0; i < capacity - 8; i++) {
      loud.Int((int)i);
      louder.Int((int)i);
    }

    // Refused rather than blocked or grown: those are the three things the
    // audio thread may not do, and a log call is never worth any of them.
    CHECK(YSE::INTERNAL::RtLog().dropped() > before);
    CHECK(louder.obj.Dropped() > 0);
    CHECK(loud.obj.Posted() + louder.obj.Posted() == capacity);

    CHECK(log.Drain() == capacity);
    // Named, not silent — one line for the whole burst.
    CHECK(log.Saw("real-time log queue overflow"));
  }

  // ─── end to end, in a real patcher ──────────────────────────────────────────

  TEST_CASE("print: a real patch's values reach the engine log (#546)") {
    // The object as a user meets it: created by name in a real patcher, wired
    // with real cords behind real objects, and read out of the engine log
    // through the public handler API. Everything above drives the inlet
    // directly; only this says the whole thing works.
    LogCapture log;

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* box = p.CreateObject(YSE::OBJ::G_INT, "0");
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::G_ADD, "5");
    YSE::pHandle* out = p.CreateObject(YSE::OBJ::G_PRINT, "sum");
    YSE::pHandle* msg = p.CreateObject(YSE::OBJ::G_MESSAGE, "hello");
    YSE::pHandle* said = p.CreateObject(YSE::OBJ::G_PRINT, "said");
    REQUIRE(box != nullptr);
    REQUIRE(add != nullptr);
    REQUIRE(out != nullptr);
    REQUIRE(msg != nullptr);
    REQUIRE(said != nullptr);

    p.Connect(box, 0, add, 0);
    p.Connect(add, 0, out, 0);
    p.Connect(msg, 0, said, 0);

    box->SetIntData(0, 42);
    msg->SetBang(0);

    log.Drain();

    // The number the patch computed, named by the object that printed it —
    // spelled with a decimal point, because `.+` sends a float and a float
    // that came back as `47` would have lost the type the cord was carrying.
    CHECK(log.Saw("sum: 47."));
    // ... and a message box's text, which travels by `SendMessage` rather than
    // as a list and would be silently invisible if only the list handler were
    // wired up.
    CHECK(log.Saw("said: hello"));
  }

  TEST_CASE("print: it survives a DumpJSON / ParseJSON round trip (#546)") {
    // Both creation arguments have to come back: a reloaded patch whose `.print`
    // lost its name would relabel every line it prints, and one that lost its
    // budget would flood where the author had asked it not to.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::G_PRINT, "bass 4");
    REQUIRE(obj != nullptr);

    const std::string json = src.DumpJSON();
    CHECK(json.find(".print") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".print");
    CHECK(copy->GetParams() == "bass 4");

    // Not merely stored verbatim — parsed back into the object that was saved.
    LogCapture log;
    copy->SetIntData(0, 9);
    log.Drain();
    CHECK(log.Saw("bass: 9"));
  }

  // ─── real-time discipline ───────────────────────────────────────────────────

  TEST_CASE("print: the message paths allocate nothing (#546)") {
    // The whole reason this object is built the way it is. An in-patcher
    // dispatch runs on `T_DSP`, so every one of these paths is audio-thread
    // code — including the refusals, which are the paths a flood spends all its
    // time on.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    LogCapture log;
    Rig rig("dbg 4");

    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message. The long one measures the
    // truncating branch, which is the one that could most easily have reached
    // for a std::string.
    const std::string list = "60 100 wibble";
    const std::string message = "hello world";
    const std::string huge(YSE::INTERNAL::rtLogQueue::kLineCapacity * 2, 'z');
    {
      TestHelpers::ProbeScope probe;
      rig.Bang();
      rig.Int(60);
      rig.Float(60.5f);
      rig.List(list);
      rig.Message(message); // the fifth message: past the budget of four
      rig.List(huge); // refused too — and the notice it posts is on this path
      rig.Int(7);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    // The probed messages really did something — an assertion that only proves
    // nothing happened proves nothing.
    CHECK(rig.obj.Posted() > 0);
    CHECK(rig.obj.Dropped() > 0);
    log.Drain();
  }
}
