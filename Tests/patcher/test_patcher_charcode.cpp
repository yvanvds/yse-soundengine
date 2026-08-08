// Tests for .atoi and .itoa (issue #493) — the inverse pair that takes text
// apart into character codes and puts it back together again.
//
// Three things are worth pinning here, and they are the three an implementation
// can get wrong without ever failing:
//
//   - **the pair is a real round trip.** .atoi after .itoa has to give back the
//     codes that went in, and .itoa after .atoi the message. That only holds if
//     both ends read and write a character the same way, which is why the
//     decoder lives in one place (pCharCodes.h) and why .atoi has to agree with
//     .spell character for character.
//   - **the state Max gives them, which .spell does not have.** Three inlets: one
//     that converts and sends, one that appends silently, one that loads
//     silently — plus a bang that re-sends and an empty message that clears.
//     Appending is at the *code* level, so 'a' then 'b' is not 'a b'.
//   - **a refused message changes nothing.** Anything that is not a character,
//     and anything that would take the contents past 256, is refused whole —
//     what was already stored has to survive it untouched, because a patch
//     assembling a name a piece at a time could not tell that a piece had gone
//     missing.
//
// No audio device and no engine of its own, except where a real patcher graph is
// the point.

#include <doctest/doctest.h>
#include <string>

#include "patcher/genericObjects/gCharCode.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::gAtoi;
using YSE::PATCHER::gItoa;

namespace {

  // A standalone object with a sink on its outlet. Standalone on purpose: these
  // objects need no patcher at all, and a test that needed one could not tell a
  // refused message from a message the patcher never delivered.
  template <typename T> struct Rig {
    MultiSink sink;
    T obj;

    Rig() {
      obj.ConnectOutlet(sink.GetInlet(0), 0);
    }
  };

