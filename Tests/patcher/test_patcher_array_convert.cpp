// Tests for .array.tolist / .array.tostring / .array.tosymbol (issue #796) —
// Max's converter trio on the name-addressed value model .array settled
// (#548).
//
// What has to be proven, and what every case below is one of:
//
//   - **the array is bound from the creation argument.** An array never
//     travels down a cord, so ".array.tolist <name>" resolves the name once,
//     on the control thread, and an `array <name>` message is honoured only
//     when it names the array already bound — ArrayReferenceNames' bounded
//     compare, never a registry lookup on a message path.
//   - **one render, three sends.** All three collect the same store under
//     one hold of its guard; what differs is what leaves the outlet. tolist
//     is typed — one element leaves as the int, float or symbol it spells,
//     several as one list message, SendAtoms' rule. tostring is the same
//     characters always as text, never retyped. tosymbol is one
//     whitespace-free token, the elements butted together, never retyped —
//     a symbol is a name.
//   - **two overflow rules, each where it is honest.** List text past what a
//     cord carries loses its tail, every lost element a counted refusal —
//     .array getvalue's rule, #796's own prescription. The one-token result
//     is refused whole instead — a token that lost elements is a different
//     name, .array.join's rule (#793).
//   - **an empty array bangs the empty outlet** — "no data" is a state a
//     patch must be able to route on, not an error.
//   - **nothing on a message path allocates**, proven with the probe on
//     T_DSP, in-patcher delivery's thread.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstddef>
#include <cstdint>
#include <string>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayConvert.h"
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
using YSE::PATCHER::gArray;
using YSE::PATCHER::gArrayToList;
using YSE::PATCHER::gArrayToString;
using YSE::PATCHER::gArrayToSymbol;

namespace {

