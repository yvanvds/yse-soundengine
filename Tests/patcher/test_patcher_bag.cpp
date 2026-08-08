// Tests for .bag (issue #495) — the patcher's unordered collection of numbers.
//
// Five things are worth pinning here, and they are the ones an implementation
// can get wrong without ever crashing:
//
//   - **which inlet is the flag, and which way round a list reads.** Max's
//     `bag` puts the *value* on the left inlet and the *add/remove flag* on the
//     right, so its list method is `<value> <flag>` — `60 1` adds 60. Issue
//     #495's summary line has the two the other way round; a patcher whose
//     argument order is the reverse of Max's is a silent trap for every patch
//     brought across, so the order is asserted rather than assumed.
//   - **adding and removing say nothing.** Max: "no output is triggered by a
//     number received in either inlet." An object that echoed would make the
//     obvious note-tracker patch feed back on itself.
//   - **the two ends of the collection.** A bang sends "in reverse order from
//     that in which they were stored" (newest first) and `cut` takes the
//     oldest. Both ends are pinned, because an implementation that got the
//     order backwards passes every count-based test.
//   - **duplicates are a creation argument, and a remove takes the newest
//     instance.** Which instance goes is observable once duplicates are on —
//     `60 62 60` dumps differently depending on the choice.
//   - **the contents are run-time state and the flag is a parameter.** The
//     duplicate mode survives a save; what the bag is holding deliberately does
//     not, which is where this object parts company with .coll.
//
// No audio device and no engine of its own, except where a real patcher graph
// is the point.

#include <doctest/doctest.h>
#include <string>
#include <vector>

#include "patcher/genericObjects/gBag.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::gBag;

namespace {

  // Records every int it receives, in order. MultiSink only keeps the last of
  // each kind, which cannot tell a dump of three numbers from a dump of one —
  // and the order of a dump is half of what this object promises.
  struct Recorder : YSE::PATCHER::pObject {
    std::vector<int> ints;
    int otherKinds = 0;

