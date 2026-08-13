// Tests for .dict.compare (issue #770) — Max's dict.compare on the
// name-addressed value model .dict settled (#550).
//
// What has to be proven, and what every case below is one of:
//
//   - **both dictionaries are bound from the creation arguments.** A
//     dictionary never travels down a cord, so ".dict.compare <left> <right>"
//     resolves both names once, on the control thread, and a `dictionary
//     <name>` message is honoured only when it names a dictionary already
//     bound — DictReferenceNames' bounded compare, never a registry lookup on
//     a message path.
//   - **equality is the mapping, not the storage.** The same entries in a
//     different order are the same dictionary; a differing value, a missing
//     key or an extra key are not. Values compare as the list text they are
//     stored as, so "2" and "2." differ — they are a different atom
//     downstream.
//   - **the comparison never holds two guards.** The left store is copied out
//     under its guard into a snapshot reserved at construction, then compared
//     against the right store under that guard alone — which is also what
//     makes the same-store case (one name on both sides) safe.
//   - **the verdict crosses the control/audio boundary and nothing
//     allocates.** In-patcher delivery dispatches on T_DSP, so "the audio
//     thread asks for a comparison" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names a dictionary uses names of its own — one case's
// contents must not be visible to the next.

#include <doctest/doctest.h>
#include <string>

#include "patcher/genericObjects/gDict.h"
#include "patcher/genericObjects/gDictCompare.h"
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
using YSE::PATCHER::gDict;
using YSE::PATCHER::gDictCompare;

namespace {

  // Two .dict objects and a .dict.compare over them, sharing one
  // patcherImplementation so the names actually bind ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sink is declared before the objects so they are torn down first, while
  // the inlet it is wired to still exists (see sinks.hpp on why that
  // matters).
  struct Rig {
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    MultiSink out;
    gDict left;
    gDict right;
    gDictCompare cmp;

    Rig(const std::string& patcherName, const std::string& leftName, const std::string& rightName) {
      p.SetName(patcherName);
      left.SetParent(&p);
      left.SetParams(leftName);
      right.SetParent(&p);
      right.SetParams(rightName);
      cmp.SetParent(&p);
      cmp.SetParams(leftName + " " + rightName);
      Wire(cmp, 0, out);
    }

