// Tests for .array.length (issue #783) — Max's array.length ("output an
// array's length") on the name-addressed value model .array settled (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **the array is bound from the creation argument.** An array never
//     travels down a cord, so ".array.length <name>" resolves the name once,
//     on the control thread, and an `array <name>` message is honoured only
//     when it names the array already bound — ArrayReferenceNames' bounded
//     compare, never a registry lookup on a message path.
//   - **the length is asked for, never announced.** A bang answers with one
//     int — store->count as it stood at the trigger — and a write to the
//     array emits nothing: .value's rule that an object driven by its inlet
//     does not emit on its own.
//   - **zero is a length, not a miss.** An empty array answers 0, and so
//     does an unnamed (private) one — there is no miss outlet, because every
//     array has a length where not every index has an element.
//   - **the ask crosses the control/audio boundary and nothing allocates.**
//     In-patcher delivery dispatches on T_DSP, so "the audio thread asks for
//     the length" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <string>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayLength.h"
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
using YSE::PATCHER::gArrayLength;

namespace {

  // An .array and an .array.length on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sink is declared before the objects so it is torn down last, while the
  // outlet wired to it still exists (see sinks.hpp on why that matters).
  struct Rig {
    MultiSink length;
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray array;
    gArrayLength len;

    Rig(const std::string& patcherName, const std::string& name) {
      p.SetName(patcherName);
      array.SetParent(&p);
      array.SetParams(name);
      len.SetParent(&p);
      len.SetParams(name);
      Wire(len, 0, length);
    }

    void Store(const std::string& message) {
      array.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Reset() {
      length.reset();
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.length: registered, two inlets, one outlet (#783)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_LENGTH);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".array.length");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 1);
  }

  TEST_CASE("array.length: appears in the registry's name list (#783)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ARRAY_LENGTH)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE(
      "array.length: the trigger inlet takes the ask, the reference inlet only lists (#783)") {
    // Inlet 0 is the ask — a bang, or the "array <name>" reference, which is
    // list text. A length is asked for, never addressed, so there is no int
    // or float method anywhere: a number on either inlet names nothing.
    gArrayLength g;
    const unsigned int triggerIn = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((triggerIn & YSE::PATCHER::IT_BANG) != 0);
    CHECK((triggerIn & YSE::PATCHER::IT_LIST) != 0);
    CHECK((triggerIn & YSE::PATCHER::IT_INT) == 0);
    CHECK((triggerIn & YSE::PATCHER::IT_FLOAT) == 0);
    const unsigned int refIn = g.GetInlet(1)->GetAcceptedTypes();
    CHECK((refIn & YSE::PATCHER::IT_LIST) != 0);
    CHECK((refIn & YSE::PATCHER::IT_INT) == 0);
    CHECK((refIn & YSE::PATCHER::IT_FLOAT) == 0);
    CHECK((refIn & YSE::PATCHER::IT_BANG) == 0);
  }

  // ─── the ask ────────────────────────────────────────────────────────────────

  TEST_CASE(
      "array.length: a bang answers the length, and only a bang — a write emits nothing (#783)") {
    // The object's whole point: the length arrives as one int a patch can
    // wire into a .uzi, and it arrives only when asked — a write to the
    // array is silent, .value's rule that an object driven by its inlet
    // does not emit on its own.
    Rig rig("aln783a", "a783a");

    rig.len.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.length.gotInt);
    CHECK(rig.length.intValue == 0);

    rig.Reset();
    rig.Store("append 10 c4 7.5");
    CHECK_FALSE(rig.length.gotInt);

    rig.len.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.length.gotInt);
    CHECK(rig.length.intValue == 3);

    rig.Reset();
    rig.Store("append 9 11");
    rig.len.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.length.gotInt);
    CHECK(rig.length.intValue == 5);

