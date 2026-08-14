// Tests for .array.fill (issue #794) — Max's array.fill on the name-addressed
// value model .array settled (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **the array is bound from the creation argument.** An array never
//     travels down a cord, so ".array.fill <name>" resolves the name once, on
//     the control thread, and an `array <name>` message is honoured only when
//     it names the array already bound — ArrayReferenceNames' bounded
//     compare, never a registry lookup on a message path.
//   - **a fill REPLACES.** The array becomes exactly count copies of the
//     value — #794's replace-or-append and shrink questions answered
//     together: a shorter fill shrinks the array and a count of 0 clears it,
//     which is what makes the object the initialiser an index-addressed
//     write pattern needs.
//   - **value hot, count cold — Max's own inlets.** An inline value fills at
//     the moment it arrives and stores nothing; a bang fills with the
//     author's configuration; the count inlet stores silently and refuses
//     what could never land — negative, or past the store's 256 — before
//     storing it, counted rather than truncated.
//   - **the whole fill is one guard hold, and nothing allocates.**
//     In-patcher delivery dispatches on T_DSP, so "the audio thread fills an
//     array" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <limits>
#include <string>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayFill.h"
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
using YSE::PATCHER::gArrayFill;

namespace {

  // An .array and one .array.fill on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sink is declared before the objects so it is torn down last, while the
  // outlet wired to it still exists (see sinks.hpp on why that matters).
  struct Rig {
    MultiSink reference; // outlet 0: the reference after a landed fill
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray array;
    gArrayFill object;

    Rig(const std::string& patcherName, const std::string& name,
        const std::string& params = std::string()) {
      p.SetName(patcherName);
      array.SetParent(&p);
      array.SetParams(name);
      object.SetParent(&p);
      object.SetParams(params.empty() ? name : params);
      Wire(object, 0, reference);
    }

    void Store(const std::string& message) {
      array.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    // The array's contents as one space-joined string — what the fill is
    // asserted against. Control thread only (ElementAt allocates).
    std::string Contents() {
      std::string out;
      for (std::size_t i = 0; i < array.Count(); i++) {
        if (i != 0) out += ' ';
        out += array.ElementAt(i);
      }
      return out;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.fill: registered, with .array.insert's three inlets over one outlet (#794)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* fill = p.CreateObject(YSE::OBJ::G_ARRAY_FILL);
    REQUIRE(fill != nullptr);
    CHECK(std::string(fill->Type()) == ".array.fill");
    CHECK(fill->GetInputs() == 3);
    CHECK(fill->GetOutputs() == 1);

    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ARRAY_FILL)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("array.fill: the value inlet takes the ask, the cold inlets only their "
            "configuration (#794)") {
    // Max's arrangement kept: the datum on the left triggers, the length on
    // the right stores. The count inlet is a number and nothing else, and
    // the reference inlet is list text only.
    gArrayFill fill;
    const unsigned int trigger = fill.GetInlet(0)->GetAcceptedTypes();
    CHECK((trigger & YSE::PATCHER::IT_BANG) != 0);
    CHECK((trigger & YSE::PATCHER::IT_INT) != 0);
    CHECK((trigger & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((trigger & YSE::PATCHER::IT_LIST) != 0);
    const unsigned int count = fill.GetInlet(1)->GetAcceptedTypes();
    CHECK((count & YSE::PATCHER::IT_INT) != 0);
    CHECK((count & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((count & YSE::PATCHER::IT_LIST) == 0);
    CHECK((count & YSE::PATCHER::IT_BANG) == 0);
    const unsigned int ref = fill.GetInlet(2)->GetAcceptedTypes();
    CHECK((ref & YSE::PATCHER::IT_LIST) != 0);
    CHECK((ref & YSE::PATCHER::IT_BANG) == 0);
  }

  // ─── the fill replaces ──────────────────────────────────────────────────────

  TEST_CASE("array.fill: the array becomes exactly count copies — replacing, shrinking, "
            "clearing (#794)") {
    // ".array.fill <name> 3 c4" seeds the stored count and value, so a bang
    // is the one-message size-and-init the object exists for.
    Rig rig("afi794a", "a794a", "a794a 3 c4");
    CHECK(rig.object.StoredCount() == 3);

    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "c4 c4 c4");
    CHECK(rig.reference.gotList);
    CHECK(rig.reference.listValue == "array a794a");

    // A fill replaces, never appends: whatever the array held is gone, and a
    // fill shorter than the current length shrinks it — the two questions
    // #794 asks, answered as one.
    rig.Store("set 0 60");
    rig.Store("append 61 62 63 64");
    CHECK(rig.array.Count() == 7);
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "c4 c4 c4");
    CHECK(rig.array.Count() == 3);

    // A count of 0 clears — and still announces: the ask was well-formed and
    // the answer is what it says.
    rig.reference.reset();
    rig.object.GetInlet(1)->SetInt(0, YSE::T_GUI);
    CHECK_FALSE(rig.reference.gotList); // stored silently
    CHECK(rig.array.Count() == 3); // nothing filled yet
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.array.Count() == 0);
    CHECK(rig.reference.gotList);
    CHECK(rig.object.Dropped() == 0);
  }

  TEST_CASE("array.fill: an absent value fills with 0 — Max's default — and an absent count "
            "with nothing (#794)") {
    // ".array.fill <name> 4": no third argument, so the stored value is "0"
    // — "without any initial data, the array will be filled with 0s" — and
    // positions 0..3 now exist for an index-addressed write to land on.
    Rig rig("afi794b", "a794b", "a794b 4");
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "0 0 0 0");

    rig.Store("set 2 60");
    CHECK(rig.Contents() == "0 0 60 0");

    // A bare ".array.fill <name>" stores count 0, so a bang clears.
    rig.object.SetParams("a794b");
    CHECK(rig.object.StoredCount() == 0);
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.array.Count() == 0);
    CHECK(rig.object.Dropped() == 0);
  }

