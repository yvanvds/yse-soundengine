// Tests for .array.insert / .array.remove (issue #785) — the
// position-mutators of the array.* family, on the name-addressed value model
// .array settled (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **the array is bound from the creation argument.** An array never
//     travels down a cord, so ".array.insert <name>" resolves the name once,
//     on the control thread, and an `array <name>` message is honoured only
//     when it names the array already bound — ArrayReferenceNames' bounded
//     compare, never a registry lookup on a message path.
//   - **the position arrives on a cord, and is applied at the moment of the
//     operation.** That is the whole reason these objects exist — .array's
//     own insert/delete need the index inside the message text. Zero-based,
//     never wrapped and never clamped: an insert past the end is a counted
//     refusal (a pure mutation), a remove past the end is a miss on an
//     outlet (a fetch that also erases — .array.at's rule).
//   - **an insert is whole-or-nothing** — a list lands entirely, in the
//     order sent, at the position, or is refused entirely and counted — and
//     an insert that lands emits the bound array's reference, so the family
//     chains.
//   - **a removal emits the departing element** — the difference from
//     .array's own "delete" — read and erased under one hold of the store's
//     guard.
//   - **the operations cross the control/audio boundary and nothing
//     allocates.** In-patcher delivery dispatches on T_DSP, so "the audio
//     thread edits an array" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <limits>
#include <string>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayPosition.h"
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
using YSE::PATCHER::gArrayInsert;
using YSE::PATCHER::gArrayRemove;

namespace {

