// Tests for .dict.join (issue #774) — Max's dict.join on the name-addressed
// value model .dict settled (#550).
//
// What has to be proven, and what every case below is one of:
//
//   - **all three dictionaries are bound from the creation arguments.** A
//     dictionary never travels down a cord, so ".dict.join <left> <right>
//     <target>" resolves the names once, on the control thread, and a
//     `dictionary <name>` message is honoured only when it names a
//     dictionary already bound — DictReferenceNames' bounded compare, never
//     a registry lookup on a message path.
//   - **the join replaces the target with left overlaid by right.** On a
//     colliding key path the right dictionary overwrites the left — Max's
//     own rule — and both sources survive, which is what makes the
//     preset-over-defaults layering composable.
//   - **refusal, never truncation.** Two full dictionaries can join to more
//     than MAX_ENTRIES; an entry past the bound is refused whole and
//     counted, and the rest of the join still lands — the partial merge.
//   - **the join never holds two guards.** Each source is read into the
//     merge buffer under its own guard alone, then the target is written
//     under its guard alone — which is also what makes every aliasing of
//     the three names, ".dict.join a b a" included, safe.
//   - **the join crosses the control/audio boundary and nothing
//     allocates.** In-patcher delivery dispatches on T_DSP, so "the audio
//     thread asks for a join" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names a dictionary uses names of its own — one case's
// contents must not be visible to the next.

#include <doctest/doctest.h>
#include <string>

#include "patcher/genericObjects/gDict.h"
#include "patcher/genericObjects/gDictJoin.h"
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
using YSE::PATCHER::gDictJoin;

namespace {

  // Three .dict objects and a .dict.join over them, sharing one
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
    gDict target;
    gDictJoin join;

    Rig(const std::string& patcherName, const std::string& leftName, const std::string& rightName,
        const std::string& targetName) {
      p.SetName(patcherName);
      left.SetParent(&p);
      left.SetParams(leftName);
      right.SetParent(&p);
      right.SetParams(rightName);
      target.SetParent(&p);
      target.SetParams(targetName);
      join.SetParent(&p);
      join.SetParams(leftName + " " + rightName + " " + targetName);
      Wire(join, 0, out);
    }

