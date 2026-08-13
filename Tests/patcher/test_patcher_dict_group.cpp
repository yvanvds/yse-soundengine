// Tests for .dict.group (issue #772) — Max's dict.group on the
// name-addressed value model .dict settled (#550).
//
// What has to be proven, and what every case below is one of:
//
//   - **both dictionaries are bound from the creation arguments.** A
//     dictionary never travels down a cord, so ".dict.group <source>
//     <target> [<key>]" resolves both names once, on the control thread, and
//     a `dictionary <name>` message is honoured only when it names a
//     dictionary already bound — DictReferenceNames' bounded compare, never
//     a registry lookup on a message path.
//   - **grouping writes "<groupValue>::<originalPath>" into the target,
//     replacing it whole.** With a key the source is a table of records and
//     each entry groups under its record's value at that key; without one
//     each entry groups under its own value — the flat table becoming an
//     index.
//   - **refusal, never truncation.** An entry without a group, an empty
//     group value, and a composed path past KEY_CAPACITY are skipped and
//     counted; the rest of the grouping still lands.
//   - **the grouping never holds two guards.** The source is copied out
//     under its guard into a snapshot reserved at construction, then the
//     target is written under that guard alone — which is also what makes
//     the same-store case (one name on both sides) safe.
//   - **the grouping crosses the control/audio boundary and nothing
//     allocates.** In-patcher delivery dispatches on T_DSP, so "the audio
//     thread asks for a grouping" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names a dictionary uses names of its own — one case's
// contents must not be visible to the next.

#include <doctest/doctest.h>
#include <string>

#include "patcher/genericObjects/gDict.h"
#include "patcher/genericObjects/gDictGroup.h"
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
using YSE::PATCHER::gDictGroup;

namespace {

  // Two .dict objects and a .dict.group over them, sharing one
  // patcherImplementation so the names actually bind ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sink is declared before the objects so they are torn down first, while
  // the inlet it is wired to still exists (see sinks.hpp on why that
  // matters).
  struct Rig {
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    MultiSink out;
    gDict source;
    gDict target;
    gDictGroup group;

    Rig(const std::string& patcherName, const std::string& sourceName,
        const std::string& targetName, const std::string& key = std::string()) {
      p.SetName(patcherName);
      source.SetParent(&p);
      source.SetParams(sourceName);
      target.SetParent(&p);
      target.SetParams(targetName);
      group.SetParent(&p);
      group.SetParams(key.empty() ? sourceName + " " + targetName
                                  : sourceName + " " + targetName + " " + key);
      Wire(group, 0, out);
    }

