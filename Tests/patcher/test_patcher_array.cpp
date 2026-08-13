// Tests for .array (issue #548) — an ordered, index-addressed sequence shared by
// name, and the second type built on the value model .dict (#550) settled.
//
// What the design gate has to prove, and what every case below is one of:
//
//   - **an array is addressed by name, never passed down a cord.** An `OUT_TYPE`
//     is BANG / FLOAT / INT / BUFFER / LIST / ANY and a send is a direct call
//     carrying a value; there is no identity for a cord to hold. So a bang emits
//     the message `array <name>` and the receiving object resolves that name
//     itself, on the control thread — which is what `ArrayReferenceNames` exists
//     for and what the array.* family will bind with.
//   - **storage lives in the named registry, held weakly.** Every `.array` of a
//     name shares one sequence; it lives exactly as long as some object
//     addresses it and no longer. Ownership is "whoever names it", not an owner
//     object and not a manual free.
//   - **an unnamed .array is private.** `"<patcherName>."` is a real address, so
//     pooling there would silently join every unconfigured `.array` in the
//     patcher. `.value`'s rule, for `.value`'s reason.
//   - **an element is one atom, so the array and the list it spells are the same
//     thing seen twice.** `append 0 4 7` adds three elements and `getvalue`
//     sends `0 4 7` back. That is the difference from `.dict`, whose value is
//     list text, and the reason for it is that `["0", "4 7"]` and `["0 4", "7"]`
//     would otherwise be indistinguishable on a cord.
//   - **an index is a position; out of range is a miss and negative is refused.**
//     Never wrapped, never clamped — decided once here for forty-odd array.*
//     objects.
//   - **the contents cross the control/audio boundary and nothing allocates.**
//     In-patcher delivery dispatches on T_DSP, so "the audio thread pushes onto
//     an array" is the ordinary case. It has to land, and without a single heap
//     allocation.
//   - **refusal, never truncation.** An over-long element or a push onto a full
//     array changes nothing and is counted rather than logged, since the
//     refusing thread may be the audio callback.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses a name of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <string>
#include <vector>

#include "patcher/genericObjects/gArray.h"
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
using YSE::PATCHER::arrayStore;
using YSE::PATCHER::gArray;

namespace {

  // A standalone .array with a sink on each of its three outlets. The sinks are
  // declared before the object so the object is torn down first, while the
  // inlets it is wired to still exist (see sinks.hpp on why that matters).
  struct Rig {
    MultiSink data;
    MultiSink reference;
    MultiSink miss;
    gArray obj;

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

  TEST_CASE("array: registered, one inlet, three outlets (#548)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".array");
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 3);
  }

