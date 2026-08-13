// Tests for .dict (issue #550) — a nested key/value dictionary shared by name,
// and the patcher's answer to the reference-passed value question the array
// (#548), string (#549) and dict epics all asked.
//
// What the design gate has to prove, and what every case below is one of:
//
//   - **a dictionary is addressed by name, never passed down a cord.** An
//     `OUT_TYPE` is BANG / FLOAT / INT / BUFFER / LIST / ANY and a send is a
//     direct call carrying a value; there is no identity for a cord to hold. So
//     a bang emits the message `dictionary <name>` and the receiving object
//     resolves that name itself, on the control thread — which is what
//     `DictReferenceNames` exists for and what the dict.* family will bind
//     with.
//   - **storage lives in the named registry, held weakly.** Every `.dict` of a
//     name shares one dictionary; the dictionary lives exactly as long as some
//     object addresses it and no longer. Ownership is "whoever names it", not
//     an owner object and not a manual free.
//   - **an unnamed .dict is private.** `"<patcherName>."` is a real address, so
//     pooling there would silently join every unconfigured `.dict` in the
//     patcher. `.value`'s rule, for `.value`'s reason.
//   - **nesting lives in the key and comes back on the way out.** `a::b::c` is
//     one flat entry three levels deep, and the JSON the patch saves is a proper
//     nested object rather than a table of dotted strings.
//   - **the contents cross the control/audio boundary and nothing allocates.**
//     In-patcher delivery dispatches on T_DSP, so "the audio thread stores into
//     a dictionary" is the ordinary case. It has to land, and it has to land
//     without a single heap allocation.
//   - **refusal, never truncation.** An over-long path, an over-long value or a
//     store into a full dictionary changes nothing and is counted rather than
//     logged, since the refusing thread may be the audio callback.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names a dictionary uses a name of its own — one case's
// contents must not be visible to the next.

#include <doctest/doctest.h>
#include <string>
#include <vector>

#include "patcher/genericObjects/gDict.h"
#include "patcher/inlet.h"
#include "patcher/namedStore.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"
#include "utils/json.hpp"

using TestHelpers::MultiSink;
using TestHelpers::Wire;
using YSE::PATCHER::dictStore;
using YSE::PATCHER::gDict;

namespace {

  // A standalone .dict with a sink on each of its three outlets. The sinks are
  // declared before the object so the object is torn down first, while the
  // inlets it is wired to still exist (see sinks.hpp on why that matters).
  struct Rig {
    MultiSink data;
    MultiSink reference;
    MultiSink miss;
    gDict obj;

    explicit Rig(const std::string& args = std::string()) {
      if (!args.empty()) obj.SetParams(args);
      Wire(obj, 0, data);
      Wire(obj, 1, reference);
      Wire(obj, 2, miss);
    }

