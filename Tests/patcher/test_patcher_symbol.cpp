// Tests for .tosymbol and .fromsymbol (issue #490) — the inverse pair that
// collapses a message into a single symbol and expands it back.
//
// They share one body (gSymbolBase) and differ only in direction, so the cases
// below are mostly written as mirror pairs. What the pair has to get right:
//
//   - **"symbol" means something honest here.** Max has atom types and this
//     patcher does not: a message is text, and every reader splits it on
//     whitespace. So a symbol is a single whitespace-free token, and the tests
//     assert exactly that — that .tosymbol with a real separator produces one
//     token a .route reads as one element, and that the *default* separator (a
//     space, Max's own) is left honestly doing nothing to the token structure
//     rather than papered over with invented quoting.
//   - **the type conversion is the other half.** A bang, an int and a float
//     collapse to the text that spells them, and .fromsymbol restores the type.
//     That half needs no separator, and it is what makes a computed value
//     usable as a name at all.
//   - **the round trip is lossless with a matching separator on both ends.**
//     That is the use case the issue names, and the end-to-end case at the
//     bottom of this file runs it through a real patcher graph.
//   - **inlet 0 has no reserved words in it.** The separator gets its own
//     inlet, the .prepend / .forward / .substitute discipline, and it matters
//     here because these objects exist to carry arbitrary words.
//   - **an over-long separator is refused, not truncated.** Half a separator is
//     a different separator, and the two ends of a round trip must agree.
//
// No audio device and no engine of its own, except where a real patcher graph
// is the point.

#include <doctest/doctest.h>
#include <string>

#include "patcher/genericObjects/gSymbol.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::gFromSymbol;
using YSE::PATCHER::gSymbolBase;
using YSE::PATCHER::gToSymbol;

namespace {

  // A standalone object with a sink on its outlet. Standalone on purpose: these
  // two need no patcher at all, and a test that needed one could not tell a
  // dropped message from a message the patcher never delivered.
  struct Rig {
    MultiSink sink;

