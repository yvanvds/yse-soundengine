// Tests for `.iter` (issue #521) — the object that sends a list's items out
// one at a time down a single cord.
//
// What is being pinned:
//
//   - **the shape**: one inlet, one outlet, no creation arguments (Max
//     documents none);
//   - **the order**: items leave first to last, and each send completes in
//     full — the whole subgraph behind the outlet — before the next item is
//     sent, which is what makes the object a serialiser rather than a scatter;
//   - **the state**: `bang` re-sends the message most recently received, and a
//     bang before anything has arrived sends nothing at all;
//   - **the inherited transport**: an item leaves as the int, float or symbol
//     it spells rather than as a list of one, which is the whole point of the
//     object;
//   - **the inherited bound**: an over-long list loses its *tail*, which is
//     counted — `.zl`'s rule, because the surplus here is input rather than
//     state — so one stimulus costs at most 256 sends;
//   - **the guard across the walk**: a cord from the outlet back to the inlet
//     re-enters the handler from inside the walk, and is refused and counted
//     rather than restarting an iteration that would never terminate.
//
// The unit-level cases drive standalone objects, which is what this object
// needs (no patcher, no clock, no scheduler). The end-to-end section at the
// bottom drives a real `YSE::patcher` graph through `pHandle`, so the claim
// that the items reach ordinary scalar objects down real cords is tested at
// the level a patch actually sees.
//
// No audio device required.

#include <doctest/doctest.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "patcher/genericObjects/gIter.h"
#include "patcher/inlet.h"
#include "patcher/pAtomList.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::MultiSink;
using TestHelpers::OrderSink;
using YSE::PATCHER::AtomList;
using YSE::PATCHER::gIter;

namespace {

  // Records every message that arrived, in arrival order and with its kind, so
  // "one message per item, in this order, of these types" is an assertion
  // rather than an inference. `OrderSink` keeps only the last of each kind,
  // which is exactly what an iterating object needs a test *not* to do.
  struct TallySink : YSE::PATCHER::pObject {
    std::vector<std::string> got; // "i:5", "f:1.5", "l:a b", "bang"

