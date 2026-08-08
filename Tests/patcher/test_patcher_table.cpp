// Tests for .table (issue #498) — the patcher's dense, index-addressed store.
//
// Six things are worth pinning here, and they are the ones an implementation can
// get wrong while still looking like it works:
//
//   - **the creation argument means two things, and Max only documents one.**
//     Issue #498 asks for a size; Max's one argument is a *name* and the size is
//     its `size` attribute. Both are honoured — a numeric token is the size, a
//     non-numeric one the name — so both spellings are asserted directly.
//   - **the armed value is one-shot, and `cancel` drops it.** A value that stayed
//     behind would turn every later read into a store, and nothing would crash.
//     `cancel` exists in Max precisely because the value is one-shot, so it is
//     tested as the pair it is.
//   - **an address out of range stays quiet.** Max does not say what it does, and
//     the alternative — clamping — answers a wrong question with a plausible
//     number that no wavetable lookup downstream could detect.
//   - **quantile reads the table as a distribution.** It is the message that
//     earns this object its place next to .funbuff, and it is asserted with a
//     table whose answer is forced regardless of the random draw.
//   - **next and prev wrap, where .funbuff's next bangs and stops.** A dense
//     table is a ring of addresses; a sparse function is a traversal with an end.
//   - **the contents save by default, and turning that off survives a reload.**
//     Max's embed defaults to 1 here and to 0 on funbuff, so the flag has to be
//     written even when it is off — otherwise `embed 0` would silently come back
//     on.
//
// No audio device and no engine of its own, except where a real patcher graph is
// the point.

#include <doctest/doctest.h>
#include <string>
#include <vector>

#include "patcher/genericObjects/gTable.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::gTable;

namespace {

  // Records everything it receives, in order — so a dump of N values reads back
  // as the exact list of sends. MultiSink only keeps the last of each kind, which
  // cannot tell a dump of three values from a dump of one.
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

  // A standalone .table with a sink on its outlet. Standalone on purpose where
  // the patcher is not the point: a test that needed one could not tell a refused
  // message from a message the patcher never delivered.
  struct Rig {
    MultiSink out;
    gTable obj;

    Rig() {
      obj.ConnectOutlet(out.GetInlet(0), 0);
    }

    void reset() {
      out.reset();
    }