    void Wire(gSymbolBase& obj) {
      obj.ConnectOutlet(sink.GetInlet(0), 0);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("symbol: both objects are registered, with two inlets and one outlet (#490)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* to = p.CreateObject(YSE::OBJ::G_TOSYMBOL);
    REQUIRE(to != nullptr);
    CHECK(std::string(to->Type()) == ".tosymbol");
    CHECK(to->GetInputs() == 2);
    CHECK(to->GetOutputs() == 1);

    YSE::pHandle* from = p.CreateObject(YSE::OBJ::G_FROMSYMBOL);
    REQUIRE(from != nullptr);
    CHECK(std::string(from->Type()) == ".fromsymbol");
    CHECK(from->GetInputs() == 2);
    CHECK(from->GetOutputs() == 1);
  }

  TEST_CASE("symbol: both appear in the registry's name list (#490)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool foundTo = false;
    bool foundFrom = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_TOSYMBOL)) foundTo = true;
      if (name == std::string(YSE::OBJ::G_FROMSYMBOL)) foundFrom = true;
    }
    CHECK(foundTo);
    CHECK(foundFrom);
  }

  TEST_CASE("symbol: inlet 0 takes everything, the separator inlet takes all but bang (#490)") {
    // The separator inlet declines a bang because there is nothing for it to do
    // — the .prepend / .substitute discipline of not registering an empty
    // handler, which keeps GetAcceptedTypes() reporting the real contract.
    gToSymbol obj;
    const unsigned int data = obj.GetInlet(0)->GetAcceptedTypes();
    CHECK((data & YSE::PATCHER::IT_BANG) != 0);
    CHECK((data & YSE::PATCHER::IT_INT) != 0);
    CHECK((data & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((data & YSE::PATCHER::IT_LIST) != 0);

    const unsigned int sep = obj.GetInlet(1)->GetAcceptedTypes();
    CHECK((sep & YSE::PATCHER::IT_BANG) == 0);
    CHECK((sep & YSE::PATCHER::IT_INT) != 0);
    CHECK((sep & YSE::PATCHER::IT_FLOAT) != 0);
    CHECK((sep & YSE::PATCHER::IT_LIST) != 0);
  }

  TEST_CASE("symbol: the default separator is a single space, Max's own (#490)") {
    gToSymbol to;
    gFromSymbol from;
    CHECK(to.Separator() == " ");
    CHECK(from.Separator() == " ");
  }

  // ─── .tosymbol: the token collapse ──────────────────────────────────────────

  TEST_CASE("tosymbol: a separator joins the tokens into one token (#490)") {
    // The whole point of the object in this message model: with a
    // non-whitespace separator the result contains no whitespace at all, so
    // every reader in the patcher sees exactly one element.
    Rig rig;
    gToSymbol obj;
    obj.SetParams("/");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("voice 3 freq", YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "voice/3/freq");
    CHECK(rig.sink.listValue.find(' ') == std::string::npos);
  }

  TEST_CASE("tosymbol: the default separator normalises whitespace and nothing more (#490)") {
    // Max's default is a space, and a space cannot make one token here because
    // whitespace is what separates tokens. Left honest: runs collapse and the
    // ends are trimmed — no quoting is invented, because nothing downstream in
    // this patcher reads a quote as anything but a character.
    Rig rig;
    gToSymbol obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("  voice   3\tfreq  ", YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "voice 3 freq");
  }

  TEST_CASE("tosymbol: a multi-character separator is joined whole (#490)") {
    Rig rig;
    gToSymbol obj;
    obj.SetParams("::");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("a b c", YSE::T_GUI);
    CHECK(rig.sink.listValue == "a::b::c");
  }

  // ─── .tosymbol: the type collapse ───────────────────────────────────────────

  TEST_CASE("tosymbol: a bang becomes the symbol bang (#490)") {
    Rig rig;
    gToSymbol obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "bang");
    CHECK_FALSE(rig.sink.gotBang);
  }

  TEST_CASE("tosymbol: an int leaves as the text that spells it, not as an int (#490)") {
    // The half of the conversion that needs no separator, and the one that
    // makes a computed value usable as a name.
    Rig rig;
    gToSymbol obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetInt(60, YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "60");
    CHECK_FALSE(rig.sink.gotInt);
  }

  TEST_CASE("tosymbol: a float keeps its decimal point (#490)") {
    Rig rig;
    gToSymbol obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetFloat(1.5f, YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "1.5");
    CHECK_FALSE(rig.sink.gotFloat);
  }

  // ─── .fromsymbol: the split ─────────────────────────────────────────────────

  TEST_CASE("fromsymbol: a symbol splits back into a message (#490)") {
    Rig rig;
    gFromSymbol obj;
    obj.SetParams("/");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("voice/3/freq", YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "voice 3 freq");
  }

  TEST_CASE("fromsymbol: empty pieces are dropped rather than emitted (#490)") {
    // An empty token would read back as a doubled separator, and no .route
    // matches one — the hazard .prepend trims its stored message for.
    Rig rig;
    gFromSymbol obj;
    obj.SetParams("/");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("/a//b/", YSE::T_GUI);
    CHECK(rig.sink.listValue == "a b");
  }

  TEST_CASE("fromsymbol: whitespace already in the message separates pieces too (#490)") {
    Rig rig;
    gFromSymbol obj;
    obj.SetParams("/");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("a/b c/d", YSE::T_GUI);
    CHECK(rig.sink.listValue == "a b c d");
  }

  // ─── .fromsymbol: the type restoration ──────────────────────────────────────

  TEST_CASE("fromsymbol: a single numeric token comes back as an int (#490)") {
    Rig rig;
    gFromSymbol obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("60", YSE::T_GUI);
    CHECK(rig.sink.gotInt);
    CHECK(rig.sink.intValue == 60);
    CHECK_FALSE(rig.sink.gotList);
  }

  TEST_CASE("fromsymbol: a token spelled as a float comes back as a float (#490)") {
    // .sel's and .match's test for how a number was written, so an int does not
    // come back wearing a decimal point it never had.
    Rig rig;
    gFromSymbol obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("1.5", YSE::T_GUI);
    CHECK(rig.sink.gotFloat);
    CHECK(rig.sink.floatValue == doctest::Approx(1.5f));
    CHECK_FALSE(rig.sink.gotInt);
  }

  TEST_CASE("fromsymbol: the word bang comes back as a bang (#490)") {
    // Max: "the word bang sent as a part of a symbol will be converted to a
    // message" — and the exact inverse of what .tosymbol does to a bang.
    Rig rig;
    gFromSymbol obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("bang", YSE::T_GUI);
    CHECK(rig.sink.gotBang);
    CHECK_FALSE(rig.sink.gotList);
  }

  TEST_CASE("fromsymbol: a non-numeric token stays the symbol it was typed as (#490)") {
    Rig rig;
    gFromSymbol obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("note", YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "note");
  }

  TEST_CASE("fromsymbol: only a single-token result gets a type back (#490)") {
    // A type is a property of the whole message here, so `bang/5` is the
    // two-token list `bang 5`, not a list with a bang atom in it — there is no
    // such thing in this patcher, and pretending otherwise is the fake the
    // whole port is written to avoid.
    Rig rig;
    gFromSymbol obj;
    obj.SetParams("/");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("bang/5", YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "bang 5");
    CHECK_FALSE(rig.sink.gotBang);
  }

  TEST_CASE("fromsymbol: an int, a float and a bang pass straight through (#490)") {
    // Max: "any integer will simply pass through to the output" — each is
    // already what a symbol would have been expanded into.
    Rig rig;
    gFromSymbol obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetInt(7, YSE::T_GUI);
    CHECK(rig.sink.gotInt);
    CHECK(rig.sink.intValue == 7);

    rig.sink.reset();
    obj.GetInlet(0)->SetFloat(2.25f, YSE::T_GUI);
    CHECK(rig.sink.gotFloat);
    CHECK(rig.sink.floatValue == doctest::Approx(2.25f));

    rig.sink.reset();
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.sink.gotBang);
    CHECK_FALSE(rig.sink.gotList);
  }

  // ─── the separator inlet ────────────────────────────────────────────────────

  TEST_CASE("symbol: setting the separator emits nothing (#490)") {
    Rig rig;
    gToSymbol obj;
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("/", YSE::T_GUI);
    CHECK_FALSE(rig.sink.gotList);
    CHECK_FALSE(rig.sink.gotBang);
    CHECK(obj.Separator() == "/");

    // ... and it does not rewrite the creation argument: re-aiming an object is
    // a message, and a message must not change what a saved patch contains.
    CHECK(obj.GetParams().empty());
  }

  TEST_CASE("symbol: inlet 0 has no reserved words in it (#490)") {
    // Max spells the change as an @separator attribute; here that would cost
    // the object the very words it exists to carry.
    Rig rig;
    gToSymbol obj;
    obj.SetParams("/");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("separator x", YSE::T_GUI);
    CHECK(rig.sink.listValue == "separator/x");
    CHECK(obj.Separator() == "/");
  }

  TEST_CASE("symbol: an empty message empties the separator (#490)") {
    // Max's `separator` with no arguments: "removes all spaces (e.g. 1 2 3 4
    // becomes 1234)".
    Rig rig;
    gToSymbol obj;
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("", YSE::T_GUI);
    CHECK(obj.Separator().empty());

    obj.GetInlet(0)->SetList("1 2 3 4", YSE::T_GUI);
    CHECK(rig.sink.listValue == "1234");
  }

  TEST_CASE("fromsymbol: an empty separator splits into individual characters (#490)") {
    // The exact inverse of joining with nothing, and Max's own phrasing for
    // what fromsymbol does.
    Rig rig;
    gFromSymbol obj;
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("", YSE::T_GUI);
    REQUIRE(obj.Separator().empty());

    obj.GetInlet(0)->SetList("1234", YSE::T_GUI);
    CHECK(rig.sink.gotList);
    CHECK(rig.sink.listValue == "1 2 3 4");
  }

  TEST_CASE("symbol: an int or a float on the separator inlet is spelled out (#490)") {
    gToSymbol obj;
    obj.GetInlet(1)->SetInt(7, YSE::T_GUI);
    CHECK(obj.Separator() == "7");
    obj.GetInlet(1)->SetFloat(1.5f, YSE::T_GUI);
    CHECK(obj.Separator() == "1.5");
  }

  TEST_CASE("symbol: an over-long separator is refused, not truncated (#490)") {
    // Half a separator is a different separator, and the two ends of a round
    // trip have to agree on it exactly.
    gToSymbol obj;
    obj.GetInlet(1)->SetList("/", YSE::T_GUI);
    REQUIRE(obj.Separator() == "/");

    obj.GetInlet(1)->SetList(std::string(17, 'x'), YSE::T_GUI);
    CHECK(obj.Separator() == "/");

    // Exactly at the cap is accepted.
    obj.GetInlet(1)->SetList(std::string(16, 'x'), YSE::T_GUI);
    CHECK(obj.Separator() == std::string(16, 'x'));
  }

  TEST_CASE("symbol: an over-long creation argument leaves the default standing (#490)") {
    gToSymbol obj;
    obj.SetParams(std::string(17, 'x'));
    CHECK(obj.Separator() == " ");
  }

  // ─── parameters ─────────────────────────────────────────────────────────────

  TEST_CASE("symbol: SetParams(\"\") restores the default separator (#490)") {
    gFromSymbol obj;
    obj.SetParams("/");
    REQUIRE(obj.Separator() == "/");

    obj.SetParams("");
    CHECK(obj.Separator() == " ");
  }

  TEST_CASE("symbol: the separator survives a DumpJSON / ParseJSON round trip (#490)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_TOSYMBOL, "/") != nullptr);
    REQUIRE(src.CreateObject(YSE::OBJ::G_FROMSYMBOL, "::") != nullptr);

    const std::string json = src.DumpJSON();
    CHECK(json.find(".tosymbol") != std::string::npos);
    CHECK(json.find(".fromsymbol") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 2);

    for (int i = 0; i < 2; i++) {
      YSE::pHandle* copy = loaded.GetHandleFromList(i);
      REQUIRE(copy != nullptr);
      const std::string type = copy->Type();
      CAPTURE(type);
      CHECK(copy->GetInputs() == 2);
      CHECK(copy->GetOutputs() == 1);
      if (type == ".tosymbol") {
        CHECK(copy->GetParams() == std::string("/"));
      } else {
        CHECK(type == ".fromsymbol");
        CHECK(copy->GetParams() == std::string("::"));
      }
    }
  }

  // ─── real-time discipline ───────────────────────────────────────────────────

  TEST_CASE("symbol: Calculate() emits nothing (#490)") {
    // Both objects are driven by their inlets; one that emitted on a DSP tick
    // would hand a patch a message it never sent — the rule .route, .sel and
    // .prepend establish.
    Rig toRig;
    gToSymbol to;
    toRig.Wire(to);
    to.Calculate(YSE::T_DSP);
    CHECK_FALSE(toRig.sink.gotList);
    CHECK_FALSE(toRig.sink.gotBang);

    Rig fromRig;
    gFromSymbol from;
    fromRig.Wire(from);
    from.Calculate(YSE::T_DSP);
    CHECK_FALSE(fromRig.sink.gotList);
    CHECK_FALSE(fromRig.sink.gotBang);
  }

  // ─── end to end, through a real patcher graph ───────────────────────────────

  TEST_CASE("symbol: a value round-trips through a name and comes back typed (#490)") {
    // The use case the issue names, run through the real thing: a list is
    // collapsed into one token that anything taking a *name* would accept,
    // carried as that token, and expanded back into the list it came from —
    // with the number in it arriving as a number again. Nothing short of the
    // whole chain proves that, because the two halves of the conversion (token
    // structure and message type) live at opposite ends of it.
    //
    // Sinks before the patcher: the patcher is torn down first, while the
    // inlets it is wired to still exist.
    MultiSink carried;
    MultiSink expanded;
    YSE::pHandle carriedHandle(&carried);
    YSE::pHandle expandedHandle(&expanded);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* to = p.CreateObject(YSE::OBJ::G_TOSYMBOL, "/");
    YSE::pHandle* from = p.CreateObject(YSE::OBJ::G_FROMSYMBOL, "/");
    REQUIRE(to != nullptr);
    REQUIRE(from != nullptr);

    p.Connect(to, 0, &carriedHandle, 0);
    p.Connect(to, 0, from, 0);
    p.Connect(from, 0, &expandedHandle, 0);

    to->SetListData(0, "voice 3 freq");

    // Halfway: one token, so a .forward or a .s would take the whole thing as a
    // destination name.
    CHECK(carried.gotList);
    CHECK(carried.listValue == "voice/3/freq");
    CHECK(carried.listValue.find(' ') == std::string::npos);

    // And back again, unchanged.
    CHECK(expanded.gotList);
    CHECK(expanded.listValue == "voice 3 freq");
  }

  TEST_CASE("symbol: the collapsed symbol is one element to a .route downstream (#490)") {
    // The claim the whole port rests on: "a symbol" here means "one token", and
    // the object that decides what a token is, is .route — it matches the
    // *leading* one. So the same data, collapsed, must miss a .route on the
    // first word and, expanded, must hit it. Only the real chain proves that; a
    // unit test asserting on the joined text cannot tell "voice/3/freq" from
    // "voice 3 freq" as far as any reader downstream is concerned.
    //
    // Sinks before the patcher: the patcher is torn down first, while the
    // inlets it is wired to still exist.
    MultiSink collapsedMatch;
    MultiSink collapsedReject;
    MultiSink splitMatch;
    YSE::pHandle collapsedMatchHandle(&collapsedMatch);
    YSE::pHandle collapsedRejectHandle(&collapsedReject);
    YSE::pHandle splitMatchHandle(&splitMatch);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* to = p.CreateObject(YSE::OBJ::G_TOSYMBOL, "/");
    YSE::pHandle* collapsedRoute = p.CreateObject(YSE::OBJ::G_ROUTE, "voice");
    YSE::pHandle* from = p.CreateObject(YSE::OBJ::G_FROMSYMBOL, "/");
    YSE::pHandle* splitRoute = p.CreateObject(YSE::OBJ::G_ROUTE, "voice");
    REQUIRE(to != nullptr);
    REQUIRE(collapsedRoute != nullptr);
    REQUIRE(from != nullptr);
    REQUIRE(splitRoute != nullptr);
    REQUIRE(collapsedRoute->GetOutputs() == 2);

    p.Connect(to, 0, collapsedRoute, 0);
    p.Connect(collapsedRoute, 0, &collapsedMatchHandle, 0);
    p.Connect(collapsedRoute, 1, &collapsedRejectHandle, 0);
    p.Connect(to, 0, from, 0);
    p.Connect(from, 0, splitRoute, 0);
    p.Connect(splitRoute, 0, &splitMatchHandle, 0);

    to->SetListData(0, "voice 3 freq");

    // Collapsed, the whole address is the leading token, so it is *not* the
    // word `voice` and falls out the reject outlet. That miss is the evidence
    // that the collapse produced a single element rather than merely text that
    // looks like one.
    CHECK_FALSE(collapsedMatch.gotList);
    CHECK(collapsedReject.gotList);
    CHECK(collapsedReject.listValue == "voice/3/freq");

    // Expanded again, the same data is a message whose first token is `voice`,
    // and the same .route now matches it.
    CHECK(splitMatch.gotList);
    CHECK(splitMatch.listValue == "voice 3 freq");
  }

  TEST_CASE("symbol: a computed number becomes part of a name and survives the trip (#490)") {
    // The round trip the issue asks for, with the value arriving at run time:
    // .tosymbol spells the int into the address, and .fromsymbol on the other
    // end hands it back as an int rather than as the text that spells it.
    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* to = p.CreateObject(YSE::OBJ::G_TOSYMBOL, "/");
    YSE::pHandle* from = p.CreateObject(YSE::OBJ::G_FROMSYMBOL, "/");
    REQUIRE(to != nullptr);
    REQUIRE(from != nullptr);

    p.Connect(to, 0, from, 0);
    p.Connect(from, 0, &sinkHandle, 0);

    to->SetIntData(0, 60);
    CHECK(sink.gotInt);
    CHECK(sink.intValue == 60);
    CHECK_FALSE(sink.gotList);
  }
}
