// Tests for .sprintf (issue #489) — format a message from words and numbers.
//
// What the object has to get right:
//
//   - **it spells a value into the middle of a word.** That is why it exists:
//     .prepend / .append and .substitute all work in whole tokens, so building
//     `/voice/3/freq` out of the number 3 was a round trip out to the host
//     application.
//   - **the format is never handed to printf.** A format string here comes from
//     a patch author and a patch may be loaded from a file, so the specifiers
//     are parsed here and every argument is rendered explicitly. The tests below
//     therefore push the things that would be undefined behaviour if the string
//     went to printf: a conversion with no argument behind it, an unknown
//     conversion letter, and far more specifiers than the object has room for.
//   - **the inlets come from the format.** "The number of inlets is determined
//     by the number of changeable arguments, with each inlet corresponding to a
//     changeable argument, in order" — so the shape of the object is a function
//     of its creation arguments, and SetParams("") has to take it back.
//   - **inlet 0 is hot and the rest are cold.** Max: "Any of the above messages
//     in the left inlet will format the message and send it out."
//   - **an unset slot follows Max's asymmetry.** A %ld or a %f renders 0, a %s
//     or a %c renders nothing.
//   - **an unconfigured .sprintf sends nothing at all**, so dropping one into a
//     working patch cannot break it.
//
// Most cases run the object standalone — no patcher, so a dropped message
// cannot be confused with one the patcher never delivered — and the last group
// runs it end to end through a real patcher graph, where the format has to
// produce a message the objects downstream actually recognise.

#include <doctest/doctest.h>
#include <string>

#include "patcher/genericObjects/gSprintf.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::gSprintf;

namespace {

  // A standalone object with a sink on its outlet.
  struct Rig {
    MultiSink out;

    void Wire(gSprintf& obj) {
      obj.ConnectOutlet(out.GetInlet(0), 0);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("sprintf: registered, and creatable through the patcher (#489)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SPRINTF, "/voice/%ld/freq %f");
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".sprintf");
    // One inlet per changeable argument — Max's "the number of inlets is
    // determined by the number of changeable arguments".
    CHECK(h->GetInputs() == 2);
    CHECK(h->GetOutputs() == 1);
  }

