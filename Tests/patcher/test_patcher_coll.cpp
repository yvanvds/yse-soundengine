// Tests for .coll (issue #494) — the patcher's first store of more than one
// thing.
//
// Four things are worth pinning here, and they are the four an implementation
// can get wrong without ever crashing:
//
//   - **addresses are numbers or symbols, and never each other.** The address 1
//     and the address `one` are different entries, a float address addresses the
//     int entry Max says it does, and a numeric address need be neither
//     contiguous nor in order.
//   - **storage order is the order things were stored in.** dump, next and prev
//     all walk it, and Max sends entries "in the order in which they are
//     stored", so an insert or a delete that quietly reorders the table breaks
//     every sequence a patch has written into one.
//   - **what leaves the data outlet is the message in the kind it is.** A stored
//     `60` has to arrive downstream as an int a `.+` can add to, not as a
//     one-element list — which is a property of the whole graph and not of the
//     object alone, so it is asserted through a real patcher.
//   - **the contents survive a save.** They cannot ride the parameter string, so
//     this is the object that made pObject grow a state hook; a reloaded
//     collection has to answer the same lookups the saved one did.
//
// No audio device and no engine of its own, except where a real patcher graph is
// the point.

#include <doctest/doctest.h>
#include <string>
#include <vector>

#include "patcher/genericObjects/gColl.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::gColl;

namespace {

  // Records everything it receives, in order, tagged by which outlet it came
  // from — so a multi-outlet sequence (address then data, entry after entry)
  // reads back as the exact list of sends. MultiSink only keeps the last of
  // each kind, which cannot tell a dump of three entries from a dump of one.
  struct Recorder : YSE::PATCHER::pObject {
    std::vector<std::string>* log = nullptr;
    std::string tag;