    void Source(const std::string& message) {
      source.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    // Bang the grouping.
    void Run(YSE::THREAD thread = YSE::T_GUI) {
      out.reset();
      group.GetInlet(0)->SetBang(thread);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("dict.group: registered, two inlets, one outlet (#772)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_GROUP);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".dict.group");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 1);
  }

  TEST_CASE("dict.group: appears in the registry's name list (#772)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_DICT_GROUP)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("dict.group: inlet 0 takes bang and list, inlet 1 list only (#772)") {
    // No int or float handler anywhere: a bare number names no dictionary.
    // And no bang on the right inlet — it only acknowledges a reference, so a
    // bang there would have to invent a meaning.
    gDictGroup g;
    const unsigned int hot = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((hot & YSE::PATCHER::IT_BANG) != 0);
    CHECK((hot & YSE::PATCHER::IT_LIST) != 0);
    const unsigned int cold = g.GetInlet(1)->GetAcceptedTypes();
    CHECK((cold & YSE::PATCHER::IT_BANG) == 0);
    CHECK((cold & YSE::PATCHER::IT_LIST) != 0);
  }

  // ─── the grouping ───────────────────────────────────────────────────────────

  TEST_CASE("dict.group: groups records by the value at the key argument (#772)") {
    // The table of voices, grouped by their instrument: the first path
    // segment names the record, and every entry of a record lands under the
    // value its record stores at the key.
    Rig rig("dg772a", "src772a", "tgt772a", "instrument");
    rig.Source("set 1::instrument piano");
    rig.Source("set 1::freq 440");
    rig.Source("set 2::instrument drum");
    rig.Source("set 2::freq 100");

    rig.Run();
    CHECK(rig.target.Count() == 4);
    CHECK(rig.target.Lookup("piano::1::instrument") == "piano");
    CHECK(rig.target.Lookup("piano::1::freq") == "440");
    CHECK(rig.target.Lookup("drum::2::instrument") == "drum");
    CHECK(rig.target.Lookup("drum::2::freq") == "100");

    // And the target's reference leaves the outlet, so the rest of the
    // dict.* family can pick the result up.
    REQUIRE(rig.out.gotList);
    CHECK(rig.out.listValue == "dictionary tgt772a");
    CHECK(rig.group.Dropped() == 0);
  }

  TEST_CASE("dict.group: without a key, each entry groups under its own value (#772)") {
    // The flat table becoming an index — the use case the object exists for:
    // voice = instrument turns into instrument::voice, so "which voices play
    // synth?" is a prefix walk.
    Rig rig("dg772b", "src772b", "tgt772b");
    rig.Source("set lead piano");
    rig.Source("set bass synth");
    rig.Source("set pad synth");

    rig.Run();
    CHECK(rig.target.Count() == 3);
    CHECK(rig.target.Lookup("piano::lead") == "piano");
    CHECK(rig.target.Lookup("synth::bass") == "synth");
    CHECK(rig.target.Lookup("synth::pad") == "synth");
    CHECK(rig.group.Dropped() == 0);
  }

  TEST_CASE("dict.group: a grouping replaces the target whole (#772)") {
    // Re-running is idempotent, and a stale entry from the last run cannot
    // survive into this one — the target holds the result, not an
    // accumulation of every result so far.
    Rig rig("dg772c", "src772c", "tgt772c");
    rig.Source("set lead piano");
    rig.Run();
    CHECK(rig.target.Lookup("piano::lead") == "piano");

    rig.source.GetInlet(0)->SetList("delete lead", YSE::T_GUI);
    rig.Source("set lead organ");
    rig.Run();
    CHECK(rig.target.Count() == 1);
    CHECK(rig.target.Lookup("organ::lead") == "organ");
    CHECK(rig.target.Lookup("piano::lead").empty());
  }

  TEST_CASE("dict.group: an entry whose record has no group key is skipped and counted (#772)") {
    Rig rig("dg772d", "src772d", "tgt772d", "instrument");
    rig.Source("set 1::instrument piano");
    rig.Source("set 1::freq 440");
    rig.Source("set 2::freq 550");

    const std::uint64_t before = rig.group.Dropped();
    rig.Run();
    // Record 2 holds no instrument, so its entry has no group — skipped and
    // counted rather than invented, and the rest of the grouping still lands.
    CHECK(rig.target.Count() == 2);
    CHECK(rig.target.Lookup("piano::1::freq") == "440");
    CHECK(rig.target.Lookup("piano::1::instrument") == "piano");
    CHECK(rig.group.Dropped() == before + 1);
  }

  TEST_CASE("dict.group: a composed path past KEY_CAPACITY is refused, never truncated (#772)") {
    // Prepending the group value can overflow the key capacity where the
    // input did not: a 100-character group before a 30-character path is 132
    // characters of key. Refused whole and counted — the issue's own rule —
    // while an entry that still fits lands.
    Rig rig("dg772e", "src772e", "tgt772e");
    const std::string longValue(100, 'v');
    const std::string longKey(30, 'k');
    rig.Source("set " + longKey + " " + longValue);
    rig.Source("set short ok");

    const std::uint64_t before = rig.group.Dropped();
    rig.Run();
    CHECK(rig.target.Count() == 1);
    CHECK(rig.target.Lookup("ok::short") == "ok");
    CHECK(rig.group.Dropped() == before + 1);
  }

  TEST_CASE("dict.group: an entry holding nothing has no group (#772)") {
    // An empty group value would compose "::<path>" — an empty path segment,
    // which is not a path the store can express. Skipped and counted.
    Rig rig("dg772f", "src772f", "tgt772f");
    rig.Source("set silent");
    rig.Source("set lead piano");

    const std::uint64_t before = rig.group.Dropped();
    rig.Run();
    CHECK(rig.target.Count() == 1);
    CHECK(rig.target.Lookup("piano::lead") == "piano");
    CHECK(rig.group.Dropped() == before + 1);
  }

  TEST_CASE("dict.group: one name on both sides groups in place (#772)") {
    // The degenerate case the snapshot design makes safe: both arguments
    // bind one store, the snapshot is taken before the store is replaced,
    // and its guard is never taken twice.
    Rig rig("dg772g", "kit772g", "kit772g");
    rig.Source("set lead piano");
    rig.Run();
    CHECK(rig.source.Count() == 1);
    CHECK(rig.source.Lookup("piano::lead") == "piano");
    CHECK(rig.source.Lookup("lead").empty());
  }

  TEST_CASE("dict.group: unnamed sides are private, and an unnamed target says nothing (#772)") {
    // No arguments means two private, empty dictionaries — .dict's rule that
    // an unnamed object does not pool on "<patcherName>.". And an unnamed
    // target has no name to pass on, so the outlet stays silent.
    MultiSink sink;
    gDictGroup g;
    Wire(g, 0, sink);
    CHECK(g.SourceName().empty());
    CHECK(g.SourceAddress().empty());
    CHECK(g.TargetAddress().empty());
    g.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(sink.gotList);
    CHECK(g.Dropped() == 0);
  }

  // ─── the reference messages ─────────────────────────────────────────────────

  TEST_CASE("dict.group: the source reference triggers, anything else is refused (#772)") {
    Rig rig("dg772h", "src772h", "tgt772h");
    rig.Source("set lead piano");

    const std::uint64_t before = rig.group.Dropped();
    rig.out.reset();
    rig.group.GetInlet(0)->SetList("dictionary src772h", YSE::T_GUI);
    REQUIRE(rig.out.gotList);
    CHECK(rig.out.listValue == "dictionary tgt772h");
    CHECK(rig.target.Lookup("piano::lead") == "piano");
    CHECK(rig.group.Dropped() == before);

    // A reference to a dictionary this object is not bound to — including
    // the target, which belongs on inlet 1 — and any other message are
    // refused and counted, never resolved: a registry lookup is a mutex, and
    // this may be the audio thread.
    rig.out.reset();
    rig.group.GetInlet(0)->SetList("dictionary somewhere_else", YSE::T_GUI);
    rig.group.GetInlet(0)->SetList("dictionary tgt772h", YSE::T_GUI);
    rig.group.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.group.Dropped() == before + 3);
  }

  TEST_CASE("dict.group: inlet 1 acknowledges the target reference and nothing else (#772)") {
    // The binding is the second creation argument, so the reference is
    // accepted silently — a patch may wire the target's reference outlet
    // across — and sets nothing.
    Rig rig("dg772i", "src772i", "tgt772i");
    const std::uint64_t before = rig.group.Dropped();

    rig.group.GetInlet(1)->SetList("dictionary tgt772i", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.group.Dropped() == before);

    rig.group.GetInlet(1)->SetList("dictionary src772i", YSE::T_GUI);
    rig.group.GetInlet(1)->SetList("dictionary elsewhere", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.group.Dropped() == before + 2);
  }

  TEST_CASE(
      "dict.group: wired from the source's reference outlet, banging the dict groups (#772)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: the source .dict's reference outlet into the grouping, a bang on
    // the .dict, and the grouped target at the far end. Max's own gesture.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher p;
    p.create(2);
    p.name("dg772j");
    YSE::pHandle* dictSource = p.CreateObject(YSE::OBJ::G_DICT, "voices772j");
    YSE::pHandle* dictTarget = p.CreateObject(YSE::OBJ::G_DICT, "byinst772j");
    YSE::pHandle* grp = p.CreateObject(YSE::OBJ::G_DICT_GROUP, "voices772j byinst772j");
    REQUIRE(dictSource != nullptr);
    REQUIRE(dictTarget != nullptr);
    REQUIRE(grp != nullptr);
    p.Connect(dictSource, 1, grp, 0);
    p.Connect(grp, 0, &sinkHandle, 0);

    dictSource->SetListData(0, "set lead piano");
    dictSource->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "dictionary byinst772j");

    // The grouped result is readable through the target .dict.
    sink.reset();
    p.Connect(dictTarget, 0, &sinkHandle, 0);
    dictTarget->SetListData(0, "get piano::lead");
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "piano");
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("dict.group: the address form is the patcher's, and RefreshBinding follows it (#772)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dg772k_before");

    gDictGroup g;
    g.SetParams("s772k t772k instrument");
    g.SetParent(&p);
    CHECK(g.SourceAddress() == "dg772k_before.s772k");
    CHECK(g.TargetAddress() == "dg772k_before.t772k");
    CHECK(g.GroupKey() == "instrument");

    // Idempotent: a rebind to the addresses it already has keeps the stores.
    g.RefreshBinding();
    CHECK(g.SourceAddress() == "dg772k_before.s772k");

    p.SetName("dg772k_after");
    g.RefreshBinding();
    CHECK(g.SourceAddress() == "dg772k_after.s772k");
    CHECK(g.TargetAddress() == "dg772k_after.t772k");
  }

