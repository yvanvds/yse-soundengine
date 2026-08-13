// Tests for .dict.slice (issue #779) — Max's dict.slice on the
// name-addressed value model .dict settled (#550).
//
// What has to be proven, and what every case below is one of:
//
//   - **all three dictionaries are bound from the creation arguments.** A
//     dictionary never travels down a cord, so ".dict.slice <source>
//     <slice> <remainder> [<path>]" resolves the names once, on the control
//     thread, and a `dictionary <name>` message is honoured only when it
//     names a dictionary already bound — DictReferenceNames' bounded
//     compare, never a registry lookup on a message path.
//   - **the split is a clean partition.** An entry under the path replaces
//     the slice target with the prefix stripped — the sub-tree becomes a
//     dictionary rooted at itself — and every other entry (a leaf at
//     exactly the path, a key that only begins like it, everything
//     unrelated) replaces the remainder target unchanged. Both targets are
//     replaced whole, and the references leave right to left.
//   - **the split never holds two guards.** The source is copied out under
//     its guard into a snapshot reserved at construction, then each half is
//     written under that target's guard alone — which is also what makes
//     the degenerate bindings (one name on two sides) safe.
//   - **the split crosses the control/audio boundary and nothing
//     allocates.** In-patcher delivery dispatches on T_DSP, so "the audio
//     thread asks for a slice" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names a dictionary uses names of its own — one case's
// contents must not be visible to the next.

#include <doctest/doctest.h>
#include <string>
#include <vector>

#include "patcher/genericObjects/gDict.h"
#include "patcher/genericObjects/gDictSlice.h"
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
using TestHelpers::OrderSink;
using TestHelpers::Wire;
using YSE::PATCHER::gDict;
using YSE::PATCHER::gDictSlice;

namespace {

  // Three .dict objects and a .dict.slice over them, sharing one
  // patcherImplementation so the names actually bind ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sinks are declared before the objects so they are torn down first, while
  // the inlets they are wired to still exist (see sinks.hpp on why that
  // matters).
  struct Rig {
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    MultiSink sliceOut;
    MultiSink remainderOut;
    gDict source;
    gDict slice;
    gDict remainder;
    gDictSlice slicer;

    Rig(const std::string& patcherName, const std::string& sourceName, const std::string& sliceName,
        const std::string& remainderName, const std::string& path = std::string()) {
      p.SetName(patcherName);
      source.SetParent(&p);
      source.SetParams(sourceName);
      slice.SetParent(&p);
      slice.SetParams(sliceName);
      remainder.SetParent(&p);
      remainder.SetParams(remainderName);
      slicer.SetParent(&p);
      std::string params = sourceName + " " + sliceName + " " + remainderName;
      if (!path.empty()) params += " " + path;
      slicer.SetParams(params);
      Wire(slicer, 0, sliceOut);
      Wire(slicer, 1, remainderOut);
    }

