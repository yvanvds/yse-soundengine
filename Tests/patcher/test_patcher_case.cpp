// Tests for .tolower and .toupper (issue #810) — ASCII case folding on the text
// of a message.
//
// They share one body (gCaseBase) and differ only in direction, so most cases
// below are written as mirror pairs. What the pair has to get right:
//
//   - **the fold itself, and nothing else.** Apart from the 52 ASCII letters
//     the message must leave exactly as it arrived: the same bytes, the same
//     length, the same tokens, the same whitespace. That is a deliberate
//     departure from .tosymbol and .spell, which normalise whitespace because
//     they rebuild the message; folding in place must not, or the very .route
//     downstream that the fold exists to feed sees different tokens than the
//     ones that were sent.
//   - **it is safe on text that is not ASCII.** A patcher message is bytes and
//     nothing upstream promises an encoding. Every byte of a multi-byte UTF-8
//     sequence is 0x80 or above and the folded range is entirely below it, so a
//     byte-wise fold provably cannot corrupt one — and the test asserts that
//     byte for byte rather than trusting the argument, since it also has to
//     hold on a platform where `char` is unsigned.
//   - **it is transparent to everything else.** A bang, an int and a float pass
//     through in their own type, and an empty message is forwarded rather than
//     swallowed. An object that is the identity by definition must not be the
//     one in a chain that drops or retypes a message.
//   - **the use case is case-insensitive matching**, and only a real graph
//     proves it: the end-to-end cases at the bottom put a fold in front of a
//     .route and assert that the same text now matches where it did not before.
//
// No audio device and no engine of its own, except where a real patcher graph
// is the point.

#include <doctest/doctest.h>
#include <string>

#include "patcher/genericObjects/gCase.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::gCaseBase;
using YSE::PATCHER::gToLower;
using YSE::PATCHER::gToUpper;

namespace {

  // A standalone object with a sink on its outlet. Standalone on purpose: these
  // two need no patcher at all, and a test that needed one could not tell a
  // dropped message from a message the patcher never delivered.
  struct Rig {
    MultiSink sink;

