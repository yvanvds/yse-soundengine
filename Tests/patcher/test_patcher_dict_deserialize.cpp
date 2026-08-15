// Tests for .dict.deserialize (issue #771) — Max's dict.deserialize ("build a
// dictionary from serialised text") on the name-addressed value model .dict
// settled (#550). The read half of the interchange pair whose write half is
// .dict.serialize (#778).
//
// What has to be proven, and what every case below is one of:
//
//   - **the dictionary is bound from the creation argument.** A dictionary
//     never travels down a cord, so ".dict.deserialize <name>" resolves the
//     name once, on the control thread, and a parsed document replaces
//     exactly that dictionary.
//   - **the parse is off the message path.** nlohmann allocates without
//     bound, so the inlet is a wait-free hand-off to the background pool
//     (dictParser) and the result is installed by the block poll — which is
//     why every case pumps Calculate() after DictParser().WaitIdle() instead
//     of expecting the document to land synchronously.
//   - **the round trip is the point.** What .dict.serialize emits,
//     .dict.deserialize reproduces — asserted at the patcher level, down a
//     real cord, with .dict.compare pronouncing the verdict.
//   - **refusal, never truncation; failure, never damage.** A document past
//     what a list payload carries is refused whole; one that fails to parse
//     is counted and the bound dictionary keeps its contents.
//   - **the file route lifts the transport bound (#840).** `read <file>`
//     loads a document through the patcher's fileScheduler — the request is
//     a claim on a slot, the bytes are handed to the same parse slot the
//     inlet uses, and the install and the announcement are the same block
//     poll — so a document past the inline bound arrives whole, up to the
//     scheduler slot's 128 KiB.
//   - **nothing on the submit or install path allocates.** In-patcher
//     delivery dispatches on T_DSP, so "the audio thread hands over a
//     document" is the ordinary case.
//
// No audio device required. The registry is process-wide, so every case that
// names a dictionary uses names of its own.

#include <doctest/doctest.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "patcher/genericObjects/dictParser.h"
#include "patcher/genericObjects/gDict.h"
#include "patcher/genericObjects/gDictCompare.h"
#include "patcher/genericObjects/gDictDeserialize.h"
#include "patcher/genericObjects/gDictSerialize.h"
#include "patcher/inlet.h"
#include "patcher/io/fileScheduler.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pool_blocker.hpp"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::PoolBlocker;
using TestHelpers::Wire;
using YSE::PATCHER::DictParser;
using YSE::PATCHER::gDict;
using YSE::PATCHER::gDictCompare;
using YSE::PATCHER::gDictDeserialize;
using YSE::PATCHER::gDictSerialize;

namespace {

  // Records every reference it receives — a count as well as the last text,
  // so a failed parse (which must send nothing) is tellable from one that
  // announced again.
  struct RefSink : YSE::PATCHER::pObject {
    std::string received;
    int count = 0;

