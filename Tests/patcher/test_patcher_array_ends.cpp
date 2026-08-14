// Tests for .array.push / .array.pop / .array.shift / .array.unshift
// (issue #784) — the end-mutators of the array.* family, on the
// name-addressed value model .array settled (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **the array is bound from the creation argument.** An array never
//     travels down a cord, so ".array.push <name>" resolves the name once,
//     on the control thread, and an `array <name>` message is honoured only
//     when it names the array already bound — ArrayReferenceNames' bounded
//     compare, never a registry lookup on a message path.
//   - **an add is whole-or-nothing.** A list lands entirely, in the order
//     sent, or is refused entirely and counted — the mutating counterpart
//     of .array.at's whole-reply rule. A writer that lands emits the bound
//     array's reference, so the family chains; a refusal emits nothing.
//   - **a removal emits the departing element** — the difference from
//     .array's own "delete" — and an empty array bangs the empty outlet
//     instead: a queue-draining loop's exit condition, not an error.
//   - **the operations cross the control/audio boundary and nothing
//     allocates.** In-patcher delivery dispatches on T_DSP, so "the audio
//     thread pushes onto an array" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <limits>
#include <string>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayEnds.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::MultiSink;
using TestHelpers::Wire;
using YSE::PATCHER::gArray;
using YSE::PATCHER::gArrayPop;
using YSE::PATCHER::gArrayPush;
using YSE::PATCHER::gArrayShift;
using YSE::PATCHER::gArrayUnshift;

namespace {