  TEST_CASE("dict.group: patcherImplementation::SetName re-anchors it (#772)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by the
    // patcher, without anybody calling RefreshBinding by hand. The keepers
    // hold the old-address stores (they are not in the patcher's object map,
    // so the rename does not touch them): before the rename a bang groups
    // the keeper source into the keeper target; after it both sides are
    // fresh empty dictionaries under the new prefix, so a second bang leaves
    // the old target exactly as it was.
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dg772l_before");

    gDict keeperSource;
    keeperSource.SetParent(&p);
    keeperSource.SetParams("src772l");
    keeperSource.GetInlet(0)->SetList("set lead piano", YSE::T_GUI);
    gDict keeperTarget;
    keeperTarget.SetParent(&p);
    keeperTarget.SetParams("tgt772l");

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_GROUP, "src772l tgt772l");
    REQUIRE(h != nullptr);

    h->SetBang(0);
    CHECK(keeperTarget.Lookup("piano::lead") == "piano");

    p.SetName("dg772l_after");
    keeperSource.GetInlet(0)->SetList("set bass synth", YSE::T_GUI);
    h->SetBang(0);
    // The old target was neither replaced nor extended: the grouping now
    // reads and writes dictionaries under the new prefix.
    CHECK(keeperTarget.Count() == 1);
    CHECK(keeperTarget.Lookup("piano::lead") == "piano");
    CHECK(keeperTarget.Lookup("synth::bass").empty());
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("dict.group: a grouping asked for over in-patcher delivery lands on T_DSP (#772)") {
    // A .r feeding the grouping dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread asks for the grouping" is the
    // ordinary case, and the whole path is a snapshot, a bounded scan and a
    // send of a string the object already owns.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dg772m");

    gDict keeperTarget;
    keeperTarget.SetParent(&p);
    keeperTarget.SetParams("t772m");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go772m");
    YSE::pHandle* dictSource = p.CreateObject(YSE::OBJ::G_DICT, "s772m");
    YSE::pHandle* grp = p.CreateObject(YSE::OBJ::G_DICT_GROUP, "s772m t772m");
    REQUIRE(recv != nullptr);
    REQUIRE(dictSource != nullptr);
    REQUIRE(grp != nullptr);
    p.Connect(recv, 0, grp, 0);
    p.Connect(grp, 0, &sinkHandle, 0);

    dictSource->SetListData(0, "set lead piano");

    p.PassData(std::string("dictionary s772m"), "go772m", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "dictionary t772m");
    CHECK(keeperTarget.Lookup("piano::lead") == "piano");
  }