    void Left(const std::string& message) {
      left.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Right(const std::string& message) {
      right.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    // Bang the join.
    void Run(YSE::THREAD thread = YSE::T_GUI) {
      out.reset();
      join.GetInlet(0)->SetBang(thread);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("dict.join: registered, two inlets, one outlet (#774)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_JOIN);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".dict.join");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 1);
  }

  TEST_CASE("dict.join: appears in the registry's name list (#774)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_DICT_JOIN)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("dict.join: inlet 0 takes bang and list, inlet 1 list only (#774)") {
    // No int or float handler anywhere: a bare number names no dictionary.
    // And no bang on the right inlet — it only acknowledges a reference, so a
    // bang there would have to invent a meaning.
    gDictJoin g;
    const unsigned int hot = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((hot & YSE::PATCHER::IT_BANG) != 0);
    CHECK((hot & YSE::PATCHER::IT_LIST) != 0);
    const unsigned int cold = g.GetInlet(1)->GetAcceptedTypes();
    CHECK((cold & YSE::PATCHER::IT_BANG) == 0);
    CHECK((cold & YSE::PATCHER::IT_LIST) != 0);
  }

  // ─── the join ───────────────────────────────────────────────────────────────

  TEST_CASE("dict.join: merges disjoint entries and sends the target's reference (#774)") {
    // The base and the overlay hold different keys: the join is their union,
    // and the target's reference leaves the outlet so the rest of the dict.*
    // family can pick the result up.
    Rig rig("dj774a", "base774a", "over774a", "out774a");
    rig.Left("set synth::freq 440");
    rig.Left("set synth::amp 0.5");
    rig.Right("set synth::cutoff 2000");

    rig.Run();
    CHECK(rig.target.Count() == 3);
    CHECK(rig.target.Lookup("synth::freq") == "440");
    CHECK(rig.target.Lookup("synth::amp") == "0.5");
    CHECK(rig.target.Lookup("synth::cutoff") == "2000");

    REQUIRE(rig.out.gotList);
    CHECK(rig.out.listValue == "dictionary out774a");
    CHECK(rig.join.Dropped() == 0);
  }

  TEST_CASE("dict.join: on a colliding key path the right overwrites the left (#774)") {
    // The whole semantics — Max's own rule, and what makes the layering read
    // naturally: the left argument is the base, the right the override.
    Rig rig("dj774b", "base774b", "over774b", "out774b");
    rig.Left("set freq 440");
    rig.Left("set amp 0.5");
    rig.Right("set freq 880");

    rig.Run();
    CHECK(rig.target.Count() == 2);
    CHECK(rig.target.Lookup("freq") == "880");
    CHECK(rig.target.Lookup("amp") == "0.5");
    CHECK(rig.join.Dropped() == 0);
  }

  TEST_CASE("dict.join: both sources survive the join (#774)") {
    // Layering a preset over a base only works if the base and the preset
    // are still there afterwards — the reason the result goes into a third
    // name instead of a source.
    Rig rig("dj774c", "base774c", "over774c", "out774c");
    rig.Left("set freq 440");
    rig.Right("set freq 880");

    rig.Run();
    CHECK(rig.left.Count() == 1);
    CHECK(rig.left.Lookup("freq") == "440");
    CHECK(rig.right.Count() == 1);
    CHECK(rig.right.Lookup("freq") == "880");
  }

  TEST_CASE("dict.join: a join replaces the target whole (#774)") {
    // Re-running is idempotent, and a stale entry from the last run cannot
    // survive into this one — the target holds the join, not an accumulation
    // of every join so far.
    Rig rig("dj774d", "base774d", "over774d", "out774d");
    rig.Left("set lead piano");
    rig.Run();
    CHECK(rig.target.Lookup("lead") == "piano");

    rig.left.GetInlet(0)->SetList("delete lead", YSE::T_GUI);
    rig.Left("set bass organ");
    rig.Run();
    CHECK(rig.target.Count() == 1);
    CHECK(rig.target.Lookup("bass") == "organ");
    CHECK(rig.target.Lookup("lead").empty());
  }

  TEST_CASE("dict.join: naming a source as the target merges in place (#774)") {
    // ".dict.join a b a" — the aliasing the snapshot design makes safe: both
    // sources are fully read into the merge buffer before the target is
    // written, and no store's guard is ever taken while another is held.
    Rig rig("dj774e", "base774e", "over774e", "base774e");
    rig.Left("set freq 440");
    rig.Left("set amp 0.5");
    rig.Right("set freq 880");

    rig.Run();
    CHECK(rig.left.Count() == 2);
    CHECK(rig.left.Lookup("freq") == "880");
    CHECK(rig.left.Lookup("amp") == "0.5");
    // And the overlay is untouched.
    CHECK(rig.right.Count() == 1);
    REQUIRE(rig.out.gotList);
    CHECK(rig.out.listValue == "dictionary base774e");
  }

  TEST_CASE(
      "dict.join: past MAX_ENTRIES the join refuses entry by entry — a partial merge (#774)") {
    // Two dictionaries of up to MAX_ENTRIES each can join to more than
    // MAX_ENTRIES. A right entry with a new key finds the buffer full and is
    // refused whole and counted; a right entry colliding with a left key
    // still lands, because a replacement does not grow the table. Whatever
    // fits — left entries first — is the result.
    Rig rig("dj774f", "base774f", "over774f", "out774f");
    for (std::size_t i = 0; i < YSE::PATCHER::dictStore::MAX_ENTRIES; i++) {
      rig.Left("set k" + std::to_string(i) + " v" + std::to_string(i));
    }
    REQUIRE(rig.left.Count() == YSE::PATCHER::dictStore::MAX_ENTRIES);
    rig.Right("set extra wontfit");
    rig.Right("set k0 replaced");

    const std::uint64_t before = rig.join.Dropped();
    rig.Run();
    CHECK(rig.target.Count() == YSE::PATCHER::dictStore::MAX_ENTRIES);
    CHECK(rig.target.Lookup("k0") == "replaced");
    CHECK(rig.target.Lookup("k1") == "v1");
    CHECK(rig.target.Lookup("extra").empty());
    CHECK(rig.join.Dropped() == before + 1);
  }

  TEST_CASE("dict.join: unnamed sides are private, and an unnamed target says nothing (#774)") {
    // No arguments means private, empty dictionaries — .dict's rule that an
    // unnamed object does not pool on "<patcherName>.". And an unnamed
    // target has no name to pass on, so the outlet stays silent.
    MultiSink sink;
    gDictJoin g;
    Wire(g, 0, sink);
    CHECK(g.LeftName().empty());
    CHECK(g.LeftAddress().empty());
    CHECK(g.RightAddress().empty());
    CHECK(g.TargetAddress().empty());
    g.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(sink.gotList);
    CHECK(g.Dropped() == 0);
  }

  // ─── the reference messages ─────────────────────────────────────────────────

  TEST_CASE("dict.join: the left reference triggers, anything else is refused (#774)") {
    Rig rig("dj774g", "base774g", "over774g", "out774g");
    rig.Left("set lead piano");

    const std::uint64_t before = rig.join.Dropped();
    rig.out.reset();
    rig.join.GetInlet(0)->SetList("dictionary base774g", YSE::T_GUI);
    REQUIRE(rig.out.gotList);
    CHECK(rig.out.listValue == "dictionary out774g");
    CHECK(rig.target.Lookup("lead") == "piano");
    CHECK(rig.join.Dropped() == before);

    // A reference to a dictionary this object is not bound to on this inlet
    // — including the right, which belongs on inlet 1 — and any other
    // message are refused and counted, never resolved: a registry lookup is
    // a mutex, and this may be the audio thread.
    rig.out.reset();
    rig.join.GetInlet(0)->SetList("dictionary somewhere_else", YSE::T_GUI);
    rig.join.GetInlet(0)->SetList("dictionary over774g", YSE::T_GUI);
    rig.join.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.join.Dropped() == before + 3);
  }

  TEST_CASE("dict.join: inlet 1 acknowledges the right reference and nothing else (#774)") {
    // The binding is the second creation argument, so the reference is
    // accepted silently — a patch may wire both reference outlets across —
    // and sets nothing.
    Rig rig("dj774h", "base774h", "over774h", "out774h");
    const std::uint64_t before = rig.join.Dropped();

    rig.join.GetInlet(1)->SetList("dictionary over774h", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.join.Dropped() == before);

    rig.join.GetInlet(1)->SetList("dictionary base774h", YSE::T_GUI);
    rig.join.GetInlet(1)->SetList("dictionary elsewhere", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.join.Dropped() == before + 2);
  }

  TEST_CASE("dict.join: wired from the base's reference outlet, banging the dict joins (#774)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: the base .dict's reference outlet into the join, a bang on the
    // .dict, and the layered result at the far end. Max's own gesture.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher p;
    p.create(2);
    p.name("dj774i");
    YSE::pHandle* dictBase = p.CreateObject(YSE::OBJ::G_DICT, "defaults774i");
    YSE::pHandle* dictOver = p.CreateObject(YSE::OBJ::G_DICT, "preset774i");
    YSE::pHandle* dictOut = p.CreateObject(YSE::OBJ::G_DICT, "live774i");
    YSE::pHandle* jn = p.CreateObject(YSE::OBJ::G_DICT_JOIN, "defaults774i preset774i live774i");
    REQUIRE(dictBase != nullptr);
    REQUIRE(dictOver != nullptr);
    REQUIRE(dictOut != nullptr);
    REQUIRE(jn != nullptr);
    p.Connect(dictBase, 1, jn, 0);
    p.Connect(jn, 0, &sinkHandle, 0);

    dictBase->SetListData(0, "set freq 440");
    dictBase->SetListData(0, "set amp 0.5");
    dictOver->SetListData(0, "set freq 880");
    dictBase->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "dictionary live774i");

    // The layered result is readable through the target .dict: the preset's
    // freq over the defaults' amp. A fetched value leaves typed by the
    // outlet's classifier — "880" an int, "0.5" a float — .dict's rule.
    sink.reset();
    p.Connect(dictOut, 0, &sinkHandle, 0);
    dictOut->SetListData(0, "get freq");
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 880);
    sink.reset();
    dictOut->SetListData(0, "get amp");
    REQUIRE(sink.gotFloat);
    CHECK(sink.floatValue == 0.5f);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("dict.join: the address form is the patcher's, and RefreshBinding follows it (#774)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dj774j_before");

    gDictJoin g;
    g.SetParams("l774j r774j t774j");
    g.SetParent(&p);
    CHECK(g.LeftAddress() == "dj774j_before.l774j");
    CHECK(g.RightAddress() == "dj774j_before.r774j");
    CHECK(g.TargetAddress() == "dj774j_before.t774j");

    // Idempotent: a rebind to the addresses it already has keeps the stores.
    g.RefreshBinding();
    CHECK(g.LeftAddress() == "dj774j_before.l774j");

    p.SetName("dj774j_after");
    g.RefreshBinding();
    CHECK(g.LeftAddress() == "dj774j_after.l774j");
    CHECK(g.RightAddress() == "dj774j_after.r774j");
    CHECK(g.TargetAddress() == "dj774j_after.t774j");
  }

  TEST_CASE("dict.join: patcherImplementation::SetName re-anchors it (#774)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by the
    // patcher, without anybody calling RefreshBinding by hand. The keepers
    // hold the old-address stores (they are not in the patcher's object map,
    // so the rename does not touch them): before the rename a bang joins the
    // keeper sources into the keeper target; after it every side is a fresh
    // empty dictionary under the new prefix, so a second bang leaves the old
    // target exactly as it was.
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dj774k_before");

    gDict keeperLeft;
    keeperLeft.SetParent(&p);
    keeperLeft.SetParams("l774k");
    keeperLeft.GetInlet(0)->SetList("set lead piano", YSE::T_GUI);
    gDict keeperRight;
    keeperRight.SetParent(&p);
    keeperRight.SetParams("r774k");
    keeperRight.GetInlet(0)->SetList("set bass synth", YSE::T_GUI);
    gDict keeperTarget;
    keeperTarget.SetParent(&p);
    keeperTarget.SetParams("t774k");

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_JOIN, "l774k r774k t774k");
    REQUIRE(h != nullptr);

    h->SetBang(0);
    CHECK(keeperTarget.Count() == 2);
    CHECK(keeperTarget.Lookup("lead") == "piano");
    CHECK(keeperTarget.Lookup("bass") == "synth");

    p.SetName("dj774k_after");
    keeperLeft.GetInlet(0)->SetList("set pad organ", YSE::T_GUI);
    h->SetBang(0);
    // The old target was neither replaced nor extended: the join now reads
    // and writes dictionaries under the new prefix.
    CHECK(keeperTarget.Count() == 2);
    CHECK(keeperTarget.Lookup("pad").empty());
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("dict.join: a join asked for over in-patcher delivery lands on T_DSP (#774)") {
    // A .r feeding the join dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread asks for the join" is the ordinary
    // case, and the whole path is bounded copies through the merge buffer
    // and a send of a string the object already owns.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dj774l");

    gDict keeperTarget;
    keeperTarget.SetParent(&p);
    keeperTarget.SetParams("t774l");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go774l");
    YSE::pHandle* dictLeft = p.CreateObject(YSE::OBJ::G_DICT, "l774l");
    YSE::pHandle* dictRight = p.CreateObject(YSE::OBJ::G_DICT, "r774l");
    YSE::pHandle* jn = p.CreateObject(YSE::OBJ::G_DICT_JOIN, "l774l r774l t774l");
    REQUIRE(recv != nullptr);
    REQUIRE(dictLeft != nullptr);
    REQUIRE(dictRight != nullptr);
    REQUIRE(jn != nullptr);
    p.Connect(recv, 0, jn, 0);
    p.Connect(jn, 0, &sinkHandle, 0);

    dictLeft->SetListData(0, "set freq 440");
    dictRight->SetListData(0, "set freq 880");

    p.PassData(std::string("dictionary l774l"), "go774l", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "dictionary t774l");
    CHECK(keeperTarget.Lookup("freq") == "880");
  }

  TEST_CASE("dict.join: no message path allocates (#774)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path: the trigger (bang and reference alike, with a
    // collision to replace and a full-buffer refusal to count), the
    // acknowledgment and the refusal.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer, and that buffer is not
    // the same width everywhere (15 on libstdc++, 22 on libc++).
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string refLeft = "dictionary probeL774";
    const std::string refRight = "dictionary probeR774";
    const std::string unknown = "frobnicate something quite long indeed";

    Rig rig("dj774m", "probeL774", "probeR774", "probeT774");
    rig.Left("set synth::freq a value past every small-string buffer");
    rig.Left("set synth::amp 0.5");
    rig.Right("set synth::freq 880 with a long tail of extra atoms");
    rig.Right("set synth::cutoff 2000");

    // Warm every path — including the sink's list assignment — so
    // first-call machinery is not what the probe catches.
    rig.Run();
    rig.join.GetInlet(0)->SetList(refLeft, YSE::T_GUI);
    rig.join.GetInlet(1)->SetList(refRight, YSE::T_GUI);
    rig.join.GetInlet(0)->SetList(unknown, YSE::T_GUI);
    const std::uint64_t before = rig.join.Dropped();

    rig.out.reset();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      rig.join.GetInlet(0)->SetBang(YSE::T_DSP);
      rig.join.GetInlet(0)->SetList(refLeft, YSE::T_DSP);
      rig.join.GetInlet(1)->SetList(refRight, YSE::T_DSP);
      rig.join.GetInlet(0)->SetList(unknown, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. Two joins ran (a bang and a reference), plus
    // the unknown message's refusal.
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "dictionary probeT774");
    CHECK(rig.target.Count() == 3);
    CHECK(rig.target.Lookup("synth::freq") == "880 with a long tail of extra atoms");
    CHECK(rig.join.Dropped() == before + 1);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("dict.join: params survive a DumpJSON / ParseJSON round trip (#774)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_DICT_JOIN, "defaults774n preset774n live774n");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".dict.join"));
    CHECK(copy->GetParams() == std::string("defaults774n preset774n live774n"));
    CHECK(copy->GetInputs() == 2);
    CHECK(copy->GetOutputs() == 1);
  }

  TEST_CASE("dict.join: carries complete documentation metadata (#774)") {
    gDictJoin g;
    CHECK_FALSE(g.GetDescription().empty());
    CHECK(g.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = g.GetParamDocs();
    REQUIRE(docs.size() == 3);
    CHECK(docs[0].name == "left");
    CHECK(docs[1].name == "right");
    CHECK(docs[2].name == "target");
  }
}