    Recorder() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { log->push_back(tag + ":bang"); });
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { log->push_back(tag + ":i " + std::to_string(v)); });
      inputs.back().RegisterFloat(
          [this](float v, int, YSE::THREAD) { log->push_back(tag + ":f " + std::to_string(v)); });
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { log->push_back(tag + ":l " + v); });
    }
    const char* Type() const override {
      return "recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // A standalone .coll with a sink on each outlet. Standalone on purpose where
  // the patcher is not the point: a test that needed one could not tell a
  // refused message from a message the patcher never delivered.
  struct Rig {
    MultiSink data;
    MultiSink address;
    MultiSink done;
    gColl obj;

    Rig() {
      obj.ConnectOutlet(data.GetInlet(0), 0);
      obj.ConnectOutlet(address.GetInlet(0), 1);
      obj.ConnectOutlet(done.GetInlet(0), 2);
    }

    void reset() {
      data.reset();
      address.reset();
      done.reset();
    }

    void List(const std::string& message) {
      obj.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Int(int value) {
      obj.GetInlet(0)->SetInt(value, YSE::T_GUI);
    }
    void Float(float value) {
      obj.GetInlet(0)->SetFloat(value, YSE::T_GUI);
    }
    void Bang() {
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("coll: registered, one inlet and three outlets (#494)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* coll = p.CreateObject(YSE::OBJ::G_COLL);
    REQUIRE(coll != nullptr);
    CHECK(std::string(coll->Type()) == ".coll");
    CHECK(coll->GetInputs() == 1);
    CHECK(coll->GetOutputs() == 3);
  }

  TEST_CASE("coll: appears in the registry's name list (#494)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_COLL)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("coll: the inlet takes bang, int, float and list (#494)") {
    gColl obj;
    const unsigned int accepted = obj.GetInlet(0)->GetAcceptedTypes();
    CHECK((accepted & YSE::PATCHER::IT_BANG) != 0);
    CHECK((accepted & YSE::PATCHER::IT_INT) != 0);
    CHECK((accepted & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((accepted & YSE::PATCHER::IT_LIST) != 0);
  }

  TEST_CASE("coll: a fresh collection is empty and sends nothing (#494)") {
    Rig rig;
    CHECK(rig.obj.Count() == 0);

    // A bang with nothing stored is inert rather than a source of empty
    // messages — the rule the whole message family keeps.
    rig.Bang();
    CHECK_FALSE(rig.data.gotList);
    CHECK_FALSE(rig.data.gotInt);
    CHECK_FALSE(rig.address.gotInt);
  }

  TEST_CASE("coll: Calculate() sends nothing (#494)") {
    // The object is driven by its inlet; one that emitted from Calculate() would
    // re-send on every DSP tick after a message arrived.
    Rig rig;
    rig.List("store a 1");
    rig.reset();

    rig.obj.Calculate(YSE::T_DSP);
    CHECK_FALSE(rig.data.gotList);
    CHECK_FALSE(rig.data.gotInt);
    CHECK_FALSE(rig.address.gotInt);
    CHECK_FALSE(rig.done.gotBang);
  }

  // ─── storing and recalling ──────────────────────────────────────────────────

  TEST_CASE("coll: store writes a symbol address and the symbol recalls it (#494)") {
    Rig rig;
    rig.List("store foo 1 2 3");
    CHECK(rig.obj.Count() == 1);
    CHECK(rig.obj.Lookup("foo") == "1 2 3");

    rig.reset();
    rig.List("foo");
    CHECK(rig.data.gotList);
    CHECK(rig.data.listValue == "1 2 3");
  }

  TEST_CASE("coll: a list whose first item is a number stores the rest (#494)") {
    // Max's list method: "the first value is used as the address at which to
    // store the remaining items in the list".
    Rig rig;
    rig.List("5 60 100");
    CHECK(rig.obj.Count() == 1);
    CHECK(rig.obj.KeyAt(0) == "5");
    CHECK(rig.obj.ValueAt(0) == "60 100");

    rig.reset();
    rig.Int(5);
    CHECK(rig.data.gotList);
    CHECK(rig.data.listValue == "60 100");
  }

  TEST_CASE("coll: a stored single number comes back as that int or float (#494)") {
    // `.route`'s rule for a remainder: a lone number is that number, not a list
    // of one, and the spelling decides int or float.
    Rig rig;
    rig.List("store a 60");
    rig.List("store b 60.5");
    rig.List("store c word");

    rig.reset();
    rig.List("a");
    CHECK(rig.data.gotInt);
    CHECK(rig.data.intValue == 60);

    rig.reset();
    rig.List("b");
    CHECK(rig.data.gotFloat);
    CHECK(rig.data.floatValue == doctest::Approx(60.5f));

    // A lone symbol has no type of its own here, so it stays a one-element list
    // rather than picking up Max's `symbol` prefix.
    rig.reset();
    rig.List("c");
    CHECK(rig.data.gotList);
    CHECK(rig.data.listValue == "word");
  }

  TEST_CASE("coll: a store at an address that exists replaces it (#494)") {
    Rig rig;
    rig.List("store foo 1");
    rig.List("store foo 2");
    CHECK(rig.obj.Count() == 1);
    CHECK(rig.obj.Lookup("foo") == "2");

    rig.List("7 a");
    rig.List("7 b");
    CHECK(rig.obj.Count() == 2);
    CHECK(rig.obj.Lookup("7") == "b");
  }

  TEST_CASE("coll: a numeric address and a symbol address never collide (#494)") {
    Rig rig;
    rig.List("1 numeric");
    rig.List("store one symbolic");
    CHECK(rig.obj.Count() == 2);
    CHECK(rig.obj.Lookup("1") == "numeric");
    CHECK(rig.obj.Lookup("one") == "symbolic");
  }

  TEST_CASE("coll: a float address reaches the int entry (#494)") {
    // Max's float method addresses the same entries the int method does — "a
    // float is converted to an int".
    Rig rig;
    rig.List("3 stored");

    rig.reset();
    rig.Float(3.7f);
    CHECK(rig.data.gotList);
    CHECK(rig.data.listValue == "stored");
  }

  TEST_CASE("coll: a lookup that misses sends nothing (#494)") {
    Rig rig;
    rig.List("store foo 1");

    rig.reset();
    rig.List("bar");
    CHECK_FALSE(rig.data.gotList);
    CHECK_FALSE(rig.data.gotInt);

    rig.reset();
    rig.Int(99);
    CHECK_FALSE(rig.data.gotList);
    CHECK_FALSE(rig.data.gotInt);
  }

  TEST_CASE("coll: a plain lookup sends no address (#494)") {
    // Max sends the address only "whenever a message out the 1st outlet is
    // triggered by bang, dump, next, prev, or sub" — a patch that looked an
    // address up already had it.
    Rig rig;
    rig.List("store foo 1 2");

    rig.reset();
    rig.List("foo");
    CHECK(rig.data.gotList);
    CHECK_FALSE(rig.address.gotInt);
    CHECK_FALSE(rig.address.gotList);
  }

  TEST_CASE("coll: items after a leading symbol are ignored, not stored (#494)") {
    // Max's `anything` routes to symbol handling: the leading symbol addresses
    // an entry, it does not name a new one. `store` is how a symbol address is
    // written, and the test that matters is that nothing was created.
    Rig rig;
    rig.List("foo 1 2 3");
    CHECK(rig.obj.Count() == 0);
  }

  // ─── the pointer ────────────────────────────────────────────────────────────

  TEST_CASE("coll: bang sends the address before the data (#494)") {
    // Max's right-to-left outlet order, and the whole reason the two outlets
    // exist separately: a patch that reads the address has to have it in hand
    // before the data it belongs to arrives.
    std::vector<std::string> log;
    Recorder dataSink;
    Recorder addressSink;
    dataSink.log = &log;
    dataSink.tag = "data";
    addressSink.log = &log;
    addressSink.tag = "addr";

    gColl obj;
    obj.ConnectOutlet(dataSink.GetInlet(0), 0);
    obj.ConnectOutlet(addressSink.GetInlet(0), 1);

    obj.GetInlet(0)->SetList("store foo 1 2", YSE::T_GUI);
    log.clear();

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == "addr:l foo");
    CHECK(log[1] == "data:l 1 2");
  }

  TEST_CASE("coll: a numeric address leaves outlet 1 as an int (#494)") {
    Rig rig;
    rig.List("9 payload");

    rig.reset();
    rig.Bang();
    CHECK(rig.address.gotInt);
    CHECK(rig.address.intValue == 9);
  }

  TEST_CASE("coll: next walks storage order and wraps (#494)") {
    std::vector<std::string> log;
    Recorder dataSink;
    dataSink.log = &log;
    dataSink.tag = "d";

    gColl obj;
    obj.ConnectOutlet(dataSink.GetInlet(0), 0);

    // Stored out of address order on purpose: dump / next / prev walk storage
    // order, not address order.
    obj.GetInlet(0)->SetList("10 ten", YSE::T_GUI);
    obj.GetInlet(0)->SetList("2 two", YSE::T_GUI);
    obj.GetInlet(0)->SetList("store name sym", YSE::T_GUI);
    log.clear();

    for (int i = 0; i < 4; i++) {
      obj.GetInlet(0)->SetList("next", YSE::T_GUI);
    }
    REQUIRE(log.size() == 4);
    CHECK(log[0] == "d:l ten");
    CHECK(log[1] == "d:l two");
    CHECK(log[2] == "d:l sym");
    CHECK(log[3] == "d:l ten"); // wrapped
  }

  TEST_CASE("coll: prev walks backwards and wraps (#494)") {
    std::vector<std::string> log;
    Recorder dataSink;
    dataSink.log = &log;
    dataSink.tag = "d";

    gColl obj;
    obj.ConnectOutlet(dataSink.GetInlet(0), 0);
    obj.GetInlet(0)->SetList("0 a", YSE::T_GUI);
    obj.GetInlet(0)->SetList("1 b", YSE::T_GUI);
    obj.GetInlet(0)->SetList("2 c", YSE::T_GUI);
    log.clear();

    obj.GetInlet(0)->SetList("prev", YSE::T_GUI); // at 0, sends a, wraps to c
    obj.GetInlet(0)->SetList("prev", YSE::T_GUI);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == "d:l a");
    CHECK(log[1] == "d:l c");
  }

  TEST_CASE("coll: goto, start and end move the pointer without output (#494)") {
    Rig rig;
    rig.List("0 a");
    rig.List("1 b");
    rig.List("store z c");

    rig.reset();
    rig.List("goto z");
    CHECK_FALSE(rig.data.gotList); // Max: "does not trigger output"
    CHECK(rig.obj.Pointer() == 2);

    rig.reset();
    rig.Bang();
    CHECK(rig.data.listValue == "c");

    rig.List("start");
    CHECK(rig.obj.Pointer() == 0);
    rig.List("end");
    CHECK(rig.obj.Pointer() == 2);

    // An address nothing is stored at leaves the pointer where it was.
    rig.List("goto nowhere");
    CHECK(rig.obj.Pointer() == 2);
  }

  // ─── dump and length ────────────────────────────────────────────────────────

  TEST_CASE("coll: dump sends every entry in storage order, then bangs (#494)") {
    std::vector<std::string> log;
    Recorder dataSink;
    Recorder addressSink;
    Recorder doneSink;
    dataSink.log = &log;
    dataSink.tag = "d";
    addressSink.log = &log;
    addressSink.tag = "a";
    doneSink.log = &log;
    doneSink.tag = "done";

    gColl obj;
    obj.ConnectOutlet(dataSink.GetInlet(0), 0);
    obj.ConnectOutlet(addressSink.GetInlet(0), 1);
    obj.ConnectOutlet(doneSink.GetInlet(0), 2);

    obj.GetInlet(0)->SetList("7 seven", YSE::T_GUI);
    obj.GetInlet(0)->SetList("store word hello", YSE::T_GUI);
    log.clear();

    obj.GetInlet(0)->SetList("dump", YSE::T_GUI);
    REQUIRE(log.size() == 5);
    CHECK(log[0] == "a:i 7");
    CHECK(log[1] == "d:l seven");
    CHECK(log[2] == "a:l word");
    CHECK(log[3] == "d:l hello");
    CHECK(log[4] == "done:bang");
  }

  TEST_CASE("coll: dump on an empty collection still bangs (#494)") {
    Rig rig;
    rig.List("dump");
    CHECK(rig.done.gotBang);
    CHECK_FALSE(rig.data.gotList);
  }

  TEST_CASE("coll: length sends the entry count as an int (#494)") {
    Rig rig;
    rig.reset();
    rig.List("length");
    CHECK(rig.data.gotInt);
    CHECK(rig.data.intValue == 0);

    rig.List("1 a");
    rig.List("2 b");
    rig.reset();
    rig.List("length");
    CHECK(rig.data.gotInt);
    CHECK(rig.data.intValue == 2);
  }

  // ─── editing ────────────────────────────────────────────────────────────────

  TEST_CASE("coll: remove drops one entry and leaves the numbering alone (#494)") {
    Rig rig;
    rig.List("1 a");
    rig.List("2 b");
    rig.List("3 c");

    rig.List("remove 2");
    CHECK(rig.obj.Count() == 2);
    CHECK(rig.obj.Lookup("2") == "");
    // Max's `remove` is the one that does *not* renumber.
    CHECK(rig.obj.Lookup("3") == "c");
    CHECK(rig.obj.KeyAt(0) == "1");
    CHECK(rig.obj.KeyAt(1) == "3");
  }

  TEST_CASE("coll: delete brings every higher numeric address down by one (#494)") {
    Rig rig;
    rig.List("1 a");
    rig.List("2 b");
    rig.List("3 c");
    rig.List("store name sym");

    rig.List("delete 2");
    CHECK(rig.obj.Count() == 3);
    CHECK(rig.obj.Lookup("1") == "a");
    // What was at 3 is now at 2 — Max: "all higher numbered addresses are
    // decremented by 1".
    CHECK(rig.obj.Lookup("2") == "c");
    CHECK(rig.obj.Lookup("3") == "");
    // A symbol address is untouched by numeric renumbering.
    CHECK(rig.obj.Lookup("name") == "sym");
  }

  TEST_CASE("coll: insert takes the address and pushes the rest up (#494)") {
    Rig rig;
    rig.List("1 a");
    rig.List("2 b");

    rig.List("insert 1 fresh");
    CHECK(rig.obj.Count() == 3);
    CHECK(rig.obj.Lookup("1") == "fresh");
    CHECK(rig.obj.Lookup("2") == "a");
    CHECK(rig.obj.Lookup("3") == "b");

    // And it lands in front of them in storage order, not at the end, so a dump
    // still reads as the sequence the addresses spell.
    CHECK(rig.obj.KeyAt(0) == "1");
    CHECK(rig.obj.ValueAt(0) == "fresh");
    CHECK(rig.obj.ValueAt(1) == "a");
    CHECK(rig.obj.ValueAt(2) == "b");
  }

  TEST_CASE("coll: append stores one past the highest numeric address (#494)") {
    Rig rig;
    rig.List("append first");
    CHECK(rig.obj.Lookup("0") == "first");

    rig.List("5 five");
    rig.List("append last");
    CHECK(rig.obj.Lookup("6") == "last");
    CHECK(rig.obj.Count() == 3);
  }

  TEST_CASE("coll: clear empties the collection (#494)") {
    Rig rig;
    rig.List("1 a");
    rig.List("store b two");
    rig.List("clear");
    CHECK(rig.obj.Count() == 0);
    CHECK(rig.obj.Pointer() == 0);

    rig.reset();
    rig.Int(1);
    CHECK_FALSE(rig.data.gotList);

    // And the table is reusable after it, rather than merely empty.
    rig.List("1 again");
    CHECK(rig.obj.Lookup("1") == "again");
  }

  // ─── refusals ───────────────────────────────────────────────────────────────

  TEST_CASE("coll: a message longer than the entry is refused whole (#494)") {
    Rig rig;
    rig.List("store a keep");

    const std::string tooLong(gColl::VALUE_CAPACITY + 1, 'x');
    rig.List("store a " + tooLong);
    // Refused rather than truncated: half a message is a different message, and
    // what was already stored has to survive it untouched.
    CHECK(rig.obj.Lookup("a") == "keep");

    const std::string fits(gColl::VALUE_CAPACITY, 'x');
    rig.List("store a " + fits);
    CHECK(rig.obj.Lookup("a") == fits);
  }

  TEST_CASE("coll: an address longer than the key is refused (#494)") {
    Rig rig;
    const std::string tooLong(gColl::KEY_CAPACITY + 1, 'k');
    rig.List("store " + tooLong + " value");
    CHECK(rig.obj.Count() == 0);

    const std::string fits(gColl::KEY_CAPACITY, 'k');
    rig.List("store " + fits + " value");
    CHECK(rig.obj.Count() == 1);
    CHECK(rig.obj.Lookup(fits) == "value");
  }

  TEST_CASE("coll: a store past the capacity is refused, not grown (#494)") {
    // Growing the table would allocate on whichever thread the message arrived
    // on, which may be the audio thread — so the bound is the object's contract
    // rather than an implementation detail.
    Rig rig;
    for (std::size_t i = 0; i < gColl::MAX_ENTRIES; i++) {
      rig.List(std::to_string(i) + " v");
    }
    CHECK(rig.obj.Count() == gColl::MAX_ENTRIES);

    rig.List(std::to_string(gColl::MAX_ENTRIES) + " overflow");
    CHECK(rig.obj.Count() == gColl::MAX_ENTRIES);
    CHECK(rig.obj.Lookup(std::to_string(gColl::MAX_ENTRIES)) == "");

    // A replacement still works when the table is full: it writes into an entry
    // that already exists rather than needing a new one.
    rig.List("0 replaced");
    CHECK(rig.obj.Lookup("0") == "replaced");
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("coll: contents survive a DumpJSON / ParseJSON round trip (#494)") {
    // Sink before the patchers: they are torn down first, while the inlets they
    // are wired to still exist.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher src;
    src.create(2);
    YSE::pHandle* coll = src.CreateObject(YSE::OBJ::G_COLL);
    REQUIRE(coll != nullptr);
    coll->SetListData(0, "1 60 100");
    coll->SetListData(0, "store name hello world");
    coll->SetListData(0, "7 seven");

    const std::string json = src.DumpJSON();
    CHECK(json.find(".coll") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    // Read back through the outlet rather than through an accessor: what has to
    // survive is the collection a patch can use, not a member variable.
    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".coll");
    loaded.Connect(copy, 0, &sinkHandle, 0);

    copy->SetIntData(0, 1);
    CHECK(sink.gotList);
    CHECK(sink.listValue == "60 100");

    sink.reset();
    copy->SetListData(0, "name");
    CHECK(sink.gotList);
    CHECK(sink.listValue == "hello world");

    sink.reset();
    copy->SetIntData(0, 7);
    CHECK(sink.gotList);
    CHECK(sink.listValue == "seven");

    // Storage order survives too, which is what makes a reloaded dump replay the
    // sequence that was saved.
    sink.reset();
    copy->SetListData(0, "length");
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 3);
  }

  TEST_CASE("coll: an empty collection and other objects write no state key (#494)") {
    // The state hook is opt-in: an object without state of its own has to
    // serialise byte for byte the way it always did, or every saved patch grows
    // a null for every object in it.
    YSE::patcher p;
    p.create(2);
    REQUIRE(p.CreateObject(YSE::OBJ::G_FLOAT) != nullptr);
    REQUIRE(p.CreateObject(YSE::OBJ::G_COLL) != nullptr);
    CHECK(p.DumpJSON().find("\"state\"") == std::string::npos);
  }

  // ─── end to end, through a real patcher graph ───────────────────────────────

  TEST_CASE("coll: a stored number arrives downstream as a number (#494)") {
    // The property the object exists for, and one no unit test on the object
    // alone can show: a note table is only usable if what comes out of it can be
    // added to. A one-element list here would silently do nothing at the .+.
    std::vector<std::string> log;
    Recorder raw;
    raw.log = &log;
    raw.tag = "raw";
    MultiSink sink;
    YSE::pHandle rawHandle(&raw);
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* coll = p.CreateObject(YSE::OBJ::G_COLL);
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::G_ADD, "12");
    REQUIRE(coll != nullptr);
    REQUIRE(add != nullptr);
    p.Connect(coll, 0, &rawHandle, 0);
    p.Connect(coll, 0, add, 0);
    p.Connect(add, 0, &sinkHandle, 0);

    coll->SetListData(0, "store c3 60");
    coll->SetListData(0, "c3");
    // The entry left the data outlet as the int it was stored as, through a
    // real graph rather than a directly wired outlet.
    REQUIRE(log.size() == 1);
    CHECK(log[0] == "raw:i 60");
    // `.+` answers in floats whatever it was given, so what this pins is that
    // the entry reached its numeric inlet at all: a one-element list would have
    // been ignored there and the sink would never have been touched.
    CHECK(sink.gotFloat);
    CHECK(sink.floatValue == doctest::Approx(72.f));
  }

  TEST_CASE("coll: a dump drives a downstream chain entry by entry (#494)") {
    // The sequence use case run through the real thing: a table of note numbers
    // dumped into a transposer, with the addresses landing on their own branch.
    std::vector<std::string> log;
    Recorder notes;
    Recorder addresses;
    notes.log = &log;
    notes.tag = "note";
    addresses.log = &log;
    addresses.tag = "at";
    YSE::pHandle noteHandle(&notes);
    YSE::pHandle addressHandle(&addresses);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* coll = p.CreateObject(YSE::OBJ::G_COLL);
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::G_ADD, "12");
    REQUIRE(coll != nullptr);
    REQUIRE(add != nullptr);
    p.Connect(coll, 0, add, 0);
    p.Connect(add, 0, &noteHandle, 0);
    p.Connect(coll, 1, &addressHandle, 0);

    coll->SetListData(0, "0 60");
    coll->SetListData(0, "1 62");
    coll->SetListData(0, "2 64");
    log.clear();

    coll->SetListData(0, "dump");
    REQUIRE(log.size() == 6);
    CHECK(log[0] == "at:i 0");
    CHECK(log[1] == "note:f 72.000000");
    CHECK(log[2] == "at:i 1");
    CHECK(log[3] == "note:f 74.000000");
    CHECK(log[4] == "at:i 2");
    CHECK(log[5] == "note:f 76.000000");
  }

  TEST_CASE("coll: a .trigger steps the collection and the entries come back (#494)") {
    // How a patch actually drives one: an upstream object fires `next`, which is
    // a command the object has to read out of another outlet's list exactly as
    // it reads one handed in by the host. A command that only worked from the
    // host would make the object undrivable from inside a patch.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* step = p.CreateObject(YSE::OBJ::G_TRIGGER, "next");
    YSE::pHandle* coll = p.CreateObject(YSE::OBJ::G_COLL);
    REQUIRE(step != nullptr);
    REQUIRE(coll != nullptr);
    p.Connect(step, 0, coll, 0);
    p.Connect(coll, 0, &sinkHandle, 0);

    coll->SetListData(0, "0 first");
    coll->SetListData(0, "1 second");

    sink.reset();
    step->SetBang(0);
    CHECK(sink.gotList);
    CHECK(sink.listValue == "first");

    sink.reset();
    step->SetBang(0);
    CHECK(sink.gotList);
    CHECK(sink.listValue == "second");
  }
}