    void List(const std::string& message, YSE::THREAD thread = YSE::T_GUI) {
      obj.GetInlet(0)->SetList(message, thread);
    }
    void Bang(YSE::THREAD thread = YSE::T_GUI) {
      obj.GetInlet(0)->SetBang(thread);
    }
    void Reset() {
      data.reset();
      reference.reset();
      miss.reset();
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("dict: registered, one inlet, three outlets (#550)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".dict");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 3);
  }

  TEST_CASE("dict: appears in the registry's name list (#550)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_DICT)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("dict: the inlet takes bang and list (#550)") {
    // No int or float handler: a bare number names neither a command nor a key,
    // and accepting one would have to invent a meaning for it.
    gDict d;
    const unsigned int types = d.GetInlet(0)->GetAcceptedTypes();
    CHECK((types & YSE::PATCHER::IT_BANG) != 0);
    CHECK((types & YSE::PATCHER::IT_LIST) != 0);
  }

  // ─── storing and fetching ───────────────────────────────────────────────────

  TEST_CASE("dict: set stores silently and get fetches (#550)") {
    Rig rig;
    rig.List("set tempo 120");
    // Storing emits nothing — a dictionary is a register, not a send. An
    // implementation that echoed on store would make every patch that writes
    // and reads one name in a graph loop.
    CHECK_FALSE(rig.data.gotInt);
    CHECK_FALSE(rig.data.gotList);
    CHECK(rig.obj.Count() == 1);

    rig.List("get tempo");
    CHECK(rig.data.gotInt);
    CHECK(rig.data.intValue == 120);
    CHECK_FALSE(rig.miss.gotBang);
  }

  TEST_CASE("dict: a fetched value is typed the way the patcher spells it (#550)") {
    // The property the object exists for and one no accessor can show: a
    // dictionary is only usable if what comes out of it can be added to. An
    // implementation that always sent list text would silently do nothing at
    // the .+ downstream.
    Rig rig;
    rig.List("set count 7");
    rig.List("set gain 0.5");
    rig.List("set note c4");
    rig.List("set chord 0 4 7");

    rig.List("get count");
    CHECK(rig.data.gotInt);
    CHECK(rig.data.intValue == 7);

    rig.Reset();
    rig.List("get gain");
    CHECK(rig.data.gotFloat);
    CHECK(rig.data.floatValue == doctest::Approx(0.5f));

    rig.Reset();
    rig.List("get note");
    CHECK(rig.data.gotList);
    CHECK(rig.data.listValue == "c4");

    rig.Reset();
    rig.List("get chord");
    CHECK(rig.data.gotList);
    CHECK(rig.data.listValue == "0 4 7");
  }

  TEST_CASE("dict: a get on a path the dictionary does not hold bangs the miss outlet (#550)") {
    // Kept off outlet 0 so a patch can tell "no such key" from a key holding
    // nothing — which is why the empty-value case below bangs neither.
    Rig rig;
    rig.List("set present 1");

    rig.List("get absent");
    CHECK(rig.miss.gotBang);
    CHECK_FALSE(rig.data.gotInt);
    CHECK_FALSE(rig.data.gotList);

    rig.Reset();
    rig.List("set empty");
    rig.List("get empty");
    CHECK_FALSE(rig.miss.gotBang);
    CHECK_FALSE(rig.data.gotList);
    CHECK(rig.obj.Lookup("empty") == "");
  }

  TEST_CASE("dict: set replaces in place, delete closes the gap, clear empties (#550)") {
    Rig rig;
    rig.List("set a 1");
    rig.List("set b 2");
    rig.List("set c 3");
    CHECK(rig.obj.Count() == 3);

    // Replacing keeps the entry where it is, so storage order stays the order
    // paths were first written — which is what getkeys and a JSON dump report.
    rig.List("set a 99");
    CHECK(rig.obj.Count() == 3);
    CHECK(rig.obj.KeyAt(0) == "a");
    CHECK(rig.obj.Lookup("a") == "99");

    rig.List("delete b");
    CHECK(rig.obj.Count() == 2);
    CHECK(rig.obj.KeyAt(0) == "a");
    CHECK(rig.obj.KeyAt(1) == "c");
    CHECK(rig.obj.Lookup("b") == "");

    rig.List("clear");
    CHECK(rig.obj.Count() == 0);
    rig.List("get a");
    CHECK(rig.miss.gotBang);
  }

  TEST_CASE("dict: getsize and getkeys report structure, not storage (#550)") {
    Rig rig;
    rig.List("set voice::1::freq 440");
    rig.List("set voice::2::freq 550");
    rig.List("set tempo 120");

    rig.List("getsize");
    CHECK(rig.data.gotInt);
    CHECK(rig.data.intValue == 3);

    rig.Reset();
    rig.List("getkeys");
    CHECK(rig.data.gotList);
    // "voice" once, not twice: two entries under one top-level key are one key.
    // A getkeys that said "voice voice tempo" would be reporting the table.
    CHECK(rig.data.listValue == "voice tempo");
  }

  TEST_CASE("dict: an unknown message is refused and counted, not logged (#550)") {
    Rig rig;
    const std::uint64_t before = rig.obj.Dropped();
    rig.List("frobnicate a b");
    rig.List("set");
    rig.List("get");
    CHECK(rig.obj.Dropped() == before + 3);
    CHECK(rig.obj.Count() == 0);
  }

  // ─── the reference, and the addressing model ────────────────────────────────

  TEST_CASE("dict: a bang emits \"dictionary <name>\" and nothing else (#550)") {
    // The whole of what travels down a cord. Not the contents — a bang that
    // dumped would make a bang mean two things and hand a patch a dump it never
    // asked for.
    Rig rig("presets");
    rig.List("set a 1");
    rig.Bang();
    CHECK(rig.reference.gotList);
    CHECK(rig.reference.listValue == "dictionary presets");
    CHECK_FALSE(rig.data.gotList);
    CHECK_FALSE(rig.data.gotInt);
    CHECK(rig.obj.Reference() == "dictionary presets");
  }

  TEST_CASE("dict: an unnamed dictionary has no reference to pass on (#550)") {
    Rig rig;
    rig.Bang();
    CHECK_FALSE(rig.reference.gotList);
    CHECK(rig.obj.Reference().empty());
  }

  TEST_CASE("dict: DictReferenceNames matches exactly and nothing else (#550)") {
    // The bounded compare a dict.* object performs when a reference arrives on
    // an inlet — the whole of what it may do with a name on a cord, because
    // resolving an unrecognised one means the registry's mutex.
    using YSE::PATCHER::DictReferenceNames;
    const std::string name = "presets";
    CHECK(DictReferenceNames(std::string("dictionary presets"), name));
    CHECK_FALSE(DictReferenceNames(std::string("dictionary presets2"), name));
    CHECK_FALSE(DictReferenceNames(std::string("dictionary preset"), name));
    CHECK_FALSE(DictReferenceNames(std::string("dictionary  presets"), name));
    CHECK_FALSE(DictReferenceNames(std::string("dict presets"), name));
    CHECK_FALSE(DictReferenceNames(std::string("presets"), name));
    // An unnamed dictionary is not addressable, so nothing names it.
    CHECK_FALSE(DictReferenceNames(std::string("dictionary "), std::string()));
  }

  // ─── sharing by name ────────────────────────────────────────────────────────

  TEST_CASE("dict: two .dict of one name share one dictionary (#550)") {
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* writer = p.CreateObject(YSE::OBJ::G_DICT, "kit550a");
    YSE::pHandle* reader = p.CreateObject(YSE::OBJ::G_DICT, "kit550a");
    REQUIRE(writer != nullptr);
    REQUIRE(reader != nullptr);
    p.Connect(reader, 0, &sinkHandle, 0);

    writer->SetListData(0, "set snare 38");
    reader->SetListData(0, "get snare");
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 38);
  }

  TEST_CASE("dict: an unnamed .dict keeps a dictionary of its own (#550)") {
    // Not "shares the empty name": "<patcherName>." is a real, reachable
    // address, so two unnamed objects pooling there would be connected in a way
    // a patch author never wired.
    Rig a;
    Rig b;
    a.List("set x 1");
    CHECK(a.obj.Count() == 1);
    CHECK(b.obj.Count() == 0);
    CHECK(a.obj.Address().empty());
    CHECK_FALSE(a.obj.IsShared());
  }

  TEST_CASE("dict: the dictionary outlives no .dict that names it (#550)") {
    // The registry holds its stores weakly: a name lives exactly as long as
    // some object addresses it. Strong ownership would make every name a patch
    // ever spelled immortal for the life of the process, and would leave one
    // test's contents visible to the next — which is the failure this asserts
    // is absent. A delta rather than an absolute, since the registry is
    // process-wide.
    const std::size_t before = YSE::PATCHER::NamedStoreCount<dictStore>();
    {
      YSE::patcher p;
      p.create(2);
      p.name("dict550b");
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT, "notes");
      REQUIRE(h != nullptr);
      h->SetListData(0, "set a 1");
      CHECK(YSE::PATCHER::NamedStoreCount<dictStore>() == before + 1);
    }
    CHECK(YSE::PATCHER::NamedStoreCount<dictStore>() == before);

    // And a fresh patcher of the same name sees an empty dictionary rather than
    // the previous one's contents.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher again;
    again.create(2);
    again.name("dict550b");
    YSE::pHandle* h = again.CreateObject(YSE::OBJ::G_DICT, "notes");
    REQUIRE(h != nullptr);
    again.Connect(h, 2, &sinkHandle, 0);
    h->SetListData(0, "get a");
    CHECK(sink.gotBang);
  }