  // An .array and one position-mutator on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sinks are declared before the objects so they are torn down last, while
  // the outlets wired to them still exist (see sinks.hpp on why that
  // matters). `out` takes outlet 0 — the insert's reference, the remove's
  // element — and `miss` takes the remove's miss outlet.
  template <typename ObjectT> struct Rig {
    MultiSink out;
    MultiSink miss;
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray array;
    ObjectT object;

    Rig(const std::string& patcherName, const std::string& params, bool wireMiss = false) {
      p.SetName(patcherName);
      object.SetParent(&p);
      object.SetParams(params);
      array.SetParent(&p);
      array.SetParams(object.ArrayName());
      Wire(object, 0, out);
      if (wireMiss) Wire(object, 1, miss);
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
      miss.reset();
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.position: both registered, with their inlets and outlets (#785)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* insert = p.CreateObject(YSE::OBJ::G_ARRAY_INSERT);
    REQUIRE(insert != nullptr);
    CHECK(std::string(insert->Type()) == ".array.insert");
    CHECK(insert->GetInputs() == 3);
    CHECK(insert->GetOutputs() == 1);

    YSE::pHandle* remove = p.CreateObject(YSE::OBJ::G_ARRAY_REMOVE);
    REQUIRE(remove != nullptr);
    CHECK(std::string(remove->Type()) == ".array.remove");
    CHECK(remove->GetInputs() == 2);
    CHECK(remove->GetOutputs() == 2);

    auto names = YSE::PATCHER::Register().AllNames();
    const char* expected[] = {YSE::OBJ::G_ARRAY_INSERT, YSE::OBJ::G_ARRAY_REMOVE};
    for (const char* type : expected) {
      bool found = false;
      for (const auto& name : names) {
        if (name == std::string(type)) found = true;
      }
      CHECK_MESSAGE(found, type);
    }
  }

  TEST_CASE("array.position: the element triggers, the position is cold, the ask is hot (#785)") {
    // The insert follows the Max idiom — position on the right, element on
    // the left triggers — and has no bang method: an insert has to be told
    // what to insert. Its index inlet takes numbers only. The remove takes
    // the position on the trigger inlet itself, because naming the position
    // is the ask. Both reference inlets take only list text.
    gArrayInsert insert;
    const unsigned int element = insert.GetInlet(0)->GetAcceptedTypes();
    CHECK((element & YSE::PATCHER::IT_INT) != 0);
    CHECK((element & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((element & YSE::PATCHER::IT_LIST) != 0);
    CHECK((element & YSE::PATCHER::IT_BANG) == 0);
    const unsigned int position = insert.GetInlet(1)->GetAcceptedTypes();
    CHECK((position & YSE::PATCHER::IT_INT) != 0);
    CHECK((position & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((position & YSE::PATCHER::IT_LIST) == 0);
    CHECK((position & YSE::PATCHER::IT_BANG) == 0);
    const unsigned int reference = insert.GetInlet(2)->GetAcceptedTypes();
    CHECK((reference & YSE::PATCHER::IT_LIST) != 0);
    CHECK((reference & YSE::PATCHER::IT_INT) == 0);
    CHECK((reference & YSE::PATCHER::IT_BANG) == 0);

    gArrayRemove remove;
    const unsigned int ask = remove.GetInlet(0)->GetAcceptedTypes();
    CHECK((ask & YSE::PATCHER::IT_BANG) != 0);
    CHECK((ask & YSE::PATCHER::IT_INT) != 0);
    CHECK((ask & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((ask & YSE::PATCHER::IT_LIST) != 0);
    const unsigned int removeRef = remove.GetInlet(1)->GetAcceptedTypes();
    CHECK((removeRef & YSE::PATCHER::IT_LIST) != 0);
    CHECK((removeRef & YSE::PATCHER::IT_INT) == 0);
  }

  // ─── insert ─────────────────────────────────────────────────────────────────

  TEST_CASE("array.insert: adds at the stored position, the rest shift up (#785)") {
    // The object's whole point: an element arriving on a cord lands in the
    // middle of the shared sequence, at the position the creation argument
    // seeded, and each landing emits "array <name>" so the next object in
    // the chain acts on the array just grown. An int and a float land as the
    // text that spells them — the float visibly a float.
    Rig<gArrayInsert> rig("api785a", "i785a 1");
    rig.Store("append 10 20 30");
    CHECK(rig.object.Index() == 1);

    rig.object.GetInlet(0)->SetInt(99, YSE::T_GUI);
    CHECK(rig.Contents() == "10 99 20 30");
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "array i785a");

    rig.Reset();
    rig.object.GetInlet(0)->SetFloat(7.5f, YSE::T_GUI);
    CHECK(rig.Contents() == "10 7.5 99 20 30");
    CHECK(rig.out.gotList);

    rig.Reset();
    const std::string symbol = "c4";
    rig.object.GetInlet(0)->SetList(symbol, YSE::T_GUI);
    CHECK(rig.Contents() == "10 c4 7.5 99 20 30");
    CHECK(rig.out.gotList);
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.insert: the position arrives on the cold inlet (#785)") {
    // The Max idiom, right to left: an int on the index inlet stores the
    // position silently, the next element lands there. A float truncates —
    // Max's float method on an int attribute. Inserting at the length
    // appends; a negative position is refused and does not move the stored
    // one.
    Rig<gArrayInsert> rig("api785b", "i785b");
    rig.Store("append a b c");

    rig.object.GetInlet(1)->SetInt(2, YSE::T_GUI);
    CHECK(rig.object.Index() == 2);
    CHECK_FALSE(rig.out.gotList); // storing a position emits nothing
    rig.object.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(rig.Contents() == "a b 1 c");

    // The length appends — positions run 0..count inclusive.
    rig.object.GetInlet(1)->SetInt(4, YSE::T_GUI);
    rig.object.GetInlet(0)->SetInt(2, YSE::T_GUI);
    CHECK(rig.Contents() == "a b 1 c 2");

    // A float position truncates to the int below it.
    rig.object.GetInlet(1)->SetFloat(1.7f, YSE::T_GUI);
    CHECK(rig.object.Index() == 1);

    // A negative position is refused, and the stored one does not move.
    const std::uint64_t before = rig.object.Dropped();
    rig.object.GetInlet(1)->SetInt(-1, YSE::T_GUI);
    CHECK(rig.object.Index() == 1);
    rig.object.GetInlet(1)->SetFloat(-0.5f, YSE::T_GUI);
    CHECK(rig.object.Index() == 1);
    rig.object.GetInlet(1)->SetFloat(std::numeric_limits<float>::quiet_NaN(), YSE::T_GUI);
    CHECK(rig.object.Index() == 1);
    CHECK(rig.object.Dropped() == before + 3);
  }

  TEST_CASE("array.insert: a list lands whole at the position, in the order sent (#785)") {
    Rig<gArrayInsert> rig("api785c", "i785c 1");
    rig.Store("append 10 20");

    rig.object.GetInlet(0)->SetList("a b c", YSE::T_GUI);
    CHECK(rig.Contents() == "10 a b c 20");
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "array i785c");
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE(
      "array.insert: refused whole — past the end, a full array, an over-long element (#785)") {
    // Whole-or-nothing, the end-writers' rule at a position: when the
    // position is past the end or the array cannot take the entire list,
    // nothing lands, one refusal is counted, and nothing is emitted — never
    // a clamp, never a fragment.
    Rig<gArrayInsert> rig("api785d", "i785d");
    rig.Store("append 10 20");

    // Past the end: count is 2, so 3 is out of range where 2 would append.
    rig.object.GetInlet(1)->SetInt(3, YSE::T_GUI);
    const std::uint64_t before = rig.object.Dropped();
    rig.object.GetInlet(0)->SetInt(99, YSE::T_GUI);
    CHECK(rig.Contents() == "10 20");
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.object.Dropped() == before + 1);

    // An element past ELEMENT_CAPACITY refuses the whole message even with
    // room to spare.
    rig.object.GetInlet(1)->SetInt(0, YSE::T_GUI);
    const std::string overlong(gArrayInsert::ELEMENT_CAPACITY + 1, 'x');
    rig.object.GetInlet(0)->SetList("ok " + overlong, YSE::T_GUI);
    CHECK(rig.Contents() == "10 20");
    CHECK(rig.object.Dropped() == before + 2);

    // A full array refuses a list it cannot take whole, and still takes one
    // that fits — the refusal was "not all of it", not "the array is
    // closed".
    Rig<gArrayInsert> rig2("api785e", "i785e");
    for (int i = 0; i < 254; i++)
      rig2.object.GetInlet(0)->SetInt(i, YSE::T_GUI);
    REQUIRE(rig2.array.Count() == 254);
    rig2.Reset();
    const std::uint64_t before2 = rig2.object.Dropped();
    rig2.object.GetInlet(0)->SetList("a b c", YSE::T_GUI);
    CHECK(rig2.array.Count() == 254);
    CHECK_FALSE(rig2.out.gotList);
    CHECK(rig2.object.Dropped() == before2 + 1);
    rig2.object.GetInlet(0)->SetList("x y", YSE::T_GUI);
    CHECK(rig2.array.Count() == 256);
    CHECK(rig2.out.gotList);
  }

  TEST_CASE("array.insert: a reference is an identity, not an element (#785)") {
    // The bound array's own reference on the element inlet is a mis-wired
    // cord: refused and counted, so it shows up in Dropped() instead of
    // inserting the words "array" and "<name>" into the data. A non-finite
    // float has no spelling that reads back, so it is refused too.
    Rig<gArrayInsert> rig("api785f", "i785f");

    const std::uint64_t before = rig.object.Dropped();
    rig.object.GetInlet(0)->SetList("array i785f", YSE::T_GUI);
    CHECK(rig.array.Count() == 0);
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.object.Dropped() == before + 1);

    rig.object.GetInlet(0)->SetFloat(std::numeric_limits<float>::infinity(), YSE::T_GUI);
    CHECK(rig.array.Count() == 0);
    CHECK(rig.object.Dropped() == before + 2);
  }

  TEST_CASE("array.insert: an unnamed object writes a private array, silently (#785)") {
    // Not "shares the empty name" — gArray's rule, inherited whole. The
    // write lands in a sequence of this object's own, and no reference
    // leaves: there is no name to pass on.
    MultiSink sink;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("api785g");
    gArrayInsert g;
    g.SetParent(&p);
    Wire(g, 0, sink);

    CHECK(g.Address().empty());
    g.GetInlet(0)->SetInt(42, YSE::T_GUI);
    CHECK_FALSE(sink.gotList);
    CHECK(g.Dropped() == 0);
  }

  // ─── remove ─────────────────────────────────────────────────────────────────

  TEST_CASE("array.remove: drops at the position and emits the departing element (#785)") {
    // The difference from .array's own "delete": the departing element
    // leaves the object, typed the way the patcher spells it, and every
    // element behind the gap moves down one. An int stores the position, so
    // a bang afterwards removes at the same place — the renumbering makes
    // that a different element each time.
    Rig<gArrayRemove> rig("apr785a", "r785a", true);
    rig.Store("append 10 7.5 c4");

    rig.object.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK(rig.out.gotFloat);
    CHECK(rig.out.floatValue == doctest::Approx(7.5f));
    CHECK(rig.Contents() == "10 c4");
    CHECK(rig.object.Index() == 1);

    rig.Reset();
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "c4");
    CHECK(rig.Contents() == "10");

    rig.Reset();
    rig.object.GetInlet(0)->SetInt(0, YSE::T_GUI);
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 10);
    CHECK(rig.Contents().empty());
    CHECK_FALSE(rig.miss.gotBang);
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.remove: a position the array does not have is a miss, on an outlet (#785)") {
    // A remove is a fetch that also erases, so out of range is .array.at's
    // miss — one bang, nothing removed, nothing counted — kept on an outlet
    // so a patch editing by position can see the edit did not land. The
    // stored position still moves: a miss is a property of the array at that
    // moment, so the same ask lands once the array has grown. A negative
    // position is malformed, not a miss: refused and counted, no outlet.
    Rig<gArrayRemove> rig("apr785b", "r785b", true);
    rig.Store("append a b c");

    rig.object.GetInlet(0)->SetInt(3, YSE::T_GUI);
    CHECK(rig.miss.gotBang);
    CHECK_FALSE(rig.out.gotInt);
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.Contents() == "a b c");
    CHECK(rig.object.Index() == 3);
    CHECK(rig.object.Dropped() == 0);

    // The array grows to four elements; the stored position now names one.
    rig.Reset();
    rig.Store("append d");
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "d");
    CHECK(rig.Contents() == "a b c");

    // An empty array misses on every position.
    Rig<gArrayRemove> empty("apr785c", "r785c", true);
    empty.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(empty.miss.gotBang);
    CHECK(empty.object.Dropped() == 0);

    // Negative: refused, counted, and neither outlet fires.
    rig.Reset();
    const std::uint64_t before = rig.object.Dropped();
    rig.object.GetInlet(0)->SetInt(-2, YSE::T_GUI);
    CHECK_FALSE(rig.miss.gotBang);
    CHECK_FALSE(rig.out.gotInt);
    CHECK(rig.object.Index() == 3); // and the stored position did not move
    CHECK(rig.object.Dropped() == before + 1);
  }

  TEST_CASE("array.remove: the reference asks on the trigger inlet, anything else refused (#785)") {
    // "array <name>" is the message an .array's reference outlet emits on a
    // bang; on the trigger inlet it removes at the stored position — the
    // family gesture. A reference to an array this object is not bound to is
    // refused, never resolved, and a multi-position remove is not offered:
    // delete renumbers, so a list of positions would name different elements
    // after each erase than it did when it was sent.
    Rig<gArrayRemove> rig("apr785d", "r785d 1", true);
    rig.Store("append 10 4 7");

    const std::uint64_t before = rig.object.Dropped();
    rig.object.GetInlet(0)->SetList("array r785d", YSE::T_GUI);
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 4);
    CHECK(rig.Contents() == "10 7");
    CHECK(rig.object.Dropped() == before);

    rig.Reset();
    rig.object.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    rig.object.GetInlet(0)->SetList("0 1", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotInt);
    CHECK(rig.Contents() == "10 7");
    CHECK(rig.object.Dropped() == before + 2);
  }

  TEST_CASE("array.position: the reference inlet acknowledges its own array, nothing more (#785)") {
    // Wiring the array's reference outlet across keeps the patch readable,
    // and the acknowledgement is silent — the binding is the creation
    // argument, so there is nothing to set. Anything else there is refused,
    // which is what makes a mis-wired cord visible in Dropped().
    Rig<gArrayInsert> insert("apx785a", "x785a");
    const std::uint64_t insertBefore = insert.object.Dropped();
    insert.object.GetInlet(2)->SetList("array x785a", YSE::T_GUI);
    CHECK_FALSE(insert.out.gotList);
    CHECK(insert.object.Dropped() == insertBefore);
    insert.object.GetInlet(2)->SetList("array somewhere_else", YSE::T_GUI);
    insert.object.GetInlet(2)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(insert.object.Dropped() == insertBefore + 2);
    CHECK(insert.array.Count() == 0);

    Rig<gArrayRemove> remove("apx785b", "x785b", true);
    remove.Store("append 1 2");
    const std::uint64_t removeBefore = remove.object.Dropped();
    remove.object.GetInlet(1)->SetList("array x785b", YSE::T_GUI);
    CHECK_FALSE(remove.out.gotInt);
    CHECK(remove.object.Dropped() == removeBefore);
    remove.object.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK(remove.object.Dropped() == removeBefore + 1);
    CHECK(remove.Contents() == "1 2");
  }

  TEST_CASE("array.position: insert then remove at one position are exact inverses (#785)") {
    // The issue's framing, exercised literally: what insert put at the
    // position is what remove takes out of it, and the array comes back as
    // it stood — one .array.insert and one .array.remove on one name, driven
    // at the same stored position.
    Rig<gArrayInsert> rig("apv785a", "v785a 1");
    rig.Store("append 10 20 30");

    gArrayRemove remover;
    MultiSink element;
    remover.SetParent(&rig.p);
    remover.SetParams("v785a 1");
    Wire(remover, 0, element);

    rig.object.GetInlet(0)->SetInt(99, YSE::T_GUI);
    CHECK(rig.Contents() == "10 99 20 30");
    remover.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(element.gotInt);
    CHECK(element.intValue == 99);
    CHECK(rig.Contents() == "10 20 30");
  }

  // ─── parameters, binding, and the rename hook ───────────────────────────────

  TEST_CASE(
      "array.position: the creation arguments seed name and position, and reset whole (#785)") {
    // ".array.insert <name> <position>" — the second argument is where the
    // first operation applies before any int has moved it, exactly as
    // .array.at seeds its index. A re-parse resets both: SetParams("") must
    // not keep pointing at wherever the previous arguments left it. A
    // negative planted position is malformed at the moment of use: refused,
    // not a miss.
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("apc785a");

    gArrayInsert insert;
    insert.SetParent(&p);
    insert.SetParams("c785a 2");
    CHECK(insert.ArrayName() == "c785a");
    CHECK(insert.Address() == "apc785a.c785a");
    CHECK(insert.Index() == 2);
    CHECK(insert.Reference() == "array c785a");

    insert.SetParams("");
    CHECK(insert.ArrayName().empty());
    CHECK(insert.Address().empty());
    CHECK(insert.Index() == 0);
    CHECK(insert.Reference().empty());

    gArrayRemove remove;
    MultiSink element;
    MultiSink miss;
    remove.SetParent(&p);
    remove.SetParams("c785b -3");
    Wire(remove, 0, element);
    Wire(remove, 1, miss);
    const std::uint64_t before = remove.Dropped();
    remove.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(miss.gotBang);
    CHECK(remove.Dropped() == before + 1);
  }

  TEST_CASE("array.position: params survive a DumpJSON / ParseJSON round trip (#785)") {
    const char* types[] = {YSE::OBJ::G_ARRAY_INSERT, YSE::OBJ::G_ARRAY_REMOVE};
    for (const char* type : types) {
      CAPTURE(type);
      YSE::patcher src;
      src.create(2);
      YSE::pHandle* h = src.CreateObject(type, "notes785 2");
      REQUIRE(h != nullptr);
      const std::string json = src.DumpJSON();

      YSE::patcher loaded;
      loaded.create(2);
      loaded.ParseJSON(json);
      REQUIRE(loaded.Objects() == 1);

      YSE::pHandle* copy = loaded.GetHandleFromList(0);
      REQUIRE(copy != nullptr);
      CHECK(std::string(copy->Type()) == std::string(type));
      CHECK(copy->GetParams() == std::string("notes785 2"));
    }
  }

  TEST_CASE("array.position: patcherImplementation::SetName re-anchors both (#785)") {
    // The rename dispatch itself: an object created *inside* a patcher must
    // be re-anchored by the patcher, without anybody calling RefreshBinding
    // by hand. The keeper holds the old-address store (it is not in the
    // patcher's object map, so the rename does not touch it): after the
    // rename, an insert lands under the new prefix — the keeper's array
    // unchanged — and the remove takes back what the insert just wrote.
    MultiSink removed;
    YSE::pHandle removedHandle(&removed);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("apn785_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("n785a");
    keeper.GetInlet(0)->SetList("append 10 4 7", YSE::T_GUI);

    YSE::pHandle* insert = p.CreateObject(YSE::OBJ::G_ARRAY_INSERT, "n785a");
    YSE::pHandle* remove = p.CreateObject(YSE::OBJ::G_ARRAY_REMOVE, "n785a");
    REQUIRE(insert != nullptr);
    REQUIRE(remove != nullptr);
    p.Connect(remove, 0, &removedHandle, 0);

    remove->SetIntData(0, 0);
    REQUIRE(removed.gotInt);
    CHECK(removed.intValue == 10);
    CHECK(keeper.Count() == 2);

    p.SetName("apn785_after");
    removed.reset();
    insert->SetIntData(0, 99);
    CHECK(keeper.Count() == 2); // the keeper's array did not take the insert
    remove->SetIntData(0, 0);
    REQUIRE(removed.gotInt);
    CHECK(removed.intValue == 99);
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE(
      "array.position: an insert and a remove over in-patcher delivery land on T_DSP (#785)") {
    // A .r feeding the object dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread edits an array" is the ordinary case:
    // the insert is one guard hold and one send, the remove one guard hold,
    // one copied element and one send.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("apd785a");

    YSE::pHandle* recvInsert = p.CreateObject(YSE::OBJ::G_RECEIVE, "go785ins");
    YSE::pHandle* recvRemove = p.CreateObject(YSE::OBJ::G_RECEIVE, "go785rem");
    YSE::pHandle* insert = p.CreateObject(YSE::OBJ::G_ARRAY_INSERT, "d785a");
    YSE::pHandle* remove = p.CreateObject(YSE::OBJ::G_ARRAY_REMOVE, "d785a 1");
    REQUIRE(recvInsert != nullptr);
    REQUIRE(recvRemove != nullptr);
    REQUIRE(insert != nullptr);
    REQUIRE(remove != nullptr);
    p.Connect(recvInsert, 0, insert, 0);
    p.Connect(recvRemove, 0, remove, 0);
    p.Connect(remove, 0, &sinkHandle, 0);

    p.PassData(std::string("60 64 67"), "go785ins", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);

    p.PassData(std::string("array d785a"), "go785rem", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 64);
  }

  TEST_CASE("array.position: no message path allocates (#785)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path of both objects: the int, float, symbol and list
    // inserts, the position store on the cold inlet, the past-the-end and
    // identity refusals, the remove that hits, the remove that misses, the
    // reference gesture, and the acknowledgement and refusal on the
    // reference inlets.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "array probeAP785";
    const std::string wrongName = "array somewhere_else_long";
    const std::string listInsert = "31 32 33";
    const std::string symbolInsert = "a_symbol_element_of_some_length";
    const std::string garbage = "frobnicate a b";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("approbe785");
    MultiSink insertOut;
    MultiSink element;
    MultiSink miss;
    gArrayInsert insert;
    gArrayRemove remove;
    insert.SetParent(&p);
    insert.SetParams("probeAP785");
    remove.SetParent(&p);
    remove.SetParams("probeAP785");
    Wire(insert, 0, insertOut);
    Wire(remove, 0, element);
    Wire(remove, 1, miss);

    // Warm every path — including the sinks' assignments — so first-call
    // machinery is not what the probe catches.
    insert.GetInlet(0)->SetInt(1, YSE::T_GUI);
    insert.GetInlet(0)->SetFloat(2.5f, YSE::T_GUI);
    insert.GetInlet(0)->SetList(listInsert, YSE::T_GUI);
    insert.GetInlet(0)->SetList(symbolInsert, YSE::T_GUI);
    insert.GetInlet(0)->SetList(reference, YSE::T_GUI);
    insert.GetInlet(1)->SetInt(0, YSE::T_GUI);
    insert.GetInlet(2)->SetList(reference, YSE::T_GUI);
    insert.GetInlet(2)->SetList(wrongName, YSE::T_GUI);
    remove.GetInlet(0)->SetInt(0, YSE::T_GUI);
    remove.GetInlet(0)->SetList(reference, YSE::T_GUI);
    remove.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    remove.GetInlet(0)->SetList(garbage, YSE::T_GUI);
    remove.GetInlet(1)->SetList(reference, YSE::T_GUI);
    remove.GetInlet(0)->SetInt(200, YSE::T_GUI); // a miss
    remove.GetInlet(0)->SetInt(0, YSE::T_GUI);

    const std::uint64_t insertBefore = insert.Dropped();
    const std::uint64_t removeBefore = remove.Dropped();
    insertOut.reset();
    element.reset();
    miss.reset();

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      insert.GetInlet(1)->SetInt(0, YSE::T_DSP); // store the position
      insert.GetInlet(0)->SetInt(60, YSE::T_DSP);
      insert.GetInlet(0)->SetFloat(61.5f, YSE::T_DSP);
      insert.GetInlet(0)->SetList(listInsert, YSE::T_DSP);
      insert.GetInlet(0)->SetList(symbolInsert, YSE::T_DSP);
      insert.GetInlet(0)->SetList(reference, YSE::T_DSP); // identity, refused
      insert.GetInlet(1)->SetInt(250, YSE::T_DSP); // stored — positions are cheap
      insert.GetInlet(0)->SetInt(9, YSE::T_DSP); // past the end, refused
      insert.GetInlet(1)->SetInt(0, YSE::T_DSP);
      insert.GetInlet(2)->SetList(reference, YSE::T_DSP); // acknowledged
      insert.GetInlet(2)->SetList(wrongName, YSE::T_DSP); // refused
      remove.GetInlet(0)->SetInt(0, YSE::T_DSP); // hit
      remove.GetInlet(0)->SetList(reference, YSE::T_DSP); // the gesture: hit
      remove.GetInlet(0)->SetInt(200, YSE::T_DSP); // miss
      remove.GetInlet(0)->SetList(wrongName, YSE::T_DSP); // refused
      remove.GetInlet(0)->SetList(garbage, YSE::T_DSP); // refused
      remove.GetInlet(1)->SetList(reference, YSE::T_DSP); // acknowledged
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. The inserts landed and announced, the removes
    // emitted and missed, and the refusals were counted.
    CHECK(insertOut.gotList);
    CHECK(element.gotInt);
    CHECK(miss.gotBang);
    CHECK(insert.Dropped() == insertBefore + 3);
    CHECK(remove.Dropped() == removeBefore + 2);
  }

  // ─── documentation ──────────────────────────────────────────────────────────

  TEST_CASE("array.position: both carry complete documentation metadata (#785)") {
    gArrayInsert insert;
    gArrayRemove remove;
    YSE::PATCHER::pObject* objects[] = {&insert, &remove};
    for (YSE::PATCHER::pObject* g : objects) {
      CAPTURE(g->Type());
      CHECK_FALSE(g->GetDescription().empty());
      CHECK(g->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
      const auto& docs = g->GetParamDocs();
      REQUIRE(docs.size() == 2);
      CHECK(docs[0].name == "name");
      CHECK(docs[1].name == "index");
    }
  }
}