  // An .array and one end-mutator on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sinks are declared before the objects so they are torn down last, while
  // the outlets wired to them still exist (see sinks.hpp on why that
  // matters). `out` takes outlet 0 — a writer's reference, a remover's
  // element — and `empty` takes a remover's empty outlet.
  template <typename ObjectT> struct Rig {
    MultiSink out;
    MultiSink empty;
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray array;
    ObjectT object;

    Rig(const std::string& patcherName, const std::string& name, bool wireEmpty = false) {
      p.SetName(patcherName);
      array.SetParent(&p);
      array.SetParams(name);
      object.SetParent(&p);
      object.SetParams(name);
      Wire(object, 0, out);
      if (wireEmpty) Wire(object, 1, empty);
    }

    void Store(const std::string& message) {
      array.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    // The array's contents as the list text they spell — read through the
    // .array on the same name, so the assertion goes through the shared
    // store rather than through the object under test.
    std::string Contents() {
      std::string text;
      for (std::size_t i = 0; i < array.Count(); i++) {
        if (!text.empty()) text += ' ';
        text += array.ElementAt(i);
      }
      return text;
    }
    void Reset() {
      out.reset();
      empty.reset();
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.ends: all four registered, with the family's inlets and outlets (#784)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* push = p.CreateObject(YSE::OBJ::G_ARRAY_PUSH);
    REQUIRE(push != nullptr);
    CHECK(std::string(push->Type()) == ".array.push");
    CHECK(push->GetInputs() == 2);
    CHECK(push->GetOutputs() == 1);

    YSE::pHandle* pop = p.CreateObject(YSE::OBJ::G_ARRAY_POP);
    REQUIRE(pop != nullptr);
    CHECK(std::string(pop->Type()) == ".array.pop");
    CHECK(pop->GetInputs() == 2);
    CHECK(pop->GetOutputs() == 2);

    YSE::pHandle* shift = p.CreateObject(YSE::OBJ::G_ARRAY_SHIFT);
    REQUIRE(shift != nullptr);
    CHECK(std::string(shift->Type()) == ".array.shift");
    CHECK(shift->GetInputs() == 2);
    CHECK(shift->GetOutputs() == 2);

    YSE::pHandle* unshift = p.CreateObject(YSE::OBJ::G_ARRAY_UNSHIFT);
    REQUIRE(unshift != nullptr);
    CHECK(std::string(unshift->Type()) == ".array.unshift");
    CHECK(unshift->GetInputs() == 2);
    CHECK(unshift->GetOutputs() == 1);
  }

  TEST_CASE("array.ends: all four appear in the registry's name list (#784)") {
    auto names = YSE::PATCHER::Register().AllNames();
    const char* expected[] = {YSE::OBJ::G_ARRAY_PUSH, YSE::OBJ::G_ARRAY_POP,
                              YSE::OBJ::G_ARRAY_SHIFT, YSE::OBJ::G_ARRAY_UNSHIFT};
    for (const char* type : expected) {
      bool found = false;
      for (const auto& name : names) {
        if (name == std::string(type)) found = true;
      }
      CHECK_MESSAGE(found, type);
    }
  }

  TEST_CASE("array.ends: a writer takes elements, a remover takes only the ask (#784)") {
    // A writer's hot inlet takes the element — an int, a float or list text —
    // and has no bang method: an add has to be told what to add. A remover's
    // hot inlet takes only the ask — a bang, or the "array <name>" reference
    // — and has no number methods: a removal is asked for, never addressed.
    // Both reference inlets take only list text.
    gArrayPush push;
    const unsigned int pushIn = push.GetInlet(0)->GetAcceptedTypes();
    CHECK((pushIn & YSE::PATCHER::IT_INT) != 0);
    CHECK((pushIn & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((pushIn & YSE::PATCHER::IT_LIST) != 0);
    CHECK((pushIn & YSE::PATCHER::IT_BANG) == 0);
    const unsigned int pushRef = push.GetInlet(1)->GetAcceptedTypes();
    CHECK((pushRef & YSE::PATCHER::IT_LIST) != 0);
    CHECK((pushRef & YSE::PATCHER::IT_BANG) == 0);
    CHECK((pushRef & YSE::PATCHER::IT_INT) == 0);
    CHECK((pushRef & YSE::PATCHER::IT_FLOAT) == 0);

    gArrayPop pop;
    const unsigned int popIn = pop.GetInlet(0)->GetAcceptedTypes();
    CHECK((popIn & YSE::PATCHER::IT_BANG) != 0);
    CHECK((popIn & YSE::PATCHER::IT_LIST) != 0);
    CHECK((popIn & YSE::PATCHER::IT_INT) == 0);
    CHECK((popIn & YSE::PATCHER::IT_FLOAT) == 0);
    const unsigned int popRef = pop.GetInlet(1)->GetAcceptedTypes();
    CHECK((popRef & YSE::PATCHER::IT_LIST) != 0);
    CHECK((popRef & YSE::PATCHER::IT_BANG) == 0);
  }

  // ─── push ───────────────────────────────────────────────────────────────────

  TEST_CASE("array.push: adds at the end and announces the array it changed (#784)") {
    // The object's whole point: values arriving on a cord land at the end of
    // the shared sequence, and each landing emits "array <name>" so the next
    // object in the chain acts on the array just grown. An int and a float
    // land as the text that spells them — the float visibly a float.
    Rig<gArrayPush> rig("aep784a", "a784a");

    rig.object.GetInlet(0)->SetInt(10, YSE::T_GUI);
    CHECK(rig.Contents() == "10");
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "array a784a");

    rig.Reset();
    rig.object.GetInlet(0)->SetFloat(7.5f, YSE::T_GUI);
    CHECK(rig.Contents() == "10 7.5");
    CHECK(rig.out.gotList);

    rig.Reset();
    const std::string symbol = "c4";
    rig.object.GetInlet(0)->SetList(symbol, YSE::T_GUI);
    CHECK(rig.Contents() == "10 7.5 c4");
    CHECK(rig.out.gotList);
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.push: a list lands whole, in the order sent (#784)") {
    Rig<gArrayPush> rig("aep784b", "a784b");
    rig.Store("append 1");

    rig.object.GetInlet(0)->SetList("2 3 4", YSE::T_GUI);
    CHECK(rig.Contents() == "1 2 3 4");
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "array a784b");
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.push: refused whole — a full array, and an over-long element (#784)") {
    // Whole-or-nothing, the mutating counterpart of .array.at's whole-reply
    // rule: when the array cannot take the entire list, nothing lands, one
    // refusal is counted, and nothing is emitted — a fragment of a list the
    // patch sent as one thing would be truncation by another name.
    Rig<gArrayPush> rig("aep784c", "a784c");
    for (int i = 0; i < 254; i++)
      rig.object.GetInlet(0)->SetInt(i, YSE::T_GUI);
    REQUIRE(rig.array.Count() == 254);

    rig.Reset();
    const std::uint64_t before = rig.object.Dropped();
    rig.object.GetInlet(0)->SetList("a b c", YSE::T_GUI);
    CHECK(rig.array.Count() == 254);
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.object.Dropped() == before + 1);

    // Two more still fit — the refusal was "not all of it", not "the array
    // is closed".
    rig.object.GetInlet(0)->SetList("x y", YSE::T_GUI);
    CHECK(rig.array.Count() == 256);
    CHECK(rig.out.gotList);

    // And on the full array a single element is refused too.
    rig.Reset();
    rig.object.GetInlet(0)->SetInt(99, YSE::T_GUI);
    CHECK(rig.array.Count() == 256);
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.object.Dropped() == before + 2);

    // An element past ELEMENT_CAPACITY refuses the whole message even with
    // room to spare.
    Rig<gArrayPush> rig2("aep784d", "a784d");
    const std::string overlong(gArrayPush::ELEMENT_CAPACITY + 1, 'x');
    const std::uint64_t before2 = rig2.object.Dropped();
    rig2.object.GetInlet(0)->SetList("ok " + overlong, YSE::T_GUI);
    CHECK(rig2.array.Count() == 0);
    CHECK_FALSE(rig2.out.gotList);
    CHECK(rig2.object.Dropped() == before2 + 1);
  }

  TEST_CASE("array.push: a reference is an identity, not an element (#784)") {
    // The bound array's own reference on the element inlet is a mis-wired
    // cord: refused and counted, so it shows up in Dropped() instead of
    // pushing the words "array" and "<name>" into the data. A non-finite
    // float has no spelling that reads back, so it is refused too.
    Rig<gArrayPush> rig("aep784e", "a784e");

    const std::uint64_t before = rig.object.Dropped();
    rig.object.GetInlet(0)->SetList("array a784e", YSE::T_GUI);
    CHECK(rig.array.Count() == 0);
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.object.Dropped() == before + 1);

    rig.object.GetInlet(0)->SetFloat(std::numeric_limits<float>::infinity(), YSE::T_GUI);
    CHECK(rig.array.Count() == 0);
    CHECK(rig.object.Dropped() == before + 2);
  }

  TEST_CASE("array.push: an unnamed object writes a private array, silently (#784)") {
    // Not "shares the empty name" — gArray's rule, inherited whole. The
    // write lands in a sequence of this object's own, and no reference
    // leaves: there is no name to pass on.
    MultiSink sink;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aep784f");
    gArrayPush g;
    g.SetParent(&p);
    Wire(g, 0, sink);

    CHECK(g.Address().empty());
    g.GetInlet(0)->SetInt(42, YSE::T_GUI);
    CHECK_FALSE(sink.gotList);
    CHECK(g.Dropped() == 0);
  }

  // ─── unshift ────────────────────────────────────────────────────────────────

  TEST_CASE("array.unshift: adds at the front, a list in the order sent (#784)") {
    // "unshift a b c" leaves a b c at the front — the whole list moves in
    // ahead of what was there, in the order it was written, not reversed.
    Rig<gArrayUnshift> rig("aeu784a", "u784a");
    rig.Store("append 9 10");

    rig.object.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(rig.Contents() == "1 9 10");
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "array u784a");

    rig.Reset();
    rig.object.GetInlet(0)->SetList("a b c", YSE::T_GUI);
    CHECK(rig.Contents() == "a b c 1 9 10");
    CHECK(rig.out.gotList);
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.unshift: refused whole when the array cannot take it all (#784)") {
    Rig<gArrayUnshift> rig("aeu784b", "u784b");
    for (int i = 0; i < 255; i++)
      rig.object.GetInlet(0)->SetInt(i, YSE::T_GUI);
    REQUIRE(rig.array.Count() == 255);
    const std::string front = rig.array.ElementAt(0);

    rig.Reset();
    const std::uint64_t before = rig.object.Dropped();
    rig.object.GetInlet(0)->SetList("a b", YSE::T_GUI);
    CHECK(rig.array.Count() == 255);
    CHECK(rig.array.ElementAt(0) == front);
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.object.Dropped() == before + 1);
  }

