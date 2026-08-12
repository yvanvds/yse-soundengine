// Tests for `.loadbang` and `.loadmess` (issue #547) — the pair that lets a
// saved patch describe its own starting state.
//
// Two layers are being pinned here and they are not the same claim.
//
// The **objects** are small: one sends a bang, the other sends a stored message
// in the kind it is, and both do it again for a bang at the inlet. Those cases
// drive standalone objects through a tap and are ordinary.
//
// The **lifecycle** is the issue. "Fire when loading has finished" is only
// meaningful if there is a defined moment at which loading *has* finished, and
// in this patcher that moment is late: `ParseJSON` creates every object, then
// restores every cord, then compiles the result into a GraphState and installs
// it with one atomic swap (issue #228). Anything fired before the last of those
// reaches a patch that does not exist yet. So the load cases here deliberately
// do **not** assert on the `.loadbang` object at all — they assert on a value
// that could only have arrived at the *far end of a chain of cords*, because
// that is the only assertion that can tell a correct firing point from a bang
// sent one line too early. Every one of them is paired with the same patch built
// live through `CreateObject`, which must read zero: that pairing is what makes
// the loaded value evidence rather than a coincidence, and it is also the test
// of the second half of the decision — a live-built object never fires.
//
// The `.s` case is the third ordering claim, and it is the one that hangs rather
// than fails if it regresses: the pass dispatches ordinary sends, and an
// ordinary send may reach a `.s`, whose `PassData` takes the patcher's mutex on
// the control thread. Firing from inside `ParseJSON`'s lock would deadlock a
// patch that merely initialises itself through a send.
//
// No audio device required.

#include <doctest/doctest.h>
#include <cstddef>
#include <string>
#include <vector>

#include "patcher/genericObjects/gLoadbang.h"
#include "patcher/genericObjects/gLoadmess.h"
#include "patcher/inlet.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObject.h"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "support/alloc_probe.hpp"

using YSE::PATCHER::gLoadbang;
using YSE::PATCHER::gLoadmess;
using YSE::PATCHER::Register;

namespace {

  // Records what arrived and in what shape, because the shape is half of what
  // `.loadmess` promises: a stored `60` that came back as the list "60" would
  // be a different message to every object downstream of it, and a rig that
  // normalised the two could not tell them apart.
  struct Tap : YSE::PATCHER::pObject {
    std::vector<std::string> log;