  TEST_CASE("array: appears in the registry's name list (#548)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ARRAY)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("array: the inlet takes bang and list (#548)") {
    // No int or float handler: a bare number names neither a command nor an
    // index, and accepting one would have to invent a meaning for it.
    gArray a;
    const unsigned int types = a.GetInlet(0)->GetAcceptedTypes();
    CHECK((types & YSE::PATCHER::IT_BANG) != 0);
    CHECK((types & YSE::PATCHER::IT_LIST) != 0);
  }

  // ─── the sequence ───────────────────────────────────────────────────────────

  TEST_CASE("array: one atom per element, and the list it spells is the same thing (#548)") {
    // The property that distinguishes this type from .dict, whose value is list
    // text. An `append 0 4 7` that stored one three-token element would make
    // `getvalue` lossy and `.array.length` disagree with the `.zl len` of the
    // list the array just emitted.
    Rig rig;
    rig.List("append 0 4 7");
    CHECK(rig.obj.Count() == 3);
    CHECK(rig.obj.ElementAt(0) == "0");
    CHECK(rig.obj.ElementAt(2) == "7");
    // Appending emits nothing — an array is a store, not a send. An
    // implementation that echoed on push would make every patch that writes and
    // reads one name in a graph loop.
    CHECK_FALSE(rig.data.gotList);

    rig.List("getvalue");
    CHECK(rig.data.gotList);
    CHECK(rig.data.listValue == "0 4 7");
  }

  TEST_CASE("array: a fetched element is typed the way the patcher spells it (#548)") {
    // The property the object exists for and one no accessor can show: an array
    // is only usable if what comes out of it can be added to. An implementation
    // that always sent list text would silently do nothing at the .+ downstream.
    Rig rig;
    rig.List("append 7 0.5 c4");

    rig.List("get 0");
    CHECK(rig.data.gotInt);
    CHECK(rig.data.intValue == 7);

    rig.Reset();
    rig.List("get 1");
    CHECK(rig.data.gotFloat);
    CHECK(rig.data.floatValue == doctest::Approx(0.5f));

    rig.Reset();
    rig.List("get 2");
    CHECK(rig.data.gotList);
    CHECK(rig.data.listValue == "c4");

    // And an array of one element sends that element rather than a list of one —
    // SendAtoms' rule, which is the same reason.
    rig.Reset();
    rig.List("clear");
    rig.List("append 42");
    rig.List("getvalue");
    CHECK(rig.data.gotInt);
    CHECK(rig.data.intValue == 42);
  }

  TEST_CASE("array: set replaces, insert shifts up, delete closes the gap (#548)") {
    Rig rig;
    rig.List("append a b c");

    rig.List("set 1 B");
    CHECK(rig.obj.Count() == 3);
    CHECK(rig.obj.ElementAt(1) == "B");

    // Inserted in the order they were written, not reversed.
    rig.List("insert 1 x y");
    CHECK(rig.obj.Count() == 5);
    CHECK(rig.obj.ElementAt(0) == "a");
    CHECK(rig.obj.ElementAt(1) == "x");
    CHECK(rig.obj.ElementAt(2) == "y");
    CHECK(rig.obj.ElementAt(3) == "B");

    // An index equal to the length is the append — the one index past the last
    // element that names a real position.
    rig.List("insert 5 z");
    CHECK(rig.obj.ElementAt(5) == "z");

    rig.List("delete 1");
    CHECK(rig.obj.Count() == 5);
    CHECK(rig.obj.ElementAt(1) == "y");
    CHECK(rig.obj.ElementAt(4) == "z");

    rig.List("clear");
    CHECK(rig.obj.Count() == 0);
    rig.List("get 0");
    CHECK(rig.miss.gotBang);
  }

  TEST_CASE("array: getsize reports the length (#548)") {
    Rig rig;
    rig.List("getsize");
    CHECK(rig.data.gotInt);
    CHECK(rig.data.intValue == 0);

    rig.Reset();
    rig.List("append 1 2 3 4");
    rig.List("getsize");
    CHECK(rig.data.gotInt);
    CHECK(rig.data.intValue == 4);
  }

  TEST_CASE("array: an empty array sends nothing at all for getvalue (#548)") {
    // The .sprintf / .prepend rule that an object with nothing to say says
    // nothing rather than sending an empty message.
    Rig rig;
    rig.List("getvalue");
    CHECK_FALSE(rig.data.gotList);
    CHECK_FALSE(rig.data.gotInt);
    CHECK_FALSE(rig.miss.gotBang);
  }

  // ─── indices ────────────────────────────────────────────────────────────────

  TEST_CASE("array: an index out of range is a miss on get and a refusal elsewhere (#548)") {
    Rig rig;
    rig.List("append a b");

    rig.List("get 5");
    CHECK(rig.miss.gotBang);
    CHECK_FALSE(rig.data.gotList);

    const std::uint64_t before = rig.obj.Dropped();
    rig.List("set 5 z");
    rig.List("delete 5");
    rig.List("insert 5 z");
    CHECK(rig.obj.Dropped() == before + 3);
    CHECK(rig.obj.Count() == 2);
  }

  TEST_CASE("array: a negative index is refused, never counted from the end (#548)") {
    // Decided here for the whole family rather than per object: an index is a
    // position. .array.wrap and .array.rotate are the objects that exist to
    // provide the alternatives, and a family where half the objects wrap and
    // half clamp is worse than either rule.
    Rig rig;
    rig.List("append a b c");

    const std::uint64_t before = rig.obj.Dropped();
    rig.List("get -1");
    CHECK_FALSE(rig.data.gotList);
    CHECK_FALSE(rig.miss.gotBang);
    rig.List("set -1 z");
    rig.List("delete -1");
    CHECK(rig.obj.Dropped() == before + 3);
    CHECK(rig.obj.ElementAt(2) == "c");
  }

  TEST_CASE("array: an unknown message is refused and counted, not logged (#548)") {
    Rig rig;
    const std::uint64_t before = rig.obj.Dropped();
    rig.List("frobnicate a b");
    rig.List("append");
    rig.List("get");
    // `set 0` with no value, and `set 0 a b` with too many: an element is one
    // atom, so a set naming several is refused whole rather than storing the
    // first.
    rig.List("set 0");
    rig.List("set 0 a b");
    CHECK(rig.obj.Dropped() == before + 5);
    CHECK(rig.obj.Count() == 0);
  }

  // ─── the reference, and the addressing model ────────────────────────────────

  TEST_CASE("array: a bang emits \"array <name>\" and nothing else (#548)") {
    // The whole of what travels down a cord. Not the contents — a bang that
    // dumped would make a bang mean two things and hand a patch a dump it never
    // asked for. `getvalue` is the message that asks.
    Rig rig("steps");
    rig.List("append 1 2");
    rig.Bang();
    CHECK(rig.reference.gotList);
    CHECK(rig.reference.listValue == "array steps");
    CHECK_FALSE(rig.data.gotList);
    CHECK_FALSE(rig.data.gotInt);
    CHECK(rig.obj.Reference() == "array steps");
  }

  TEST_CASE("array: an unnamed array has no reference to pass on (#548)") {
    Rig rig;
    rig.Bang();
    CHECK_FALSE(rig.reference.gotList);
    CHECK(rig.obj.Reference().empty());
  }

  TEST_CASE("array: ArrayReferenceNames matches exactly and nothing else (#548)") {
    // The bounded compare an array.* object performs when a reference arrives on
    // an inlet — the whole of what it may do with a name on a cord, because
    // resolving an unrecognised one means the registry's mutex.
    using YSE::PATCHER::ArrayReferenceNames;
    const std::string name = "steps";
    CHECK(ArrayReferenceNames(std::string("array steps"), name));
    CHECK_FALSE(ArrayReferenceNames(std::string("array steps2"), name));
    CHECK_FALSE(ArrayReferenceNames(std::string("array step"), name));
    CHECK_FALSE(ArrayReferenceNames(std::string("array  steps"), name));
    CHECK_FALSE(ArrayReferenceNames(std::string("dictionary steps"), name));
    CHECK_FALSE(ArrayReferenceNames(std::string("steps"), name));
    // An unnamed array is not addressable, so nothing names it.
    CHECK_FALSE(ArrayReferenceNames(std::string("array "), std::string()));
  }

  // ─── sharing by name ────────────────────────────────────────────────────────

  TEST_CASE("array: two .array of one name share one sequence (#548)") {
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* writer = p.CreateObject(YSE::OBJ::G_ARRAY, "seq548a");
    YSE::pHandle* reader = p.CreateObject(YSE::OBJ::G_ARRAY, "seq548a");
    REQUIRE(writer != nullptr);
    REQUIRE(reader != nullptr);
    p.Connect(reader, 0, &sinkHandle, 0);

    writer->SetListData(0, "append 60 64 67");
    reader->SetListData(0, "get 1");
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 64);
  }