  TEST_CASE("sprintf: appears in the registry's name list (#489)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_SPRINTF)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("sprintf: a format with no changeable arguments still has one inlet (#489)") {
    // Max's is a constant message generator in that case, and an object with no
    // inlets at all could not be sent anything.
    gSprintf obj;
    obj.SetParams("hello world");
    CHECK(obj.NumInputs() == 1);
    CHECK(obj.SlotCount() == 0);
    CHECK(obj.Format() == "hello world");
  }

  TEST_CASE("sprintf: inlet 0 takes everything, the cold inlets decline a bang (#489)") {
    gSprintf obj;
    obj.SetParams("%ld %s %f");
    REQUIRE(obj.NumInputs() == 3);

    const unsigned int hot = obj.GetInlet(0)->GetAcceptedTypes();
    CHECK((hot & YSE::PATCHER::IT_BANG) != 0);
    CHECK((hot & YSE::PATCHER::IT_INT) != 0);
    CHECK((hot & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((hot & YSE::PATCHER::IT_LIST) != 0);

    for (int pin = 1; pin <= 2; pin++) {
      CAPTURE(pin);
      const unsigned int cold = obj.GetInlet(pin)->GetAcceptedTypes();
      // Nothing for a bang to do on a cold inlet, so it stays out of the
      // reported contract — the .substitute / .router discipline.
      CHECK((cold & YSE::PATCHER::IT_BANG) == 0);
      CHECK((cold & YSE::PATCHER::IT_INT) != 0);
      CHECK((cold & YSE::PATCHER::IT_FLOAT) != 0);
      CHECK((cold & YSE::PATCHER::IT_LIST) != 0);
    }
  }

  // ─── the formatting ─────────────────────────────────────────────────────────

  TEST_CASE("sprintf: an int is spelled into the middle of a word (#489)") {
    // The whole reason the object exists: no other message-construction object
    // can put a value *inside* a token.
    Rig rig;
    gSprintf obj;
    obj.SetParams("/voice/%ld/freq");
    rig.Wire(obj);

    obj.GetInlet(0)->SetInt(3, YSE::T_GUI);
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "/voice/3/freq");
  }

  TEST_CASE("sprintf: %f renders C's six decimals, and a precision narrows it (#489)") {
    Rig rig;
    gSprintf obj;
    obj.SetParams("%f");
    rig.Wire(obj);

    obj.GetInlet(0)->SetFloat(440.5f, YSE::T_GUI);
    CHECK(rig.out.listValue == "440.500000");

    Rig rig2;
    gSprintf obj2;
    obj2.SetParams("freq %.2f");
    rig2.Wire(obj2);
    obj2.GetInlet(0)->SetFloat(440.567f, YSE::T_GUI);
    CHECK(rig2.out.listValue == "freq 440.57");

    // A precision of 0 drops the point entirely, as C's does.
    Rig rig3;
    gSprintf obj3;
    obj3.SetParams("%.0f");
    rig3.Wire(obj3);
    obj3.GetInlet(0)->SetFloat(2.4f, YSE::T_GUI);
    CHECK(rig3.out.listValue == "2");
    obj3.GetInlet(0)->SetFloat(2.6f, YSE::T_GUI);
    CHECK(rig3.out.listValue == "3");
  }

  TEST_CASE("sprintf: a negative float keeps its sign and its decimals (#489)") {
    Rig rig;
    gSprintf obj;
    obj.SetParams("%.3f");
    rig.Wire(obj);

    obj.GetInlet(0)->SetFloat(-1.25f, YSE::T_GUI);
    CHECK(rig.out.listValue == "-1.250");
  }

  TEST_CASE("sprintf: %s takes a symbol, and a number as the text that spells it (#489)") {
    Rig rig;
    gSprintf obj;
    obj.SetParams("/%s/gain");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("bass", YSE::T_GUI);
    CHECK(rig.out.listValue == "/bass/gain");

    // A patch feeding a counter into a %s obviously means the digits, so the
    // number is stored as its text rather than refused.
    obj.GetInlet(0)->SetInt(7, YSE::T_GUI);
    CHECK(rig.out.listValue == "/7/gain");
  }

  TEST_CASE("sprintf: %c renders the int as its character (#489)") {
    Rig rig;
    gSprintf obj;
    obj.SetParams("note %c");
    rig.Wire(obj);

    obj.GetInlet(0)->SetInt(65, YSE::T_GUI);
    CHECK(rig.out.listValue == "note A");
  }

  TEST_CASE("sprintf: an unset slot follows Max's asymmetry — 0 or blank (#489)") {
    // Max: "If no value has been received for a changeable number argument (%ld
    // or %f), 0 will be substituted for that argument. If no value has been
    // received for a %s or %c argument, that argument will be left blank."
    Rig rig;
    gSprintf obj;
    obj.SetParams("[%ld][%f][%s][%c]");
    rig.Wire(obj);
    REQUIRE(obj.NumInputs() == 4);

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "[0][0.000000][][]");
  }

  TEST_CASE("sprintf: %% is one literal per cent sign and builds no inlet (#489)") {
    Rig rig;
    gSprintf obj;
    obj.SetParams("%ld%% done");
    rig.Wire(obj);
    CHECK(obj.SlotCount() == 1);

    obj.GetInlet(0)->SetInt(50, YSE::T_GUI);
    CHECK(rig.out.listValue == "50% done");
  }

  // ─── flags, width and precision ─────────────────────────────────────────────

  TEST_CASE("sprintf: a zero-padded width is the OSC/MIDI-label idiom (#489)") {
    Rig rig;
    gSprintf obj;
    obj.SetParams("chan%03ld");
    rig.Wire(obj);

    obj.GetInlet(0)->SetInt(7, YSE::T_GUI);
    CHECK(rig.out.listValue == "chan007");

    // The sign stays in front of the zeros, which is C's rule and the only
    // spelling that reads back as the number it is.
    obj.GetInlet(0)->SetInt(-5, YSE::T_GUI);
    CHECK(rig.out.listValue == "chan-05");
  }

  TEST_CASE("sprintf: a plain width pads with spaces, and '-' flips the side (#489)") {
    Rig rig;
    gSprintf obj;
    obj.SetParams("[%4ld][%-4ld]");
    rig.Wire(obj);
    REQUIRE(obj.NumInputs() == 2);

    obj.GetInlet(1)->SetInt(7, YSE::T_GUI);
    obj.GetInlet(0)->SetInt(7, YSE::T_GUI);
    CHECK(rig.out.listValue == "[   7][7   ]");
  }

  TEST_CASE("sprintf: a precision on %s cuts the symbol to that many characters (#489)") {
    Rig rig;
    gSprintf obj;
    obj.SetParams("%.3s");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("abcdefg", YSE::T_GUI);
    CHECK(rig.out.listValue == "abc");
  }

  // ─── which inlet acts ───────────────────────────────────────────────────────

  TEST_CASE("sprintf: inlet 0 is hot and every other inlet is cold (#489)") {
    // Max: "Any of the above messages in the left inlet will format the message
    // and send it out."
    Rig rig;
    gSprintf obj;
    obj.SetParams("%s %ld");
    rig.Wire(obj);
    REQUIRE(obj.NumInputs() == 2);

    obj.GetInlet(1)->SetInt(60, YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);

    obj.GetInlet(0)->SetList("note", YSE::T_GUI);
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "note 60");
  }