    void List(const std::string& message) {
      obj.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Address(int value) {
      obj.GetInlet(0)->SetInt(value, YSE::T_GUI);
    }
    void AddressFloat(float value) {
      obj.GetInlet(0)->SetFloat(value, YSE::T_GUI);
    }
    void Value(int value) {
      obj.GetInlet(1)->SetInt(value, YSE::T_GUI);
    }
    void Bang() {
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
    }
    // Store one value the way a patch does: the value on the cold inlet, then
    // the address on the hot one.
    void Store(int address, int value) {
      Value(value);
      Address(address);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("table: registered, two inlets and one outlet (#498)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* t = p.CreateObject(YSE::OBJ::G_TABLE);
    REQUIRE(t != nullptr);
    CHECK(std::string(t->Type()) == ".table");
    CHECK(t->GetInputs() == 2);
    // Max's second outlet bangs only on an edit in the graphic editing window,
    // which the headless patcher has none of — it is left off rather than left
    // dead or repurposed.
    CHECK(t->GetOutputs() == 1);
  }

  TEST_CASE("table: appears in the registry's name list (#498)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_TABLE)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("table: inlet 0 takes bang, int, float and list; inlet 1 takes numbers (#498)") {
    gTable obj;
    const unsigned int hot = obj.GetInlet(0)->GetAcceptedTypes();
    CHECK((hot & YSE::PATCHER::IT_BANG) != 0);
    CHECK((hot & YSE::PATCHER::IT_INT) != 0);
    CHECK((hot & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((hot & YSE::PATCHER::IT_LIST) != 0);

    const unsigned int cold = obj.GetInlet(1)->GetAcceptedTypes();
    CHECK((cold & YSE::PATCHER::IT_INT) != 0);
    CHECK((cold & YSE::PATCHER::IT_FLOAT) != 0);
  }

  // ─── the creation argument, where the issue and Max disagree ────────────────

  TEST_CASE("table: with no argument the size is Max's 128 (#498)") {
    // Max's `size` attribute: "the default is 128 values, indexed with numbers
    // from 0 to 127".
    gTable obj;
    CHECK(obj.Size() == gTable::DEFAULT_SIZE);
    CHECK(obj.Name().empty());
  }

  TEST_CASE("table: a numeric argument is the size, a word is Max's name (#498)") {
    // Issue #498 asks for "fixed size from the creation argument"; Max's one
    // argument is a name and the size lives in an attribute the patcher has no
    // mechanism for. Both are honoured rather than one being dropped.
    gTable size;
    size.SetParams("512");
    CHECK(size.Size() == 512);
    CHECK(size.Name().empty());

    gTable named;
    named.SetParams("mytable");
    CHECK(named.Name() == "mytable");
    CHECK(named.Size() == gTable::DEFAULT_SIZE);

    gTable both;
    both.SetParams("mytable 512");
    CHECK(both.Name() == "mytable");
    CHECK(both.Size() == 512);

    // And in Max's own order-free spirit, the other way round too.
    gTable reversed;
    reversed.SetParams("64 wave");
    CHECK(reversed.Name() == "wave");
    CHECK(reversed.Size() == 64);
  }

  TEST_CASE("table: the size argument is clamped rather than honoured (#498)") {
    // The array is allocated whole at construction, so honouring a larger request
    // would mean a table a message path might later have to grow.
    gTable obj;
    obj.SetParams("100000");
    CHECK(obj.Size() == gTable::MAX_SIZE);

    obj.SetParams("0");
    CHECK(obj.Size() == gTable::MIN_SIZE);
    obj.SetParams("-5");
    CHECK(obj.Size() == gTable::MIN_SIZE);
  }

  TEST_CASE("table: clearing the argument returns it to Max's default (#498)") {
    gTable obj;
    obj.SetParams("mytable 8");
    REQUIRE(obj.Size() == 8);
    obj.SetParams("");
    CHECK(obj.Size() == gTable::DEFAULT_SIZE);
    CHECK(obj.Name().empty());
  }

  TEST_CASE("table: every live address holds a value from the start (#498)") {
    // What "dense" means, and the difference from every other store in the
    // family: there is no such thing as an empty address.
    Rig rig;
    rig.obj.SetParams("4");
    for (int i = 0; i < 4; i++) {
      CAPTURE(i);
      rig.reset();
      rig.Address(i);
      CHECK(rig.out.gotInt);
      CHECK(rig.out.intValue == 0);
    }
  }

  // ─── reading and writing ────────────────────────────────────────────────────

  TEST_CASE("table: the cold inlet arms a value the next address stores (#498)") {
    // Max's right inlet: "stores the value at the next index number received at
    // the left inlet".
    Rig rig;
    rig.Store(5, 111);
    CHECK(rig.obj.ValueAt(5) == 111);
    // Storing sends nothing.
    CHECK_FALSE(rig.out.gotInt);

    rig.reset();
    rig.Address(5);
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 111);
  }

  TEST_CASE("table: the armed value is consumed by exactly one address (#498)") {
    // A value that stayed behind would turn every later read into a store, so the
    // object could never be read at all — and nothing would crash.
    Rig rig;
    rig.Store(1, 42);
    rig.reset();

    rig.Address(2);
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 0);
    CHECK(rig.obj.ValueAt(2) == 0);
  }

  TEST_CASE("table: cancel drops an armed value before it is used (#498)") {
    // Max: "causes table to ignore a number received in the right inlet, so that
    // the next number received in the left inlet will output a number, rather
    // than storing a number at that address". This message only makes sense
    // because the armed value is one-shot, so the two are tested as a pair.
    Rig rig;
    rig.List("set 3 77");
    rig.Value(999);
    rig.List("cancel");
    rig.reset();

    rig.Address(3);
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 77);
    CHECK(rig.obj.ValueAt(3) == 77);
  }

  TEST_CASE("table: a two-number list stores directly and arms nothing (#498)") {
    // Max's list method: "the second number is stored at the address (index)
    // specified by the first number".
    Rig rig;
    rig.List("7 210");
    CHECK(rig.obj.ValueAt(7) == 210);
    CHECK_FALSE(rig.out.gotInt);

    // Nothing was armed by it, so the very next address reads rather than writes.
    rig.reset();
    rig.Address(7);
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 210);
  }

  TEST_CASE("table: a float address is truncated on both inlets (#498)") {
    // Max's float method on this object is "convert to int".
    Rig rig;
    rig.obj.GetInlet(1)->SetFloat(9.8f, YSE::T_GUI);
    rig.AddressFloat(4.7f);
    CHECK(rig.obj.ValueAt(4) == 9);

    rig.reset();
    rig.AddressFloat(4.2f);
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 9);
  }