  TEST_CASE("array: one namespace per store type — an .array and a .dict of one name differ "
            "(#548)") {
    // namedStore.h's rule, and Max's arrangement too: `Store` selects the map.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher p;
    p.create(2);
    p.name("both548");
    YSE::pHandle* d = p.CreateObject(YSE::OBJ::G_DICT, "notes");
    YSE::pHandle* a = p.CreateObject(YSE::OBJ::G_ARRAY, "notes");
    REQUIRE(d != nullptr);
    REQUIRE(a != nullptr);
    p.Connect(a, 2, &sinkHandle, 0);

    d->SetListData(0, "set x 1");
    a->SetListData(0, "get 0");
    CHECK(sink.gotBang);
  }

  TEST_CASE("array: an unnamed .array keeps a sequence of its own (#548)") {
    // Not "shares the empty name": "<patcherName>." is a real, reachable
    // address, so two unnamed objects pooling there would be connected in a way
    // a patch author never wired.
    Rig a;
    Rig b;
    a.List("append 1");
    CHECK(a.obj.Count() == 1);
    CHECK(b.obj.Count() == 0);
    CHECK(a.obj.Address().empty());
    CHECK_FALSE(a.obj.IsShared());
  }

  TEST_CASE("array: the sequence outlives no .array that names it (#548)") {
    // The registry holds its stores weakly: a name lives exactly as long as some
    // object addresses it. Strong ownership would make every name a patch ever
    // spelled immortal for the life of the process, and would leave one test's
    // contents visible to the next — which is the failure this asserts is
    // absent. A delta rather than an absolute, since the registry is
    // process-wide.
    const std::size_t before = YSE::PATCHER::NamedStoreCount<arrayStore>();
    {
      YSE::patcher p;
      p.create(2);
      p.name("array548b");
      YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY, "steps");
      REQUIRE(h != nullptr);
      h->SetListData(0, "append 1");
      CHECK(YSE::PATCHER::NamedStoreCount<arrayStore>() == before + 1);
    }
    CHECK(YSE::PATCHER::NamedStoreCount<arrayStore>() == before);

