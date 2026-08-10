// Tests for .spell (issue #492) — spell a message out as the character codes of
// its text.
//
// Max documents four rules, one per atom type (int digit by digit, symbol
// character by character, a list with a space 32 between its items, anything
// likewise). This patcher has no atom types — a message is text — so those four
// collapse into one rule, and the tests are written to pin that the collapse is
// *faithful* rather than merely convenient: each of Max's four cases gets its
// own case here and has to come out with Max's answer.
//
// The other two things worth pinning are the ones an encoding-blind
// implementation gets silently wrong:
//
//   - **what a character is.** A patcher message is bytes. A well-formed UTF-8
//     sequence has to spell *one* code point, and a byte that is not part of one
//     has to spell its own value rather than being dropped or guessed at.
//   - **an over-long input is refused, not truncated**, since half a spelling is
//     a different word rather than a shorter one.
//
// No audio device and no engine of its own, except where a real patcher graph is
// the point.

#include <doctest/doctest.h>
#include <string>

#include "patcher/genericObjects/gSpell.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::gSpell;

namespace {

  // A standalone object with a sink on its outlet. Standalone on purpose: this
  // object needs no patcher at all, and a test that needed one could not tell a
  // refused message from a message the patcher never delivered.
  struct Rig {
    MultiSink sink;

    void Wire(gSpell& obj) {
      TestHelpers::Wire(obj, 0, sink);
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

  TEST_CASE("spell: registered, one inlet and one outlet (#492)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::G_SPELL);
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == ".spell");
    CHECK(obj->GetInputs() == 1);
    CHECK(obj->GetOutputs() == 1);
  }