  TEST_CASE("dict: the address form is the patcher's, and RefreshBinding follows it (#550)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dict550c_before");

    gDict obj;
    obj.SetParams("notes550c");
    obj.SetParent(&p);
    CHECK(obj.Address() == "dict550c_before.notes550c");

    // Idempotent: a rebind to the address it already has keeps the dictionary,
    // and with it everything in it.
    obj.GetInlet(0)->SetList("set triad 0 4 7", YSE::T_GUI);
    obj.RefreshBinding();
    CHECK(obj.Address() == "dict550c_before.notes550c");
    CHECK(obj.Count() == 1);

    // The address prefix moved, so the object now addresses a different
    // dictionary — an empty one, exactly as .value and .coll do.
    p.SetName("dict550c_after");
    obj.RefreshBinding();
    CHECK(obj.Address() == "dict550c_after.notes550c");
    CHECK(obj.Count() == 0);
  }

  TEST_CASE("dict: patcherImplementation::SetName re-anchors the dictionaries it holds (#550)") {
    // The rename hook itself, which the standalone case above cannot reach: an
    // object created *inside* a patcher must be re-anchored by the patcher,
    // without anybody calling RefreshBinding by hand. Asserted through the miss
    // outlet, since the object pointer is not reachable from a pHandle.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dict550i_before");

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_DICT, "notes550i");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &sinkHandle, 0);
    h->SetListData(0, "set a 1");
    h->SetListData(0, "get a");
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 1);

    MultiSink missSink;
    YSE::pHandle missHandle(&missSink);
    p.Connect(h, 2, &missHandle, 0);

    p.SetName("dict550i_after");
    h->SetListData(0, "get a");
    CHECK(missSink.gotBang);
  }

  // ─── bounds ─────────────────────────────────────────────────────────────────

  TEST_CASE("dict: an over-long path or value is refused, not truncated (#550)") {
    Rig rig;
    const std::string longestKey(gDict::KEY_CAPACITY, 'k');
    const std::string tooLongKey(gDict::KEY_CAPACITY + 1, 'k');
    const std::string longestValue(gDict::VALUE_CAPACITY, 'v');
    const std::string tooLongValue(gDict::VALUE_CAPACITY + 1, 'v');

    rig.List("set " + longestKey + " ok");
    CHECK(rig.obj.Count() == 1);
    rig.List("set " + tooLongKey + " ok");
    CHECK(rig.obj.Count() == 1);
    CHECK(rig.obj.Lookup(tooLongKey) == "");

    rig.List("set wide " + longestValue);
    CHECK(rig.obj.Lookup("wide") == longestValue);
    // Half a list is a different list: the stored value a patch is reading must
    // not silently become one.
    rig.List("set wide " + tooLongValue);
    CHECK(rig.obj.Lookup("wide") == longestValue);
  }

  TEST_CASE("dict: a store into a full dictionary is refused and counted (#550)") {
    Rig rig;
    for (std::size_t i = 0; i < gDict::MAX_ENTRIES; i++) {
      rig.List("set k" + std::to_string(i) + " " + std::to_string(i));
    }
    CHECK(rig.obj.Count() == gDict::MAX_ENTRIES);

    const std::uint64_t before = rig.obj.Dropped();
    rig.List("set overflow 1");
    CHECK(rig.obj.Count() == gDict::MAX_ENTRIES);
    CHECK(rig.obj.Dropped() == before + 1);

    // A replacement still works when the table is full: it writes into an entry
    // that already exists rather than needing a new one.
    rig.List("set k0 replaced");
    CHECK(rig.obj.Lookup("k0") == "replaced");
  }

  // ─── nesting, and the JSON it becomes ───────────────────────────────────────

  TEST_CASE("dict: paths expand into nested JSON and flatten back (#550)") {
    // The half of the design that makes a flat table a nested document again,
    // and the reason .dict was worth doing before the array and string types:
    // the patcher already had the JSON layer to build it on.
    dictStore store;
    CHECK(YSE::PATCHER::DictStoreAt(store, "voice::1::freq", 14, "440", 3));
    CHECK(YSE::PATCHER::DictStoreAt(store, "voice::1::gain", 14, "0.5", 3));
    CHECK(YSE::PATCHER::DictStoreAt(store, "voice::2::freq", 14, "550", 3));
    CHECK(YSE::PATCHER::DictStoreAt(store, "name", 4, "lead", 4));
    CHECK(YSE::PATCHER::DictStoreAt(store, "chord", 5, "0 4 7", 5));

    nlohmann::json json;
    YSE::PATCHER::DictToJson(store, json);

    REQUIRE(json.is_object());
    REQUIRE(json["voice"].is_object());
    REQUIRE(json["voice"]["1"].is_object());
    // Typed, not stringly: a number in the document is a JSON number, so a
    // dictionary saved here is a document another tool can read.
    CHECK(json["voice"]["1"]["freq"].is_number_integer());
    CHECK(json["voice"]["1"]["freq"].get<int>() == 440);
    CHECK(json["voice"]["1"]["gain"].is_number_float());
    CHECK(json["voice"]["1"]["gain"].get<double>() == doctest::Approx(0.5));
    CHECK(json["voice"]["2"]["freq"].get<int>() == 550);
    CHECK(json["name"].is_string());
    CHECK(json["name"].get<std::string>() == "lead");
    REQUIRE(json["chord"].is_array());
    CHECK(json["chord"].size() == 3);
    CHECK(json["chord"][1].get<int>() == 4);

    dictStore back;
    YSE::PATCHER::DictFromJson(json, back);
    CHECK(back.count == store.count);
    const std::size_t at = YSE::PATCHER::DictFind(back, "voice::1::gain", 14);
    REQUIRE(at < back.count);
    CHECK(back.entries[at].value == "0.5");
    const std::size_t chord = YSE::PATCHER::DictFind(back, "chord", 5);
    REQUIRE(chord < back.count);
    CHECK(back.entries[chord].value == "0 4 7");
  }

  TEST_CASE("dict: contents survive a DumpJSON / ParseJSON round trip (#550)") {
    // Read back through the outlet rather than through an accessor: what has to
    // survive is the dictionary a patch can use, not a member variable.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher src;
    src.create(2);
    src.name("dict550d");
    YSE::pHandle* d = src.CreateObject(YSE::OBJ::G_DICT, "kit");
    REQUIRE(d != nullptr);
    d->SetListData(0, "set voice::1::freq 440");
    d->SetListData(0, "set name lead");
    d->SetListData(0, "set chord 0 4 7");

    const std::string json = src.DumpJSON();
    CHECK(json.find(".dict") != std::string::npos);
    // Nested in the saved form, not a table of dotted keys — the acceptance
    // criterion the epic states.
    CHECK(json.find("\"voice\"") != std::string::npos);
    CHECK(json.find("voice::1::freq") == std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.name("dict550e");
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".dict");
    CHECK(copy->GetParams() == std::string("kit"));
    loaded.Connect(copy, 0, &sinkHandle, 0);

    copy->SetListData(0, "get voice::1::freq");
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 440);

    sink.reset();
    copy->SetListData(0, "get name");
    CHECK(sink.gotList);
    CHECK(sink.listValue == "lead");

    sink.reset();
    copy->SetListData(0, "get chord");
    CHECK(sink.gotList);
    CHECK(sink.listValue == "0 4 7");
  }

  TEST_CASE("dict: an empty dictionary writes no state key (#550)") {
    // The state hook is opt-in: an object without state of its own has to
    // serialise byte for byte the way it always did, or every saved patch grows
    // a null for every object in it.
    YSE::patcher p;
    p.create(2);
    REQUIRE(p.CreateObject(YSE::OBJ::G_DICT) != nullptr);
    CHECK(p.DumpJSON().find("\"state\"") == std::string::npos);
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("dict: a store arriving on the audio thread lands (#550)") {
    // The in-patcher delivery path defers to the audio thread and is drained by
    // an explicit Calculate (issue #225), so a .r feeding a .dict stores on
    // T_DSP. That is the ordinary case, not an exotic one — and it is the whole
    // of what "the value type crosses the control/audio boundary" means here:
    // the name was resolved on the control thread, so the store on the audio
    // thread is one pointer hop and one exchange.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dict550f");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "cmd");
    YSE::pHandle* store = p.CreateObject(YSE::OBJ::G_DICT, "state");
    YSE::pHandle* poll = p.CreateObject(YSE::OBJ::G_DICT, "state");
    REQUIRE(recv != nullptr);
    REQUIRE(store != nullptr);
    REQUIRE(poll != nullptr);
    p.Connect(recv, 0, store, 0);
    p.Connect(poll, 0, &sinkHandle, 0);

    p.PassData(std::string("set voice::1::freq 440"), "cmd", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);

    poll->SetListData(0, "get voice::1::freq");
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 440);
  }

  TEST_CASE("dict: a fetch driven on the audio thread emits on the audio thread (#550)") {
    Rig rig;
    const std::string set = "set held::on::dsp a value that is comfortably long";
    const std::string get = "get held::on::dsp";
    rig.List(set, YSE::T_DSP);
    rig.List(get, YSE::T_DSP);
    CHECK(rig.data.gotList);
    CHECK(rig.data.listValue == "a value that is comfortably long");
  }

  TEST_CASE("dict: no message path allocates (#550)") {
    // The claim the design gate rests on. The counter is read inside the scope
    // and asserted outside it, since doctest's own machinery allocates on first
    // use.
    //
    // **Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it.** `inlet::SetList` takes a
    // `const std::string&`, so a literal at the call site materialises a
    // temporary — a heap allocation whenever the text is longer than the
    // implementation's small-string buffer, and that buffer is not the same
    // width everywhere: 15 characters on libstdc++, 22 on libc++. A 16-to-22
    // character literal therefore passes locally and fails CI.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string setNested = "set voice::1::freq 440";
    const std::string setSymbol = "set voice::1::name a name past every small-string buffer";
    const std::string setList = "set chord 0 4 7 12 16 19 24";
    const std::string replace = "set voice::1::freq 550";
    const std::string getHit = "get voice::1::name";
    const std::string getNumber = "get voice::1::freq";
    const std::string getMiss = "get voice::9::freq";
    const std::string keys = "getkeys";
    const std::string size = "getsize";
    const std::string remove = "delete chord";
    const std::string unknown = "frobnicate something quite long indeed";
    const std::string tooLong = "set wide " + std::string(gDict::VALUE_CAPACITY + 1, 'z');

    Rig rig("probe550");

    // Warm every path, so the sinks' own buffers and any first-call machinery
    // are not what the probe catches.
    rig.List(setNested);
    rig.List(setSymbol);
    rig.List(setList);
    rig.List(getHit);
    rig.List(getNumber);
    rig.List(getMiss);
    rig.List(keys);
    rig.List(size);
    rig.Bang();
    REQUIRE(rig.data.gotList);
    REQUIRE(rig.reference.gotList);
    REQUIRE(rig.miss.gotBang);

    // Size the sinks' buffers independently of the object, so a later send that
    // happens to be longer than every warm-up send cannot be the allocation.
    const std::string sinkWarm(gDict::VALUE_CAPACITY, 'x');
    rig.data.GetInlet(0)->SetList(sinkWarm, YSE::T_GUI);
    rig.reference.GetInlet(0)->SetList(sinkWarm, YSE::T_GUI);

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      rig.List(setNested, YSE::T_DSP);
      rig.List(setSymbol, YSE::T_DSP);
      rig.List(setList, YSE::T_DSP);
      rig.List(replace, YSE::T_DSP);
      rig.List(getHit, YSE::T_DSP);
      rig.List(getNumber, YSE::T_DSP);
      rig.List(getMiss, YSE::T_DSP);
      rig.List(keys, YSE::T_DSP);
      rig.List(size, YSE::T_DSP);
      rig.List(remove, YSE::T_DSP);
      rig.List(unknown, YSE::T_DSP);
      rig.List(tooLong, YSE::T_DSP);
      rig.Bang(YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing.
    CHECK(rig.obj.Lookup("voice::1::freq") == "550");
    CHECK(rig.obj.Lookup("chord") == "");
    CHECK(rig.obj.Lookup("wide") == "");
    // "chord" is still there when getkeys runs — the delete comes after it.
    CHECK(rig.data.listValue == "voice chord");
    CHECK(rig.reference.listValue == "dictionary probe550");
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("dict: params survive a DumpJSON / ParseJSON round trip (#550)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_DICT, "kit550g");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".dict"));
    CHECK(copy->GetParams() == std::string("kit550g"));
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 3);
  }

  TEST_CASE("dict: a re-parse keeps the contents while the name is still held (#550)") {
    // A SetParams on a published object is a rebuild (#234): the replacement is
    // constructed while the original is still alive, so the dictionary is still
    // held and the running contents carry across the edit rather than snapping
    // back to empty under every object reading them. Modelled here with two
    // objects on one name, which is the same "somebody else still holds it"
    // situation and the one a unit test can build directly.
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("dict550h");

    gDict keeper;
    keeper.SetParent(&p);
    keeper.SetParams("kit");

    gDict d;
    d.SetParent(&p);
    d.SetParams("kit");
    CHECK(d.IsShared());
    d.GetInlet(0)->SetList("set a 1", YSE::T_GUI);
    CHECK(keeper.Count() == 1);
    d.SetParams("kit");
    CHECK(d.Count() == 1);

    // An empty argument string is a real reset back to a private dictionary,
    // which is what PARM_CLEAR is for: Parameters::Set() returns early on an
    // empty string, so without the clear hook the object would keep its old
    // name.
    d.SetParams("");
    CHECK(d.DictName().empty());
    CHECK_FALSE(d.IsShared());
    CHECK(d.Count() == 0);
    // And the shared dictionary is untouched: one object left the name, the
    // data stayed with the objects that are still on it.
    CHECK(keeper.Count() == 1);
  }

  TEST_CASE("dict: carries complete documentation metadata (#550)") {
    gDict d;
    CHECK_FALSE(d.GetDescription().empty());
    CHECK(d.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = d.GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == "name");
  }
}