  // ─── pop and shift ──────────────────────────────────────────────────────────

  TEST_CASE("array.pop: removes the last element and emits it, typed (#784)") {
    // The difference from .array's own "delete": the departing element
    // leaves the object, typed the way the patcher spells it — an int as an
    // int, a float as a float, a symbol as one-token list text.
    Rig<gArrayPop> rig("aepp784a", "p784a", true);
    rig.Store("append 10 7.5 c4");

    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "c4");
    CHECK(rig.Contents() == "10 7.5");

    rig.Reset();
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.out.gotFloat);
    CHECK(rig.out.floatValue == doctest::Approx(7.5f));
    CHECK(rig.Contents() == "10");

    rig.Reset();
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 10);
    CHECK(rig.Contents().empty());
    CHECK_FALSE(rig.empty.gotBang);
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.pop: an empty array bangs the empty outlet (#784)") {
    // "Nothing left" is the queue-draining loop's exit condition, not an
    // error: the empty outlet bangs, the element outlet stays quiet, and
    // nothing is counted. An unnamed (private) array is always empty.
    Rig<gArrayPop> rig("aepp784b", "p784b", true);

    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.out.gotInt);
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.empty.gotBang);
    CHECK(rig.object.Dropped() == 0);

    MultiSink element;
    MultiSink empty;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aepp784c");
    gArrayPop unnamed;
    unnamed.SetParent(&p);
    Wire(unnamed, 0, element);
    Wire(unnamed, 1, empty);
    unnamed.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(empty.gotBang);
    CHECK_FALSE(element.gotInt);
  }

  TEST_CASE("array.shift: removes the first element, the rest move down (#784)") {
    Rig<gArrayShift> rig("aes784a", "s784a", true);
    rig.Store("append 10 20 30");

    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 10);
    CHECK(rig.Contents() == "20 30");

    rig.Reset();
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 20);
    CHECK(rig.Contents() == "30");

    rig.Reset();
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.out.intValue == 30);
    CHECK(rig.empty.gotBang);
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.ends: push into shift is a queue, push into pop is a stack (#784)") {
    // The use case the issue names, end to end: events pushed on one side
    // come off the other in arrival order (queue), or off the same side in
    // reverse (stack).
    Rig<gArrayShift> queue("aeq784a", "q784a", true);
    gArrayPush feeder;
    feeder.SetParent(&queue.p);
    feeder.SetParams("q784a");
    feeder.GetInlet(0)->SetList("60 64 67", YSE::T_GUI);

    queue.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(queue.out.intValue == 60);
    queue.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(queue.out.intValue == 64);
    queue.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(queue.out.intValue == 67);
    queue.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(queue.empty.gotBang);

    Rig<gArrayPop> stack("aeq784b", "q784b", true);
    gArrayPush pusher;
    pusher.SetParent(&stack.p);
    pusher.SetParams("q784b");
    pusher.GetInlet(0)->SetList("60 64 67", YSE::T_GUI);

    stack.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(stack.out.intValue == 67);
    stack.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(stack.out.intValue == 64);
    stack.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(stack.out.intValue == 60);
  }

  // ─── the reference ──────────────────────────────────────────────────────────

  TEST_CASE("array.pop: the reference asks on the trigger inlet, anything else refused (#784)") {
    // "array <name>" is the message an .array's reference outlet emits on a
    // bang; on the trigger inlet it removes — the family gesture. A
    // reference to an array this object is not bound to is refused, never
    // resolved: a registry lookup is a mutex, and this may be the audio
    // thread.
    Rig<gArrayPop> rig("aer784a", "r784a", true);
    rig.Store("append 10 4 7");

    const std::uint64_t before = rig.object.Dropped();
    rig.object.GetInlet(0)->SetList("array r784a", YSE::T_GUI);
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 7);
    CHECK(rig.Contents() == "10 4");
    CHECK(rig.object.Dropped() == before);

    rig.Reset();
    rig.object.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    rig.object.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotInt);
    CHECK(rig.object.Dropped() == before + 2);
  }

  TEST_CASE("array.ends: the reference inlet acknowledges its own array and nothing more (#784)") {
    // Wiring the array's reference outlet across keeps the patch readable,
    // and the acknowledgement is silent — the binding is the creation
    // argument, so there is nothing to set. Anything else there is refused,
    // which is what makes a mis-wired cord visible in Dropped(). One writer
    // and one remover, since the handling lives on the shared base.
    Rig<gArrayPush> push("aer784b", "r784b");
    const std::uint64_t pushBefore = push.object.Dropped();
    push.object.GetInlet(1)->SetList("array r784b", YSE::T_GUI);
    CHECK_FALSE(push.out.gotList);
    CHECK(push.object.Dropped() == pushBefore);
    push.object.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    push.object.GetInlet(1)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(push.object.Dropped() == pushBefore + 2);
    CHECK(push.array.Count() == 0);

    Rig<gArrayShift> shift("aer784c", "r784c", true);
    shift.Store("append 1 2");
    const std::uint64_t shiftBefore = shift.object.Dropped();
    shift.object.GetInlet(1)->SetList("array r784c", YSE::T_GUI);
    CHECK_FALSE(shift.out.gotInt);
    CHECK(shift.object.Dropped() == shiftBefore);
    shift.object.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK(shift.object.Dropped() == shiftBefore + 1);
    CHECK(shift.Contents() == "1 2");
  }

  TEST_CASE("array.ends: wired from the array's reference outlet, banging the array pops (#784)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: the .array's reference outlet into the trigger inlet gives the
    // family gesture — bang the array, out comes an element.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher p;
    p.create(2);
    p.name("aer784d");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "r784d");
    YSE::pHandle* pop = p.CreateObject(YSE::OBJ::G_ARRAY_POP, "r784d");
    REQUIRE(array != nullptr);
    REQUIRE(pop != nullptr);
    p.Connect(array, 1, pop, 0);
    p.Connect(pop, 0, &sinkHandle, 0);

    array->SetListData(0, "append 60 64 67");
    array->SetBang(0);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 67);

    sink.reset();
    array->SetBang(0);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 64);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.ends: the address form is the patcher's, and RefreshBinding follows it (#784)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aeb784a_before");

    gArrayUnshift g;
    g.SetParams("b784a");
    g.SetParent(&p);
    CHECK(g.ArrayName() == "b784a");
    CHECK(g.Address() == "aeb784a_before.b784a");
    CHECK(g.Reference() == "array b784a");

    // Idempotent: a rebind to the address it already has keeps the store.
    g.RefreshBinding();
    CHECK(g.Address() == "aeb784a_before.b784a");

    p.SetName("aeb784a_after");
    g.RefreshBinding();
    CHECK(g.Address() == "aeb784a_after.b784a");
    CHECK(g.Reference() == "array b784a");
  }

  TEST_CASE("array.ends: patcherImplementation::SetName re-anchors all four (#784)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by the
    // patcher, without anybody calling RefreshBinding by hand. The keeper
    // holds the old-address store (it is not in the patcher's object map, so
    // the rename does not touch it): before the rename the pop takes the
    // keeper's last element; after it, a push lands under the new prefix —
    // the keeper's array unchanged — and the pop drains what the push just
    // wrote.
    MultiSink popped;
    YSE::pHandle poppedHandle(&popped);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aeb784b_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("b784b");
    keeper.GetInlet(0)->SetList("append 10 4 7", YSE::T_GUI);

    YSE::pHandle* push = p.CreateObject(YSE::OBJ::G_ARRAY_PUSH, "b784b");
    YSE::pHandle* pop = p.CreateObject(YSE::OBJ::G_ARRAY_POP, "b784b");
    REQUIRE(push != nullptr);
    REQUIRE(pop != nullptr);
    p.Connect(pop, 0, &poppedHandle, 0);

    pop->SetBang(0);
    REQUIRE(popped.gotInt);
    CHECK(popped.intValue == 7);
    CHECK(keeper.Count() == 2);

    p.SetName("aeb784b_after");
    popped.reset();
    push->SetIntData(0, 99);
    CHECK(keeper.Count() == 2); // the keeper's array did not take the push
    pop->SetBang(0);
    REQUIRE(popped.gotInt);
    CHECK(popped.intValue == 99);
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.ends: a push and a pop over in-patcher delivery land on T_DSP (#784)") {
    // A .r feeding the object dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread pushes onto an array" is the ordinary
    // case, and the pop is one guard hold, one copied element and one send.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aed784a");

    YSE::pHandle* recvPush = p.CreateObject(YSE::OBJ::G_RECEIVE, "go784push");
    YSE::pHandle* recvPop = p.CreateObject(YSE::OBJ::G_RECEIVE, "go784pop");
    YSE::pHandle* push = p.CreateObject(YSE::OBJ::G_ARRAY_PUSH, "d784a");
    YSE::pHandle* pop = p.CreateObject(YSE::OBJ::G_ARRAY_POP, "d784a");
    REQUIRE(recvPush != nullptr);
    REQUIRE(recvPop != nullptr);
    REQUIRE(push != nullptr);
    REQUIRE(pop != nullptr);
    p.Connect(recvPush, 0, push, 0);
    p.Connect(recvPop, 0, pop, 0);
    p.Connect(pop, 0, &sinkHandle, 0);

    p.PassData(std::string("60 64"), "go784push", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);

    p.PassData(std::string("array d784a"), "go784pop", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 64);
  }

  TEST_CASE("array.ends: no message path allocates (#784)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path of both halves: the int, float and list pushes, the
    // unshift, the identity refusal on the element inlet, the pop with an
    // element and the pop of an empty array, the reference gesture, and the
    // acknowledgement and refusal on the reference inlets.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "array probeAE784";
    const std::string wrongName = "array somewhere_else_long";
    const std::string listPush = "31 32 33";
    const std::string symbolPush = "a_symbol_element_of_some_length";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aeprobe784");
    MultiSink pushOut;
    MultiSink element;
    MultiSink empty;
    gArrayPush push;
    gArrayUnshift unshift;
    gArrayPop pop;
    gArrayShift shift;
    push.SetParent(&p);
    push.SetParams("probeAE784");
    unshift.SetParent(&p);
    unshift.SetParams("probeAE784");
    pop.SetParent(&p);
    pop.SetParams("probeAE784");
    shift.SetParent(&p);
    shift.SetParams("probeAE784");
    Wire(push, 0, pushOut);
    Wire(pop, 0, element);
    Wire(pop, 1, empty);

    // Warm every path — including the sinks' assignments — so first-call
    // machinery is not what the probe catches.
    push.GetInlet(0)->SetInt(1, YSE::T_GUI);
    push.GetInlet(0)->SetFloat(2.5f, YSE::T_GUI);
    push.GetInlet(0)->SetList(listPush, YSE::T_GUI);
    push.GetInlet(0)->SetList(symbolPush, YSE::T_GUI);
    push.GetInlet(0)->SetList(reference, YSE::T_GUI);
    push.GetInlet(1)->SetList(reference, YSE::T_GUI);
    push.GetInlet(1)->SetList(wrongName, YSE::T_GUI);
    unshift.GetInlet(0)->SetInt(0, YSE::T_GUI);
    pop.GetInlet(0)->SetList(reference, YSE::T_GUI);
    pop.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    while (!empty.gotBang)
      pop.GetInlet(0)->SetBang(YSE::T_GUI);
    shift.GetInlet(0)->SetBang(YSE::T_GUI);

    const std::uint64_t pushBefore = push.Dropped();
    const std::uint64_t popBefore = pop.Dropped();
    pushOut.reset();
    element.reset();
    empty.reset();

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      push.GetInlet(0)->SetInt(60, YSE::T_DSP);
      push.GetInlet(0)->SetFloat(61.5f, YSE::T_DSP);
      push.GetInlet(0)->SetList(listPush, YSE::T_DSP);
      push.GetInlet(0)->SetList(symbolPush, YSE::T_DSP);
      push.GetInlet(0)->SetList(reference, YSE::T_DSP); // identity, refused
      push.GetInlet(1)->SetList(reference, YSE::T_DSP); // acknowledged
      push.GetInlet(1)->SetList(wrongName, YSE::T_DSP); // refused
      unshift.GetInlet(0)->SetInt(59, YSE::T_DSP);
      pop.GetInlet(0)->SetBang(YSE::T_DSP); // pops the symbol
      pop.GetInlet(0)->SetList(reference, YSE::T_DSP); // the gesture: pops 33
      pop.GetInlet(0)->SetList(wrongName, YSE::T_DSP); // refused
      shift.GetInlet(0)->SetBang(YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. The pushes landed and announced, the pops
    // emitted, and the refusals were counted.
    CHECK(pushOut.gotList);
    CHECK(element.gotList); // the symbol pop
    CHECK(push.Dropped() == pushBefore + 2);
    CHECK(pop.Dropped() == popBefore + 1);
  }

  TEST_CASE("array.ends: a drained queue bangs empty without allocating (#784)") {
    // The empty-outlet path under the probe as well: the exit condition of
    // the draining loop is exactly the path a patch hits on the audio
    // thread, over and over.
    if (!TestHelpers::probeCountsAllocations()) return;

    MultiSink element;
    MultiSink empty;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aeprobe784b");
    gArrayShift shift;
    shift.SetParent(&p);
    shift.SetParams("probeAEE784");
    Wire(shift, 0, element);
    Wire(shift, 1, empty);

    shift.GetInlet(0)->SetBang(YSE::T_GUI); // warm: already empty
    empty.reset();

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      shift.GetInlet(0)->SetBang(YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);
    CHECK(empty.gotBang);
    CHECK(shift.Dropped() == 0);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.ends: params survive a DumpJSON / ParseJSON round trip (#784)") {
    const char* types[] = {YSE::OBJ::G_ARRAY_PUSH, YSE::OBJ::G_ARRAY_POP, YSE::OBJ::G_ARRAY_SHIFT,
                           YSE::OBJ::G_ARRAY_UNSHIFT};
    for (const char* type : types) {
      CAPTURE(type);
      YSE::patcher src;
      src.create(2);
      YSE::pHandle* h = src.CreateObject(type, "notes784");
      REQUIRE(h != nullptr);
      const std::string json = src.DumpJSON();

      YSE::patcher loaded;
      loaded.create(2);
      loaded.ParseJSON(json);
      REQUIRE(loaded.Objects() == 1);

      YSE::pHandle* copy = loaded.GetHandleFromList(0);
      REQUIRE(copy != nullptr);
      CHECK(std::string(copy->Type()) == std::string(type));
      CHECK(copy->GetParams() == std::string("notes784"));
      CHECK(copy->GetInputs() == 2);
    }
  }

  TEST_CASE("array.ends: all four carry complete documentation metadata (#784)") {
    gArrayPush push;
    gArrayPop pop;
    gArrayShift shift;
    gArrayUnshift unshift;
    YSE::PATCHER::pObject* objects[] = {&push, &pop, &shift, &unshift};
    for (YSE::PATCHER::pObject* g : objects) {
      CAPTURE(g->Type());
      CHECK_FALSE(g->GetDescription().empty());
      CHECK(g->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
      const auto& docs = g->GetParamDocs();
      REQUIRE(docs.size() == 1);
      CHECK(docs[0].name == "name");
    }
  }
}