    void Wire(gCaseBase& obj) {
      TestHelpers::Wire(obj, 0, sink);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("case: both objects are registered, with one inlet and one outlet (#810)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* lower = p.CreateObject(YSE::OBJ::G_TOLOWER);
    REQUIRE(lower != nullptr);
    CHECK(std::string(lower->Type()) == ".tolower");
    CHECK(lower->GetInputs() == 1);
    CHECK(lower->GetOutputs() == 1);

    YSE::pHandle* upper = p.CreateObject(YSE::OBJ::G_TOUPPER);
    REQUIRE(upper != nullptr);
    CHECK(std::string(upper->Type()) == ".toupper");
    CHECK(upper->GetInputs() == 1);
    CHECK(upper->GetOutputs() == 1);
  }

  TEST_CASE("case: both appear in the registry's name list (#810)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool foundLower = false;
    bool foundUpper = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_TOLOWER)) foundLower = true;
      if (name == std::string(YSE::OBJ::G_TOUPPER)) foundUpper = true;
    }
    CHECK(foundLower);
    CHECK(foundUpper);
  }

  TEST_CASE("case: the one inlet takes every kind of message (#810)") {
    // Including a bang, unlike .spell's data inlet: .spell has nothing to spell
    // of one, while the fold has a perfectly good answer for it, and a
    // case-folder that broke a patch by dropping its bangs could not be dropped
    // into a working patch at all.
    gToLower lower;
    const unsigned int in = lower.GetInlet(0)->GetAcceptedTypes();
    CHECK((in & YSE::PATCHER::IT_BANG) != 0);
    CHECK((in & YSE::PATCHER::IT_INT) != 0);
    CHECK((in & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((in & YSE::PATCHER::IT_LIST) != 0);

    gToUpper upper;
    CHECK(upper.GetInlet(0)->GetAcceptedTypes() == in);
  }

  // ─── the fold ───────────────────────────────────────────────────────────────

  TEST_CASE("tolower: every A-Z becomes the matching a-z (#810)") {
    Rig rig;
    gToLower obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("ABCDEFGHIJKLMNOPQRSTUVWXYZ", YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "abcdefghijklmnopqrstuvwxyz");
  }

  TEST_CASE("toupper: every a-z becomes the matching A-Z (#810)") {
    Rig rig;
    gToUpper obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("abcdefghijklmnopqrstuvwxyz", YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
  }

  TEST_CASE("case: text already in the target case is unchanged (#810)") {
    Rig lowerRig;
    gToLower lower;
    lowerRig.Wire(lower);
    lower.GetInlet(0)->SetList("voice 3 freq", YSE::T_GUI);
    CHECK(lowerRig.sink.listValue == "voice 3 freq");

    Rig upperRig;
    gToUpper upper;
    upperRig.Wire(upper);
    upper.GetInlet(0)->SetList("VOICE 3 FREQ", YSE::T_GUI);
    CHECK(upperRig.sink.listValue == "VOICE 3 FREQ");
  }

  TEST_CASE("case: mixed case folds one way and only one way (#810)") {
    // The pair is not an involution — .tolower does not put back what .toupper
    // took away — so both directions are asserted on the same input.
    Rig lowerRig;
    gToLower lower;
    lowerRig.Wire(lower);
    lower.GetInlet(0)->SetList("VoIcE MiDi Note", YSE::T_GUI);
    CHECK(lowerRig.sink.listValue == "voice midi note");

    Rig upperRig;
    gToUpper upper;
    upperRig.Wire(upper);
    upper.GetInlet(0)->SetList("VoIcE MiDi Note", YSE::T_GUI);
    CHECK(upperRig.sink.listValue == "VOICE MIDI NOTE");
  }

  TEST_CASE("case: digits, punctuation and symbols are left alone (#810)") {
    // The bytes either side of the folded ranges are the ones an off-by-one in
    // the comparison would catch: '@' is 'A'-1, '[' is 'Z'+1, '`' is 'a'-1 and
    // '{' is 'z'+1.
    Rig lowerRig;
    gToLower lower;
    lowerRig.Wire(lower);
    lower.GetInlet(0)->SetList("@[`{ 0123456789 -+.*/_:;", YSE::T_GUI);
    CHECK(lowerRig.sink.listValue == "@[`{ 0123456789 -+.*/_:;");

    Rig upperRig;
    gToUpper upper;
    upperRig.Wire(upper);
    upper.GetInlet(0)->SetList("@[`{ 0123456789 -+.*/_:;", YSE::T_GUI);
    CHECK(upperRig.sink.listValue == "@[`{ 0123456789 -+.*/_:;");
  }

  // ─── the identity everywhere else ───────────────────────────────────────────

  TEST_CASE("case: whitespace and token structure survive the fold untouched (#810)") {
    // The deliberate difference from .tosymbol and .spell, which collapse runs
    // and trim the ends because they are rebuilding the message. This one
    // rewrites characters in place, and a fold that also re-spaced the message
    // would change what a .route or a .zl downstream sees — which is precisely
    // the thing the object exists to feed.
    Rig rig;
    gToLower obj;
    rig.Wire(obj);

    const std::string spaced = "  VOICE   3\tFREQ  ";
    obj.GetInlet(0)->SetList(spaced, YSE::T_GUI);
    CHECK(rig.sink.listValue == "  voice   3\tfreq  ");
    // Length-preserving, which is the property that makes the reserved buffer a
    // real bound rather than an estimate.
    CHECK(rig.sink.listValue.size() == spaced.size());
  }

  TEST_CASE("case: an empty message is forwarded rather than swallowed (#810)") {
    // Elsewhere in the family an empty result is suppressed because the object
    // produced it out of nothing to send. Here it is what arrived, and a wire
    // that ate it would not be a wire.
    Rig rig;
    gToUpper obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("", YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue.empty());
  }

  TEST_CASE("case: bytes outside ASCII pass through byte for byte (#810)") {
    // A patcher message is bytes and nothing upstream promises an encoding, so
    // this is the case an encoding-blind fold gets silently wrong. Every byte
    // of a multi-byte UTF-8 sequence is 0x80 or above and the folded ranges are
    // entirely below it, so the sequence cannot be touched — and the assertion
    // is on the bytes rather than on the argument, because it also has to hold
    // where `char` is unsigned (Android, ARM Linux), which is the other side of
    // the comparison.
    //
    // "café" — the é is 0xC3 0xA9, and 0xA9 would be the low byte of a
    // lower-case letter if anything read these as Latin-1.
    const std::string accented = "caf\xC3\xA9 CAF\xC3\x89";

    Rig upperRig;
    gToUpper upper;
    upperRig.Wire(upper);
    upper.GetInlet(0)->SetList(accented, YSE::T_GUI);
    // The ASCII letters folded; both accented sequences are exactly as they
    // arrived, so nothing was half-converted and nothing was split.
    CHECK(upperRig.sink.listValue == "CAF\xC3\xA9 CAF\xC3\x89");
    CHECK(upperRig.sink.listValue.size() == accented.size());

    Rig lowerRig;
    gToLower lower;
    lowerRig.Wire(lower);
    lower.GetInlet(0)->SetList(accented, YSE::T_GUI);
    CHECK(lowerRig.sink.listValue == "caf\xC3\xA9 caf\xC3\x89");
    CHECK(lowerRig.sink.listValue.size() == accented.size());
  }

  TEST_CASE("case: every byte value survives except the 52 letters (#810)") {
    // The exhaustive form of the two cases above: build a message out of every
    // byte the object could ever see (0 excepted, which cannot ride a
    // null-terminated message) and assert that exactly the ASCII letters moved.
    std::string all;
    for (int b = 1; b < 256; b++)
      all.push_back(static_cast<char>(b));

    Rig rig;
    gToLower obj;
    rig.Wire(obj);
    obj.GetInlet(0)->SetList(all, YSE::T_GUI);

    const std::string& folded = rig.sink.listValue;
    REQUIRE(folded.size() == all.size());
    for (std::size_t i = 0; i < all.size(); i++) {
      const char in = all[i];
      const char expected = (in >= 'A' && in <= 'Z') ? static_cast<char>(in - 'A' + 'a') : in;
      CAPTURE(static_cast<int>(static_cast<unsigned char>(in)));
      CHECK(folded[i] == expected);
    }
  }

  // ─── the types that have no case ────────────────────────────────────────────

  TEST_CASE("case: a bang, an int and a float pass through in their own type (#810)") {
    // .fromsymbol's rule: an object that is transparent by definition must not
    // be the thing in a chain that quietly changes a message's type.
    Rig rig;
    gToUpper obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.gotBang);
    CHECK_FALSE(rig.sink.gotList);

    rig.sink.reset();
    obj.GetInlet(0)->SetInt(60, YSE::T_GUI);
    CHECK(rig.sink.gotInt);
    CHECK(rig.sink.intValue == 60);
    CHECK_FALSE(rig.sink.gotList);

    rig.sink.reset();
    obj.GetInlet(0)->SetFloat(1.5f, YSE::T_GUI);
    CHECK(rig.sink.gotFloat);
    CHECK(rig.sink.floatValue == doctest::Approx(1.5f));
    CHECK_FALSE(rig.sink.gotList);
  }

  // ─── parameters ─────────────────────────────────────────────────────────────

  TEST_CASE("case: the objects survive a DumpJSON / ParseJSON round trip (#810)") {
    // Neither takes an argument — the fold has nothing to configure — so what a
    // round trip has to preserve is the object's identity and its shape. It is
    // Type() that DumpJSON writes and ParseJSON looks up, so a pair sharing one
    // body is exactly where a save could bring back the wrong half.
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_TOLOWER) != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::G_TOUPPER) != nullptr);

    const std::string json = src.DumpJSON();
    CHECK(json.find(".tolower") != std::string::npos);
    CHECK(json.find(".toupper") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 2);

    bool sawLower = false;
    bool sawUpper = false;
    for (int i = 0; i < 2; i++) {
      YSE::pHandle* copy = loaded.GetHandleFromList(i);
      REQUIRE(copy != nullptr);
      const std::string type = copy->Type();
      CAPTURE(type);
      CHECK(copy->GetInputs() == 1);
      CHECK(copy->GetOutputs() == 1);
      CHECK(copy->GetParams().empty());
      if (type == ".tolower") sawLower = true;
      if (type == ".toupper") sawUpper = true;
    }
    CHECK(sawLower);
    CHECK(sawUpper);

    // And the reloaded object still folds, which is the half a shape-only
    // assertion would miss if ParseJSON had built the other direction.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    for (int i = 0; i < 2; i++) {
      YSE::pHandle* copy = loaded.GetHandleFromList(i);
      if (std::string(copy->Type()) != ".tolower") continue;
      loaded.Connect(copy, 0, &sinkHandle, 0);
      copy->SetListData(0, "VOICE 3 FREQ");
      CHECK(sink.listValue == "voice 3 freq");
    }
  }

  // ─── real-time discipline ───────────────────────────────────────────────────

  TEST_CASE("case: Calculate() emits nothing (#810)") {
    // Both objects are driven by their inlet; one that emitted on a DSP tick
    // would re-fold and hand a patch a message it never sent — the rule .route,
    // .sel, .prepend, .tosymbol and .spell establish.
    Rig lowerRig;
    gToLower lower;
    lowerRig.Wire(lower);
    lower.Calculate(YSE::T_DSP);
    CHECK_FALSE(lowerRig.sink.gotList);
    CHECK_FALSE(lowerRig.sink.gotBang);

    Rig upperRig;
    gToUpper upper;
    upperRig.Wire(upper);
    upper.Calculate(YSE::T_DSP);
    CHECK_FALSE(upperRig.sink.gotList);
    CHECK_FALSE(upperRig.sink.gotBang);
  }

  TEST_CASE("case: the message path allocates nothing (#810)") {
    // Every string this drives the object with is hoisted into a named
    // `std::string` **rather than written as a literal inside the probe**, and
    // that is not tidiness. `inlet::SetList` takes a `const std::string&`, so a
    // literal at the call site materialises a temporary — a heap allocation
    // whenever the text is longer than the implementation's small-string
    // buffer, and that buffer is not the same width everywhere: 15 characters
    // on libstdc++, 22 on libc++. A 16-to-22 character literal therefore costs
    // nothing on the Windows/libc++ build and one allocation on the
    // Linux/libstdc++ one, which is a probe that passes locally and fails in CI
    // while the object under test is innocent.
    //
    // Every one of them is deliberately longer than *both* buffers, so the
    // object is driven from genuinely heap-backed input throughout.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    const std::string warmA = "A FIRST MESSAGE FAR LONGER THAN ANY SMALL-STRING BUFFER";
    const std::string warmB = "a second message, also comfortably past both of them";
    const std::string mixed = "VoIcE 3 FrEq On ThE MaStEr BuS, WhIcH iS a LoNg NaMe";
    const std::string accented =
        "caf\xC3\xA9 na\xC3\xAFve r\xC3\xA9sum\xC3\xA9 well past the buffer";
    const std::string spaced = "   MANY   INTERIOR   SPACES   AND   TRAILING   ONES   ";
    // Exactly the reserved capacity: the widest message the patcher's own
    // queues can carry, and so the widest fold that must not allocate.
    const std::string full(gCaseBase::TEXT_CAPACITY, 'Q');

    MultiSink sink;
    gToLower obj;
    TestHelpers::Wire(obj, 0, sink);

    // Warm every path, so the sink's own buffer and any first-call machinery
    // are not what the probe catches.
    obj.GetInlet(0)->SetList(warmA, YSE::T_GUI);
    obj.GetInlet(0)->SetList(warmB, YSE::T_GUI);
    obj.GetInlet(0)->SetInt(1, YSE::T_GUI);
    obj.GetInlet(0)->SetFloat(0.5f, YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(sink.gotList);

    // Size the sink's buffer independently of the object, so a long input is
    // not what grows it inside the probe.
    const std::string sinkWarm(gCaseBase::TEXT_CAPACITY, 'x');
    sink.GetInlet(0)->SetList(sinkWarm, YSE::T_GUI);

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetList(mixed, YSE::T_DSP);
      obj.GetInlet(0)->SetList(accented, YSE::T_DSP);
      obj.GetInlet(0)->SetList(spaced, YSE::T_DSP);
      obj.GetInlet(0)->SetList(full, YSE::T_DSP);
      obj.GetInlet(0)->SetInt(440, YSE::T_DSP);
      obj.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
      obj.GetInlet(0)->SetBang(YSE::T_DSP);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);

    // And the sends really did happen, so the zero above is not a vacuous pass.
    CHECK(sink.gotBang);
    CHECK(obj.LastOutput() == std::string(gCaseBase::TEXT_CAPACITY, 'q'));
  }

  // ─── end to end, through a real patcher graph ───────────────────────────────

  TEST_CASE("case: .tolower in front of a .route makes the match case-insensitive (#810)") {
    // The use case the issue names, run through the real thing. A unit
    // assertion on the folded text cannot show it: what has to be true is that
    // a *different object* — the one that actually decides whether two names
    // are the same — now answers yes where it answered no, and that it still
    // sees the same token boundaries afterwards.
    //
    // The unfolded arm is the control. Without it a green folded arm would only
    // say ".route matches", not ".route would not have matched without this".
    //
    // Sinks before the patcher: the patcher is torn down first, while the
    // inlets it is wired to still exist.
    MultiSink folded;
    MultiSink foldedReject;
    MultiSink rawMatch;
    MultiSink rawReject;
    YSE::pHandle foldedHandle(&folded);
    YSE::pHandle foldedRejectHandle(&foldedReject);
    YSE::pHandle rawMatchHandle(&rawMatch);
    YSE::pHandle rawRejectHandle(&rawReject);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* lower = p.CreateObject(YSE::OBJ::G_TOLOWER);
    YSE::pHandle* foldedRoute = p.CreateObject(YSE::OBJ::G_ROUTE, "voice");
    YSE::pHandle* rawRoute = p.CreateObject(YSE::OBJ::G_ROUTE, "voice");
    REQUIRE(lower != nullptr);
    REQUIRE(foldedRoute != nullptr);
    REQUIRE(rawRoute != nullptr);
    REQUIRE(foldedRoute->GetOutputs() == 2);

    p.Connect(lower, 0, foldedRoute, 0);
    p.Connect(foldedRoute, 0, &foldedHandle, 0);
    p.Connect(foldedRoute, 1, &foldedRejectHandle, 0);
    p.Connect(rawRoute, 0, &rawMatchHandle, 0);
    p.Connect(rawRoute, 1, &rawRejectHandle, 0);

    // The same address, in the case a user or a host application happened to
    // type it, into both arms.
    lower->SetListData(0, "VOICE 3 FREQ");
    rawRoute->SetListData(0, "VOICE 3 FREQ");

    // Folded: the route matches, and it consumed exactly the token it matched
    // on (#672) — so the fold left the token boundaries where they were.
    CHECK(folded.gotList);
    CHECK(folded.listValue == "3 freq");
    CHECK_FALSE(foldedReject.gotList);

    // Unfolded: the same route, the same text, no match. Every comparison in
    // this patcher is exact, which is the whole reason the object exists.
    CHECK_FALSE(rawMatch.gotList);
    CHECK(rawReject.gotList);
    CHECK(rawReject.listValue == "VOICE 3 FREQ");
  }

  TEST_CASE("case: .toupper feeds an upper-case .route and stays transparent (#810)") {
    // The mirror, and the half that proves the pair is genuinely two directions
    // of one body rather than one direction wired twice: here it is the *patch*
    // that is written in capitals and the incoming text that is folded up to
    // meet it.
    //
    // A bang travels the same wire in the same graph, because transparency is
    // part of the contract and a real graph is where a dropped or retyped
    // message would actually break a patch.
    MultiSink matched;
    MultiSink rejected;
    MultiSink direct;
    YSE::pHandle matchedHandle(&matched);
    YSE::pHandle rejectedHandle(&rejected);
    YSE::pHandle directHandle(&direct);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* upper = p.CreateObject(YSE::OBJ::G_TOUPPER);
    YSE::pHandle* route = p.CreateObject(YSE::OBJ::G_ROUTE, "VOICE");
    REQUIRE(upper != nullptr);
    REQUIRE(route != nullptr);

    p.Connect(upper, 0, route, 0);
    p.Connect(route, 0, &matchedHandle, 0);
    p.Connect(route, 1, &rejectedHandle, 0);
    p.Connect(upper, 0, &directHandle, 0);

    upper->SetListData(0, "voice 3 freq");
    CHECK(matched.gotList);
    CHECK(matched.listValue == "3 FREQ");
    CHECK_FALSE(rejected.gotList);
    CHECK(direct.listValue == "VOICE 3 FREQ");

    // Transparent to a message that has no case: it arrives as the bang it was
    // sent as, not as the symbol that spells one.
    direct.reset();
    upper->SetBang(0);
    CHECK(direct.gotBang);
    CHECK_FALSE(direct.gotList);
  }
}