  TEST_CASE("sprintf: a bang re-sends the stored values without changing them (#489)") {
    Rig rig;
    gSprintf obj;
    obj.SetParams("%s %ld");
    rig.Wire(obj);

    obj.GetInlet(1)->SetInt(60, YSE::T_GUI);
    obj.GetInlet(0)->SetList("note", YSE::T_GUI);
    rig.out.reset();

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "note 60");
  }

  TEST_CASE("sprintf: a list is spread one token per changeable argument (#489)") {
    // Max: "Each item in the list is treated as if it had been received in a
    // separate inlet, up to the number of inlets."
    Rig rig;
    gSprintf obj;
    obj.SetParams("/%s/%ld gain %.1f");
    rig.Wire(obj);
    REQUIRE(obj.NumInputs() == 3);

    obj.GetInlet(0)->SetList("bass 3 0.75", YSE::T_GUI);
    CHECK(rig.out.listValue == "/bass/3 gain 0.8");
  }

  TEST_CASE("sprintf: tokens past the last changeable argument are dropped (#489)") {
    Rig rig;
    gSprintf obj;
    obj.SetParams("%s");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("one two three", YSE::T_GUI);
    CHECK(rig.out.listValue == "one");
  }

  TEST_CASE("sprintf: a list into a cold inlet fills from there rightwards (#489)") {
    Rig rig;
    gSprintf obj;
    obj.SetParams("%s-%s-%s");
    rig.Wire(obj);
    REQUIRE(obj.NumInputs() == 3);

    obj.GetInlet(1)->SetList("b c", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);

    obj.GetInlet(0)->SetList("a", YSE::T_GUI);
    CHECK(rig.out.listValue == "a-b-c");
  }

  TEST_CASE("sprintf: inlet 0 has no reserved words — 'set' is data (#489)") {
    // The .prepend / .substitute discipline: the values this object formats are
    // often symbols, so a word inlet 0 swallowed would be a word a patch could
    // not format.
    Rig rig;
    gSprintf obj;
    obj.SetParams("<%s>");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("set", YSE::T_GUI);
    CHECK(rig.out.gotList);
    CHECK(rig.out.listValue == "<set>");
  }

  // ─── what a slot refuses ────────────────────────────────────────────────────

  TEST_CASE("sprintf: a symbol at a number's inlet is refused, not zeroed (#489)") {
    // Leaving the previous value standing keeps the rest of the message readable
    // while the patching mistake is found; a 0 would look like a value the patch
    // sent.
    Rig rig;
    gSprintf obj;
    obj.SetParams("%ld");
    rig.Wire(obj);

    obj.GetInlet(0)->SetInt(12, YSE::T_GUI);
    CHECK(rig.out.listValue == "12");

    obj.GetInlet(0)->SetList("banana", YSE::T_GUI);
    CHECK(rig.out.listValue == "12");
  }

  TEST_CASE("sprintf: a symbol longer than the capacity is refused, not truncated (#489)") {
    Rig rig;
    gSprintf obj;
    obj.SetParams("%s");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("short", YSE::T_GUI);
    CHECK(rig.out.listValue == "short");

    const std::string tooLong(gSprintf::TOKEN_CAPACITY + 1, 'x');
    obj.GetInlet(0)->SetList(tooLong, YSE::T_GUI);
    // Half a symbol is a different symbol: the previous one stands.
    CHECK(rig.out.listValue == "short");

    const std::string atCapacity(gSprintf::TOKEN_CAPACITY, 'y');
    obj.GetInlet(0)->SetList(atCapacity, YSE::T_GUI);
    CHECK(rig.out.listValue == atCapacity);
  }

