// Tests for .swap (issue #476) — reverse the order of a pair of numbers.
//
// Four rules carry this file, and each is one a plausible implementation gets
// wrong:
//
//   - **the pair crosses over.** Max: "The number is sent out the right outlet,
//     then the number in the right inlet is sent out the left outlet." An
//     implementation that forgets the crossing passes every count-based test
//     and is a two-outlet pass-through.
//   - **right outlet first.** The object exists to feed a two-inlet box, so the
//     idiom is outlet 1 into the cold inlet and outlet 0 into the hot one.
//     Reverse the order and every downstream result is one input stale — which
//     is the very bug a patch reached for .swap to avoid, so it is pinned both
//     through order-logging sinks and end to end through a real .- in a real
//     patcher.
//   - **the kind is forwarded, not normalised.** This is the whole of why there
//     is one object here and not Max's swap / fswap pair (see gSwap.h): an int
//     in comes out an int and a float in comes out a float, on either side
//     independently, and the creation argument's *spelling* decides the kind of
//     the right slot. An implementation that stores both sides as float passes
//     every value test and silently rewrites the type of everything that goes
//     through it.
//   - **only inlet 0 releases.** Inlet 1 is cold, as the right inlet of every
//     other two-inlet object here is.
//
// The rest is the message grammar from the Max reference: a bang is a replay of
// the current pair rather than an exchange of the slots, and a list writes both
// slots and then releases.
//
// The standalone rigs wire objects directly rather than through a patcher, as
// every sibling suite does; the ordering and idiom cases additionally run
// through a real patcher so the guarantee is asserted where a patch can see it.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/gSwap.h"
#include "patcher/sinks.hpp"

namespace {

  using TestHelpers::FloatSink;
  using TestHelpers::MultiSink;
  using TestHelpers::OrderSink;
  using YSE::PATCHER::gSwap;