  // How many codes a result holds — the tokens of the list it came out as.
  std::size_t CodeCount(const std::string& list) {
    if (list.empty()) return 0;
    std::size_t count = 1;
    for (char c : list) {
      if (c == ' ') count++;
    }
    return count;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("atoi/itoa: registered, three inlets and one outlet (#493)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* atoi = p.CreateObject(YSE::OBJ::G_ATOI);
    REQUIRE(atoi != nullptr);
    CHECK(std::string(atoi->Type()) == ".atoi");
    CHECK(atoi->GetInputs() == 3);
    CHECK(atoi->GetOutputs() == 1);

    YSE::pHandle* itoa = p.CreateObject(YSE::OBJ::G_ITOA);
    REQUIRE(itoa != nullptr);
    CHECK(std::string(itoa->Type()) == ".itoa");
    CHECK(itoa->GetInputs() == 3);
    CHECK(itoa->GetOutputs() == 1);
  }

  TEST_CASE("atoi/itoa: appear in the registry's name list (#493)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool foundAtoi = false;
    bool foundItoa = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ATOI)) foundAtoi = true;
      if (name == std::string(YSE::OBJ::G_ITOA)) foundItoa = true;
    }
    CHECK(foundAtoi);
    CHECK(foundItoa);
  }

  TEST_CASE("atoi: only inlet 0 takes a bang (#493)") {
    // Max's bang triggers the output of what is stored, and the two silent
    // inlets have nothing for one to do — so no handler is registered there and
    // GetAcceptedTypes() keeps reporting the object's real contract.
    gAtoi obj;
    const unsigned int in0 = obj.GetInlet(0)->GetAcceptedTypes();
    CHECK((in0 & YSE::PATCHER::IT_INT) != 0);
    CHECK((in0 & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((in0 & YSE::PATCHER::IT_LIST) != 0);
    CHECK((in0 & YSE::PATCHER::IT_BANG) != 0);

    for (int pin = 1; pin <= 2; pin++) {
      const unsigned int accepted = obj.GetInlet(pin)->GetAcceptedTypes();
      CHECK((accepted & YSE::PATCHER::IT_INT) != 0);
      CHECK((accepted & YSE::PATCHER::IT_FLOAT) != 0);
      CHECK((accepted & YSE::PATCHER::IT_LIST) != 0);
      CHECK((accepted & YSE::PATCHER::IT_BANG) == 0);
    }
  }

  TEST_CASE("itoa: only inlet 0 takes a bang (#493)") {
    gItoa obj;
    CHECK((obj.GetInlet(0)->GetAcceptedTypes() & YSE::PATCHER::IT_BANG) != 0);
    CHECK((obj.GetInlet(1)->GetAcceptedTypes() & YSE::PATCHER::IT_BANG) == 0);
    CHECK((obj.GetInlet(2)->GetAcceptedTypes() & YSE::PATCHER::IT_BANG) == 0);
  }

  TEST_CASE("atoi/itoa: a fresh object holds nothing (#493)") {
    gAtoi atoi;
    CHECK(atoi.Contents().empty());
    CHECK(atoi.Count() == 0);

    gItoa itoa;
    CHECK(itoa.Contents().empty());
    CHECK(itoa.Count() == 0);
  }

  TEST_CASE("atoi/itoa: Calculate() sends nothing (#493)") {
    // The rule the whole message family keeps: the object is driven by its
    // inlets, and one that emitted from Calculate() would re-send on every DSP
    // tick after an inlet fired.
    Rig<gAtoi> atoi;
    atoi.obj.GetInlet(0)->SetList("hi", YSE::T_GUI);
    atoi.sink.reset();
    atoi.obj.Calculate(YSE::T_DSP);
    CHECK_FALSE(atoi.sink.gotList);

    Rig<gItoa> itoa;
    itoa.obj.GetInlet(0)->SetList("104 105", YSE::T_GUI);
    itoa.sink.reset();
    itoa.obj.Calculate(YSE::T_DSP);
    CHECK_FALSE(itoa.sink.gotList);
  }

  // ─── .atoi: Max's four rules, which are one rule here ───────────────────────

  TEST_CASE("atoi: a symbol converts character by character (#493)") {
    // Max: "the character code value of each letter, digit, or other character
    // in the symbol is stored internally."
    Rig<gAtoi> rig;
    rig.obj.GetInlet(0)->SetList("hi", YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "104 105");
  }

  TEST_CASE("atoi: an int converts digit by digit (#493)") {
    // Max: "the character code value of each of the digits of the number is
    // stored internally and sent out." The digits are the number's text, which
    // is why this needs no rule of its own here.
    Rig<gAtoi> rig;
    rig.obj.GetInlet(0)->SetInt(123, YSE::T_GUI);
    CHECK(rig.sink.listValue == "49 50 51");
  }

  TEST_CASE("atoi: a float converts its decimal point too (#493)") {
    Rig<gAtoi> rig;
    rig.obj.GetInlet(0)->SetFloat(2.5f, YSE::T_GUI);
    CHECK(rig.sink.listValue == "50 46 53");
  }

  TEST_CASE("atoi: a list gets Max's space, 32, between its items (#493)") {
    // Max: "a space character (ASCII value 32) is inserted between items in the
    // list." Here that is not a rule of its own: a space is the character that
    // is actually there.
    Rig<gAtoi> rig;
    rig.obj.GetInlet(0)->SetList("1 2", YSE::T_GUI);
    CHECK(rig.sink.listValue == "49 32 50");
  }

  TEST_CASE("atoi: whitespace runs collapse and the ends are trimmed (#493)") {
    Rig<gAtoi> rig;
    rig.obj.GetInlet(0)->SetList("  a   b  ", YSE::T_GUI);
    CHECK(rig.sink.listValue == "97 32 98");
  }

  TEST_CASE("atoi: an empty message stores nothing and sends nothing (#493)") {
    // Inert rather than a source of empty messages — the .prepend / .spell rule
    // that makes an object safe to drop into a working patch.
    Rig<gAtoi> rig;
    rig.obj.GetInlet(0)->SetList("", YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
    CHECK(rig.obj.Contents().empty());

    rig.obj.GetInlet(0)->SetList("   ", YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
  }

  // ─── .atoi: the encoding, shared with .spell ────────────────────────────────

  TEST_CASE("atoi: a well-formed UTF-8 sequence is one code point (#493)") {
    // The case an encoding-blind implementation gets wrong without ever
    // failing: it would emit two codes for one visible character. Written as
    // explicit bytes so the test does not depend on the source file's encoding.
    Rig<gAtoi> rig;

    // U+00E9 LATIN SMALL LETTER E WITH ACUTE, two bytes.
    rig.obj.GetInlet(0)->SetList(std::string("\xC3\xA9"), YSE::T_GUI);
    CHECK(rig.sink.listValue == "233");

    // U+20AC EURO SIGN, three bytes.
    rig.obj.GetInlet(0)->SetList(std::string("\xE2\x82\xAC"), YSE::T_GUI);
    CHECK(rig.sink.listValue == "8364");

    // U+1F600 GRINNING FACE, four bytes.
    rig.obj.GetInlet(0)->SetList(std::string("\xF0\x9F\x98\x80"), YSE::T_GUI);
    CHECK(rig.sink.listValue == "128512");
  }

  TEST_CASE("atoi: a byte that is not well-formed UTF-8 spells its own value (#493)") {
    Rig<gAtoi> rig;

    // A lead byte with no continuation after it.
    rig.obj.GetInlet(0)->SetList(std::string("\xC3"), YSE::T_GUI);
    CHECK(rig.sink.listValue == "195");

    // A truncated three-byte sequence: each byte spells itself, and the 'a'
    // after it is still reached.
    rig.obj.GetInlet(0)->SetList(std::string("\xE2\x82") + "a", YSE::T_GUI);
    CHECK(rig.sink.listValue == "226 130 97");
  }

  TEST_CASE("atoi: gives the same answer as .spell, character for character (#493)") {
    // Both objects read a character through the same decoder, and this is what
    // that is for: if they ever disagreed, a patch could not use one to check
    // the other and the .itoa round trip would depend on which object produced
    // the codes.
    MultiSink atoiSink;
    MultiSink spellSink;
    YSE::pHandle atoiHandle(&atoiSink);
    YSE::pHandle spellHandle(&spellSink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* atoi = p.CreateObject(YSE::OBJ::G_ATOI);
    YSE::pHandle* spell = p.CreateObject(YSE::OBJ::G_SPELL);
    REQUIRE(atoi != nullptr);
    REQUIRE(spell != nullptr);
    p.Connect(atoi, 0, &atoiHandle, 0);
    p.Connect(spell, 0, &spellHandle, 0);

    const std::string samples[] = {"hi",  "a b",  "123",   std::string("\xC3\xA9"),
                                   "a 1", "\xC3", "cab 7", std::string("x") + "\xE2\x82\xAC"};
    for (const auto& sample : samples) {
      atoiSink.reset();
      spellSink.reset();
      atoi->SetListData(0, sample);
      spell->SetListData(0, sample);
      CHECK(atoiSink.listValue == spellSink.listValue);
    }
  }

  // ─── .atoi: the three inlets ────────────────────────────────────────────────

  TEST_CASE("atoi: a bang re-sends the stored codes (#493)") {
    // Max: "a bang message can be used to trigger the output of the currently
    // stored numerical list."
    Rig<gAtoi> rig;
    rig.obj.GetInlet(0)->SetList("hi", YSE::T_GUI);
    REQUIRE(rig.sink.listValue == "104 105");

    rig.sink.reset();
    rig.obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "104 105");
  }

  TEST_CASE("atoi: a bang with nothing stored sends nothing (#493)") {
    Rig<gAtoi> rig;
    rig.obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
  }

  TEST_CASE("atoi: inlet 1 appends at the code level and sends nothing (#493)") {
    // Max's middle inlet. Appending is at the code level, so 'a' then 'b' is
    // the same as the one message 'ab' and *not* the 97 32 98 that the
    // two-token message 'a b' gives — the distinction the object would lose if
    // it appended whole messages.
    Rig<gAtoi> rig;
    rig.obj.GetInlet(1)->SetList("a", YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
    rig.obj.GetInlet(1)->SetList("b", YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
    CHECK(rig.obj.Contents() == "97 98");

    rig.obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.listValue == "97 98");
  }

  TEST_CASE("atoi: inlet 2 replaces the contents and sends nothing (#493)") {
    Rig<gAtoi> rig;
    rig.obj.GetInlet(0)->SetList("hi", YSE::T_GUI);
    rig.sink.reset();

    rig.obj.GetInlet(2)->SetList("ab", YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
    CHECK(rig.obj.Contents() == "97 98");

    rig.obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.listValue == "97 98");
  }

  TEST_CASE("atoi: an empty message on inlet 2 is Max's clear (#493)") {
    // Spelled as an empty message rather than a reserved word, because inlet 0
    // has to be able to carry the word 'clear' as text like any other — the
    // .prepend discipline, and .fromsymbol's empty separator is the same move.
    Rig<gAtoi> rig;
    rig.obj.GetInlet(0)->SetList("hi", YSE::T_GUI);
    REQUIRE(rig.obj.Count() == 2);
    rig.sink.reset();

    rig.obj.GetInlet(2)->SetList("", YSE::T_GUI);
    CHECK(rig.obj.Contents().empty());
    CHECK(rig.obj.Count() == 0);
    CHECK_FALSE(rig.sink.gotList);

    rig.obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
  }

  TEST_CASE("atoi: the word clear is converted, not obeyed (#493)") {
    // The reason clear is not a reserved word: this object exists to carry
    // arbitrary text, and a reserved one would swallow the very message it was
    // asked to convert.
    Rig<gAtoi> rig;
    rig.obj.GetInlet(0)->SetList("clear", YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "99 108 101 97 114");
  }

  // ─── .itoa: codes back into characters ──────────────────────────────────────

  TEST_CASE("itoa: a list of codes gives the text they spell (#493)") {
    Rig<gItoa> rig;
    rig.obj.GetInlet(0)->SetList("104 105", YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "hi");
  }

  TEST_CASE("itoa: a single int gives the one character it names (#493)") {
    Rig<gItoa> rig;
    rig.obj.GetInlet(0)->SetInt(65, YSE::T_GUI);
    CHECK(rig.sink.listValue == "A");
  }

  TEST_CASE("itoa: a float is truncated to an int first (#493)") {
    // Max: "converted to an int before being used."
    Rig<gItoa> rig;
    rig.obj.GetInlet(0)->SetFloat(65.9f, YSE::T_GUI);
    CHECK(rig.sink.listValue == "A");
  }

  TEST_CASE("itoa: code 32 puts a real space in the text (#493)") {
    Rig<gItoa> rig;
    rig.obj.GetInlet(0)->SetList("97 32 98", YSE::T_GUI);
    CHECK(rig.sink.listValue == "a b");
  }

  TEST_CASE("itoa: a code above ASCII is written as UTF-8 (#493)") {
    // The inverse of what .atoi reads, byte for byte — the whole reason the two
    // share one implementation of the encoding.
    Rig<gItoa> rig;
    rig.obj.GetInlet(0)->SetList("233", YSE::T_GUI);
    CHECK(rig.sink.listValue == std::string("\xC3\xA9"));

    rig.obj.GetInlet(0)->SetList("8364", YSE::T_GUI);
    CHECK(rig.sink.listValue == std::string("\xE2\x82\xAC"));

    rig.obj.GetInlet(0)->SetList("128512", YSE::T_GUI);
    CHECK(rig.sink.listValue == std::string("\xF0\x9F\x98\x80"));
  }

  TEST_CASE("itoa: an empty message stores nothing and sends nothing (#493)") {
    Rig<gItoa> rig;
    rig.obj.GetInlet(0)->SetList("", YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
    CHECK(rig.obj.Contents().empty());
  }

  TEST_CASE("itoa: a token that is not a number is refused whole (#493)") {
    // Strict, as the rest of the family is: there is no character for 'a' to
    // name, and converting the part of the message that does read as numbers
    // would send a *different* symbol without saying so.
    Rig<gItoa> rig;
    rig.obj.GetInlet(0)->SetList("104 105", YSE::T_GUI);
    REQUIRE(rig.sink.listValue == "hi");
    rig.sink.reset();

    rig.obj.GetInlet(0)->SetList("104 a 105", YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
    // And what was already there survived the refusal.
    CHECK(rig.obj.Contents() == "hi");
    rig.obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.listValue == "hi");
  }

  TEST_CASE("itoa: a number that is not a code point is refused whole (#493)") {
    // Negative, above the highest code point, or a surrogate half — none of the
    // three has an encoding, and inventing one would produce bytes an .atoi
    // reads back as a different code.
    Rig<gItoa> rig;

    rig.obj.GetInlet(0)->SetList("104 -1", YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
    CHECK(rig.obj.Contents().empty());

    rig.obj.GetInlet(0)->SetList("1114112", YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);

    rig.obj.GetInlet(0)->SetList("55296", YSE::T_GUI); // U+D800, a surrogate half
    CHECK_FALSE(rig.sink.gotList);

    // The values on either side of the surrogate block are characters.
    rig.obj.GetInlet(0)->SetList("55295", YSE::T_GUI);
    CHECK(rig.sink.gotList);
    rig.sink.reset();
    rig.obj.GetInlet(0)->SetList("57344", YSE::T_GUI);
    CHECK(rig.sink.gotList);
  }

  // ─── .itoa: the three inlets ────────────────────────────────────────────────

  TEST_CASE("itoa: a bang re-sends the stored characters (#493)") {
    Rig<gItoa> rig;
    rig.obj.GetInlet(0)->SetList("104 105", YSE::T_GUI);
    rig.sink.reset();
    rig.obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.listValue == "hi");
  }

  TEST_CASE("itoa: inlet 1 builds a symbol one character at a time (#493)") {
    // Max's middle inlet, and the thing the pair exists for: a patch assembles
    // a name a character at a time and sends it once it is complete.
    Rig<gItoa> rig;
    rig.obj.GetInlet(1)->SetInt(104, YSE::T_GUI);
    rig.obj.GetInlet(1)->SetInt(101, YSE::T_GUI);
    rig.obj.GetInlet(1)->SetList("108 108 111", YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
    CHECK(rig.obj.Contents() == "hello");
    CHECK(rig.obj.Count() == 5);

    rig.obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.listValue == "hello");
  }

  TEST_CASE("itoa: inlet 2 replaces the contents, and empty is Max's clear (#493)") {
    Rig<gItoa> rig;
    rig.obj.GetInlet(2)->SetList("104 105", YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
    CHECK(rig.obj.Contents() == "hi");

    rig.obj.GetInlet(2)->SetList("", YSE::T_GUI);
    CHECK(rig.obj.Contents().empty());
    rig.obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
  }

  // ─── limits: refused whole, never half-applied ──────────────────────────────

  TEST_CASE("atoi: exactly the maximum number of codes is converted (#493)") {
    Rig<gAtoi> rig;
    rig.obj.GetInlet(0)->SetList(std::string(gAtoi::MAX_CODES, 'a'), YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(CodeCount(rig.sink.listValue) == gAtoi::MAX_CODES);
  }

  TEST_CASE("atoi: input past the maximum is refused, not truncated (#493)") {
    Rig<gAtoi> rig;
    rig.obj.GetInlet(0)->SetList("hi", YSE::T_GUI);
    REQUIRE(rig.sink.listValue == "104 105");
    rig.sink.reset();

    rig.obj.GetInlet(0)->SetList(std::string(gAtoi::MAX_CODES + 1, 'a'), YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
    // Nothing half-built where the next reader of the object's state would find
    // it, and the previous contents survive.
    CHECK(rig.obj.Contents() == "104 105");

    // And the object still works afterwards.
    rig.obj.GetInlet(0)->SetList("hi", YSE::T_GUI);
    CHECK(rig.sink.listValue == "104 105");
  }

  TEST_CASE("atoi: an append that would overflow leaves the contents untouched (#493)") {
    // The case that matters most for the accumulator: a patch adding a piece at
    // a time cannot tell a dropped piece from a delivered one, so a refused
    // append must not silently take part of the message either.
    Rig<gAtoi> rig;
    rig.obj.GetInlet(0)->SetList(std::string(gAtoi::MAX_CODES - 2, 'a'), YSE::T_GUI);
    const std::string before = rig.obj.Contents();
    REQUIRE(rig.obj.Count() == gAtoi::MAX_CODES - 2);

    rig.obj.GetInlet(1)->SetList("abc", YSE::T_GUI);
    CHECK(rig.obj.Contents() == before);
    CHECK(rig.obj.Count() == gAtoi::MAX_CODES - 2);

    // Two more still fit exactly.
    rig.obj.GetInlet(1)->SetList("ab", YSE::T_GUI);
    CHECK(rig.obj.Count() == gAtoi::MAX_CODES);
  }

  TEST_CASE("itoa: more than the maximum number of codes is refused (#493)") {
    // Max's own limit: "up to 256 integer character codes."
    Rig<gItoa> rig;
    std::string codes;
    for (std::size_t i = 0; i < gItoa::MAX_CODES; i++) {
      if (i) codes.push_back(' ');
      codes.append("97");
    }
    rig.obj.GetInlet(0)->SetList(codes, YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.obj.Count() == gItoa::MAX_CODES);

    rig.sink.reset();
    rig.obj.GetInlet(0)->SetList(codes + " 98", YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
    CHECK(rig.obj.Count() == gItoa::MAX_CODES);
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("atoi/itoa: survive a DumpJSON / ParseJSON round trip (#493)") {
    // Neither object takes a creation argument — Max gives them none — so what
    // has to round trip is the object itself, with its three inlets and its
    // outlet intact and no parameters invented along the way.
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_ATOI) != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::G_ITOA) != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".atoi") != std::string::npos);
    CHECK(json.find(".itoa") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 2);

    for (int i = 0; i < 2; i++) {
      YSE::pHandle* copy = loaded.GetHandleFromList(i);
      REQUIRE(copy != nullptr);
      const std::string type = copy->Type();
      CHECK((type == ".atoi" || type == ".itoa"));
      CHECK(copy->GetParams().empty());
      CHECK(copy->GetInputs() == 3);
      CHECK(copy->GetOutputs() == 1);
    }
  }

  // ─── end to end, through a real patcher graph ───────────────────────────────

  TEST_CASE("atoi into itoa returns the message that went in (#493)") {
    // The round trip the pair exists for, run through the real thing. A unit
    // test on either object alone cannot show this: it is a property of the two
    // agreeing about what a character is, and it is exactly what would break if
    // one of them grew its own copy of the encoding.
    //
    // Sinks before the patcher: the patcher is torn down first, while the inlets
    // it is wired to still exist.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* atoi = p.CreateObject(YSE::OBJ::G_ATOI);
    YSE::pHandle* itoa = p.CreateObject(YSE::OBJ::G_ITOA);
    REQUIRE(atoi != nullptr);
    REQUIRE(itoa != nullptr);
    p.Connect(atoi, 0, itoa, 0);
    p.Connect(itoa, 0, &sinkHandle, 0);

    const std::string samples[] = {"hi", "a b", "voice/3/freq", "123",
                                   std::string("caf") + "\xC3\xA9"};
    for (const auto& sample : samples) {
      sink.reset();
      atoi->SetListData(0, sample);
      CHECK(sink.gotList);
      CHECK(sink.listValue == sample);
    }
  }

  TEST_CASE("itoa into atoi returns the codes that went in (#493)") {
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* itoa = p.CreateObject(YSE::OBJ::G_ITOA);
    YSE::pHandle* atoi = p.CreateObject(YSE::OBJ::G_ATOI);
    REQUIRE(itoa != nullptr);
    REQUIRE(atoi != nullptr);
    p.Connect(itoa, 0, atoi, 0);
    p.Connect(atoi, 0, &sinkHandle, 0);

    itoa->SetListData(0, "104 105");
    CHECK(sink.listValue == "104 105");

    sink.reset();
    itoa->SetListData(0, "233 8364 128512");
    CHECK(sink.listValue == "233 8364 128512");
  }

  TEST_CASE("itoa assembles a name a character at a time and a .route reads it (#493)") {
    // The issue's use case driven the way a patch drives it: codes arrive one at
    // a time on inlet 1, a bang sends the finished symbol, and a .route
    // downstream matches it as one element — which is the proof that what came
    // out really is a single token and not text that merely looks like one.
    MultiSink matched;
    MultiSink rejected;
    YSE::pHandle matchedHandle(&matched);
    YSE::pHandle rejectedHandle(&rejected);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* itoa = p.CreateObject(YSE::OBJ::G_ITOA);
    YSE::pHandle* route = p.CreateObject(YSE::OBJ::G_ROUTE, "hi");
    REQUIRE(itoa != nullptr);
    REQUIRE(route != nullptr);
    p.Connect(itoa, 0, route, 0);
    p.Connect(route, 0, &matchedHandle, 0);
    p.Connect(route, 1, &rejectedHandle, 0);

    itoa->SetIntData(1, 104);
    itoa->SetIntData(1, 105);
    CHECK_FALSE(matched.gotBang);
    CHECK_FALSE(matched.gotList);

    itoa->SetBang(0);
    // .route strips the token it matched, leaving nothing — which it reports as
    // a bang.
    CHECK(matched.gotBang);
    CHECK_FALSE(rejected.gotList);
  }

  TEST_CASE("atoi feeds arithmetic and itoa turns the answer back into text (#493)") {
    // The full loop out of the text model and back into it, which is the
    // direction nothing in the message family could go before .itoa: shift
    // every character code by one and read the result as a word again.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* atoi = p.CreateObject(YSE::OBJ::G_ATOI);
    YSE::pHandle* shift = p.CreateObject(YSE::OBJ::G_VEXPR, "$i1 + 1");
    YSE::pHandle* itoa = p.CreateObject(YSE::OBJ::G_ITOA);
    REQUIRE(atoi != nullptr);
    REQUIRE(shift != nullptr);
    REQUIRE(itoa != nullptr);
    p.Connect(atoi, 0, shift, 0);
    p.Connect(shift, 0, itoa, 0);
    p.Connect(itoa, 0, &sinkHandle, 0);

    atoi->SetListData(0, "hal");
    CHECK(sink.gotList);
    CHECK(sink.listValue == "ibm");
  }
}
