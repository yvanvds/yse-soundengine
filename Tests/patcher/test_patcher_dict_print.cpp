// Tests for .dict.print (issue #776) — Max's dict.print ("print the contents
// of a dictionary in the Max Console") on the name-addressed value model
// .dict settled (#550). There is no Max window here, so the destination is
// the engine log, .print's destination.
//
// What has to be proven, and what every case below is one of:
//
//   - **the dictionary is bound from the creation argument.** A dictionary
//     never travels down a cord, so ".dict.print <name>" resolves the name
//     once, on the control thread, and a `dictionary <name>` message is
//     honoured only when it names the dictionary already bound —
//     DictReferenceNames' bounded compare, never a registry lookup on a
//     message path.
//   - **the dump is the nested JSON document, read out of the engine log.**
//     The assertion that matters to a user is not "the object accepted the
//     bang" but "the document came out of the log", so the content cases
//     install a real YSE::logHandler through the public API, exactly as a
//     host would, and read the lines that arrive there.
//   - **the emitter and DictToJson stay in lockstep.** The object composes
//     its JSON with a bounded allocation-free emitter of its own, because
//     DictToJson builds an nlohmann tree and is control-thread only; the
//     lockstep case parses what the object printed and compares it, as a
//     document, against DictToJson's output for an identically-filled store —
//     nesting, collisions, value typing and all.
//   - **flood behaviour reports.** A dump cut by the per-tick budget posts a
//     notice and counts its losses; a debugging instrument that silently
//     printed half a dictionary would lie about the patch it is debugging.
//   - **the dump crosses the control/audio boundary and nothing allocates.**
//     In-patcher delivery dispatches on T_DSP, so "the audio thread asks for
//     the dump" is the ordinary case.
//
// No audio device required. The registry is process-wide, so every case that
// names a dictionary uses names of its own.

#include <doctest/doctest.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "internal/rtLogQueue.h"
#include "log.hpp"
#include "patcher/genericObjects/gDict.h"
#include "patcher/genericObjects/gDictPrint.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "support/alloc_probe.hpp"

using YSE::PATCHER::dictStore;
using YSE::PATCHER::DictStoreAt;
using YSE::PATCHER::DictToJson;
using YSE::PATCHER::gDict;
using YSE::PATCHER::gDictPrint;

namespace {

  // The engine log, watched the way a host watches it: a real logHandler
  // installed through the public API — test_patcher_print's capture, for the
  // same reason. Everything the engine logs while this is alive arrives
  // here, so the accessors search rather than compare.
  struct LogCapture : YSE::logHandler {
    std::vector<std::string> lines;

    LogCapture() {
      lines.reserve(4096);
      // Whatever is still queued from an earlier test belongs to that test.
      YSE::INTERNAL::RtLog().drain();
      YSE::Log().setHandler(this);
    }
    ~LogCapture() override {
      YSE::Log().setHandler(nullptr);
    }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;
    LogCapture(LogCapture&&) = delete;
    LogCapture& operator=(LogCapture&&) = delete;

    void AddMessage(const std::string& message) override {
      lines.push_back(message);
    }

    // Run the control-thread half of the object: this is what
    // system::update() does once per tick, and until it happens a printed
    // line is still sitting in the queue.
    std::size_t Drain() {
      return YSE::INTERNAL::RtLog().drain();
    }

    bool Saw(const std::string& needle) const {
      for (const std::string& line : lines) {
        if (line.find(needle) != std::string::npos) return true;
      }
      return false;
    }

    // Every captured line carrying "<label>: ", stripped down to what follows
    // it, in arrival order — the document's rows, shorn of the label and of
    // the engine's own log tag. What Join() of these should parse as is the
    // whole point of the object.
    std::vector<std::string> Body(const std::string& label) const {
      std::vector<std::string> body;
      const std::string mark = label + ": ";
      for (const std::string& line : lines) {
        const std::size_t at = line.find(mark);
        if (at == std::string::npos) continue;
        body.push_back(line.substr(at + mark.size()));
      }
      return body;
    }

    std::string Joined(const std::string& label) const {
      std::string joined;
      for (const std::string& row : Body(label)) {
        joined += row;
        joined += '\n';
      }
      return joined;
    }

