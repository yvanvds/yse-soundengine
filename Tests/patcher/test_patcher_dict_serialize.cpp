// Tests for .dict.serialize (issue #778) — Max's dict.serialize ("convert a
// dictionary into a single symbol") on the name-addressed value model .dict
// settled (#550). The write half of the interchange pair whose read half is
// .dict.deserialize (#771).
//
// What has to be proven, and what every case below is one of:
//
//   - **the dictionary is bound from the creation argument.** A dictionary
//     never travels down a cord, so ".dict.serialize <name>" resolves the
//     name once, on the control thread, and a `dictionary <name>` message is
//     honoured only when it names the dictionary already bound —
//     DictReferenceNames' bounded compare, never a registry lookup on a
//     message path.
//   - **the document is the format contract.** One list message holding a
//     single-line compact JSON object — "::" paths expanded into real
//     nesting, members in storage order, no whitespace outside strings —
//     asserted character for character, because .dict.deserialize parses
//     exactly this.
//   - **the emitter and DictToJson stay in lockstep.** The object composes
//     its JSON with the bounded allocation-free emitter shape .dict.print
//     established, because DictToJson builds an nlohmann tree and is
//     control-thread only; the lockstep case parses what the object emitted
//     and compares it, as a document, against DictToJson's output for an
//     identically-filled store — nesting, collisions, value typing and all.
//   - **refusal, never truncation.** A finished document past what a list
//     payload can carry is refused whole and counted; a truncated JSON
//     document parses as a different dictionary or none at all.
//   - **the file route lifts the outlet bound (#840).** `write <file>` hands
//     the same composed document to the patcher's fileScheduler instead of
//     the outlet, so a dictionary too large for a list payload leaves whole
//     — up to the scheduler slot's 128 KiB, past which it is refused whole,
//     the same rule at the wider bound.
//   - **the document crosses the control/audio boundary and nothing
//     allocates.** In-patcher delivery dispatches on T_DSP, so "the audio
//     thread asks for the document" is the ordinary case.
//
// No audio device required. The registry is process-wide, so every case that
// names a dictionary uses names of its own.

#include <doctest/doctest.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <system_error>

#include "patcher/genericObjects/gDict.h"
#include "patcher/genericObjects/gDictSerialize.h"
#include "patcher/inlet.h"
#include "patcher/io/fileScheduler.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::Wire;
using YSE::PATCHER::dictStore;
using YSE::PATCHER::DictStoreAt;
using YSE::PATCHER::DictToJson;
using YSE::PATCHER::gDict;
using YSE::PATCHER::gDictSerialize;

namespace {

  // Records every document it receives — a count as well as the last text,
  // so a refused serialisation (which must send nothing) is tellable from
  // one that sent the previous document again. `onList` lets a case act from
  // *inside* the send: the loop-back trigger is a thing the document's own
  // subgraph does.
  struct DocSink : YSE::PATCHER::pObject {
    std::string received;
    int count = 0;
    std::function<void()> onList;

