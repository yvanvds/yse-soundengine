// Tests for .dict.strip (issue #780) — Max's dict.strip on the
// name-addressed value model .dict settled (#550).
//
// What has to be proven, and what every case below is one of:
//
//   - **the dictionary is bound from the creation argument.** A dictionary
//     never travels down a cord, so ".dict.strip <name> [<path>]" resolves
//     the name once, on the control thread, and a `dictionary <name>`
//     message is honoured only when it names the dictionary already bound —
//     DictReferenceNames' bounded compare, never a registry lookup on a
//     message path.
//   - **the strip removes exactly the sub-tree, in place.** An entry under
//     the path — its key begins "<path>::" — is erased; everything else
//     survives untouched, including a leaf stored at exactly the path
//     (a value, not a sub-tree) and a key that only begins like the path
//     ("voices" against "voice" — the prefix ends at a "::" boundary or it
//     is not a prefix). The erase walks back to front, so DictEraseAt's
//     gap-closing never moves a row past the cursor — pinned by the
//     interleaved case below.
//   - **the strip crosses the control/audio boundary and nothing
//     allocates.** In-patcher delivery dispatches on T_DSP, so "the audio
//     thread asks for a strip" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names a dictionary uses names of its own — one case's
// contents must not be visible to the next.

#include <doctest/doctest.h>
#include <string>

#include "patcher/genericObjects/gDict.h"
#include "patcher/genericObjects/gDictStrip.h"
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
using YSE::PATCHER::gDictStrip;

namespace {

  // A .dict and a .dict.strip over it, sharing one patcherImplementation so
  // the name actually binds ("<patcherName>.<name>" needs a patcher to
  // prefix with — a parentless object stays private). The sink is declared
  // before the objects so it is torn down first, while the inlet it is wired
  // to still exists (see sinks.hpp on why that matters).
  struct Rig {
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    MultiSink out;
    gDict dict;
    gDictStrip stripper;

    Rig(const std::string& patcherName, const std::string& dictName,
        const std::string& path = std::string()) {
      p.SetName(patcherName);
      dict.SetParent(&p);
      dict.SetParams(dictName);
      stripper.SetParent(&p);
      std::string params = dictName;
      if (!path.empty()) params += " " + path;
      stripper.SetParams(params);
      Wire(stripper, 0, out);
    }