    RefSink() : pObject(false) {
      received.reserve(512);
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        received = v;
        count++;
      });
    }
    const char* Type() const override {
      return "ref_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A .dict keeper and a .dict.deserialize on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // keeper is what the assertions read the dictionary through. The sink is
  // declared before the objects so it is torn down last, while the outlet
  // wired to it still exists (see sinks.hpp on why that matters).
  struct Rig {
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    RefSink out;
    gDict dict;
    gDictDeserialize des;

    Rig(const std::string& patcherName, const std::string& name) {
      p.SetName(patcherName);
      dict.SetParent(&p);
      dict.SetParams(name);
      des.SetParent(&p);
      des.SetParams(name);
      Wire(des, 0, out);
    }

    void Submit(const std::string& document, YSE::THREAD thread = YSE::T_GUI) {
      des.GetInlet(0)->SetList(document, thread);
    }

    // The deferred half, made deterministic: join the background parse, then
    // pump the poll the patcher would run at the top of its next block.
    void Pump(YSE::THREAD thread = YSE::T_GUI) {
      DictParser().WaitIdle();
      des.Calculate(thread);
    }
  };

  // ─── file helpers (issue #840) — test_patcher_textfile's, for its reasons ───

  // A path in the system temp directory, deleted first so a leftover from an
  // earlier run cannot make a test pass for the wrong reason.
  std::string TempFile(const char* name) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path.string();
  }

  void WriteWholeFile(const std::string& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary);
    out << contents;
  }

  void Remove(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
  }

  // Drive a patcher until a `read` has landed in the dictionary. Four halves,
  // each deterministic: WaitIdle joins the disk job, the first Calculate is
  // the dispatch frame that hands the bytes to the parse slot, the parser's
  // WaitIdle joins the parse, and the second Calculate is the block poll that
  // installs the document and announces the reference.
  void SettleRead(YSE::PATCHER::patcherImplementation& p) {
    YSE::PATCHER::fileScheduler* io = p.FileIO();
    REQUIRE(io != nullptr);
    io->WaitIdle();
    p.Calculate(YSE::T_DSP);
    DictParser().WaitIdle();
    p.Calculate(YSE::T_DSP);
  }

  // A valid single-line JSON object comfortably past DOCUMENT_CAPACITY —
  // what the inline route must refuse and the file route must carry whole.
  std::string LongDocument(std::size_t entries) {
    std::string document = "{";
    for (std::size_t i = 0; i < entries; i++) {
      if (i > 0) document += ",";
      document += "\"key" + std::to_string(i) + "\":\"" + std::string(20, 'v') + "\"";
    }
    document += "}";
    return document;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("dict.deserialize: registered, one inlet, one outlet (#771)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_DESERIALIZE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".dict.deserialize");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
  }

  TEST_CASE("dict.deserialize: appears in the registry's name list (#771)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_DICT_DESERIALIZE)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("dict.deserialize: the inlet takes a list and nothing else (#771)") {
    // The document is the trigger: a bang carries no document and a bare
    // number names nothing, so neither has a handler.
    gDictDeserialize g;
    const unsigned int accepted = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((accepted & YSE::PATCHER::IT_LIST) != 0);
    CHECK((accepted & YSE::PATCHER::IT_BANG) == 0);
    CHECK((accepted & YSE::PATCHER::IT_INT) == 0);
    CHECK((accepted & YSE::PATCHER::IT_FLOAT) == 0);
  }

  // ─── creation arguments ─────────────────────────────────────────────────────

  TEST_CASE("dict.deserialize: the name is the creation argument, and '' resets (#771)") {
    {
      gDictDeserialize g;
      CHECK(g.DictName().empty());
      CHECK(g.Address().empty());
    }
    {
      gDictDeserialize g;
      g.SetParams("tempo771");
      CHECK(g.DictName() == "tempo771");
    }
    {
      // SetParams("") has to leave the no-argument object behind rather than
      // one still holding the previous binding.
      gDictDeserialize g;
      g.SetParams("tempo771");
      REQUIRE(g.DictName() == "tempo771");
      g.SetParams("");
      CHECK(g.DictName().empty());
      CHECK(g.Address().empty());
    }
  }

  // ─── the document, in ───────────────────────────────────────────────────────

  TEST_CASE("dict.deserialize: a document becomes the dictionary, then the reference (#771)") {
    // The core: nesting flattened back to "::" paths, values respelled as
    // list text (an array as its space-separated items, a string as its
    // characters, an empty string as an empty value), the reference out the
    // outlet once — and only once — the result is installed. Before the
    // pump, nothing has happened: the parse is on the pool, which is the
    // design, not a latency bug.
    Rig rig("dd771a", "d771a");
    rig.Submit("{\"voice\":{\"1\":{\"freq\":440,\"gain\":0.5}},\"chord\":[0,4,7],\"muted\":\"\"}");
    CHECK(rig.dict.Count() == 0);
    CHECK(rig.out.count == 0);

    rig.Pump();
    REQUIRE(rig.out.count == 1);
    CHECK(rig.out.received == "dictionary d771a");
    CHECK(rig.des.Parsed() == 1);
    CHECK(rig.des.Failed() == 0);
    CHECK(rig.des.Dropped() == 0);

    REQUIRE(rig.dict.Count() == 4);
    CHECK(rig.dict.Lookup("voice::1::freq") == "440");
    CHECK(rig.dict.Lookup("voice::1::gain") == "0.5");
    CHECK(rig.dict.Lookup("chord") == "0 4 7");
    CHECK(rig.dict.Lookup("muted").empty());
    // Storage order is nlohmann's object order (sorted by key), exactly as a
    // saved patch reloads through RestoreState — the same document, whichever
    // road it arrives by. Pinned so a drift between the two roads would show.
    CHECK(rig.dict.KeyAt(0) == "chord");
    CHECK(rig.dict.KeyAt(1) == "muted");
    CHECK(rig.dict.KeyAt(2) == "voice::1::freq");
    CHECK(rig.dict.KeyAt(3) == "voice::1::gain");
  }

  TEST_CASE("dict.deserialize: a parsed document replaces the dictionary whole (#771)") {
    // DictFromJson's contract, and .dict.pack's and .dict.join's rule for a
    // target: a stale entry from an earlier run must not survive inside the
    // parsed unit.
    Rig rig("dd771b", "d771b");
    rig.dict.GetInlet(0)->SetList("set stale 1", YSE::T_GUI);
    REQUIRE(rig.dict.Count() == 1);

    rig.Submit("{\"fresh\":2}");
    rig.Pump();
    REQUIRE(rig.dict.Count() == 1);
    CHECK(rig.dict.Lookup("fresh") == "2");
    CHECK(rig.dict.Lookup("stale").empty());
  }

  TEST_CASE("dict.deserialize: external JSON is respelled as patcher list text (#771)") {
    // A document need not come from .dict.serialize: a host hands over
    // booleans, nulls and mixed arrays this patcher never spells. They load
    // by DictFromJson's reading — true/false as 1/0, null as an empty value,
    // an array as the space-separated list it spells.
    Rig rig("dd771c", "d771c");
    rig.Submit("{\"a\":true,\"b\":null,\"c\":[1,\"x\",false],\"n\":{\"m\":2}}");
    rig.Pump();
    REQUIRE(rig.des.Parsed() == 1);
    CHECK(rig.dict.Lookup("a") == "1");
    CHECK(rig.dict.Lookup("b").empty());
    CHECK(rig.dict.Lookup("c") == "1 x 0");
    CHECK(rig.dict.Lookup("n::m") == "2");
  }

  TEST_CASE("dict.deserialize: an empty document is an empty dictionary, announced (#771)") {
    // "{}" is what .dict.serialize emits for an empty dictionary, so it must
    // deserialise — to a cleared dictionary, not to a failure.
    Rig rig("dd771d", "d771d");
    rig.dict.GetInlet(0)->SetList("set lead piano", YSE::T_GUI);
    REQUIRE(rig.dict.Count() == 1);

    rig.Submit("{}");
    rig.Pump();
    CHECK(rig.des.Parsed() == 1);
    CHECK(rig.dict.Count() == 0);
    CHECK(rig.out.count == 1);
  }

  TEST_CASE("dict.deserialize: an unnamed object fills its private dictionary, silently (#771)") {
    // The family's rule for the empty name — private, never pooled — and the
    // reference rule that follows: nothing to name, nothing to announce. The
    // document still parses and installs, which Parsed() is here to see.
    RefSink out;
    gDictDeserialize g;
    Wire(g, 0, out);
    g.GetInlet(0)->SetList("{\"a\":1}", YSE::T_GUI);
    DictParser().WaitIdle();
    g.Calculate(YSE::T_GUI);
    CHECK(g.Parsed() == 1);
    CHECK(g.Dropped() == 0);
    CHECK(out.count == 0);
  }

  // ─── failure, never damage ──────────────────────────────────────────────────

  TEST_CASE("dict.deserialize: a malformed document fails, counted, changing nothing (#771)") {
    Rig rig("dd771e", "d771e");
    rig.dict.GetInlet(0)->SetList("set keep me", YSE::T_GUI);
    REQUIRE(rig.dict.Count() == 1);

    rig.Submit("{\"broken\":");
    rig.Pump();
    CHECK(rig.des.Failed() == 1);
    CHECK(rig.des.Parsed() == 0);
    CHECK(rig.out.count == 0);
    // The bound dictionary is untouched: a bad document never costs a
    // dictionary its contents.
    CHECK(rig.dict.Count() == 1);
    CHECK(rig.dict.Lookup("keep") == "me");
  }

  TEST_CASE("dict.deserialize: a document that is not a JSON object fails (#771)") {
    // A dictionary is a JSON object. A bare number, an array, and a
    // dictionary *reference* wired here by mistake are all failures — not
    // empty dictionaries, and never a resolve of the name on a message path.
    Rig rig("dd771f", "d771f");
    const char* const bad[] = {"5", "[1,2,3]", "\"text\"", "dictionary d771f"};
    std::uint64_t expected = 0;
    for (const char* document : bad) {
      rig.Submit(document);
      rig.Pump();
      expected++;
      CHECK(rig.des.Failed() == expected);
    }
    CHECK(rig.des.Parsed() == 0);
    CHECK(rig.out.count == 0);
  }

  // ─── refusal, never truncation ──────────────────────────────────────────────

  TEST_CASE("dict.deserialize: a document past the payload bound is refused whole (#771)") {
    // The bound is what the patcher's value queue carries (kValueListCap - 1
    // = DOCUMENT_CAPACITY), the same bound .dict.serialize enforces from the
    // write side: a longer document could arrive over a direct cord but
    // never over a .s/.r, and the prefix of a JSON document is a different
    // document — so nothing is parsed at all, and the loss is counted.
    Rig rig("dd771g", "d771g");
    std::string document = "{\"k\":\"";
    document.append(gDictDeserialize::DOCUMENT_CAPACITY, 'a');
    document += "\"}";
    REQUIRE(document.size() > gDictDeserialize::DOCUMENT_CAPACITY);

    rig.Submit(document);
    CHECK(rig.des.Dropped() == 1);
    rig.Pump();
    CHECK(rig.des.Parsed() == 0);
    CHECK(rig.des.Failed() == 0);
    CHECK(rig.out.count == 0);
    CHECK(rig.dict.Count() == 0);
  }

  TEST_CASE("dict.deserialize: a document arriving mid-parse is refused, not queued (#771)") {
    // One slot, one document: remembering a second ask without its bytes
    // would re-parse the old text as the new one, so the loser is refused
    // and counted — the family's busy rule. Park the pool's one worker in a
    // blocker so the first document provably cannot have finished.
    PoolBlocker blocker;
    REQUIRE(blocker.Park());

    Rig rig("dd771h", "d771h");
    rig.Submit("{\"first\":1}");
    rig.Submit("{\"second\":2}");
    CHECK(rig.des.Dropped() == 1);

    blocker.Unpark();
    rig.Pump();

    // The first document is the one that landed.
    CHECK(rig.des.Parsed() == 1);
    CHECK(rig.dict.Lookup("first") == "1");
    CHECK(rig.dict.Lookup("second").empty());
  }

  // ─── the round trip — the interchange pair ──────────────────────────────────

  TEST_CASE("dict.deserialize: serialise -> deserialise reproduces the dictionary (#771)") {
    // The pair working as a pair, down a real cord: .dict.serialize's outlet
    // wired straight into .dict.deserialize's inlet, and .dict.compare — a
    // third object, reading both dictionaries by name — pronouncing the
    // verdict. Nesting, a float, an array, symbols and an empty value all
    // make the crossing.
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dd771i");
    TestHelpers::IntSink verdict;
    RefSink refOut;
    gDict src;
    gDict dst;
    gDictSerialize ser;
    gDictDeserialize des;
    gDictCompare cmp;

    src.SetParent(&p);
    src.SetParams("src771i");
    dst.SetParent(&p);
    dst.SetParams("dst771i");
    ser.SetParent(&p);
    ser.SetParams("src771i");
    des.SetParent(&p);
    des.SetParams("dst771i");
    cmp.SetParent(&p);
    cmp.SetParams("src771i dst771i");
    Wire(ser, 0, des);
    Wire(des, 0, refOut);
    Wire(cmp, 0, verdict);

    src.GetInlet(0)->SetList("set voice::1::freq 440", YSE::T_GUI);
    src.GetInlet(0)->SetList("set voice::1::gain 0.5", YSE::T_GUI);
    src.GetInlet(0)->SetList("set chord 0 4 7", YSE::T_GUI);
    src.GetInlet(0)->SetList("set label warm pad", YSE::T_GUI);
    src.GetInlet(0)->SetList("set muted", YSE::T_GUI);
    REQUIRE(src.Count() == 5);

    // Not equal yet — the comparison must be able to say no, or its yes
    // below proves nothing.
    cmp.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(verdict.gotInt);
    CHECK(verdict.received == 0);

    // Bang the serialiser: the document crosses the cord into the
    // deserialiser's inlet synchronously, parses on the pool, installs on
    // the pump.
    ser.GetInlet(0)->SetBang(YSE::T_GUI);
    DictParser().WaitIdle();
    des.Calculate(YSE::T_GUI);
    REQUIRE(des.Parsed() == 1);
    REQUIRE(refOut.count == 1);
    CHECK(refOut.received == "dictionary dst771i");

    cmp.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(verdict.received == 1);

    // And entry for entry, because "equal" should be checkable by hand too.
    CHECK(dst.Count() == 5);
    CHECK(dst.Lookup("voice::1::freq") == "440");
    CHECK(dst.Lookup("voice::1::gain") == "0.5");
    CHECK(dst.Lookup("chord") == "0 4 7");
    CHECK(dst.Lookup("label") == "warm pad");
    CHECK(dst.Lookup("muted").empty());
  }

  TEST_CASE("dict.deserialize: the round trip canonicalises a float's spelling (#771)") {
    // The documented respelling, pinned so it cannot drift silently: a
    // stored "120." leaves .dict.serialize as the JSON number 120.0 and
    // comes back as the text "120.0" — the same number, stable from the
    // second round trip on.
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dd771j");
    gDict src;
    gDict dst;
    gDictSerialize ser;
    gDictDeserialize des;

    src.SetParent(&p);
    src.SetParams("src771j");
    dst.SetParent(&p);
    dst.SetParams("dst771j");
    ser.SetParent(&p);
    ser.SetParams("src771j");
    des.SetParent(&p);
    des.SetParams("dst771j");
    Wire(ser, 0, des);

    src.GetInlet(0)->SetList("set bpm 120.", YSE::T_GUI);
    ser.GetInlet(0)->SetBang(YSE::T_GUI);
    DictParser().WaitIdle();
    des.Calculate(YSE::T_GUI);
    REQUIRE(des.Parsed() == 1);
    CHECK(dst.Lookup("bpm") == "120.0");
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE(
      "dict.deserialize: the address form is the patcher's, and RefreshBinding follows it (#771)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dd771k_before");

    gDictDeserialize g;
    g.SetParams("d771k");
    g.SetParent(&p);
    CHECK(g.DictName() == "d771k");
    CHECK(g.Address() == "dd771k_before.d771k");

    // Idempotent: a rebind to the address it already has keeps the store.
    g.RefreshBinding();
    CHECK(g.Address() == "dd771k_before.d771k");

    p.SetName("dd771k_after");
    g.RefreshBinding();
    CHECK(g.Address() == "dd771k_after.d771k");
  }

  TEST_CASE("dict.deserialize: patcherImplementation::SetName re-anchors it (#771)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by the
    // patcher, without anybody calling RefreshBinding by hand. The keepers
    // are standalone, so each stays on the address it bound: after the
    // rename, a parsed document lands in the new-prefix dictionary and the
    // old one keeps what it had.
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dd771l_before");

    gDict keeperOld;
    keeperOld.SetParent(&p);
    keeperOld.SetParams("d771l");

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_DESERIALIZE, "d771l");
    REQUIRE(h != nullptr);

    h->SetListData(0, "{\"first\":1}");
    DictParser().WaitIdle();
    p.Calculate(YSE::T_DSP);
    REQUIRE(keeperOld.Count() == 1);
    CHECK(keeperOld.Lookup("first") == "1");

    p.SetName("dd771l_after");
    gDict keeperNew;
    keeperNew.SetParent(&p);
    keeperNew.SetParams("d771l");

    h->SetListData(0, "{\"second\":2}");
    DictParser().WaitIdle();
    p.Calculate(YSE::T_DSP);
    CHECK(keeperNew.Count() == 1);
    CHECK(keeperNew.Lookup("second") == "2");
    // The old-prefix dictionary was not what the renamed object filled.
    CHECK(keeperOld.Count() == 1);
    CHECK(keeperOld.Lookup("second").empty());
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("dict.deserialize: the block poll delivers a document sent through the patcher "
            "(#771)") {
    // The full in-patcher path, exactly as a patch runs it: the host passes
    // the document to a .r, the value drain hands it to the inlet on the
    // audio thread (T_DSP block), the submit rides to the pool, and a block
    // poll — WantsBlockPoll, the reason this object is in the GraphState's
    // pollers at all — installs it and announces it. No hand-pumped Calculate
    // anywhere: the patcher's own dispatch does it.
    //
    // The pool's worker is parked across the submitting block (issue #855,
    // the .array.deserialize twin's #854). patcherImplementation::Calculate
    // drains the value queue *before* it runs the pollers, so the submit and
    // a poll are both inside that one block: a parse that finished in between
    // is installed by that same block's poll, which is the object behaving
    // correctly — dictParser hands a slot's result to exactly one consumer
    // (READY -> IDLE inside Consume), so it announces once, at the first poll
    // that finds a finished parse — but it makes an unparked "nothing
    // announced yet" a bet on how fast the pool is rather than a statement
    // about the object. Parked, what the object does promise is asserted as
    // fact, in both halves: nothing is installed while the parse is
    // unfinished, and nothing is installed once it has finished either until
    // a block polls.
    RefSink out;
    YSE::pHandle outHandle(&out);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dd771m");

    gDict keeper;
    keeper.SetParent(&p);
    keeper.SetParams("d771m");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "doc771m");
    YSE::pHandle* des = p.CreateObject(YSE::OBJ::G_DICT_DESERIALIZE, "d771m");
    REQUIRE(recv != nullptr);
    REQUIRE(des != nullptr);
    p.Connect(recv, 0, des, 0);
    p.Connect(des, 0, &outHandle, 0);

    // Parked here rather than at the top of the case: the graph is built
    // first, so nothing this patcher needs from the pool is queued behind the
    // blocker, and the blocker is torn down before the patcher either way.
    PoolBlocker blocker;
    REQUIRE(blocker.Park());

    p.PassData(std::string("{\"lead\":\"piano\",\"gain\":0.5}"), "doc771m", YSE::T_GUI);
    p.Calculate(YSE::T_DSP); // drains the value queue: the submit happens here
    CHECK(out.count == 0); // the parse is parked, so there is nothing to
                           // install — and the inlet installed nothing itself

    blocker.Unpark(); // the parse runs
    DictParser().WaitIdle(); // and finishes on the pool
    CHECK(out.count == 0); // still nothing: only a block poll installs a result
    p.Calculate(YSE::T_DSP); // the poll installs and announces

    REQUIRE(out.count == 1);
    CHECK(out.received == "dictionary d771m");
    CHECK(keeper.Count() == 2);
    CHECK(keeper.Lookup("lead") == "piano");
    CHECK(keeper.Lookup("gain") == "0.5");
  }

  TEST_CASE("dict.deserialize: neither the submit nor the install allocates (#771)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // both halves the object runs on the patcher's threads: the hand-off
    // (submit, the over-long refusal, the busy refusal — T_DSP, the ordinary
    // case) and the block poll's install-and-announce. The parse itself
    // allocates on the background pool, which is the design — and the probe
    // is thread-scoped (#701), so what it measures here is exactly the two
    // paths under test.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    Rig rig("dd771n", "probeD771");
    const std::string document =
        "{\"voice\":{\"1\":{\"freq\":440,\"gain\":0.5}},\"chord\":[0,4,7],\"title\":\"warm pad\"}";
    std::string overlong = "{\"k\":\"";
    overlong.append(gDictDeserialize::DOCUMENT_CAPACITY, 'a');
    overlong += "\"}";

    // Warm every path — including the sink's assignment and the store's row
    // assigns — so first-call machinery is not what the probe catches.
    rig.Submit(document);
    rig.Pump();
    REQUIRE(rig.des.Parsed() == 1);
    const std::uint64_t before = rig.des.Dropped();

    int submitCount = -1;
    {
      TestHelpers::ProbeScope probe;
      rig.Submit(document, YSE::T_DSP); // the hand-off
      rig.Submit(document, YSE::T_DSP); // refused: the slot is busy
      rig.Submit(overlong, YSE::T_DSP); // refused: past the payload bound
      submitCount = TestHelpers::g_alloc_count.load();
    }
    CHECK(submitCount == 0);

    DictParser().WaitIdle();

    int installCount = -1;
    {
      TestHelpers::ProbeScope probe;
      rig.des.Calculate(YSE::T_GUI); // the poll: install + announce
      installCount = TestHelpers::g_alloc_count.load();
    }
    CHECK(installCount == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing.
    CHECK(rig.des.Parsed() == 2);
    CHECK(rig.des.Dropped() == before + 2);
    CHECK(rig.out.count == 2);
  }

  // ─── the file route (issue #840) ────────────────────────────────────────────

  TEST_CASE("dict.deserialize: read loads a document from a real file (#840)") {
    // The read route end to end, through a real patcher and a real file on
    // disk: the message is a claim on a fileScheduler slot and nothing more,
    // the completion hands the bytes to the parse slot, and the block poll
    // installs the document and announces the reference — exactly the inlet
    // path from the hand-off on.
    const std::string path = TempFile("yse_dict_read_840.json");
    WriteWholeFile(path, "{\"voice\":{\"1\":{\"freq\":440}},\"chord\":[0,4,7]}");

    RefSink out;
    YSE::pHandle outHandle(&out);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dd840a");

    gDict keeper;
    keeper.SetParent(&p);
    keeper.SetParams("d840a");

    YSE::pHandle* des = p.CreateObject(YSE::OBJ::G_DICT_DESERIALIZE, "d840a");
    REQUIRE(des != nullptr);
    p.Connect(des, 0, &outHandle, 0);

    des->SetListData(0, "read " + path);
    // Nothing on the message path: the request is a claim on a slot, and the
    // disk has not been touched on this thread.
    REQUIRE(p.FileIO() != nullptr);
    CHECK(p.FileIO()->PendingCount() == 1);
    CHECK(keeper.Count() == 0);

    SettleRead(p);
    REQUIRE(out.count == 1);
    CHECK(out.received == "dictionary d840a");
    REQUIRE(keeper.Count() == 2);
    CHECK(keeper.Lookup("voice::1::freq") == "440");
    CHECK(keeper.Lookup("chord") == "0 4 7");

    Remove(path);
  }

  TEST_CASE("dict.deserialize: read lifts the inline transport bound (#840)") {
    // The reason the route exists: a document past DOCUMENT_CAPACITY — which
    // the inlet refuses whole, the value queue being its transport — arrives
    // whole through a fileScheduler slot. The same document, both routes: one
    // refusal, one dictionary.
    const std::string document = LongDocument(24);
    REQUIRE(document.size() > gDictDeserialize::DOCUMENT_CAPACITY);

    const std::string path = TempFile("yse_dict_read_long_840.json");
    WriteWholeFile(path, document);

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dd840b");

    gDict keeper;
    keeper.SetParent(&p);
    keeper.SetParams("d840b");

    YSE::pHandle* des = p.CreateObject(YSE::OBJ::G_DICT_DESERIALIZE, "d840b");
    REQUIRE(des != nullptr);

    // The inline route refuses it whole — nothing lands.
    des->SetListData(0, document);
    DictParser().WaitIdle();
    p.Calculate(YSE::T_DSP);
    CHECK(keeper.Count() == 0);

    // The file route carries it whole — every entry lands.
    des->SetListData(0, "read " + path);
    SettleRead(p);
    REQUIRE(keeper.Count() == 24);
    CHECK(keeper.Lookup("key0") == std::string(20, 'v'));
    CHECK(keeper.Lookup("key23") == std::string(20, 'v'));

    Remove(path);
  }

  TEST_CASE("dict.deserialize: a missing file fails, changing nothing (#840)") {
    // The scheduler reports a file it could not read as a completion with no
    // bytes; like a malformed document, it installs nothing and announces
    // nothing — a bad read never costs a dictionary its contents.
    const std::string path = TempFile("yse_dict_read_missing_840.json");

    RefSink out;
    YSE::pHandle outHandle(&out);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dd840c");

    gDict keeper;
    keeper.SetParent(&p);
    keeper.SetParams("d840c");
    keeper.GetInlet(0)->SetList("set keep me", YSE::T_GUI);
    REQUIRE(keeper.Count() == 1);

    YSE::pHandle* des = p.CreateObject(YSE::OBJ::G_DICT_DESERIALIZE, "d840c");
    REQUIRE(des != nullptr);
    p.Connect(des, 0, &outHandle, 0);

    des->SetListData(0, "read " + path);
    SettleRead(p);
    CHECK(out.count == 0);
    CHECK(keeper.Count() == 1);
    CHECK(keeper.Lookup("keep") == "me");
  }

  TEST_CASE("dict.deserialize: a read with no plumbing or no path is refused, counted (#840)") {
    {
      // A standalone object has no patcher and so no file plumbing.
      gDictDeserialize g;
      g.GetInlet(0)->SetList("read somewhere.json", YSE::T_GUI);
      CHECK(g.Dropped() == 1);
      CHECK(g.Parsed() == 0);
    }
    {
      // Parented, but a bare `read` with no path ever given: there is no
      // dialog to ask with, and nothing remembered to fall back on.
      YSE::PATCHER::patcherImplementation p(2, nullptr);
      p.SetName("dd840d");
      gDictDeserialize g;
      g.SetParent(&p);
      g.SetParams("d840d");
      REQUIRE(p.FileIO() != nullptr);
      g.GetInlet(0)->SetList("read", YSE::T_GUI);
      CHECK(g.Dropped() == 1);
      CHECK(p.FileIO()->PendingCount() == 0);
    }
  }

  TEST_CASE("dict.deserialize: the read request path does not allocate (#840)") {
    // The request is a claim on a patcher-owned slot: a path remembered in
    // storage reserved at construction and a bounded copy into the slot —
    // nothing more, because a `read` may arrive on the audio callback. The
    // probe is thread-scoped (#701), so the disk work and the parse on the
    // background pool are not what it measures.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string path = TempFile("yse_dict_read_probe_840.json");
    WriteWholeFile(path, "{\"a\":1}");

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dd840e");
    gDictDeserialize g;
    g.SetParent(&p);
    g.SetParams("d840e");
    REQUIRE(p.FileIO() != nullptr);

    const std::string message = "read " + path;
    // Warm the path memory and the scheduler machinery.
    g.GetInlet(0)->SetList(message, YSE::T_GUI);
    p.FileIO()->WaitIdle();

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      g.GetInlet(0)->SetList(message, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);
    CHECK(g.Dropped() == 0);

    p.FileIO()->WaitIdle();
    Remove(path);
  }

  TEST_CASE("dict.deserialize: write -> file -> read reproduces the dictionary (#840)") {
    // The interchange pair through a real file on disk, past the inline
    // bound: a dictionary too large for a list payload leaves whole through
    // .dict.serialize's `write`, comes back whole through this object's
    // `read`, and .dict.compare — a third object, reading both dictionaries
    // by name — pronounces the verdict.
    const std::string path = TempFile("yse_dict_file_roundtrip_840.json");

    // The sinks are declared before the patcher so they are torn down last,
    // while the cords wired to them still exist (sinks.hpp's rule).
    TestHelpers::IntSink verdict;
    RefSink refOut;
    YSE::pHandle refOutHandle(&refOut);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dd840f");
    gDict src;
    gDict dst;
    gDictCompare cmp;

    src.SetParent(&p);
    src.SetParams("src840f");
    dst.SetParent(&p);
    dst.SetParams("dst840f");
    cmp.SetParent(&p);
    cmp.SetParams("src840f dst840f");
    Wire(cmp, 0, verdict);

    YSE::pHandle* ser = p.CreateObject(YSE::OBJ::G_DICT_SERIALIZE, "src840f");
    YSE::pHandle* des = p.CreateObject(YSE::OBJ::G_DICT_DESERIALIZE, "dst840f");
    REQUIRE(ser != nullptr);
    REQUIRE(des != nullptr);
    p.Connect(des, 0, &refOutHandle, 0);

    // A dictionary whose document is past what a list payload carries.
    const std::string longValue(20, 'v');
    for (int i = 0; i < 24; i++) {
      src.GetInlet(0)->SetList("set key" + std::to_string(i) + " " + longValue, YSE::T_GUI);
    }
    REQUIRE(src.Count() == 24);

    // Not equal yet — the comparison must be able to say no, or its yes
    // below proves nothing.
    cmp.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(verdict.gotInt);
    CHECK(verdict.received == 0);

    ser->SetListData(0, "write " + path);
    REQUIRE(p.FileIO() != nullptr);
    p.FileIO()->WaitIdle();

    des->SetListData(0, "read " + path);
    SettleRead(p);
    REQUIRE(refOut.count == 1);
    CHECK(refOut.received == "dictionary dst840f");

    cmp.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(verdict.received == 1);
    CHECK(dst.Count() == 24);
    CHECK(dst.Lookup("key0") == longValue);
    CHECK(dst.Lookup("key23") == longValue);

    Remove(path);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("dict.deserialize: params survive a DumpJSON / ParseJSON round trip (#771)") {
    // The creation argument has to come back: a reloaded patch whose
    // .dict.deserialize lost its name would fill a different dictionary.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_DICT_DESERIALIZE, "cfg771");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".dict.deserialize"));
    CHECK(copy->GetParams() == std::string("cfg771"));
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 1);
  }

  TEST_CASE("dict.deserialize: carries complete documentation metadata (#771)") {
    gDictDeserialize g;
    CHECK_FALSE(g.GetDescription().empty());
    CHECK(g.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = g.GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == "name");
  }
}