  TEST_CASE("table: an address out of range reads and writes nothing (#498)") {
    // Max does not state this edge. Staying quiet is the family's rule and the
    // safer reading: clamping would answer a wrong question with a plausible
    // number nothing downstream could detect.
    Rig rig;
    rig.obj.SetParams("4");
    rig.List("const 5");
    rig.reset();

    rig.Address(4);
    CHECK_FALSE(rig.out.gotInt);
    rig.Address(-1);
    CHECK_FALSE(rig.out.gotInt);

    // And a write past the end does not wrap into a live address.
    rig.Store(9, 123);
    for (int i = 0; i < 4; i++) {
      CAPTURE(i);
      CHECK(rig.obj.ValueAt((std::size_t)i) == 5);
    }
  }

  // ─── the bulk messages ──────────────────────────────────────────────────────

  TEST_CASE("table: set stores successive addresses from a start (#498)") {
    // Max: "the first argument specifies an address. The next number is the value
    // to be stored in that address, and each number after that is stored in a
    // successive address."
    Rig rig;
    rig.List("set 2 10 20 30");
    CHECK(rig.obj.ValueAt(1) == 0);
    CHECK(rig.obj.ValueAt(2) == 10);
    CHECK(rig.obj.ValueAt(3) == 20);
    CHECK(rig.obj.ValueAt(4) == 30);
    CHECK(rig.obj.ValueAt(5) == 0);
  }

  TEST_CASE("table: a set running past the end drops the overflow (#498)") {
    // Max's `setresizes` attribute defaults to 0, so `set` does not grow the
    // table — and growing it would allocate on whichever thread the message
    // arrived on.
    Rig rig;
    rig.obj.SetParams("3");
    rig.List("set 1 10 20 30 40");
    CHECK(rig.obj.Size() == 3);
    CHECK(rig.obj.ValueAt(1) == 10);
    CHECK(rig.obj.ValueAt(2) == 20);
  }

  TEST_CASE("table: const fills and clear empties, neither resizing (#498)") {
    // Max: const "fill the table with a number"; clear "set all values to 0".
    // Neither is a resize: a dense table with no addresses would not be one.
    Rig rig;
    rig.obj.SetParams("5");
    rig.List("const 9");
    for (int i = 0; i < 5; i++) {
      CAPTURE(i);
      CHECK(rig.obj.ValueAt((std::size_t)i) == 9);
    }

    rig.List("clear");
    CHECK(rig.obj.Size() == 5);
    for (int i = 0; i < 5; i++) {
      CAPTURE(i);
      CHECK(rig.obj.ValueAt((std::size_t)i) == 0);
    }
  }