    TallySink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { got.push_back("bang"); });
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { got.push_back("i:" + std::to_string(v)); });
      inputs.back().RegisterFloat(
          [this](float v, int, YSE::THREAD) { got.push_back("f:" + std::to_string(v)); });
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { got.push_back("l:" + v); });
      got.reserve(1024);
    }
    const char* Type() const override {
      return "tally_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    void reset() {
      got.clear();
    }
  };

  // Logs a bracket around its own forward, and shares the log with the sink
  // behind it, so a depth-first send reads back as "(*)(*)(*)" and a queued one
  // could not. The only way to test the "each send completes in full before the
  // next item leaves" claim, which a count-only or last-value-only sink cannot
  // see.
  struct Relay : YSE::PATCHER::pObject {
    std::vector<char>* log = nullptr;

    Relay() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int, int, YSE::THREAD t) { Pass(t); });
      inputs.back().RegisterFloat([this](float, int, YSE::THREAD t) { Pass(t); });
      inputs.back().RegisterList([this](const std::string&, int, YSE::THREAD t) { Pass(t); });
      outputs.emplace_back(this, YSE::OUT_TYPE::ANY);
    }
    const char* Type() const override {
      return "relay";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

  private:
    void Pass(YSE::THREAD thread) {
      if (log) log->push_back('(');
      outputs[0].SendBang(thread);
      if (log) log->push_back(')');
    }
  };

  // A `.iter` with one tally on its outlet. Sink first, so it outlives the
  // object that sends to it — sinks.hpp's teardown rule.
  struct Rig {
    TallySink sink;
    std::unique_ptr<gIter> op;

    // Declared sink-first, so the member destruction order tears the object
    // down while the inlet it is wired to still exists — sinks.hpp's rule.
    Rig() : op(new gIter()) {
      TestHelpers::Wire(*op, 0, sink, 0);
    }

    void SendList(const std::string& text) {
      op->GetInlet(0)->SetList(text, YSE::T_GUI);
    }
    void SendInt(int value) {
      op->GetInlet(0)->SetInt(value, YSE::T_GUI);
    }
    void SendFloat(float value) {
      op->GetInlet(0)->SetFloat(value, YSE::T_GUI);
    }
    void SendBang() {
      op->GetInlet(0)->SetBang(YSE::T_GUI);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape and registration ───────────────────────────────────────────────

  TEST_CASE("iter: registered, one inlet and one outlet (#521)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::G_ITER, "");
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == ".iter");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 1);
  }

  TEST_CASE("iter: appears in the registry's name list (#521)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ITER)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("iter: the inlet reports bang, int, float and list (#521)") {
    // The object holds the last message, so unlike `.unjoin` it really does
    // take a bang — GetAcceptedTypes() must say so.
    gIter obj;
    const unsigned int types = obj.GetInlet(0)->GetAcceptedTypes();
    CHECK((types & YSE::PATCHER::IT_BANG) != 0);
    CHECK((types & YSE::PATCHER::IT_INT) != 0);
    CHECK((types & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((types & YSE::PATCHER::IT_LIST) != 0);
  }

  // ─── the walk ─────────────────────────────────────────────────────────────

  TEST_CASE("iter: a list leaves as one message per item, in order (#521)") {
    // Max: "the numbers in the list are sent out the outlet in sequential
    // order". Three items in, three messages out, first to last — and each of
    // them an int rather than a list of one, which is the whole point.
    Rig rig;
    rig.SendList("1 2 3");

    REQUIRE(rig.sink.got.size() == 3);
    CHECK(rig.sink.got[0] == "i:1");
    CHECK(rig.sink.got[1] == "i:2");
    CHECK(rig.sink.got[2] == "i:3");
  }

  TEST_CASE("iter: each item's spelling decides its message type (#521)") {
    // The family's SendAtom rule, applied per item: a whole number leaves as an
    // int, a spelled float as a float, and anything else as one-token list
    // text. Nothing is coerced — an item carries whatever the list carried.
    Rig rig;
    rig.SendList("7 2.5 name");

    REQUIRE(rig.sink.got.size() == 3);
    CHECK(rig.sink.got[0] == "i:7");
    CHECK(rig.sink.got[1].substr(0, 2) == "f:");
    CHECK(rig.sink.got[1].find("2.5") != std::string::npos);
    CHECK(rig.sink.got[2] == "l:name");
  }

  TEST_CASE("iter: an int or a float is a one-item list and passes through (#521)") {
    // Max: "int: Outputs the number." One message in, one message out, of the
    // same type.
    Rig rig;
    rig.SendInt(42);
    REQUIRE(rig.sink.got.size() == 1);
    CHECK(rig.sink.got[0] == "i:42");

    rig.sink.reset();
    rig.SendFloat(1.5f);
    REQUIRE(rig.sink.got.size() == 1);
    CHECK(rig.sink.got[0].substr(0, 2) == "f:");
    CHECK(rig.sink.got[0].find("1.5") != std::string::npos);
  }

  TEST_CASE("iter: a message with no items sends nothing at all (#521)") {
    // The family's rule that an object with nothing to say says nothing, rather
    // than an empty message a downstream object would read as data.
    Rig rig;
    rig.SendList("");
    CHECK(rig.sink.got.empty());
    CHECK(rig.op->Held() == 0);

    rig.SendList("   ");
    CHECK(rig.sink.got.empty());
  }

  TEST_CASE("iter: each send completes in full before the next item leaves (#521)") {
    // The claim that makes this a serialiser rather than a scatter: the whole
    // subgraph behind the outlet runs, depth first, for one item before the
    // next item is sent. A count-only sink cannot tell the two apart.
    //
    // Declared innermost-first so the senders die before their targets.
    std::vector<char> log;
    log.reserve(64);

    OrderSink deep;
    Relay relay;

    deep.tag = '*';
    deep.log = &log;
    relay.log = &log;

    {
      gIter op;
      TestHelpers::Wire(op, 0, relay, 0);
      TestHelpers::Wire(relay, 0, deep, 0);

      // The relay writes '(' on entry, forwards, and writes ')' on the way out,
      // and the sink behind it writes '*' in between. Depth first, the brackets
      // close one item at a time; a queued send path would give "((( ***".
      op.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
      CHECK(std::string(log.begin(), log.end()) == "(*)(*)(*)");
    }
  }

  // ─── the held message ─────────────────────────────────────────────────────

  TEST_CASE("iter: bang re-sends the message most recently received (#521)") {
    // Max: "bang: Sends the number or list most recently received, in
    // sequential order." This is why the object holds a list at all.
    Rig rig;
    rig.SendList("4 5");
    REQUIRE(rig.sink.got.size() == 2);

    rig.sink.reset();
    rig.SendBang();
    REQUIRE(rig.sink.got.size() == 2);
    CHECK(rig.sink.got[0] == "i:4");
    CHECK(rig.sink.got[1] == "i:5");

    // And it is the *most recent* message, not the first one.
    rig.SendList("9");
    rig.sink.reset();
    rig.SendBang();
    REQUIRE(rig.sink.got.size() == 1);
    CHECK(rig.sink.got[0] == "i:9");
  }

  TEST_CASE("iter: a bang before anything has arrived sends nothing (#521)") {
    Rig rig;
    rig.SendBang();
    CHECK(rig.sink.got.empty());
    CHECK(rig.op->Held() == 0);
    CHECK(rig.op->Dropped() == 0);
  }

  TEST_CASE("iter: Held() and Stored() report the message being walked (#521)") {
    Rig rig;
    CHECK(rig.op->Held() == 0);
    CHECK(rig.op->Stored().empty());

    rig.SendList("1 2 sym");
    CHECK(rig.op->Held() == 3);
    CHECK(rig.op->Stored() == "1 2 sym");
  }

  // ─── the bound ────────────────────────────────────────────────────────────

  TEST_CASE("iter: an over-long list loses its tail, counted, and the head is walked (#521)") {
    // `.zl`'s side of the family's two-sided overflow rule: the surplus here is
    // input the patch has just sent, not state the object was holding, so the
    // head is kept rather than the whole message refused. And the count is a
    // counter rather than a log line — the refusing thread may be the audio
    // callback.
    //
    // Single-character items, so the atom ceiling is reached before the text
    // one: 300 of them is 44 past MAX_ATOMS.
    Rig rig;
    std::string text;
    const int sent = 300;
    for (int i = 0; i < sent; i++) {
      if (i != 0) text.push_back(' ');
      text.push_back('1');
    }
    rig.SendList(text);

    CHECK(rig.op->Held() == AtomList::MAX_ATOMS);
    CHECK(rig.op->Dropped() == (std::uint64_t)(sent - (int)AtomList::MAX_ATOMS));
    // The documented message budget: one stimulus can never cost more sends
    // than the list can hold items.
    CHECK(rig.sink.got.size() == gIter::MAX_ITEMS);
    CHECK(gIter::MAX_ITEMS == AtomList::MAX_ATOMS);
  }

  TEST_CASE("iter: a list past the text bound also loses its tail (#521)") {
    // The other ceiling: TEXT_CAPACITY rather than MAX_ATOMS, reached first
    // when the items are long. Fewer items than the atom limit still overflow.
    Rig rig;
    std::string text;
    const int sent = 100;
    for (int i = 0; i < sent; i++) {
      if (i != 0) text.push_back(' ');
      text.append("abcdefghijklmnopqrst"); // 20 characters each
    }
    rig.SendList(text);

    CHECK(rig.op->Held() < (std::size_t)sent);
    CHECK(rig.op->Held() == AtomList::TEXT_CAPACITY / 20);
    CHECK(rig.op->Dropped() == (std::uint64_t)(sent - (int)rig.op->Held()));
    CHECK(rig.sink.got.size() == rig.op->Held());
  }

  // ─── real-time behaviour ──────────────────────────────────────────────────

  TEST_CASE("iter: Calculate() emits nothing (#521)") {
    // The object is driven by its inlet; one that emitted here would re-walk
    // the held list on every DSP tick from a stimulus no patch sent — and this
    // is one of the more expensive ways to break that rule.
    Rig rig;
    rig.SendList("1 2 3");
    rig.sink.reset();

    for (int i = 0; i < 8; i++)
      rig.op->Calculate(YSE::T_DSP);
    CHECK(rig.sink.got.empty());
  }

  TEST_CASE("iter: a cord back into its own inlet is bounded, not fatal (#521)") {
    // The guard spans the *whole* walk, and this is the case that needs it: the
    // object emits in a loop, so a feedback cord re-enters the handler from
    // inside the walk. Letting that through would restart an iteration that
    // never terminates and rewrite the list being walked. Each item's send
    // comes back round once and is refused, so the count is the number of items
    // — and the call returns.
    gIter obj;
    REQUIRE(obj.ConnectInlet(obj.GetOutlet(0), 0));
    obj.ConnectOutlet(obj.GetInlet(0), 0);

    obj.GetInlet(0)->SetList("5 6 7", YSE::T_GUI);
    CHECK(obj.Dropped() == 3);
    // The list being walked was not rewritten by the messages coming back.
    CHECK(obj.Stored() == "5 6 7");
  }

  TEST_CASE("iter: the message path allocates nothing (#521)") {
    if (!TestHelpers::probeCountsAllocations()) return;
    // The claim covers a path that fills list storage and renders item text, so
    // it only means anything if the probe can see a std::string's own
    // allocations (issue #697).
    if (!TestHelpers::probeSeesStringAllocations()) return;

    // No TallySink here: it builds a std::string per message by design, which
    // is test scaffolding rather than the object under test.
    OrderSink sink;
    std::vector<char> log;
    log.reserve(1024);
    sink.tag = 'a';
    sink.log = &log;

    gIter op;
    TestHelpers::Wire(op, 0, sink, 0);

    const std::string listText = "111 222.5 name other 5 6 7 8";
    const std::string shortText = "12 13 14";

    // Warm every buffer the path touches, including the sink's `lastList`.
    op.GetInlet(0)->SetList(listText, YSE::T_GUI);
    op.GetInlet(0)->SetList(shortText, YSE::T_GUI);
    op.GetInlet(0)->SetInt(7, YSE::T_GUI);
    op.GetInlet(0)->SetFloat(1.25f, YSE::T_GUI);
    op.GetInlet(0)->SetBang(YSE::T_GUI);

    {
      TestHelpers::ProbeScope probe;
      op.GetInlet(0)->SetList(listText, YSE::T_GUI);
      op.GetInlet(0)->SetList(shortText, YSE::T_GUI);
      op.GetInlet(0)->SetInt(5, YSE::T_GUI);
      op.GetInlet(0)->SetFloat(2.5f, YSE::T_GUI);
      op.GetInlet(0)->SetBang(YSE::T_GUI);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  // ─── persistence ──────────────────────────────────────────────────────────

  TEST_CASE("iter: survives a DumpJSON / ParseJSON round trip (#521)") {
    // There are no creation arguments — Max documents none — so what the round
    // trip has to carry is the object itself and an empty parameter string,
    // rather than a shape that would come back wrong.
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_ITER, "") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".iter") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".iter");
    CHECK(copy->GetParams().empty());
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 1);
  }

  // ─── end to end, through a real patcher graph ─────────────────────────────

  TEST_CASE("iter: the items reach real objects one at a time down real cords (#521)") {
    // A standalone rig can only show the text an outlet carried; only the whole
    // chain shows the patcher delivering three separate int *messages* down one
    // cord, which is what the object is for.
    //
    // Sinks before the patcher: the patcher is torn down first, while the
    // inlets it is wired to still exist.
    TallySink tally;
    YSE::pHandle tallyHandle(&tally);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* iter = p.CreateObject(YSE::OBJ::G_ITER, "");
    REQUIRE(iter != nullptr);
    p.Connect(iter, 0, &tallyHandle, 0);

    iter->SetListData(0, "1 2 3");
    REQUIRE(tally.got.size() == 3);
    CHECK(tally.got[0] == "i:1");
    CHECK(tally.got[1] == "i:2");
    CHECK(tally.got[2] == "i:3");

    // And a bang down a real cord re-runs the whole sequence.
    tally.reset();
    iter->SetBang(0);
    REQUIRE(tally.got.size() == 3);
    CHECK(tally.got[0] == "i:1");
    CHECK(tally.got[2] == "i:3");
  }

  TEST_CASE("iter: per-element processing adds up through a real graph (#521)") {
    // The use the issue names — "serialise a list into individual messages, so
    // per-element processing works with ordinary scalar objects" — wired the
    // way a patch would: every item lands on `.accum`'s silent add inlet, so
    // the register ends up holding the sum. A single list message reaching that
    // inlet would add nothing at all, and one item would add only itself, so
    // the total is the assertion that all three arrived separately.
    MultiSink sum;
    YSE::pHandle sumHandle(&sum);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* iter = p.CreateObject(YSE::OBJ::G_ITER, "");
    REQUIRE(iter != nullptr);
    YSE::pHandle* accum = p.CreateObject(YSE::OBJ::G_ACCUM, "0");
    REQUIRE(accum != nullptr);
    p.Connect(iter, 0, accum, 1);
    p.Connect(accum, 0, &sumHandle, 0);

    iter->SetListData(0, "1 2 3");
    // The add inlet is silent, as Max documents, so nothing has been sent yet.
    CHECK_FALSE(sum.gotFloat);

    accum->SetBang(0);
    CHECK(sum.gotFloat);
    CHECK(sum.floatValue == doctest::Approx(6.f));

    // A bang re-runs the walk, so the same three items are added again.
    sum.reset();
    iter->SetBang(0);
    accum->SetBang(0);
    CHECK(sum.floatValue == doctest::Approx(12.f));
  }

  TEST_CASE("iter: symbols reach a real object as messages, not as a list (#521)") {
    // The other half of the transport rule through a real graph: a symbol item
    // arrives as a one-token message rather than as part of the list it came
    // from, so a downstream `.zl len` measures 1 per item rather than 2 once.
    MultiSink counted;
    YSE::pHandle countedHandle(&counted);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* iter = p.CreateObject(YSE::OBJ::G_ITER, "");
    REQUIRE(iter != nullptr);
    YSE::pHandle* zl = p.CreateObject(YSE::OBJ::G_ZL, "len");
    REQUIRE(zl != nullptr);
    p.Connect(iter, 0, zl, 0);
    p.Connect(zl, 0, &countedHandle, 0);

    iter->SetListData(0, "alpha beta");
    CHECK(counted.gotInt);
    CHECK(counted.intValue == 1);
  }

} // TEST_SUITE