  TEST_CASE("spell: appears in the registry's name list (#492)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_SPELL)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("spell: the inlet takes int, float and list but not bang (#492)") {
    // Max's spell has no bang method and there is nothing for one to spell, so
    // no handler is registered — the .spray / .combine discipline, which keeps
    // GetAcceptedTypes() reporting the object's real contract.
    gSpell obj;
    const unsigned int accepted = obj.GetInlet(0)->GetAcceptedTypes();
    CHECK((accepted & YSE::PATCHER::IT_INT) != 0);
    CHECK((accepted & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((accepted & YSE::PATCHER::IT_LIST) != 0);
    CHECK((accepted & YSE::PATCHER::IT_BANG) == 0);
  }

  TEST_CASE("spell: defaults are no minimum and a fill of 32 (#492)") {
    gSpell obj;
    CHECK(obj.MinimumSize() == 0);
    CHECK(obj.FillCode() == gSpell::DEFAULT_FILL);
  }

  // ─── Max's four rules, which are one rule here ──────────────────────────────

  TEST_CASE("spell: a symbol spells character by character (#492)") {
    // Max: "the ASCII value of each letter, digit, or other character in the
    // symbol is sent out the outlet, one character at a time."
    Rig rig;
    gSpell obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("hi", YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "104 105");
  }

  TEST_CASE("spell: an int spells digit by digit (#492)") {
    // Max: "the ASCII value of each of the digits of the number is sent out the
    // outlet, one digit at a time." The digits are the number's text, which is
    // why this needs no rule of its own here.
    Rig rig;
    gSpell obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetInt(123, YSE::T_GUI);
    CHECK(rig.sink.listValue == "49 50 51");
  }

  TEST_CASE("spell: a negative int spells its minus sign too (#492)") {
    Rig rig;
    gSpell obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetInt(-12, YSE::T_GUI);
    CHECK(rig.sink.listValue == "45 49 50");
  }

  TEST_CASE("spell: a float spells its decimal point (#492)") {
    // The same rule applied to the text a float actually has — and the reason
    // the object formats through ExprFormatValue rather than inventing a
    // spelling of its own.
    Rig rig;
    gSpell obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetFloat(2.5f, YSE::T_GUI);
    CHECK(rig.sink.listValue == "50 46 53");
  }

  TEST_CASE("spell: a list gets Max's space, 32, between its items (#492)") {
    // Max: "each int in the list is converted to ASCII as described above, and a
    // space character (32) is sent out between items in the list." Here that is
    // not a rule of its own: a space is the character that is actually there.
    Rig rig;
    gSpell obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("1 2", YSE::T_GUI);
    CHECK(rig.sink.listValue == "49 32 50");
  }

  TEST_CASE("spell: a message beginning with a symbol spells all of its items (#492)") {
    // Max's `anything` case, which needs no separate treatment because nothing
    // here distinguishes it from a list in the first place.
    Rig rig;
    gSpell obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("a 1", YSE::T_GUI);
    CHECK(rig.sink.listValue == "97 32 49");
  }

  TEST_CASE("spell: whitespace runs collapse and the ends are trimmed (#492)") {
    // Max's spacing for a list is a single 32 between items, so incidental
    // whitespace from the sender must not change the answer — the same
    // normalisation .tosymbol and .combine perform.
    Rig rig;
    gSpell obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("  a   b  ", YSE::T_GUI);
    CHECK(rig.sink.listValue == "97 32 98");
  }

  TEST_CASE("spell: an empty message with no minimum sends nothing (#492)") {
    // Inert rather than a source of empty messages — the .prepend / .sprintf /
    // .combine rule that makes an unconfigured object safe to drop into a
    // working patch.
    Rig rig;
    gSpell obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("", YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
    obj.GetInlet(0)->SetList("   ", YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
  }

  TEST_CASE("spell: Calculate() sends nothing (#492)") {
    // The rule the whole message family keeps: the object is driven by its
    // inlet, and one that emitted from Calculate() would re-spell on every DSP
    // tick after the inlet fired.
    Rig rig;
    gSpell obj;
    rig.Wire(obj);

    obj.Calculate(YSE::T_DSP);
    CHECK_FALSE(rig.sink.gotList);
  }

  // ─── the encoding, which is the part that can be silently wrong ─────────────

  TEST_CASE("spell: a well-formed UTF-8 sequence spells one code point (#492)") {
    // The case an encoding-blind implementation gets wrong without ever
    // failing: it would emit two codes for one visible character. Written as
    // explicit bytes so the test does not depend on the source file's encoding.
    Rig rig;
    gSpell obj;
    rig.Wire(obj);

    // U+00E9 LATIN SMALL LETTER E WITH ACUTE, two bytes.
    obj.GetInlet(0)->SetList(std::string("\xC3\xA9"), YSE::T_GUI);
    CHECK(rig.sink.listValue == "233");

    // U+20AC EURO SIGN, three bytes.
    obj.GetInlet(0)->SetList(std::string("\xE2\x82\xAC"), YSE::T_GUI);
    CHECK(rig.sink.listValue == "8364");

    // U+1F600 GRINNING FACE, four bytes.
    obj.GetInlet(0)->SetList(std::string("\xF0\x9F\x98\x80"), YSE::T_GUI);
    CHECK(rig.sink.listValue == "128512");
  }

  TEST_CASE("spell: ASCII and multi-byte characters mix in one message (#492)") {
    Rig rig;
    gSpell obj;
    rig.Wire(obj);

    // "a" + U+00E9 + "b" — the hex escapes are kept in their own literals so
    // the compiler cannot swallow the following letter as another hex digit.
    obj.GetInlet(0)->SetList(std::string("a") + "\xC3\xA9" + "b", YSE::T_GUI);
    CHECK(rig.sink.listValue == "97 233 98");
  }

  TEST_CASE("spell: a byte that is not well-formed UTF-8 spells its own value (#492)") {
    // The stated fallback. It matters that it is stated: a patch fed Latin-1 or
    // raw binary must get one code per byte rather than lose characters, and
    // decoding has to resume at the next byte rather than run off.
    Rig rig;
    gSpell obj;
    rig.Wire(obj);

    // A lead byte with no continuation after it.
    obj.GetInlet(0)->SetList(std::string("\xC3"), YSE::T_GUI);
    CHECK(rig.sink.listValue == "195");

    // A continuation byte with no lead before it.
    obj.GetInlet(0)->SetList(std::string("\xA9"), YSE::T_GUI);
    CHECK(rig.sink.listValue == "169");

    // Latin-1's own e-acute, which UTF-8 would read as the lead of a three-byte
    // sequence that is not there. It spells 233 — the same code point the
    // well-formed two-byte sequence above spells, which is exactly why the
    // fallback is the useful one for a patch fed the wrong encoding.
    obj.GetInlet(0)->SetList(std::string("\xE9"), YSE::T_GUI);
    CHECK(rig.sink.listValue == "233");

    // A truncated three-byte sequence: each byte spells itself, and the 'a'
    // after it is still reached.
    obj.GetInlet(0)->SetList(std::string("\xE2\x82") + "a", YSE::T_GUI);
    CHECK(rig.sink.listValue == "226 130 97");
  }

  TEST_CASE("spell: overlong, surrogate and out-of-range sequences are not characters (#492)") {
    // Each of these decodes arithmetically to something plausible but is not
    // well-formed UTF-8, so each takes the same route as any other malformed
    // byte rather than becoming a code point that was never written.
    Rig rig;
    gSpell obj;
    rig.Wire(obj);

    // C0 AF is an overlong encoding of '/' (U+002F).
    obj.GetInlet(0)->SetList(std::string("\xC0\xAF"), YSE::T_GUI);
    CHECK(rig.sink.listValue == "192 175");

    // ED A0 80 spells the surrogate half U+D800, which is not a character.
    obj.GetInlet(0)->SetList(std::string("\xED\xA0\x80"), YSE::T_GUI);
    CHECK(rig.sink.listValue == "237 160 128");

    // F5 80 80 80 would be U+140000, above the highest code point.
    obj.GetInlet(0)->SetList(std::string("\xF5\x80\x80\x80"), YSE::T_GUI);
    CHECK(rig.sink.listValue == "245 128 128 128");
  }

  // ─── size and fill ──────────────────────────────────────────────────────────

  TEST_CASE("spell: size pads short output with the default 32 (#492)") {
    // Max: "any input that doesn't 'spell' to the minimum length is followed by
    // enough fill characters."
    Rig rig;
    gSpell obj;
    obj.SetParams("5");
    rig.Wire(obj);

    CHECK(obj.MinimumSize() == 5);
    obj.GetInlet(0)->SetList("hi", YSE::T_GUI);
    CHECK(rig.sink.listValue == "104 105 32 32 32");
  }

  TEST_CASE("spell: fill names the code padded with (#492)") {
    Rig rig;
    gSpell obj;
    obj.SetParams("5 48");
    rig.Wire(obj);

    CHECK(obj.FillCode() == 48);
    obj.GetInlet(0)->SetList("hi", YSE::T_GUI);
    CHECK(rig.sink.listValue == "104 105 48 48 48");
  }

  TEST_CASE("spell: a fill of 0 is the code 0, not Max's default escape (#492)") {
    // Max cannot tell an absent second argument from one that is 0, which is the
    // whole reason it has the "use any negative number" workaround. Arguments
    // are read as tokens here, so 0 means 0.
    Rig rig;
    gSpell obj;
    obj.SetParams("3 0");
    rig.Wire(obj);

    CHECK(obj.FillCode() == 0);
    obj.GetInlet(0)->SetList("a", YSE::T_GUI);
    CHECK(rig.sink.listValue == "97 0 0");
  }

  TEST_CASE("spell: a negative fill is refused and the default kept (#492)") {
    // Deliberately *not* Max's 48: turning -1 into the character '0' silently
    // would be a trap for anyone who wrote it meaning something else, and the
    // ambiguity that forced Max's hand does not exist here.
    Rig rig;
    gSpell obj;
    obj.SetParams("3 -1");
    rig.Wire(obj);

    CHECK(obj.FillCode() == gSpell::DEFAULT_FILL);
    obj.GetInlet(0)->SetList("a", YSE::T_GUI);
    CHECK(rig.sink.listValue == "97 32 32");
  }

  TEST_CASE("spell: a fill above the highest code point is refused (#492)") {
    gSpell obj;
    obj.SetParams("3 1114112");
    CHECK(obj.FillCode() == gSpell::DEFAULT_FILL);
  }

  TEST_CASE("spell: a negative size means no minimum (#492)") {
    gSpell obj;
    obj.SetParams("-4");
    CHECK(obj.MinimumSize() == 0);
  }

  TEST_CASE("spell: size is clamped to the most codes the object will send (#492)") {
    gSpell obj;
    obj.SetParams("400");
    CHECK(obj.MinimumSize() == (int)gSpell::MAX_CODES);
  }

  TEST_CASE("spell: an empty message still pads to the minimum (#492)") {
    // The two rules meeting: there is nothing to spell, but there is still a
    // minimum length the record has to reach, so the object is only inert when
    // it has nothing at all to send.
    Rig rig;
    gSpell obj;
    obj.SetParams("3");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("", YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "32 32 32");
  }

  TEST_CASE("spell: output already at the minimum is not padded (#492)") {
    Rig rig;
    gSpell obj;
    obj.SetParams("2");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("hi", YSE::T_GUI);
    CHECK(rig.sink.listValue == "104 105");

    obj.GetInlet(0)->SetList("hey", YSE::T_GUI);
    CHECK(rig.sink.listValue == "104 101 121");
  }

  // ─── limits ─────────────────────────────────────────────────────────────────

  TEST_CASE("spell: exactly the maximum number of codes is sent (#492)") {
    Rig rig;
    gSpell obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetList(std::string(gSpell::MAX_CODES, 'a'), YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(CodeCount(rig.sink.listValue) == gSpell::MAX_CODES);
  }

  TEST_CASE("spell: input that would spell past the maximum is refused, not truncated (#492)") {
    // Half a spelling is a different word rather than a shorter one, and a patch
    // reassembling text from codes downstream would have no way to tell the two
    // apart. Silent, because this may be the audio thread.
    Rig rig;
    gSpell obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetList(std::string(gSpell::MAX_CODES + 1, 'a'), YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
    CHECK(obj.LastOutput().empty());
  }

  TEST_CASE("spell: a refusal is not a partial send, and does not break the next one (#492)") {
    // Nothing goes out, nothing half-built is left where the next reader of the
    // object's state would find it, and the object still works afterwards.
    Rig rig;
    gSpell obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("hi", YSE::T_GUI);
    CHECK(rig.sink.listValue == "104 105");

    rig.sink.reset();
    obj.GetInlet(0)->SetList(std::string(gSpell::MAX_CODES + 20, 'a'), YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);

    // And the object still works afterwards.
    obj.GetInlet(0)->SetList("hi", YSE::T_GUI);
    CHECK(rig.sink.listValue == "104 105");
  }

  // ─── re-configuration ───────────────────────────────────────────────────────

  TEST_CASE("spell: SetParams(\"\") returns the object to its defaults (#492)") {
    // Parameters::Set returns without calling the parse callback for an empty
    // argument string, so the clear callback is the whole of the reset.
    gSpell obj;
    obj.SetParams("8 45");
    REQUIRE(obj.MinimumSize() == 8);
    REQUIRE(obj.FillCode() == 45);

    obj.SetParams("");
    CHECK(obj.MinimumSize() == 0);
    CHECK(obj.FillCode() == gSpell::DEFAULT_FILL);
  }

  TEST_CASE("spell: a shorter argument list does not inherit the previous fill (#492)") {
    // Parameters::Set only writes the arguments it is given, so without the
    // clear callback the fill of 45 would survive an argument list that never
    // mentions it.
    gSpell obj;
    obj.SetParams("8 45");
    obj.SetParams("4");
    CHECK(obj.MinimumSize() == 4);
    CHECK(obj.FillCode() == gSpell::DEFAULT_FILL);
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("spell: params survive a DumpJSON / ParseJSON round trip (#492)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_SPELL, "8 48") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".spell") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".spell");
    CHECK(copy->GetParams() == std::string("8 48"));
    CHECK(copy->GetInputs() == 1);
    CHECK(copy->GetOutputs() == 1);
  }

  // ─── end to end, through a real patcher graph ───────────────────────────────

  TEST_CASE("spell: the codes are numbers a .vexpr computes with, element by element (#492)") {
    // The issue's use case run through the real thing — "so text can be
    // processed numerically inside the patcher" — and the strongest available
    // proof that the result is a genuine multi-element numeric list rather than
    // text that merely looks like one: .vexpr evaluates its expression once per
    // *element*, which is impossible unless the elements really are separate
    // numbers. Asserting on the string "104 105" inside a unit test cannot tell
    // that from a single token that happens to contain a space, and that is
    // exactly the distinction every reader in this patcher makes.
    //
    // Sinks before the patcher: the patcher is torn down first, while the inlets
    // it is wired to still exist.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* spell = p.CreateObject(YSE::OBJ::G_SPELL);
    // 97 is 'a', so this turns each letter into its position in the alphabet.
    YSE::pHandle* letters = p.CreateObject(YSE::OBJ::G_VEXPR, "$i1 - 96");
    REQUIRE(spell != nullptr);
    REQUIRE(letters != nullptr);

    p.Connect(spell, 0, letters, 0);
    p.Connect(letters, 0, &sinkHandle, 0);

    spell->SetListData(0, "hi");
    CHECK(sink.gotList);
    CHECK(sink.listValue == "8 9");

    sink.reset();
    spell->SetListData(0, "cab");
    CHECK(sink.gotList);
    CHECK(sink.listValue == "3 1 2");
  }

  TEST_CASE("spell: size and fill make fixed-width records through the graph (#492)") {
    // What Max's two arguments are for, driven the way a patch drives them: every
    // message leaves the object the same length whatever went in, so a reader
    // downstream can rely on the count.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* spell = p.CreateObject(YSE::OBJ::G_SPELL, "6 45");
    REQUIRE(spell != nullptr);
    p.Connect(spell, 0, &sinkHandle, 0);

    spell->SetListData(0, "hi");
    CHECK(sink.gotList);
    CHECK(sink.listValue == "104 105 45 45 45 45");

    sink.reset();
    spell->SetIntData(0, 42);
    CHECK(sink.listValue == "52 50 45 45 45 45");
  }

  TEST_CASE("spell: a .tosymbol feeding a .spell spells the collapsed name (#492)") {
    // The family working together, which is the chain the issue's second use
    // case describes: a name is assembled out of parts, collapsed into one
    // token, and then turned into numbers a generative mapping can do
    // arithmetic on. The separator is spelled too, because by then it is simply
    // one of the characters of the name.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* to = p.CreateObject(YSE::OBJ::G_TOSYMBOL, "/");
    YSE::pHandle* spell = p.CreateObject(YSE::OBJ::G_SPELL);
    REQUIRE(to != nullptr);
    REQUIRE(spell != nullptr);

    p.Connect(to, 0, spell, 0);
    p.Connect(spell, 0, &sinkHandle, 0);

    to->SetListData(0, "a b");
    CHECK(sink.gotList);
    // "a/b" — 97, 47, 98. The 47 is the separator, and the absence of a 32 is
    // the evidence that .tosymbol really did collapse the two tokens into one.
    CHECK(sink.listValue == "97 47 98");
  }
}