  TEST_CASE("dict.group: no message path allocates (#772)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path: the trigger (bang and reference alike, with and
    // without a group key resolve), the acknowledgment and the refusal.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer, and that buffer is not
    // the same width everywhere (15 on libstdc++, 22 on libc++).
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string refSource = "dictionary probeS772";
    const std::string refTarget = "dictionary probeT772";
    const std::string unknown = "frobnicate something quite long indeed";

    Rig rig("dg772n", "probeS772", "probeT772", "instrument");
    rig.Source("set 1::instrument a value past every small-string buffer");
    rig.Source("set 1::freq 440");
    rig.Source("set 2::chord 0 4 7 12");

    // Warm every path — including the sink's list assignment — so
    // first-call machinery is not what the probe catches.
    rig.Run();
    rig.group.GetInlet(0)->SetList(refSource, YSE::T_GUI);
    rig.group.GetInlet(1)->SetList(refTarget, YSE::T_GUI);
    rig.group.GetInlet(0)->SetList(unknown, YSE::T_GUI);
    const std::uint64_t before = rig.group.Dropped();

    rig.out.reset();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      rig.group.GetInlet(0)->SetBang(YSE::T_DSP);
      rig.group.GetInlet(0)->SetList(refSource, YSE::T_DSP);
      rig.group.GetInlet(1)->SetList(refTarget, YSE::T_DSP);
      rig.group.GetInlet(0)->SetList(unknown, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. Two triggers ran (a bang and a reference),
    // each skipping record 2's group-less entry, plus the unknown message.
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "dictionary probeT772");
    CHECK(rig.target.Count() == 2);
    CHECK(rig.group.Dropped() == before + 3);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("dict.group: params survive a DumpJSON / ParseJSON round trip (#772)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_DICT_GROUP, "voices772o byinst772o instrument");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".dict.group"));
    CHECK(copy->GetParams() == std::string("voices772o byinst772o instrument"));
    CHECK(copy->GetInputs() == 2);
    CHECK(copy->GetOutputs() == 1);
  }

  TEST_CASE(
      "dict.group: the key argument is optional, and stays optional through a round trip (#772)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_DICT_GROUP, "flat772p index772p");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);
    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(copy->GetParams() == std::string("flat772p index772p"));
  }

  TEST_CASE("dict.group: carries complete documentation metadata (#772)") {
    gDictGroup g;
    CHECK_FALSE(g.GetDescription().empty());
    CHECK(g.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = g.GetParamDocs();
    REQUIRE(docs.size() == 3);
    CHECK(docs[0].name == "source");
    CHECK(docs[1].name == "target");
    CHECK(docs[2].name == "key");
  }
}