  // ─── the format parser refuses rather than guesses ──────────────────────────

  TEST_CASE("sprintf: an unknown conversion builds no inlet and stays text (#489)") {
    // The case that would be undefined behaviour if the string reached printf.
    Rig rig;
    gSprintf obj;
    obj.SetParams("%q done");
    rig.Wire(obj);
    CHECK(obj.SlotCount() == 0);
    CHECK(obj.NumInputs() == 1);

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.out.listValue == "%q done");
  }

  TEST_CASE("sprintf: a trailing bare %% builds no inlet and stays text (#489)") {
    Rig rig;
    gSprintf obj;
    obj.SetParams("100%");
    rig.Wire(obj);
    CHECK(obj.SlotCount() == 0);

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.out.listValue == "100%");
  }

  TEST_CASE("sprintf: a width beyond the cap is refused rather than clamped (#489)") {
    gSprintf obj;
    obj.SetParams("%999ld");
    // Silently shrinking the field would spell the message differently from the
    // way the patch asked for it, and the output buffer this bounds is reserved
    // before the object is published.
    CHECK(obj.SlotCount() == 0);

    gSprintf ok;
    ok.SetParams("%64ld");
    CHECK(ok.SlotCount() == 1);
  }

  TEST_CASE("sprintf: at most 32 changeable arguments become inlets (#489)") {
    std::string format;
    for (int i = 0; i < 40; i++)
      format += "%ld";

    gSprintf obj;
    obj.SetParams(format);
    CHECK(obj.SlotCount() == (int)gSprintf::MAX_SLOTS);
    CHECK(obj.NumInputs() == (int)gSprintf::MAX_SLOTS);
  }

  TEST_CASE("sprintf: %ld, %d and %lld all mean the patcher's one integer type (#489)") {
    // Max's own documentation spells an int argument %ld, so refusing the length
    // modifiers would refuse every format copied over from a Max patch.
    Rig rig;
    gSprintf obj;
    obj.SetParams("%ld %d %lld");
    rig.Wire(obj);
    REQUIRE(obj.NumInputs() == 3);

    obj.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    CHECK(rig.out.listValue == "1 2 3");
  }

  // ─── configuration ──────────────────────────────────────────────────────────

  TEST_CASE("sprintf: with no format at all it sends nothing (#489)") {
    // The .prepend rule that makes it safe to drop an unconfigured object into a
    // working patch.
    Rig rig;
    gSprintf obj;
    rig.Wire(obj);
    CHECK(obj.NumInputs() == 1);
    CHECK(obj.Format().empty());

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    obj.GetInlet(0)->SetInt(5, YSE::T_GUI);
    obj.GetInlet(0)->SetList("anything", YSE::T_GUI);
    CHECK_FALSE(rig.out.gotList);
    CHECK_FALSE(rig.out.gotInt);
    CHECK_FALSE(rig.out.gotBang);
  }

  TEST_CASE("sprintf: SetParams(\"\") is a real reset, not a no-op (#489)") {
    // Parameters::Set returns early on an empty argument string, so only the
    // clear callback makes this a reset — and here it has to take the inlets
    // away again, not just the text.
    gSprintf obj;
    obj.SetParams("%s/%ld");
    REQUIRE(obj.NumInputs() == 2);

    obj.SetParams("");
    CHECK(obj.NumInputs() == 1);
    CHECK(obj.SlotCount() == 0);
    CHECK(obj.Format().empty());
  }

  TEST_CASE("sprintf: symout is consumed rather than formatted (#489)") {
    // Max: "the word symout itself is not included in the output of sprintf".
    // It switches nothing here — this patcher carries every message as one piece
    // of text — but reading it as part of the format would put a stray word at
    // the front of every message copied over from Max.
    Rig rig;
    gSprintf obj;
    obj.SetParams("symout /voice/%ld");
    rig.Wire(obj);
    CHECK(obj.SymOut());
    CHECK(obj.Format() == "/voice/%ld");

    obj.GetInlet(0)->SetInt(2, YSE::T_GUI);
    CHECK(rig.out.listValue == "/voice/2");
  }

  TEST_CASE("sprintf: the format is the whole argument string, spaces included (#489)") {
    gSprintf obj;
    obj.SetParams("note %ld vel %ld");
    CHECK(obj.Format() == "note %ld vel %ld");
    CHECK(obj.SlotCount() == 2);
  }

  TEST_CASE("sprintf: stored values are run-time state, not parameters (#489)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_SPRINTF, "/voice/%ld");
    REQUIRE(h != nullptr);

    h->SetIntData(0, 9);
    CHECK(h->GetParams() == std::string("/voice/%ld"));
  }

  TEST_CASE("sprintf: params survive a DumpJSON / ParseJSON round trip (#489)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_SPRINTF, "/voice/%ld/gain %.2f") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".sprintf") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".sprintf");
    CHECK(copy->GetParams() == std::string("/voice/%ld/gain %.2f"));
    // The inlets are rebuilt from the reloaded format, not remembered.
    CHECK(copy->GetInputs() == 2);
    CHECK(copy->GetOutputs() == 1);
  }

  // ─── end to end, through a real patcher graph ───────────────────────────────

  TEST_CASE("sprintf: builds a selector a .route downstream matches on (#489)") {
    // The use case the issue names, run through the real thing: a number that
    // only exists at run time becomes part of a *word*, and a .route matches on
    // it. Nothing short of the whole chain proves that — a unit test asserting
    // on the formatted text cannot tell "voice3" from "voice3 ", and only one of
    // those routes.
    //
    // Sinks before the patcher: the patcher is torn down first, while the inlets
    // it is wired to still exist.
    MultiSink matched;
    MultiSink fallthrough;
    YSE::pHandle matchedHandle(&matched);
    YSE::pHandle fallthroughHandle(&fallthrough);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* fmt = p.CreateObject(YSE::OBJ::G_SPRINTF, "voice%ld");
    YSE::pHandle* route = p.CreateObject(YSE::OBJ::G_ROUTE, "voice3");
    REQUIRE(fmt != nullptr);
    REQUIRE(route != nullptr);
    REQUIRE(route->GetOutputs() == 2);

    p.Connect(fmt, 0, route, 0);
    p.Connect(route, 0, &matchedHandle, 0);
    p.Connect(route, 1, &fallthroughHandle, 0);

    fmt->SetIntData(0, 3);
    CHECK(matched.gotList);
    CHECK(matched.listValue == "voice3");
    CHECK_FALSE(fallthrough.gotList);

    matched.reset();
    fallthrough.reset();
    fmt->SetIntData(0, 4);
    CHECK_FALSE(matched.gotList);
    CHECK(fallthrough.gotList);
    CHECK(fallthrough.listValue == "voice4");
  }

  TEST_CASE("sprintf: a .sprintf feeding a .prepend composes into one message (#489)") {
    // The message-construction family working together: .sprintf spells the
    // value into a word, .prepend puts the selector in front of it.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* fmt = p.CreateObject(YSE::OBJ::G_SPRINTF, "/ch/%02ld/gain");
    YSE::pHandle* pre = p.CreateObject(YSE::OBJ::G_PREPEND, "send");
    REQUIRE(fmt != nullptr);
    REQUIRE(pre != nullptr);

    p.Connect(fmt, 0, pre, 0);
    p.Connect(pre, 0, &sinkHandle, 0);

    fmt->SetIntData(0, 4);
    CHECK(sink.gotList);
    CHECK(sink.listValue == "send /ch/04/gain");
  }

  TEST_CASE("sprintf: a cold inlet fed through the graph waits for inlet 0 (#489)") {
    // The hot/cold split, driven the way a patch drives it rather than by
    // calling the handlers directly: a value arriving down a cord into inlet 1
    // must not emit, and the next message into inlet 0 must carry it.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* fmt = p.CreateObject(YSE::OBJ::G_SPRINTF, "note %ld vel %ld");
    REQUIRE(fmt != nullptr);
    REQUIRE(fmt->GetInputs() == 2);

    p.Connect(fmt, 0, &sinkHandle, 0);

    fmt->SetIntData(1, 100);
    CHECK_FALSE(sink.gotList);

    fmt->SetIntData(0, 60);
    CHECK(sink.gotList);
    CHECK(sink.listValue == "note 60 vel 100");
  }
}