    rig.Reset();
    rig.Store("clear");
    rig.len.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.length.gotInt);
    CHECK(rig.length.intValue == 0);
    CHECK(rig.len.Dropped() == 0);
  }

  TEST_CASE("array.length: an unnamed object reads a private, empty array (#783)") {
    // Not "shares the empty name" — gArray's rule, inherited whole. The
    // answer is still a length: 0, out the length outlet, because zero is a
    // length and not a miss.
    MultiSink sink;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aln783b");
    gArrayLength g;
    g.SetParent(&p);
    Wire(g, 0, sink);

    CHECK(g.Address().empty());
    g.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 0);
    CHECK(g.Dropped() == 0);
  }

  // ─── the reference ──────────────────────────────────────────────────────────

  TEST_CASE("array.length: the reference asks on the trigger inlet, anything else refused (#783)") {
    // "array <name>" is the message an .array's reference outlet emits on a
    // bang; on the trigger inlet it asks for the length — the family
    // gesture: bang the array, out comes its length. A reference to an
    // array this object is not bound to is refused, never resolved: a
    // registry lookup is a mutex, and this may be the audio thread.
    Rig rig("aln783c", "a783c");
    rig.Store("append 10 4 7");

    const std::uint64_t before = rig.len.Dropped();
    rig.len.GetInlet(0)->SetList("array a783c", YSE::T_GUI);
    CHECK(rig.length.gotInt);
    CHECK(rig.length.intValue == 3);
    CHECK(rig.len.Dropped() == before);

    rig.Reset();
    rig.len.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    rig.len.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK_FALSE(rig.length.gotInt);
    CHECK(rig.len.Dropped() == before + 2);
  }

  TEST_CASE(
      "array.length: the reference inlet acknowledges its own array and nothing more (#783)") {
    // Wiring the array's reference outlet across keeps the patch readable,
    // and the acknowledgement is silent — the binding is the creation
    // argument, so there is nothing to set. Anything else there is refused,
    // which is what makes a mis-wired cord visible in Dropped().
    Rig rig("aln783d", "a783d");
    rig.Store("append 10 4 7");

    const std::uint64_t before = rig.len.Dropped();
    rig.len.GetInlet(1)->SetList("array a783d", YSE::T_GUI);
    CHECK_FALSE(rig.length.gotInt);
    CHECK(rig.len.Dropped() == before);

    rig.len.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    rig.len.GetInlet(1)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.len.Dropped() == before + 2);
  }

  TEST_CASE(
      "array.length: wired from the array's reference outlet, banging the array answers (#783)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: the .array's reference outlet into the trigger inlet gives the
    // family gesture — bang the array, out comes its length.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher p;
    p.create(2);
    p.name("aln783e");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a783e");
    YSE::pHandle* len = p.CreateObject(YSE::OBJ::G_ARRAY_LENGTH, "a783e");
    REQUIRE(array != nullptr);
    REQUIRE(len != nullptr);
    p.Connect(array, 1, len, 0);
    p.Connect(len, 0, &sinkHandle, 0);

    array->SetListData(0, "append 60 64 67");
    array->SetBang(0);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 3);

    sink.reset();
    array->SetListData(0, "append 72");
    array->SetBang(0);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 4);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE(
      "array.length: the address form is the patcher's, and RefreshBinding follows it (#783)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aln783f_before");

    gArrayLength g;
    g.SetParams("a783f");
    g.SetParent(&p);
    CHECK(g.ArrayName() == "a783f");
    CHECK(g.Address() == "aln783f_before.a783f");

    // Idempotent: a rebind to the address it already has keeps the store.
    g.RefreshBinding();
    CHECK(g.Address() == "aln783f_before.a783f");

    p.SetName("aln783f_after");
    g.RefreshBinding();
    CHECK(g.Address() == "aln783f_after.a783f");
  }

  TEST_CASE("array.length: patcherImplementation::SetName re-anchors it (#783)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by the
    // patcher, without anybody calling RefreshBinding by hand. The keeper
    // holds the old-address store (it is not in the patcher's object map, so
    // the rename does not touch it): before the rename the ask answers the
    // keeper's three elements; after it the ask reads a fresh empty array
    // under the new prefix, so the answer is 0.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aln783g_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a783g");
    keeper.GetInlet(0)->SetList("append 10 4 7", YSE::T_GUI);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_LENGTH, "a783g");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &sinkHandle, 0);

    h->SetBang(0);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 3);

    p.SetName("aln783g_after");
    sink.reset();
    h->SetBang(0);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 0);
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.length: an ask over in-patcher delivery lands on T_DSP (#783)") {
    // A .r feeding the trigger inlet dispatches on T_DSP when the block
    // drains it (issue #225) — "the audio thread asks for the length" is the
    // ordinary case, and the whole path is one guard hold, one copied count
    // and a send of one int.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aln783h");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go783h");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a783h");
    YSE::pHandle* len = p.CreateObject(YSE::OBJ::G_ARRAY_LENGTH, "a783h");
    REQUIRE(recv != nullptr);
    REQUIRE(array != nullptr);
    REQUIRE(len != nullptr);
    p.Connect(recv, 0, len, 0);
    p.Connect(len, 0, &sinkHandle, 0);

    array->SetListData(0, "append 10 4 7");

    p.PassData(std::string("array a783h"), "go783h", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 3);
  }

  TEST_CASE("array.length: no message path allocates (#783)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path: the bang, the reference ask, the wrong-name
    // refusal on the trigger inlet, and the acknowledgement and refusal on
    // the reference inlet.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "array probeAL783";
    const std::string wrongName = "array somewhere_else_long";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("aln783i");
    MultiSink sink;
    gArray array;
    gArrayLength len;
    array.SetParent(&p);
    array.SetParams("probeAL783");
    len.SetParent(&p);
    len.SetParams("probeAL783");
    Wire(len, 0, sink);

    array.GetInlet(0)->SetList("append 10 4 7", YSE::T_GUI);

    // Warm every path — including the sink's assignment — so first-call
    // machinery is not what the probe catches.
    len.GetInlet(0)->SetBang(YSE::T_GUI);
    len.GetInlet(0)->SetList(reference, YSE::T_GUI);
    len.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    len.GetInlet(1)->SetList(reference, YSE::T_GUI);
    len.GetInlet(1)->SetList(wrongName, YSE::T_GUI);
    const std::uint64_t before = len.Dropped();

    sink.reset();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      len.GetInlet(0)->SetBang(YSE::T_DSP);
      len.GetInlet(0)->SetList(reference, YSE::T_DSP);
      len.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      len.GetInlet(1)->SetList(reference, YSE::T_DSP);
      len.GetInlet(1)->SetList(wrongName, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. The asks answered, and both wrong-name
    // refusals (one per inlet) were counted.
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 3);
    CHECK(len.Dropped() == before + 2);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.length: params survive a DumpJSON / ParseJSON round trip (#783)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_ARRAY_LENGTH, "notes783");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".array.length"));
    CHECK(copy->GetParams() == std::string("notes783"));
    CHECK(copy->GetInputs() == 2);
    CHECK(copy->GetOutputs() == 1);
  }

  TEST_CASE("array.length: carries complete documentation metadata (#783)") {
    gArrayLength g;
    CHECK_FALSE(g.GetDescription().empty());
    CHECK(g.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = g.GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == "name");
  }
}