    void Store(const std::string& message) {
      dict.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    // Bang the strip.
    void Run(YSE::THREAD thread = YSE::T_GUI) {
      out.reset();
      stripper.GetInlet(0)->SetBang(thread);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("dict.strip: registered, one inlet, one outlet (#780)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_STRIP);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".dict.strip");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
  }

  TEST_CASE("dict.strip: appears in the registry's name list (#780)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_DICT_STRIP)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("dict.strip: the inlet takes bang and list, nothing else (#780)") {
    // No int or float handler: a bare number names no dictionary — the
    // family's rule.
    gDictStrip g;
    const unsigned int accepted = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((accepted & YSE::PATCHER::IT_BANG) != 0);
    CHECK((accepted & YSE::PATCHER::IT_LIST) != 0);
    CHECK((accepted & YSE::PATCHER::IT_INT) == 0);
    CHECK((accepted & YSE::PATCHER::IT_FLOAT) == 0);
  }

  // ─── the strip ──────────────────────────────────────────────────────────────

  TEST_CASE("dict.strip: removes the sub-tree in place, everything else survives (#780)") {
    // The use case the object exists for: one voice cleared out of a voice
    // table, the rest of the table untouched, in the dictionary itself —
    // what .dict's delete (one path, not a branch) cannot spell.
    Rig rig("dt780a", "dict780a", "voice::1");
    rig.Store("set voice::1::freq 440");
    rig.Store("set voice::1::adsr 5 10 80 200");
    rig.Store("set voice::2::freq 550");
    rig.Store("set master::gain 0.8");

    rig.Run();
    CHECK(rig.dict.Count() == 2);
    CHECK(rig.dict.Lookup("voice::1::freq").empty());
    CHECK(rig.dict.Lookup("voice::1::adsr").empty());
    CHECK(rig.dict.Lookup("voice::2::freq") == "550");
    CHECK(rig.dict.Lookup("master::gain") == "0.8");

    // And the reference leaves, so the rest of the dict.* family can pick
    // the edited dictionary up.
    REQUIRE(rig.out.gotList);
    CHECK(rig.out.listValue == "dictionary dict780a");
    CHECK(rig.stripper.Dropped() == 0);
  }

  TEST_CASE("dict.strip: a leaf at the path, and a near-prefix key, survive (#780)") {
    // An entry stored at exactly the path is a value, not a sub-tree, so it
    // is not part of the branch — removing that one entry is .dict's delete.
    // And a key that merely begins like the path is no match at all: the
    // prefix ends at a "::" boundary or it is not a prefix, so stripping
    // "voice" must not also remove "voices::1" — the case the issue pins.
    Rig rig("dt780b", "dict780b", "voice");
    rig.Store("set voice lead");
    rig.Store("set voice::freq 440");
    rig.Store("set voices::1 spare");
    rig.Store("set voicecard rev2");

    rig.Run();
    CHECK(rig.dict.Count() == 3);
    CHECK(rig.dict.Lookup("voice") == "lead");
    CHECK(rig.dict.Lookup("voice::freq").empty());
    CHECK(rig.dict.Lookup("voices::1") == "spare");
    CHECK(rig.dict.Lookup("voicecard") == "rev2");
    CHECK(rig.stripper.Dropped() == 0);
  }

  TEST_CASE("dict.strip: interleaved matches all go — the back-to-front erase (#780)") {
    // The mechanics the issue calls out: DictEraseAt closes each gap by
    // moving the rows above the erased position down, so a forward cursor
    // would step over the row that just slid into its place. Matches and
    // survivors alternating in storage order is exactly the layout that
    // trips a forward scan — every match must go and every survivor must
    // stay, in order.
    Rig rig("dt780c", "dict780c", "x");
    rig.Store("set x::a 1");
    rig.Store("set keep::a 2");
    rig.Store("set x::b 3");
    rig.Store("set keep::b 4");
    rig.Store("set x::c 5");

    rig.Run();
    CHECK(rig.dict.Count() == 2);
    CHECK(rig.dict.Lookup("keep::a") == "2");
    CHECK(rig.dict.Lookup("keep::b") == "4");
    CHECK(rig.dict.KeyAt(0) == "keep::a");
    CHECK(rig.dict.KeyAt(1) == "keep::b");
    CHECK(rig.stripper.Dropped() == 0);
  }

  TEST_CASE("dict.strip: re-running is idempotent (#780)") {
    // A second strip of the same path finds nothing under it and changes
    // nothing — and still announces the dictionary, because the operation
    // ran.
    Rig rig("dt780d", "dict780d", "voice::1");
    rig.Store("set voice::1::freq 440");
    rig.Store("set master::gain 0.8");
    rig.Run();
    CHECK(rig.dict.Count() == 1);

    rig.Run();
    CHECK(rig.dict.Count() == 1);
    CHECK(rig.dict.Lookup("master::gain") == "0.8");
    REQUIRE(rig.out.gotList);
    CHECK(rig.out.listValue == "dictionary dict780d");
    CHECK(rig.stripper.Dropped() == 0);
  }

  TEST_CASE("dict.strip: an empty path matches nothing (#780)") {
    // No path argument means no sub-tree to remove — there is no sub-tree
    // above the root — so the dictionary comes through untouched. Clearing
    // a whole dictionary is .dict's clear, not a strip.
    Rig rig("dt780e", "dict780e");
    rig.Store("set voice::1::freq 440");
    rig.Store("set master::gain 0.8");

    rig.Run();
    CHECK(rig.dict.Count() == 2);
    CHECK(rig.dict.Lookup("voice::1::freq") == "440");
    CHECK(rig.dict.Lookup("master::gain") == "0.8");
    CHECK(rig.stripper.Dropped() == 0);
  }

  TEST_CASE("dict.strip: unnamed is private, and says nothing (#780)") {
    // No arguments means a private, empty dictionary — .dict's rule that an
    // unnamed object does not pool on "<patcherName>.". And with no name
    // there is nothing to pass on, so the outlet stays silent; the strip
    // itself still ran, without a refusal.
    MultiSink sink;
    gDictStrip g;
    Wire(g, 0, sink);
    CHECK(g.DictName().empty());
    CHECK(g.Address().empty());
    g.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(sink.gotList);
    CHECK(g.Dropped() == 0);
  }

  // ─── the reference messages ─────────────────────────────────────────────────

  TEST_CASE("dict.strip: the bound reference triggers, anything else is refused (#780)") {
    Rig rig("dt780f", "dict780f", "voice::1");
    rig.Store("set voice::1::freq 440");
    rig.Store("set master::gain 0.8");

    const std::uint64_t before = rig.stripper.Dropped();
    rig.out.reset();
    rig.stripper.GetInlet(0)->SetList("dictionary dict780f", YSE::T_GUI);
    REQUIRE(rig.out.gotList);
    CHECK(rig.out.listValue == "dictionary dict780f");
    CHECK(rig.dict.Count() == 1);
    CHECK(rig.dict.Lookup("master::gain") == "0.8");
    CHECK(rig.stripper.Dropped() == before);

    // A reference to a dictionary this object is not bound to, and any
    // other message, are refused and counted, never resolved: a registry
    // lookup is a mutex, and this may be the audio thread.
    rig.out.reset();
    rig.stripper.GetInlet(0)->SetList("dictionary somewhere_else", YSE::T_GUI);
    rig.stripper.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK(rig.stripper.Dropped() == before + 2);
  }

  TEST_CASE("dict.strip: wired from the dict's reference outlet, banging the dict strips (#780)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: the .dict's reference outlet into the stripper, a bang on the
    // .dict, and the sub-tree gone at the far end. Max's own gesture.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher p;
    p.create(2);
    p.name("dt780g");
    YSE::pHandle* dict = p.CreateObject(YSE::OBJ::G_DICT, "voices780g");
    YSE::pHandle* strip = p.CreateObject(YSE::OBJ::G_DICT_STRIP, "voices780g voice::1");
    REQUIRE(dict != nullptr);
    REQUIRE(strip != nullptr);
    p.Connect(dict, 1, strip, 0);
    p.Connect(strip, 0, &sinkHandle, 0);

    dict->SetListData(0, "set voice::1::freq 440");
    dict->SetListData(0, "set voice::2::freq 550");
    dict->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "dictionary voices780g");

    // The surviving entry is readable back through the .dict. The stored
    // value "550" is a single numeric token, so the .dict's get classifies
    // it at the outlet and it arrives as an int — SendAtom's rule.
    sink.reset();
    p.Connect(dict, 0, &sinkHandle, 0);
    dict->SetListData(0, "get voice::2::freq");
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 550);

    // And the stripped one is gone: a get with no stored value answers
    // nothing.
    sink.reset();
    dict->SetListData(0, "get voice::1::freq");
    CHECK_FALSE(sink.gotInt);
    CHECK_FALSE(sink.gotList);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("dict.strip: the address form is the patcher's, and RefreshBinding follows it (#780)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dt780h_before");

    gDictStrip g;
    g.SetParams("d780h voice::1");
    g.SetParent(&p);
    CHECK(g.Address() == "dt780h_before.d780h");
    CHECK(g.StripPath() == "voice::1");

    // Idempotent: a rebind to the address it already has keeps the store.
    g.RefreshBinding();
    CHECK(g.Address() == "dt780h_before.d780h");

    p.SetName("dt780h_after");
    g.RefreshBinding();
    CHECK(g.Address() == "dt780h_after.d780h");
  }

  TEST_CASE("dict.strip: patcherImplementation::SetName re-anchors it (#780)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by the
    // patcher, without anybody calling RefreshBinding by hand. The keeper
    // holds the old-address store (it is not in the patcher's object map, so
    // the rename does not touch it): before the rename a bang strips the
    // keeper's dictionary; after it the stripper edits a fresh dictionary
    // under the new prefix, so a second bang leaves the keeper's contents
    // exactly as they were.
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dt780i_before");

    gDict keeper;
    keeper.SetParent(&p);
    keeper.SetParams("d780i");
    keeper.GetInlet(0)->SetList("set voice::1::freq 440", YSE::T_GUI);
    keeper.GetInlet(0)->SetList("set master::gain 0.8", YSE::T_GUI);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_STRIP, "d780i voice::1");
    REQUIRE(h != nullptr);

    h->SetBang(0);
    CHECK(keeper.Count() == 1);
    CHECK(keeper.Lookup("master::gain") == "0.8");

    p.SetName("dt780i_after");
    keeper.GetInlet(0)->SetList("set voice::1::amp 0.5", YSE::T_GUI);
    h->SetBang(0);
    // The keeper's dictionary was not touched: the strip now edits a
    // dictionary under the new prefix.
    CHECK(keeper.Count() == 2);
    CHECK(keeper.Lookup("voice::1::amp") == "0.5");
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("dict.strip: a strip asked for over in-patcher delivery lands on T_DSP (#780)") {
    // A .r feeding the stripper dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread asks for the strip" is the ordinary
    // case, and the whole path is one guarded bounded scan and a send of a
    // string the object already owns.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dt780j");

    gDict keeper;
    keeper.SetParent(&p);
    keeper.SetParams("d780j");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go780j");
    YSE::pHandle* strip = p.CreateObject(YSE::OBJ::G_DICT_STRIP, "d780j voice::1");
    REQUIRE(recv != nullptr);
    REQUIRE(strip != nullptr);
    p.Connect(recv, 0, strip, 0);
    p.Connect(strip, 0, &sinkHandle, 0);

    keeper.GetInlet(0)->SetList("set voice::1::freq 440", YSE::T_GUI);
    keeper.GetInlet(0)->SetList("set master::gain 0.8", YSE::T_GUI);

    p.PassData(std::string("dictionary d780j"), "go780j", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "dictionary d780j");
    CHECK(keeper.Count() == 1);
    CHECK(keeper.Lookup("master::gain") == "0.8");
    CHECK(keeper.Lookup("voice::1::freq").empty());
  }

  TEST_CASE("dict.strip: no message path allocates (#780)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path: the trigger (bang and reference alike) and the
    // refusal.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer, and that buffer is not
    // the same width everywhere (15 on libstdc++, 22 on libc++).
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string ref = "dictionary probeDict780";
    const std::string unknown = "frobnicate something quite long indeed";

    Rig rig("dt780k", "probeDict780", "voice::1");
    rig.Store("set voice::1::freq a value past every small-string buffer");
    rig.Store("set voice::1::adsr 5 10 80 200");
    rig.Store("set master::chain 0 4 7 12");

    // Warm every path — including the sink's list assignment — so first-call
    // machinery is not what the probe catches. Then restock the sub-tree so
    // the probed strip really erases.
    rig.Run();
    rig.stripper.GetInlet(0)->SetList(ref, YSE::T_GUI);
    rig.stripper.GetInlet(0)->SetList(unknown, YSE::T_GUI);
    rig.Store("set voice::1::freq 440");
    rig.Store("set voice::1::amp 0.5");
    const std::uint64_t before = rig.stripper.Dropped();

    rig.out.reset();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      rig.stripper.GetInlet(0)->SetBang(YSE::T_DSP);
      rig.stripper.GetInlet(0)->SetList(ref, YSE::T_DSP);
      rig.stripper.GetInlet(0)->SetList(unknown, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. Two triggers ran (a bang and a reference),
    // the first of which erased the restocked sub-tree, plus the unknown
    // message.
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "dictionary probeDict780");
    CHECK(rig.dict.Count() == 1);
    CHECK(rig.dict.Lookup("master::chain") == "0 4 7 12");
    CHECK(rig.stripper.Dropped() == before + 1);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("dict.strip: params survive a DumpJSON / ParseJSON round trip (#780)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_DICT_STRIP, "voices780l voice::1");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".dict.strip"));
    CHECK(copy->GetParams() == std::string("voices780l voice::1"));
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 1);
  }

  TEST_CASE(
      "dict.strip: the path argument is optional, and stays optional through a round trip (#780)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_DICT_STRIP, "voices780m");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);
    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(copy->GetParams() == std::string("voices780m"));
  }

  TEST_CASE("dict.strip: carries complete documentation metadata (#780)") {
    gDictStrip g;
    CHECK_FALSE(g.GetDescription().empty());
    CHECK(g.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = g.GetParamDocs();
    REQUIRE(docs.size() == 2);
    CHECK(docs[0].name == "name");
    CHECK(docs[1].name == "path");
  }
}