  // An .array and one converter on one name, sharing one
  // patcherImplementation so the name actually binds ("<patcherName>.<name>"
  // needs a patcher to prefix with — a parentless object stays private). The
  // sinks are declared before the objects so they are torn down last, while
  // the outlets wired to them still exist (see sinks.hpp on why that
  // matters).
  template <typename ObjectT> struct Rig {
    MultiSink result; // outlet 0: the conversion
    MultiSink empty; // outlet 1: the empty bang
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    gArray array;
    ObjectT object;

    Rig(const std::string& patcherName, const std::string& name) {
      p.SetName(patcherName);
      array.SetParent(&p);
      array.SetParams(name);
      object.SetParent(&p);
      object.SetParams(name);
      Wire(object, 0, result);
      Wire(object, 1, empty);
    }

    void Store(const std::string& message) {
      array.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Ask() {
      object.GetInlet(0)->SetBang(YSE::T_GUI);
    }
  };

  // 17 identical elements of ELEMENT_CAPACITY characters spell more list
  // text than a cord carries (17 * 64 > AtomList::TEXT_CAPACITY), so the
  // seventeenth is the one that does not fit; 16 fit exactly.
  std::string WideToken() {
    return std::string(YSE::PATCHER::arrayStore::ELEMENT_CAPACITY, 'x');
  }
  std::string WideAppend(std::size_t count) {
    std::string out = "append";
    for (std::size_t i = 0; i < count; i++) {
      out += ' ';
      out += WideToken();
    }
    return out;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.convert: all three registered, with .array.join's two inlets over two "
            "outlets (#796)") {
    YSE::patcher p;
    p.create(2);

    const char* types[] = {YSE::OBJ::G_ARRAY_TOLIST, YSE::OBJ::G_ARRAY_TOSTRING,
                           YSE::OBJ::G_ARRAY_TOSYMBOL};
    auto names = YSE::PATCHER::Register().AllNames();
    for (const char* type : types) {
      CAPTURE(type);
      YSE::pHandle* handle = p.CreateObject(type);
      REQUIRE(handle != nullptr);
      CHECK(std::string(handle->Type()) == type);
      CHECK(handle->GetInputs() == 2);
      CHECK(handle->GetOutputs() == 2);

      bool found = false;
      for (const auto& name : names) {
        if (name == std::string(type)) found = true;
      }
      CHECK(found);
    }
  }

  TEST_CASE("array.convert: the trigger takes the ask, the reference inlet only list text "
            "(#796)") {
    // The result is asked for with a bang, never addressed, so there is no
    // int or float method anywhere — gArrayJoin's shape.
    gArrayToList object;
    const unsigned int trigger = object.GetInlet(0)->GetAcceptedTypes();
    CHECK((trigger & YSE::PATCHER::IT_BANG) != 0);
    CHECK((trigger & YSE::PATCHER::IT_LIST) != 0);
    CHECK((trigger & YSE::PATCHER::IT_INT) == 0);
    CHECK((trigger & YSE::PATCHER::IT_FLOAT) == 0);
    const unsigned int ref = object.GetInlet(1)->GetAcceptedTypes();
    CHECK((ref & YSE::PATCHER::IT_LIST) != 0);
    CHECK((ref & YSE::PATCHER::IT_BANG) == 0);
  }

  // ─── .array.tolist: the typed send ──────────────────────────────────────────

  TEST_CASE("array.tolist: several elements leave as one list, one element as the value it "
            "spells (#796)") {
    Rig<gArrayToList> rig("acv796a", "a796a");
    rig.Store("append c4 e4 g4");
    rig.Ask();
    CHECK(rig.result.gotList);
    CHECK(rig.result.listValue == "c4 e4 g4");
    CHECK_FALSE(rig.empty.gotBang);

    // Spellings survive whole: an int stays an int, a float keeps its
    // decimal point, a symbol its characters — the array and the list it
    // spells are the same thing seen twice.
    rig.Store("clear");
    rig.Store("append 1 2.5 x");
    rig.result.reset();
    rig.Ask();
    CHECK(rig.result.listValue == "1 2.5 x");

    // One element is not a list of one — SendAtoms' rule: a numeric element
    // leaves as the int or float its spelling reads as, a symbol as itself.
    rig.Store("clear");
    rig.Store("append 60");
    rig.result.reset();
    rig.Ask();
    CHECK(rig.result.gotInt);
    CHECK(rig.result.intValue == 60);
    CHECK_FALSE(rig.result.gotList);

    rig.Store("clear");
    rig.Store("append 7.5");
    rig.result.reset();
    rig.Ask();
    CHECK(rig.result.gotFloat);
    CHECK(rig.result.floatValue == doctest::Approx(7.5f));

    rig.Store("clear");
    rig.Store("append c4");
    rig.result.reset();
    rig.Ask();
    CHECK(rig.result.gotList);
    CHECK(rig.result.listValue == "c4");
    CHECK(rig.object.Dropped() == 0);
  }

  // ─── .array.tostring: the same characters, always text ──────────────────────

  TEST_CASE("array.tostring: the list text always leaves as text, one numeric element "
            "included (#796)") {
    Rig<gArrayToString> rig("acv796b", "a796b");
    rig.Store("append c4 e4 g4");
    rig.Ask();
    CHECK(rig.result.gotList);
    CHECK(rig.result.listValue == "c4 e4 g4");

    // The whole difference from .array.tolist: a string is characters, not a
    // value, so the single element that tolist would retype to the int 60
    // leaves as the text that spells it.
    rig.Store("clear");
    rig.Store("append 60");
    rig.result.reset();
    rig.Ask();
    CHECK(rig.result.gotList);
    CHECK(rig.result.listValue == "60");
    CHECK_FALSE(rig.result.gotInt);
    CHECK_FALSE(rig.result.gotFloat);
    CHECK(rig.object.Dropped() == 0);
  }

  // ─── .array.tosymbol: one token, never retyped ──────────────────────────────

  TEST_CASE("array.tosymbol: the elements leave butted together as one token, as text "
            "(#796)") {
    // The file-name use the issue names: path pieces in, one name out.
    Rig<gArrayToSymbol> rig("acv796c", "a796c");
    rig.Store("append kick .wav");
    rig.Ask();
    CHECK(rig.result.gotList);
    CHECK(rig.result.listValue == "kick.wav");
    CHECK_FALSE(rig.empty.gotBang);

    // Never retyped — a symbol is a name: "1 2" collapses to the characters
    // 12, where .array.join's empty-separator default deliberately leaves
    // the int 12.
    rig.Store("clear");
    rig.Store("append 1 2");
    rig.result.reset();
    rig.Ask();
    CHECK(rig.result.gotList);
    CHECK(rig.result.listValue == "12");
    CHECK_FALSE(rig.result.gotInt);

    // A single element answers its characters alone — still text, even when
    // they spell a number.
    rig.Store("clear");
    rig.Store("append 60");
    rig.result.reset();
    rig.Ask();
    CHECK(rig.result.gotList);
    CHECK(rig.result.listValue == "60");
    CHECK_FALSE(rig.result.gotInt);
    CHECK(rig.object.Dropped() == 0);
  }

  // ─── the empty outlet ───────────────────────────────────────────────────────

  TEST_CASE("array.convert: an empty array bangs the empty outlet on all three (#796)") {
    Rig<gArrayToList> list("acv796d", "a796d1");
    Rig<gArrayToString> text("acv796d2", "a796d2");
    Rig<gArrayToSymbol> symbol("acv796d3", "a796d3");
    list.Ask();
    text.Ask();
    symbol.Ask();
    CHECK(list.empty.gotBang);
    CHECK(text.empty.gotBang);
    CHECK(symbol.empty.gotBang);
    CHECK_FALSE(list.result.gotList);
    CHECK_FALSE(text.result.gotList);
    CHECK_FALSE(symbol.result.gotList);
    CHECK(list.object.Dropped() == 0);
    CHECK(text.object.Dropped() == 0);
    CHECK(symbol.object.Dropped() == 0);
  }

  TEST_CASE("array.convert: an unnamed object reads a private, empty array — every ask bangs "
            "the empty outlet (#796)") {
    MultiSink result;
    MultiSink empty;
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("acv796e");
    gArrayToSymbol object;
    object.SetParent(&p);
    Wire(object, 0, result);
    Wire(object, 1, empty);
    CHECK(object.Address().empty());

    object.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(empty.gotBang);
    CHECK_FALSE(result.gotList);
    CHECK(object.Dropped() == 0);
  }

  // ─── the two overflow rules ─────────────────────────────────────────────────

  TEST_CASE("array.tolist/.tostring: past what a cord carries the tail is lost and counted — "
            "getvalue's rule (#796)") {
    // 17 elements of 64 characters spell 1088 characters of list text
    // against AtomList's 1024: sixteen fit, the seventeenth is the tail.
    std::string expected;
    for (int i = 0; i < 16; i++) {
      if (i != 0) expected += ' ';
      expected += WideToken();
    }

    Rig<gArrayToList> list("acv796f", "a796f");
    list.Store(WideAppend(17));
    CHECK(list.array.Count() == 17);
    list.Ask();
    CHECK(list.result.gotList);
    CHECK(list.result.listValue == expected);
    CHECK(list.object.Dropped() == 1);

    Rig<gArrayToString> text("acv796f2", "a796f2");
    text.Store(WideAppend(17));
    text.Ask();
    CHECK(text.result.gotList);
    CHECK(text.result.listValue == expected);
    CHECK(text.object.Dropped() == 1);
  }

  TEST_CASE("array.tosymbol: a result that cannot leave whole is refused whole — join's rule, "
            "not getvalue's (#796)") {
    // The same seventeen elements: a token that lost one would be a
    // different name, so nothing fires and the refusal is counted — neither
    // outlet is the answer.
    Rig<gArrayToSymbol> rig("acv796g", "a796g");
    rig.Store(WideAppend(17));
    rig.Ask();
    CHECK_FALSE(rig.result.gotList);
    CHECK_FALSE(rig.empty.gotBang);
    CHECK(rig.object.Dropped() == 1);

    // At exactly the bound the token leaves whole: sixteen 64-character
    // elements butt into one 1024-character name.
    Rig<gArrayToSymbol> fit("acv796g2", "a796g2");
    fit.Store(WideAppend(16));
    fit.Ask();
    CHECK(fit.result.gotList);
    CHECK(fit.result.listValue.size() == 16 * YSE::PATCHER::arrayStore::ELEMENT_CAPACITY);
    CHECK(fit.result.listValue.find(' ') == std::string::npos);
    CHECK(fit.object.Dropped() == 0);
  }

  // ─── the reference gesture ──────────────────────────────────────────────────

  TEST_CASE("array.convert: the reference asks on the trigger, acknowledges on its own inlet, "
            "and anything else is refused (#796)") {
    Rig<gArrayToList> rig("acv796h", "a796h");
    rig.Store("append 60 64 67");

    // The message an .array's reference outlet emits on a bang — the
    // family's gesture: bang the array, out comes the conversion.
    rig.object.GetInlet(0)->SetList("array a796h", YSE::T_GUI);
    CHECK(rig.result.gotList);
    CHECK(rig.result.listValue == "60 64 67");
    CHECK(rig.object.Dropped() == 0);

    // A reference naming an array this object is not bound to is refused
    // everywhere, never resolved: a registry lookup is a mutex, and this may
    // be the audio thread.
    rig.result.reset();
    rig.object.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK_FALSE(rig.result.gotList);
    CHECK(rig.object.Dropped() == 1);

    // Any other message on the trigger is refused too — the ask is a bang or
    // the bound reference, nothing else.
    rig.object.GetInlet(0)->SetList("getvalue", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 2);

    // The reference inlet acknowledges the bound array silently and refuses
    // anything else — gDictSlice's inlet rule.
    rig.object.GetInlet(1)->SetList("array a796h", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 2);
    rig.object.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK(rig.object.Dropped() == 3);
    CHECK_FALSE(rig.result.gotList);
  }

  // ─── the family chains ──────────────────────────────────────────────────────

  TEST_CASE("array.convert: wired from the array's reference outlet, banging the array "
            "converts, end to end on cords (#796)") {
    // The use case #796 names, run through the public patcher API: the array
    // out to everything that is not an array — the same store leaving as a
    // list down one cord and as one symbol down another, from one bang.
    MultiSink listOut;
    MultiSink symbolOut;
    YSE::pHandle listHandle(&listOut);
    YSE::pHandle symbolHandle(&symbolOut);
    YSE::patcher p;
    p.create(2);
    p.name("acv796i");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a796i");
    YSE::pHandle* tolist = p.CreateObject(YSE::OBJ::G_ARRAY_TOLIST, "a796i");
    YSE::pHandle* tosymbol = p.CreateObject(YSE::OBJ::G_ARRAY_TOSYMBOL, "a796i");
    REQUIRE(array != nullptr);
    REQUIRE(tolist != nullptr);
    REQUIRE(tosymbol != nullptr);
    p.Connect(array, 1, tolist, 0);
    p.Connect(array, 1, tosymbol, 0);
    p.Connect(tolist, 0, &listHandle, 0);
    p.Connect(tosymbol, 0, &symbolHandle, 0);

    array->SetListData(0, "append snare .wav");
    array->SetBang(0);
    REQUIRE(listOut.gotList);
    CHECK(listOut.listValue == "snare .wav");
    REQUIRE(symbolOut.gotList);
    CHECK(symbolOut.listValue == "snare.wav");
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.convert: patcherImplementation::SetName re-anchors the shared base "
            "(#796)") {
    // The rename dispatch itself: an object created *inside* a patcher must
    // be re-anchored by the patcher, without anybody calling RefreshBinding
    // by hand. The keeper holds the old-address store; after the rename the
    // object reads a fresh empty array under the new prefix, so an ask now
    // bangs the empty outlet instead of answering the keeper's contents.
    MultiSink result;
    MultiSink empty;
    YSE::pHandle resultHandle(&result);
    YSE::pHandle emptyHandle(&empty);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("acv796j_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a796j");
    keeper.GetInlet(0)->SetList("append c4 e4", YSE::T_GUI);

    YSE::pHandle* object = p.CreateObject(YSE::OBJ::G_ARRAY_TOSYMBOL, "a796j");
    REQUIRE(object != nullptr);
    p.Connect(object, 0, &resultHandle, 0);
    p.Connect(object, 1, &emptyHandle, 0);

    object->SetBang(0);
    REQUIRE(result.gotList);
    CHECK(result.listValue == "c4e4");
    CHECK_FALSE(empty.gotBang);

    p.SetName("acv796j_after");
    result.reset();
    object->SetBang(0);
    // A different address is a different (empty) array — and the keeper's
    // contents were not touched.
    CHECK_FALSE(result.gotList);
    CHECK(empty.gotBang);
    CHECK(keeper.Count() == 2);
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.convert: an ask arriving over in-patcher delivery lands on T_DSP (#796)") {
    // A .r feeding the trigger dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread asks for the conversion" is the
    // ordinary case, and the whole path is one guard hold, one bounded
    // render and a send from storage the object already owns.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("acv796k");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a796k");
    keeper.GetInlet(0)->SetList("append 60 64 67", YSE::T_GUI);

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go796k");
    YSE::pHandle* object = p.CreateObject(YSE::OBJ::G_ARRAY_TOLIST, "a796k");
    REQUIRE(recv != nullptr);
    REQUIRE(object != nullptr);
    p.Connect(recv, 0, object, 0);
    p.Connect(object, 0, &sinkHandle, 0);

    p.PassData(std::string("array a796k"), "go796k", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "60 64 67");
  }

  TEST_CASE("array.convert: no message path allocates (#796)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path of all three objects — the bang on a several-element
    // and on a one-element array, the reference gesture, the refusals, the
    // reference-inlet acknowledgement and the empty-array bang — on T_DSP,
    // in-patcher delivery's thread.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "array probeACV796";
    const std::string wrongName = "array somewhere_else_long";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("acv796l");
    MultiSink listOut;
    MultiSink textOut;
    MultiSink symbolOut;
    MultiSink singleOut;
    MultiSink emptyOut;
    gArray array;
    gArray single;
    gArrayToList tolist;
    gArrayToString tostring;
    gArrayToSymbol tosymbol;
    gArrayToList tolistSingle;
    gArrayToSymbol unnamed;
    array.SetParent(&p);
    array.SetParams("probeACV796");
    single.SetParent(&p);
    single.SetParams("probeACV796s");
    tolist.SetParent(&p);
    tolist.SetParams("probeACV796");
    tostring.SetParent(&p);
    tostring.SetParams("probeACV796");
    tosymbol.SetParent(&p);
    tosymbol.SetParams("probeACV796");
    tolistSingle.SetParent(&p);
    tolistSingle.SetParams("probeACV796s");
    unnamed.SetParent(&p);
    Wire(tolist, 0, listOut);
    Wire(tostring, 0, textOut);
    Wire(tosymbol, 0, symbolOut);
    Wire(tolistSingle, 0, singleOut);
    Wire(unnamed, 1, emptyOut);

    array.GetInlet(0)->SetList("append c4 e4 g4", YSE::T_GUI);
    single.GetInlet(0)->SetList("append 60", YSE::T_GUI);

    // Warm every path — including the sinks' assignments — so first-call
    // machinery is not what the probe catches.
    tolist.GetInlet(0)->SetBang(YSE::T_GUI);
    tostring.GetInlet(0)->SetBang(YSE::T_GUI);
    tosymbol.GetInlet(0)->SetBang(YSE::T_GUI);
    tolistSingle.GetInlet(0)->SetBang(YSE::T_GUI);
    unnamed.GetInlet(0)->SetBang(YSE::T_GUI);
    tolist.GetInlet(0)->SetList(reference, YSE::T_GUI);
    tolist.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    tostring.GetInlet(1)->SetList(reference, YSE::T_GUI);

    const std::uint64_t drops = tolist.Dropped();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      tolist.GetInlet(0)->SetBang(YSE::T_DSP); // the several-element list
      tostring.GetInlet(0)->SetBang(YSE::T_DSP); // the rendered text
      tosymbol.GetInlet(0)->SetBang(YSE::T_DSP); // the butted token
      tolistSingle.GetInlet(0)->SetBang(YSE::T_DSP); // the typed single element
      unnamed.GetInlet(0)->SetBang(YSE::T_DSP); // the empty bang
      tolist.GetInlet(0)->SetList(reference, YSE::T_DSP); // the gesture
      tolist.GetInlet(0)->SetList(wrongName, YSE::T_DSP); // refused
      tostring.GetInlet(1)->SetList(reference, YSE::T_DSP); // acknowledged
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing.
    CHECK(listOut.gotList);
    CHECK(listOut.listValue == "c4 e4 g4"); // the gesture re-sent the list last
    CHECK(singleOut.gotInt);
    CHECK(singleOut.intValue == 60);
    CHECK_FALSE(singleOut.gotList); // one element is not a list of one
    CHECK(textOut.listValue == "c4 e4 g4");
    CHECK(symbolOut.listValue == "c4e4g4");
    CHECK(emptyOut.gotBang);
    CHECK(tolist.Dropped() == drops + 1);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.convert: params survive a DumpJSON / ParseJSON round trip (#796)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_ARRAY_TOLIST, "notes796m") != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::G_ARRAY_TOSTRING, "notes796m") != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::G_ARRAY_TOSYMBOL, "notes796m") != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 3);

    // Found by type rather than by list position: the loaded patcher's
    // enumeration order is not the creation order.
    const char* types[] = {".array.tolist", ".array.tostring", ".array.tosymbol"};
    for (const char* type : types) {
      CAPTURE(type);
      YSE::pHandle* copy = nullptr;
      for (unsigned int i = 0; i < loaded.Objects(); i++) {
        YSE::pHandle* handle = loaded.GetHandleFromList(static_cast<int>(i));
        REQUIRE(handle != nullptr);
        if (std::string(handle->Type()) == type) copy = handle;
      }
      REQUIRE(copy != nullptr);
      // The analyzer cannot see that a failed REQUIRE aborts the case
      // (doctest's failure path is a runtime jump), so it assumes `copy` may
      // be null here.
      // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
      CHECK(copy->GetParams() == std::string("notes796m"));
      CHECK(copy->GetInputs() == 2);
      CHECK(copy->GetOutputs() == 2);
    }
  }

  TEST_CASE("array.convert: all three carry complete documentation metadata (#796)") {
    gArrayToList tolist;
    gArrayToString tostring;
    gArrayToSymbol tosymbol;
    YSE::PATCHER::pObject* objects[] = {&tolist, &tostring, &tosymbol};
    for (YSE::PATCHER::pObject* object : objects) {
      CAPTURE(object->Type());
      CHECK_FALSE(object->GetDescription().empty());
      CHECK(object->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
      REQUIRE(object->GetParamDocs().size() == 1);
      CHECK(object->GetParamDocs()[0].name == "name");
    }
  }
}