    void Left(const std::string& message) {
      left.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Right(const std::string& message) {
      right.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    // Bang the comparison and return the verdict.
    int Verdict(YSE::THREAD thread = YSE::T_GUI) {
      out.reset();
      cmp.GetInlet(0)->SetBang(thread);
      REQUIRE(out.gotInt);
      return out.intValue;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("dict.compare: registered, two inlets, one outlet (#770)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_COMPARE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".dict.compare");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 1);
  }

  TEST_CASE("dict.compare: appears in the registry's name list (#770)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_DICT_COMPARE)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("dict.compare: inlet 0 takes bang and list, inlet 1 list only (#770)") {
    // No int or float handler anywhere: a bare number names no dictionary.
    // And no bang on the right inlet — it only acknowledges a reference, so a
    // bang there would have to invent a meaning.
    gDictCompare c;
    const unsigned int hot = c.GetInlet(0)->GetAcceptedTypes();
    CHECK((hot & YSE::PATCHER::IT_BANG) != 0);
    CHECK((hot & YSE::PATCHER::IT_LIST) != 0);
    const unsigned int cold = c.GetInlet(1)->GetAcceptedTypes();
    CHECK((cold & YSE::PATCHER::IT_BANG) == 0);
    CHECK((cold & YSE::PATCHER::IT_LIST) != 0);
  }

  // ─── the comparison ─────────────────────────────────────────────────────────

  TEST_CASE("dict.compare: reports 1 for the same entries and 0 for a difference (#770)") {
    Rig rig("dc770a", "left770a", "right770a");

    // Two empty dictionaries are the same dictionary.
    CHECK(rig.Verdict() == 1);

    rig.Left("set tempo 120");
    CHECK(rig.Verdict() == 0);

    rig.Right("set tempo 120");
    CHECK(rig.Verdict() == 1);

    // Same key, different value.
    rig.Right("set tempo 140");
    CHECK(rig.Verdict() == 0);
  }

  TEST_CASE("dict.compare: equality is order-insensitive and value-exact (#770)") {
    Rig rig("dc770b", "left770b", "right770b");

    // The same pairs stored in a different order are the same mapping —
    // storage order is what getkeys reports, not what a dictionary is.
    rig.Left("set voice::1::freq 440");
    rig.Left("set voice::2::freq 550");
    rig.Right("set voice::2::freq 550");
    rig.Right("set voice::1::freq 440");
    CHECK(rig.Verdict() == 1);

    // Values compare as the list text they are stored as: "2" and "2." are a
    // different atom downstream, so they are a different value here.
    rig.Left("set gain 2");
    rig.Right("set gain 2.");
    CHECK(rig.Verdict() == 0);
  }

  TEST_CASE("dict.compare: a subset is not equality, in either direction (#770)") {
    Rig rig("dc770c", "left770c", "right770c");
    rig.Left("set a 1");
    rig.Left("set b 2");
    rig.Right("set a 1");
    // Left holds an entry right does not.
    CHECK(rig.Verdict() == 0);

    rig.Right("set b 2");
    rig.Right("set c 3");
    // And the mirror: every left entry is in right, but right holds more.
    CHECK(rig.Verdict() == 0);
  }

  TEST_CASE("dict.compare: one name on both sides always compares equal (#770)") {
    // The degenerate case the snapshot design makes safe: both arguments bind
    // one store, and the comparison compares it against itself without ever
    // taking its guard twice.
    Rig rig("dc770d", "kit770d", "kit770d");
    CHECK(rig.Verdict() == 1);
    rig.Left("set snare 38");
    rig.Left("set kick 36");
    CHECK(rig.Verdict() == 1);
  }

  TEST_CASE("dict.compare: unnamed sides are private and empty (#770)") {
    // No arguments means two private, empty dictionaries — .dict's rule that
    // an unnamed object does not pool on "<patcherName>.". Nothing can store
    // into them, so they stay equal.
    MultiSink sink;
    gDictCompare c;
    Wire(c, 0, sink);
    CHECK(c.LeftName().empty());
    CHECK(c.LeftAddress().empty());
    CHECK(c.RightAddress().empty());
    c.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 1);
  }

  // ─── the reference messages ─────────────────────────────────────────────────

  TEST_CASE("dict.compare: the left reference triggers, anything else is refused (#770)") {
    Rig rig("dc770e", "left770e", "right770e");
    rig.Left("set a 1");

    const std::uint64_t before = rig.cmp.Dropped();
    rig.out.reset();
    rig.cmp.GetInlet(0)->SetList("dictionary left770e", YSE::T_GUI);
    REQUIRE(rig.out.gotInt);
    CHECK(rig.out.intValue == 0);
    CHECK(rig.cmp.Dropped() == before);

    // A reference to a dictionary this object is not bound to — including the
    // right one, which belongs on inlet 1 — and any other message are refused
    // and counted, never resolved: a registry lookup is a mutex, and this may
    // be the audio thread.
    rig.out.reset();
    rig.cmp.GetInlet(0)->SetList("dictionary somewhere_else", YSE::T_GUI);
    rig.cmp.GetInlet(0)->SetList("dictionary right770e", YSE::T_GUI);
    rig.cmp.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotInt);
    CHECK(rig.cmp.Dropped() == before + 3);
  }

  TEST_CASE("dict.compare: inlet 1 acknowledges the right reference and nothing else (#770)") {
    // Max's right inlet sets the dictionary to compare against; here that
    // binding is the second creation argument, so the reference is accepted
    // silently — a patch may wire both reference outlets across — and sets
    // nothing.
    Rig rig("dc770f", "left770f", "right770f");
    const std::uint64_t before = rig.cmp.Dropped();

    rig.cmp.GetInlet(1)->SetList("dictionary right770f", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotInt);
    CHECK(rig.cmp.Dropped() == before);

    rig.cmp.GetInlet(1)->SetList("dictionary left770f", YSE::T_GUI);
    rig.cmp.GetInlet(1)->SetList("dictionary elsewhere", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotInt);
    CHECK(rig.cmp.Dropped() == before + 2);
  }

  TEST_CASE("dict.compare: wired from the reference outlets, banging the dict compares (#770)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: each .dict's reference outlet into the comparison, a bang on the
    // left .dict, and the verdict at the far end. Max's own gesture.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher p;
    p.create(2);
    p.name("dc770g");
    YSE::pHandle* dictLeft = p.CreateObject(YSE::OBJ::G_DICT, "running770g");
    YSE::pHandle* dictRight = p.CreateObject(YSE::OBJ::G_DICT, "preset770g");
    YSE::pHandle* cmp = p.CreateObject(YSE::OBJ::G_DICT_COMPARE, "running770g preset770g");
    REQUIRE(dictLeft != nullptr);
    REQUIRE(dictRight != nullptr);
    REQUIRE(cmp != nullptr);
    p.Connect(dictLeft, 1, cmp, 0);
    p.Connect(dictRight, 1, cmp, 1);
    p.Connect(cmp, 0, &sinkHandle, 0);

    dictLeft->SetListData(0, "set voice::1::freq 440");
    dictRight->SetListData(0, "set voice::1::freq 440");
    dictLeft->SetBang(0);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 1);

    // The preset drifts; the same bang now reports the difference.
    sink.reset();
    dictRight->SetListData(0, "set voice::1::freq 550");
    dictLeft->SetBang(0);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 0);

    // And the right dict's bang lands on inlet 1 without a refusal or an
    // output — the wiring Max's patch shape produces.
    sink.reset();
    dictRight->SetBang(0);
    CHECK_FALSE(sink.gotInt);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE(
      "dict.compare: the address form is the patcher's, and RefreshBinding follows it (#770)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dc770h_before");

    gDictCompare c;
    c.SetParams("l770h r770h");
    c.SetParent(&p);
    CHECK(c.LeftAddress() == "dc770h_before.l770h");
    CHECK(c.RightAddress() == "dc770h_before.r770h");

    // Idempotent: a rebind to the addresses it already has keeps the stores.
    c.RefreshBinding();
    CHECK(c.LeftAddress() == "dc770h_before.l770h");

    p.SetName("dc770h_after");
    c.RefreshBinding();
    CHECK(c.LeftAddress() == "dc770h_after.l770h");
    CHECK(c.RightAddress() == "dc770h_after.r770h");
  }