    // And a fresh patcher of the same name sees an empty array rather than the
    // previous one's contents.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher again;
    again.create(2);
    again.name("array548b");
    YSE::pHandle* h = again.CreateObject(YSE::OBJ::G_ARRAY, "steps");
    REQUIRE(h != nullptr);
    again.Connect(h, 2, &sinkHandle, 0);
    h->SetListData(0, "get 0");
    CHECK(sink.gotBang);
  }

  TEST_CASE("array: the address form is the patcher's, and RefreshBinding follows it (#548)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("array548c_before");

    gArray obj;
    obj.SetParams("steps548c");
    obj.SetParent(&p);
    CHECK(obj.Address() == "array548c_before.steps548c");

    // Idempotent: a rebind to the address it already has keeps the sequence, and
    // with it everything in it.
    obj.GetInlet(0)->SetList("append 0 4 7", YSE::T_GUI);
    obj.RefreshBinding();
    CHECK(obj.Address() == "array548c_before.steps548c");
    CHECK(obj.Count() == 3);

    // The address prefix moved, so the object now addresses a different array —
    // an empty one, exactly as .value, .coll and .dict do.
    p.SetName("array548c_after");
    obj.RefreshBinding();
    CHECK(obj.Address() == "array548c_after.steps548c");
    CHECK(obj.Count() == 0);
  }

  TEST_CASE("array: patcherImplementation::SetName re-anchors the arrays it holds (#548)") {
    // The rename hook itself, which the standalone case above cannot reach: an
    // object created *inside* a patcher must be re-anchored by the patcher,
    // without anybody calling RefreshBinding by hand. Asserted through the miss
    // outlet, since the object pointer is not reachable from a pHandle.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("array548i_before");

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY, "steps548i");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &sinkHandle, 0);
    h->SetListData(0, "append 5");
    h->SetListData(0, "get 0");
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 5);

    MultiSink missSink;
    YSE::pHandle missHandle(&missSink);
    p.Connect(h, 2, &missHandle, 0);

    p.SetName("array548i_after");
    h->SetListData(0, "get 0");
    CHECK(missSink.gotBang);
  }

  // ─── bounds ─────────────────────────────────────────────────────────────────

  TEST_CASE("array: an over-long element is refused, not truncated (#548)") {
    Rig rig;
    const std::string longest(gArray::ELEMENT_CAPACITY, 'v');
    const std::string tooLong(gArray::ELEMENT_CAPACITY + 1, 'v');

    rig.List("append " + longest);
    CHECK(rig.obj.Count() == 1);
    CHECK(rig.obj.ElementAt(0) == longest);

    const std::uint64_t before = rig.obj.Dropped();
    rig.List("append " + tooLong);
    CHECK(rig.obj.Count() == 1);
    CHECK(rig.obj.Dropped() == before + 1);

    // Half a symbol is a different symbol: the element a patch is reading must
    // not silently become one.
    rig.List("set 0 " + tooLong);
    CHECK(rig.obj.ElementAt(0) == longest);
  }

  TEST_CASE("array: a push onto a full array is refused and counted (#548)") {
    Rig rig;
    for (std::size_t i = 0; i < gArray::MAX_ELEMENTS; i++)
      rig.List("append " + std::to_string(i));
    CHECK(rig.obj.Count() == gArray::MAX_ELEMENTS);

    const std::uint64_t before = rig.obj.Dropped();
    rig.List("append 999");
    rig.List("insert 0 999");
    CHECK(rig.obj.Count() == gArray::MAX_ELEMENTS);
    CHECK(rig.obj.Dropped() == before + 2);

    // A replacement still works when the array is full: it writes into an
    // element that already exists rather than needing a new one.
    rig.List("set 0 replaced");
    CHECK(rig.obj.ElementAt(0) == "replaced");
  }

  TEST_CASE("array: an append past the end loses its tail, not its head (#548)") {
    // AtomList's rule, and the failure that costs a patch least.
    Rig rig;
    for (std::size_t i = 0; i + 2 < gArray::MAX_ELEMENTS; i++)
      rig.List("append x");
    CHECK(rig.obj.Count() == gArray::MAX_ELEMENTS - 2);

    const std::uint64_t before = rig.obj.Dropped();
    rig.List("append a b c d");
    CHECK(rig.obj.Count() == gArray::MAX_ELEMENTS);
    CHECK(rig.obj.ElementAt(gArray::MAX_ELEMENTS - 2) == "a");
    CHECK(rig.obj.ElementAt(gArray::MAX_ELEMENTS - 1) == "b");
    CHECK(rig.obj.Dropped() == before + 2);
  }

  TEST_CASE("array: getvalue refuses what will not fit a cord rather than allocating (#548)") {
    // An array of 256 long symbols spells more list text than the patcher's
    // AtomList carries. Refused and counted, not silently grown — the send would
    // then be an allocation on whichever thread asked.
    Rig rig;
    const std::string wide(48, 'w');
    for (std::size_t i = 0; i < gArray::MAX_ELEMENTS; i++)
      rig.List("append " + wide);
    CHECK(rig.obj.Count() == gArray::MAX_ELEMENTS);

    const std::uint64_t before = rig.obj.Dropped();
    rig.List("getvalue");
    CHECK(rig.data.gotList);
    CHECK(rig.obj.Dropped() > before);
    CHECK(rig.data.listValue.size() <= YSE::PATCHER::AtomList::RENDER_CAPACITY);
  }

  // ─── JSON ───────────────────────────────────────────────────────────────────

  TEST_CASE("array: elements are typed on the way into JSON and back (#548)") {
    // Shorter than .dict's story on purpose: a dictionary had to make a flat
    // table nested again, where an array *is* a JSON array.
    arrayStore store;
    CHECK(YSE::PATCHER::ArrayAppend(store, "440", 3));
    CHECK(YSE::PATCHER::ArrayAppend(store, "0.5", 3));
    CHECK(YSE::PATCHER::ArrayAppend(store, "lead", 4));

    nlohmann::json json;
    YSE::PATCHER::ArrayToJson(store, json);
    REQUIRE(json.is_array());
    REQUIRE(json.size() == 3);
    // Typed, not stringly: a number in the document is a JSON number, so an
    // array saved here is a document another tool can read.
    CHECK(json[0].is_number_integer());
    CHECK(json[0].get<int>() == 440);
    CHECK(json[1].is_number_float());
    CHECK(json[1].get<double>() == doctest::Approx(0.5));
    CHECK(json[2].is_string());
    CHECK(json[2].get<std::string>() == "lead");

    arrayStore back;
    YSE::PATCHER::ArrayFromJson(json, back);
    REQUIRE(back.count == 3);
    CHECK(back.elements[0] == "440");
    CHECK(back.elements[1] == "0.5");
    CHECK(back.elements[2] == "lead");
    CHECK(YSE::PATCHER::ArrayFind(back, "lead", 4) == 2);
    CHECK(YSE::PATCHER::ArrayFind(back, "nope", 4) == back.count);
  }

  TEST_CASE("array: a nested document has no atom that spells it and is skipped (#548)") {
    // An element is one atom. A sub-document is not one, and inventing a
    // spelling for it would be inventing list text per element — the ambiguity
    // this type exists to avoid.
    nlohmann::json json = nlohmann::json::array();
    json.push_back(1);
    json.push_back(nlohmann::json::array({1, 2}));
    json.push_back(nlohmann::json::object());
    json.push_back(nullptr);
    json.push_back(true);
    json.push_back(2);

    arrayStore store;
    YSE::PATCHER::ArrayFromJson(json, store);
    REQUIRE(store.count == 3);
    CHECK(store.elements[0] == "1");
    CHECK(store.elements[1] == "1");
    CHECK(store.elements[2] == "2");
  }

  TEST_CASE("array: contents survive a DumpJSON / ParseJSON round trip (#548)") {
    // Read back through the outlet rather than through an accessor: what has to
    // survive is the array a patch can use, not a member variable.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher src;
    src.create(2);
    src.name("array548d");
    YSE::pHandle* a = src.CreateObject(YSE::OBJ::G_ARRAY, "steps");
    REQUIRE(a != nullptr);
    a->SetListData(0, "append 60 0.5 lead");

    const std::string json = src.DumpJSON();
    CHECK(json.find(".array") != std::string::npos);
    CHECK(json.find("lead") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.name("array548e");
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".array");
    CHECK(copy->GetParams() == std::string("steps"));
    loaded.Connect(copy, 0, &sinkHandle, 0);

    copy->SetListData(0, "getvalue");
    CHECK(sink.gotList);
    CHECK(sink.listValue == "60 0.5 lead");
  }

  TEST_CASE("array: an empty array writes no state key (#548)") {
    // The state hook is opt-in: an object without state of its own has to
    // serialise byte for byte the way it always did, or every saved patch grows
    // a null for every object in it.
    YSE::patcher p;
    p.create(2);
    REQUIRE(p.CreateObject(YSE::OBJ::G_ARRAY) != nullptr);
    CHECK(p.DumpJSON().find("\"state\"") == std::string::npos);
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array: a push arriving on the audio thread lands (#548)") {
    // The in-patcher delivery path defers to the audio thread and is drained by
    // an explicit Calculate (issue #225), so a .r feeding an .array pushes on
    // T_DSP. That is the ordinary case, not an exotic one — and it is the whole
    // of what "the value type crosses the control/audio boundary" means here:
    // the name was resolved on the control thread, so the push on the audio
    // thread is one pointer hop and one exchange.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("array548f");

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "cmd548f");
    YSE::pHandle* store = p.CreateObject(YSE::OBJ::G_ARRAY, "state");
    YSE::pHandle* poll = p.CreateObject(YSE::OBJ::G_ARRAY, "state");
    REQUIRE(recv != nullptr);
    REQUIRE(store != nullptr);
    REQUIRE(poll != nullptr);
    p.Connect(recv, 0, store, 0);
    p.Connect(poll, 0, &sinkHandle, 0);

    p.PassData(std::string("append 60 64 67"), "cmd548f", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);

    poll->SetListData(0, "get 2");
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 67);
  }

  TEST_CASE("array: a fetch driven on the audio thread emits on the audio thread (#548)") {
    Rig rig;
    const std::string push = "append a-symbol-comfortably-past-any-small-string-buffer";
    const std::string get = "get 0";
    rig.List(push, YSE::T_DSP);
    rig.List(get, YSE::T_DSP);
    CHECK(rig.data.gotList);
    CHECK(rig.data.listValue == "a-symbol-comfortably-past-any-small-string-buffer");
  }

  TEST_CASE("array: no message path allocates (#548)") {
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

    const std::string appendNumbers = "append 60 64 67 72 76 79 84";
    const std::string appendSymbol = "append a-symbol-past-every-small-string-buffer";
    const std::string insertMid = "insert 1 inserted-symbol-of-a-comfortable-width";
    const std::string replace = "set 0 55";
    const std::string getHit = "get 1";
    const std::string getNumber = "get 0";
    const std::string getMiss = "get 900";
    const std::string negative = "get -3";
    const std::string whole = "getvalue";
    const std::string size = "getsize";
    const std::string remove = "delete 2";
    const std::string unknown = "frobnicate something quite long indeed";
    const std::string tooLong = "append " + std::string(gArray::ELEMENT_CAPACITY + 1, 'z');

    Rig rig("probe548");

    // Warm every path, so the sinks' own buffers and any first-call machinery
    // are not what the probe catches.
    rig.List(appendNumbers);
    rig.List(appendSymbol);
    rig.List(insertMid);
    rig.List(getHit);
    rig.List(getNumber);
    rig.List(getMiss);
    rig.List(whole);
    rig.List(size);
    rig.Bang();
    REQUIRE(rig.data.gotList);
    REQUIRE(rig.reference.gotList);
    REQUIRE(rig.miss.gotBang);

    // Size the sinks' buffers independently of the object, so a later send that
    // happens to be longer than every warm-up send cannot be the allocation.
    const std::string sinkWarm(YSE::PATCHER::AtomList::RENDER_CAPACITY, 'x');
    rig.data.GetInlet(0)->SetList(sinkWarm, YSE::T_GUI);
    rig.reference.GetInlet(0)->SetList(sinkWarm, YSE::T_GUI);

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      rig.List(appendNumbers, YSE::T_DSP);
      rig.List(appendSymbol, YSE::T_DSP);
      rig.List(insertMid, YSE::T_DSP);
      rig.List(replace, YSE::T_DSP);
      rig.List(getHit, YSE::T_DSP);
      rig.List(getNumber, YSE::T_DSP);
      rig.List(getMiss, YSE::T_DSP);
      rig.List(negative, YSE::T_DSP);
      rig.List(whole, YSE::T_DSP);
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
    CHECK(rig.obj.ElementAt(0) == "55");
    CHECK(rig.obj.Count() > 0);
    CHECK(rig.reference.listValue == "array probe548");
    CHECK(rig.data.listValue.find("55") != std::string::npos);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array: params survive a DumpJSON / ParseJSON round trip (#548)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_ARRAY, "steps548g");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".array"));
    CHECK(copy->GetParams() == std::string("steps548g"));
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 3);
  }

  TEST_CASE("array: a re-parse keeps the contents while the name is still held (#548)") {
    // A SetParams on a published object is a rebuild (#234): the replacement is
    // constructed while the original is still alive, so the array is still held
    // and the running contents carry across the edit rather than snapping back
    // to empty under every object reading them. Modelled here with two objects
    // on one name, which is the same "somebody else still holds it" situation
    // and the one a unit test can build directly.
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("array548h");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("steps");

    gArray a;
    a.SetParent(&p);
    a.SetParams("steps");
    CHECK(a.IsShared());
    a.GetInlet(0)->SetList("append 1", YSE::T_GUI);
    CHECK(keeper.Count() == 1);
    a.SetParams("steps");
    CHECK(a.Count() == 1);

    // An empty argument string is a real reset back to a private array, which is
    // what PARM_CLEAR is for: Parameters::Set() returns early on an empty
    // string, so without the clear hook the object would keep its old name.
    a.SetParams("");
    CHECK(a.ArrayName().empty());
    CHECK_FALSE(a.IsShared());
    CHECK(a.Count() == 0);
    // And the shared array is untouched: one object left the name, the data
    // stayed with the objects that are still on it.
    CHECK(keeper.Count() == 1);
  }

  TEST_CASE("array: carries complete documentation metadata (#548)") {
    gArray a;
    CHECK_FALSE(a.GetDescription().empty());
    CHECK(a.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = a.GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == "name");
  }
}
