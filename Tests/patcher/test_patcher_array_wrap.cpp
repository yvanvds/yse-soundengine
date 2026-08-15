// Tests for .array.wrap (issue #809) — Max's array.wrap ("index an array with
// wrapping") on the name-addressed value model .array settled (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **every index lands.** The family treats an index as a position — out
//     of range is a miss on .array and .array.at, negative a counted refusal,
//     decided once on arrayStore — and this is the object that exists to
//     provide the alternative: the index is taken modulo the length, so 5
//     into a three-element array reads position 2 and -1 reads the last
//     element. If this object ever misses or refuses on a well-formed index,
//     it has stopped being the thing #809 asked for.
//   - **the wrap is mathematical, not C's.** C++'s % truncates toward zero,
//     so a negative remainder is one length below the position it names; the
//     object adds the length back. INT_MIN and INT_MAX must wrap like any
//     other index rather than overflow.
//   - **the array is bound from the creation argument.** An array never
//     travels down a cord, so ".array.wrap <name> [<index>]" resolves the
//     name once, on the control thread, and an `array <name>` message is
//     honoured only when it names the array already bound —
//     ArrayReferenceNames' bounded compare, never a registry lookup on a
//     message path.
//   - **a fetch is atomic.** The length is read and every requested position
//     wrapped against it under one hold of the store's guard, the send after
//     it is released — so the modulus is the length that same hold read and
//     the reply is the array as it stood at the trigger.
//   - **an empty array bangs the empty outlet.** There is nothing to wrap
//     onto: a modulus of zero names no position, the one miss the wrapping
//     cannot remove.
//   - **nothing on a message path allocates.** In-patcher delivery
//     dispatches on T_DSP, so "the audio thread asks for an element" is the
//     ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <limits>
#include <string>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayWrap.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::BangSink;
using TestHelpers::MultiSink;
using TestHelpers::Wire;
using YSE::PATCHER::gArray;
using YSE::PATCHER::gArrayWrap;

namespace {

  // An .array and one .array.wrap on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sinks are declared before the objects so they are torn down last, while
  // the outlets wired to them still exist (see sinks.hpp on why that
  // matters).
  struct Rig {
    MultiSink element; // outlet 0: the fetched element, typed by its spelling
    BangSink empty; // outlet 1: bang when there was nothing to wrap onto
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray array;
    gArrayWrap object;

    Rig(const std::string& patcherName, const std::string& name,
        const std::string& params = std::string()) {
      p.SetName(patcherName);
      array.SetParent(&p);
      array.SetParams(name);
      object.SetParent(&p);
      object.SetParams(params.empty() ? name : params);
      Wire(object, 0, element);
      Wire(object, 1, empty);
    }

