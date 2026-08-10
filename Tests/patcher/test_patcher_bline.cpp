// Tests for `.bline` — Max's bline, "generates a linear ramp driven by
// incoming bang messages" (issue #511).
//
// `.bline` is `.line` with the clock taken out, so the two files divide the
// work: everything about the *shared* ramp core — the breakpoint queue, the
// interpolation, the arrival bang, Max's output typing — is proved once in
// test_patcher_line.cpp against `.line`, and what is asserted here is the half
// that is this object's own:
//
//   - **the bang is the whole timebase**: a value comes out on a bang and at no
//     other moment, a breakpoint pair counts bangs rather than milliseconds,
//     and a bang with nothing running does nothing at all.
//
//   - **nothing is emitted at message time**, which is the one place the base's
//     behaviour had to be bent: Max's "the time is considered 0 and bline
//     outputs the target value *when it receives a bang*" means a bare number
//     arms a one-bang segment rather than arriving on the spot, where `.line`
//     with no time arrives immediately.
//
//   - **no clock is involved anywhere**: the object behaves identically
//     standing alone and inside a rendering patcher, and it never touches the
//     deferred-message scheduler. That is asserted rather than assumed — a
//     regression that quietly gave this object a scheduler would pass every
//     value assertion in the file and fail here.
//
//   - **the two words**, `stop` and `set`, and the absence of Max's `pause` /
//     `resume`, which `bline` does not have.
//
//   - **the refusals and the RT discipline**: an over-long list, and the
//     allocation probe over every message path including the bang.
//
// Nothing here ever sleeps, and almost nothing here renders: the object's clock
// is the patch, so a test that wants time to pass sends a bang.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "patcher/time/gLine.h"
#include "patcher/time/messageScheduler.h"
#include "support/alloc_probe.hpp"

using YSE::PATCHER::gBline;
using YSE::PATCHER::messageScheduler;
using YSE::PATCHER::patcherImplementation;

namespace {

  // Records every message this object receives, in order and *with its type* —
  // the type being half of what a line-family object promises, since a ramp
  // that always emitted floats would not reach the `.i` a patch wired it to.
  struct BlineRecorder : YSE::PATCHER::pObject {
    struct Event {
      char kind = 'i'; // 'b' bang, 'i' int, 'f' float, 'l' list
      int intValue = 0;
      float floatValue = 0.f;
      std::string text;
    };

    std::vector<Event> events;