    void Source(const std::string& message) {
      source.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    // Bang the split.
    void Run(YSE::THREAD thread = YSE::T_GUI) {
      sliceOut.reset();
      remainderOut.reset();
      slicer.GetInlet(0)->SetBang(thread);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("dict.slice: registered, three inlets, two outlets (#779)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_SLICE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".dict.slice");
    CHECK(h->GetInputs() == 3);
    CHECK(h->GetOutputs() == 2);
  }

  TEST_CASE("dict.slice: appears in the registry's name list (#779)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_DICT_SLICE)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("dict.slice: inlet 0 takes bang and list, inlets 1 and 2 list only (#779)") {
    // No int or float handler anywhere: a bare number names no dictionary.
    // And no bang on the acknowledging inlets — each only acknowledges a
    // reference, so a bang there would have to invent a meaning.
    gDictSlice g;
    const unsigned int hot = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((hot & YSE::PATCHER::IT_BANG) != 0);
    CHECK((hot & YSE::PATCHER::IT_LIST) != 0);
    for (int inlet = 1; inlet <= 2; inlet++) {
      const unsigned int cold = g.GetInlet(inlet)->GetAcceptedTypes();
      CHECK((cold & YSE::PATCHER::IT_BANG) == 0);
      CHECK((cold & YSE::PATCHER::IT_LIST) != 0);
    }
  }

  // ─── the split ──────────────────────────────────────────────────────────────

  TEST_CASE("dict.slice: slices the sub-tree out, prefix stripped, remainder unchanged (#779)") {
    // The use case the object exists for: one voice read out of a voice
    // table as a dictionary of its own, rooted at itself, and the rest of
    // the table left intact in the remainder.
    Rig rig("ds779a", "src779a", "slc779a", "rem779a", "voice::1");
    rig.Source("set voice::1::freq 440");
    rig.Source("set voice::1::adsr 5 10 80 200");
    rig.Source("set voice::2::freq 550");
    rig.Source("set master::gain 0.8");

    rig.Run();
    CHECK(rig.slice.Count() == 2);
    CHECK(rig.slice.Lookup("freq") == "440");
    CHECK(rig.slice.Lookup("adsr") == "5 10 80 200");
    CHECK(rig.remainder.Count() == 2);
    CHECK(rig.remainder.Lookup("voice::2::freq") == "550");
    CHECK(rig.remainder.Lookup("master::gain") == "0.8");

    // And both references leave, so the rest of the dict.* family can pick
    // either half up.
    REQUIRE(rig.sliceOut.gotList);
    CHECK(rig.sliceOut.listValue == "dictionary slc779a");
    REQUIRE(rig.remainderOut.gotList);
    CHECK(rig.remainderOut.listValue == "dictionary rem779a");
    CHECK(rig.slicer.Dropped() == 0);
  }

  TEST_CASE("dict.slice: a leaf at the path, and a near-prefix key, stay in the remainder (#779)") {
    // An entry stored at exactly the path is a value, not a sub-tree, so it
    // is not part of the slice; and a key that merely begins like the path
    // is no match at all — the prefix ends at a "::" boundary or it is not
    // a prefix. Every entry lands in exactly one half.
    Rig rig("ds779b", "src779b", "slc779b", "rem779b", "voice");
    rig.Source("set voice lead");
    rig.Source("set voice::freq 440");
    rig.Source("set voicecard rev2");

    rig.Run();
    CHECK(rig.slice.Count() == 1);
    CHECK(rig.slice.Lookup("freq") == "440");
    CHECK(rig.remainder.Count() == 2);
    CHECK(rig.remainder.Lookup("voice") == "lead");
    CHECK(rig.remainder.Lookup("voicecard") == "rev2");
    CHECK(rig.slicer.Dropped() == 0);
  }

  TEST_CASE("dict.slice: a split replaces both targets whole (#779)") {
    // Re-running is idempotent, and a stale entry from the last run cannot
    // survive into this one — each target holds its half of the result, not
    // an accumulation of every result so far.
    Rig rig("ds779c", "src779c", "slc779c", "rem779c", "voice::1");
    rig.Source("set voice::1::freq 440");
    rig.Source("set master::gain 0.8");
    rig.Run();
    CHECK(rig.slice.Lookup("freq") == "440");
    CHECK(rig.remainder.Lookup("master::gain") == "0.8");

    rig.source.GetInlet(0)->SetList("clear", YSE::T_GUI);
    rig.Source("set voice::1::amp 0.5");
    rig.Source("set other::x 1");
    rig.Run();
    CHECK(rig.slice.Count() == 1);
    CHECK(rig.slice.Lookup("amp") == "0.5");
    CHECK(rig.slice.Lookup("freq").empty());
    CHECK(rig.remainder.Count() == 1);
    CHECK(rig.remainder.Lookup("other::x") == "1");
    CHECK(rig.remainder.Lookup("master::gain").empty());
  }

  TEST_CASE("dict.slice: an empty path matches nothing (#779)") {
    // No path argument means no sub-tree to take — there is no sub-tree
    // above the root — so the slice comes out empty and the remainder is
    // the whole source. The degenerate case falls out of the partition rule
    // rather than being a special one.
    Rig rig("ds779d", "src779d", "slc779d", "rem779d");
    rig.Source("set voice::1::freq 440");
    rig.Source("set master::gain 0.8");

    rig.Run();
    CHECK(rig.slice.Count() == 0);
    CHECK(rig.remainder.Count() == 2);
    CHECK(rig.remainder.Lookup("voice::1::freq") == "440");
    CHECK(rig.remainder.Lookup("master::gain") == "0.8");
    CHECK(rig.slicer.Dropped() == 0);
  }

  TEST_CASE("dict.slice: the references leave right to left (#779)") {
    // Max's universal outlet order: the remainder (outlet 1) is announced
    // before the slice (outlet 0), so a patch that consumes both halves
    // sees them in the order every other multi-outlet object delivers.
    std::vector<char> log;
    OrderSink sliceSink;
    sliceSink.log = &log;
    sliceSink.tag = 'S';
    OrderSink remainderSink;
    remainderSink.log = &log;
    remainderSink.tag = 'R';

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ds779e");
    gDict source;
    source.SetParent(&p);
    source.SetParams("src779e");
    source.GetInlet(0)->SetList("set voice::1::freq 440", YSE::T_GUI);

    gDictSlice slicer;
    slicer.SetParent(&p);
    slicer.SetParams("src779e slc779e rem779e voice::1");
    Wire(slicer, 0, sliceSink);
    Wire(slicer, 1, remainderSink);

    slicer.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == 'R');
    CHECK(log[1] == 'S');
    CHECK(sliceSink.lastList == "dictionary slc779e");
    CHECK(remainderSink.lastList == "dictionary rem779e");
  }

  TEST_CASE("dict.slice: the source as its own remainder splits in place (#779)") {
    // The degenerate binding the snapshot design makes safe: the source is
    // copied out before any target is replaced, and no guard is ever taken
    // twice — so ".dict.slice kit slc kit voice::1" pulls the sub-tree out
    // of the dictionary it leaves the remainder in.
    Rig rig("ds779f", "kit779f", "slc779f", "kit779f", "voice::1");
    rig.Source("set voice::1::freq 440");
    rig.Source("set master::gain 0.8");

    rig.Run();
    CHECK(rig.slice.Count() == 1);
    CHECK(rig.slice.Lookup("freq") == "440");
    CHECK(rig.source.Count() == 1);
    CHECK(rig.source.Lookup("master::gain") == "0.8");
    CHECK(rig.source.Lookup("voice::1::freq").empty());
  }

  TEST_CASE("dict.slice: both targets on one name — the remainder wins (#779)") {
    // One name on both target sides writes the store twice, and the
    // remainder is written second, so it is what survives. Documented
    // behaviour rather than a refusal: the binding is legal, just rarely
    // what a patch wants.
    Rig rig("ds779g", "src779g", "both779g", "both779g", "voice::1");
    rig.Source("set voice::1::freq 440");
    rig.Source("set master::gain 0.8");

    rig.Run();
    CHECK(rig.slice.Count() == 1);
    CHECK(rig.slice.Lookup("master::gain") == "0.8");
    CHECK(rig.slice.Lookup("freq").empty());
  }

  TEST_CASE("dict.slice: unnamed sides are private, and unnamed targets say nothing (#779)") {
    // No arguments means three private, empty dictionaries — .dict's rule
    // that an unnamed object does not pool on "<patcherName>.". And an
    // unnamed target has no name to pass on, so both outlets stay silent.
    MultiSink sliceSink;
    MultiSink remainderSink;
    gDictSlice g;
    Wire(g, 0, sliceSink);
    Wire(g, 1, remainderSink);
    CHECK(g.SourceName().empty());
    CHECK(g.SourceAddress().empty());
    CHECK(g.SliceAddress().empty());
    CHECK(g.RemainderAddress().empty());
    g.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(sliceSink.gotList);
    CHECK_FALSE(remainderSink.gotList);
    CHECK(g.Dropped() == 0);
  }

  // ─── the reference messages ─────────────────────────────────────────────────

  TEST_CASE("dict.slice: the source reference triggers, anything else is refused (#779)") {
    Rig rig("ds779h", "src779h", "slc779h", "rem779h", "voice::1");
    rig.Source("set voice::1::freq 440");

    const std::uint64_t before = rig.slicer.Dropped();
    rig.sliceOut.reset();
    rig.slicer.GetInlet(0)->SetList("dictionary src779h", YSE::T_GUI);
    REQUIRE(rig.sliceOut.gotList);
    CHECK(rig.sliceOut.listValue == "dictionary slc779h");
    CHECK(rig.slice.Lookup("freq") == "440");
    CHECK(rig.slicer.Dropped() == before);

    // A reference to a dictionary this object is not bound to — including
    // the targets, which belong on inlets 1 and 2 — and any other message
    // are refused and counted, never resolved: a registry lookup is a
    // mutex, and this may be the audio thread.
    rig.sliceOut.reset();
    rig.slicer.GetInlet(0)->SetList("dictionary somewhere_else", YSE::T_GUI);
    rig.slicer.GetInlet(0)->SetList("dictionary slc779h", YSE::T_GUI);
    rig.slicer.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK_FALSE(rig.sliceOut.gotList);
    CHECK(rig.slicer.Dropped() == before + 3);
  }

  TEST_CASE("dict.slice: inlets 1 and 2 acknowledge their own references only (#779)") {
    // The bindings are creation arguments, so each reference is accepted
    // silently — a patch may wire a target's reference outlet across — and
    // sets nothing. A reference naming anything else, including the other
    // inlet's target, is refused and counted.
    Rig rig("ds779i", "src779i", "slc779i", "rem779i", "voice::1");
    const std::uint64_t before = rig.slicer.Dropped();

    rig.slicer.GetInlet(1)->SetList("dictionary slc779i", YSE::T_GUI);
    rig.slicer.GetInlet(2)->SetList("dictionary rem779i", YSE::T_GUI);
    CHECK_FALSE(rig.sliceOut.gotList);
    CHECK_FALSE(rig.remainderOut.gotList);
    CHECK(rig.slicer.Dropped() == before);

    rig.slicer.GetInlet(1)->SetList("dictionary rem779i", YSE::T_GUI);
    rig.slicer.GetInlet(2)->SetList("dictionary slc779i", YSE::T_GUI);
    rig.slicer.GetInlet(1)->SetList("dictionary elsewhere", YSE::T_GUI);
    CHECK_FALSE(rig.sliceOut.gotList);
    CHECK_FALSE(rig.remainderOut.gotList);
    CHECK(rig.slicer.Dropped() == before + 3);
  }

  TEST_CASE(
      "dict.slice: wired from the source's reference outlet, banging the dict slices (#779)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: the source .dict's reference outlet into the slicer, a bang on
    // the .dict, and both halves at the far end. Max's own gesture.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher p;
    p.create(2);
    p.name("ds779j");
    YSE::pHandle* dictSource = p.CreateObject(YSE::OBJ::G_DICT, "voices779j");
    YSE::pHandle* dictSlice = p.CreateObject(YSE::OBJ::G_DICT, "one779j");
    YSE::pHandle* dictRemainder = p.CreateObject(YSE::OBJ::G_DICT, "rest779j");
    YSE::pHandle* slc =
        p.CreateObject(YSE::OBJ::G_DICT_SLICE, "voices779j one779j rest779j voice::1");
    REQUIRE(dictSource != nullptr);
    REQUIRE(dictSlice != nullptr);
    REQUIRE(dictRemainder != nullptr);
    REQUIRE(slc != nullptr);
    p.Connect(dictSource, 1, slc, 0);
    p.Connect(slc, 0, &sinkHandle, 0);

    dictSource->SetListData(0, "set voice::1::freq 440");
    dictSource->SetListData(0, "set voice::2::freq 550");
    dictSource->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "dictionary one779j");

    // Both halves are readable through their .dict objects. The stored value
    // "440" is a single numeric token, so the .dict's get classifies it at
    // the outlet and it arrives as an int — SendAtom's rule, not this
    // object's.
    sink.reset();
    p.Connect(dictSlice, 0, &sinkHandle, 0);
    dictSlice->SetListData(0, "get freq");
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 440);

    sink.reset();
    p.Connect(dictRemainder, 0, &sinkHandle, 0);
    dictRemainder->SetListData(0, "get voice::2::freq");
    REQUIRE(sink.gotInt);
    CHECK(sink.intValue == 550);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("dict.slice: the address form is the patcher's, and RefreshBinding follows it (#779)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ds779k_before");

    gDictSlice g;
    g.SetParams("s779k l779k r779k voice::1");
    g.SetParent(&p);
    CHECK(g.SourceAddress() == "ds779k_before.s779k");
    CHECK(g.SliceAddress() == "ds779k_before.l779k");
    CHECK(g.RemainderAddress() == "ds779k_before.r779k");
    CHECK(g.SlicePath() == "voice::1");

    // Idempotent: a rebind to the addresses it already has keeps the stores.
    g.RefreshBinding();
    CHECK(g.SourceAddress() == "ds779k_before.s779k");

    p.SetName("ds779k_after");
    g.RefreshBinding();
    CHECK(g.SourceAddress() == "ds779k_after.s779k");
    CHECK(g.SliceAddress() == "ds779k_after.l779k");
    CHECK(g.RemainderAddress() == "ds779k_after.r779k");
  }