    void Store(const std::string& message) {
      array.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Reset() {
      element.reset();
      empty.bangCount = 0;
      empty.gotBang = false;
    }
    // One int fetch, answered as the int that arrived. Every element the
    // cases below store is int-spelled, so an int is what has to come back.
    int FetchInt(int index) {
      Reset();
      object.GetInlet(0)->SetInt(index, YSE::T_GUI);
      return element.gotInt ? element.intValue : -1;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.wrap: registered, two inlets, two outlets (#809)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_WRAP);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".array.wrap");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 2);
  }

  TEST_CASE("array.wrap: appears in the registry's name list (#809)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ARRAY_WRAP)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("array.wrap: the index inlet takes everything, the reference inlet only lists (#809)") {
    // .array.at's arrangement, kept so a patch can swap the strict fetch for
    // the wrapping one without rewiring: inlet 0 is the index — int, float,
    // bang and list all mean a fetch — and inlet 1 only ever carries the
    // "array <name>" reference, which is list text.
    gArrayWrap g;
    const unsigned int indexIn = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((indexIn & YSE::PATCHER::IT_INT) != 0);
    CHECK((indexIn & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((indexIn & YSE::PATCHER::IT_BANG) != 0);
    CHECK((indexIn & YSE::PATCHER::IT_LIST) != 0);
    const unsigned int refIn = g.GetInlet(1)->GetAcceptedTypes();
    CHECK((refIn & YSE::PATCHER::IT_LIST) != 0);
    CHECK((refIn & YSE::PATCHER::IT_INT) == 0);
    CHECK((refIn & YSE::PATCHER::IT_FLOAT) == 0);
    CHECK((refIn & YSE::PATCHER::IT_BANG) == 0);
  }

  // ─── the wrap ───────────────────────────────────────────────────────────────

  TEST_CASE("array.wrap: an index past the end wraps instead of missing (#809)") {
    // The whole point: .array.at bangs its miss outlet at index 3 of a
    // three-element array, and this object reads position 0. Nothing is ever
    // refused and the empty outlet never fires while there is an element.
    Rig rig("awr809a", "a809a");
    rig.Store("append 10 20 30");

    CHECK(rig.FetchInt(0) == 10);
    CHECK(rig.FetchInt(1) == 20);
    CHECK(rig.FetchInt(2) == 30);
    CHECK(rig.FetchInt(3) == 10);
    CHECK(rig.FetchInt(4) == 20);
    CHECK(rig.FetchInt(5) == 30);
    // Several laps out, which is what a bar counter actually sends.
    CHECK(rig.FetchInt(31) == 20);
    CHECK(rig.empty.bangCount == 0);
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.wrap: a negative index counts from the end and keeps wrapping (#809)") {
    // Where .array.at refuses a negative and counts it, here it is the
    // point: -1 is the last element, and past a whole lap it wraps again —
    // C++'s % truncates toward zero, so the object has to add the length
    // back rather than hand out a negative position.
    Rig rig("awr809b", "a809b");
    rig.Store("append 10 20 30");

    CHECK(rig.FetchInt(-1) == 30);
    CHECK(rig.FetchInt(-2) == 20);
    CHECK(rig.FetchInt(-3) == 10);
    CHECK(rig.FetchInt(-4) == 30);
    CHECK(rig.FetchInt(-7) == 30);
    CHECK(rig.empty.bangCount == 0);
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.wrap: the extreme indices wrap rather than overflow (#809)") {
    // INT_MIN has no positive counterpart in int, so negating it before the
    // modulus would be undefined; the wrap is done in a wider type. Both
    // limits have to name a real element, computed the same way any other
    // index is.
    Rig rig("awr809c", "a809c");
    rig.Store("append 10 20 30");

    // INT_MAX == 2147483647; 2147483647 % 3 == 1.
    CHECK(rig.FetchInt(std::numeric_limits<int>::max()) == 20);
    // INT_MIN == -2147483648; -2147483648 % 3 == -2, +3 == 1.
    CHECK(rig.FetchInt(std::numeric_limits<int>::min()) == 20);
    CHECK(rig.empty.bangCount == 0);
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.wrap: the element is typed the way the patcher spells it (#809)") {
    // SendAtom's rule, the same one .array's own "get" and .array.at follow:
    // an int-spelled element leaves as an int, a float-spelled one as a
    // float, anything else as a symbol — reached here through wrapped
    // indices, so the typing survives the modulo.
    Rig rig("awr809d", "a809d");
    rig.Store("append 10 c4 7.5");

    rig.Reset();
    rig.object.GetInlet(0)->SetInt(3, YSE::T_GUI);
    CHECK(rig.element.gotInt);
    CHECK(rig.element.intValue == 10);

    rig.Reset();
    rig.object.GetInlet(0)->SetInt(-2, YSE::T_GUI);
    CHECK(rig.element.gotList);
    CHECK(rig.element.listValue == "c4");

    rig.Reset();
    rig.object.GetInlet(0)->SetInt(-1, YSE::T_GUI);
    CHECK(rig.element.gotFloat);
    CHECK(rig.element.floatValue == doctest::Approx(7.5f));
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.wrap: the length it wraps by is the array's now, not at creation (#809)") {
    // A renumbering write between two fetches simply moves what the unchanged
    // index wraps onto — which is what a position means, and the mid-walk
    // answer #809 asks for: index 4 of a three-element array is position 1,
    // and of a five-element one position 4.
    Rig rig("awr809e", "a809e");
    rig.Store("append 10 20 30");
    CHECK(rig.FetchInt(4) == 20);

    rig.Store("append 40 50");
    CHECK(rig.FetchInt(4) == 50);
    CHECK(rig.FetchInt(-1) == 50);
    CHECK(rig.object.Dropped() == 0);
  }

  // ─── the stored index ───────────────────────────────────────────────────────

  TEST_CASE("array.wrap: a bang re-fetches at the stored index, seeded by the argument (#809)") {
    // ".array.wrap <name> -1" starts pointing at the last element, so a bang
    // before any int has arrived already names a position — and an int moves
    // the index, so the next bang follows it. A negative argument is legal
    // here where .array.at would refuse it.
    Rig rig("awr809f", "a809f", "a809f -1");
    rig.Store("append 10 20 30");
    CHECK(rig.object.Index() == -1);

    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.element.gotInt);
    CHECK(rig.element.intValue == 30);

    CHECK(rig.FetchInt(7) == 20);
    CHECK(rig.object.Index() == 7);
    rig.Reset();
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.element.gotInt);
    CHECK(rig.element.intValue == 20);
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.wrap: a float truncates to an int — Max's float method (#809)") {
    // .table's and .array.at's precedent. A negative float truncates toward
    // zero first and then wraps, so -0.5 is index 0 rather than the last
    // element; a non-finite one is refused rather than quietly fetching
    // element 0.
    Rig rig("awr809g", "a809g");
    rig.Store("append 10 20 30");

    rig.Reset();
    rig.object.GetInlet(0)->SetFloat(4.9f, YSE::T_GUI);
    CHECK(rig.element.gotInt);
    CHECK(rig.element.intValue == 20);
    CHECK(rig.object.Index() == 4);

    rig.Reset();
    rig.object.GetInlet(0)->SetFloat(-1.5f, YSE::T_GUI);
    CHECK(rig.element.gotInt);
    CHECK(rig.element.intValue == 30);
    CHECK(rig.object.Index() == -1);

    const std::uint64_t before = rig.object.Dropped();
    rig.Reset();
    rig.object.GetInlet(0)->SetFloat(std::numeric_limits<float>::quiet_NaN(), YSE::T_GUI);
    CHECK_FALSE(rig.element.gotInt);
    CHECK(rig.object.Index() == -1);
    CHECK(rig.object.Dropped() == before + 1);
  }

  // ─── the list fetch ─────────────────────────────────────────────────────────

  TEST_CASE("array.wrap: a list of indices is answered whole, every position wrapped (#809)") {
    // Max's array.wrap takes several; the reply is one list, the positions in
    // the order the request named them, each wrapped independently — and a
    // one-index list leaves as the element itself, the patcher's transport
    // rule for a list of one.
    Rig rig("awr809h", "a809h", "a809h 2");
    rig.Store("append 10 20 30");

    rig.Reset();
    rig.object.GetInlet(0)->SetList("0 4 -1 9", YSE::T_GUI);
    CHECK(rig.element.gotList);
    CHECK(rig.element.listValue == "10 20 30 10");

    // A list never moves the stored index: it is a compound fetch, not a
    // cursor move — .array.at's rule.
    CHECK(rig.object.Index() == 2);

    rig.Reset();
    rig.object.GetInlet(0)->SetList("-2", YSE::T_GUI);
    CHECK(rig.element.gotInt);
    CHECK(rig.element.intValue == 20);
    CHECK(rig.empty.bangCount == 0);
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.wrap: a malformed index list is refused whole and counted (#809)") {
    // A symbol or a float-spelled token names no position, and a request that
    // is partly malformed is refused whole rather than the readable half being
    // answered — counted, not logged, since the inlet may be the audio
    // thread. A negative is *not* malformed here, which is the difference
    // from .array.at.
    Rig rig("awr809i", "a809i");
    rig.Store("append 10 20 30");

    const std::uint64_t before = rig.object.Dropped();
    rig.Reset();
    rig.object.GetInlet(0)->SetList("1 x", YSE::T_GUI);
    rig.object.GetInlet(0)->SetList("2.5", YSE::T_GUI);
    CHECK_FALSE(rig.element.gotList);
    CHECK_FALSE(rig.element.gotInt);
    CHECK(rig.empty.bangCount == 0);
    CHECK(rig.object.Dropped() == before + 2);

    // And the negative that .array.at refuses is answered here.
    rig.Reset();
    rig.object.GetInlet(0)->SetList("1 -2", YSE::T_GUI);
    CHECK(rig.element.gotList);
    CHECK(rig.element.listValue == "20 20");
    CHECK(rig.object.Dropped() == before + 2);
  }

  // ─── the empty array ────────────────────────────────────────────────────────

  TEST_CASE("array.wrap: an empty array bangs the empty outlet — nothing to wrap onto (#809)") {
    // A modulus of zero names no position, so the miss the wrapping removed
    // everywhere else survives exactly here — and one bang for a list fetch
    // too, the whole-reply rule. Nothing is counted: an empty array is a
    // state, not a malformed request.
    Rig rig("awr809j", "a809j");

    rig.Reset();
    rig.object.GetInlet(0)->SetInt(0, YSE::T_GUI);
    CHECK_FALSE(rig.element.gotInt);
    CHECK(rig.empty.bangCount == 1);

    rig.Reset();
    rig.object.GetInlet(0)->SetInt(-5, YSE::T_GUI);
    CHECK(rig.empty.bangCount == 1);

    rig.Reset();
    rig.object.GetInlet(0)->SetList("0 1 2", YSE::T_GUI);
    CHECK_FALSE(rig.element.gotList);
    CHECK(rig.empty.bangCount == 1);
    CHECK(rig.object.Dropped() == 0);

    // And it stops the moment there is something to wrap onto.
    rig.Store("append 10");
    CHECK(rig.FetchInt(9) == 10);
    CHECK(rig.empty.bangCount == 0);
  }

  TEST_CASE("array.wrap: an unnamed object reads a private, empty array (#809)") {
    // gArray's rule: no name means no address, and no address means a
    // private store nothing else can reach — so every fetch bangs the empty
    // outlet however the patcher is named.
    MultiSink element;
    BangSink empty;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("awr809k");

    gArrayWrap g;
    g.SetParent(&p);
    Wire(g, 0, element);
    Wire(g, 1, empty);
    CHECK(g.Address().empty());

    g.GetInlet(0)->SetInt(0, YSE::T_GUI);
    CHECK_FALSE(element.gotInt);
    CHECK(empty.bangCount == 1);
    CHECK(g.Dropped() == 0);
  }

  // ─── the reference ──────────────────────────────────────────────────────────

  TEST_CASE("array.wrap: the reference fetches on the index inlet, anything else refused (#809)") {
    // "array <name>" is the message an .array's reference outlet emits on a
    // bang; on the index inlet it fetches at the stored index — the family
    // gesture. A reference to an array this object is not bound to is
    // refused, never resolved: a registry lookup is a mutex, and this may be
    // the audio thread.
    Rig rig("awr809l", "a809l", "a809l -1");
    rig.Store("append 10 20 30");

    const std::uint64_t before = rig.object.Dropped();
    rig.Reset();
    rig.object.GetInlet(0)->SetList("array a809l", YSE::T_GUI);
    CHECK(rig.element.gotInt);
    CHECK(rig.element.intValue == 30);
    CHECK(rig.object.Dropped() == before);

    rig.Reset();
    rig.object.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK_FALSE(rig.element.gotInt);
    CHECK(rig.object.Dropped() == before + 1);
  }

  TEST_CASE("array.wrap: the reference inlet acknowledges its own array and nothing more (#809)") {
    // Wiring the array's reference outlet across keeps the patch readable,
    // and the acknowledgement is silent — the binding is the creation
    // argument, so there is nothing to set. Anything else there is refused,
    // which is what makes a mis-wired cord visible in Dropped().
    Rig rig("awr809m", "a809m");
    rig.Store("append 10 20 30");

    const std::uint64_t before = rig.object.Dropped();
    rig.object.GetInlet(1)->SetList("array a809m", YSE::T_GUI);
    CHECK_FALSE(rig.element.gotInt);
    CHECK_FALSE(rig.element.gotList);
    CHECK(rig.empty.bangCount == 0);
    CHECK(rig.object.Dropped() == before);

    rig.object.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    rig.object.GetInlet(1)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.object.Dropped() == before + 2);
  }

  // ─── the flow a patch actually wires ────────────────────────────────────────

  TEST_CASE("array.wrap: wired end to end, a counter past the end keeps reading (#809)") {
    // The user-visible flow, built through the public patcher API rather than
    // by poking objects: an .array holding a pattern, a counter's index into
    // .array.wrap, and the same cord into .array.at beside it. The sequencer
    // case #809 is about — the bar counter runs past the pattern's length and
    // the wrap keeps producing notes where the strict fetch has started
    // missing.
    MultiSink wrapped;
    YSE::pHandle wrappedHandle(&wrapped);
    MultiSink strict;
    YSE::pHandle strictHandle(&strict);
    BangSink strictMiss;
    YSE::pHandle strictMissHandle(&strictMiss);
    YSE::patcher p;
    p.create(2);
    p.name("awr809n");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a809n");
    YSE::pHandle* wrap = p.CreateObject(YSE::OBJ::G_ARRAY_WRAP, "a809n");
    YSE::pHandle* at = p.CreateObject(YSE::OBJ::G_ARRAY_AT, "a809n");
    REQUIRE(array != nullptr);
    REQUIRE(wrap != nullptr);
    REQUIRE(at != nullptr);
    p.Connect(wrap, 0, &wrappedHandle, 0);
    p.Connect(at, 0, &strictHandle, 0);
    p.Connect(at, 1, &strictMissHandle, 0);

    array->SetListData(0, "append 60 64 67");

    // Inside the pattern the two agree.
    wrap->SetIntData(0, 1);
    at->SetIntData(0, 1);
    REQUIRE(wrapped.gotInt);
    CHECK(wrapped.intValue == 64);
    REQUIRE(strict.gotInt);
    CHECK(strict.intValue == 64);
    CHECK_FALSE(strictMiss.gotBang);

    // Past it they part company: the wrap starts the pattern again where the
    // strict fetch bangs its miss outlet and sends no element.
    wrapped.reset();
    strict.reset();
    wrap->SetIntData(0, 4);
    at->SetIntData(0, 4);
    REQUIRE(wrapped.gotInt);
    CHECK(wrapped.intValue == 64);
    CHECK_FALSE(strict.gotInt);
    CHECK(strictMiss.gotBang);
  }

  TEST_CASE(
      "array.wrap: wired from the array's reference outlet, banging the array fetches (#809)") {
    // The family's gesture, end to end through the public API: the .array's
    // reference outlet into the index inlet, so a bang on the array produces
    // the element at the stored (here negative) index.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    BangSink empty;
    YSE::pHandle emptyHandle(&empty);
    YSE::patcher p;
    p.create(2);
    p.name("awr809o");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a809o");
    YSE::pHandle* wrap = p.CreateObject(YSE::OBJ::G_ARRAY_WRAP, "a809o -1");
    REQUIRE(array != nullptr);
    REQUIRE(wrap != nullptr);
    p.Connect(array, 1, wrap, 0);
    p.Connect(wrap, 0, &sinkHandle, 0);
    p.Connect(wrap, 1, &emptyHandle, 0);

    array->SetListData(0, "append 60 64 67");
    array->SetBang(0);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 67);
    CHECK_FALSE(empty.gotBang);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.wrap: the address form is the patcher's, and RefreshBinding follows it (#809)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("awr809p_before");

    gArrayWrap g;
    g.SetParams("a809p");
    g.SetParent(&p);
    CHECK(g.ArrayName() == "a809p");
    CHECK(g.Address() == "awr809p_before.a809p");

    // Idempotent: a rebind to the address it already has keeps the store.
    g.RefreshBinding();
    CHECK(g.Address() == "awr809p_before.a809p");

    p.SetName("awr809p_after");
    g.RefreshBinding();
    CHECK(g.Address() == "awr809p_after.a809p");
  }

  TEST_CASE("array.wrap: patcherImplementation::SetName re-anchors it (#809)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by the
    // patcher, without anybody calling RefreshBinding by hand. The keeper
    // holds the old-address store (it is not in the patcher's object map, so
    // the rename does not touch it): before the rename a fetch finds the
    // keeper's element; after it the fetch reads a fresh empty array under
    // the new prefix, so the same index bangs the empty outlet.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    BangSink empty;
    YSE::pHandle emptyHandle(&empty);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("awr809q_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a809q");
    keeper.GetInlet(0)->SetList("append 10 20 30", YSE::T_GUI);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_WRAP, "a809q");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &sinkHandle, 0);
    p.Connect(h, 1, &emptyHandle, 0);

    h->SetIntData(0, 5);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 30);
    CHECK_FALSE(empty.gotBang);

    p.SetName("awr809q_after");
    sink.reset();
    h->SetIntData(0, 5);
    CHECK_FALSE(sink.gotInt);
    CHECK(empty.gotBang);
  }

  TEST_CASE("array.wrap: SetParams(\"\") resets the name and the stored index (#809)") {
    // A re-parse must not leave half of the previous configuration standing:
    // the index goes back to 0 along with the name, and the object is reading
    // a private, empty array again.
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("awr809r");

    gArrayWrap g;
    g.SetParent(&p);
    g.SetParams("a809r -2");
    CHECK(g.Index() == -2);
    CHECK(g.Address() == "awr809r.a809r");

    g.SetParams("");
    CHECK(g.Index() == 0);
    CHECK(g.ArrayName().empty());
    CHECK(g.Address().empty());
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.wrap: a fetch asked for over in-patcher delivery lands on T_DSP (#809)") {
    // A .r feeding the index inlet dispatches on T_DSP when the block drains
    // it (issue #225) — "the audio thread asks for an element" is the
    // ordinary case, and the whole path is one guard hold, a bounded copy and
    // a send of storage the object already owns.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("awr809s");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go809s");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a809s");
    YSE::pHandle* wrap = p.CreateObject(YSE::OBJ::G_ARRAY_WRAP, "a809s");
    REQUIRE(recv != nullptr);
    REQUIRE(array != nullptr);
    REQUIRE(wrap != nullptr);
    p.Connect(recv, 0, wrap, 0);
    p.Connect(wrap, 0, &sinkHandle, 0);

    array->SetListData(0, "append 10 20 30");

    p.PassData(7, "go809s", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 20);
  }

  TEST_CASE("array.wrap: no message path allocates (#809)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path: the int, float, bang and list fetches, a positive
    // and a negative index, the reference on both inlets, the wrong-name
    // refusal and the malformed list.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string indices = "5 -1 0";
    const std::string reference = "array probeA809";
    const std::string wrongName = "array somewhere_else_long";
    const std::string malformed = "1 not_an_index_at_all";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("awr809t");
    MultiSink element;
    BangSink empty;
    gArray array;
    gArrayWrap wrap;
    array.SetParent(&p);
    array.SetParams("probeA809");
    wrap.SetParent(&p);
    wrap.SetParams("probeA809");
    Wire(wrap, 0, element);
    Wire(wrap, 1, empty);

    array.GetInlet(0)->SetList("append 10 a-symbol-past-every-small-string-buffer 7.5", YSE::T_GUI);

    // Warm every path — including the sink's list assignment — so first-call
    // machinery is not what the probe catches.
    wrap.GetInlet(0)->SetInt(4, YSE::T_GUI);
    wrap.GetInlet(0)->SetInt(-2, YSE::T_GUI);
    wrap.GetInlet(0)->SetFloat(0.f, YSE::T_GUI);
    wrap.GetInlet(0)->SetBang(YSE::T_GUI);
    wrap.GetInlet(0)->SetList(indices, YSE::T_GUI);
    wrap.GetInlet(0)->SetList(reference, YSE::T_GUI);
    wrap.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    wrap.GetInlet(0)->SetList(malformed, YSE::T_GUI);
    wrap.GetInlet(1)->SetList(reference, YSE::T_GUI);
    wrap.GetInlet(1)->SetList(wrongName, YSE::T_GUI);
    const std::uint64_t before = wrap.Dropped();

    element.reset();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      wrap.GetInlet(0)->SetInt(4, YSE::T_DSP);
      wrap.GetInlet(0)->SetInt(-2, YSE::T_DSP);
      wrap.GetInlet(0)->SetFloat(0.f, YSE::T_DSP);
      wrap.GetInlet(0)->SetBang(YSE::T_DSP);
      wrap.GetInlet(0)->SetList(indices, YSE::T_DSP);
      wrap.GetInlet(0)->SetList(reference, YSE::T_DSP);
      wrap.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      wrap.GetInlet(0)->SetList(malformed, YSE::T_DSP);
      wrap.GetInlet(1)->SetList(reference, YSE::T_DSP);
      wrap.GetInlet(1)->SetList(wrongName, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. The list fetch answered with its wrapped
    // positions, and both refusals (the wrong name on each inlet, the
    // malformed list) were counted.
    CHECK(element.gotList);
    CHECK(element.listValue == "7.5 7.5 10");
    CHECK(empty.bangCount == 0);
    CHECK(wrap.Dropped() == before + 3);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.wrap: params survive a DumpJSON / ParseJSON round trip (#809)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_ARRAY_WRAP, "notes809 -3");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".array.wrap"));
    CHECK(copy->GetParams() == std::string("notes809 -3"));
    CHECK(copy->GetInputs() == 2);
    CHECK(copy->GetOutputs() == 2);
  }

  TEST_CASE("array.wrap: carries complete documentation metadata (#809)") {
    gArrayWrap g;
    CHECK_FALSE(g.GetDescription().empty());
    CHECK(g.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = g.GetParamDocs();
    REQUIRE(docs.size() == 2);
    CHECK(docs[0].name == "name");
    CHECK(docs[1].name == "index");
  }
}
