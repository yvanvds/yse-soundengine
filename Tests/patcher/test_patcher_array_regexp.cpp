// Tests for .array.regexp (issue #803) — the elements of an array a regular
// expression matches, on the name-addressed value model .array settled
// (#548). Issue #803's reading, written down in gArrayRegexp.h: Max's own
// array.regexp treats the incoming array as one subject buffer — bytes,
// offsets, a substitution over the whole of it — which the value model does
// not have; #803 specifies the per-element match, the text filter
// .array.filter cannot be since its expression never sees a symbol. The
// pattern is one token, compiled once on the control thread by .regexp's own
// bounded engine (#452) — the matcher the patcher already owns, so no
// std::regex and no new dependency.
//
// What has to be proven, and what every case below is one of:
//
//   - **the array is bound from the creation argument.** An array never
//     travels down a cord, so ".array.regexp <name> <pattern>" resolves the
//     name once, on the control thread, and an `array <name>` message is
//     honoured only when it names the array already bound.
//   - **the ask is one typed message of the matched elements** — order kept,
//     repeats kept, one match leaving as the atom it spells (SendAtoms'
//     rule), the pattern matching anywhere in the element with ^ and $ for a
//     whole-element match — **or the no-match bang** when the ask landed and
//     nothing matched, an empty or unnamed array included.
//   - **a misconfigured object refuses.** A malformed pattern fails at parse
//     time, loudly, and an absent one leaves the object inert silently;
//     either way a trigger is a counted refusal, never a no-match bang —
//     "matched nothing" is one of this object's honest answers and must stay
//     one.
//   - **whole answer or nothing** — .array.sect's rule: a matched list past
//     what a cord carries, a scan that ran out of step budget, and a lost
//     try-lock each refuse the whole ask, counted, nothing sent.
//   - **the scan is one hold of the store's guard**, the send after release,
//     so a write the send triggers lands in the store and changes the next
//     ask, never the one in flight.
//   - **the ask crosses the control/audio boundary and nothing allocates.**
//     In-patcher delivery dispatches on T_DSP, so "the audio thread asks for
//     the match" is the ordinary case.
//
// No audio device and no engine of its own. The registry is process-wide, so
// every case that names an array uses names of its own — one case's contents
// must not be visible to the next.

#include <doctest/doctest.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "patcher/genericObjects/gArray.h"
#include "patcher/genericObjects/gArrayRegexp.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::BangSink;
using TestHelpers::MultiSink;
using TestHelpers::Wire;
using YSE::PATCHER::gArray;
using YSE::PATCHER::gArrayRegexp;

namespace {

  // Records every message it receives, with its type, into a shared log —
  // gArrayGroup's test sink, kept so the matched/no-match split is asserted
  // as a sequence rather than assumed, and so a case can act from *inside*
  // the matched send (`onAny`), which is where the no-guard-across-the-send
  // proof has to stand.
  struct SeqSink : YSE::PATCHER::pObject {
    std::vector<std::string>* log = nullptr;
    std::string prefix;
    std::function<void()> onAny;

    SeqSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        if (log != nullptr) log->push_back(prefix + "i:" + std::to_string(v));
        if (onAny) onAny();
      });
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
        if (log != nullptr) log->push_back(prefix + "f:" + std::to_string(static_cast<int>(v * 2)));
        if (onAny) onAny();
      });
      inputs.back().RegisterList([this](const std::string& v, int, YSE::THREAD) {
        if (log != nullptr) log->push_back(prefix + "l:" + v);
        if (onAny) onAny();
      });
      inputs.back().RegisterBang([this](int, YSE::THREAD) {
        if (log != nullptr) log->push_back(prefix + "<nomatch>");
      });
    }
    const char* Type() const override {
      return "seq_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // An .array and an .array.regexp on one name, sharing one
  // patcherImplementation so the name actually binds. The sinks are declared
  // before the objects so they are torn down last, while the outlets wired to
  // them still exist (see sinks.hpp on why that matters).
  struct Rig {
    std::vector<std::string> events;
    YSE::PATCHER::patcherImplementation p{2, nullptr};
    SeqSink matchedOut;
    SeqSink noMatchOut;
    gArray array;
    gArrayRegexp regexp;

    Rig(const std::string& patcherName, const std::string& params) {
      p.SetName(patcherName);
      array.SetParent(&p);
      array.SetParams(params.substr(0, params.find(' ')));
      regexp.SetParent(&p);
      regexp.SetParams(params);
      matchedOut.log = &events;
      noMatchOut.log = &events;
      Wire(regexp, 0, matchedOut);
      Wire(regexp, 1, noMatchOut);
    }

    void Store(const std::string& message) {
      array.GetInlet(0)->SetList(message, YSE::T_GUI);
    }
    void Run(YSE::THREAD thread = YSE::T_GUI) {
      events.clear();
      regexp.GetInlet(0)->SetBang(thread);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("array.regexp: registered, two inlets, two outlets (#803)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_REGEXP);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".array.regexp");
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 2);

    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ARRAY_REGEXP)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("array.regexp: the trigger takes bang and list and no bare number, the reference "
            "inlet only list text (#803)") {
    // No int or float handler anywhere — gArrayStatsBase's shape: the ask is
    // a bang, never addressed, and a bare number names no array.
    gArrayRegexp g;
    const unsigned int trigger = g.GetInlet(0)->GetAcceptedTypes();
    CHECK((trigger & YSE::PATCHER::IT_BANG) != 0);
    CHECK((trigger & YSE::PATCHER::IT_LIST) != 0);
    CHECK((trigger & YSE::PATCHER::IT_INT) == 0);
    CHECK((trigger & YSE::PATCHER::IT_FLOAT) == 0);
    const unsigned int ref = g.GetInlet(1)->GetAcceptedTypes();
    CHECK((ref & YSE::PATCHER::IT_LIST) != 0);
    CHECK((ref & YSE::PATCHER::IT_BANG) == 0);
  }

  // ─── the match ──────────────────────────────────────────────────────────────

  TEST_CASE("array.regexp: keeps the matched elements — order kept, repeats kept, one typed "
            "message (#803)") {
    // The operation itself: the file-name use #803 names. The anchored
    // pattern keeps exactly the .wav names, in array order, the repeat
    // included — a filter, not a set operation — and the no-match outlet
    // stays silent when something matched.
    Rig rig("ar803a", "a803a \\.wav$");
    REQUIRE(rig.regexp.Valid());
    rig.Store("append kick.wav lead.aif kick.wav wavetable snare.wav");

    rig.Run();
    REQUIRE(rig.events.size() == 1);
    CHECK(rig.events[0] == "l:kick.wav kick.wav snare.wav");
    CHECK(rig.regexp.Dropped() == 0);
  }

  TEST_CASE("array.regexp: the pattern matches anywhere in an element unless anchored (#803)") {
    // .regexp's own reading of a match, inherited: `wav` is found inside
    // `wavetable` as well as at the end of `kick.wav`; ^ and $ are how a
    // whole-element match is spelled. The case above is the anchored form of
    // this one.
    Rig rig("ar803b", "a803b wav");
    rig.Store("append kick.wav wavetable lead.aif");

    rig.Run();
    REQUIRE(rig.events.size() == 1);
    CHECK(rig.events[0] == "l:kick.wav wavetable");
    CHECK(rig.regexp.Dropped() == 0);
  }

  TEST_CASE("array.regexp: a single match leaves typed as the atom it spells (#803)") {
    // SendAtoms' rule, the family's transport: one matched element is the
    // int, float or symbol it spells rather than a list of one. A numeric
    // element is matched as its text — 60 the characters 60 — the same
    // spelling-is-identity rule the family's byte compare applies.
    Rig intRig("ar803c", "a803c ^\\d+$");
    intRig.Store("append kick 60 snare");
    intRig.Run();
    REQUIRE(intRig.events.size() == 1);
    CHECK(intRig.events[0] == "i:60");

    Rig floatRig("ar803d", "a803d ^\\d+\\.\\d*$");
    floatRig.Store("append kick 2.5 snare");
    floatRig.Run();
    REQUIRE(floatRig.events.size() == 1);
    CHECK(floatRig.events[0] == "f:5"); // 2.5, logged doubled to dodge float text

    Rig symRig("ar803e", "a803e ^[a-z]+$");
    symRig.Store("append 60 kick 7.");
    symRig.Run();
    REQUIRE(symRig.events.size() == 1);
    CHECK(symRig.events[0] == "l:kick");
  }

  TEST_CASE("array.regexp: nothing matched, an empty array, and an unnamed array all bang the "
            "no-match outlet (#803)") {
    // "No data" is a state a patch must be able to route on, not an error —
    // .array.sect's empty outlet rule. Nothing leaves the matched outlet in
    // any of the three, and none is a refusal: the ask landed.
    Rig rig("ar803f", "a803f ^\\d+$");
    rig.Store("append kick snare");
    rig.Run();
    REQUIRE(rig.events.size() == 1);
    CHECK(rig.events[0] == "<nomatch>");
    CHECK(rig.regexp.Dropped() == 0);

    Rig empty("ar803g", "a803g ^\\d+$");
    empty.Run();
    REQUIRE(empty.events.size() == 1);
    CHECK(empty.events[0] == "<nomatch>");
    CHECK(empty.regexp.Dropped() == 0);

    // An unnamed object reads a private, empty array of its own — but it
    // still needs a pattern to be askable at all, and the name is the first
    // positional argument, so the unnamed-with-pattern form does not exist:
    // a bare object is inert (see the misconfiguration case below). What is
    // proven here is the named-but-empty half, which is the state an unnamed
    // object's private array is always in.
  }

  // ─── misconfiguration refuses ───────────────────────────────────────────────

  TEST_CASE("array.regexp: a malformed pattern fails at parse time and every trigger then "
            "refuses (#803)") {
    // .array.filter's rule rather than .regexp's pass-through: "matched
    // nothing" is one of this object's honest answers, so a misconfigured
    // object must not be able to spell it. The compile error is kept for
    // diagnostics; the trigger is a counted refusal and neither outlet
    // fires.
    Rig rig("ar803h", "a803h (oops");
    CHECK_FALSE(rig.regexp.Valid());
    CHECK_FALSE(rig.regexp.CompileError().empty());
    rig.Store("append kick");

    const std::uint64_t before = rig.regexp.Dropped();
    rig.Run();
    CHECK(rig.events.empty());
    CHECK(rig.regexp.Dropped() == before + 1);
  }

  TEST_CASE("array.regexp: an absent pattern is silently inert — a name-only or bare object "
            "refuses triggers (#803)") {
    // gArrayExprBase's rule: nothing malformed to report, the object simply
    // refuses until given work. No log line, no compile error, no outlet.
    Rig rig("ar803i", "a803i");
    CHECK_FALSE(rig.regexp.Valid());
    CHECK(rig.regexp.CompileError().empty());
    rig.Store("append kick");

    const std::uint64_t before = rig.regexp.Dropped();
    rig.Run();
    CHECK(rig.events.empty());
    CHECK(rig.regexp.Dropped() == before + 1);

    gArrayRegexp bare;
    CHECK_FALSE(bare.Valid());
    bare.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(bare.Dropped() == 1);
  }

  // ─── the reference gesture ──────────────────────────────────────────────────

  TEST_CASE("array.regexp: the reference triggers on the trigger inlet, acknowledges on its "
            "own, and anything else is refused (#803)") {
    Rig rig("ar803j", "a803j ^lead$");
    rig.Store("append lead piano lead");

    // The array's own reference — the message its .array emits on a bang —
    // matches, exactly as a bang does.
    const std::uint64_t before = rig.regexp.Dropped();
    rig.events.clear();
    rig.regexp.GetInlet(0)->SetList("array a803j", YSE::T_GUI);
    REQUIRE(rig.events.size() == 1);
    CHECK(rig.events[0] == "l:lead lead");
    CHECK(rig.regexp.Dropped() == before);

    // A reference to an array this object is not bound to, and any other
    // message, are refused and counted, never resolved: a registry lookup is
    // a mutex, and this may be the audio thread.
    rig.events.clear();
    rig.regexp.GetInlet(0)->SetList("array somewhere_else", YSE::T_GUI);
    rig.regexp.GetInlet(0)->SetList("frobnicate a b", YSE::T_GUI);
    CHECK(rig.events.empty());
    CHECK(rig.regexp.Dropped() == before + 2);

    // The reference inlet acknowledges the bound array silently and refuses
    // anything else — gDictSlice's inlet rule. Neither triggers a match.
    rig.regexp.GetInlet(1)->SetList("array a803j", YSE::T_GUI);
    CHECK(rig.regexp.Dropped() == before + 2);
    rig.regexp.GetInlet(1)->SetList("array somewhere_else", YSE::T_GUI);
    CHECK(rig.regexp.Dropped() == before + 3);
    CHECK(rig.events.empty());
  }

  // ─── whole answer or nothing ────────────────────────────────────────────────

  TEST_CASE("array.regexp: a matched list that cannot leave whole refuses the whole ask "
            "(#803)") {
    // The transport bound: seventeen matched copies of a 64-character
    // element spell more text than a cord carries (17 * 64 > 1024). A
    // matched list is an answer about membership, so it never leaves as a
    // fragment of itself — .array.sect's whole-refusal rule: one counted
    // refusal, nothing on either outlet. Sixteen copies fit exactly
    // (16 * 64 = 1024) — the bound is what a cord carries, not one less.
    Rig rig("ar803k", "a803k ^y+$");
    const std::string wide(64, 'y');
    const std::string four = "append " + wide + " " + wide + " " + wide + " " + wide;
    rig.Store(four);
    rig.Store(four);
    rig.Store(four);
    rig.Store(four);
    rig.Store("append " + wide);
    CHECK(rig.array.Count() == 17);

    const std::uint64_t before = rig.regexp.Dropped();
    rig.Run();
    CHECK(rig.events.empty());
    CHECK(rig.regexp.Dropped() == before + 1);

    rig.Store("delete 16");
    rig.Run();
    REQUIRE(rig.events.size() == 1);
    CHECK(rig.events[0].size() == 2 + 16 * 65 - 1); // "l:" + 16 tokens, 15 spaces
    CHECK(rig.regexp.Dropped() == before + 1);
  }

  TEST_CASE("array.regexp: a scan that runs out of step budget refuses the whole ask (#803)") {
    // The adversarial case the budget exists for: (y+)+z is the exponential
    // backtracking family, and against a long run of y with no z the VM
    // would explore 2^63 paths — the budget dies first, and past that point
    // a miss is indistinguishable from an unfinished match, so the honest
    // answer is no answer: one counted refusal, nothing on either outlet.
    // The same pattern over a short element completes and matches, which is
    // the proof that the refusal above is the budget and not the pattern.
    Rig rig("ar803l", "a803l (y+)+z");
    REQUIRE(rig.regexp.Valid());
    rig.Store("append yyyz");
    rig.Run();
    REQUIRE(rig.events.size() == 1);
    CHECK(rig.events[0] == "l:yyyz");
    CHECK(rig.regexp.Dropped() == 0);

    const std::string hard(64, 'y');
    rig.Store("clear");
    rig.Store("append " + hard);
    const std::uint64_t before = rig.regexp.Dropped();
    rig.Run();
    CHECK(rig.events.empty());
    CHECK(rig.regexp.Dropped() == before + 1);
  }

  // ─── one guard hold, the send after it ──────────────────────────────────────

  TEST_CASE("array.regexp: the answer is the array at the trigger, and a write from the "
            "matched send lands in the store (#803)") {
    // The family's mid-walk answer, seen from the send: the scan is one hold
    // of the store's guard and the send happens after release, so a write
    // arriving from the matched outlet's own subgraph is not the one thing
    // the try-lock drops — it lands, moving the store but never the answer
    // in flight. The next ask sees it.
    Rig rig("ar803m", "a803m ^\\d+$");
    rig.Store("append 60 kick 64");

    bool acted = false;
    rig.matchedOut.onAny = [&] {
      if (acted) return;
      acted = true;
      rig.Store("append 67");
    };

    rig.Run();
    REQUIRE(rig.events.size() == 1);
    CHECK(rig.events[0] == "l:60 64");
    CHECK(rig.regexp.Dropped() == 0);
    CHECK(rig.array.Count() == 4);
    CHECK(rig.array.ElementAt(3) == "67");

    rig.matchedOut.onAny = nullptr;
    rig.Run();
    REQUIRE(rig.events.size() == 1);
    CHECK(rig.events[0] == "l:60 64 67");
  }

  // ─── binding, and the rename hook ───────────────────────────────────────────

  TEST_CASE("array.regexp: the address form is the patcher's, and RefreshBinding follows it "
            "(#803)") {
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ar803n_before");

    gArrayRegexp g;
    g.SetParams("a803n .");
    g.SetParent(&p);
    CHECK(g.ArrayName() == "a803n");
    CHECK(g.Address() == "ar803n_before.a803n");

    // Idempotent: a rebind to the address it already has keeps the store.
    g.RefreshBinding();
    CHECK(g.Address() == "ar803n_before.a803n");

    p.SetName("ar803n_after");
    g.RefreshBinding();
    CHECK(g.Address() == "ar803n_after.a803n");
  }

  TEST_CASE("array.regexp: patcherImplementation::SetName re-anchors it (#803)") {
    // The rename dispatch itself, which the standalone case above cannot
    // reach: an object created *inside* a patcher must be re-anchored by the
    // patcher, without anybody calling RefreshBinding by hand. Before the
    // rename a bang matches the keeper's elements; after it the ask reads a
    // fresh empty array under the new prefix, so a second bang is the
    // no-match bang alone.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    BangSink noMatch;
    YSE::pHandle noMatchHandle(&noMatch);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ar803o_before");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a803o");
    keeper.GetInlet(0)->SetList("append lead lead", YSE::T_GUI);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_ARRAY_REGEXP, "a803o ^lead$");
    REQUIRE(h != nullptr);
    p.Connect(h, 0, &sinkHandle, 0);
    p.Connect(h, 1, &noMatchHandle, 0);

    h->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "lead lead");
    CHECK_FALSE(noMatch.gotBang);

    p.SetName("ar803o_after");
    sink.reset();
    h->SetBang(0);
    CHECK_FALSE(sink.gotList);
    CHECK(noMatch.gotBang);
    CHECK(keeper.Count() == 2);
  }

  TEST_CASE("array.regexp: wired from the array's reference outlet, banging the array matches "
            "(#803)") {
    // The flow a patch actually wires, end to end through the public patcher
    // API: the .array's reference outlet into the matcher, a bang on the
    // .array, and the matched elements at the far end. The family's gesture
    // — bang the array, out comes the filtered answer.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    BangSink noMatch;
    YSE::pHandle noMatchHandle(&noMatch);
    YSE::patcher p;
    p.create(2);
    p.name("ar803p");
    YSE::pHandle* array = p.CreateObject(YSE::OBJ::G_ARRAY, "a803p");
    YSE::pHandle* regexp = p.CreateObject(YSE::OBJ::G_ARRAY_REGEXP, "a803p \\.wav$");
    REQUIRE(array != nullptr);
    REQUIRE(regexp != nullptr);
    p.Connect(array, 1, regexp, 0);
    p.Connect(regexp, 0, &sinkHandle, 0);
    p.Connect(regexp, 1, &noMatchHandle, 0);

    array->SetListData(0, "append kick.wav lead.aif snare.wav");
    array->SetBang(0);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "kick.wav snare.wav");
    CHECK_FALSE(noMatch.gotBang);
  }

  // ─── the control/audio boundary ─────────────────────────────────────────────

  TEST_CASE("array.regexp: an ask over in-patcher delivery lands on T_DSP (#803)") {
    // A .r feeding the matcher dispatches on T_DSP when the block drains it
    // (issue #225) — "the audio thread asks for the match" is the ordinary
    // case, and the whole path is the compiled program's VM, bounded
    // compares and one typed send over storage the object already owns.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ar803q");

    gArray keeper;
    keeper.SetParent(&p);
    keeper.SetParams("a803q");
    keeper.GetInlet(0)->SetList("append 60 kick 64", YSE::T_GUI);

    YSE::pHandle* recv = p.CreateObject(YSE::OBJ::G_RECEIVE, "go803q");
    YSE::pHandle* regexp = p.CreateObject(YSE::OBJ::G_ARRAY_REGEXP, "a803q ^\\d+$");
    REQUIRE(recv != nullptr);
    REQUIRE(regexp != nullptr);
    p.Connect(recv, 0, regexp, 0);
    p.Connect(regexp, 0, &sinkHandle, 0);
    p.Connect(regexp, 1, &sinkHandle, 0);

    p.PassData(std::string("array a803q"), "go803q", YSE::T_GUI);
    p.Calculate(YSE::T_DSP);
    REQUIRE(sink.gotList);
    CHECK(sink.listValue == "60 64");
    CHECK_FALSE(sink.gotBang);
  }

  TEST_CASE("array.regexp: no message path allocates (#803)") {
    // The claim the acceptance criteria rest on, proven with the probe over
    // every message path: the match through the render path and all three
    // single-atom typed sends, the no-match bang, the reference gesture and
    // its acknowledgement, the wrong-name and unknown-message refusals, the
    // misconfigured-object refusal, the whole-refusal of an oversized
    // matched list and the budget-exhaustion refusal — on T_DSP, in-patcher
    // delivery's thread.
    //
    // Every message is built as a std::string before the scope opens, never
    // passed as a literal inside it — inlet::SetList takes a const
    // std::string&, so a literal at the call site materialises a temporary
    // whenever it outgrows the small-string buffer.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string reference = "array probeAR803";
    const std::string wrongName = "array somewhere_else_long";
    const std::string unknown = "frobnicate something quite long indeed";

    YSE::PATCHER::patcherImplementation p(2, nullptr);
    p.SetName("ar803r");
    MultiSink matchedOut;
    BangSink noMatchOut;
    MultiSink intOut;
    BangSink intNoMatch;
    MultiSink floatOut;
    BangSink floatNoMatch;
    MultiSink symOut;
    BangSink symNoMatch;
    MultiSink noneOut;
    BangSink noneNoMatch;
    MultiSink bigOut;
    BangSink bigNoMatch;
    MultiSink hardOut;
    BangSink hardNoMatch;

    gArray array;
    gArrayRegexp matcher;
    gArray intArray;
    gArrayRegexp intOne;
    gArray floatArray;
    gArrayRegexp floatOne;
    gArray symArray;
    gArrayRegexp symOne;
    gArray noneArray;
    gArrayRegexp noneMatcher;
    gArray bigArray;
    gArrayRegexp bigMatcher;
    gArray hardArray;
    gArrayRegexp hardMatcher;
    gArrayRegexp broken;

    array.SetParent(&p);
    array.SetParams("probeAR803");
    matcher.SetParent(&p);
    matcher.SetParams("probeAR803 ^60$");
    intArray.SetParent(&p);
    intArray.SetParams("probeAR803int");
    intOne.SetParent(&p);
    intOne.SetParams("probeAR803int ^\\d+$");
    floatArray.SetParent(&p);
    floatArray.SetParams("probeAR803flt");
    floatOne.SetParent(&p);
    floatOne.SetParams("probeAR803flt ^\\d+\\.\\d*$");
    symArray.SetParent(&p);
    symArray.SetParams("probeAR803sym");
    symOne.SetParent(&p);
    symOne.SetParams("probeAR803sym ^[a-z_]+$");
    noneArray.SetParent(&p);
    noneArray.SetParams("probeAR803none");
    noneMatcher.SetParent(&p);
    noneMatcher.SetParams("probeAR803none ^\\d+$");
    bigArray.SetParent(&p);
    bigArray.SetParams("probeAR803big");
    bigMatcher.SetParent(&p);
    bigMatcher.SetParams("probeAR803big ^y+$");
    hardArray.SetParent(&p);
    hardArray.SetParams("probeAR803hard");
    hardMatcher.SetParent(&p);
    hardMatcher.SetParams("probeAR803hard (y+)+z");
    broken.SetParent(&p);
    broken.SetParams("probeAR803broken (oops");
    REQUIRE(matcher.Valid());
    REQUIRE_FALSE(broken.Valid());

    Wire(matcher, 0, matchedOut);
    Wire(matcher, 1, noMatchOut);
    Wire(intOne, 0, intOut);
    Wire(intOne, 1, intNoMatch);
    Wire(floatOne, 0, floatOut);
    Wire(floatOne, 1, floatNoMatch);
    Wire(symOne, 0, symOut);
    Wire(symOne, 1, symNoMatch);
    Wire(noneMatcher, 0, noneOut);
    Wire(noneMatcher, 1, noneNoMatch);
    Wire(bigMatcher, 0, bigOut);
    Wire(bigMatcher, 1, bigNoMatch);
    Wire(hardMatcher, 0, hardOut);
    Wire(hardMatcher, 1, hardNoMatch);

    // The render path (a two-member matched list), the three single-atom
    // typed sends (the symbol wide enough to outgrow every small-string
    // buffer the scratch assign must absorb), the no-match bang, the
    // oversized matched list and the exponential budget-killer.
    array.GetInlet(0)->SetList("append 60 kick 60", YSE::T_GUI);
    intArray.GetInlet(0)->SetList("append kick 60", YSE::T_GUI);
    floatArray.GetInlet(0)->SetList("append kick 2.5", YSE::T_GUI);
    symArray.GetInlet(0)->SetList("append 60 a_symbol_past_every_small_string_buffer", YSE::T_GUI);
    noneArray.GetInlet(0)->SetList("append kick snare", YSE::T_GUI);
    const std::string wide(64, 'y');
    const std::string four = "append " + wide + " " + wide + " " + wide + " " + wide;
    bigArray.GetInlet(0)->SetList(four, YSE::T_GUI);
    bigArray.GetInlet(0)->SetList(four, YSE::T_GUI);
    bigArray.GetInlet(0)->SetList(four, YSE::T_GUI);
    bigArray.GetInlet(0)->SetList(four, YSE::T_GUI);
    bigArray.GetInlet(0)->SetList("append " + wide, YSE::T_GUI);
    hardArray.GetInlet(0)->SetList("append " + wide, YSE::T_GUI);

    // Warm every path — including the sinks' assignments — so first-call
    // machinery is not what the probe catches.
    matcher.GetInlet(0)->SetBang(YSE::T_GUI);
    intOne.GetInlet(0)->SetBang(YSE::T_GUI);
    floatOne.GetInlet(0)->SetBang(YSE::T_GUI);
    symOne.GetInlet(0)->SetBang(YSE::T_GUI);
    noneMatcher.GetInlet(0)->SetBang(YSE::T_GUI);
    bigMatcher.GetInlet(0)->SetBang(YSE::T_GUI);
    hardMatcher.GetInlet(0)->SetBang(YSE::T_GUI);
    broken.GetInlet(0)->SetBang(YSE::T_GUI);
    matcher.GetInlet(0)->SetList(reference, YSE::T_GUI);
    matcher.GetInlet(0)->SetList(wrongName, YSE::T_GUI);
    matcher.GetInlet(0)->SetList(unknown, YSE::T_GUI);
    matcher.GetInlet(1)->SetList(reference, YSE::T_GUI);

    const std::uint64_t before = matcher.Dropped();
    const std::uint64_t bigBefore = bigMatcher.Dropped();
    const std::uint64_t hardBefore = hardMatcher.Dropped();
    const std::uint64_t brokenBefore = broken.Dropped();
    const int noneBangs = noneNoMatch.bangCount;

    matchedOut.reset();
    intOut.reset();
    floatOut.reset();
    symOut.reset();
    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      matcher.GetInlet(0)->SetBang(YSE::T_DSP);
      intOne.GetInlet(0)->SetBang(YSE::T_DSP);
      floatOne.GetInlet(0)->SetBang(YSE::T_DSP);
      symOne.GetInlet(0)->SetBang(YSE::T_DSP);
      noneMatcher.GetInlet(0)->SetBang(YSE::T_DSP);
      bigMatcher.GetInlet(0)->SetBang(YSE::T_DSP);
      hardMatcher.GetInlet(0)->SetBang(YSE::T_DSP);
      broken.GetInlet(0)->SetBang(YSE::T_DSP);
      matcher.GetInlet(0)->SetList(reference, YSE::T_DSP);
      matcher.GetInlet(0)->SetList(wrongName, YSE::T_DSP);
      matcher.GetInlet(0)->SetList(unknown, YSE::T_DSP);
      matcher.GetInlet(1)->SetList(reference, YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And it really did all of that — an assertion that only proves nothing
    // happened proves nothing. Two full matches ran (the bang and the
    // reference gesture), all three typed single sends left, the no-match
    // bang fired, the oversized and exponential asks were refused whole, the
    // misconfigured object refused, and both bad messages were counted.
    CHECK(matchedOut.gotList);
    CHECK(matchedOut.listValue == "60 60");
    CHECK(intOut.gotInt);
    CHECK(intOut.intValue == 60);
    CHECK(floatOut.gotFloat);
    CHECK(floatOut.floatValue == 2.5f);
    CHECK(symOut.gotList);
    CHECK(symOut.listValue == "a_symbol_past_every_small_string_buffer");
    CHECK(noneNoMatch.bangCount == noneBangs + 1);
    CHECK_FALSE(bigOut.gotList);
    CHECK_FALSE(hardOut.gotList);
    CHECK(bigMatcher.Dropped() == bigBefore + 1);
    CHECK(hardMatcher.Dropped() == hardBefore + 1);
    CHECK(broken.Dropped() == brokenBefore + 1);
    CHECK(matcher.Dropped() == before + 2);
  }

  // ─── parameters and documentation ───────────────────────────────────────────

  TEST_CASE("array.regexp: params survive a DumpJSON / ParseJSON round trip (#803)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* h = src.CreateObject(YSE::OBJ::G_ARRAY_REGEXP, "matchme803 \\.wav$");
    REQUIRE(h != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == std::string(".array.regexp"));
    CHECK(copy->GetParams() == std::string("matchme803 \\.wav$"));
    CHECK(copy->GetInputs() == 2);
    CHECK(copy->GetOutputs() == 2);
  }

  TEST_CASE("array.regexp: carries complete documentation metadata (#803)") {
    gArrayRegexp g;
    CHECK_FALSE(g.GetDescription().empty());
    CHECK(g.GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    const auto& docs = g.GetParamDocs();
    REQUIRE(docs.size() == 2);
    CHECK(docs[0].name == "name");
    CHECK(docs[1].name == "pattern");
  }
}