    Tap() : pObject(false) {
      log.reserve(32);
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { log.push_back("bang"); });
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { log.push_back("i" + std::to_string(v)); });
      inputs.back().RegisterFloat(
          [this](float v, int, YSE::THREAD) { log.push_back("f" + std::to_string(v)); });
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { log.push_back("l" + v); });
    }
    const char* Type() const override {
      return "loadbang_tap";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    std::string trace() const {
      std::string out;
      for (const std::string& entry : log) {
        if (!out.empty()) out.push_back(' ');
        out += entry;
      }
      return out;
    }
  };

  // The same sink for the allocation cases, counting instead of describing.
  // `Tap` above builds a std::string per message, which is the *test rig*
  // allocating on the path being measured — the object under test would then be
  // convicted of its observer's behaviour. Downstream delivery has to stay in
  // the probed region (a send that reached nothing would measure nothing), so
  // what changes is the sink, not the wiring.
  struct CountTap : YSE::PATCHER::pObject {
    int bangs = 0;
    int ints = 0;
    int floats = 0;
    int lists = 0;

    CountTap() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { bangs++; });
      inputs.back().RegisterInt([this](int, int, YSE::THREAD) { ints++; });
      inputs.back().RegisterFloat([this](float, int, YSE::THREAD) { floats++; });
      inputs.back().RegisterList([this](const std::string&, int, YSE::THREAD) { lists++; });
    }
    const char* Type() const override {
      return "loadbang_count_tap";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    int total() const {
      return bangs + ints + floats + lists;
    }
  };

  // A standalone `.loadmess` with a tap on its outlet. Neither object needs a
  // patcher: the message behaviour is entirely local, and the load hook is a
  // public method the patcher calls, so it can be called here the same way.
  struct MessRig {
    gLoadmess obj;
    Tap tap;

    explicit MessRig(const std::string& args = "") {
      if (!args.empty()) obj.SetParams(args);
      obj.GetOutlet(0)->Connect(tap.GetInlet(0));
      tap.GetInlet(0)->Connect(obj.GetOutlet(0));
    }
    MessRig(const MessRig&) = delete;
    MessRig& operator=(const MessRig&) = delete;
    MessRig(MessRig&&) = delete;
    MessRig& operator=(MessRig&&) = delete;

    // What the patcher's post-publish pass does to this object.
    void Load() {
      obj.Loadbang(YSE::T_GUI);
    }
    void Bang() {
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
    }
    void List(const std::string& value) {
      obj.GetInlet(0)->SetList(value, YSE::T_GUI);
    }
    // A message box's text: it travels by SendMessage and lands on the
    // `anything` handler rather than on the list one.
    void Message(const std::string& value) {
      obj.GetInlet(0)->SetMessage(value, YSE::T_GUI);
    }
  };

  // The patch every load case below is built from, in one place so the loaded
  // and the live-built halves are provably the same patch.
  //
  //   0: the load object under test
  //   1: `.i 7`  — a bang makes it emit 7, which is what gives `.loadbang` a
  //               payload to be observed by
  //   2: `.i`    — the far end, and the only thing any of these cases reads
  //
  // Storage IDs are 0, 1, 2 in creation order and survive the round trip
  // (issue #730), so the far end is `GetHandleFromID(2)` on either patcher.
  void BuildChain(YSE::patcher& p, const char* type, const std::string& args) {
    YSE::pHandle* load = p.CreateObject(type, args);
    YSE::pHandle* seven = p.CreateObject(YSE::OBJ::G_INT, "7");
    YSE::pHandle* sink = p.CreateObject(YSE::OBJ::G_INT, "");
    REQUIRE(load != nullptr);
    REQUIRE(seven != nullptr);
    REQUIRE(sink != nullptr);
    p.Connect(load, 0, seven, 0);
    p.Connect(seven, 0, sink, 0);
  }

  std::string FarEnd(YSE::patcher& p) {
    YSE::pHandle* sink = p.GetHandleFromID(2);
    REQUIRE(sink != nullptr);
    return sink->GetGuiValue();
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── the objects exist ──────────────────────────────────────────────────────

  TEST_CASE("loadbang: both objects are registered and have Max's shape (#547)") {
    auto names = Register().AllNames();
    bool hasBang = false;
    bool hasMess = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_LOADBANG)) hasBang = true;
      if (name == std::string(YSE::OBJ::G_LOADMESS)) hasMess = true;
    }
    CHECK(hasBang);
    CHECK(hasMess);

    std::unique_ptr<YSE::PATCHER::pObject> bang(Register().Get(YSE::OBJ::G_LOADBANG));
    REQUIRE(bang != nullptr);
    CHECK(std::string(bang->Type()) == ".loadbang");
    // One inlet — Max's manual trigger, "sending a bang message to a loadbang
    // object causes it to output a bang message" — and one outlet.
    CHECK(bang->NumInputs() == 1);
    CHECK(bang->NumOutputs() == 1);

    std::unique_ptr<YSE::PATCHER::pObject> mess(Register().Get(YSE::OBJ::G_LOADMESS));
    REQUIRE(mess != nullptr);
    CHECK(std::string(mess->Type()) == ".loadmess");
    CHECK(mess->NumInputs() == 1);
    CHECK(mess->NumOutputs() == 1);
  }

  // ─── the lifecycle: when "loading finished" is ──────────────────────────────

  TEST_CASE("loadbang: a loaded patch fires it down its whole chain (#547)") {
    // The issue's crux, and the only assertion that can see it. The value read
    // at the end is 7, which is a number *no object in the patch holds until a
    // bang has travelled two cords*: `.loadbang` has no payload, and the 7 lives
    // on the middle object, which only emits when banged.
    //
    // So a `.loadbang` that fired from its constructor would leave 0 here (no
    // cords yet). One that fired at the end of ParseJSON's create loop would
    // leave 0 too (objects, but no connections restored). Only a bang sent after
    // the graph is built, wired and published reaches the far end — which is
    // what makes this case, and not any assertion on the object itself, the test
    // of the firing point.
    YSE::patcher src;
    src.create(2);
    BuildChain(src, YSE::OBJ::G_LOADBANG, "");

    // The control, and the second half of the decision: built live through
    // CreateObject, nothing has fired. A test that only checked the loaded
    // patcher could not tell "fires at the right moment" from "fires always".
    CHECK(FarEnd(src) == "0");

    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 3);

    CHECK(FarEnd(loaded) == "7");
  }

  TEST_CASE("loadbang: .loadmess carries a payload down the same chain (#547)") {
    // Same lifecycle, but the value is the object's own: `.loadmess 42` needs no
    // second object to have something to say, so the far end reading 42 says the
    // stored message travelled the cords rather than merely that a bang did.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* load = src.CreateObject(YSE::OBJ::G_LOADMESS, "42");
    YSE::pHandle* sink = src.CreateObject(YSE::OBJ::G_INT, "");
    REQUIRE(load != nullptr);
    REQUIRE(sink != nullptr);
    src.Connect(load, 0, sink, 0);

    CHECK(src.GetHandleFromID(1)->GetGuiValue() == "0");

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(src.DumpJSON());
    REQUIRE(loaded.Objects() == 2);

    CHECK(loaded.GetHandleFromID(1)->GetGuiValue() == "42");
  }

  TEST_CASE("loadbang: an object created live in a running patcher never fires (#547)") {
    // The decision the issue asks to be made and documented, stated on its own
    // rather than as a side effect of another case. A `.loadbang` added to a
    // patch that is already loaded stays silent — at creation time it has no
    // cords, and re-firing on a later edit would re-run the patch's
    // initialisation every time the patch was touched.
    //
    // Building the chain *after* the parse is the sharp version of the claim:
    // the patcher has already been through a load, so "it only fires during a
    // load" is being distinguished from "it fires once per patcher".
    YSE::patcher p;
    p.create(2);
    p.ParseJSON("{}");

    BuildChain(p, YSE::OBJ::G_LOADBANG, "");
    CHECK(FarEnd(p) == "0");

    YSE::pHandle* load = p.GetHandleFromID(0);
    REQUIRE(load != nullptr);

    // And the answer for that case, which is why silence is affordable: the
    // inlet is Max's documented manual trigger, so a host that has just built a
    // graph object by object initialises it exactly as a load would have.
    load->SetBang(0);
    CHECK(FarEnd(p) == "7");
  }

  TEST_CASE("loadbang: a second load does not re-fire the first load's objects (#547)") {
    // The pass fires what *this* parse created, not everything in the patcher.
    // ParseJSON is additive, so a host that loads a second patch into a live
    // patcher must not have the first patch's initialisation replayed under it —
    // which would overwrite whatever has happened since, and is the same hazard
    // as firing on every edit.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* load = src.CreateObject(YSE::OBJ::G_LOADMESS, "42");
    YSE::pHandle* sink = src.CreateObject(YSE::OBJ::G_INT, "");
    REQUIRE(load != nullptr);
    REQUIRE(sink != nullptr);
    src.Connect(load, 0, sink, 0);
    const std::string first = src.DumpJSON();

    YSE::patcher other;
    other.create(2);
    YSE::pHandle* lone = other.CreateObject(YSE::OBJ::G_LOADBANG, "");
    REQUIRE(lone != nullptr);
    const std::string second = other.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(first);
    YSE::pHandle* far = loaded.GetHandleFromID(1);
    REQUIRE(far != nullptr);
    REQUIRE(far->GetGuiValue() == "42");

    // Inlet 1 sets without emitting, so this is the patch being used rather than
    // reset — exactly what a re-fire would trample.
    far->SetIntData(1, 0);
    REQUIRE(far->GetGuiValue() == "0");

    loaded.ParseJSON(second);
    CHECK(far->GetGuiValue() == "0");
  }

  TEST_CASE("loadbang: a patch that initialises through .s loads rather than hanging (#547)") {
    // The ordering claim that hangs instead of failing, so it is worth being
    // explicit: this case does not assert a value, it asserts *termination*.
    //
    // The pass dispatches ordinary synchronous sends, and an ordinary send may
    // land on a `.s`, whose PassData scans the patcher's object map under `mtx`
    // on the control thread. `mtx` is a plain std::mutex and ParseJSON holds it
    // for the whole build, so a pass fired from inside that lock deadlocks on a
    // patch whose only sin is initialising itself through a send. Scoping the
    // lock so the pass runs after it is released is what this pins.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* load = src.CreateObject(YSE::OBJ::G_LOADMESS, "5");
    YSE::pHandle* send = src.CreateObject(YSE::OBJ::G_SEND, "init");
    YSE::pHandle* receive = src.CreateObject(YSE::OBJ::G_RECEIVE, "init");
    YSE::pHandle* sink = src.CreateObject(YSE::OBJ::G_INT, "");
    REQUIRE(load != nullptr);
    REQUIRE(send != nullptr);
    REQUIRE(receive != nullptr);
    REQUIRE(sink != nullptr);
    src.Connect(load, 0, send, 0);
    src.Connect(receive, 0, sink, 0);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(src.DumpJSON());

    // Reaching this line at all is the assertion. The value is deliberately not
    // checked: an in-patcher `.s` on the control thread enqueues for the audio
    // thread to deliver, and nothing renders here.
    CHECK(loaded.Objects() == 4);
  }

  // ─── what .loadmess sends ───────────────────────────────────────────────────

  TEST_CASE("loadmess: the stored message leaves in the kind it is (#547)") {
    // Max: "any arguments you type into a loadmess object are treated as a
    // message to be sent when output is triggered." Which message it is decides
    // which inlet it can reach downstream, so the kind is the object.
    {
      // The single word `bang`, "the one message that is a word rather than a
      // number" — `.if`'s phrase and `.if`'s rule.
      MessRig rig("bang");
      rig.Load();
      CHECK(rig.tap.trace() == "bang");
    }
    {
      // Spelled as an int, so an int atom — the test `.trigger`, `.route` and
      // `.qlist` already share, which is what stops a stored 60 coming back as
      // "60.".
      MessRig rig("60");
      rig.Load();
      CHECK(rig.tap.trace() == "i60");
    }
    {
      // A decimal point makes it a float atom.
      MessRig rig("0.5");
      rig.Load();
      REQUIRE(rig.tap.log.size() == 1);
      CHECK(rig.tap.log[0][0] == 'f');
      CHECK(rig.tap.log[0].find("0.5") != std::string::npos);
    }
    {
      // Several tokens are one list, verbatim.
      MessRig rig("1 2 3");
      rig.Load();
      CHECK(rig.tap.trace() == "l1 2 3");
    }
    {
      // A word that is not a number and not `bang` is a symbol, and text travels
      // as a list message in this patcher — the deviation `.trigger` documents.
      MessRig rig("start");
      rig.Load();
      CHECK(rig.tap.trace() == "lstart");
    }
    {
      // Nothing typed, nothing sent. `.loadbang` is the object for wanting a
      // bang, so inventing one here would only make the two overlap.
      MessRig rig;
      rig.Load();
      CHECK(rig.tap.log.empty());
      CHECK(rig.obj.Sent() == 0);
    }
  }

  TEST_CASE("loadmess: a bang at the inlet sends the same message a load would (#547)") {
    // Max: "sending a bang message to a loadmess object causes it to output its
    // typed message." It has to be the *same* message, not a near-copy: a host
    // that initialises a live-built graph by banging must get what a load would
    // have sent, or the two ways of starting a patch would start it differently.
    MessRig rig("60");
    rig.Load();
    rig.Bang();
    rig.Bang();
    CHECK(rig.tap.trace() == "i60 i60 i60");
    CHECK(rig.obj.Sent() == 3);
    CHECK(rig.obj.Dropped() == 0);
  }

  TEST_CASE("loadbang: a bang at the inlet is the same bang a load sends (#547)") {
    gLoadbang obj;
    Tap tap;
    obj.GetOutlet(0)->Connect(tap.GetInlet(0));
    tap.GetInlet(0)->Connect(obj.GetOutlet(0));

    obj.Loadbang(YSE::T_GUI);
    CHECK(obj.Fired() == 1);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(obj.Fired() == 2);
    CHECK(tap.trace() == "bang bang");
  }

  // ─── Max's set ──────────────────────────────────────────────────────────────

  TEST_CASE("loadmess: set replaces the message without sending it (#547)") {
    // Max: "the word set followed by any message will set the message held by
    // loadmess without any output. (Can be used for output in conjunction with
    // bang.)" Both halves matter — the silence as much as the store.
    MessRig rig("60");
    rig.List("set 99");
    CHECK(rig.tap.log.empty());
    CHECK(rig.obj.Message() == "99");

    rig.Bang();
    CHECK(rig.tap.trace() == "i99");
  }

  TEST_CASE("loadmess: set works from a message box too (#547)") {
    // The other route text arrives by. A message box sends through
    // outlet::SendMessage, which lands on the `anything` handler rather than on
    // the list one, so a `set` implemented only on the list inlet would be
    // silently ignored by the most obvious way of sending one.
    MessRig rig("60");
    rig.Message("set hello world");
    CHECK(rig.tap.log.empty());
    CHECK(rig.obj.Message() == "hello world");

    rig.Bang();
    CHECK(rig.tap.trace() == "lhello world");
  }

  TEST_CASE("loadmess: a bare set empties the message (#547)") {
    // `set` with nothing after it is still a set — of nothing — and what it
    // leaves behind is exactly the object a `.loadmess` with no arguments is.
    MessRig rig("60");
    rig.List("set");
    CHECK(rig.obj.Message().empty());

    rig.Bang();
    CHECK(rig.tap.log.empty());
  }

  TEST_CASE("loadmess: extra spaces after set are not part of the message (#547)") {
    // `set  0.5` must store the float, not the list " 0.5": the surrounding
    // whitespace was never part of what anyone typed, and leaving it in would
    // change the *kind* of the message and so which inlet it can reach.
    MessRig rig;
    rig.List("set   0.5   ");
    CHECK(rig.obj.Message() == "0.5");

    rig.Bang();
    REQUIRE(rig.tap.log.size() == 1);
    CHECK(rig.tap.log[0][0] == 'f');
  }

  TEST_CASE("loadmess: a list that is not a set is ignored (#547)") {
    // Max documents exactly bang, set and a double-click. Treating a bare list
    // as an implicit set would let every value that happened to pass through
    // this object overwrite the initialisation it exists to hold — and the
    // object would then be a `.m` with a worse name.
    MessRig rig("60");
    rig.List("99");
    rig.List("setup 3"); // not `set`: the word has to end where it ends
    rig.Message("hello");
    CHECK(rig.tap.log.empty());
    CHECK(rig.obj.Message() == "60");
  }

  TEST_CASE("loadmess: an over-long set is refused whole (#547)") {
    // `.capture`'s rule rather than `.print`'s, and for `.capture`'s reason: this
    // stores a message that will later be *sent*, and half of one is a different
    // message. Truncating would quietly initialise a patch with something nobody
    // wrote. The refusal is counted, so it is visible rather than silent.
    MessRig rig("60");
    const std::string huge(gLoadmess::MESSAGE_CAPACITY + 1, 'z');
    rig.List("set " + huge);
    CHECK(rig.obj.Message() == "60");
    CHECK(rig.obj.Dropped() == 1);

    // One character shorter is accepted, so the bound is the bound and not an
    // off-by-one somewhere near it.
    const std::string fits(gLoadmess::MESSAGE_CAPACITY, 'z');
    rig.List("set " + fits);
    CHECK(rig.obj.Message() == fits);
    CHECK(rig.obj.Dropped() == 1);
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("loadmess: the creation arguments survive a DumpJSON / ParseJSON round trip (#547)") {
    // A reloaded patch whose `.loadmess` lost its arguments would initialise
    // nothing, which is the whole of what the object is for — and the failure
    // would be invisible until the patch behaved oddly much later.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::G_LOADMESS, "1 2 3");
    REQUIRE(obj != nullptr);

    const std::string json = src.DumpJSON();
    CHECK(json.find(".loadmess") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".loadmess");
    CHECK(copy->GetParams() == "1 2 3");
  }

  TEST_CASE("loadmess: SetParams re-reads the message, and an empty one clears it (#547)") {
    // The clear callback is the whole of `SetParams("")`: Parameters::Set returns
    // without calling the parse callback for an empty argument, so without it the
    // object would keep the message it had and a patch could not be returned to
    // Max's no-argument shape.
    MessRig rig("60");
    CHECK(rig.obj.Message() == "60");

    rig.obj.SetParams("1 2 3");
    CHECK(rig.obj.Message() == "1 2 3");

    rig.obj.SetParams("");
    CHECK(rig.obj.Message().empty());
    rig.Load();
    CHECK(rig.tap.log.empty());
  }

  TEST_CASE("loadmess: an over-long argument list still loads (#547)") {
    // A creation argument arriving from a hand-edited or newer saved patch must
    // never be able to break loading it, so the parse stops at the last token
    // that fits rather than throwing. The argument string is stored verbatim by
    // Parameters either way, so a save still writes back what was typed.
    std::string args;
    while (args.size() < gLoadmess::MESSAGE_CAPACITY * 2) {
      if (!args.empty()) args.push_back(' ');
      args += "12345";
    }

    MessRig rig(args);
    CHECK(rig.obj.Message().size() <= gLoadmess::MESSAGE_CAPACITY);
    CHECK_FALSE(rig.obj.Message().empty());
    CHECK(rig.obj.GetParams() == args);
  }

  // ─── real-time discipline ───────────────────────────────────────────────────

  TEST_CASE("loadmess: the message paths allocate nothing (#547)") {
    // A message handler runs on whichever thread dispatched the message, and
    // in-patcher delivery dispatches on T_DSP, so both `set` and the bang are
    // audio-thread code. The store is a string whose capacity is reserved at
    // construction for exactly this reason, and the sends hand the outlet that
    // string by reference rather than building a new one.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    gLoadmess obj;
    CountTap tap;
    obj.SetParams("60");
    obj.GetOutlet(0)->Connect(tap.GetInlet(0));
    tap.GetInlet(0)->Connect(obj.GetOutlet(0));

    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message. The long one measures the
    // refusing branch, which is the one that could most easily have reached for
    // a std::string on the way to saying no.
    const std::string setNumber = "set 99";
    const std::string setList = "set 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16";
    const std::string setBang = "set bang";
    const std::string ignored = "99";
    const std::string huge = "set " + std::string(gLoadmess::MESSAGE_CAPACITY + 1, 'z');
    // The count is read out and asserted *outside* the scope. doctest's own
    // assertion machinery allocates the first time it runs, so a CHECK placed
    // inside the armed region can convict the code under test of doctest's
    // warm-up — which is exactly what happened while this case was being
    // written, and only in this case, because it is the first probed one in the
    // file. Reading the counter into an int and checking it afterwards measures
    // the messages and nothing else.
    int combined = -1;
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetBang(YSE::T_GUI); // the int form
      obj.GetInlet(0)->SetList(setList, YSE::T_GUI);
      // The list form: the outlet takes the stored string by reference rather
      // than building a new one, which is what this measures.
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
      obj.GetInlet(0)->SetList(setBang, YSE::T_GUI);
      obj.GetInlet(0)->SetBang(YSE::T_GUI); // the bang form
      obj.GetInlet(0)->SetList(setNumber, YSE::T_GUI);
      // The message-box route into the same reader.
      obj.GetInlet(0)->SetMessage(setNumber, YSE::T_GUI);
      obj.GetInlet(0)->SetList(ignored, YSE::T_GUI); // refused: not a set
      obj.GetInlet(0)->SetList(huge, YSE::T_GUI); // refused: past the capacity
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
      combined = TestHelpers::g_alloc_count.load();
    }
    CHECK(combined == 0);

    // The probed messages really did something — an assertion that only proves
    // nothing happened proves nothing. Four sends, one of every kind the object
    // can produce plus a repeat, and one refusal.
    CHECK(obj.Sent() == 4);
    CHECK(obj.Dropped() == 1);
    CHECK(tap.total() == 4);
    CHECK(tap.ints == 2);
    CHECK(tap.lists == 1);
    CHECK(tap.bangs == 1);
  }

  TEST_CASE("loadbang: sending allocates nothing (#547)") {
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    gLoadbang obj;
    CountTap tap;
    obj.GetOutlet(0)->Connect(tap.GetInlet(0));
    tap.GetInlet(0)->Connect(obj.GetOutlet(0));

    // Read out and asserted outside the scope, for the reason the `.loadmess`
    // case above spells out.
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      obj.Loadbang(YSE::T_GUI);
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);
    CHECK(obj.Fired() == 2);
    CHECK(tap.bangs == 2);
  }

} // TEST_SUITE