  TEST_CASE("table: dump sends every value in address order from 0 (#498)") {
    // Max: "sends all the numbers stored in the table out the left outlet in
    // immediate succession, beginning with address 0".
    std::vector<std::string> log;
    Recorder rec;
    rec.log = &log;
    rec.tag = "v";

    gTable obj;
    obj.SetParams("4");
    obj.ConnectOutlet(rec.GetInlet(0), 0);
    obj.GetInlet(0)->SetList("set 0 3 1 4 1", YSE::T_GUI);

    log.clear();
    obj.GetInlet(0)->SetList("dump", YSE::T_GUI);
    REQUIRE(log.size() == 4);
    CHECK(log[0] == "v:i 3");
    CHECK(log[1] == "v:i 1");
    CHECK(log[2] == "v:i 4");
    CHECK(log[3] == "v:i 1");
  }

  TEST_CASE("table: length, sum, min and max report the whole table (#498)") {
    Rig rig;
    rig.obj.SetParams("4");
    rig.List("set 0 5 -2 7 1");

    rig.reset();
    rig.List("length");
    CHECK(rig.out.intValue == 4);

    rig.reset();
    rig.List("sum");
    CHECK(rig.out.intValue == 11);

    rig.reset();
    rig.List("min");
    CHECK(rig.out.intValue == -2);

    rig.reset();
    rig.List("max");
    CHECK(rig.out.intValue == 7);
  }

  TEST_CASE("table: min and max always answer, unlike .funbuff's (#498)") {
    // .funbuff stays quiet when it is empty; a table always has at least one
    // address and every address always holds a value, so there is no empty case
    // to be silent about.
    Rig rig;
    rig.List("min");
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 0);
  }

  TEST_CASE("table: inv answers with the address of the first value at or above (#498)") {
    // Max: "finds the first value which is greater than or equal to that number,
    // and sends the address of that value out the left outlet".
    Rig rig;
    rig.obj.SetParams("5");
    rig.List("set 0 1 3 2 8 4");

    rig.reset();
    rig.List("inv 3");
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 1);

