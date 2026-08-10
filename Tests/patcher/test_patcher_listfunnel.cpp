// Tests for `.listfunnel` (issue #522) — the object that indexes a message's
// elements and sends each one out as an `<index> <element>` pair.
//
// What is being pinned:
//
//   - **the shape**: one inlet, one outlet, and no `bang` method — Max
//     documents none, and unlike `.iter` this object holds no message to
//     re-send, so the inlet declines the type rather than swallowing it;
//   - **the pair**: every element leaves as a *two-element list*, which is
//     exactly what separates it from `.iter` (which sends the bare atom), and
//     the element keeps the spelling it arrived with;
//   - **the order**: pairs leave first to last, and each send completes in
//     full — the whole subgraph behind the outlet — before the next is sent;
//   - **the offset**: the creation argument and the `offset` message are one
//     value, the message is run-time state that does not write back to the
//     parameters, and the addition saturates rather than wraps;
//   - **the inherited bound**: an over-long message loses its *tail*, which is
//     counted — the list family's input-side rule — so one stimulus costs at
//     most 256 sends;
//   - **the guard across the walk**: a cord from the outlet back to the inlet
//     is refused and counted rather than restarting a walk that would never
//     terminate.
//
// The unit-level cases drive standalone objects, which is all this object
// needs (no patcher, no clock, no scheduler). The end-to-end section at the
// bottom drives a real `YSE::patcher` graph through `pHandle`, including the
// `.listfunnel` → `.spray` pairing the issue names, so the claim that the
// pairs address real objects down real cords is tested at the level a patch
// actually sees.
//
// No audio device required.

#include <doctest/doctest.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "patcher/genericObjects/gListFunnel.h"
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
using YSE::PATCHER::gListFunnel;

namespace {

  // Records every message that arrived, in arrival order and with its kind, so
  // "one pair per element, in this order, with these indices" is an assertion
  // rather than an inference. `OrderSink` keeps only the last of each kind,
  // which is exactly what an object emitting repeatedly needs a test *not* to
  // do. Shared in shape with test_patcher_iter.cpp's sink of the same name.
  struct TallySink : YSE::PATCHER::pObject {
    std::vector<std::string> got; // "i:5", "f:1.5", "l:0 a", "bang"

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
  // next pair leaves" claim, which a count-only sink cannot see.
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

  // A `.listfunnel` with one tally on its outlet. Sink first, so it outlives
  // the object that sends to it — sinks.hpp's teardown rule.
  struct Rig {
    TallySink sink;
    std::unique_ptr<gListFunnel> op;

    explicit Rig(const std::string& params = "") : op(new gListFunnel()) {
      // Parameters before wiring: the family's parameter callbacks run before
      // an object is published, and a rig that wired first would be testing a
      // shape the patcher never builds.
      if (!params.empty()) op->SetParams(params);
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
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape and registration ───────────────────────────────────────────────

  TEST_CASE("listfunnel: registered, one inlet and one outlet (#522)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::G_LISTFUNNEL, "");
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == ".listfunnel");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 1);
  }