    BlineRecorder() : pObject(false) {
      // Reserved up front so the allocation probe measures the *object* rather
      // than this sink's own vector growing under it.
      events.reserve(1024);
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) {
        Event e;
        e.kind = 'b';
        events.push_back(e);
      });
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        Event e;
        e.kind = 'i';
        e.intValue = v;
        events.push_back(e);
      });
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
        Event e;
        e.kind = 'f';
        e.floatValue = v;
        events.push_back(e);
      });
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        Event e;
        e.kind = 'l';
        e.text = v;
        events.push_back(e);
      });
    }
    const char* Type() const override {
      return "bline_recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    std::size_t n() const {
      return events.size();
    }
    double number(std::size_t i) const {
      return events[i].kind == 'f' ? (double)events[i].floatValue : (double)events[i].intValue;
    }
    double last() const {
      return number(events.size() - 1);
    }
  };

  using TestHelpers::Wire;

  // A standalone `.bline` with a recorder on each outlet. Standalone is not a
  // compromise for this object the way it is for `.line`: with no clock to
  // stand in for, a `.bline` outside a patcher behaves exactly as one inside
  // it, which is most of what this file is here to prove.
  //
  // Both recorders are declared **before** the object so they are destroyed
  // after it — see Wire on why that matters even for a symmetric edge.
  struct Rig {
    BlineRecorder out;
    BlineRecorder done;
    gBline obj;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) obj.SetParams(args);
      Wire(obj, 0, out);
      Wire(obj, 1, done);
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    Rig(Rig&&) = delete;
    Rig& operator=(Rig&&) = delete;

    void Bang(int times = 1) {
      for (int i = 0; i < times; i++)
        obj.GetInlet(0)->SetBang(YSE::T_GUI);
    }
    void Int(int value) {
      obj.GetInlet(0)->SetInt(value, YSE::T_GUI);
    }
    void Float(float value) {
      obj.GetInlet(0)->SetFloat(value, YSE::T_GUI);
    }
    void List(const std::string& message) {
      obj.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("bline: the object is creatable through the registry (#511)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* bline = p.CreateObject(YSE::OBJ::G_BLINE);
    REQUIRE(bline != nullptr);
    CHECK(std::string(bline->Type()) == ".bline");
  }

  TEST_CASE("bline: it is listed by pRegistry::AllNames alongside .line (#511)") {
    auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".bline")) != names.end());
    // A sibling of `.line`, not a replacement for it: the two differ only in
    // what moves the cursor, and a patch may well want both.
    CHECK(std::find(names.begin(), names.end(), std::string(".line")) != names.end());
  }

  TEST_CASE("bline: the shape is Max's — one inlet, a value outlet and a bang (#511)") {
    // `.line` has three inlets because it has two times to be told. This one
    // has neither a ramp time nor a grain, so it has one.
    gBline bline;
    CHECK(bline.NumInputs() == 1);
    CHECK(bline.NumOutputs() == 2);
    CHECK(bline.GetOutputType(0) == YSE::OUT_TYPE::ANY);
    CHECK(bline.GetOutputType(1) == YSE::OUT_TYPE::BANG);
    CHECK(bline.GetCategory() == YSE::PATCHER::pCategory::TIME);
  }

  TEST_CASE("bline: a fresh object has Max's defaults and is not ramping (#511)") {
    gBline bline;
    // "If the first argument is an int, the bline object outputs integer
    // values" — and with no argument at all the stored value is 0 and the
    // object is an int one.
    CHECK(bline.Value() == doctest::Approx(0.0));
    CHECK_FALSE(bline.FloatObject());
    CHECK_FALSE(bline.IsRunning());
    CHECK(bline.Pending() == 0u);
    CHECK(bline.Dropped() == 0u);
  }

  TEST_CASE("bline: the creation argument's spelling decides the output type (#511)") {
    // Max: "an argument may be used to set the initial value to be stored and
    // the output type for the object — if the first argument is an int, the
    // bline object outputs integer values, and a float will set the bline
    // object to output floating point values."
    Rig asInt("5");
    CHECK(asInt.obj.Value() == doctest::Approx(5.0));
    CHECK_FALSE(asInt.obj.FloatObject());

    Rig asFloat("0.");
    CHECK(asFloat.obj.FloatObject());

    Rig alsoFloat("1.5");
    CHECK(alsoFloat.obj.FloatObject());
    CHECK(alsoFloat.obj.Value() == doctest::Approx(1.5));

    // An argument that is not a number leaves both at the default.
    Rig nonsense("wibble");
    CHECK(nonsense.obj.Value() == doctest::Approx(0.0));
    CHECK_FALSE(nonsense.obj.FloatObject());
  }

  // ─── the bang is the whole timebase ─────────────────────────────────────────

  TEST_CASE("bline: a list ramps one step per bang and bangs on arrival (#511)") {
    // Max: the second number of a pair is "an integer that specifies the number
    // of bang messages that will have to be received before reaching the target
    // value", and a bang "sends a new step in the breakpoint list out the left
    // outlet. If the current list of ramp segments is finished, a bang message
    // will be sent out the right outlet."
    Rig rig;
    rig.List("100 4");

    // Nothing at message time: this object's clock is the patch.
    CHECK(rig.out.n() == 0);
    CHECK(rig.done.n() == 0);
    CHECK(rig.obj.IsRunning());

    rig.Bang();
    REQUIRE(rig.out.n() == 1);
    CHECK(rig.out.number(0) == doctest::Approx(25.0));
    CHECK(rig.done.n() == 0);

    rig.Bang();
    rig.Bang();
    REQUIRE(rig.out.n() == 3);
    CHECK(rig.out.number(1) == doctest::Approx(50.0));
    CHECK(rig.out.number(2) == doctest::Approx(75.0));
    CHECK(rig.done.n() == 0);

    // The fourth bang is the arrival: the target exactly, and the bang that
    // says so.
    rig.Bang();
    REQUIRE(rig.out.n() == 4);
    CHECK(rig.out.number(3) == doctest::Approx(100.0));
    CHECK(rig.done.n() == 1);
    CHECK(rig.done.events[0].kind == 'b');
    CHECK_FALSE(rig.obj.IsRunning());
    CHECK(rig.obj.Value() == doctest::Approx(100.0));
  }

  TEST_CASE("bline: a bang with no ramp running does nothing (#511)") {
    // Not a value, and not a second arrival bang: Max's right outlet fires when
    // the list finishes, not on every bang after it.
    Rig rig;
    rig.Bang(8);
    CHECK(rig.out.n() == 0);
    CHECK(rig.done.n() == 0);

    rig.List("10 2");
    rig.Bang(2);
    REQUIRE(rig.out.n() == 2);
    CHECK(rig.done.n() == 1);

    // The ramp is over; banging on changes nothing.
    rig.Bang(8);
    CHECK(rig.out.n() == 2);
    CHECK(rig.done.n() == 1);
  }

  TEST_CASE("bline: a bare number is reached on the next bang, not on arrival (#511)") {
    // The one place the shared core had to be bent for this object. Max's int:
    // "sets the bline object to the specified integer value. Any and all
    // pending breakpoint segments are forgotten (i.e. the time is considered 0
    // and bline outputs the target value *when it receives a bang*)." `.line`
    // with no time arrives on the spot; this one waits to be banged.
    Rig rig;
    rig.Int(5);
    CHECK(rig.out.n() == 0);
    CHECK(rig.done.n() == 0);
    CHECK(rig.obj.IsRunning());

    rig.Bang();
    REQUIRE(rig.out.n() == 1);
    CHECK(rig.out.events[0].kind == 'i');
    CHECK(rig.out.events[0].intValue == 5);
    CHECK(rig.done.n() == 1);
    CHECK(rig.obj.Value() == doctest::Approx(5.0));

    // A float behaves the same way.
    rig.Float(2.5f);
    CHECK(rig.out.n() == 1);
    rig.Bang();
    REQUIRE(rig.out.n() == 2);
    CHECK(rig.obj.Value() == doctest::Approx(2.5));
    CHECK(rig.done.n() == 2);
  }

  TEST_CASE("bline: a pair counted zero or less is one bang, not none (#511)") {
    // Every segment costs at least the bang that travels it, which is what
    // keeps "nothing comes out except on a bang" true for a list too.
    Rig rig;
    rig.List("7 0");
    CHECK(rig.out.n() == 0);
    rig.Bang();
    REQUIRE(rig.out.n() == 1);
    CHECK(rig.out.number(0) == doctest::Approx(7.0));
    CHECK(rig.done.n() == 1);

    Rig negative;
    negative.List("7 -4");
    CHECK(negative.out.n() == 0);
    negative.Bang();
    REQUIRE(negative.out.n() == 1);
    CHECK(negative.out.number(0) == doctest::Approx(7.0));
    CHECK(negative.done.n() == 1);
  }

  TEST_CASE("bline: a breakpoint list travels its segments in order and bangs once (#511)") {
    // Max's list rule, in bangs: "100 4 0 4" climbs over four bangs and comes
    // back down over four more, with one arrival at the end of the whole list.
    Rig rig;
    rig.List("100 4 0 4");
    CHECK(rig.obj.Pending() == 1u); // the second segment, queued

    rig.Bang(4);
    REQUIRE(rig.out.n() == 4);
    CHECK(rig.out.number(3) == doctest::Approx(100.0));
    CHECK(rig.done.n() == 0); // the list is not finished

    rig.Bang(4);
    REQUIRE(rig.out.n() == 8);
    CHECK(rig.out.number(4) == doctest::Approx(75.0));
    CHECK(rig.out.last() == doctest::Approx(0.0));
    CHECK(rig.done.n() == 1);
    CHECK_FALSE(rig.obj.IsRunning());
  }

  TEST_CASE("bline: a three-element list is one pair with the third number ignored (#511)") {
    // `.line`'s third element is the grain. There is no grain here, so the rule
    // is Max's fallback: an odd trailing element is dropped.
    Rig rig;
    rig.List("10 2 999");
    rig.Bang(2);
    REQUIRE(rig.out.n() == 2);
    CHECK(rig.out.last() == doctest::Approx(10.0));
    CHECK(rig.done.n() == 1);
  }

  TEST_CASE("bline: a fractional count arrives on the first bang at or past the target (#511)") {
    // Max asks for an integer count; a float one is travelled in whole bangs.
    Rig rig;
    rig.List("10 2.5");
    rig.Bang(2);
    CHECK(rig.done.n() == 0);
    rig.Bang();
    REQUIRE(rig.out.n() == 3);
    CHECK(rig.out.last() == doctest::Approx(10.0));
    CHECK(rig.done.n() == 1);
  }

  TEST_CASE("bline: a number mid-ramp clears the queue and starts from where it stands (#511)") {
    // Max: "any and all pending breakpoint segments are forgotten", and the
    // family's continuity rule — a redirected ramp starts from the value the
    // object currently stands at.
    Rig rig;
    rig.List("100 4 0 4");
    rig.Bang(2);
    REQUIRE(rig.out.n() == 2);
    const double interrupted = rig.out.last();
    CHECK(interrupted == doctest::Approx(50.0));

    rig.List("0 2");
    CHECK(rig.obj.Pending() == 0u); // the queued descent is gone
    CHECK(rig.out.n() == 2); // and nothing came out on the message

    rig.Bang();
    REQUIRE(rig.out.n() == 3);
    // No jump: the new ramp leaves from where the old one had got to.
    CHECK(rig.out.number(2) == doctest::Approx(25.0));
    rig.Bang();
    CHECK(rig.out.last() == doctest::Approx(0.0));
    CHECK(rig.done.n() == 1); // the interrupted segment never arrived
  }

  // ─── the two words ──────────────────────────────────────────────────────────

  TEST_CASE("bline: 'stop' freezes the ramp and does not bang (#511)") {
    // Max: "stops bline from sending out numbers, until a new list of ramp
    // segments is received."
    Rig rig;
    rig.List("100 4");
    rig.Bang(2);
    REQUIRE(rig.out.n() == 2);
    const double frozen = rig.out.last();

    rig.List("stop");
    CHECK_FALSE(rig.obj.IsRunning());
    rig.Bang(8);
    CHECK(rig.out.n() == 2); // banging a stopped ramp does nothing
    CHECK(rig.done.n() == 0); // it never arrived
    CHECK(rig.obj.Value() == doctest::Approx(frozen));

    // A new list starts from the frozen value.
    rig.List("100 2");
    rig.Bang(2);
    REQUIRE(rig.out.n() == 4);
    CHECK(rig.out.last() == doctest::Approx(100.0));
    CHECK(rig.done.n() == 1);
  }

  TEST_CASE("bline: 'set' moves the stored value and arms nothing (#511)") {
    // The documented departure: Max's page gives `set` the same sentence as
    // `int`, which would make the two the same message. They are read apart
    // here the way `line` reads them apart — `set` is a silent move that leaves
    // nothing for the next bang to emit.
    Rig rig;
    rig.List("set 42");
    CHECK(rig.obj.Value() == doctest::Approx(42.0));
    CHECK_FALSE(rig.obj.IsRunning());
    rig.Bang(4);
    CHECK(rig.out.n() == 0);
    CHECK(rig.done.n() == 0);

    // And the next target ramps from there rather than from 0.
    rig.List("46 2");
    rig.Bang();
    REQUIRE(rig.out.n() == 1);
    CHECK(rig.out.number(0) == doctest::Approx(44.0));

    // `set` also stops a ramp in progress.
    rig.List("set 0");
    CHECK_FALSE(rig.obj.IsRunning());
    rig.Bang(4);
    CHECK(rig.out.n() == 1);
  }

  TEST_CASE("bline: Max's 'pause' and 'resume' are not words here (#511)") {
    // `bline` has neither — on an object clocked by the patch, not banging it
    // is the pause. Both are read as list data, and neither has a number in it,
    // so both do nothing at all rather than silently halting the ramp.
    Rig rig;
    rig.List("100 4");
    rig.List("pause");
    CHECK(rig.obj.IsRunning());
    rig.Bang();
    REQUIRE(rig.out.n() == 1);
    CHECK(rig.out.number(0) == doctest::Approx(25.0));

    rig.List("resume");
    rig.Bang(3);
    CHECK(rig.out.n() == 4);
    CHECK(rig.out.last() == doctest::Approx(100.0));
    CHECK(rig.done.n() == 1);
  }

  TEST_CASE("bline: Calculate sends nothing (#511)") {
    // The object is driven by its inlet and by nothing else; one that emitted
    // would send a value on every DSP tick from a stimulus no patch sent.
    Rig rig;
    rig.List("100 4");
    for (int i = 0; i < 16; i++)
      rig.obj.Calculate(YSE::T_DSP);
    CHECK(rig.out.n() == 0);
    CHECK(rig.done.n() == 0);
  }

  // ─── no clock, anywhere ─────────────────────────────────────────────────────

  TEST_CASE("bline: rendering a patcher moves the ramp not at all (#511)") {
    // The claim that separates this object from every other one in its
    // directory: there is no scheduler, no timer and no block clock behind it,
    // so a patcher rendering for as long as it likes changes nothing until a
    // bang arrives. A regression that gave this object a clock would pass every
    // other assertion in this file.
    patcherImplementation p(1, nullptr);
    BlineRecorder out;
    BlineRecorder done;
    YSE::pHandle outHandle(&out);
    YSE::pHandle doneHandle(&done);
    YSE::pHandle* bline = p.CreateObject(YSE::OBJ::G_BLINE, "0");
    REQUIRE(bline != nullptr);
    p.Connect(bline, 0, &outHandle, 0);
    p.Connect(bline, 1, &doneHandle, 0);

    bline->SetListData(0, "100 4");
    // Nothing armed: the pending set is the patcher-wide count of deferred
    // messages, and this object never puts one there.
    CHECK(p.Scheduler()->PendingCount() == 0);

    for (std::uint64_t i = 0; i < messageScheduler::BlocksForMillis(500); i++)
      p.Calculate(YSE::T_DSP);
    CHECK(out.n() == 0);
    CHECK(done.n() == 0);
    CHECK(p.Scheduler()->PendingCount() == 0);

    // And the four bangs it was waiting for still finish it.
    for (int i = 0; i < 4; i++)
      bline->SetBang(0);
    REQUIRE(out.n() == 4);
    CHECK(out.last() == doctest::Approx(100.0));
    CHECK(done.n() == 1);
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  // ─── the output typing ──────────────────────────────────────────────────────

  TEST_CASE("bline: an int object emits ints and a float object emits floats (#511)") {
    Rig asInt("0");
    Rig asFloat("0.");
    asInt.List("100 4");
    asFloat.List("100 4");
    asInt.Bang(4);
    asFloat.Bang(4);

    REQUIRE(asInt.out.n() == 4);
    REQUIRE(asFloat.out.n() == 4);
    for (std::size_t i = 0; i < asInt.out.n(); i++) {
      CAPTURE(i);
      CHECK(asInt.out.events[i].kind == 'i');
      CHECK(asFloat.out.events[i].kind == 'f');
    }
  }

  TEST_CASE("bline: an int object emits floats for a ramp too small to show (#511)") {
    // Max's floatoutput default of 2, auto: floats when the distance is <= 1,
    // the object has no float argument, and the step is < 0.4. Without it a
    // ramp from 0 to 1 over ten bangs is a run of zeroes and then a one.
    Rig rig("0");
    rig.List("1 10");
    rig.Bang(10);

    REQUIRE(rig.out.n() == 10);
    bool sawIntermediate = false;
    for (std::size_t i = 0; i < rig.out.n(); i++) {
      CAPTURE(i);
      CHECK(rig.out.events[i].kind == 'f');
      const double v = rig.out.number(i);
      if (v > 0.0 && v < 1.0) sawIntermediate = true;
    }
    CHECK(sawIntermediate);
    CHECK(rig.out.last() == doctest::Approx(1.0));

    // The rule is per segment, not per object: the same object ramping further
    // than one unit is an int object again.
    Rig wide("0");
    wide.List("50 10");
    wide.Bang(2);
    REQUIRE(wide.out.n() == 2);
    CHECK(wide.out.events[0].kind == 'i');
  }

  // ─── what a save carries ────────────────────────────────────────────────────

  TEST_CASE("bline: the creation argument survives a DumpJSON / ParseJSON round trip (#511)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_BLINE, "5") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".bline") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".bline");
    CHECK(copy->GetParams() == "5");
  }

  TEST_CASE("bline: a float creation argument survives the round trip as a float (#511)") {
    // The spelling is the object's type, so a save that normalised "0." to "0"
    // would load back a different object.
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_BLINE, "0.") != nullptr);
    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(src.DumpJSON());
    REQUIRE(loaded.Objects() == 1);
    CHECK(loaded.GetHandleFromList(0)->GetParams() == "0.");
  }

  // ─── the bounded list ───────────────────────────────────────────────────────

  TEST_CASE("bline: a list longer than the object holds keeps its head and counts it (#511)") {
    // 129 numbers is Max's maxpoints default and 64 segments is what this
    // object holds; anything longer is truncated rather than allocated for,
    // the arriving thread being routinely the audio callback.
    Rig rig;
    std::string huge;
    huge.reserve(8192);
    for (int i = 0; i < gBline::MAX_POINTS + 40; i++) {
      if (i > 0) huge.push_back(' ');
      huge += std::to_string(i + 1);
    }
    rig.List(huge);
    CHECK(rig.obj.Dropped() >= 1u);
    // The head of the list is queued and, this object being bang-driven, still
    // waiting: the first segment is under way and the rest are behind it.
    CHECK(rig.obj.IsRunning());
    CHECK(rig.obj.Pending() == gBline::CAPACITY - 1);
    CHECK(rig.out.n() == 0);
  }

  // ─── the audio-thread path, end to end ──────────────────────────────────────

  TEST_CASE("bline: a real chain ramps from bangs arriving inside Calculate (#511)") {
    // The integration case, and the one that matches what the object is for: a
    // `.pipe` releases a count from the scheduler at the top of `Calculate` —
    // on the audio callback — which runs a `.uzi`, whose bangs drive the ramp
    // to its target. Every value therefore comes out on the audio thread inside
    // the patcher's own dispatch, which is precisely the code that must not
    // allocate or lock, and the arrival bang goes on through a `.delay` so the
    // "ramp, then do the next thing" idiom runs end to end.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* pipe = p.CreateObject(YSE::OBJ::G_PIPE, "5");
    YSE::pHandle* uzi = p.CreateObject(YSE::OBJ::G_UZI, "1");
    YSE::pHandle* bline = p.CreateObject(YSE::OBJ::G_BLINE, "0");
    YSE::pHandle* after = p.CreateObject(YSE::OBJ::G_DELAY, "5");
    REQUIRE(pipe != nullptr);
    REQUIRE(uzi != nullptr);
    REQUIRE(bline != nullptr);
    REQUIRE(after != nullptr);

    BlineRecorder out;
    BlineRecorder chained;
    YSE::pHandle outHandle(&out);
    YSE::pHandle chainedHandle(&chained);
    p.Connect(pipe, 0, uzi, 0);
    p.Connect(uzi, 0, bline, 0);
    p.Connect(bline, 0, &outHandle, 0);
    p.Connect(bline, 1, after, 0);
    p.Connect(after, 0, &chainedHandle, 0);

    // The ramp first, then the burst of bangs that will travel it: eight bangs
    // arriving a few blocks later, from inside the audio callback.
    bline->SetListData(0, "64 8");
    pipe->SetIntData(0, 8);
    CHECK(out.n() == 0);

    const std::uint64_t budget = messageScheduler::BlocksForMillis(5) +
                                 messageScheduler::BlocksForMillis(5) +
                                 messageScheduler::BlocksForMillis(20);
    for (std::uint64_t i = 0; i < budget; i++)
      p.Calculate(YSE::T_DSP);

    // Eight bangs, eight values, the last of them the target exactly.
    REQUIRE(out.n() == 8);
    CHECK(out.number(0) == doctest::Approx(8.0));
    CHECK(out.last() == doctest::Approx(64.0));
    // The arrival bang went through the `.delay` and came out the far side, so
    // the causal chain survived both deferrals.
    CHECK(chained.n() == 1);
    CHECK(chained.events[0].kind == 'b');
    CHECK(p.Scheduler()->PendingCount() == 0);
  }

  // ─── real-time discipline ───────────────────────────────────────────────────

  TEST_CASE("bline: the message paths allocate nothing (#511)") {
    // Everything a message can do on the way in, measured on the thread that
    // does it — the bang included, since that is the path a patch takes on
    // every step of every ramp. A breakpoint list is read into a stack array by
    // ExprParseFloatList and copied into segments the object already owns, so a
    // 64-segment ramp costs no allocation however it arrives.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    patcherImplementation p(1, nullptr);
    YSE::pHandle* bline = p.CreateObject(YSE::OBJ::G_BLINE, "0");
    REQUIRE(bline != nullptr);

    BlineRecorder out;
    BlineRecorder done;
    YSE::pHandle outHandle(&out);
    YSE::pHandle doneHandle(&done);
    p.Connect(bline, 0, &outHandle, 0);
    p.Connect(bline, 1, &doneHandle, 0);

    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message.
    const std::string set = "set 5";
    const std::string stop = "stop";
    const std::string withTrailing = "1 8 999";
    const std::string breakpoints = "10 8 20 8 30 8";
    {
      TestHelpers::ProbeScope probe;
      bline->SetListData(0, breakpoints);
      for (int i = 0; i < 12; i++)
        bline->SetBang(0);
      bline->SetListData(0, withTrailing);
      bline->SetBang(0);
      bline->SetListData(0, set);
      bline->SetListData(0, stop);
      bline->SetIntData(0, 100);
      bline->SetBang(0);
      bline->SetFloatData(0, 7.f);
      bline->SetBang(0);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    // The probed messages really did something — an assertion that only proves
    // nothing happened proves nothing. Fifteen bangs came out as values, and
    // the last two of them were arrivals.
    CHECK(out.n() == 15u);
    CHECK(out.last() == doctest::Approx(7.0));
    CHECK(done.n() == 2u);
    CHECK(p.Scheduler()->PendingCount() == 0);
  }
}