    void Clear() {
      lines.clear();
    }
  };

  // A .dict and a .dict.print on one name, sharing one patcherImplementation
  // so the name actually binds ("<patcherName>.<name>" needs a patcher to
  // prefix with — a parentless object stays private).
  struct Rig {
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gDict dict;
    gDictPrint print;

    Rig(const std::string& patcherName, const std::string& name,
        const std::string& printArgs = "") {
      p.SetName(patcherName);
      dict.SetParent(&p);
      dict.SetParams(name);
      print.SetParent(&p);
      print.SetParams(printArgs.empty() ? name : name + " " + printArgs);
    }

    void Store(const std::string& message) {
      dict.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Bang(YSE::THREAD thread = YSE::T_GUI) {
      print.GetInlet(0)->SetBang(thread);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("dict.print: registered, one inlet, no outlets (#776)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_PRINT);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".dict.print");
    CHECK(h->GetInputs() == 1);
    // No outlets: .print's shape, and the right one — an outlet would turn a
    // probe into a participant, and a patch that had to wire this object's
    // output somewhere could no longer leave it in place once it was working.
    CHECK(h->GetOutputs() == 0);
  }

  TEST_CASE("dict.print: appears in the registry's name list (#776)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_DICT_PRINT)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("dict.print: the inlet takes bang and list, and no bare number (#776)") {
    // No int or float handler: a bare number names no dictionary — the
    // family's rule.
    gDictPrint g;
    const unsigned int accepted = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((accepted & YSE::PATCHER::IT_BANG) != 0);
    CHECK((accepted & YSE::PATCHER::IT_LIST) != 0);
    CHECK((accepted & YSE::PATCHER::IT_INT) == 0);
    CHECK((accepted & YSE::PATCHER::IT_FLOAT) == 0);
  }

  // ─── creation arguments ─────────────────────────────────────────────────────

  TEST_CASE("dict.print: the name is the first argument, the budget the second (#776)") {
    {
      gDictPrint g;
      CHECK(g.DictName().empty());
      CHECK(g.LinesPerTick() == gDictPrint::DEFAULT_LINES_PER_TICK);
    }
    {
      gDictPrint g;
      g.SetParams("tempo776");
      CHECK(g.DictName() == "tempo776");
      CHECK(g.LinesPerTick() == gDictPrint::DEFAULT_LINES_PER_TICK);
    }
    {
      gDictPrint g;
      g.SetParams("tempo776 4");
      CHECK(g.LinesPerTick() == 4);
    }
    {
      // Zero is not offered: an object that printed nothing is a deleted
      // object, spelled confusingly.
      gDictPrint g;
      g.SetParams("tempo776 0");
      CHECK(g.LinesPerTick() == gDictPrint::MIN_LINES_PER_TICK);
    }
    {
      // Past the queue's own capacity there is nothing more to buy.
      gDictPrint g;
      g.SetParams("tempo776 99999");
      CHECK(g.LinesPerTick() == gDictPrint::MAX_LINES_PER_TICK);
    }
    {
      // A float is truncated, as everywhere else a creation argument is read
      // as a count.
      gDictPrint g;
      g.SetParams("tempo776 5.9");
      CHECK(g.LinesPerTick() == 5);
    }
    {
      // The case that must not throw: a non-numeric budget from a hand-edited
      // or newer saved patch leaves the default standing rather than breaking
      // the load — gPrint's rule.
      gDictPrint g;
      g.SetParams("tempo776 wibble");
      CHECK(g.DictName() == "tempo776");
      CHECK(g.LinesPerTick() == gDictPrint::DEFAULT_LINES_PER_TICK);
    }
    {
      // SetParams("") has to leave the no-argument object behind rather than
      // one still holding the previous binding.
      gDictPrint g;
      g.SetParams("tempo776 4");
      REQUIRE(g.DictName() == "tempo776");
      g.SetParams("");
      CHECK(g.DictName().empty());
      CHECK(g.Address().empty());
      CHECK(g.LinesPerTick() == gDictPrint::DEFAULT_LINES_PER_TICK);
    }
  }

  // ─── the document, end to end through the engine log ────────────────────────

  TEST_CASE("dict.print: a bang dumps the dictionary as nested JSON lines (#776)") {
    // The whole point of the object, asserted row by row: "::" paths expanded
    // into real nesting, entries grouped in storage order of first
    // appearance, values typed by the patcher's classifier — a numeric token
    // as a number, a multi-token value as an array, an empty value as "" —
    // and every line labelled with the dictionary's name.
    LogCapture log;
    Rig rig("dp776a", "d776a");
    rig.Store("set voice::1::freq 440");
    rig.Store("set voice::1::gain 0.5");
    rig.Store("set chord 0 4 7");
    rig.Store("set muted");

    log.Clear();
    rig.Bang();
    log.Drain();

    const auto body = log.Body("d776a");
    REQUIRE(body.size() == 10);
    CHECK(body[0] == "{");
    CHECK(body[1] == "  \"voice\": {");
    CHECK(body[2] == "    \"1\": {");
    CHECK(body[3] == "      \"freq\": 440,");
    CHECK(body[4] == "      \"gain\": 0.5");
    CHECK(body[5] == "    }");
    CHECK(body[6] == "  },");
    CHECK(body[7] == "  \"chord\": [0, 4, 7],");
    CHECK(body[8] == "  \"muted\": \"\"");
    CHECK(body[9] == "}");
    CHECK(rig.print.Dropped() == 0);
  }

  TEST_CASE("dict.print: what it prints parses back to DictToJson's document (#776)") {
    // The lockstep case. The object composes its JSON with a bounded emitter
    // of its own — DictToJson allocates, and a dump may be asked for on the
    // audio callback — so this is the assertion that keeps the two
    // implementations one design: for a store exercising nesting, both
    // collision directions, an empty-segment path, escaping and every value
    // type, the printed document parses to exactly the document DictToJson
    // builds. Values stay inside float precision, since the classifier the
    // object shares with every outlet is the patcher's float classifier.
    const char* const sets[] = {
        "set voice::1::freq 440", // nested int
        "set voice::1::gain 0.5", // nested float
        "set chord 0 4 7", // multi-token: an array
        "set label warm pad", // multi-token symbols: array of strings
        "set muted", // empty value: ""
        "set title he\"llo\\x", // characters JSON has to escape
        "set a 1", // leaf first ...
        "set a::b 2", // ... later sub-tree loses to it
        "set c::d 3", // branch first ...
        "set c 4", // ... later leaf loses to it
        "set e::", // empty tail segment: an empty object
        "set count 007", // spelled with leading zeros
        "set ratio 1e3", // exponent spelling: a float
        "set offset -7", // negative int
    };

    LogCapture log;
    Rig rig("dp776b", "d776b");
    dictStore expected;
    for (const char* message : sets) {
      rig.Store(message);
      // The same record, minus the "set " word, into the reference store.
      const std::string text(message + 4);
      const std::size_t space = text.find(' ');
      const std::string key = text.substr(0, space);
      const std::string value = space == std::string::npos ? "" : text.substr(space + 1);
      DictStoreAt(expected, key.c_str(), key.size(), value.c_str(), value.size());
    }
    REQUIRE(rig.dict.Count() == 14);

    log.Clear();
    rig.Bang();
    log.Drain();

    const std::string printed = log.Joined("d776b");
    REQUIRE_FALSE(printed.empty());
    const nlohmann::json parsed = nlohmann::json::parse(printed, nullptr, false);
    REQUIRE_FALSE(parsed.is_discarded());

    nlohmann::json reference;
    DictToJson(expected, reference);
    CHECK(parsed == reference);
    CHECK(rig.print.Dropped() == 0);
  }

  TEST_CASE("dict.print: an empty dictionary prints one '{}' line (#776)") {
    LogCapture log;
    Rig rig("dp776c", "d776c");

    log.Clear();
    rig.Bang();
    log.Drain();

    const auto body = log.Body("d776c");
    REQUIRE(body.size() == 1);
    CHECK(body[0] == "{}");
    CHECK(rig.print.Dropped() == 0);
  }

  TEST_CASE("dict.print: an unnamed object dumps its private dictionary as 'dict.print' (#776)") {
    // The unnamed binding is private and empty — the family's rule — and the
    // label falls back so the line still says who wrote it.
    LogCapture log;
    gDictPrint g;
    g.GetInlet(0)->SetBang(YSE::T_GUI);
    log.Drain();
    CHECK(log.Saw("dict.print: {}"));
    CHECK(g.Dropped() == 0);
  }

  TEST_CASE("dict.print: the reference dumps, anything else is refused (#776)") {
    LogCapture log;
    Rig rig("dp776d", "d776d");
    rig.Store("set lead piano");

    // The dictionary's own reference — the message its .dict emits on a
    // bang — dumps, exactly as a bang does.
    const std::uint64_t before = rig.print.Dropped();
    log.Clear();
    rig.print.GetInlet(0)->SetList("dictionary d776d", YSE::T_GUI);
    log.Drain();
    CHECK(log.Saw("d776d:   \"lead\": \"piano\""));
    CHECK(rig.print.Dropped() == before);

    // A reference to a dictionary this object is not bound to, and any other
    // message, are refused and counted, never resolved: a registry lookup is
    // a mutex, and this may be the audio thread. A refused dump prints
    // nothing.
    log.Clear();
    rig.print.GetInlet(0)->SetList("dictionary somewhere_else", YSE::T_GUI);
    rig.print.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    log.Drain();
    CHECK(log.Body("d776d").empty());
    CHECK(rig.print.Dropped() == before + 2);
  }

  // ─── flooding ───────────────────────────────────────────────────────────────

  TEST_CASE("dict.print: a dump past the budget is cut, and says so (#776)") {
    LogCapture log;
    Rig rig("dp776e", "d776e", "3");
    REQUIRE(rig.print.LinesPerTick() == 3);
    rig.Store("set a 1");
    rig.Store("set b 2");
    rig.Store("set c 3");
    rig.Store("set d 4");
    rig.Store("set e 5");

    log.Clear();
    rig.Bang(); // seven lines wanted, three allowed
    log.Drain();

    const auto body = log.Body("d776e");
    // Three rows and the notice: a truncated dump reads as a marked gap, not
    // as a complete dictionary — the one thing a debugging instrument must
    // never lie about.
    REQUIRE(body.size() == 4);
    CHECK(body[0] == "{");
    CHECK(body[1] == "  \"a\": 1,");
    CHECK(body[2] == "  \"b\": 2,");
    CHECK(body[3] == "rate limit reached (3 per tick); rest of the dump dropped");
    CHECK(rig.print.Dropped() == 4);
    CHECK(rig.print.Posted() == 4);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("dict.print: the address form is the patcher's, and RefreshBinding follows it (#776)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dp776f_before");

    gDictPrint g;
    g.SetParams("d776f");
    g.SetParent(&p);
    CHECK(g.DictName() == "d776f");
    CHECK(g.Address() == "dp776f_before.d776f");

    // Idempotent: a rebind to the address it already has keeps the store.
    g.RefreshBinding();
    CHECK(g.Address() == "dp776f_before.d776f");

    p.SetName("dp776f_after");
    g.RefreshBinding();
    CHECK(g.Address() == "dp776f_after.d776f");
  }

  TEST_CASE("dict.print: patcherImplementation::SetName re-anchors it (#776)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by the
    // patcher, without anybody calling RefreshBinding by hand. The keeper
    // holds the old-address store; after the rename the dump reads a fresh
    // empty dictionary under the new prefix.
    LogCapture log;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dp776g_before");

    gDict keeper;
    keeper.SetParent(&p);
    keeper.SetParams("d776g");
    keeper.GetInlet(0)->SetList("set lead piano", YSE::T_GUI);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_PRINT, "d776g");
    REQUIRE(h != nullptr);

    log.Clear();
    h->SetBang(0);
    log.Drain();
    CHECK(log.Saw("d776g:   \"lead\": \"piano\""));

    p.SetName("dp776g_after");
    log.Clear();
    h->SetBang(0);
    log.Drain();
    const auto body = log.Body("d776g");
    REQUIRE(body.size() == 1);
    CHECK(body[0] == "{}");
  }

  TEST_CASE("dict.print: wired from the dict's reference outlet, banging the dict prints (#776)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: the .dict's reference outlet into the dump, a bang on the .dict,
    // and the document at the far end of the engine log. Max's own gesture —
    // bang the dict, read its contents.
    LogCapture log;
    YSE::patcher p;
    p.create(2);
    p.name("dp776h");
    YSE::pHandle* dict = p.CreateObject(YSE::OBJ::G_DICT, "d776h");
    YSE::pHandle* print = p.CreateObject(YSE::OBJ::G_DICT_PRINT, "d776h");
    REQUIRE(dict != nullptr);
    REQUIRE(print != nullptr);
    p.Connect(dict, 1, print, 0);

    dict->SetListData(0, "set lead piano");
    log.Clear();
    dict->SetBang(0);
    log.Drain();

    const auto body = log.Body("d776h");
    REQUIRE(body.size() == 3);
    CHECK(body[0] == "{");
    CHECK(body[1] == "  \"lead\": \"piano\"");
    CHECK(body[2] == "}");
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("dict.print: a dump asked for over in-patcher delivery lands on T_DSP (#776)") {
    // A .r feeding the dump dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread asks for the dump" is the ordinary
    // case, and the whole path is a snapshot, bounded composes and lock-free
    // pushes.
    LogCapture log;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dp776i");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go776i");
    YSE::pHandle* dict = p.CreateObject(YSE::OBJ::G_DICT, "d776i");
    YSE::pHandle* print = p.CreateObject(YSE::OBJ::G_DICT_PRINT, "d776i");
    REQUIRE(recv != nullptr);
    REQUIRE(dict != nullptr);
    REQUIRE(print != nullptr);
    p.Connect(recv, 0, print, 0);

    dict->SetListData(0, "set lead piano");

    log.Clear();
    p.PassData(std::string("dictionary d776i"), "go776i", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    log.Drain();

    const auto body = log.Body("d776i");
    REQUIRE(body.size() == 3);
    CHECK(body[1] == "  \"lead\": \"piano\"");
  }

  TEST_CASE("dict.print: no message path allocates (#776)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path: the dump (bang and reference alike), the
    // wrong-name refusal and the unknown message. The dump composes a
    // document with nesting, arrays, strings, floats and an escape, so the
    // whole emitter runs inside the scope.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "dictionary probeD776";
    const std::string wrongName = "dictionary somewhere_else_long";
    const std::string unknown = "frobnicate something quite long indeed";

    LogCapture log;
    Rig rig("dp776j", "probeD776");
    rig.Store("set voice::1::freq a value past every small-string buffer");
    rig.Store("set chord 0 4 7 12");
    rig.Store("set gain 0.5");
    rig.Store("set title he\"llo");

    // Warm every path so first-call machinery is not what the probe catches.
    rig.Bang();
    rig.print.GetInlet(0)->SetList(reference, YSE::T_GUI);
    rig.print.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    rig.print.GetInlet(0)->SetList(unknown, YSE::T_GUI);
    log.Drain();
    const std::uint64_t posted = rig.print.Posted();
    const std::uint64_t before = rig.print.Dropped();

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      rig.Bang(YSE::T_DSP);
      rig.print.GetInlet(0)->SetList(reference, YSE::T_DSP);
      rig.print.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      rig.print.GetInlet(0)->SetList(unknown, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. Two dumps were queued and both refusals were
    // counted.
    CHECK(rig.print.Posted() > posted);
    CHECK(rig.print.Dropped() == before + 2);
    log.Drain();
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("dict.print: params survive a DumpJSON / ParseJSON round trip (#776)") {
    // Both creation arguments have to come back: a reloaded patch whose
    // .dict.print lost its name would dump a different dictionary, and one
    // that lost its budget would cut where the author had asked it not to.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_DICT_PRINT, "cfg776 32");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".dict.print"));
    CHECK(copy->GetParams() == std::string("cfg776 32"));
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 0);
  }

  TEST_CASE("dict.print: carries complete documentation metadata (#776)") {
    gDictPrint g;
    CHECK_FALSE(g.GetDescription().empty());
    CHECK(g.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = g.GetParamDocs();
    REQUIRE(docs.size() == 2);
    CHECK(docs[0].name == "name");
    CHECK(docs[1].name == "lines");
  }
}