  TEST_CASE("dict.compare: patcherImplementation::SetName re-anchors it (#770)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by the
    // patcher, without anybody calling RefreshBinding by hand. The keeper
    // holds the old-address store (it is not in the patcher's object map, so
    // the rename does not touch it): before the rename the comparison sees
    // its entry and reports a difference, after it both sides are fresh empty
    // dictionaries under the new prefix and the difference is gone.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dc770i_before");

    gDict keeper;
    keeper.SetParent(&p);
    keeper.SetParams("left770i");
    keeper.GetInlet(0)->SetList("set a 1", YSE::T_GUI);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_COMPARE, "left770i right770i");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &sinkHandle, 0);

    h->SetBang(0);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 0);

    p.SetName("dc770i_after");
    sink.reset();
    h->SetBang(0);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 1);
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("dict.compare: a comparison asked for over in-patcher delivery lands on T_DSP (#770)") {
    // A .r feeding the comparison dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread asks for the comparison" is the
    // ordinary case, and the whole path is a snapshot, a bounded scan and an
    // int send.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dc770j");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go770j");
    YSE::pHandle* dictLeft = p.CreateObject(YSE::OBJ::G_DICT, "a770j");
    YSE::pHandle* dictRight = p.CreateObject(YSE::OBJ::G_DICT, "b770j");
    YSE::pHandle* cmp = p.CreateObject(YSE::OBJ::G_DICT_COMPARE, "a770j b770j");
    REQUIRE(recv != nullptr);
    REQUIRE(dictLeft != nullptr);
    REQUIRE(dictRight != nullptr);
    REQUIRE(cmp != nullptr);
    p.Connect(recv, 0, cmp, 0);
    p.Connect(cmp, 0, &sinkHandle, 0);

    dictLeft->SetListData(0, "set voice::1::freq 440");
    dictRight->SetListData(0, "set voice::1::freq 440");

    p.PassData(std::string("dictionary a770j"), "go770j", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 1);
  }

  TEST_CASE("dict.compare: no message path allocates (#770)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path: the trigger, the acknowledgment and the refusal.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer, and that buffer is not
    // the same width everywhere (15 on libstdc++, 22 on libc++).
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string refLeft = "dictionary probeL770";
    const std::string refRight = "dictionary probeR770";
    const std::string unknown = "frobnicate something quite long indeed";

    Rig rig("dc770k", "probeL770", "probeR770");
    rig.Left("set voice::1::freq 440");
    rig.Left("set voice::1::name a value past every small-string buffer");
    rig.Left("set chord 0 4 7 12");
    rig.Right("set chord 0 4 7 12");
    rig.Right("set voice::1::name a value past every small-string buffer");
    rig.Right("set voice::1::freq 440");

    // Warm every path, so first-call machinery is not what the probe catches.
    CHECK(rig.Verdict() == 1);
    rig.cmp.GetInlet(0)->SetList(refLeft, YSE::T_GUI);
    rig.cmp.GetInlet(1)->SetList(refRight, YSE::T_GUI);
    rig.cmp.GetInlet(0)->SetList(unknown, YSE::T_GUI);
    const std::uint64_t before = rig.cmp.Dropped();

    rig.out.reset();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      rig.cmp.GetInlet(0)->SetBang(YSE::T_DSP);
      rig.cmp.GetInlet(0)->SetList(refLeft, YSE::T_DSP);
      rig.cmp.GetInlet(1)->SetList(refRight, YSE::T_DSP);
      rig.cmp.GetInlet(0)->SetList(unknown, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing.
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 1);
    CHECK(rig.cmp.Dropped() == before + 1);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("dict.compare: params survive a DumpJSON / ParseJSON round trip (#770)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_DICT_COMPARE, "kitA770 kitB770");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".dict.compare"));
    CHECK(copy->GetParams() == std::string("kitA770 kitB770"));
    CHECK(copy->GetInputs() == 2);
    CHECK(copy->GetOutputs() == 1);
  }

  TEST_CASE("dict.compare: carries complete documentation metadata (#770)") {
    gDictCompare c;
    CHECK_FALSE(c.GetDescription().empty());
    CHECK(c.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = c.GetParamDocs();
    REQUIRE(docs.size() == 2);
    CHECK(docs[0].name == "left");
    CHECK(docs[1].name == "right");
  }
}