    // Not sorted, so it really is the first in *address* order and not a search.
    rig.reset();
    rig.List("inv 8");
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 3);

    // No value reaches it, so there is no address to name and nothing is sent.
    rig.reset();
    rig.List("inv 99");
    CHECK_FALSE(rig.out.gotInt);
  }

  // ─── the pointer ────────────────────────────────────────────────────────────

  TEST_CASE("table: next walks forward and wraps at the end (#498)") {
    // Max: "sends the value stored in the address pointed at by the pointer out
    // the left outlet, then sets the pointer to the next address. If the pointer
    // is currently at the last address in the table, it wraps around to the first
    // address." That is deliberately unlike .funbuff's next, which bangs an
    // end-of-traversal outlet and stops: a dense table is a ring of addresses,
    // where a sparse function is a traversal with an end.
    Rig rig;
    rig.obj.SetParams("3");
    rig.List("set 0 11 22 33");

    const int expected[] = {11, 22, 33, 11};
    for (int i = 0; i < 4; i++) {
      CAPTURE(i);
      rig.reset();
      rig.List("next");
      CHECK(rig.out.gotInt);
      CHECK(rig.out.intValue == expected[i]);
    }
  }

  TEST_CASE("table: prev reads the same address then steps back, wrapping (#498)") {
    // Max: "causes the same output as the next message, but the pointer is then
    // decremented rather than incremented" — so the *value* read is the one at
    // the pointer in both cases, and only the step differs.
    Rig rig;
    rig.obj.SetParams("3");
    rig.List("set 0 11 22 33");

    const int expected[] = {11, 33, 22, 11};
    for (int i = 0; i < 4; i++) {
      CAPTURE(i);
      rig.reset();
      rig.List("prev");
      CHECK(rig.out.gotInt);
      CHECK(rig.out.intValue == expected[i]);
    }
  }

  TEST_CASE("table: goto moves the pointer and is clamped into the table (#498)") {
    // Max: "sets a pointer to the address specified by the number". Clamped
    // rather than refused: the pointer is a position in a dense array and every
    // out-of-range request has a nearest legal answer, unlike a lookup, where the
    // nearest legal answer would be a wrong value.
    Rig rig;
    rig.obj.SetParams("4");
    rig.List("set 0 11 22 33 44");

    rig.List("goto 2");
    CHECK(rig.obj.Pointer() == 2);
    rig.reset();
    rig.List("next");
    CHECK(rig.out.intValue == 33);

    rig.List("goto 99");
    CHECK(rig.obj.Pointer() == 3);
    rig.List("goto -7");
    CHECK(rig.obj.Pointer() == 0);
  }

  // ─── the distribution ───────────────────────────────────────────────────────

  TEST_CASE("table: quantile answers with the address the running sum reaches (#498)") {
    // Max: "multiplies the incoming number by the sum of all the numbers in the
    // table. This result is then divided by 2^15 (32,768). Then, table sends out
    // the address at which the sum of all values up to that address is greater
    // than or equal to the result." This is what makes a dense array a
    // probability distribution, and it is the issue's own use case.
    Rig rig;
    rig.obj.SetParams("4");
    // Total 100: cumulative 10, 30, 60, 100.
    rig.List("set 0 10 20 30 40");

    // 0.05 of the total is 5, which address 0 already covers.
    rig.reset();
    rig.List("quantile 1638");
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 0);

    // Half of the total is 50, first reached at address 2.
    rig.reset();
    rig.List("quantile 16384");
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 2);

    // The whole total is only reached at the last address.
    rig.reset();
    rig.List("quantile 32768");
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 3);
  }

  TEST_CASE("table: fquantile takes the fraction directly (#498)") {
    // Max: "given a number between zero and one, multiplies the number by the sum
    // of all the numbers in the table" — the same walk without the 32768 scaling.
    Rig rig;
    rig.obj.SetParams("4");
    rig.List("set 0 10 20 30 40");

    rig.reset();
    rig.List("fquantile 0.5");
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 2);

    rig.reset();
    rig.List("fquantile 0.0");
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 0);
  }

  TEST_CASE("table: a bang is a weighted random draw over the table (#498)") {
    // Max: "same as a quantile message with a random number between 0 and
    // 32,768". Asserted with a distribution whose answer is forced whatever the
    // draw is — every address but one has zero weight — so the test pins the
    // *weighting*, not a particular seed.
    Rig rig;
    rig.obj.SetParams("5");
    rig.List("set 2 1000");

    for (int i = 0; i < 20; i++) {
      CAPTURE(i);
      rig.reset();
      rig.Bang();
      CHECK(rig.out.gotInt);
      CHECK(rig.out.intValue == 2);
    }
  }

  TEST_CASE("table: a bang on an all-zero table answers with address 0 (#498)") {
    // An edge Max does not state: with a total of zero every running sum is zero,
    // so the walk is satisfied on its first step.
    Rig rig;
    rig.obj.SetParams("8");
    rig.reset();
    rig.Bang();
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 0);
  }

  // ─── bit fields ─────────────────────────────────────────────────────────────

  TEST_CASE("table: getbits reads the field ending at the start bit (#498)") {
    // Max numbers bits "0 to 31, from the least significant bit to the most
    // significant bit" and counts "bits to the right of the starting bit
    // location", which leaves open whether the start bit is included. It is read
    // here as the count of bits ending at and including `start`, so `getbits 0 7
    // 8` is the low byte.
    Rig rig;
    rig.List("set 0 43981"); // 0xABCD

    rig.reset();
    rig.List("getbits 0 7 8");
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 0xCD);

    rig.reset();
    rig.List("getbits 0 15 8");
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 0xAB);

    rig.reset();
    rig.List("getbits 0 3 4");
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 0xD);
  }

  TEST_CASE("table: setbits replaces only the named field (#498)") {
    Rig rig;
    rig.List("set 0 43981"); // 0xABCD
    rig.List("setbits 0 7 8 255");
    CHECK(rig.obj.ValueAt(0) == 0xABFF);

    rig.List("setbits 0 15 8 0");
    CHECK(rig.obj.ValueAt(0) == 0x00FF);
  }

  TEST_CASE("table: a bit field outside the word is refused whole (#498)") {
    // Refused rather than clipped to whatever part of it is legal: a partially
    // honoured bit request writes bits the patch did not ask about.
    Rig rig;
    rig.List("set 0 255");

    rig.reset();
    rig.List("getbits 0 32 4");
    CHECK_FALSE(rig.out.gotInt);
    rig.List("getbits 0 -1 4");
    CHECK_FALSE(rig.out.gotInt);
    // A field reaching past bit 0.
    rig.List("getbits 0 3 8");
    CHECK_FALSE(rig.out.gotInt);

    rig.List("setbits 0 3 8 15");
    CHECK(rig.obj.ValueAt(0) == 255);
  }

  // ─── load mode ──────────────────────────────────────────────────────────────

  TEST_CASE("table: load mode stores every number from address 0 (#498)") {
    // Max: "in load mode, every number received in the left inlet gets stored in
    // the table, beginning at address 0 and continuing until the table is filled
    // ... if more numbers are received than will fit in the size of the table,
    // additional numbers are ignored".
    Rig rig;
    rig.obj.SetParams("3");
    rig.List("load");
    CHECK(rig.obj.Loading());

    rig.reset();
    rig.Address(11);
    rig.Address(22);
    rig.Address(33);
    rig.Address(44);
    // Loading is silent: nothing is read while every number is data.
    CHECK_FALSE(rig.out.gotInt);
    CHECK(rig.obj.ValueAt(0) == 11);
    CHECK(rig.obj.ValueAt(1) == 22);
    CHECK(rig.obj.ValueAt(2) == 33);

    rig.List("normal");
    CHECK_FALSE(rig.obj.Loading());
    rig.reset();
    rig.Address(1);
    CHECK(rig.out.gotInt);
    CHECK(rig.out.intValue == 22);
  }

  TEST_CASE("table: words still dispatch as messages while loading (#498)") {
    // Otherwise `normal` could never turn load mode off — the object would eat
    // its own escape hatch.
    Rig rig;
    rig.obj.SetParams("4");
    rig.List("load");
    rig.List("normal");
    CHECK_FALSE(rig.obj.Loading());
    CHECK(rig.obj.ValueAt(0) == 0);
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("table: the contents survive a JSON round trip by default (#498)") {
    // Max's embed for this object: "the default behavior is 1 (save the data)" —
    // the opposite default from .funbuff's, and what issue #498's "contents in
    // the JSON round trip" asks for.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher src;
    src.create(2);
    YSE::pHandle* t = src.CreateObject(YSE::OBJ::G_TABLE, "8");
    REQUIRE(t != nullptr);
    t->SetListData(0, "set 0 5 6 7 8");

    const std::string json = src.DumpJSON();
    CHECK(json.find(".table") != std::string::npos);
    CHECK(json.find("\"state\"") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    // Read back through the outlet rather than through an accessor: what has to
    // survive is the array a patch can use, not a member variable.
    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".table");
    loaded.Connect(copy, 0, &sinkHandle, 0);

    copy->SetIntData(0, 2);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 7);

    // And the size came back with them, so the reloaded table is the same shape.
    sink.reset();
    copy->SetListData(0, "length");
    CHECK(sink.intValue == 8);
  }

  TEST_CASE("table: embed 0 stops the contents saving, and survives a reload (#498)") {
    // The flag is written even when it is off. This object's default is on, so
    // silence would bring `embed 0` back as `embed 1` and the table would quietly
    // start saving itself again — which is why .funbuff's "write nothing at all"
    // could not be copied here.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* t = src.CreateObject(YSE::OBJ::G_TABLE, "4");
    REQUIRE(t != nullptr);
    t->SetListData(0, "set 0 1 2 3 4");
    t->SetListData(0, "embed 0");

    const std::string json = src.DumpJSON();
    CHECK(json.find("\"values\"") == std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);
    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);

    // The contents did not come back...
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    loaded.Connect(copy, 0, &sinkHandle, 0);
    copy->SetIntData(0, 1);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 0);

    // ...and neither did the flag turn itself back on.
    CHECK(loaded.DumpJSON().find("\"values\"") == std::string::npos);
  }

  TEST_CASE("table: flags takes over embed's job from its first argument (#498)") {
    // Max: "the first argument affects the Save with Patcher option, and the
    // second argument affects the Don't Save option". The first is embed under
    // another name; the second concerns the table's own file, of which there is
    // none here.
    Rig rig;
    CHECK(rig.obj.Embeds());
    rig.List("flags 0 1");
    CHECK_FALSE(rig.obj.Embeds());
    rig.List("flags 1 0");
    CHECK(rig.obj.Embeds());
  }

  TEST_CASE("table: the name argument survives a round trip unchanged (#498)") {
    // Held so a `.table mytable` brought across from Max still builds, and so the
    // argument the author typed is not quietly rewritten — but it addresses
    // nothing: two tables of the same name do not share values here.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* t = src.CreateObject(YSE::OBJ::G_TABLE, "mytable 16");
    REQUIRE(t != nullptr);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(src.DumpJSON());
    REQUIRE(loaded.Objects() == 1);
    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(copy->GetParams() == "mytable 16");
  }

  TEST_CASE("table: two tables of the same name keep separate values (#498)") {
    // Issue #498 proposes sharing named tables through INTERNAL::NamedBus, which
    // is a publish/subscribe value bus with no storage and no way to answer "give
    // me the object called X" — sharing would need exactly the shared-name
    // registry the issue says not to build. The departure is asserted rather than
    // left implicit, because a patch relying on two tables being one store would
    // be wrong in a way nothing downstream could see.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* a = p.CreateObject(YSE::OBJ::G_TABLE, "shared 8");
    YSE::pHandle* b = p.CreateObject(YSE::OBJ::G_TABLE, "shared 8");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    p.Connect(b, 0, &sinkHandle, 0);

    a->SetListData(0, "set 0 42");
    b->SetIntData(0, 0);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 0);
  }

  // ─── end to end, through a real patcher graph ───────────────────────────────

  TEST_CASE("table: a looked-up value arrives downstream as a number (#498)") {
    // The property the object exists for, and one no unit test on the object
    // alone can show: a velocity curve is only usable if what comes out of it can
    // be added to. A one-element list here would silently do nothing at the .+.
    std::vector<std::string> log;
    Recorder raw;
    raw.log = &log;
    raw.tag = "raw";
    MultiSink sink;
    YSE::pHandle rawHandle(&raw);
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* t = p.CreateObject(YSE::OBJ::G_TABLE, "16");
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::G_ADD, "12");
    REQUIRE(t != nullptr);
    REQUIRE(add != nullptr);
    p.Connect(t, 0, &rawHandle, 0);
    p.Connect(t, 0, add, 0);
    p.Connect(add, 0, &sinkHandle, 0);

    t->SetListData(0, "set 1 60");
    t->SetIntData(0, 1);

    REQUIRE(log.size() == 1);
    CHECK(log[0] == "raw:i 60");
    // `.+` answers in floats whatever it was given, so what this pins is that the
    // value reached its numeric inlet at all: a list would have been ignored
    // there and the sink never touched.
    CHECK(sink.gotFloat);
    CHECK(sink.floatValue == doctest::Approx(72.f));
  }

  TEST_CASE("table: a dump drives a downstream chain value by value (#498)") {
    // The wavetable use case run through the real thing: a curve dumped into a
    // transposer.
    std::vector<std::string> log;
    Recorder notes;
    notes.log = &log;
    notes.tag = "note";
    YSE::pHandle noteHandle(&notes);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* t = p.CreateObject(YSE::OBJ::G_TABLE, "3");
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::G_ADD, "12");
    REQUIRE(t != nullptr);
    REQUIRE(add != nullptr);
    p.Connect(t, 0, add, 0);
    p.Connect(add, 0, &noteHandle, 0);

    t->SetListData(0, "set 0 60 62 64");
    log.clear();

    t->SetListData(0, "dump");
    REQUIRE(log.size() == 3);
    CHECK(log[0] == "note:f 72.000000");
    CHECK(log[1] == "note:f 74.000000");
    CHECK(log[2] == "note:f 76.000000");
  }

  TEST_CASE("table: a .trigger steps the pointer from inside the patch (#498)") {
    // How a patch actually drives one: an upstream object fires `next`, which is
    // a command the object has to read out of another outlet's list exactly as it
    // reads one handed in by the host. A command that only worked from the host
    // would make the object undrivable from inside a patch.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* step = p.CreateObject(YSE::OBJ::G_TRIGGER, "next");
    YSE::pHandle* t = p.CreateObject(YSE::OBJ::G_TABLE, "2");
    REQUIRE(step != nullptr);
    REQUIRE(t != nullptr);
    p.Connect(step, 0, t, 0);
    p.Connect(t, 0, &sinkHandle, 0);

    t->SetListData(0, "set 0 111 222");

    sink.reset();
    step->SetBang(0);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 111);

    sink.reset();
    step->SetBang(0);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 222);

    // And it wraps, so a sequencer driven this way loops on its own.
    sink.reset();
    step->SetBang(0);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 111);
  }

  TEST_CASE("table: a real graph writes values through the two inlets (#498)") {
    // Writing is the half a standalone rig cannot prove: the cold inlet has to be
    // reachable through a real connection, and the hot inlet has to still see the
    // value that arrived on it. This is the curve-building idiom.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* addresses = p.CreateObject(YSE::OBJ::G_INT);
    YSE::pHandle* vals = p.CreateObject(YSE::OBJ::G_INT);
    YSE::pHandle* t = p.CreateObject(YSE::OBJ::G_TABLE, "16");
    REQUIRE(addresses != nullptr);
    REQUIRE(vals != nullptr);
    REQUIRE(t != nullptr);
    p.Connect(vals, 0, t, 1);
    p.Connect(addresses, 0, t, 0);
    p.Connect(t, 0, &sinkHandle, 0);

    // value first, then address — the order a patch writes an entry in.
    vals->SetIntData(0, 440);
    addresses->SetIntData(0, 3);
    vals->SetIntData(0, 880);
    addresses->SetIntData(0, 9);

    sink.reset();
    addresses->SetIntData(0, 3);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 440);

    sink.reset();
    addresses->SetIntData(0, 9);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 880);
  }

  TEST_CASE("table: send hands a stored value to a named .r in the patch (#498)") {
    // Max: "sends the value stored at the incoming address to all receive objects
    // with that name" — the one place issue #498's NamedBus suggestion actually
    // fits, since .s / .r are exactly that mechanism here. Driven through a real
    // patcherImplementation because the delivery is the patcher's rather than the
    // object's: a T_GUI PassData is queued and drained by a Calculate, so the
    // explicit tick is part of the flow being tested (.forward's rig, #485).
    //
    // What is asserted is which receiver a value reaches, never by which of the
    // two delivery paths — in the monolithic test binary another translation unit
    // may have called System::init() first, in which case the receiver is also
    // subscribed to this patcher's own bus address.
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::pHandle* t = p.CreateObject(YSE::OBJ::G_TABLE, "8");
    YSE::pHandle* receiver = p.CreateObject(YSE::OBJ::G_RECEIVE, "curve");
    REQUIRE(t != nullptr);
    REQUIRE(receiver != nullptr);
    p.Connect(receiver, 0, &sinkHandle, 0);

    t->SetListData(0, "set 4 321");
    sink.reset();
    t->SetListData(0, "send curve 4");
    p.Calculate(YSE::T_DSP);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 321);

    // An out-of-range address sends nothing at all rather than a zero that would
    // read downstream as a stored value.
    sink.reset();
    t->SetListData(0, "send curve 99");
    p.Calculate(YSE::T_DSP);
    CHECK_FALSE(sink.gotInt);
  }
}