  TEST_CASE("dict.slice: patcherImplementation::SetName re-anchors it (#779)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by the
    // patcher, without anybody calling RefreshBinding by hand. The keepers
    // hold the old-address stores (they are not in the patcher's object map,
    // so the rename does not touch them): before the rename a bang splits
    // the keeper source into the keeper targets; after it every side is a
    // fresh empty dictionary under the new prefix, so a second bang leaves
    // the old targets exactly as they were.
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ds779l_before");

    gDict keeperSource;
    keeperSource.SetParent(&p);
    keeperSource.SetParams("src779l");
    keeperSource.GetInlet(0)->SetList("set voice::1::freq 440", YSE::T_GUI);
    gDict keeperSlice;
    keeperSlice.SetParent(&p);
    keeperSlice.SetParams("slc779l");
    gDict keeperRemainder;
    keeperRemainder.SetParent(&p);
    keeperRemainder.SetParams("rem779l");

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_SLICE, "src779l slc779l rem779l voice::1");
    REQUIRE(h != nullptr);

    h->SetBang(0);
    CHECK(keeperSlice.Lookup("freq") == "440");

    p.SetName("ds779l_after");
    keeperSource.GetInlet(0)->SetList("set voice::1::amp 0.5", YSE::T_GUI);
    h->SetBang(0);
    // The old targets were neither replaced nor extended: the split now
    // reads and writes dictionaries under the new prefix.
    CHECK(keeperSlice.Count() == 1);
    CHECK(keeperSlice.Lookup("freq") == "440");
    CHECK(keeperSlice.Lookup("amp").empty());
    CHECK(keeperRemainder.Count() == 0);
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("dict.slice: a split asked for over in-patcher delivery lands on T_DSP (#779)") {
    // A .r feeding the slicer dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread asks for the split" is the ordinary
    // case, and the whole path is a snapshot, two bounded scans and sends of
    // strings the object already owns.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ds779m");

    gDict keeperSlice;
    keeperSlice.SetParent(&p);
    keeperSlice.SetParams("l779m");
    gDict keeperRemainder;
    keeperRemainder.SetParent(&p);
    keeperRemainder.SetParams("r779m");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go779m");
    YSE::pHandle* dictSource = p.CreateObject(YSE::OBJ::G_DICT, "s779m");
    YSE::pHandle* slc = p.CreateObject(YSE::OBJ::G_DICT_SLICE, "s779m l779m r779m voice::1");
    REQUIRE(recv != nullptr);
    REQUIRE(dictSource != nullptr);
    REQUIRE(slc != nullptr);
    p.Connect(recv, 0, slc, 0);
    p.Connect(slc, 0, &sinkHandle, 0);

    dictSource->SetListData(0, "set voice::1::freq 440");
    dictSource->SetListData(0, "set master::gain 0.8");

    p.PassData(std::string("dictionary s779m"), "go779m", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "dictionary l779m");
    CHECK(keeperSlice.Lookup("freq") == "440");
    CHECK(keeperRemainder.Lookup("master::gain") == "0.8");
  }