    Recorder() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { otherKinds++; });
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) { ints.push_back(v); });
      inputs.back().RegisterFloat([this](float, int, YSE::THREAD) { otherKinds++; });
      inputs.back().RegisterList([this](const std::string&, int, YSE::THREAD) { otherKinds++; });
    }
    const char* Type() const override {
      return "recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    void reset() {
      ints.clear();
      otherKinds = 0;
    }
  };

  // A standalone .bag with a recorder on its outlet. Standalone where the
  // patcher is not the point: a test that needed one could not tell a refused
  // message from a message the patcher never delivered.
  struct Rig {
    Recorder out;
    gBag obj;

    Rig() {
      obj.ConnectOutlet(out.GetInlet(0), 0);
    }

    void List(const std::string& message) {
      obj.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Value(int value) {
      obj.GetInlet(0)->SetInt(value, YSE::T_GUI);
    }
    void ValueFloat(float value) {
      obj.GetInlet(0)->SetFloat(value, YSE::T_GUI);
    }
    void Flag(int value) {
      obj.GetInlet(1)->SetInt(value, YSE::T_GUI);
    }
    void FlagFloat(float value) {
      obj.GetInlet(1)->SetFloat(value, YSE::T_GUI);
    }
    void Bang() {
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
    }

    // Put `value` in, whatever the flag was before.
    void Add(int value) {
      Flag(1);
      Value(value);
    }
    // Take `value` out, whatever the flag was before.
    void Drop(int value) {
      Flag(0);
      Value(value);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("bag: registered, two inlets and one outlet (#495)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* bag = p.CreateObject(YSE::OBJ::G_BAG);
    REQUIRE(bag != nullptr);
    CHECK(std::string(bag->Type()) == ".bag");
    CHECK(bag->GetInputs() == 2);
    CHECK(bag->GetOutputs() == 1);
  }

  TEST_CASE("bag: appears in the registry's name list (#495)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_BAG)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("bag: the value inlet takes bang, int, float and list (#495)") {
    gBag obj;
    const unsigned int value = obj.GetInlet(0)->GetAcceptedTypes();
    CHECK((value & YSE::PATCHER::IT_BANG) != 0);
    CHECK((value & YSE::PATCHER::IT_INT) != 0);
    CHECK((value & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((value & YSE::PATCHER::IT_LIST) != 0);

    // The flag inlet is Max's [int] method plus its float conversion, and
    // nothing else: a bang there would have no meaning.
    const unsigned int flag = obj.GetInlet(1)->GetAcceptedTypes();
    CHECK((flag & YSE::PATCHER::IT_INT) != 0);
    CHECK((flag & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((flag & YSE::PATCHER::IT_BANG) == 0);
  }

  TEST_CASE("bag: a fresh collection is empty and a bang sends nothing (#495)") {
    Rig rig;
    CHECK(rig.obj.Count() == 0);

    rig.Bang();
    CHECK(rig.out.ints.empty());
    CHECK(rig.out.otherKinds == 0);
  }

  TEST_CASE("bag: Calculate() sends nothing (#495)") {
    // The object is driven by its inlets; one that emitted from Calculate()
    // would re-dump the collection on every DSP tick.
    Rig rig;
    rig.Add(60);
    rig.out.reset();

    rig.obj.Calculate(YSE::T_DSP);
    CHECK(rig.out.ints.empty());
    CHECK(rig.out.otherKinds == 0);
  }

  // ─── the flag, and what a number means ──────────────────────────────────────

  TEST_CASE("bag: a non-zero flag adds and a zero flag removes (#495)") {
    Rig rig;
    rig.Flag(1);
    rig.Value(60);
    CHECK(rig.obj.Count() == 1);
    CHECK(rig.obj.Contains(60));

    rig.Flag(0);
    rig.Value(60);
    CHECK(rig.obj.Count() == 0);
    CHECK_FALSE(rig.obj.Contains(60));
  }

  TEST_CASE("bag: the flag starts at zero, so a bare number does not join (#495)") {
    // Max documents no initial value; 0 is the conservative one — a value that
    // arrives before the patch has said what to do with it is not silently
    // collected.
    Rig rig;
    CHECK_FALSE(rig.obj.AddMode());
    rig.Value(60);
    CHECK(rig.obj.Count() == 0);
  }

  TEST_CASE("bag: adding and removing send nothing (#495)") {
    // Max: "no output is triggered by a number received in either inlet."
    Rig rig;
    rig.Add(60);
    rig.Add(62);
    rig.Drop(60);
    rig.Flag(1);
    CHECK(rig.out.ints.empty());
    CHECK(rig.out.otherKinds == 0);
  }

  TEST_CASE("bag: removing something that is not there does nothing (#495)") {
    Rig rig;
    rig.Add(60);
    rig.Drop(62);
    CHECK(rig.obj.Count() == 1);
    CHECK(rig.obj.Contains(60));
  }

  TEST_CASE("bag: a float is converted to an int on either inlet (#495)") {
    Rig rig;
    rig.Flag(1);
    rig.ValueFloat(60.7f);
    CHECK(rig.obj.Contains(60));
    CHECK(rig.obj.Count() == 1);

    // 0.5 truncates to 0, so it means remove — the conversion is not a
    // "non-zero float is true" test.
    rig.FlagFloat(0.5f);
    CHECK_FALSE(rig.obj.AddMode());
    rig.Value(60);
    CHECK(rig.obj.Count() == 0);
  }

  // ─── the list form, and its order ───────────────────────────────────────────

  TEST_CASE("bag: a list reads as <value> <flag>, Max's order (#495)") {
    // Max's list method: "the first list item was sent to the left inlet and
    // the second list item was sent to the right inlet". Issue #495's summary
    // has it the other way round; this is the assertion that keeps a ported
    // Max patch meaning what it meant.
    Rig rig;
    rig.List("60 1");
    CHECK(rig.obj.Count() == 1);
    CHECK(rig.obj.Contains(60));
    CHECK_FALSE(rig.obj.Contains(1));

    rig.List("60 0");
    CHECK(rig.obj.Count() == 0);
  }

  TEST_CASE("bag: a list leaves its flag behind for later bare numbers (#495)") {
    Rig rig;
    rig.List("60 1");
    CHECK(rig.obj.AddMode());
    rig.Value(62);
    CHECK(rig.obj.Count() == 2);
    CHECK(rig.obj.Contains(62));

    rig.List("60 0");
    CHECK_FALSE(rig.obj.AddMode());
    rig.Value(62);
    CHECK(rig.obj.Count() == 0);
  }

  TEST_CASE("bag: a one-item list uses the flag already set (#495)") {
    Rig rig;
    rig.Flag(1);
    rig.List("60");
    CHECK(rig.obj.Contains(60));
  }

  TEST_CASE("bag: items past the second are ignored (#495)") {
    // Max's list method reads "any list composed of two numbers".
    Rig rig;
    rig.List("60 1 62 64");
    CHECK(rig.obj.Count() == 1);
    CHECK(rig.obj.Contains(60));
  }

  TEST_CASE("bag: a message that does not start with a number is ignored (#495)") {
    // Max documents no `anything` method for bag.
    Rig rig;
    rig.Flag(1);
    rig.List("hello 1");
    CHECK(rig.obj.Count() == 0);
    CHECK(rig.out.ints.empty());
  }

  // ─── bang, cut, length, clear ───────────────────────────────────────────────

  TEST_CASE("bag: a bang sends every number, newest first (#495)") {
    // Max: "all the numbers stored in bag are sent out one at a time, in
    // reverse order from that in which they were stored."
    Rig rig;
    rig.Add(60);
    rig.Add(62);
    rig.Add(64);
    rig.out.reset();

    rig.Bang();
    REQUIRE(rig.out.ints.size() == 3);
    CHECK(rig.out.ints[0] == 64);
    CHECK(rig.out.ints[1] == 62);
    CHECK(rig.out.ints[2] == 60);
    // The collection is not consumed by reading it.
    CHECK(rig.obj.Count() == 3);
  }

  TEST_CASE("bag: cut sends the oldest number and deletes it (#495)") {
    // The other end of the collection from a bang, and Max pins both.
    Rig rig;
    rig.Add(60);
    rig.Add(62);
    rig.Add(64);
    rig.out.reset();

    rig.List("cut");
    REQUIRE(rig.out.ints.size() == 1);
    CHECK(rig.out.ints[0] == 60);
    CHECK(rig.obj.Count() == 2);
    CHECK_FALSE(rig.obj.Contains(60));

    // What is left keeps its order, so the next cut takes the next-oldest.
    rig.out.reset();
    rig.List("cut");
    REQUIRE(rig.out.ints.size() == 1);
    CHECK(rig.out.ints[0] == 62);
  }

  TEST_CASE("bag: cut on an empty collection sends nothing (#495)") {
    Rig rig;
    rig.List("cut");
    CHECK(rig.out.ints.empty());
    CHECK(rig.out.otherKinds == 0);
  }

  TEST_CASE("bag: length reports the count as an int (#495)") {
    Rig rig;
    rig.List("length");
    REQUIRE(rig.out.ints.size() == 1);
    CHECK(rig.out.ints[0] == 0);

    rig.Add(60);
    rig.Add(62);
    rig.out.reset();
    rig.List("length");
    REQUIRE(rig.out.ints.size() == 1);
    CHECK(rig.out.ints[0] == 2);
  }

  TEST_CASE("bag: clear empties the collection silently (#495)") {
    Rig rig;
    rig.Add(60);
    rig.Add(62);
    rig.out.reset();

    rig.List("clear");
    CHECK(rig.obj.Count() == 0);
    CHECK(rig.out.ints.empty());

    rig.Bang();
    CHECK(rig.out.ints.empty());
  }

  // ─── duplicates ─────────────────────────────────────────────────────────────

  TEST_CASE("bag: with no argument it holds only one of each number (#495)") {
    // Max: "If there is no argument, bag will store only one of each number at
    // a time."
    Rig rig;
    CHECK_FALSE(rig.obj.StoresDuplicates());
    rig.Add(60);
    rig.Add(60);
    CHECK(rig.obj.Count() == 1);
    CHECK(rig.obj.CountOf(60) == 1);

    // And one remove is enough to empty it again.
    rig.Drop(60);
    CHECK(rig.obj.Count() == 0);
  }

  TEST_CASE("bag: a repeated add keeps the position the entry already had (#495)") {
    // "Holds only one of each number at a time" is a statement about the
    // contents, not about their order, so re-adding must not move the entry to
    // the front and change what the next bang sends first.
    Rig rig;
    rig.Add(60);
    rig.Add(62);
    rig.Add(60);
    rig.out.reset();

    rig.Bang();
    REQUIRE(rig.out.ints.size() == 2);
    CHECK(rig.out.ints[0] == 62);
    CHECK(rig.out.ints[1] == 60);
  }

  TEST_CASE("bag: any creation argument turns duplicates on (#495)") {
    // Max: "The presence of any symbol argument causes the bag to store
    // duplicate values." The argument is read for its presence, never its
    // value.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* bag = p.CreateObject(YSE::OBJ::G_BAG, "dup");
    REQUIRE(bag != nullptr);

    bag->SetIntData(1, 1);
    bag->SetIntData(0, 60);
    bag->SetIntData(0, 60);

    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    p.Connect(bag, 0, &sinkHandle, 0);
    bag->SetListData(0, "length");
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 2);
  }

  TEST_CASE("bag: with duplicates on, one remove takes one instance (#495)") {
    gBag obj;
    obj.SetParams("dup");
    CHECK(obj.StoresDuplicates());

    obj.GetInlet(1)->SetInt(1, YSE::T_GUI);
    obj.GetInlet(0)->SetInt(60, YSE::T_GUI);
    obj.GetInlet(0)->SetInt(60, YSE::T_GUI);
    CHECK(obj.CountOf(60) == 2);

    obj.GetInlet(1)->SetInt(0, YSE::T_GUI);
    obj.GetInlet(0)->SetInt(60, YSE::T_GUI);
    CHECK(obj.CountOf(60) == 1);
  }

  TEST_CASE("bag: a remove takes the newest matching instance (#495)") {
    // Observable once duplicates are on, and Max leaves the choice open: a bag
    // holding 60 62 60 dumps differently depending on which 60 goes. Taking the
    // newest makes an add and a remove of the same value an exact undo of each
    // other.
    Recorder out;
    gBag obj;
    obj.SetParams("dup");
    obj.ConnectOutlet(out.GetInlet(0), 0);

    obj.GetInlet(1)->SetInt(1, YSE::T_GUI);
    obj.GetInlet(0)->SetInt(60, YSE::T_GUI);
    obj.GetInlet(0)->SetInt(62, YSE::T_GUI);
    obj.GetInlet(0)->SetInt(60, YSE::T_GUI);

    obj.GetInlet(1)->SetInt(0, YSE::T_GUI);
    obj.GetInlet(0)->SetInt(60, YSE::T_GUI);

    // Storage order is now [60, 62] — the *older* 60 survived — so a bang, which
    // is newest first, sends 62 and then 60. Removing the oldest instead would
    // have left [62, 60] and sent them the other way round.
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(out.ints.size() == 2);
    CHECK(out.ints[0] == 62);
    CHECK(out.ints[1] == 60);
  }

  TEST_CASE("bag: clearing the argument turns duplicates off again (#495)") {
    // The clear callback is what makes SetParams("") return the object to Max's
    // no-argument shape rather than leaving the previous mode in place.
    gBag obj;
    obj.SetParams("dup");
    REQUIRE(obj.StoresDuplicates());
    obj.SetParams("");
    CHECK_FALSE(obj.StoresDuplicates());
  }

  // ─── bounds ─────────────────────────────────────────────────────────────────

  TEST_CASE("bag: an add past the capacity is refused, not grown (#495)") {
    gBag obj;
    obj.SetParams("dup");
    obj.GetInlet(1)->SetInt(1, YSE::T_GUI);

    for (std::size_t i = 0; i < gBag::MAX_ENTRIES + 10; i++) {
      obj.GetInlet(0)->SetInt((int)i, YSE::T_GUI);
    }
    CHECK(obj.Count() == gBag::MAX_ENTRIES);

    // Refused whole: the entries that did fit are the first ones, untouched.
    CHECK(obj.ValueAt(0) == 0);
    CHECK(obj.ValueAt(gBag::MAX_ENTRIES - 1) == (int)gBag::MAX_ENTRIES - 1);
  }

  // ─── what a save carries ────────────────────────────────────────────────────

  TEST_CASE("bag: the duplicate flag survives a DumpJSON / ParseJSON round trip (#495)") {
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_BAG, "dup") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".bag") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".bag");
    loaded.Connect(copy, 0, &sinkHandle, 0);

    // Read the mode back through the object's own behaviour rather than an
    // accessor: what has to survive is the bag a patch can use.
    copy->SetIntData(1, 1);
    copy->SetIntData(0, 60);
    copy->SetIntData(0, 60);
    copy->SetListData(0, "length");
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 2);
  }

  TEST_CASE("bag: the contents deliberately do not survive a save (#495)") {
    // Max's bag has no "save data with patcher" flag — that is coll's, which is
    // why .coll and not this object is what made pObject grow a state hook. A
    // reloaded patch whose bag came back holding the notes that were down when
    // it was saved would be holding notes nothing is sounding.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* bag = src.CreateObject(YSE::OBJ::G_BAG);
    REQUIRE(bag != nullptr);
    bag->SetIntData(1, 1);
    bag->SetIntData(0, 60);
    bag->SetIntData(0, 62);

    const std::string json = src.DumpJSON();
    CHECK(json.find("\"state\"") == std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);

    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    loaded.Connect(copy, 0, &sinkHandle, 0);
    copy->SetListData(0, "length");
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 0);
  }

  // ─── end to end, through a real patcher graph ───────────────────────────────

  TEST_CASE("bag: a held-note tracker answers what is sounding (#495)") {
    // The use case the object exists for, run through a real graph rather than
    // a directly wired outlet: note-ons add, the matching note-offs remove, and
    // a bang at any moment hands back exactly the notes still down — as ints a
    // transposer downstream can actually add to. A one-element list here would
    // silently do nothing at the .+.
    Recorder raw;
    MultiSink sink;
    YSE::pHandle rawHandle(&raw);
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* bag = p.CreateObject(YSE::OBJ::G_BAG);
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::G_ADD, "12");
    REQUIRE(bag != nullptr);
    REQUIRE(add != nullptr);
    p.Connect(bag, 0, &rawHandle, 0);
    p.Connect(bag, 0, add, 0);
    p.Connect(add, 0, &sinkHandle, 0);

    // Three keys down, spelled the way a patch would: the list form carries the
    // flag with the note.
    bag->SetListData(0, "60 1");
    bag->SetListData(0, "64 1");
    bag->SetListData(0, "67 1");
    // The middle one lifts again.
    bag->SetListData(0, "64 0");
    CHECK(raw.ints.empty()); // adding and removing are silent

    bag->SetBang(0);
    REQUIRE(raw.ints.size() == 2);
    CHECK(raw.ints[0] == 67); // newest first
    CHECK(raw.ints[1] == 60);
    // `.+` answers in floats whatever it was given, so what this pins is that
    // the numbers reached its numeric inlet at all: a list would have been
    // ignored there and the sink never touched. The last value through wins.
    CHECK(sink.gotFloat);
    CHECK(sink.floatValue == doctest::Approx(72.f));
  }

  TEST_CASE("bag: a .trigger drives cut and length round a real graph (#495)") {
    // The queue use of the object: `cut` pops the oldest, and the count that
    // comes back proves the pop actually left the collection. Driven from a
    // .trigger so the two messages arrive the way a patch would send them,
    // right to left.
    Recorder out;
    YSE::pHandle outHandle(&out);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* bag = p.CreateObject(YSE::OBJ::G_BAG);
    YSE::pHandle* trig = p.CreateObject(YSE::OBJ::G_TRIGGER, "length cut");
    REQUIRE(bag != nullptr);
    REQUIRE(trig != nullptr);
    // .trigger fires its outlets right to left, so outlet 1 (`cut`) reaches the
    // bag before outlet 0 (`length`) does.
    p.Connect(trig, 0, bag, 0);
    p.Connect(trig, 1, bag, 0);
    p.Connect(bag, 0, &outHandle, 0);

    bag->SetListData(0, "60 1");
    bag->SetListData(0, "62 1");
    bag->SetListData(0, "64 1");
    out.reset();

    trig->SetBang(0);
    // `cut` sent the oldest number, then `length` reported what was left.
    REQUIRE(out.ints.size() == 2);
    CHECK(out.ints[0] == 60);
    CHECK(out.ints[1] == 2);
  }

} // TEST_SUITE