  // ─── the value inlet ────────────────────────────────────────────────────────

  TEST_CASE("array.fill: an inline value fills at the moment it arrives and stores nothing "
            "(#794)") {
    Rig rig("afi794c", "a794c", "a794c 3 c4");

    // An int fills with the stored count of copies of THAT value —
    // gArrayIndexMap's trigger rule — and a bang afterwards fills with the
    // author's value, proving the inline one was stored nowhere.
    rig.object.GetInlet(0)->SetInt(60, YSE::T_GUI);
    CHECK(rig.Contents() == "60 60 60");
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "c4 c4 c4");

    // A float keeps its spelling — 7.5 stays visibly a float — and a symbol
    // arrives as a one-atom list, kept equivalent to the number.
    rig.object.GetInlet(0)->SetFloat(7.5f, YSE::T_GUI);
    CHECK(rig.Contents() == "7.5 7.5 7.5");
    const std::string symbol = "e4";
    rig.object.GetInlet(0)->SetList(symbol, YSE::T_GUI);
    CHECK(rig.Contents() == "e4 e4 e4");
    CHECK(rig.object.Dropped() == 0);

    // A non-finite float has no spelling that reads back — refused, nothing
    // changed.
    rig.object.GetInlet(0)->SetFloat(std::numeric_limits<float>::quiet_NaN(), YSE::T_GUI);
    CHECK(rig.Contents() == "e4 e4 e4");
    CHECK(rig.object.Dropped() == 1);

    // A list of more than one atom is refused whole: the fill value is one
    // atom, and Max's fill-with-these-contents gesture deliberately does
    // not land here — quietly re-reading "60 64" as <count> <value> would
    // corrupt exactly the message a ported patch sends.
    rig.object.GetInlet(0)->SetList("60 64", YSE::T_GUI);
    CHECK(rig.Contents() == "e4 e4 e4");
    CHECK(rig.object.Dropped() == 2);

    // A value no element can hold — past ELEMENT_CAPACITY — is refused
    // whole, never truncated.
    const std::string overlong(YSE::PATCHER::arrayStore::ELEMENT_CAPACITY + 1, 'x');
    rig.object.GetInlet(0)->SetList(overlong, YSE::T_GUI);
    CHECK(rig.Contents() == "e4 e4 e4");
    CHECK(rig.object.Dropped() == 3);
  }

  // ─── the count inlet ────────────────────────────────────────────────────────

  TEST_CASE("array.fill: the count inlet stores silently and refuses what could never land "
            "(#794)") {
    Rig rig("afi794d", "a794d", "a794d 2 7");

    // An int stores silently — nothing emitted, nothing filled.
    rig.object.GetInlet(1)->SetInt(4, YSE::T_GUI);
    CHECK_FALSE(rig.reference.gotList);
    CHECK(rig.array.Count() == 0);
    CHECK(rig.object.StoredCount() == 4);
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.Contents() == "7 7 7 7");

    // Negative is malformed (a count is a size, not a distance) and past the
    // store's 256 could never land: both are refused before they are
    // stored, so a bang after the refusal fills with what it would have
    // filled before it.
    rig.object.GetInlet(1)->SetInt(-1, YSE::T_GUI);
    CHECK(rig.object.StoredCount() == 4);
    CHECK(rig.object.Dropped() == 1);
    rig.object.GetInlet(1)->SetInt(257, YSE::T_GUI);
    CHECK(rig.object.StoredCount() == 4);
    CHECK(rig.object.Dropped() == 2);