  TEST_CASE("listfunnel: appears in the registry's name list (#522)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_LISTFUNNEL)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("listfunnel: the inlet takes int, float and list but not bang (#522)") {
    // Max documents no bang method for `listfunnel`, and it follows from the
    // object: `.iter`'s bang re-sends the message most recently received, so
    // `.iter` holds state, while this holds nothing between messages but its
    // offset. The inlet declines the type outright rather than accepting it and
    // doing nothing, so GetAcceptedTypes() reports the real contract.
    gListFunnel obj;
    const unsigned int types = obj.GetInlet(0)->GetAcceptedTypes();
    CHECK((types & YSE::PATCHER::IT_INT) != 0);
    CHECK((types & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((types & YSE::PATCHER::IT_LIST) != 0);
    CHECK((types & YSE::PATCHER::IT_BANG) == 0);
  }

  // ─── the walk ─────────────────────────────────────────────────────────────

  TEST_CASE("listfunnel: a list leaves as one index/element pair per element (#522)") {
    // Max: "outputs the elements of an incoming list in the format: [index]
    // [element] for each element of the list". Three elements in, three
    // messages out, first to last — and every one of them a *list*, never a
    // bare atom, which is what separates this from `.iter`.
    Rig rig;
    rig.SendList("10 20 30");

    REQUIRE(rig.sink.got.size() == 3);
    CHECK(rig.sink.got[0] == "l:0 10");
    CHECK(rig.sink.got[1] == "l:1 20");
    CHECK(rig.sink.got[2] == "l:2 30");
  }

  TEST_CASE("listfunnel: an element keeps the spelling it arrived with (#522)") {
    // The element is copied out of the stored text rather than re-formatted, so
    // an int stays an int, a spelled float keeps its point, and a symbol comes
    // through as itself. Only the index is (re)written, and always as an int.
    Rig rig;
    rig.SendList("7 2.5 name");

    REQUIRE(rig.sink.got.size() == 3);
    CHECK(rig.sink.got[0] == "l:0 7");
    CHECK(rig.sink.got[1] == "l:1 2.5");
    CHECK(rig.sink.got[2] == "l:2 name");
  }

  TEST_CASE("listfunnel: an int or a float is a one-element message (#522)") {
    // Max: "The low index value and the received number are sent out as a
    // two-element list."
    Rig rig;
    rig.SendInt(42);
    REQUIRE(rig.sink.got.size() == 1);
    CHECK(rig.sink.got[0] == "l:0 42");

    rig.sink.reset();
    rig.SendFloat(1.5f);
    REQUIRE(rig.sink.got.size() == 1);
    CHECK(rig.sink.got[0].substr(0, 4) == "l:0 ");
    CHECK(rig.sink.got[0].find("1.5") != std::string::npos);
  }

  TEST_CASE("listfunnel: a message with no elements sends nothing at all (#522)") {
    // The family's rule that an object with nothing to say says nothing, rather
    // than an empty message a downstream object would read as data.
    Rig rig;
    rig.SendList("");
    CHECK(rig.sink.got.empty());
    CHECK(rig.op->Held() == 0);

    rig.SendList("   ");
    CHECK(rig.sink.got.empty());
    CHECK(rig.op->Held() == 0);
  }

  TEST_CASE("listfunnel: each send completes in full before the next pair leaves (#522)") {
    // The whole subgraph behind the outlet runs, depth first, for one pair
    // before the next pair is sent. A count-only sink cannot tell that apart
    // from a queued fan-out.
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
      gListFunnel op;
      TestHelpers::Wire(op, 0, relay, 0);
      TestHelpers::Wire(relay, 0, deep, 0);

      op.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
      CHECK(std::string(log.begin(), log.end()) == "(*)(*)(*)");
    }
  }

  TEST_CASE("listfunnel: Held() and Stored() report the message being indexed (#522)") {
    Rig rig;
    CHECK(rig.op->Held() == 0);
    CHECK(rig.op->Stored().empty());

    rig.SendList("1 2 sym");
    CHECK(rig.op->Held() == 3);
    CHECK(rig.op->Stored() == "1 2 sym");
  }

  // ─── the offset ───────────────────────────────────────────────────────────

  TEST_CASE("listfunnel: the creation argument sets the first index (#522)") {
    // Max's argument is "a starting index value", so element k leaves tagged
    // offset + k.
    Rig rig("1");
    CHECK(rig.op->Offset() == 1);
    rig.SendList("a b c");

    REQUIRE(rig.sink.got.size() == 3);
    CHECK(rig.sink.got[0] == "l:1 a");
    CHECK(rig.sink.got[1] == "l:2 b");
    CHECK(rig.sink.got[2] == "l:3 c");
  }

  TEST_CASE("listfunnel: a negative offset counts up from it (#522)") {
    Rig rig("-2");
    CHECK(rig.op->Offset() == -2);
    CHECK(rig.op->IndexFor(0) == -2);
    CHECK(rig.op->IndexFor(3) == 1);

    rig.SendList("a b c");
    REQUIRE(rig.sink.got.size() == 3);
    CHECK(rig.sink.got[0] == "l:-2 a");
    CHECK(rig.sink.got[2] == "l:0 c");
  }

  TEST_CASE("listfunnel: a float creation argument is truncated, a symbol ignored (#522)") {
    Rig truncated("2.7");
    CHECK(truncated.op->Offset() == 2);

    Rig ignored("abc");
    CHECK(ignored.op->Offset() == 0);
  }

  TEST_CASE("listfunnel: the offset message changes the index and sends nothing (#522)") {
    // Max: "The word offset followed by an integer argument is used to specify
    // an offset for the first index value."
    Rig rig;
    rig.SendList("offset 5");
    CHECK(rig.sink.got.empty());
    CHECK(rig.op->Offset() == 5);
    CHECK(rig.op->Dropped() == 0);

    rig.SendList("a b");
    REQUIRE(rig.sink.got.size() == 2);
    CHECK(rig.sink.got[0] == "l:5 a");
    CHECK(rig.sink.got[1] == "l:6 b");
  }

  TEST_CASE("listfunnel: a bare or malformed offset does nothing at all (#522)") {
    // The message word with nothing usable after it is the method with nothing
    // to do — indexing it as the symbol it also is would answer a method call
    // with `0 offset`, which no patch means.
    Rig rig("3");
    rig.SendList("offset");
    CHECK(rig.sink.got.empty());
    CHECK(rig.op->Offset() == 3);

    rig.SendList("offset abc");
    CHECK(rig.sink.got.empty());
    CHECK(rig.op->Offset() == 3);
  }

  TEST_CASE("listfunnel: the offset message does not write back to the parameters (#522)") {
    // Run-time state, as `.funnel`'s and `.spray`'s offsets are: a saved patch
    // carries the creation argument rather than the last message.
    Rig rig("4");
    rig.SendList("offset 9");
    CHECK(rig.op->Offset() == 9);
    CHECK(rig.op->GetParams() == "4");
  }

  TEST_CASE("listfunnel: an index that would overflow saturates rather than wraps (#522)") {
    // A wrapped index would read as a perfectly plausible one to whatever
    // consumes it, so the addition is pinned at the int limits instead.
    //
    // 2147483008 is 639 below INT_MAX and exactly representable as a float, so
    // the creation argument survives the family's strict token reader unchanged
    // and the boundary can be named rather than approached.
    gListFunnel high;
    high.SetParams("2147483008");
    CHECK(high.Offset() == 2147483008);
    CHECK(high.IndexFor(0) == 2147483008);
    CHECK(high.IndexFor(639) == 2147483647);
    CHECK(high.IndexFor(640) == 2147483647);
    CHECK(high.IndexFor(100000) == 2147483647);

    // The far end of the range is an ordinary offset, not a saturating one: an
    // element's position is never negative, so nothing can push the sum below
    // INT_MIN.
    gListFunnel low;
    low.SetParams("-2147483648");
    CHECK(low.Offset() == -2147483647 - 1);
    CHECK(low.IndexFor(0) == -2147483647 - 1);
    CHECK(low.IndexFor(2) == -2147483646);
  }

  TEST_CASE("listfunnel: SetParams(\"\") returns the object to a zero offset (#522)") {
    // The clear callback is the whole of `SetParams("")`: Parameters::Set
    // returns without calling the parse callback for an empty argument.
    gListFunnel obj;
    obj.SetParams("7");
    CHECK(obj.Offset() == 7);
    obj.SetParams("");
    CHECK(obj.Offset() == 0);
  }

  // ─── the bound ────────────────────────────────────────────────────────────

  TEST_CASE("listfunnel: an over-long message loses its tail, counted (#522)") {
    // The list family's input-side overflow rule: the surplus here is input the
    // patch has just sent, not state the object was holding, so the head is
    // kept rather than the whole message refused. And the count is a counter
    // rather than a log line — the refusing thread may be the audio callback.
    //
    // Single-character elements, so the atom ceiling is reached before the text
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
    // than the storage can hold elements.
    CHECK(rig.sink.got.size() == gListFunnel::MAX_ITEMS);
    CHECK(gListFunnel::MAX_ITEMS == AtomList::MAX_ATOMS);
    // The head is what was indexed, so the last pair carries the last index the
    // bound allows rather than the one the patch sent.
    CHECK(rig.sink.got.front() == "l:0 1");
    CHECK(rig.sink.got.back() == "l:255 1");
  }

  TEST_CASE("listfunnel: a message past the text bound also loses its tail (#522)") {
    // The other ceiling: TEXT_CAPACITY rather than MAX_ATOMS, reached first
    // when the elements are long. Fewer elements than the atom limit still
    // overflow.
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

  TEST_CASE("listfunnel: Calculate() emits nothing (#522)") {
    // The object is driven by its inlet; one that emitted here would re-index
    // the last message on every DSP tick from a stimulus no patch sent.
    Rig rig;
    rig.SendList("1 2 3");
    rig.sink.reset();

    for (int i = 0; i < 8; i++)
      rig.op->Calculate(YSE::T_DSP);
    CHECK(rig.sink.got.empty());
  }

  TEST_CASE("listfunnel: a cord back into its own inlet is bounded, not fatal (#522)") {
    // The guard spans the *whole* walk, and this is the case that needs it: the
    // object emits in a loop, so a feedback cord re-enters the handler from
    // inside the walk. Letting that through would restart a walk that never
    // terminates and rewrite the list being walked. Each pair comes back round
    // once and is refused, so the count is the number of elements — and the
    // call returns.
    gListFunnel obj;
    REQUIRE(obj.ConnectInlet(obj.GetOutlet(0), 0));
    obj.ConnectOutlet(obj.GetInlet(0), 0);

    obj.GetInlet(0)->SetList("5 6 7", YSE::T_GUI);
    CHECK(obj.Dropped() == 3);
    // The list being indexed was not rewritten by the pairs coming back.
    CHECK(obj.Stored() == "5 6 7");
  }

  TEST_CASE("listfunnel: the message path allocates nothing (#522)") {
    if (!TestHelpers::probeCountsAllocations()) return;
    // The claim covers a path that fills list storage and assembles pair text,
    // so it only means anything if the probe can see a std::string's own
    // allocations (issue #697).
    if (!TestHelpers::probeSeesStringAllocations()) return;

    // No TallySink here: it builds a std::string per message by design, which
    // is test scaffolding rather than the object under test.
    OrderSink sink;
    std::vector<char> log;
    log.reserve(1024);
    sink.tag = 'a';
    sink.log = &log;

    gListFunnel op;
    TestHelpers::Wire(op, 0, sink, 0);

    const std::string listText = "111 222.5 name other 5 6 7 8";
    const std::string shortText = "12 13 14";

    // Warm every buffer the path touches, including the sink's `lastList`.
    op.GetInlet(0)->SetList(listText, YSE::T_GUI);
    op.GetInlet(0)->SetList(shortText, YSE::T_GUI);
    op.GetInlet(0)->SetList("offset 3", YSE::T_GUI);
    op.GetInlet(0)->SetInt(7, YSE::T_GUI);
    op.GetInlet(0)->SetFloat(1.25f, YSE::T_GUI);

    {
      TestHelpers::ProbeScope probe;
      op.GetInlet(0)->SetList(listText, YSE::T_GUI);
      op.GetInlet(0)->SetList(shortText, YSE::T_GUI);
      op.GetInlet(0)->SetList("offset 4", YSE::T_GUI);
      op.GetInlet(0)->SetInt(5, YSE::T_GUI);
      op.GetInlet(0)->SetFloat(2.5f, YSE::T_GUI);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  // ─── persistence ──────────────────────────────────────────────────────────

  TEST_CASE("listfunnel: survives a DumpJSON / ParseJSON round trip (#522)") {
    // The offset is the one thing a saved patch has to carry, and the round
    // trip has to bring back an object that *behaves* with it rather than
    // merely one whose parameter string reads right.
    TallySink tally;
    YSE::pHandle tallyHandle(&tally);

    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_LISTFUNNEL, "5") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".listfunnel") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".listfunnel");
    CHECK(copy->GetParams() == "5");
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 1);

    loaded.Connect(copy, 0, &tallyHandle, 0);
    copy->SetListData(0, "a b");
    REQUIRE(tally.got.size() == 2);
    CHECK(tally.got[0] == "l:5 a");
    CHECK(tally.got[1] == "l:6 b");
  }

  // ─── end to end, through a real patcher graph ─────────────────────────────

  TEST_CASE("listfunnel: the pairs reach real objects down real cords (#522)") {
    // A standalone rig can only show the text an outlet carried; only the whole
    // chain shows the patcher delivering three separate list *messages* down
    // one cord, which is what the object is for.
    //
    // Sinks before the patcher: the patcher is torn down first, while the
    // inlets it is wired to still exist.
    TallySink tally;
    YSE::pHandle tallyHandle(&tally);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* funnel = p.CreateObject(YSE::OBJ::G_LISTFUNNEL, "");
    REQUIRE(funnel != nullptr);
    p.Connect(funnel, 0, &tallyHandle, 0);

    funnel->SetListData(0, "10 20 30");
    REQUIRE(tally.got.size() == 3);
    CHECK(tally.got[0] == "l:0 10");
    CHECK(tally.got[1] == "l:1 20");
    CHECK(tally.got[2] == "l:2 30");

    // And the `offset` message reaches it down the same cord.
    tally.reset();
    funnel->SetListData(0, "offset 100");
    CHECK(tally.got.empty());
    funnel->SetListData(0, "10 20");
    REQUIRE(tally.got.size() == 2);
    CHECK(tally.got[0] == "l:100 10");
    CHECK(tally.got[1] == "l:101 20");
  }

  TEST_CASE("listfunnel: element k reaches .spray outlet k through a real graph (#522)") {
    // The use the issue names — "the natural front end for anything addressed
    // by index — .spray, .table, a bank of parameters" — wired the way a patch
    // would. `.spray` reads the first element of a list as an outlet number, so
    // the pair this object makes is exactly `.spray`'s input format: a list of
    // three values arrives at one inlet and leaves by three different outlets,
    // which no single message could have done.
    //
    // Sinks before the patcher, so the patcher is torn down first.
    MultiSink out0;
    MultiSink out1;
    MultiSink out2;
    YSE::pHandle h0(&out0);
    YSE::pHandle h1(&out1);
    YSE::pHandle h2(&out2);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* funnel = p.CreateObject(YSE::OBJ::G_LISTFUNNEL, "");
    REQUIRE(funnel != nullptr);
    YSE::pHandle* spray = p.CreateObject(YSE::OBJ::G_SPRAY, "3");
    REQUIRE(spray != nullptr);
    p.Connect(funnel, 0, spray, 0);
    p.Connect(spray, 0, &h0, 0);
    p.Connect(spray, 1, &h1, 0);
    p.Connect(spray, 2, &h2, 0);

    funnel->SetListData(0, "11 22 33");

    CHECK(out0.gotInt);
    CHECK(out0.intValue == 11);
    CHECK(out1.gotInt);
    CHECK(out1.intValue == 22);
    CHECK(out2.gotInt);
    CHECK(out2.intValue == 33);
  }

  TEST_CASE("listfunnel: a pair arrives as a two-element list, not as an atom (#522)") {
    // The other half of the transport rule through a real graph, and the line
    // between this object and `.iter`: `.iter` would send `alpha` as a
    // one-token message, so a downstream `.zl len` would measure 1. Here every
    // element is paired with its index, so the length is always 2.
    MultiSink counted;
    YSE::pHandle countedHandle(&counted);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* funnel = p.CreateObject(YSE::OBJ::G_LISTFUNNEL, "");
    REQUIRE(funnel != nullptr);
    YSE::pHandle* zl = p.CreateObject(YSE::OBJ::G_ZL, "len");
    REQUIRE(zl != nullptr);
    p.Connect(funnel, 0, zl, 0);
    p.Connect(zl, 0, &countedHandle, 0);

    funnel->SetListData(0, "alpha beta");
    CHECK(counted.gotInt);
    CHECK(counted.intValue == 2);
  }

} // TEST_SUITE
