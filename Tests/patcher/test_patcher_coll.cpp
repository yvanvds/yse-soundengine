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
//   - **a collection round-trips through a file, and never on the message
//     path.** Issue #683. A `read` arrives on whichever thread dispatched it, so
//     the proof that matters is not only that the entries come back but that
//     *nothing happens in the handler*: the collection is untouched until the
//     patcher renders a block. Asserted through a real patcherImplementation and
//     a real file on disk, because both halves — the background job and the
//     completion delivered into a dispatch frame — only exist there.
//
// No audio device and no engine of its own, except where a real patcher graph is
// the point.

#include <doctest/doctest.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "patcher/genericObjects/gColl.h"
#include "patcher/inlet.h"
#include "patcher/io/fileScheduler.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::gColl;
using YSE::PATCHER::patcherImplementation;

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

  // ─── file helpers (issue #683) ────────────────────────────────────────────

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

  // Drive a patcher until its file requests have landed. The two halves are
  // deterministic for different reasons: WaitIdle joins the background pool's
  // jobs (so no sleep and no polling), and the single Calculate is the dispatch
  // frame the completions are handed out in — there is deliberately no other
  // way for them to arrive.
  void SettleFiles(patcherImplementation& p) {
    YSE::PATCHER::fileScheduler* io = p.FileIO();
    REQUIRE(io != nullptr);
    io->WaitIdle();
    p.Calculate(YSE::T_DSP);
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("coll: registered, one inlet and four outlets (#494, #683)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* coll = p.CreateObject(YSE::OBJ::G_COLL);
    REQUIRE(coll != nullptr);
    CHECK(std::string(coll->Type()) == ".coll");
    CHECK(coll->GetInputs() == 1);
    // Three until #683; the file outlet is the fourth because it was appended
    // rather than inserted in Max's third position, so the dump outlet is still
    // outlet 2 and no patch saved against the three-outlet object has to be
    // rewired.
    CHECK(coll->GetOutputs() == 4);
    CHECK(coll->OutputDataType(2) == YSE::OUT_TYPE::BANG);
    CHECK(coll->OutputDataType(3) == YSE::OUT_TYPE::BANG);
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

  // ─── collection files, through a real patcher and a real disk (#683) ────────

  TEST_CASE("coll: write then read round-trips a collection through a file (#683)") {
    // The acceptance case, end to end: a real patcherImplementation (the
    // background job and the completion frame exist nowhere else), a real file
    // on disk, and the contents read back through the object's own outlets
    // rather than an accessor — what a patch can see is what has to survive.
    const std::string path = TempFile("yse_coll_roundtrip_683.txt");

    std::vector<std::string> log;
    Recorder data;
    Recorder address;
    Recorder file;
    data.log = &log;
    data.tag = "d";
    address.log = &log;
    address.tag = "a";
    file.log = &log;
    file.tag = "file";
    YSE::pHandle dataHandle(&data);
    YSE::pHandle addressHandle(&address);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* coll = p.CreateObject(YSE::OBJ::G_COLL, "");
    REQUIRE(coll != nullptr);
    p.Connect(coll, 0, &dataHandle, 0);
    p.Connect(coll, 1, &addressHandle, 0);
    p.Connect(coll, 3, &fileHandle, 0);

    coll->SetListData(0, "0 60 100");
    coll->SetListData(0, "1 62");
    coll->SetListData(0, "store name hello");

    coll->SetListData(0, "write " + path);
    SettleFiles(p);

    // Max's plain-text collection format, one `<address>, <message>;` record per
    // line, in storage order.
    CHECK(ReadWholeFile(path) == "0, 60 100;\n1, 62;\nname, hello;\n");
    // Max has no outlet for a finished write and neither does this.
    CHECK(log.empty());

    coll->SetListData(0, "clear");
    coll->SetListData(0, "dump");
    CHECK(log.empty());

    coll->SetListData(0, "read " + path);
    SettleFiles(p);
    // The read outlet fires once, and only after the contents are in place.
    REQUIRE(log.size() == 1);
    CHECK(log[0] == "file:bang");

    log.clear();
    coll->SetListData(0, "dump");
    REQUIRE(log.size() == 6);
    CHECK(log[0] == "a:i 0");
    CHECK(log[1] == "d:l 60 100");
    CHECK(log[2] == "a:i 1");
    CHECK(log[3] == "d:i 62");
    CHECK(log[4] == "a:l name");
    CHECK(log[5] == "d:l hello");

    std::error_code ec;
    std::filesystem::remove(path, ec);
  }

  TEST_CASE("coll: read does nothing in the message handler (#683)") {
    // The reason the plumbing exists. A `read` may be dispatched on the audio
    // callback, so the handler must not open anything — which is observable:
    // the collection is still empty when the message returns, and only a
    // rendered block puts the file in it.
    const std::string path = TempFile("yse_coll_deferred_683.txt");
    {
      std::ofstream out(path, std::ios::binary);
      out << "0, 60;\n";
    }

    std::vector<std::string> log;
    Recorder data;
    Recorder file;
    data.log = &log;
    data.tag = "d";
    file.log = &log;
    file.tag = "file";
    YSE::pHandle dataHandle(&data);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* coll = p.CreateObject(YSE::OBJ::G_COLL, "");
    REQUIRE(coll != nullptr);
    p.Connect(coll, 0, &dataHandle, 0);
    p.Connect(coll, 3, &fileHandle, 0);

    coll->SetListData(0, "read " + path);
    // Nothing yet: no entry, no bang. The request is a claim on a slot and the
    // disk has not been touched on this thread.
    coll->SetListData(0, "length");
    REQUIRE(log.size() == 1);
    CHECK(log[0] == "d:i 0");
    REQUIRE(p.FileIO() != nullptr);
    CHECK(p.FileIO()->PendingCount() == 1);

    log.clear();
    SettleFiles(p);
    REQUIRE(log.size() == 1);
    CHECK(log[0] == "file:bang");
    CHECK(p.FileIO()->PendingCount() == 0);

    log.clear();
    coll->SetListData(0, "length");
    REQUIRE(log.size() == 1);
    CHECK(log[0] == "d:i 1");

    std::error_code ec;
    std::filesystem::remove(path, ec);
  }

  TEST_CASE("coll: a read replaces what was held (#683)") {
    // Max's read loads a file "into the collection"; it is not a merge. A patch
    // that reloads a preset file has to get the preset, not the preset plus
    // whatever it had been editing.
    const std::string path = TempFile("yse_coll_replace_683.txt");
    {
      std::ofstream out(path, std::ios::binary);
      out << "5, five;\n";
    }

    std::vector<std::string> log;
    Recorder data;
    data.log = &log;
    data.tag = "d";
    YSE::pHandle dataHandle(&data);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* coll = p.CreateObject(YSE::OBJ::G_COLL, "");
    REQUIRE(coll != nullptr);
    p.Connect(coll, 0, &dataHandle, 0);

    coll->SetListData(0, "0 gone");
    coll->SetListData(0, "1 also gone");
    coll->SetListData(0, "read " + path);
    SettleFiles(p);

    log.clear();
    coll->SetListData(0, "length");
    coll->SetListData(0, "5");
    coll->SetListData(0, "0");
    REQUIRE(log.size() == 2);
    CHECK(log[0] == "d:i 1");
    CHECK(log[1] == "d:l five");

    std::error_code ec;
    std::filesystem::remove(path, ec);
  }

  TEST_CASE("coll: readagain and writeagain reuse the last name (#683)") {
    // Max's bare read / write open a file dialog; a headless patcher has none,
    // so the `again` forms and the bare forms are the same thing here — and
    // both have to remember a name given on a message path without allocating.
    const std::string path = TempFile("yse_coll_again_683.txt");

    std::vector<std::string> log;
    Recorder data;
    data.log = &log;
    data.tag = "d";
    YSE::pHandle dataHandle(&data);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* coll = p.CreateObject(YSE::OBJ::G_COLL, "");
    REQUIRE(coll != nullptr);
    p.Connect(coll, 0, &dataHandle, 0);

    coll->SetListData(0, "0 first");
    coll->SetListData(0, "write " + path);
    SettleFiles(p);
    CHECK(ReadWholeFile(path) == "0, first;\n");

    // Same name, no argument.
    coll->SetListData(0, "1 second");
    coll->SetListData(0, "writeagain");
    SettleFiles(p);
    CHECK(ReadWholeFile(path) == "0, first;\n1, second;\n");

    coll->SetListData(0, "clear");
    coll->SetListData(0, "read " + path);
    SettleFiles(p);
    coll->SetListData(0, "clear");
    coll->SetListData(0, "readagain");
    SettleFiles(p);

    log.clear();
    coll->SetListData(0, "length");
    REQUIRE(log.size() == 1);
    CHECK(log[0] == "d:i 2");

    std::error_code ec;
    std::filesystem::remove(path, ec);
  }

  TEST_CASE("coll: readagain with nothing read yet does nothing (#683)") {
    // Max falls back to its Open dialog; there is none here, so the honest
    // behaviour is silence rather than a guess at a filename.
    std::vector<std::string> log;
    Recorder file;
    file.log = &log;
    file.tag = "file";
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* coll = p.CreateObject(YSE::OBJ::G_COLL, "");
    REQUIRE(coll != nullptr);
    p.Connect(coll, 3, &fileHandle, 0);

    coll->SetListData(0, "readagain");
    coll->SetListData(0, "read");
    coll->SetListData(0, "writeagain");
    REQUIRE(p.FileIO() != nullptr);
    CHECK(p.FileIO()->PendingCount() == 0);
    SettleFiles(p);
    CHECK(log.empty());
  }

  TEST_CASE("coll: a read of a missing file leaves the collection alone (#683)") {
    // A failure is only discoverable on the background pool, so it arrives as a
    // completion rather than as a refusal — and it must not fire the outlet a
    // patch uses to mean "the file is loaded".
    const std::string path = TempFile("yse_coll_no_such_file_683.txt");

    std::vector<std::string> log;
    Recorder data;
    Recorder file;
    data.log = &log;
    data.tag = "d";
    file.log = &log;
    file.tag = "file";
    YSE::pHandle dataHandle(&data);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* coll = p.CreateObject(YSE::OBJ::G_COLL, "");
    REQUIRE(coll != nullptr);
    p.Connect(coll, 0, &dataHandle, 0);
    p.Connect(coll, 3, &fileHandle, 0);

    coll->SetListData(0, "0 kept");
    coll->SetListData(0, "read " + path);
    SettleFiles(p);
    CHECK(log.empty());

    coll->SetListData(0, "length");
    REQUIRE(log.size() == 1);
    CHECK(log[0] == "d:i 1");
    // And the slot came back, so a failed read does not leak the table away.
    CHECK(p.FileIO()->PendingCount() == 0);
  }

  TEST_CASE("coll: a file with more records than the collection holds keeps the first 256 (#683)") {
    // The bound is the object's contract: growing the table would allocate on
    // whichever thread the message arrived on. Truncating at the bound and
    // keeping what fits is the same rule a `store` into a full collection
    // follows.
    const std::string path = TempFile("yse_coll_overflow_683.txt");
    {
      std::ofstream out(path, std::ios::binary);
      for (int i = 0; i < 300; i++)
        out << i << ", v" << i << ";\n";
    }

    std::vector<std::string> log;
    Recorder data;
    data.log = &log;
    data.tag = "d";
    YSE::pHandle dataHandle(&data);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* coll = p.CreateObject(YSE::OBJ::G_COLL, "");
    REQUIRE(coll != nullptr);
    p.Connect(coll, 0, &dataHandle, 0);

    coll->SetListData(0, "read " + path);
    SettleFiles(p);

    log.clear();
    coll->SetListData(0, "length");
    coll->SetListData(0, "255");
    coll->SetListData(0, "256");
    REQUIRE(log.size() == 2);
    CHECK(log[0] == "d:i " + std::to_string(gColl::MAX_ENTRIES));
    CHECK(log[1] == "d:l v255");

    std::error_code ec;
    std::filesystem::remove(path, ec);
  }

  TEST_CASE("coll: a file larger than a slot is refused whole (#683)") {
    // Refused rather than truncated, for the reason every other bound in this
    // object is: half a collection is a different collection, and a patch could
    // not tell a clipped preset from a loaded one.
    const std::string path = TempFile("yse_coll_too_big_683.txt");
    {
      std::ofstream out(path, std::ios::binary);
      const std::string filler(1024, 'x');
      // Comfortably past BYTES_CAPACITY, in records that would each parse.
      for (std::size_t written = 0; written <= YSE::PATCHER::fileScheduler::BYTES_CAPACITY;
           written += filler.size() + 8) {
        out << "0, " << filler << ";\n";
      }
    }

    std::vector<std::string> log;
    Recorder data;
    Recorder file;
    data.log = &log;
    data.tag = "d";
    file.log = &log;
    file.tag = "file";
    YSE::pHandle dataHandle(&data);
    YSE::pHandle fileHandle(&file);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* coll = p.CreateObject(YSE::OBJ::G_COLL, "");
    REQUIRE(coll != nullptr);
    p.Connect(coll, 0, &dataHandle, 0);
    p.Connect(coll, 3, &fileHandle, 0);

    coll->SetListData(0, "0 kept");
    coll->SetListData(0, "read " + path);
    SettleFiles(p);
    CHECK(log.empty());

    coll->SetListData(0, "length");
    REQUIRE(log.size() == 1);
    CHECK(log[0] == "d:i 1");

    std::error_code ec;
    std::filesystem::remove(path, ec);
  }

  TEST_CASE("coll: filetype is consumed rather than read as an address (#683)") {
    // Max's filetype filters the file *dialogs*, which a headless patcher does
    // not have. It still has to be a reserved word: read as a bare symbol it
    // would look up an entry called "filetype" instead, which is a different
    // and silent wrong answer.
    Rig rig;
    rig.List("store filetype trap");
    rig.reset();

    rig.List("filetype TEXT");
    CHECK_FALSE(rig.data.gotList);
    CHECK_FALSE(rig.data.gotInt);

    // The entry is still reachable by writing it as an address the normal way.
    rig.reset();
    rig.List("store x 1");
    rig.reset();
    rig.List("length");
    CHECK(rig.data.gotInt);
    CHECK(rig.data.intValue == 2);
  }

  TEST_CASE("coll: a standalone .coll consumes read and write without storing them (#683)") {
    // A standalone object has no patcher and so no file plumbing at all. The
    // commands still have to be *consumed*: read as bare symbols they would
    // become addresses, and a patch moved from a standalone rig into a patcher
    // would then behave differently for the worse reason.
    Rig rig;
    rig.List("read somewhere.txt");
    rig.List("write somewhere.txt");
    rig.List("readagain");
    rig.List("writeagain");

    rig.reset();
    rig.List("length");
    CHECK(rig.data.gotInt);
    CHECK(rig.data.intValue == 0);
  }

  // ─── the shared name context (issue #684) ───────────────────────────────────
  //
  // The registry is process-wide and holds its stores weakly, so every test
  // below spells a name nothing else uses: a name leaked from one test into the
  // next would make a store look shared when it was only stale.

  TEST_CASE("coll: a name binds the store to the patcher's address form (#684)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("coll_song");

    gColl named;
    named.SetParams("notes684a");
    named.SetParent(&p);
    CHECK(named.IsShared());
    CHECK(named.StoreAddress() == "coll_song.notes684a");
    CHECK(named.CollName() == "notes684a");

    // An unnamed .coll is private rather than pooled on "<patcherName>.", which
    // is a real reachable address: two unconfigured objects sharing it would be
    // wired together in a way no patch author could see.
    gColl unnamed;
    unnamed.SetParent(&p);
    CHECK_FALSE(unnamed.IsShared());
    CHECK(unnamed.StoreAddress().empty());
  }

  TEST_CASE("coll: two .coll objects of one name share their contents (#684)") {
    // Max: "all coll objects that share the same name share their contents".
    // Asserted through a real patcher and a real cord rather than an accessor,
    // because what has to hold is that a *patch* can store on one object and
    // recall on another.
    MultiSink fromWriter;
    MultiSink fromReader;
    YSE::pHandle writerSink(&fromWriter);
    YSE::pHandle readerSink(&fromReader);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* writer = p.CreateObject(YSE::OBJ::G_COLL, "notes684b");
    YSE::pHandle* reader = p.CreateObject(YSE::OBJ::G_COLL, "notes684b");
    REQUIRE(writer != nullptr);
    REQUIRE(reader != nullptr);
    p.Connect(writer, 0, &writerSink, 0);
    p.Connect(reader, 0, &readerSink, 0);

    writer->SetListData(0, "store triad 0 4 7");
    reader->SetListData(0, "triad");
    CHECK(fromReader.gotList);
    CHECK(fromReader.listValue == "0 4 7");

    // And the other way round, so this is one table rather than two that happen
    // to have been written the same way.
    reader->SetListData(0, "5 60 100");
    writer->SetListData(0, "5");
    CHECK(fromWriter.gotList);
    CHECK(fromWriter.listValue == "60 100");
  }

  TEST_CASE("coll: two unnamed .coll objects keep separate stores (#684)") {
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* first = p.CreateObject(YSE::OBJ::G_COLL);
    YSE::pHandle* second = p.CreateObject(YSE::OBJ::G_COLL);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    p.Connect(second, 0, &sinkHandle, 0);

    first->SetListData(0, "store triad 0 4 7");
    second->SetListData(0, "length");
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 0);
  }

  TEST_CASE("coll: the pointer stays per-object on a shared store (#684)") {
    // The store holds the entries; the cursor belongs to the object. Two .coll
    // objects on one name have to be able to walk the same collection without
    // dragging each other's position around, which is what makes a shared
    // collection usable as a sequence by more than one reader.
    std::vector<std::string> log;
    Recorder first;
    Recorder second;
    first.log = &log;
    first.tag = "one";
    second.log = &log;
    second.tag = "two";
    YSE::pHandle firstHandle(&first);
    YSE::pHandle secondHandle(&second);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* a = p.CreateObject(YSE::OBJ::G_COLL, "notes684c");
    YSE::pHandle* b = p.CreateObject(YSE::OBJ::G_COLL, "notes684c");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    p.Connect(a, 0, &firstHandle, 0);
    p.Connect(b, 0, &secondHandle, 0);

    a->SetListData(0, "0 alpha");
    a->SetListData(0, "1 beta");

    // The first object steps its own cursor twice; the second has never been
    // stepped and is still on the first entry.
    a->SetListData(0, "next");
    a->SetListData(0, "next");
    b->SetListData(0, "next");

    REQUIRE(log.size() == 3);
    CHECK(log[0] == "one:l alpha");
    CHECK(log[1] == "one:l beta");
    CHECK(log[2] == "two:l alpha");
  }

  TEST_CASE("coll: patchers sharing a name share their collections (#684)") {
    // The address is "<patcherName>.<name>", so the isolation between patchers
    // is the patcher name — exactly as it already is for .s, .r and .value. Two
    // patchers left on their auto-generated "patcher_<N>" names stay apart.
    MultiSink shared;
    MultiSink isolated;
    YSE::pHandle sharedHandle(&shared);
    YSE::pHandle isolatedHandle(&isolated);

    YSE::patcher first;
    first.create(2);
    first.name("coll_684d");
    YSE::patcher second;
    second.create(2);
    second.name("coll_684d");
    YSE::patcher elsewhere;
    elsewhere.create(2);

    YSE::pHandle* writer = first.CreateObject(YSE::OBJ::G_COLL, "notes684d");
    YSE::pHandle* reader = second.CreateObject(YSE::OBJ::G_COLL, "notes684d");
    YSE::pHandle* stranger = elsewhere.CreateObject(YSE::OBJ::G_COLL, "notes684d");
    REQUIRE(writer != nullptr);
    REQUIRE(reader != nullptr);
    REQUIRE(stranger != nullptr);
    second.Connect(reader, 0, &sharedHandle, 0);
    elsewhere.Connect(stranger, 0, &isolatedHandle, 0);

    writer->SetListData(0, "store triad 0 4 7");

    reader->SetListData(0, "triad");
    CHECK(shared.gotList);
    CHECK(shared.listValue == "0 4 7");

    stranger->SetListData(0, "triad");
    CHECK_FALSE(isolated.gotList);
  }

  TEST_CASE("coll: renaming a patcher re-anchors its collections (#684)") {
    // The prefix moved, so the object now addresses a different store — the
    // same thing a rename already does to .s, .r and .value, and the reason
    // patcherImplementation::SetName has to know about this object.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher host;
    host.create(2);
    host.name("coll_684e_before");
    YSE::pHandle* coll = host.CreateObject(YSE::OBJ::G_COLL, "notes684e");
    REQUIRE(coll != nullptr);
    host.Connect(coll, 0, &sinkHandle, 0);
    coll->SetListData(0, "store triad 0 4 7");

    // The rename goes through patcherImplementation::SetName, which re-binds
    // every named collection in the patcher — so this object now addresses a
    // store nothing has written to.
    host.name("coll_684e_after");
    coll->SetListData(0, "length");
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 0);

    // And the store it now names is the one a patcher of that name reaches.
    coll->SetListData(0, "store moved 1 2");
    sink.reset();

    MultiSink neighbour;
    YSE::pHandle neighbourSink(&neighbour);
    YSE::patcher other;
    other.create(2);
    other.name("coll_684e_after");
    YSE::pHandle* mirror = other.CreateObject(YSE::OBJ::G_COLL, "notes684e");
    REQUIRE(mirror != nullptr);
    other.Connect(mirror, 0, &neighbourSink, 0);
    mirror->SetListData(0, "moved");
    CHECK(neighbour.gotList);
    CHECK(neighbour.listValue == "1 2");
  }

  TEST_CASE("coll: the address form is the patcher's, and RefreshBinding follows it (#684)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("coll_684j_before");

    gColl obj;
    obj.SetParams("notes684j");
    obj.SetParent(&p);
    CHECK(obj.StoreAddress() == "coll_684j_before.notes684j");

    // Idempotent: a rebind to the address it already has keeps the store, and
    // with it everything in it.
    obj.GetInlet(0)->SetList("store triad 0 4 7", YSE::T_GUI);
    obj.RefreshBinding();
    CHECK(obj.StoreAddress() == "coll_684j_before.notes684j");
    CHECK(obj.Count() == 1);

    p.SetName("coll_684j_after");
    obj.RefreshBinding();
    CHECK(obj.StoreAddress() == "coll_684j_after.notes684j");
    CHECK(obj.Count() == 0);
  }

  TEST_CASE("coll: a store outlives no .coll that names it (#684)") {
    // The registry holds its stores weakly: a name lives exactly as long as
    // some object addresses it. Strong ownership would make every name a patch
    // ever spelled immortal — an unbounded leak for an engine that opens and
    // closes patchers — and would leave one test's contents visible to the
    // next, which is the failure this asserts is absent.
    //
    // Counted as a delta rather than an absolute: the registry is process-wide,
    // so what this pins is that the name this test spelled left no key behind.
    const std::size_t before = YSE::PATCHER::NamedStoreCount<YSE::PATCHER::collStore>();
    {
      YSE::patcher p;
      p.create(2);
      p.name("coll_684f");
      YSE::pHandle* coll = p.CreateObject(YSE::OBJ::G_COLL, "notes684f");
      REQUIRE(coll != nullptr);
      coll->SetListData(0, "store triad 0 4 7");
      CHECK(YSE::PATCHER::NamedStoreCount<YSE::PATCHER::collStore>() == before + 1);
    }
    CHECK(YSE::PATCHER::NamedStoreCount<YSE::PATCHER::collStore>() == before);

    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::patcher fresh;
    fresh.create(2);
    fresh.name("coll_684f");
    YSE::pHandle* coll = fresh.CreateObject(YSE::OBJ::G_COLL, "notes684f");
    REQUIRE(coll != nullptr);
    fresh.Connect(coll, 0, &sinkHandle, 0);

    coll->SetListData(0, "length");
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 0);
  }

  TEST_CASE("coll: a re-parse keeps the collection while a sibling holds the name (#684)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("coll_684g");

    // A live SetParams on a published object is a rebuild (#234): the
    // replacement is constructed while the original still holds the store, so
    // the collection carries across the edit instead of being emptied by it.
    // `sibling` stands in for that still-live original.
    gColl sibling;
    sibling.SetParams("notes684g");
    sibling.SetParent(&p);

    gColl obj;
    obj.SetParams("notes684g");
    obj.SetParent(&p);
    obj.GetInlet(0)->SetList("store triad 0 4 7", YSE::T_GUI);
    CHECK(obj.Count() == 1);
    CHECK(sibling.Count() == 1);

    obj.SetParams("notes684g");
    CHECK(obj.Count() == 1);

    // A different name is a different store, and this one has nothing in it —
    // while the name it left still holds what was written to it.
    obj.SetParams("notes684g_other");
    CHECK(obj.StoreAddress() == "coll_684g.notes684g_other");
    CHECK(obj.Count() == 0);
    CHECK(sibling.Count() == 1);

    // And back to no name at all is a private store, which is also empty.
    obj.SetParams("");
    CHECK_FALSE(obj.IsShared());
    CHECK(obj.Count() == 0);
  }

  TEST_CASE("coll: the name survives a save and only the store's creator reloads it (#684)") {
    // Two .coll objects on one name both write the contents — they are reading
    // one table, so their copies are identical, and nominating a single writer
    // would mean the collection silently stopped being saved the day that
    // object was deleted. The duplication is resolved on the way back in: only
    // the object that created the store fills it, so a reload produces the
    // collection that was saved rather than that collection loaded twice.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    std::string json;
    {
      YSE::patcher src;
      src.create(2);
      src.name("coll_684h");
      YSE::pHandle* writer = src.CreateObject(YSE::OBJ::G_COLL, "notes684h");
      YSE::pHandle* reader = src.CreateObject(YSE::OBJ::G_COLL, "notes684h");
      REQUIRE(writer != nullptr);
      REQUIRE(reader != nullptr);
      writer->SetListData(0, "0 60 100");
      writer->SetListData(0, "store name hello");

      // The argument the author typed survives the round trip byte for byte —
      // the contents ride the state hook precisely so the parameter string does
      // not have to be rewritten from run-time state.
      CHECK(reader->GetParams() == "notes684h");
      json = src.DumpJSON();
    }

    // The copy is loaded under its own auto-generated patcher name, so it
    // builds a store of its own rather than joining anything left over.
    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 2);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(copy->GetParams() == "notes684h");
    loaded.Connect(copy, 0, &sinkHandle, 0);

    // Two entries, not four: the sibling adopted what the creator loaded rather
    // than reloading identical contents over the top.
    copy->SetListData(0, "length");
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 2);

    sink.reset();
    copy->SetListData(0, "name");
    CHECK(sink.gotList);
    CHECK(sink.listValue == "hello");
  }

  TEST_CASE("coll: no-search is accepted and inert (#684)") {
    // Max's second argument suppresses its hunt for a file named after the
    // collection. There is no such hunt here, so all this has to do is build —
    // and, crucially, not be read as part of the name.
    gColl obj;
    obj.SetParams("notes684i 1");
    CHECK(obj.CollName() == "notes684i");
    CHECK(obj.GetParams() == "notes684i 1");
  }

  // ─── sub, nsub and nth (issue #684) ─────────────────────────────────────────

  TEST_CASE("coll: nsub replaces one element and sends nothing (#684)") {
    // Max: "nsub 2 4 7 replaces the fourth element of address 2 with the value
    // 7", and positions are 1-based.
    Rig rig;
    rig.List("2 10 20 30 40");
    rig.reset();

    rig.List("nsub 2 4 7");
    CHECK(rig.obj.Lookup("2") == "10 20 30 7");
    // nsub is the silent half of the pair.
    CHECK_FALSE(rig.data.gotList);
    CHECK_FALSE(rig.address.gotInt);

    // A symbol substitutes as readily as a number.
    rig.List("nsub 2 1 word");
    CHECK(rig.obj.Lookup("2") == "word 20 30 7");
  }

  TEST_CASE("coll: sub replaces and then sends the address and the message (#684)") {
    // Max: "the same as nsub, except that the message stored at the specified
    // address is sent out after the item has been substituted" — and sub is the
    // fifth trigger Max lists for the address outlet.
    Rig rig;
    rig.List("store chord 0 4 7");
    rig.reset();

    rig.List("sub chord 2 3");
    CHECK(rig.obj.Lookup("chord") == "0 3 7");
    CHECK(rig.data.gotList);
    CHECK(rig.data.listValue == "0 3 7");
    CHECK(rig.address.gotList);
    CHECK(rig.address.listValue == "chord");
  }

  TEST_CASE("coll: a sub that changes nothing sends nothing (#684)") {
    Rig rig;
    rig.List("2 10 20");
    rig.reset();

    // No fifth element to replace.
    rig.List("sub 2 5 99");
    CHECK(rig.obj.Lookup("2") == "10 20");
    CHECK_FALSE(rig.data.gotList);

    // No such address.
    rig.List("sub 9 1 99");
    CHECK_FALSE(rig.data.gotList);
  }

  TEST_CASE("coll: a substitution that would overflow the entry is refused whole (#684)") {
    // Refused rather than truncated, and refused *before* a character moves, so
    // an over-long splice leaves the entry exactly as it was.
    Rig rig;
    const std::string filler(gColl::VALUE_CAPACITY - 2, 'x');
    rig.List("store big " + filler + " a");
    REQUIRE(rig.obj.Lookup("big") == filler + " a");

    rig.List("nsub big 2 " + std::string(20, 'y'));
    CHECK(rig.obj.Lookup("big") == filler + " a");
  }

  TEST_CASE("coll: nth sends the element at a position in the kind it is (#684)") {
    // Max: "nth 75 2 will output the second item in the list stored at address
    // 75."
    Rig rig;
    rig.List("75 alpha 60 60.5");
    rig.reset();

    rig.List("nth 75 2");
    CHECK(rig.data.gotInt);
    CHECK(rig.data.intValue == 60);

    rig.reset();
    rig.List("nth 75 3");
    CHECK(rig.data.gotFloat);
    CHECK(rig.data.floatValue == doctest::Approx(60.5f));

    rig.reset();
    rig.List("nth 75 1");
    CHECK(rig.data.gotList);
    CHECK(rig.data.listValue == "alpha");

    // Past the end, and no address outlet in any case: Max lists nth among none
    // of the address outlet's triggers.
    rig.reset();
    rig.List("nth 75 9");
    CHECK_FALSE(rig.data.gotList);
    CHECK_FALSE(rig.data.gotInt);
    CHECK_FALSE(rig.address.gotInt);
  }

  // ─── min and max (issue #684) ───────────────────────────────────────────────

  TEST_CASE("coll: min and max scan an element position across every entry (#684)") {
    // Max: "Gets the lowest value in any entry. An optional integer argument
    // (defaults to '1') specifies an element position to use."
    Rig rig;
    rig.List("0 30 5");
    rig.List("1 10 9");
    rig.List("2 20 1");

    rig.reset();
    rig.List("min");
    CHECK(rig.data.gotInt);
    CHECK(rig.data.intValue == 10);

    rig.reset();
    rig.List("max");
    CHECK(rig.data.gotInt);
    CHECK(rig.data.intValue == 30);

    // The second element rather than the first.
    rig.reset();
    rig.List("min 2");
    CHECK(rig.data.gotInt);
    CHECK(rig.data.intValue == 1);

    rig.reset();
    rig.List("max 2");
    CHECK(rig.data.gotInt);
    CHECK(rig.data.intValue == 9);
  }

  TEST_CASE("coll: min and max ignore entries with no number there (#684)") {
    // An entry whose element is a word has no value to be lowest, so it is
    // skipped rather than counted as zero — which would make every collection
    // holding a symbol answer 0 to min.
    Rig rig;
    rig.List("store a word");
    rig.List("1 40.5");
    rig.List("2 12");

    rig.reset();
    rig.List("min");
    CHECK(rig.data.gotInt);
    CHECK(rig.data.intValue == 12);

    // The winning token's own spelling decides int or float, as everywhere else.
    rig.reset();
    rig.List("max");
    CHECK(rig.data.gotFloat);
    CHECK(rig.data.floatValue == doctest::Approx(40.5f));

    // Nothing numeric at all is silence rather than a zero.
    Rig empty;
    empty.List("store a word");
    empty.reset();
    empty.List("min");
    CHECK_FALSE(empty.data.gotInt);
    CHECK_FALSE(empty.data.gotFloat);
    CHECK_FALSE(empty.data.gotList);
  }

  // ─── sort (issue #684) ──────────────────────────────────────────────────────

  TEST_CASE("coll: sort reorders storage without moving the addresses (#684)") {
    // What sort changes is the order dump, next and prev walk. The addresses
    // stay with the data they belong to — moving them is `swap`'s job, and a
    // sort that renumbered would silently break every lookup in the patch.
    Rig rig;
    rig.List("10 30");
    rig.List("20 10");
    rig.List("30 20");

    rig.List("sort -1");
    CHECK(rig.obj.KeyAt(0) == "20");
    CHECK(rig.obj.ValueAt(0) == "10");
    CHECK(rig.obj.KeyAt(1) == "30");
    CHECK(rig.obj.ValueAt(1) == "20");
    CHECK(rig.obj.KeyAt(2) == "10");
    CHECK(rig.obj.ValueAt(2) == "30");

    // Every address still finds its own message.
    CHECK(rig.obj.Lookup("10") == "30");
    CHECK(rig.obj.Lookup("20") == "10");
    CHECK(rig.obj.Lookup("30") == "20");
  }

  TEST_CASE("coll: sort 1 is descending and a bare sort is ascending (#684)") {
    Rig rig;
    rig.List("0 3");
    rig.List("1 1");
    rig.List("2 2");

    rig.List("sort 1");
    CHECK(rig.obj.ValueAt(0) == "3");
    CHECK(rig.obj.ValueAt(1) == "2");
    CHECK(rig.obj.ValueAt(2) == "1");

    // Max states no default for the order; a bare sort is ascending here.
    rig.List("sort");
    CHECK(rig.obj.ValueAt(0) == "1");
    CHECK(rig.obj.ValueAt(1) == "2");
    CHECK(rig.obj.ValueAt(2) == "3");
  }

  TEST_CASE("coll: sort -1 -1 sorts by the address (#684)") {
    // Max: "If the second argument is -1, the index (either number or symbol)
    // associated with the data is used."
    Rig rig;
    rig.List("30 c");
    rig.List("store zulu z");
    rig.List("10 a");
    rig.List("store alpha x");
    rig.List("20 b");

    rig.List("sort -1 -1");
    // Numbers before symbols, each group in its own order. Max documents no
    // ordering across the two kinds; this is the reading that keeps a numeric
    // run contiguous.
    CHECK(rig.obj.KeyAt(0) == "10");
    CHECK(rig.obj.KeyAt(1) == "20");
    CHECK(rig.obj.KeyAt(2) == "30");
    CHECK(rig.obj.KeyAt(3) == "alpha");
    CHECK(rig.obj.KeyAt(4) == "zulu");
  }

  TEST_CASE("coll: sort's element argument picks which element decides (#684)") {
    Rig rig;
    rig.List("0 9 1");
    rig.List("1 8 3");
    rig.List("2 7 2");

    // 0 and 1 both name the first element, read literally from Max's wording.
    rig.List("sort -1 0");
    CHECK(rig.obj.KeyAt(0) == "2");
    rig.List("sort -1 1");
    CHECK(rig.obj.KeyAt(0) == "2");

    rig.List("sort -1 2");
    CHECK(rig.obj.KeyAt(0) == "0");
    CHECK(rig.obj.KeyAt(1) == "2");
    CHECK(rig.obj.KeyAt(2) == "1");
  }

  TEST_CASE("coll: sort is stable and leaves an already-sorted table alone (#684)") {
    // Entries that compare equal keep the storage order they had — the only
    // behaviour under which sorting a collection twice is the same as sorting
    // it once.
    Rig rig;
    rig.List("0 5 first");
    rig.List("1 5 second");
    rig.List("2 5 third");
    rig.List("3 1 zero");

    rig.List("sort -1");
    CHECK(rig.obj.KeyAt(0) == "3");
    CHECK(rig.obj.KeyAt(1) == "0");
    CHECK(rig.obj.KeyAt(2) == "1");
    CHECK(rig.obj.KeyAt(3) == "2");

    rig.List("sort -1");
    CHECK(rig.obj.KeyAt(0) == "3");
    CHECK(rig.obj.KeyAt(1) == "0");
    CHECK(rig.obj.KeyAt(2) == "1");
    CHECK(rig.obj.KeyAt(3) == "2");
  }

  TEST_CASE("coll: sorting a full collection keeps every entry (#684)") {
    // The permutation is applied by cycle-following through one scratch entry.
    // A cycle walked wrongly loses or duplicates entries rather than crashing,
    // so the whole table reversed is the case worth pinning.
    Rig rig;
    for (std::size_t i = 0; i < gColl::MAX_ENTRIES; i++) {
      rig.List(std::to_string(i) + " " + std::to_string(gColl::MAX_ENTRIES - i));
    }
    REQUIRE(rig.obj.Count() == gColl::MAX_ENTRIES);

    rig.List("sort -1");
    CHECK(rig.obj.Count() == gColl::MAX_ENTRIES);
    for (std::size_t i = 0; i < gColl::MAX_ENTRIES; i++) {
      // Ascending by the stored number, and every address still on its own
      // message.
      CHECK(rig.obj.ValueAt(i) == std::to_string(i + 1));
      CHECK(rig.obj.KeyAt(i) == std::to_string(gColl::MAX_ENTRIES - i - 1));
    }
  }

  // ─── swap, merge, separate and renumber (issue #684) ────────────────────────

  TEST_CASE("coll: swap exchanges two addresses and leaves the data where it is (#684)") {
    // Max: "Exchanges the indices associated with two addresses. The data is
    // unchanged, but the indexes that they use are swapped."
    Rig rig;
    rig.List("1 alpha");
    rig.List("2 beta");

    rig.List("swap 1 2");
    // Storage order is untouched; only the keys moved.
    CHECK(rig.obj.KeyAt(0) == "2");
    CHECK(rig.obj.ValueAt(0) == "alpha");
    CHECK(rig.obj.KeyAt(1) == "1");
    CHECK(rig.obj.ValueAt(1) == "beta");
    CHECK(rig.obj.Lookup("1") == "beta");
    CHECK(rig.obj.Lookup("2") == "alpha");
  }

  TEST_CASE("coll: swap works across a numeric and a symbol address (#684)") {
    Rig rig;
    rig.List("1 alpha");
    rig.List("store name beta");

    rig.List("swap 1 name");
    CHECK(rig.obj.KeyAt(0) == "name");
    CHECK(rig.obj.ValueAt(0) == "alpha");
    CHECK(rig.obj.KeyAt(1) == "1");
    CHECK(rig.obj.ValueAt(1) == "beta");
  }

  TEST_CASE("coll: a swap with a missing address does nothing (#684)") {
    // Half a swap would leave one entry holding an address that no longer names
    // it, which is worse than the message being ignored.
    Rig rig;
    rig.List("1 alpha");
    rig.List("swap 1 9");
    CHECK(rig.obj.KeyAt(0) == "1");
    CHECK(rig.obj.ValueAt(0) == "alpha");
  }

  TEST_CASE("coll: merge appends to an address and creates a missing one (#684)") {
    // Max: "Appends data at the end of the data found at the specified index.
    // If the address does not yet exist, it is created."
    Rig rig;
    rig.List("1 60");

    rig.List("merge 1 100 127");
    CHECK(rig.obj.Lookup("1") == "60 100 127");

    rig.List("merge 5 hello");
    CHECK(rig.obj.Lookup("5") == "hello");
    CHECK(rig.obj.Count() == 2);

    // Merging past the entry's bound is refused whole, so the message that was
    // there is not left half rewritten.
    rig.List("merge 1 " + std::string(gColl::VALUE_CAPACITY, 'z'));
    CHECK(rig.obj.Lookup("1") == "60 100 127");
  }

  TEST_CASE("coll: separate opens a slot above the address given (#684)") {
    // Max: "Increments the numerical indices for all data whose index is
    // greater than the provided." Strictly greater — `insert`'s "equal or
    // greater" is the other rule, and the two are deliberately different.
    Rig rig;
    rig.List("0 a");
    rig.List("1 b");
    rig.List("2 c");
    rig.List("store name x");

    rig.List("separate 1");
    CHECK(rig.obj.KeyAt(0) == "0");
    CHECK(rig.obj.KeyAt(1) == "1");
    CHECK(rig.obj.KeyAt(2) == "3");
    // A symbol address has no number to increment.
    CHECK(rig.obj.KeyAt(3) == "name");

    // Which is what leaves 2 free for the store that follows.
    rig.List("2 new");
    CHECK(rig.obj.Lookup("2") == "new");
    CHECK(rig.obj.Lookup("1") == "b");
    CHECK(rig.obj.Lookup("3") == "c");
  }

  TEST_CASE("coll: renumber makes the numeric addresses consecutive (#684)") {
    // Max states no default starting address. Bare renumber starts at 0 and
    // bare renumber2 at 1 — see the class documentation and #694.
    Rig rig;
    rig.List("10 a");
    rig.List("store name x");
    rig.List("40 b");
    rig.List("70 c");

    rig.List("renumber");
    CHECK(rig.obj.KeyAt(0) == "0");
    // Symbol addresses are left alone: they have no place in a numeric
    // sequence, and renumbering one would destroy the only handle the patch has
    // on that entry.
    CHECK(rig.obj.KeyAt(1) == "name");
    CHECK(rig.obj.KeyAt(2) == "1");
    CHECK(rig.obj.KeyAt(3) == "2");

    rig.List("renumber 10");
    CHECK(rig.obj.KeyAt(0) == "10");
    CHECK(rig.obj.KeyAt(2) == "11");
    CHECK(rig.obj.KeyAt(3) == "12");

    rig.List("renumber2");
    CHECK(rig.obj.KeyAt(0) == "1");
    CHECK(rig.obj.KeyAt(2) == "2");
    CHECK(rig.obj.KeyAt(3) == "3");

    rig.List("renumber2 10");
    CHECK(rig.obj.KeyAt(0) == "11");
    CHECK(rig.obj.KeyAt(2) == "12");
    CHECK(rig.obj.KeyAt(3) == "13");
  }

  // ─── end to end, through a real patcher graph ───────────────────────────────

  TEST_CASE("coll: a sorted collection dumps into a chain in its new order (#684)") {
    // The use case sort exists for, run through the real thing: a table of note
    // numbers put in order and then dumped into a transposer. What has to hold
    // is a property of the whole graph — the entries leave the data outlet as
    // numbers a `.+` can add to, in the order sort put them, with their own
    // addresses beside them on the other branch.
    std::vector<std::string> log;
    Recorder notes;
    Recorder addresses;
    notes.log = &log;
    notes.tag = "note";
    addresses.log = &log;
    addresses.tag = "at";
    MultiSink sink;
    YSE::pHandle noteHandle(&notes);
    YSE::pHandle addressHandle(&addresses);
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* coll = p.CreateObject(YSE::OBJ::G_COLL, "sorted684");
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::G_ADD, "12");
    REQUIRE(coll != nullptr);
    REQUIRE(add != nullptr);
    p.Connect(coll, 0, &noteHandle, 0);
    p.Connect(coll, 1, &addressHandle, 0);
    p.Connect(coll, 0, add, 0);
    p.Connect(add, 0, &sinkHandle, 0);

    coll->SetListData(0, "0 67");
    coll->SetListData(0, "1 60");
    coll->SetListData(0, "2 64");
    coll->SetListData(0, "sort -1");
    log.clear();

    coll->SetListData(0, "dump");
    REQUIRE(log.size() == 6);
    CHECK(log[0] == "at:i 1");
    CHECK(log[1] == "note:i 60");
    CHECK(log[2] == "at:i 2");
    CHECK(log[3] == "note:i 64");
    CHECK(log[4] == "at:i 0");
    CHECK(log[5] == "note:i 67");

    // The last entry reached the `.+`'s numeric inlet, which a one-element list
    // would not have.
    CHECK(sink.gotFloat);
    CHECK(sink.floatValue == doctest::Approx(79.f));
  }
}