    // A float truncates to an int first; a non-finite one is refused rather
    // than quietly becoming count 0.
    rig.object.GetInlet(1)->SetFloat(3.7f, YSE::T_GUI);
    CHECK(rig.object.StoredCount() == 3);
    rig.object.GetInlet(1)->SetFloat(std::numeric_limits<float>::infinity(), YSE::T_GUI);
    CHECK(rig.object.StoredCount() == 3);
    CHECK(rig.object.Dropped() == 3);

    // The store's whole bound is fillable: 256 lands.
    rig.object.GetInlet(1)->SetInt(256, YSE::T_GUI);
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.array.Count() == 256);
    CHECK(rig.array.ElementAt(255) == "7");
    CHECK(rig.object.Dropped() == 3);

    // A re-parse must not leave half of the previous configuration
    // standing: SetParams("") drops the count back to 0 and the value back
    // to its default along with the name.
    rig.object.SetParams("");
    CHECK(rig.object.StoredCount() == 0);
  }

  TEST_CASE("array.fill: an out-of-range count only a creation argument can plant is refused "
            "at the fill (#794)") {
    // The inlet refuses one before storing it, so only ".array.fill <name>
    // 500" can plant one — malformed, not a miss, and never clamped.
    Rig rig("afi794e", "a794e", "a794e 500 7");
    CHECK(rig.object.StoredCount() == 500);
    rig.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.array.Count() == 0);
    CHECK_FALSE(rig.reference.gotList);
    CHECK(rig.object.Dropped() == 1);

    // An author's value no element can hold refuses the same way: refusal,
    // never truncation.
    Rig wide("afi794e2", "a794e2",
             "a794e2 2 " + std::string(YSE::PATCHER::arrayStore::ELEMENT_CAPACITY + 1, 'y'));
    wide.object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(wide.array.Count() == 0);
    CHECK_FALSE(wide.reference.gotList);
    CHECK(wide.object.Dropped() == 1);
  }

  // ─── the reference gesture ──────────────────────────────────────────────────

  TEST_CASE("array.fill: the reference fills on the trigger, acknowledges on its own inlet, "
            "and anything else is refused (#794)") {
    Rig rig("afi794f", "a794f", "a794f 2 g3");

    rig.object.GetInlet(0)->SetList("array a794f", YSE::T_GUI);
    CHECK(rig.Contents() == "g3 g3");
    CHECK(rig.reference.gotList);
    CHECK(rig.object.Dropped() == 0);

    // A reference naming an array this object is not bound to is refused
    // everywhere, never resolved: a registry lookup is a mutex, and this may
    // be the audio thread.
    rig.reference.reset();
    rig.object.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK_FALSE(rig.reference.gotList);
    CHECK(rig.Contents() == "g3 g3");
    CHECK(rig.object.Dropped() == 1);

    rig.object.GetInlet(2)->SetList("array a794f", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 1);
    rig.object.GetInlet(2)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 2);
    CHECK_FALSE(rig.reference.gotList);
  }

  TEST_CASE("array.fill: an unnamed object fills a private array, silently (#794)") {
    // No name, no shared store, no reference to pass on — the fill lands (on
    // a private array nothing else can see), the announcement is simply
    // empty, and nothing is a refusal: the ask was well-formed.
    MultiSink sink;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("afi794g");
    gArrayFill fill;
    fill.SetParent(&p);
    Wire(fill, 0, sink);
    CHECK(fill.Address().empty());

    fill.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(sink.gotList);
    CHECK(fill.Dropped() == 0);
  }

  // ─── the family chains ──────────────────────────────────────────────────────

  TEST_CASE("array.fill: sizes an array for the index-addressed pattern, end to end on cords "
            "(#794)") {
    // The use case #794 names, run through the public patcher API: fill
    // sizes and initialises, the reference chains into .array.length to
    // report the new size, and the positions the fill created take
    // index-addressed writes that the store would have refused before.
    MultiSink lengthOut;
    MultiSink contentsOut;
    YSE::pHandle lengthHandle(&lengthOut);
    YSE::pHandle contentsHandle(&contentsOut);
    YSE::patcher p;
    p.create(2);
    p.name("afi794h");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "notes794h");
    YSE::pHandle* fill = p.CreateObject(YSE::OBJ::G_ARRAY_FILL, "notes794h 4");
    YSE::pHandle* length = p.CreateObject(YSE::OBJ::G_ARRAY_LENGTH, "notes794h");
    REQUIRE(array != nullptr);
    REQUIRE(fill != nullptr);
    REQUIRE(length != nullptr);
    p.Connect(fill, 0, length, 0);
    p.Connect(length, 0, &lengthHandle, 0);
    p.Connect(array, 0, &contentsHandle, 0);

    // Before the fill, an index-addressed write past the end is refused —
    // the store's rule, and the reason this object exists.
    array->SetListData(0, "set 2 60");
    array->SetListData(0, "getsize");
    REQUIRE(lengthOut.gotInt == false);
    REQUIRE(contentsOut.gotInt);
    CHECK(contentsOut.intValue == 0);

    // One bang: four zeros, and the reference chains the new size out of
    // .array.length without anything else being touched.
    fill->SetBang(0);
    REQUIRE(lengthOut.gotInt);
    CHECK(lengthOut.intValue == 4);

    // The positions now exist, so the write that was refused lands.
    array->SetListData(0, "set 2 60");
    array->SetListData(0, "getvalue");
    REQUIRE(contentsOut.gotList);
    CHECK(contentsOut.listValue == "0 0 60 0");
  }

  TEST_CASE("array.fill: wired from the array's reference outlet, banging the array fills "
            "(#794)") {
    // The family gesture, end to end through the public patcher API: the
    // .array's reference outlet into the value inlet gives "bang the array,
    // out comes the filled array's reference", and the contents are read
    // back through the array's own "getvalue", so the whole loop runs on
    // cords.
    MultiSink chained;
    MultiSink contents;
    YSE::pHandle chainedHandle(&chained);
    YSE::pHandle contentsHandle(&contents);
    YSE::patcher p;
    p.create(2);
    p.name("afi794i");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a794i");
    YSE::pHandle* fill = p.CreateObject(YSE::OBJ::G_ARRAY_FILL, "a794i 3 60");
    REQUIRE(array != nullptr);
    REQUIRE(fill != nullptr);
    p.Connect(array, 1, fill, 0);
    p.Connect(fill, 0, &chainedHandle, 0);
    p.Connect(array, 0, &contentsHandle, 0);

    array->SetBang(0);
    REQUIRE(chained.gotList);
    CHECK(chained.listValue == "array a794i");

    array->SetListData(0, "getvalue");
    REQUIRE(contents.gotList);
    CHECK(contents.listValue == "60 60 60");
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.fill: patcherImplementation::SetName re-anchors the shared base (#794)") {
    // The rename dispatch itself: an object created *inside* a patcher must
    // be re-anchored by the patcher, without anybody calling RefreshBinding
    // by hand. The keeper holds the old-address store; after the rename the
    // object fills a fresh empty array under the new prefix, so the keeper's
    // contents stop moving — while the stored count survives, being the
    // object's own state rather than anything derived from the name.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("afi794j_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a794j");
    keeper.GetInlet(0)->SetList("append 1 2 3 4 5", YSE::T_GUI);

    YSE::pHandle* fill = p.CreateObject(YSE::OBJ::G_ARRAY_FILL, "a794j 2 9");
    REQUIRE(fill != nullptr);
    p.Connect(fill, 0, &sinkHandle, 0);

    fill->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(keeper.Count() == 2);
    CHECK(keeper.ElementAt(0) == "9");

    p.SetName("afi794j_after");
    sink.reset();
    fill->SetBang(0);
    // The fill landed — on the new, empty array — and the keeper's contents
    // were not touched.
    CHECK(sink.gotList);
    CHECK(keeper.Count() == 2);
    CHECK(keeper.ElementAt(0) == "9");
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.fill: a value arriving over in-patcher delivery lands on T_DSP (#794)") {
    // A .r feeding the value inlet dispatches on T_DSP when the block drains
    // it (issue #225) — "the audio thread fills an array" is the ordinary
    // case, and the whole path is one bounded render, one guard hold and a
    // send of a string the object already owns.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("afi794k");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a794k");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go794k");
    YSE::pHandle* fill = p.CreateObject(YSE::OBJ::G_ARRAY_FILL, "a794k 3");
    REQUIRE(recv != nullptr);
    REQUIRE(fill != nullptr);
    p.Connect(recv, 0, fill, 0);
    p.Connect(fill, 0, &sinkHandle, 0);

    p.PassData(std::string("64"), "go794k", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "array a794k");
    CHECK(keeper.Count() == 3);
    CHECK(keeper.ElementAt(0) == "64");
    CHECK(keeper.ElementAt(2) == "64");
  }

  TEST_CASE("array.fill: no message path allocates (#794)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path — the bang, the inline int, float and symbol, the
    // reference gesture, the count stores, the refusals, and the
    // reference-inlet acknowledgement — on T_DSP, in-patcher delivery's
    // thread.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "array probeAFI794";
    const std::string wrongName = "array somewhere_else_long";
    const std::string symbol = "a_symbol_value";
    const std::string malformed = "60 64 67";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("afi794l");
    MultiSink referenceSink;
    gArray array;
    gArrayFill fill;
    array.SetParent(&p);
    array.SetParams("probeAFI794");
    fill.SetParent(&p);
    fill.SetParams("probeAFI794 4 c4");
    Wire(fill, 0, referenceSink);

    // Warm every path — including the sink's assignments — so first-call
    // machinery is not what the probe catches.
    fill.GetInlet(0)->SetBang(YSE::T_GUI);
    fill.GetInlet(0)->SetInt(60, YSE::T_GUI);
    fill.GetInlet(0)->SetFloat(7.5f, YSE::T_GUI);
    fill.GetInlet(0)->SetList(symbol, YSE::T_GUI);
    fill.GetInlet(0)->SetList(reference, YSE::T_GUI);
    fill.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    fill.GetInlet(0)->SetList(malformed, YSE::T_GUI);
    fill.GetInlet(1)->SetInt(3, YSE::T_GUI);
    fill.GetInlet(1)->SetInt(-1, YSE::T_GUI);
    fill.GetInlet(1)->SetFloat(2.5f, YSE::T_GUI);
    fill.GetInlet(2)->SetList(reference, YSE::T_GUI);

    const std::uint64_t drops = fill.Dropped();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      fill.GetInlet(0)->SetBang(YSE::T_DSP);
      fill.GetInlet(0)->SetInt(60, YSE::T_DSP);
      fill.GetInlet(0)->SetFloat(7.5f, YSE::T_DSP);
      fill.GetInlet(0)->SetList(symbol, YSE::T_DSP);
      fill.GetInlet(0)->SetList(reference, YSE::T_DSP);
      fill.GetInlet(0)->SetList(wrongName, YSE::T_DSP); // refused
      fill.GetInlet(0)->SetList(malformed, YSE::T_DSP); // refused
      fill.GetInlet(1)->SetInt(3, YSE::T_DSP); // stored, silent
      fill.GetInlet(1)->SetInt(-1, YSE::T_DSP); // refused
      fill.GetInlet(1)->SetFloat(2.5f, YSE::T_DSP); // stored, silent
      fill.GetInlet(2)->SetList(reference, YSE::T_DSP); // acknowledged
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. The last fill under the probe was the
    // reference gesture: two copies (the count the warm-up's float store
    // left) of the author's c4. The count inlet afterwards moved the stored
    // count to 3 and the final float back to 2, and exactly the three
    // refusals were counted.
    CHECK(referenceSink.gotList);
    CHECK(fill.StoredCount() == 2);
    CHECK(fill.Dropped() == drops + 3);
    CHECK(array.Count() == 2);
    CHECK(array.ElementAt(0) == "c4");
    CHECK(array.ElementAt(1) == "c4");
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.fill: params survive a DumpJSON / ParseJSON round trip (#794)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* fill = src.CreateObject(YSE::OBJ::G_ARRAY_FILL, "notes794m 8 c4");
    REQUIRE(fill != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    // Found by type rather than by list position: the loaded patcher's
    // enumeration order is not the creation order.
    YSE::pHandle* copy = nullptr;
    for (unsigned int i = 0; i < loaded.Objects(); i++) {
      YSE::pHandle* handle = loaded.GetHandleFromList(static_cast<int>(i));
      REQUIRE(handle != nullptr);
      if (std::string(handle->Type()) == ".array.fill") copy = handle;
    }

    REQUIRE(copy != nullptr);
    // The analyzer cannot see that a failed REQUIRE aborts the case (doctest's
    // failure path is a runtime jump), so it assumes `copy` may be null here.
    // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
    CHECK(copy->GetParams() == std::string("notes794m 8 c4"));
    CHECK(copy->GetInputs() == 3);
    CHECK(copy->GetOutputs() == 1);
  }

  TEST_CASE("array.fill: carries complete documentation metadata (#794)") {
    gArrayFill fill;
    CHECK_FALSE(fill.GetDescription().empty());
    CHECK(fill.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    REQUIRE(fill.GetParamDocs().size() == 3);
    CHECK(fill.GetParamDocs()[0].name == "name");
    CHECK(fill.GetParamDocs()[1].name == "count");
    CHECK(fill.GetParamDocs()[2].name == "value");
  }
}