  // One order-logging sink per outlet, both sharing one log, so "which outlet
  // sent what, and in what order" is an assertion rather than an inference.
  struct Rig {
    gSwap op;
    OrderSink left; // wired to outlet 0, tagged 'a'
    OrderSink right; // wired to outlet 1, tagged 'b'
    std::vector<char> order;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) op.SetParams(args);
      Wire();
    }

    void Wire() {
      left.tag = 'a';
      left.log = &order;
      op.ConnectOutlet(left.GetInlet(0), 0);
      left.ConnectInlet(op.GetOutlet(0), 0);

      right.tag = 'b';
      right.log = &order;
      op.ConnectOutlet(right.GetInlet(0), 1);
      right.ConnectInlet(op.GetOutlet(1), 0);
    }

    void Bang(int inlet = 0) {
      op.GetInlet(inlet)->SetBang(YSE::T_GUI);
    }
    void SendInt(int inlet, int v) {
      op.GetInlet(inlet)->SetInt(v, YSE::T_GUI);
    }
    void SendFloat(int inlet, float v) {
      op.GetInlet(inlet)->SetFloat(v, YSE::T_GUI);
    }
    void List(int inlet, const std::string& text) {
      op.GetInlet(inlet)->SetList(text, YSE::T_GUI);
    }

    std::string Log() const {
      return std::string(order.begin(), order.end());
    }
    void ClearLog() {
      order.clear();
    }
    int Total() const {
      return left.count + right.count;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── registry / shape ───────────────────────────────────────────────────────

  TEST_CASE("swap: creatable through the registry (#476)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SWAP);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".swap"));
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 2);
  }

  TEST_CASE("swap: listed by pRegistry::AllNames (#476)") {
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(YSE::OBJ::G_SWAP)) != names.end());
  }

  TEST_CASE("swap: both outlets are ANY, since either kind may leave them (#476)") {
    // The shape that makes one object do the work of Max's swap *and* fswap: a
    // fixed INT or FLOAT outlet here would force the pair of boxes back.
    gSwap op;
    CHECK(op.GetOutputType(0) == YSE::OUT_TYPE::ANY);
    CHECK(op.GetOutputType(1) == YSE::OUT_TYPE::ANY);
  }

  TEST_CASE("swap: there is no .fswap — the one object covers both (#476)") {
    // The decision recorded in gSwap.h, asserted rather than only described: a
    // second box would differ from this one in nothing, so it is not registered.
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".fswap")) == names.end());
  }

  // ─── the crossing ───────────────────────────────────────────────────────────

  TEST_CASE("swap: an int on inlet 0 crosses over with the stored right value (#476)") {
    // Max: "The number is sent out the right outlet, then the number in the
    // right inlet is sent out the left outlet."
    Rig rig;
    rig.SendInt(1, 7);
    REQUIRE(rig.Total() == 0); // the cold inlet emitted nothing

    rig.SendInt(0, 3);
    CHECK(rig.right.lastKind == OrderSink::INT);
    CHECK(rig.right.lastInt == 3); // what arrived on the left
    CHECK(rig.left.lastKind == OrderSink::INT);
    CHECK(rig.left.lastInt == 7); // what was stored on the right
  }

  TEST_CASE("swap: inlet 1 stores without emitting (#476)") {
    Rig rig;
    rig.SendInt(1, 1);
    rig.SendFloat(1, 2.5f);
    rig.SendInt(1, 9);
    CHECK(rig.Total() == 0);
    CHECK(rig.Log().empty());

    // ...and the last one stored is the one that comes out.
    rig.SendInt(0, 0);
    CHECK(rig.left.lastInt == 9);
  }

  TEST_CASE("swap: the stored right value is not consumed by a release (#476)") {
    // Nothing in the reference says the right slot is emptied, and an object
    // that consumed it would answer 0 to every input after the first.
    Rig rig("4");
    rig.SendInt(0, 1);
    CHECK(rig.left.lastInt == 4);
    rig.SendInt(0, 2);
    CHECK(rig.left.lastInt == 4);
    CHECK(rig.right.lastInt == 2);
  }

  TEST_CASE("swap: the left slot persists between releases (#476)") {
    Rig rig;
    rig.SendInt(0, 5);
    REQUIRE(rig.right.lastInt == 5);
    // A bang replays it, so the left slot was not emptied by the first release.
    rig.Bang();
    CHECK(rig.right.lastInt == 5);
  }

  // ─── ordering ───────────────────────────────────────────────────────────────

  TEST_CASE("swap: the right outlet fires before the left one (#476)") {
    Rig rig("2");
    rig.SendInt(0, 1);
    // 'b' is outlet 1: right to left, not left to right.
    CHECK(rig.Log() == "ba");
    CHECK(rig.left.count == 1);
    CHECK(rig.right.count == 1);
  }

  TEST_CASE("swap: each send completes before the outlet to its left fires (#476)") {
    // Not merely "outlet 1 is served first", but "the whole subgraph behind
    // outlet 1 has run first" — the guarantee the feed-an-arithmetic-box idiom
    // actually depends on. A second .swap hangs off outlet 1, so its own pair
    // ('y' then 'x') must complete before the outer object's outlet 0 ('z') is
    // served.
    std::vector<char> order;
    gSwap outer;
    gSwap inner;

    outer.ConnectOutlet(inner.GetInlet(0), 1);
    inner.ConnectInlet(outer.GetOutlet(1), 0);

    OrderSink x;
    x.tag = 'x';
    x.log = &order;
    inner.ConnectOutlet(x.GetInlet(0), 0);
    x.ConnectInlet(inner.GetOutlet(0), 0);

    OrderSink y;
    y.tag = 'y';
    y.log = &order;
    inner.ConnectOutlet(y.GetInlet(0), 1);
    y.ConnectInlet(inner.GetOutlet(1), 0);

    OrderSink z;
    z.tag = 'z';
    z.log = &order;
    outer.ConnectOutlet(z.GetInlet(0), 0);
    z.ConnectInlet(outer.GetOutlet(0), 0);

    outer.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(std::string(order.begin(), order.end()) == "yxz");
  }

  // ─── bang ───────────────────────────────────────────────────────────────────

  TEST_CASE("swap: a bang releases the current pair (#476)") {
    // Max: "Swaps and outputs the currently stored numbers."
    Rig rig("8");
    rig.SendInt(0, 3);
    rig.ClearLog();

    rig.Bang();
    CHECK(rig.Log() == "ba");
    CHECK(rig.right.lastInt == 3);
    CHECK(rig.left.lastInt == 8);
  }

  TEST_CASE("swap: a bang is a replay, not an exchange of the slots (#476)") {
    // The natural wrong reading: an object that really swapped its slots would
    // alternate on repeated bangs. Two bangs must send the same pair twice.
    Rig rig("8");
    rig.SendInt(0, 3);

    rig.Bang();
    const int firstLeft = rig.left.lastInt;
    const int firstRight = rig.right.lastInt;
    rig.Bang();
    CHECK(rig.left.lastInt == firstLeft);
    CHECK(rig.right.lastInt == firstRight);
    CHECK(rig.op.LeftInt() == 3);
    CHECK(rig.op.RightInt() == 8);
  }

  TEST_CASE("swap: a bang before any input sends 0 and the argument (#476)") {
    Rig rig("6");
    rig.Bang();
    CHECK(rig.right.lastKind == OrderSink::INT);
    CHECK(rig.right.lastInt == 0);
    CHECK(rig.left.lastInt == 6);
  }

  TEST_CASE("swap: a bang on inlet 1 does nothing (#476)") {
    // Max documents the bang on the left inlet only, and the cold inlet has no
    // bang handler at all — so nothing is dispatched, not merely nothing sent.
    Rig rig;
    rig.Bang(1);
    CHECK(rig.Total() == 0);
  }

  // ─── the kind is forwarded, not normalised ──────────────────────────────────

  TEST_CASE("swap: an int arrives as an int and a float as a float (#476)") {
    // The .fswap decision, from the outside: this is what a fixed-type outlet
    // pair would make impossible.
    Rig rig;
    rig.SendInt(0, 5);
    CHECK(rig.right.lastKind == OrderSink::INT);
    CHECK(rig.right.lastInt == 5);

    rig.SendFloat(0, 5.5f);
    CHECK(rig.right.lastKind == OrderSink::FLOAT);
    CHECK(rig.right.lastFloat == doctest::Approx(5.5f));
  }

  TEST_CASE("swap: the two sides keep their kinds independently (#476)") {
    // An int on one side and a float on the other, in both arrangements: an
    // implementation that stores a single kind for the object, or that widens
    // everything to float, fails one of the two.
    Rig a;
    a.SendFloat(1, 2.5f);
    a.SendInt(0, 4);
    CHECK(a.right.lastKind == OrderSink::INT);
    CHECK(a.right.lastInt == 4);
    CHECK(a.left.lastKind == OrderSink::FLOAT);
    CHECK(a.left.lastFloat == doctest::Approx(2.5f));

    Rig b;
    b.SendInt(1, 4);
    b.SendFloat(0, 2.5f);
    CHECK(b.right.lastKind == OrderSink::FLOAT);
    CHECK(b.right.lastFloat == doctest::Approx(2.5f));
    CHECK(b.left.lastKind == OrderSink::INT);
    CHECK(b.left.lastInt == 4);
  }

  TEST_CASE("swap: a float on the cold inlet stays a float (#476)") {
    // The cold inlet has to keep the kind too — this is the half Max needed
    // `fswap` for, since `swap`'s right inlet is an int inlet.
    Rig rig;
    rig.SendFloat(1, -0.25f);
    rig.Bang();
    CHECK(rig.left.lastKind == OrderSink::FLOAT);
    CHECK(rig.left.lastFloat == doctest::Approx(-0.25f));
  }

  // ─── the creation argument ──────────────────────────────────────────────────

  TEST_CASE("swap: the argument pre-loads the right slot (#476)") {
    // Max: the argument "sets the initial value sent from the left outlet".
    gSwap op;
    op.SetParams("12");
    CHECK(op.RightKind() == gSwap::Kind::INT);
    CHECK(op.RightInt() == 12);
  }

  TEST_CASE("swap: the argument's spelling decides the kind (#476)") {
    // Max's "a float argument causes float output from the left outlet", said
    // the way this object can say it.
    gSwap asInt;
    asInt.SetParams("5");
    CHECK(asInt.RightKind() == gSwap::Kind::INT);
    CHECK(asInt.RightInt() == 5);

    gSwap asFloat;
    asFloat.SetParams("5.");
    CHECK(asFloat.RightKind() == gSwap::Kind::FLOAT);
    CHECK(asFloat.RightFloat() == doctest::Approx(5.f));

    gSwap withExponent;
    withExponent.SetParams("5e0");
    CHECK(withExponent.RightKind() == gSwap::Kind::FLOAT);
    CHECK(withExponent.RightFloat() == doctest::Approx(5.f));
  }

  TEST_CASE("swap: the argument's kind reaches the outlet (#476)") {
    // Not just stored as a float — actually sent as one, which is the only part
    // a patch can see.
    Rig integral("5");
    integral.Bang();
    CHECK(integral.left.lastKind == OrderSink::INT);

    Rig fractional("5.");
    fractional.Bang();
    CHECK(fractional.left.lastKind == OrderSink::FLOAT);
    CHECK(fractional.left.lastFloat == doctest::Approx(5.f));
  }

  TEST_CASE("swap: with no argument the right slot is int 0 (#476)") {
    gSwap op;
    CHECK(op.RightKind() == gSwap::Kind::INT);
    CHECK(op.RightInt() == 0);
    CHECK(op.LeftKind() == gSwap::Kind::INT);
    CHECK(op.LeftInt() == 0);
  }

  TEST_CASE("swap: a non-numeric argument leaves the default in place (#476)") {
    // Strictly, so `5abc` is not read as 5 and `1e999` is not folded to 0 —
    // both of which would be a silently wrong initial value rather than none.
    gSwap word;
    word.SetParams("hello");
    CHECK(word.RightKind() == gSwap::Kind::INT);
    CHECK(word.RightInt() == 0);

    gSwap partial;
    partial.SetParams("5abc");
    CHECK(partial.RightInt() == 0);

    gSwap overflowing;
    overflowing.SetParams("1e999");
    CHECK(overflowing.RightKind() == gSwap::Kind::INT);
    CHECK(overflowing.RightInt() == 0);
  }

  TEST_CASE("swap: re-parsing an empty parameter string returns to int 0 (#476)") {
    gSwap op;
    op.SetParams("9.5");
    REQUIRE(op.RightKind() == gSwap::Kind::FLOAT);

    op.SetParams("");
    CHECK(op.RightKind() == gSwap::Kind::INT);
    CHECK(op.RightInt() == 0);
  }

  TEST_CASE("swap: a surplus argument is ignored (#476)") {
    // Max documents one argument; the second token belongs to no parameter.
    gSwap op;
    op.SetParams("3 99");
    CHECK(op.RightKind() == gSwap::Kind::INT);
    CHECK(op.RightInt() == 3);
  }

  // ─── the list method ────────────────────────────────────────────────────────

  TEST_CASE("swap: a two-number list writes both slots and releases (#476)") {
    // Max: "The numbers are stored in swap. The first number is sent out the
    // right outlet, then the second number is sent out the left outlet."
    Rig rig;
    rig.List(0, "1 2");
    CHECK(rig.Log() == "ba");
    CHECK(rig.right.lastInt == 1);
    CHECK(rig.left.lastInt == 2);
    CHECK(rig.op.LeftInt() == 1);
    CHECK(rig.op.RightInt() == 2);
  }

  TEST_CASE("swap: each list element keeps its own spelling (#476)") {
    Rig rig;
    rig.List(0, "1 2.5");
    CHECK(rig.right.lastKind == OrderSink::INT);
    CHECK(rig.right.lastInt == 1);
    CHECK(rig.left.lastKind == OrderSink::FLOAT);
    CHECK(rig.left.lastFloat == doctest::Approx(2.5f));
  }

  TEST_CASE("swap: a one-number list is the plain number (#476)") {
    Rig rig("7");
    rig.List(0, "3");
    CHECK(rig.right.lastInt == 3);
    CHECK(rig.left.lastInt == 7); // the right slot was left alone
  }

  TEST_CASE("swap: list elements past the second are dropped (#476)") {
    Rig rig;
    rig.List(0, "1 2 3 4");
    CHECK(rig.right.lastInt == 1);
    CHECK(rig.left.lastInt == 2);
    CHECK(rig.left.count == 1);
    CHECK(rig.right.count == 1);
  }

  TEST_CASE("swap: reading stops at the first token that is not a number (#476)") {
    Rig rig("7");
    rig.List(0, "3 hello");
    // The `3` is a one-number list; `hello` neither stores nor stops the
    // release, which would be the two other plausible readings.
    CHECK(rig.right.lastInt == 3);
    CHECK(rig.left.lastInt == 7);
  }

  TEST_CASE("swap: a message with no leading number is ignored entirely (#476)") {
    // Not "releases the old pair" and not "stores something" — a word this
    // object does not know.
    Rig rig("7");
    rig.SendInt(0, 1);
    rig.ClearLog();

    rig.List(0, "hello world");
    CHECK(rig.Log().empty());
    CHECK(rig.op.LeftInt() == 1);
    CHECK(rig.op.RightInt() == 7);
  }

  TEST_CASE("swap: an empty list message is ignored (#476)") {
    Rig rig;
    rig.List(0, "");
    rig.List(0, "   ");
    CHECK(rig.Total() == 0);
  }

  TEST_CASE("swap: a list on inlet 1 does nothing (#476)") {
    // Max documents the list method on the left inlet only, and the cold inlet
    // registers no list handler.
    Rig rig;
    rig.List(1, "1 2");
    CHECK(rig.Total() == 0);
    CHECK(rig.op.RightInt() == 0);
  }

  // ─── re-entrancy ────────────────────────────────────────────────────────────

  TEST_CASE("swap: the pair is snapshotted before the first send (#476)") {
    // Outlet 1 is wired back into inlet 1, so the release re-enters this object
    // inside its own first Send. Outlet 0 must still carry the value the
    // release began with; reading the slot again at that point would send the
    // value the loop has just written instead.
    gSwap op;
    op.SetParams("7");

    OrderSink left;
    op.ConnectOutlet(left.GetInlet(0), 0);
    left.ConnectInlet(op.GetOutlet(0), 0);

    op.ConnectOutlet(op.GetInlet(1), 1);
    op.ConnectInlet(op.GetOutlet(1), 1);

    op.GetInlet(0)->SetInt(3, YSE::T_GUI);

    CHECK(left.count == 1);
    CHECK(left.lastInt == 7); // the argument, not the 3 the loop wrote back
    // ...and the loop did land, so the next release sees it.
    CHECK(op.RightInt() == 3);
  }

  // ─── Calculate ──────────────────────────────────────────────────────────────

  TEST_CASE("swap: Calculate sends nothing (#476)") {
    // An emitting Calculate() would re-release the pair on every DSP tick from
    // a stimulus no patch sent — the rule .sel, .trigger, .past and .bondo set.
    Rig rig("2");
    rig.SendInt(0, 1);
    rig.ClearLog();

    for (int i = 0; i < 5; i++)
      rig.op.Calculate(YSE::T_DSP);
    CHECK(rig.Log().empty());
  }

  // ─── params / persistence ───────────────────────────────────────────────────

  TEST_CASE("swap: survives a DumpJSON / ParseJSON round trip (#476)") {
    // The float spelling has to survive too, or a reloaded patch quietly starts
    // emitting ints where it emitted floats.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_SWAP);
    REQUIRE(h != nullptr);
    h->SetParams("5.");
    const std::string json = src.DumpJSON();
    CHECK(json.find(".swap") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".swap"));
    CHECK(copy->GetParams() == std::string("5."));

    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    loaded.Connect(copy, 0, &sinkHandle, 0);
    copy->SetBang(0);
    CHECK(sink.gotFloat);
    CHECK_FALSE(sink.gotInt);
    CHECK(sink.floatValue == doctest::Approx(5.f));
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category, the port labels and the parameter name,
  // which is what a binding generator keys on.

  TEST_CASE("swap: documents itself as MATH with a labelled port set (#476)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_SWAP));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
    CHECK_FALSE(obj->GetDescription().empty());

    REQUIRE(obj->NumInputs() == 2);
    CHECK(obj->GetInlet(0)->GetDocLabel() == "left");
    CHECK(obj->GetInlet(1)->GetDocLabel() == "right");

    REQUIRE(obj->NumOutputs() == 2);
    CHECK(obj->GetOutlet(0)->GetDocLabel() == "fromRight");
    CHECK(obj->GetOutlet(1)->GetDocLabel() == "fromLeft");

    REQUIRE(obj->GetParamDocs().size() == 1);
    CHECK(obj->GetParamDocs()[0].name == "right");
    CHECK(obj->GetParamDocs()[0].defaultValue == "0");
  }

  TEST_CASE("swap: inlet 0 accepts bang, int, float and list; inlet 1 only numbers (#476)") {
    gSwap op;
    const unsigned int hot = op.GetInlet(0)->GetAcceptedTypes();
    CHECK((hot & YSE::PATCHER::IT_BANG) != 0);
    CHECK((hot & YSE::PATCHER::IT_INT) != 0);
    CHECK((hot & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((hot & YSE::PATCHER::IT_LIST) != 0);

    const unsigned int cold = op.GetInlet(1)->GetAcceptedTypes();
    CHECK((cold & YSE::PATCHER::IT_INT) != 0);
    CHECK((cold & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((cold & YSE::PATCHER::IT_BANG) == 0);
    CHECK((cold & YSE::PATCHER::IT_LIST) == 0);
  }

  // ─── end to end, in a real patcher ──────────────────────────────────────────

  TEST_CASE("swap: reverses the operands of a real .- in a real patcher (#476)") {
    // The idiom the object exists for, through the public patcher surface and
    // through real objects rather than sinks: outlet 1 into the cold inlet of
    // the arithmetic and outlet 0 into its hot one. `.-` computes left - right,
    // so feeding it 10 through .swap against a stored 3 must give 3 - 10 = -7,
    // where the same wiring without .swap gives 10 - 3 = 7.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* swap = p.CreateObject(YSE::OBJ::G_SWAP);
    YSE::pHandle* minus = p.CreateObject(YSE::OBJ::G_SUBSTRACT);
    REQUIRE(swap != nullptr);
    REQUIRE(minus != nullptr);

    FloatSink sink;
    YSE::pHandle sinkHandle(&sink);

    p.Connect(swap, 1, minus, 1); // right outlet -> cold inlet, served first
    p.Connect(swap, 0, minus, 0); // left outlet  -> hot inlet, fires the subtract
    p.Connect(minus, 0, &sinkHandle, 0);

    swap->SetIntData(1, 3);
    swap->SetIntData(0, 10);
    REQUIRE(sink.gotFloat);
    CHECK(sink.received == doctest::Approx(-7.f));

    // Again with a different pair: a left-to-right implementation would be one
    // input stale here rather than wrong only once.
    swap->SetIntData(1, 100);
    swap->SetIntData(0, 40);
    CHECK(sink.received == doctest::Approx(60.f));
  }

  TEST_CASE("swap: the ordering guarantee is what makes that patch correct (#476)") {
    // The same patch with the two cords crossed — outlet 0 into the cold inlet
    // and outlet 1 into the hot one — is exactly the stale-operand bug, and it
    // has to be visible, or the test above would pass under any outlet order.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* swap = p.CreateObject(YSE::OBJ::G_SWAP);
    YSE::pHandle* minus = p.CreateObject(YSE::OBJ::G_SUBSTRACT);
    REQUIRE(swap != nullptr);
    REQUIRE(minus != nullptr);

    FloatSink sink;
    YSE::pHandle sinkHandle(&sink);

    p.Connect(swap, 0, minus, 1); // served *second*, after the subtract fired
    p.Connect(swap, 1, minus, 0);
    p.Connect(minus, 0, &sinkHandle, 0);

    swap->SetIntData(1, 3);
    swap->SetIntData(0, 10);
    REQUIRE(sink.gotFloat);
    // 10 - 0: the cold inlet still held its initial 0 when the subtract ran.
    CHECK(sink.received == doctest::Approx(10.f));
  }

  TEST_CASE("swap: a list drives the same patch in one message (#476)") {
    // The list method end to end: `10 3` sets both slots and releases, so the
    // downstream .- sees the same 3 - 10 the two separate messages produced.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* swap = p.CreateObject(YSE::OBJ::G_SWAP);
    YSE::pHandle* minus = p.CreateObject(YSE::OBJ::G_SUBSTRACT);
    REQUIRE(swap != nullptr);
    REQUIRE(minus != nullptr);

    FloatSink sink;
    YSE::pHandle sinkHandle(&sink);
    p.Connect(swap, 1, minus, 1);
    p.Connect(swap, 0, minus, 0);
    p.Connect(minus, 0, &sinkHandle, 0);

    swap->SetListData(0, "10 3");
    REQUIRE(sink.gotFloat);
    CHECK(sink.received == doctest::Approx(-7.f));
  }

  TEST_CASE("swap: the kind survives the trip through a patcher (#476)") {
    // The .fswap decision where a patch can see it: an int stays routable as an
    // int and a float arrives as a float, through the public surface.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* swap = p.CreateObject(YSE::OBJ::G_SWAP);
    REQUIRE(swap != nullptr);

    MultiSink outlet0; // carries the right slot
    MultiSink outlet1; // carries the left slot
    YSE::pHandle handle0(&outlet0);
    YSE::pHandle handle1(&outlet1);
    p.Connect(swap, 0, &handle0, 0);
    p.Connect(swap, 1, &handle1, 0);

    swap->SetFloatData(1, 1.5f);
    swap->SetIntData(0, 2);

    // The int that arrived on inlet 0 leaves outlet 1 still an int...
    CHECK(outlet1.gotInt);
    CHECK_FALSE(outlet1.gotFloat);
    CHECK(outlet1.intValue == 2);

    // ...and the float stored on inlet 1 leaves outlet 0 still a float.
    CHECK(outlet0.gotFloat);
    CHECK_FALSE(outlet0.gotInt);
    CHECK(outlet0.floatValue == doctest::Approx(1.5f));
  }
}