  TEST_CASE("dict.slice: no message path allocates (#779)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path: the trigger (bang and reference alike), both
    // acknowledgments and the refusal.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer, and that buffer is not
    // the same width everywhere (15 on libstdc++, 22 on libc++).
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string refSource = "dictionary probeSrc779";
    const std::string refSlice = "dictionary probeSlc779";
    const std::string refRemainder = "dictionary probeRem779";
    const std::string unknown = "frobnicate something quite long indeed";

    Rig rig("ds779n", "probeSrc779", "probeSlc779", "probeRem779", "voice::1");
    rig.Source("set voice::1::freq a value past every small-string buffer");
    rig.Source("set voice::1::adsr 5 10 80 200");
    rig.Source("set master::chain 0 4 7 12");

    // Warm every path — including both sinks' list assignments — so
    // first-call machinery is not what the probe catches.
    rig.Run();
    rig.slicer.GetInlet(0)->SetList(refSource, YSE::T_GUI);
    rig.slicer.GetInlet(1)->SetList(refSlice, YSE::T_GUI);
    rig.slicer.GetInlet(2)->SetList(refRemainder, YSE::T_GUI);
    rig.slicer.GetInlet(0)->SetList(unknown, YSE::T_GUI);
    const std::uint64_t before = rig.slicer.Dropped();

    rig.sliceOut.reset();
    rig.remainderOut.reset();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      rig.slicer.GetInlet(0)->SetBang(YSE::T_DSP);
      rig.slicer.GetInlet(0)->SetList(refSource, YSE::T_DSP);
      rig.slicer.GetInlet(1)->SetList(refSlice, YSE::T_DSP);
      rig.slicer.GetInlet(2)->SetList(refRemainder, YSE::T_DSP);
      rig.slicer.GetInlet(0)->SetList(unknown, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. Two triggers ran (a bang and a reference),
    // each replacing both halves, plus the unknown message.
    CHECK(rig.sliceOut.gotList);
    CHECK(rig.sliceOut.listValue == "dictionary probeSlc779");
    CHECK(rig.remainderOut.gotList);
    CHECK(rig.remainderOut.listValue == "dictionary probeRem779");
    CHECK(rig.slice.Count() == 2);
    CHECK(rig.remainder.Count() == 1);
    CHECK(rig.slicer.Dropped() == before + 1);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("dict.slice: params survive a DumpJSON / ParseJSON round trip (#779)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h =
        src.CreateObject(YSE::OBJ::G_DICT_SLICE, "voices779o one779o rest779o voice::1");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".dict.slice"));
    CHECK(copy->GetParams() == std::string("voices779o one779o rest779o voice::1"));
    CHECK(copy->GetInputs() == 3);
    CHECK(copy->GetOutputs() == 2);
  }

  TEST_CASE(
      "dict.slice: the path argument is optional, and stays optional through a round trip (#779)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_DICT_SLICE, "src779p slc779p rem779p");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);
    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(copy->GetParams() == std::string("src779p slc779p rem779p"));
  }

  TEST_CASE("dict.slice: carries complete documentation metadata (#779)") {
    gDictSlice g;
    CHECK_FALSE(g.GetDescription().empty());
    CHECK(g.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = g.GetParamDocs();
    REQUIRE(docs.size() == 4);
    CHECK(docs[0].name == "source");
    CHECK(docs[1].name == "slice");
    CHECK(docs[2].name == "remainder");
    CHECK(docs[3].name == "path");
  }
}