    DocSink() : pObject(false) {
      received.reserve(512);
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        received = v;
        count++;
        if (onList) onList();
      });
    }
    const char* Type() const override {
      return "doc_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A .dict and a .dict.serialize on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sink is declared before the objects so it is torn down last, while the
  // outlet wired to it still exists (see sinks.hpp on why that matters).
  struct Rig {
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    DocSink out;
    gDict dict;
    gDictSerialize ser;

    Rig(const std::string& patcherName, const std::string& name) {
      p.SetName(patcherName);
      dict.SetParent(&p);
      dict.SetParams(name);
      ser.SetParent(&p);
      ser.SetParams(name);
      Wire(ser, 0, out);
    }

    void Store(const std::string& message) {
      dict.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Bang(YSE::THREAD thread = YSE::T_GUI) {
      ser.GetInlet(0)->SetBang(thread);
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

  std::string ReadWholeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }

  void Remove(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("dict.serialize: registered, one inlet, one outlet (#778)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_SERIALIZE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".dict.serialize");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
  }

  TEST_CASE("dict.serialize: appears in the registry's name list (#778)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_DICT_SERIALIZE)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("dict.serialize: the inlet takes bang and list, and no bare number (#778)") {
    // No int or float handler: a bare number names no dictionary — the
    // family's rule.
    gDictSerialize g;
    const unsigned int accepted = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((accepted & YSE::PATCHER::IT_BANG) != 0);
    CHECK((accepted & YSE::PATCHER::IT_LIST) != 0);
    CHECK((accepted & YSE::PATCHER::IT_INT) == 0);
    CHECK((accepted & YSE::PATCHER::IT_FLOAT) == 0);
  }

  // ─── creation arguments ─────────────────────────────────────────────────────

  TEST_CASE("dict.serialize: the name is the creation argument, and '' resets (#778)") {
    {
      gDictSerialize g;
      CHECK(g.DictName().empty());
      CHECK(g.Address().empty());
    }
    {
      gDictSerialize g;
      g.SetParams("tempo778");
      CHECK(g.DictName() == "tempo778");
    }
    {
      // SetParams("") has to leave the no-argument object behind rather than
      // one still holding the previous binding.
      gDictSerialize g;
      g.SetParams("tempo778");
      REQUIRE(g.DictName() == "tempo778");
      g.SetParams("");
      CHECK(g.DictName().empty());
      CHECK(g.Address().empty());
    }
  }

  // ─── the document — the format contract ─────────────────────────────────────

  TEST_CASE("dict.serialize: a bang sends the compact single-line document (#778)") {
    // The format contract, asserted character for character, because
    // .dict.deserialize parses exactly this: one JSON object on one line, no
    // whitespace outside strings, "::" paths expanded into real nesting,
    // members in storage order of first appearance, values typed by the
    // patcher's classifier — a numeric token as a number, a multi-token
    // value as an array, an empty value as "".
    Rig rig("ds778a", "d778a");
    rig.Store("set voice::1::freq 440");
    rig.Store("set voice::1::gain 0.5");
    rig.Store("set chord 0 4 7");
    rig.Store("set muted");

    rig.Bang();
    REQUIRE(rig.out.count == 1);
    CHECK(rig.out.received ==
          "{\"voice\":{\"1\":{\"freq\":440,\"gain\":0.5}},\"chord\":[0,4,7],\"muted\":\"\"}");
    CHECK(rig.ser.Emitted() == 1);
    CHECK(rig.ser.Dropped() == 0);
  }

  TEST_CASE("dict.serialize: what it emits parses back to DictToJson's document (#778)") {
    // The lockstep case. The object composes its JSON with a bounded emitter
    // — DictToJson allocates, and a document may be asked for on the audio
    // callback — so this is the assertion that keeps the two implementations
    // one design: for a store exercising nesting, both collision directions,
    // an empty-segment path, escaping and every value type, the emitted
    // document parses to exactly the document DictToJson builds. Values stay
    // inside float precision, since the classifier the object shares with
    // every outlet is the patcher's float classifier.
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

    Rig rig("ds778b", "d778b");
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

    rig.Bang();
    REQUIRE(rig.out.count == 1);
    const nlohmann::json parsed = nlohmann::json::parse(rig.out.received, nullptr, false);
    REQUIRE_FALSE(parsed.is_discarded());

    nlohmann::json reference;
    DictToJson(expected, reference);
    CHECK(parsed == reference);

    // And the contract's spelling holds even here: one line, no whitespace
    // outside strings — the blank in "warm pad" is inside one.
    CHECK(rig.out.received.find('\n') == std::string::npos);
    CHECK(rig.ser.Dropped() == 0);
  }

  TEST_CASE("dict.serialize: an empty dictionary is '{}' (#778)") {
    Rig rig("ds778c", "d778c");
    rig.Bang();
    REQUIRE(rig.out.count == 1);
    CHECK(rig.out.received == "{}");
  }

  TEST_CASE("dict.serialize: an unnamed object serialises its private dictionary (#778)") {
    // The unnamed binding is private and empty — the family's rule — so the
    // document is {} rather than a refusal: an empty dictionary is a
    // dictionary.
    DocSink out;
    gDictSerialize g;
    Wire(g, 0, out);
    g.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(out.count == 1);
    CHECK(out.received == "{}");
    CHECK(g.Dropped() == 0);
  }

  TEST_CASE("dict.serialize: the reference serialises, anything else is refused (#778)") {
    Rig rig("ds778d", "d778d");
    rig.Store("set lead piano");

    // The dictionary's own reference — the message its .dict emits on a
    // bang — serialises, exactly as a bang does.
    const std::uint64_t before = rig.ser.Dropped();
    rig.ser.GetInlet(0)->SetList("dictionary d778d", YSE::T_GUI);
    REQUIRE(rig.out.count == 1);
    CHECK(rig.out.received == "{\"lead\":\"piano\"}");
    CHECK(rig.ser.Dropped() == before);

    // A reference to a dictionary this object is not bound to, and any other
    // message, are refused and counted, never resolved: a registry lookup is
    // a mutex, and this may be the audio thread. A refused serialisation
    // sends nothing.
    rig.ser.GetInlet(0)->SetList("dictionary somewhere_else", YSE::T_GUI);
    rig.ser.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.out.count == 1);
    CHECK(rig.ser.Dropped() == before + 2);
  }

  // ─── refusal, never truncation ──────────────────────────────────────────────

  TEST_CASE("dict.serialize: a document past the payload bound is refused whole (#778)") {
    // The bound is what the patcher's value queue carries
    // (DOCUMENT_CAPACITY, kValueListCap - 1): a longer send could reach a
    // direct neighbour but silently vanish crossing a .s/.r, and a truncated
    // JSON document parses as a different dictionary — so nothing is sent at
    // all, and the loss is counted.
    Rig rig("ds778e", "d778e");
    const std::string longValue(40, 'a');
    for (int i = 0; i < 10; i++) {
      rig.Store("set key" + std::to_string(i) + " " + longValue);
    }
    REQUIRE(rig.dict.Count() == 10);

    const std::uint64_t before = rig.ser.Dropped();
    rig.Bang();
    CHECK(rig.out.count == 0);
    CHECK(rig.ser.Emitted() == 0);
    CHECK(rig.ser.Dropped() == before + 1);

    // And the bound is the document's, not the dictionary's: trimmed back
    // under it, the same object serialises again.
    rig.dict.GetInlet(0)->SetList("clear", YSE::T_GUI);
    rig.Store("set key0 " + longValue);
    rig.Bang();
    CHECK(rig.out.count == 1);
    CHECK(rig.ser.Emitted() == 1);
  }

  TEST_CASE("dict.serialize: a trigger looping back from the outlet is refused (#778)") {
    // The document's own subgraph re-entering the inlet — directly or round
    // a chain — would rewrite the snapshot and the compose buffer under the
    // send, so the guard held across the whole serialisation drops it,
    // counted, exactly as .uzi refuses a re-entrant start.
    Rig rig("ds778f", "d778f");
    rig.Store("set lead piano");

    int reentered = 0;
    rig.out.onList = [&rig, &reentered]() {
      if (reentered++ > 0) return; // once is the proof; don't recurse forever
      rig.ser.GetInlet(0)->SetBang(YSE::T_GUI);
    };

    const std::uint64_t before = rig.ser.Dropped();
    rig.Bang();
    CHECK(rig.out.count == 1);
    CHECK(rig.ser.Emitted() == 1);
    CHECK(rig.ser.Dropped() == before + 1);
  }

  // ─── the file route (issue #840) ────────────────────────────────────────────

  TEST_CASE("dict.serialize: write puts the document in a real file (#840)") {
    // The write route end to end, through a real file on disk: the same
    // compact document the outlet would carry, handed to the fileScheduler
    // instead — the message is a snapshot, a compose and a claim on a
    // patcher-owned slot, and the disk work runs on the background pool.
    Rig rig("ds840a", "d840a");
    rig.Store("set voice::1::freq 440");
    rig.Store("set chord 0 4 7");

    const std::string path = TempFile("yse_dict_write_840.json");
    rig.ser.GetInlet(0)->SetList("write " + path, YSE::T_GUI);
    REQUIRE(rig.p.FileIO() != nullptr);
    rig.p.FileIO()->WaitIdle();

    CHECK(ReadWholeFile(path) == "{\"voice\":{\"1\":{\"freq\":440}},\"chord\":[0,4,7]}");
    // The file route sends nothing: Max has no outlet for a finished write
    // and neither does this. Emitted counts outlet documents only.
    CHECK(rig.out.count == 0);
    CHECK(rig.ser.Emitted() == 0);
    CHECK(rig.ser.Dropped() == 0);

    // A bare `write` reuses the last name given — and the document is the
    // dictionary as it stands now, not as it stood then.
    rig.Store("set gain 0.5");
    rig.ser.GetInlet(0)->SetList("write", YSE::T_GUI);
    rig.p.FileIO()->WaitIdle();
    CHECK(ReadWholeFile(path) ==
          "{\"voice\":{\"1\":{\"freq\":440}},\"chord\":[0,4,7],\"gain\":0.5}");
    CHECK(rig.ser.Dropped() == 0);

    Remove(path);
  }

  TEST_CASE("dict.serialize: write carries a document past the payload bound whole (#840)") {
    // The reason the route exists: the same dictionary whose document the
    // outlet refuses — past what a list payload carries — leaves whole
    // through a fileScheduler slot, parsing back to every entry.
    Rig rig("ds840b", "d840b");
    const std::string longValue(40, 'a');
    for (int i = 0; i < 10; i++) {
      rig.Store("set key" + std::to_string(i) + " " + longValue);
    }

    const std::uint64_t before = rig.ser.Dropped();
    rig.Bang();
    CHECK(rig.out.count == 0);
    CHECK(rig.ser.Dropped() == before + 1);

    const std::string path = TempFile("yse_dict_write_long_840.json");
    rig.ser.GetInlet(0)->SetList("write " + path, YSE::T_GUI);
    REQUIRE(rig.p.FileIO() != nullptr);
    rig.p.FileIO()->WaitIdle();

    const std::string written = ReadWholeFile(path);
    CHECK(written.size() > gDictSerialize::DOCUMENT_CAPACITY);
    const nlohmann::json parsed = nlohmann::json::parse(written, nullptr, false);
    REQUIRE_FALSE(parsed.is_discarded());
    REQUIRE(parsed.is_object());
    CHECK(parsed.size() == 10);
    CHECK(parsed["key0"] == longValue);
    CHECK(parsed["key9"] == longValue);
    CHECK(rig.ser.Dropped() == before + 1);

    Remove(path);
  }

  TEST_CASE("dict.serialize: a document past the scheduler slot is refused whole (#840)") {
    // Refusal-not-truncation at the slot bound too: a dictionary whose
    // document composes past fileScheduler::BYTES_CAPACITY — every entry at
    // its widest, escape-heavy values doubling on the way out — is refused
    // whole and counted, and no file appears. Half a document is a different
    // dictionary or none.
    Rig rig("ds840c", "d840c");

    // 256 entries, each spelling ~600 document characters: a ~100-character
    // key and a value of 250 quotes, each escaping to two characters.
    const std::string value(250, '"');
    const std::string pad(96, 'x');
    std::string message;
    for (int i = 0; i < 256; i++) {
      message = "set k";
      message += std::to_string(1000 + i);
      message += pad;
      message += ' ';
      message += value;
      rig.Store(message);
    }
    REQUIRE(rig.dict.Count() == 256);

    const std::string path = TempFile("yse_dict_write_over_840.json");
    const std::uint64_t before = rig.ser.Dropped();
    rig.ser.GetInlet(0)->SetList("write " + path, YSE::T_GUI);
    REQUIRE(rig.p.FileIO() != nullptr);
    rig.p.FileIO()->WaitIdle();

    CHECK(rig.ser.Dropped() == before + 1);
    CHECK(ReadWholeFile(path).empty());

    Remove(path);
  }

  TEST_CASE("dict.serialize: a write with no plumbing or no path is refused, counted (#840)") {
    {
      // A standalone object has no patcher and so no file plumbing.
      gDictSerialize g;
      g.GetInlet(0)->SetList("write somewhere.json", YSE::T_GUI);
      CHECK(g.Dropped() == 1);
    }
    {
      // Parented, but a bare `write` with no path ever given: there is no
      // dialog to ask with, and nothing remembered to fall back on.
      Rig rig("ds840d", "d840d");
      rig.ser.GetInlet(0)->SetList("write", YSE::T_GUI);
      CHECK(rig.ser.Dropped() == 1);
      REQUIRE(rig.p.FileIO() != nullptr);
      CHECK(rig.p.FileIO()->PendingCount() == 0);
    }
  }

  TEST_CASE("dict.serialize: the write request path does not allocate (#840)") {
    // The claim the file route inherits from the outlet route: a `write` may
    // arrive on the audio callback, so the whole message path — remembering
    // the path, the snapshot, the compose, the copy into the scheduler's
    // slot — reuses storage that already exists. The probe is thread-scoped
    // (#701), so the disk work on the background pool is not what it
    // measures.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    Rig rig("ds840e", "probeD840");
    rig.Store("set voice::1::freq 440");
    rig.Store("set chord 0 4 7");
    rig.Store("set title he\"llo");

    const std::string path = TempFile("yse_dict_write_probe_840.json");
    const std::string message = "write " + path;
    // Warm the path memory and the scheduler machinery.
    rig.ser.GetInlet(0)->SetList(message, YSE::T_GUI);
    REQUIRE(rig.p.FileIO() != nullptr);
    rig.p.FileIO()->WaitIdle();

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      rig.ser.GetInlet(0)->SetList(message, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);
    CHECK(rig.ser.Dropped() == 0);

    rig.p.FileIO()->WaitIdle();
    CHECK_FALSE(ReadWholeFile(path).empty());

    Remove(path);
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE(
      "dict.serialize: the address form is the patcher's, and RefreshBinding follows it (#778)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ds778g_before");

    gDictSerialize g;
    g.SetParams("d778g");
    g.SetParent(&p);
    CHECK(g.DictName() == "d778g");
    CHECK(g.Address() == "ds778g_before.d778g");

    // Idempotent: a rebind to the address it already has keeps the store.
    g.RefreshBinding();
    CHECK(g.Address() == "ds778g_before.d778g");

    p.SetName("ds778g_after");
    g.RefreshBinding();
    CHECK(g.Address() == "ds778g_after.d778g");
  }

  TEST_CASE("dict.serialize: patcherImplementation::SetName re-anchors it (#778)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by the
    // patcher, without anybody calling RefreshBinding by hand. The keeper
    // holds the old-address store; after the rename the document reads a
    // fresh empty dictionary under the new prefix.
    DocSink out;
    YSE::pHandle outHandle(&out);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ds778h_before");

    gDict keeper;
    keeper.SetParent(&p);
    keeper.SetParams("d778h");
    keeper.GetInlet(0)->SetList("set lead piano", YSE::T_GUI);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT_SERIALIZE, "d778h");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &outHandle, 0);

    h->SetBang(0);
    REQUIRE(out.count == 1);
    CHECK(out.received == "{\"lead\":\"piano\"}");

    p.SetName("ds778h_after");
    h->SetBang(0);
    REQUIRE(out.count == 2);
    CHECK(out.received == "{}");
  }

  TEST_CASE("dict.serialize: wired from the dict's reference outlet, banging the dict serialises "
            "(#778)") {
    // The flow a patch actually wires: the .dict's reference outlet into the
    // serialiser, a bang on the .dict, and the document at the far end.
    // Max's own gesture — bang the dict, out comes its text.
    Rig rig("ds778i", "d778i");
    Wire(rig.dict, 1, rig.ser);
    rig.Store("set lead piano");

    rig.dict.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(rig.out.count == 1);
    CHECK(rig.out.received == "{\"lead\":\"piano\"}");
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("dict.serialize: a document asked for over in-patcher delivery lands on T_DSP (#778)") {
    // A .r feeding the serialiser dispatches on T_DSP when the block drains
    // it (issue #225) — "the audio thread asks for the document" is the
    // ordinary case, and the whole path is a snapshot, bounded composes and
    // one send.
    DocSink out;
    YSE::pHandle outHandle(&out);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ds778j");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go778j");
    YSE::pHandle* dict = p.CreateObject(YSE::OBJ::G_DICT, "d778j");
    YSE::pHandle* ser = p.CreateObject(YSE::OBJ::G_DICT_SERIALIZE, "d778j");
    REQUIRE(recv != nullptr);
    REQUIRE(dict != nullptr);
    REQUIRE(ser != nullptr);
    p.Connect(recv, 0, ser, 0);
    p.Connect(ser, 0, &outHandle, 0);

    dict->SetListData(0, "set lead piano");

    p.PassData(std::string("dictionary d778j"), "go778j", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);

    REQUIRE(out.count == 1);
    CHECK(out.received == "{\"lead\":\"piano\"}");
  }

  TEST_CASE("dict.serialize: no message path allocates (#778)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path: the serialisation (bang and reference alike), the
    // wrong-name refusal, the unknown message, and the over-long refusal.
    // The document composes nesting, arrays, strings, floats and an escape,
    // so the whole emitter runs inside the scope.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "dictionary probeD778";
    const std::string wrongName = "dictionary somewhere_else_long";
    const std::string unknown = "frobnicate something quite long indeed";

    Rig rig("ds778k", "probeD778");
    rig.Store("set voice::1::freq a value past every small-string buffer");
    rig.Store("set chord 0 4 7 12");
    rig.Store("set gain 0.5");
    rig.Store("set title he\"llo");

    Rig full("ds778l", "probeD778long");
    const std::string longValue(40, 'a');
    for (int i = 0; i < 10; i++) {
      full.Store("set key" + std::to_string(i) + " " + longValue);
    }

    // Warm every path — including the sink's list assignment — so
    // first-call machinery is not what the probe catches.
    rig.Bang();
    rig.ser.GetInlet(0)->SetList(reference, YSE::T_GUI);
    rig.ser.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    rig.ser.GetInlet(0)->SetList(unknown, YSE::T_GUI);
    full.Bang();
    const std::uint64_t sent = rig.ser.Emitted();
    const std::uint64_t before = rig.ser.Dropped();
    const std::uint64_t refused = full.ser.Dropped();

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      rig.Bang(YSE::T_DSP);
      rig.ser.GetInlet(0)->SetList(reference, YSE::T_DSP);
      rig.ser.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      rig.ser.GetInlet(0)->SetList(unknown, YSE::T_DSP);
      full.Bang(YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. Two documents were sent, and all three
    // refusals were counted.
    CHECK(rig.ser.Emitted() == sent + 2);
    CHECK(rig.ser.Dropped() == before + 2);
    CHECK(full.ser.Dropped() == refused + 1);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("dict.serialize: params survive a DumpJSON / ParseJSON round trip (#778)") {
    // The creation argument has to come back: a reloaded patch whose
    // .dict.serialize lost its name would serialise a different dictionary.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_DICT_SERIALIZE, "cfg778");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".dict.serialize"));
    CHECK(copy->GetParams() == std::string("cfg778"));
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 1);
  }

  TEST_CASE("dict.serialize: carries complete documentation metadata (#778)") {
    gDictSerialize g;
    CHECK_FALSE(g.GetDescription().empty());
    CHECK(g.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = g.GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == "name");
  }
}
