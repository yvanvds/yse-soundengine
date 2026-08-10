// Tests for `.zl` (issues #523, #524, #525, #526, #527) — the patcher's
// list-processing object, and the design gate for the whole list group.
//
// Four things are being pinned here, and only the first of them is about the
// modes themselves:
//
//   - **the modes**: `len`, `rev` and `nth` from #523, including `nth`'s two
//     outlets and the right-before-left order Max guarantees for them; and the
//     reordering group from #524 — `rot`, `scramble`, `sort`, `swap` and
//     `indexmap`, including the index map `sort` publishes, the 1-based
//     numbering that lets it be fed straight back into an `indexmap`, and the
//     seeded shuffle that makes `scramble` assertable at all; and the
//     structural group from #527 — `reg`, `iter`, `join`, `lace`, `delace`,
//     `ecils`, `group`, `stream`, `queue` and `stack`;
//   - **the one store that survives a message** (#527): the accumulator the
//     four collecting modes share, the bang that consumes rather than re-runs,
//     and `AtomList::Keep` reclaiming the characters a consumed atom leaves
//     behind — without which a queue popped often enough would refuse atoms it
//     has room for;
//   - **the bounded storage model** the siblings inherit — 256 atoms of at most
//     1024 characters, pre-allocated, with overflow *refused and counted*
//     rather than truncated, and the head kept rather than the tail;
//   - **the transport convention** the siblings inherit — a result of one atom
//     leaves as the int/float/symbol it spells, a result of none sends nothing
//     at all, and a longer one is list text.
//
// The unit-level cases drive standalone objects, which is what this object
// needs (it has no patcher, no clock and no scheduler). The end-to-end section
// at the bottom drives a real `YSE::patcher` graph through `pHandle`, because
// the claims that matter to a patch — that the two outlets land on two
// different downstream objects in the documented order, and that the params
// survive a save/load — cannot be seen from a standalone object at all.
//
// No audio device required.

#include <doctest/doctest.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "patcher/genericObjects/gZl.h"
#include "patcher/inlet.h"
#include "patcher/pAtomList.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::MultiSink;
using TestHelpers::OrderSink;
using YSE::PATCHER::AtomList;
using YSE::PATCHER::gZl;
using Mode = YSE::PATCHER::gZl::Mode;

namespace {

  // A standalone `.zl` with a sink on each outlet. Standalone on purpose: the
  // object needs no patcher, and a test that needed one could not tell a
  // dropped message from one the patcher never delivered. Sinks first, so the
  // object dies before the inlets it is wired to.
  struct Rig {
    MultiSink left;
    MultiSink right;

    void Wire(gZl& obj) {
      TestHelpers::Wire(obj, 0, left);
      TestHelpers::Wire(obj, 1, right);
    }

    void reset() {
      left.reset();
      right.reset();
    }
  };

  // Records every message that arrived, in order and with its kind, so "these
  // messages, in this order, of these types" is an assertion rather than an
  // inference. `MultiSink` keeps only the last of each kind, which is exactly
  // what the modes that send *several* messages per stimulus (#527's `iter`
  // and `group`) need a test not to do. Borrowed from `.iter`'s tests (#521),
  // which needed it first and for the same reason.
  struct TallySink : YSE::PATCHER::pObject {
    std::vector<std::string> got; // "i:5", "f", "l:a b", "bang"

    TallySink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { got.push_back("bang"); });
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { got.push_back("i:" + std::to_string(v)); });
      inputs.back().RegisterFloat([this](float, int, YSE::THREAD) { got.push_back("f"); });
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { got.push_back("l:" + v); });
      got.reserve(1024);
    }
    const char* Type() const override {
      return "tally_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    void reset() {
      got.clear();
    }
  };

  // A `.zl` with a tallying sink on each outlet — the rig the multi-send modes
  // need. Sinks first, so the object dies before the inlets it is wired to.
  struct TallyRig {
    TallySink left;
    TallySink right;

    void Wire(gZl& obj) {
      TestHelpers::Wire(obj, 0, left);
      TestHelpers::Wire(obj, 1, right);
    }

    void reset() {
      left.reset();
      right.reset();
    }
  };

  // A list of `count` single-digit atoms — "1 2 3 ...", wrapping at 9 so every
  // atom stays one character wide and the text length is predictable.
  std::string Digits(std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count; i++) {
      if (i > 0) out.push_back(' ');
      out.push_back((char)('1' + (i % 9)));
    }
    return out;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── the shared bounded list ────────────────────────────────────────────────

  TEST_CASE("zl: AtomList refuses past its atom ceiling and keeps the head (#523)") {
    // The storage model the whole list family inherits. Nothing is truncated:
    // Add answers false and changes nothing, and the atoms already collected
    // stay put — an over-long list loses its tail.
    AtomList list;
    CHECK(list.Empty());

    for (std::size_t i = 0; i < AtomList::MAX_ATOMS; i++)
      CHECK(list.Add("1", 1));
    CHECK(list.Size() == AtomList::MAX_ATOMS);

    CHECK_FALSE(list.Add("2", 1));
    CHECK(list.Size() == AtomList::MAX_ATOMS);
    // The head is intact: the refusal did not disturb what was already there.
    REQUIRE(list.AtomText(0) != nullptr);
    CHECK(list.AtomLength(0) == 1);
    CHECK(list.AtomText(0)[0] == '1');
  }

  TEST_CASE("zl: AtomList refuses past its text ceiling too (#523)") {
    // The atom count is not the bound that limits the memory — the characters
    // are. A handful of long symbols fills the text long before 256 atoms do.
    AtomList list;
    const std::string wide(64, 'x');
    std::size_t added = 0;
    while (list.Add(wide)) {
      added++;
      REQUIRE(added <= AtomList::MAX_ATOMS);
    }
    CHECK(added == AtomList::TEXT_CAPACITY / 64);
    CHECK(list.Size() == added);
    CHECK(added < AtomList::MAX_ATOMS);
  }

  TEST_CASE("zl: AtomList narrows to a caller's limit without widening past the ceiling (#523)") {
    AtomList list;
    for (int i = 0; i < 10; i++)
      list.Add("1", 1, 4);
    CHECK(list.Size() == 4);

    // A limit above the ceiling is clamped rather than honoured: the storage
    // behind it was allocated once.
    AtomList other;
    const std::size_t refused =
        other.AddTokens(Digits(AtomList::MAX_ATOMS + 8), AtomList::MAX_ATOMS + 8);
    CHECK(other.Size() == AtomList::MAX_ATOMS);
    CHECK(refused == 8);
  }

  TEST_CASE("zl: AtomList classifies atoms once, on the way in (#523)") {
    AtomList list;
    list.AddTokens("1 2.5 abc 1e999");
    REQUIRE(list.Size() == 4);

    CHECK(list.AtomIsNumber(0));
    CHECK_FALSE(list.AtomIsFloat(0));
    CHECK(list.AtomValue(0) == doctest::Approx(1.f));

    CHECK(list.AtomIsNumber(1));
    CHECK(list.AtomIsFloat(1));
    CHECK(list.AtomValue(1) == doctest::Approx(2.5f));

    CHECK_FALSE(list.AtomIsNumber(2));
    // An overflowing literal is more useful as the symbol it was typed as than
    // as an infinity — ReadNumericToken's rule, inherited here.
    CHECK_FALSE(list.AtomIsNumber(3));

    // Out of range answers rather than trapping.
    CHECK(list.AtomText(4) == nullptr);
    CHECK(list.AtomLength(4) == 0);
    CHECK_FALSE(list.AtomIsNumber(4));
  }

  TEST_CASE("zl: AtomList reorders the table without moving the text (#523)") {
    AtomList list;
    list.AddTokens("a b c d");
    list.Reverse();

    std::string out;
    AtomList::ReserveRender(out);
    list.Render(out);
    CHECK(out == "d c b a");

    list.RenderExcept(out, 1);
    CHECK(out == "d b a");

    // A copy is independent: reversing the copy leaves the original alone.
    AtomList copy;
    copy.Assign(list);
    copy.Reverse();
    copy.Render(out);
    CHECK(out == "a b c d");
    list.Render(out);
    CHECK(out == "d c b a");
  }

  TEST_CASE("zl: AtomList applies an index order, dropping entries that name nothing (#524)") {
    // The shared reordering primitive every #524 mode goes through.
    AtomList source;
    source.AddTokens("a b c d");

    AtomList out;
    std::string text;
    AtomList::ReserveRender(text);

    const std::uint16_t reversed[] = {3, 2, 1, 0};
    out.AssignOrder(source, reversed, 4);
    out.Render(text);
    CHECK(text == "d c b a");
    // The source is untouched: the order is applied to a copy.
    source.Render(text);
    CHECK(text == "a b c d");

    // Entries may repeat, and the result is as long as the order rather than as
    // long as the source.
    const std::uint16_t repeated[] = {1, 1, 0};
    out.AssignOrder(source, repeated, 3);
    CHECK(out.Size() == 3);
    out.Render(text);
    CHECK(text == "b b a");

    // An entry naming no atom contributes nothing — which is how a mode refuses
    // an index without compacting its own array first.
    const std::uint16_t withGaps[] = {2, 0xFFFF, 9, 0};
    out.AssignOrder(source, withGaps, 4);
    CHECK(out.Size() == 2);
    out.Render(text);
    CHECK(text == "c a");

    // Nothing survivable at all leaves an empty list rather than a wild read.
    const std::uint16_t none[] = {0xFFFF, 0xFFFF};
    out.AssignOrder(source, none, 2);
    CHECK(out.Empty());
  }

  TEST_CASE("zl: AtomList appends atoms of another list, and a run of them (#527)") {
    // The other direction from Assign / AssignOrder, which take a whole backing
    // text: this builds one list out of two, which is what `join` and `lace`
    // need (#527) and what `union` (#526) did by hand.
    AtomList a;
    a.AddTokens("1 2");
    AtomList b;
    b.AddTokens("x y z");

    AtomList out;
    std::string text;
    AtomList::ReserveRender(text);

    CHECK(out.AddRange(a, 0, a.Size()) == 0);
    CHECK(out.AddAtom(b, 1));
    out.Render(text);
    CHECK(text == "1 2 y");

    // The run is clamped to the source, so a count past its end is not a
    // refusal — there was no atom there to refuse.
    CHECK(out.AddRange(b, 2, 99) == 0);
    out.Render(text);
    CHECK(text == "1 2 y z");
    CHECK(out.AddRange(b, 9, 3) == 0);
    CHECK(out.Size() == 4);

    // An index naming no atom is refused rather than appending a blank.
    CHECK_FALSE(out.AddAtom(b, 7));
    CHECK(out.Size() == 4);

    // The ceiling still applies, and what does not fit is reported.
    AtomList full;
    for (std::size_t i = 0; i < AtomList::MAX_ATOMS; i++)
      full.Add("1", 1);
    CHECK(full.AddRange(a, 0, 2) == 2);
    CHECK(full.Size() == AtomList::MAX_ATOMS);
  }

  TEST_CASE("zl: AtomList keeps a run and reclaims the characters it drops (#527)") {
    // The primitive the accumulating modes rest on. The text is append-only
    // everywhere else, which is fine for a list rebuilt per message and fatal
    // for one consumed from an end over and over — so this is the operation
    // that moves the retained characters down rather than leaving holes.
    AtomList list;
    list.AddTokens("aa bb cc dd");

    std::string text;
    AtomList::ReserveRender(text);

    list.Keep(1, 2);
    CHECK(list.Size() == 2);
    list.Render(text);
    CHECK(text == "bb cc");

    // The atoms still read as themselves after the move — the table followed
    // the characters rather than being left pointing at the old offsets.
    CHECK(list.AtomLength(0) == 2);
    CHECK(std::string(list.AtomText(0), 2) == "bb");

    // Both bounds are clamped, and a begin past the end empties the list.
    list.Keep(0, 99);
    CHECK(list.Size() == 2);
    list.Keep(5, 1);
    CHECK(list.Empty());

    // The property that matters: pushing and dropping forever costs no text.
    // 400 rounds of a four-character atom is 1600 characters through a 1024
    // character buffer, so an append-only list would have started refusing.
    AtomList rolling;
    for (int round = 0; round < 400; round++) {
      REQUIRE(rolling.Add("abcd", 4));
      rolling.Keep(1, rolling.Size() - 1);
    }
    CHECK(rolling.Empty());

    // And the numeric classification survives the move, since it travels in the
    // table rather than being re-read from the characters.
    AtomList mixed;
    mixed.AddTokens("sym 1 2.5");
    mixed.Keep(1, 2);
    CHECK(mixed.AtomIsNumber(0));
    CHECK(mixed.AtomValue(0) == 1.f);
    CHECK(mixed.AtomIsFloat(1));
    CHECK(mixed.AtomValue(1) == doctest::Approx(2.5f));
  }

  // ─── shape and registration ─────────────────────────────────────────────────

  TEST_CASE("zl: registered, two inlets and two outlets (#523)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::G_ZL, "len");
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == ".zl");
    CHECK(obj->GetInputs() == 2);
    CHECK(obj->GetOutputs() == 2);
  }

  TEST_CASE("zl: appears in the registry's name list (#523)") {
    auto names = YSE::PATCHER::Register().AllNames();
    bool found = false;
    for (const auto& name : names) {
      if (name == std::string(YSE::OBJ::G_ZL)) found = true;
    }
    CHECK(found);
  }

  TEST_CASE("zl: the left inlet takes a bang, the right one does not (#523)") {
    // The right inlet holds an argument and has nothing to do with a bang, so
    // it registers no handler — the .prepend / .combine discipline, which keeps
    // GetAcceptedTypes() reporting the real contract.
    gZl obj;
    const unsigned int left = obj.GetInlet(0)->GetAcceptedTypes();
    CHECK((left & YSE::PATCHER::IT_BANG) != 0);
    CHECK((left & YSE::PATCHER::IT_LIST) != 0);

    const unsigned int right = obj.GetInlet(1)->GetAcceptedTypes();
    CHECK((right & YSE::PATCHER::IT_BANG) == 0);
    CHECK((right & YSE::PATCHER::IT_INT) != 0);
    CHECK((right & YSE::PATCHER::IT_LIST) != 0);
  }

  // ─── the mode argument ──────────────────────────────────────────────────────

  TEST_CASE("zl: the creation arguments set the mode, the limit and the argument (#523)") {
    gZl bare;
    // No mode word: inert rather than silently behaving as one the patch did
    // not ask for. Max's undocumented default is `reg`, which is not ported.
    CHECK(bare.CurrentMode() == Mode::NONE);
    CHECK(bare.Limit() == AtomList::MAX_ATOMS);

    gZl nth;
    nth.SetParams("nth 2");
    CHECK(nth.CurrentMode() == Mode::NTH);
    CHECK(nth.Argument() == 2);
    CHECK(nth.Limit() == AtomList::MAX_ATOMS);

    // Max's optional leading maximum length, and the reason it is unambiguous:
    // a mode word is never wholly an integer.
    gZl capped;
    capped.SetParams("64 nth 3");
    CHECK(capped.CurrentMode() == Mode::NTH);
    CHECK(capped.Limit() == 64);
    CHECK(capped.Argument() == 3);
  }

  TEST_CASE("zl: an unknown mode word leaves the object inert, not guessing (#523)") {
    // `median` and `sum` are the last two words of Max's vocabulary that are
    // not ported yet — the words this test needs are whichever ones are still
    // unimplemented, and it moved off `scramble` / `sort` when #524 implemented
    // them and off `queue` when #527 did.
    gZl obj;
    obj.SetParams("median");
    CHECK(obj.CurrentMode() == Mode::NONE);

    // And a mode message naming a mode that is not implemented yet leaves the
    // mode where it was, rather than falling back to another one.
    obj.SetParams("rev");
    REQUIRE(obj.CurrentMode() == Mode::REV);
    obj.GetInlet(0)->SetList("mode sum", YSE::T_GUI);
    CHECK(obj.CurrentMode() == Mode::REV);
  }

  TEST_CASE("zl: every mode word round trips through ReadMode and ModeName (#524)") {
    // The two halves of the vocabulary have to agree: a spelling ReadMode knows
    // and ModeName does not is a mode a patch can select and the documentation
    // cannot name.
    const Mode all[] = {Mode::LEN,      Mode::REV,    Mode::NTH,      Mode::ROT,    Mode::SORT,
                        Mode::SCRAMBLE, Mode::SWAP,   Mode::INDEXMAP, Mode::MTH,    Mode::SLICE,
                        Mode::SUB,      Mode::LOOKUP, Mode::SECT,     Mode::UNION,  Mode::UNIQUE,
                        Mode::THIN,     Mode::FILTER, Mode::COMPARE,  Mode::CHANGE, Mode::REG,
                        Mode::ITER,     Mode::JOIN,   Mode::LACE,     Mode::DELACE, Mode::ECILS,
                        Mode::GROUP,    Mode::STREAM, Mode::QUEUE,    Mode::STACK};
    for (Mode wanted : all) {
      const char* word = gZl::ModeName(wanted);
      REQUIRE(word[0] != '\0');
      Mode parsed = Mode::NONE;
      CHECK(gZl::ReadMode(word, std::string(word).size(), parsed));
      CHECK(parsed == wanted);
    }

    // NONE is the absence of a word rather than a word.
    CHECK(std::string(gZl::ModeName(Mode::NONE)).empty());

    // Strict: a token that merely starts with a mode word is not that mode.
    Mode parsed = Mode::NONE;
    CHECK_FALSE(gZl::ReadMode("sorted", 6, parsed));
    CHECK_FALSE(gZl::ReadMode("so", 2, parsed));
  }

  TEST_CASE("zl: SetParams(\"\") returns the object to its no-argument shape (#523)") {
    // Parameters::Set returns without calling the parse callback for an empty
    // argument, so the clear callback is the whole of the reset.
    gZl obj;
    obj.SetParams("8 nth 2");
    REQUIRE(obj.CurrentMode() == Mode::NTH);
    obj.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    REQUIRE(obj.Stored() == 3);

    obj.SetParams("");
    CHECK(obj.CurrentMode() == Mode::NONE);
    CHECK(obj.Limit() == AtomList::MAX_ATOMS);
    CHECK(obj.Argument() == 0);
    CHECK(obj.Stored() == 0);
  }

  TEST_CASE("zl: the limit clamps out-of-range values on read (#523)") {
    // Clamped on read rather than on write, so a live SetParams re-parse and
    // the zlmaxsize message are covered by the same range.
    gZl obj;
    obj.SetParams("0 len");
    CHECK(obj.Limit() == 1);

    obj.SetParams("9999 len");
    CHECK(obj.Limit() == AtomList::MAX_ATOMS);

    obj.GetInlet(0)->SetList("zlmaxsize -4", YSE::T_GUI);
    CHECK(obj.Limit() == 1);
    obj.GetInlet(0)->SetList("zlmaxsize 3", YSE::T_GUI);
    CHECK(obj.Limit() == 3);
  }

  // ─── len ────────────────────────────────────────────────────────────────────

  TEST_CASE("zl len: sends the number of items out the left outlet (#523)") {
    Rig rig;
    gZl obj;
    obj.SetParams("len");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("10 20 30 40", YSE::T_GUI);
    CHECK(rig.left.gotInt);
    CHECK(rig.left.intValue == 4);
    // A mode with one result says nothing on the right outlet, so a patch can
    // tell "no second half" from "the second half is the whole list".
    CHECK_FALSE(rig.right.gotInt);
    CHECK_FALSE(rig.right.gotList);
    CHECK_FALSE(rig.right.gotBang);

    // A bare number is a list of one, as it is in Max.
    rig.reset();
    obj.GetInlet(0)->SetInt(7, YSE::T_GUI);
    CHECK(rig.left.gotInt);
    CHECK(rig.left.intValue == 1);
  }

  TEST_CASE("zl len: a bang re-runs the mode over the stored list (#523)") {
    Rig rig;
    gZl obj;
    obj.SetParams("len");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("a b c", YSE::T_GUI);
    REQUIRE(rig.left.intValue == 3);

    rig.reset();
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.gotInt);
    CHECK(rig.left.intValue == 3);

    // zlclear empties the stored list; the mode and the limit are
    // configuration rather than contents and survive it.
    rig.reset();
    obj.GetInlet(0)->SetList("zlclear", YSE::T_GUI);
    CHECK_FALSE(rig.left.gotInt);
    CHECK(obj.Stored() == 0);
    CHECK(obj.CurrentMode() == Mode::LEN);

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.gotInt);
    CHECK(rig.left.intValue == 0);
  }

  // ─── rev ────────────────────────────────────────────────────────────────────

  TEST_CASE("zl rev: sends the list back reversed (#523)") {
    Rig rig;
    gZl obj;
    obj.SetParams("rev");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("1 2 3 4", YSE::T_GUI);
    CHECK(rig.left.gotList);
    CHECK(rig.left.listValue == "4 3 2 1");
    CHECK_FALSE(rig.right.gotList);

    // Reversing does not consume the stored list: a bang reverses the same
    // list again rather than un-reversing the previous answer.
    rig.reset();
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.listValue == "4 3 2 1");
  }

  TEST_CASE("zl rev: a one-item result leaves as the value it spells (#523)") {
    // The family's transport convention: a list of one atom is not a list, and
    // this patcher does no coercion at an inlet, so a single-atom result has to
    // reach the int inlet an uncollected value would have reached.
    Rig rig;
    gZl obj;
    obj.SetParams("rev");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("42", YSE::T_GUI);
    CHECK(rig.left.gotInt);
    CHECK(rig.left.intValue == 42);
    CHECK_FALSE(rig.left.gotList);

    rig.reset();
    obj.GetInlet(0)->SetList("2.5", YSE::T_GUI);
    CHECK(rig.left.gotFloat);
    CHECK(rig.left.floatValue == doctest::Approx(2.5f));

    rig.reset();
    obj.GetInlet(0)->SetList("hello", YSE::T_GUI);
    CHECK(rig.left.gotList);
    CHECK(rig.left.listValue == "hello");
  }

  TEST_CASE("zl rev: an empty stored list sends nothing at all (#523)") {
    // The .sprintf / .prepend rule: an object with nothing to say says nothing
    // rather than emitting an empty message.
    Rig rig;
    gZl obj;
    obj.SetParams("rev");
    rig.Wire(obj);

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.left.gotList);
    CHECK_FALSE(rig.left.gotInt);
    CHECK_FALSE(rig.left.gotFloat);
    CHECK_FALSE(rig.left.gotBang);
  }

  // ─── nth ────────────────────────────────────────────────────────────────────

  TEST_CASE("zl nth: picks a 1-based item, remainder out the right outlet (#523)") {
    Rig rig;
    gZl obj;
    obj.SetParams("nth 2");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("10 20 30 40", YSE::T_GUI);
    CHECK(rig.left.gotInt);
    CHECK(rig.left.intValue == 20);
    CHECK(rig.right.gotList);
    CHECK(rig.right.listValue == "10 30 40");
  }

  TEST_CASE("zl nth: the right outlet fires before the left one (#523)") {
    // Max's right-to-left rule, and it is only observable with two sinks
    // sharing a log — counting hits could not tell the two orders apart.
    std::vector<char> log;
    OrderSink left;
    OrderSink right;
    left.log = &log;
    left.tag = 'L';
    right.log = &log;
    right.tag = 'R';

    gZl obj;
    obj.SetParams("nth 1");
    TestHelpers::Wire(obj, 0, left);
    TestHelpers::Wire(obj, 1, right);

    obj.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == 'R');
    CHECK(log[1] == 'L');
  }

  TEST_CASE("zl nth: the right inlet sets the index, cold (#523)") {
    Rig rig;
    gZl obj;
    obj.SetParams("nth");
    rig.Wire(obj);

    // Setting the index emits nothing on its own.
    obj.GetInlet(1)->SetInt(3, YSE::T_GUI);
    CHECK(obj.Argument() == 3);
    CHECK_FALSE(rig.left.gotInt);
    CHECK_FALSE(rig.right.gotList);

    obj.GetInlet(0)->SetList("a b c d", YSE::T_GUI);
    CHECK(rig.left.gotList);
    CHECK(rig.left.listValue == "c");
    CHECK(rig.right.listValue == "a b d");

    // A float index is truncated: an index is a whole number.
    rig.reset();
    obj.GetInlet(1)->SetFloat(2.9f, YSE::T_GUI);
    CHECK(obj.Argument() == 2);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.listValue == "b");
  }

  TEST_CASE("zl nth: an out-of-range index sends nothing from either outlet (#523)") {
    // An index naming no item has no item to send, and no "everything else"
    // that means anything either — the whole list is not the remainder of a
    // pick that did not happen.
    Rig rig;
    gZl obj;
    obj.SetParams("nth 0");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    CHECK_FALSE(rig.left.gotInt);
    CHECK_FALSE(rig.left.gotList);
    CHECK_FALSE(rig.right.gotList);

    obj.GetInlet(1)->SetInt(4, YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.left.gotInt);
    CHECK_FALSE(rig.right.gotList);
  }

  TEST_CASE("zl nth: a two-item list leaves one item, sent as a value (#523)") {
    Rig rig;
    gZl obj;
    obj.SetParams("nth 1");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("11 22", YSE::T_GUI);
    CHECK(rig.left.gotInt);
    CHECK(rig.left.intValue == 11);
    // One atom left over, and it leaves as the int it spells rather than as a
    // list of one — the same convention the left outlet follows.
    CHECK(rig.right.gotInt);
    CHECK(rig.right.intValue == 22);
    CHECK_FALSE(rig.right.gotList);

    // A one-item list leaves nothing at all on the right.
    rig.reset();
    obj.GetInlet(0)->SetList("99", YSE::T_GUI);
    CHECK(rig.left.intValue == 99);
    CHECK_FALSE(rig.right.gotInt);
    CHECK_FALSE(rig.right.gotList);
  }

  // ─── rot ────────────────────────────────────────────────────────────────────

  TEST_CASE("zl rot: rotates by the number of places the argument gives (#524)") {
    Rig rig;
    gZl obj;
    obj.SetParams("rot 1");
    rig.Wire(obj);

    // Positive rotates toward the end of the list: the item that was last comes
    // out first.
    obj.GetInlet(0)->SetList("1 2 3 4 5", YSE::T_GUI);
    CHECK(rig.left.gotList);
    CHECK(rig.left.listValue == "5 1 2 3 4");
    // One result, so nothing on the right outlet.
    CHECK_FALSE(rig.right.gotList);

    // Negative rotates toward the start.
    obj.GetInlet(1)->SetInt(-1, YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.listValue == "2 3 4 5 1");

    // Rotating does not consume the stored list: two bangs at the same setting
    // give the same answer rather than compounding.
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.listValue == "2 3 4 5 1");
  }

  TEST_CASE("zl rot: any magnitude is legal, a whole turn is the identity (#524)") {
    // Modulo the length, so a patch need not keep the number in range.
    Rig rig;
    gZl obj;
    obj.SetParams("rot 0");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("1 2 3 4", YSE::T_GUI);
    CHECK(rig.left.listValue == "1 2 3 4");

    obj.GetInlet(1)->SetInt(4, YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.listValue == "1 2 3 4");

    obj.GetInlet(1)->SetInt(401, YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.listValue == "4 1 2 3");

    obj.GetInlet(1)->SetInt(-401, YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.listValue == "2 3 4 1");

    // An empty stored list sends nothing at all rather than an empty message.
    rig.reset();
    obj.GetInlet(0)->SetList("zlclear", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.left.gotList);
    CHECK_FALSE(rig.left.gotInt);
  }

  // ─── scramble ───────────────────────────────────────────────────────────────

  TEST_CASE("zl scramble: a seeded shuffle replays, a different seed does not (#524)") {
    // The reason `.zl` carries a per-object RandomSource rather than drawing
    // from the engine-wide generator: a shuffle nobody can replay is a shuffle
    // no test can pin beyond "it was a permutation".
    Rig rig;
    gZl obj;
    obj.SetParams("scramble");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("zlseed 4242", YSE::T_GUI);
    obj.GetInlet(0)->SetList("1 2 3 4 5 6 7 8", YSE::T_GUI);
    REQUIRE(rig.left.gotList);
    const std::string first = rig.left.listValue;

    // Re-seeded to the same stream, the same input gives the same order back.
    obj.GetInlet(0)->SetList("zlseed 4242", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.listValue == first);

    // A different seed is a different walk — the trap RandomSource's own header
    // records, where neighbouring seeds shared a stream.
    obj.GetInlet(0)->SetList("zlseed 4243", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.listValue != first);

    // Seeding emits nothing on its own and keeps the stored list.
    rig.reset();
    obj.GetInlet(0)->SetList("zlseed 7", YSE::T_GUI);
    CHECK_FALSE(rig.left.gotList);
    CHECK(obj.Stored() == 8);
  }

  TEST_CASE("zl scramble: the result is a permutation, and one draw per item moved (#524)") {
    Rig rig;
    gZl obj;
    obj.SetParams("scramble");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("zlseed 99", YSE::T_GUI);
    const auto before = obj.Draws();
    obj.GetInlet(0)->SetList("1 2 3 4 5 6", YSE::T_GUI);
    REQUIRE(rig.left.gotList);

    // Fisher-Yates moves every item but the first, so a six-item list costs
    // five draws — the property a refactor breaks silently.
    CHECK(obj.Draws() - before == 5);

    // Every item is still there, exactly once.
    std::vector<char> seen;
    for (char c : rig.left.listValue) {
      if (c != ' ') seen.push_back(c);
    }
    REQUIRE(seen.size() == 6);
    for (char want = '1'; want <= '6'; want++) {
      int count = 0;
      for (char c : seen) {
        if (c == want) count++;
      }
      CHECK(count == 1);
    }

    // Nothing on the right outlet, and an empty list sends nothing at all.
    CHECK_FALSE(rig.right.gotList);
    rig.reset();
    obj.GetInlet(0)->SetList("zlclear", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.left.gotList);
  }

  // ─── sort ───────────────────────────────────────────────────────────────────

  TEST_CASE("zl sort: sorts left, publishes the 1-based index map right (#524)") {
    Rig rig;
    gZl obj;
    obj.SetParams("sort");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("30 10 20", YSE::T_GUI);
    CHECK(rig.left.gotList);
    CHECK(rig.left.listValue == "10 20 30");
    // For each item of the sorted list, the 1-based position it held in the
    // input: 10 was second, 20 third, 30 first.
    CHECK(rig.right.gotList);
    CHECK(rig.right.listValue == "2 3 1");
  }

  TEST_CASE("zl sort: the index map arrives before the sorted list (#524)") {
    // Max's right-to-left rule, and here it carries weight: the map is what a
    // patch feeds to an `indexmap` for a parallel list, so it has to be in
    // place before the sorted list sets that patch running.
    std::vector<char> log;
    OrderSink left;
    OrderSink right;
    left.log = &log;
    left.tag = 'L';
    right.log = &log;
    right.tag = 'R';

    gZl obj;
    obj.SetParams("sort");
    TestHelpers::Wire(obj, 0, left);
    TestHelpers::Wire(obj, 1, right);

    obj.GetInlet(0)->SetList("3 1 2", YSE::T_GUI);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == 'R');
    CHECK(log[1] == 'L');
  }

  TEST_CASE("zl sort: a negative argument sorts downwards (#524)") {
    Rig rig;
    gZl obj;
    obj.SetParams("sort -1");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("30 10 20", YSE::T_GUI);
    CHECK(rig.left.listValue == "30 20 10");
    CHECK(rig.right.listValue == "1 3 2");

    // Anything else sorts upwards, so an unset argument is an ascending sort.
    obj.GetInlet(1)->SetInt(0, YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.listValue == "10 20 30");
  }

  TEST_CASE("zl sort: equal items keep the order they arrived in (#524)") {
    // Stability is what makes the published map one a patch can reason about:
    // an unstable sort would give a different map for the same input from one
    // build to the next.
    Rig rig;
    gZl obj;
    obj.SetParams("sort");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("2 1 2 1", YSE::T_GUI);
    CHECK(rig.left.listValue == "1 1 2 2");
    // The first 1 was at position 2 and the second at 4; the first 2 at 1 and
    // the second at 3.
    CHECK(rig.right.listValue == "2 4 1 3");
  }

  TEST_CASE("zl sort: numbers before symbols in both directions (#524)") {
    // The number/symbol split is a type ordering rather than a value one, so a
    // descending sort does not sweep the symbols to the front — `sort` and
    // `sort -1` stay one question asked two ways.
    Rig rig;
    gZl obj;
    obj.SetParams("sort");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("pear 2 apple 1", YSE::T_GUI);
    CHECK(rig.left.listValue == "1 2 apple pear");

    obj.GetInlet(1)->SetInt(-1, YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.listValue == "2 1 pear apple");

    // A symbol that is a prefix of another sorts first.
    obj.GetInlet(1)->SetInt(1, YSE::T_GUI);
    obj.GetInlet(0)->SetList("ab a abc", YSE::T_GUI);
    CHECK(rig.left.listValue == "a ab abc");
  }

  TEST_CASE("zl sort: a one-item list still answers on both outlets (#524)") {
    Rig rig;
    gZl obj;
    obj.SetParams("sort");
    rig.Wire(obj);

    // The transport convention applies to the map as much as to the list: one
    // atom leaves as the int it spells.
    obj.GetInlet(0)->SetList("42", YSE::T_GUI);
    CHECK(rig.left.gotInt);
    CHECK(rig.left.intValue == 42);
    CHECK(rig.right.gotInt);
    CHECK(rig.right.intValue == 1);

    // And an empty list answers on neither.
    rig.reset();
    obj.GetInlet(0)->SetList("zlclear", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.left.gotInt);
    CHECK_FALSE(rig.left.gotList);
    CHECK_FALSE(rig.right.gotInt);
    CHECK_FALSE(rig.right.gotList);
  }

  TEST_CASE("zl sort: a full-length list sorts correctly (#524)") {
    // The merge sort runs its full depth here — eight passes over 256 items —
    // which is where an off-by-one in the run bounds would show.
    Rig rig;
    gZl obj;
    obj.SetParams("sort");
    rig.Wire(obj);

    std::string descending;
    for (std::size_t i = 0; i < AtomList::MAX_ATOMS; i++) {
      if (i > 0) descending.push_back(' ');
      descending += std::to_string(AtomList::MAX_ATOMS - i);
    }
    obj.GetInlet(0)->SetList(descending, YSE::T_GUI);
    REQUIRE(rig.left.gotList);

    // Reading the result back through an AtomList is the cheapest way to assert
    // "ascending, and all of it".
    AtomList result;
    result.AddTokens(rig.left.listValue);
    REQUIRE(result.Size() == AtomList::MAX_ATOMS);
    for (std::size_t i = 0; i < result.Size(); i++)
      CHECK(result.AtomValue(i) == doctest::Approx((float)(i + 1)));
  }

  // ─── swap ───────────────────────────────────────────────────────────────────

  TEST_CASE("zl swap: exchanges the two items its 1-based indices name (#524)") {
    Rig rig;
    gZl obj;
    obj.SetParams("swap 2 4");
    rig.Wire(obj);

    CHECK(obj.ArgumentCount() == 2);
    CHECK(obj.ArgumentAt(0) == 2);
    CHECK(obj.ArgumentAt(1) == 4);

    obj.GetInlet(0)->SetList("a b c d e", YSE::T_GUI);
    CHECK(rig.left.gotList);
    CHECK(rig.left.listValue == "a d c b e");
    CHECK_FALSE(rig.right.gotList);

    // The right inlet takes both indices as one list — the reason it reads more
    // than its leading token now.
    obj.GetInlet(1)->SetList("1 5", YSE::T_GUI);
    CHECK(obj.ArgumentCount() == 2);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.listValue == "e b c d a");

    // Swapping an item with itself is the list unchanged, not an error.
    obj.GetInlet(1)->SetList("3 3", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.listValue == "a b c d e");
  }

  TEST_CASE("zl swap: an index naming no item sends nothing at all (#524)") {
    // `nth`'s answer to the same question: the exchange asked for cannot be
    // made, and a list that is not the one the patch asked for is worse than
    // none.
    Rig rig;
    gZl obj;
    obj.SetParams("swap 2 9");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("a b c", YSE::T_GUI);
    CHECK_FALSE(rig.left.gotList);
    CHECK_FALSE(rig.right.gotList);

    obj.GetInlet(1)->SetList("0 2", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.left.gotList);

    // One index is not half a swap; it is not a swap.
    obj.GetInlet(1)->SetInt(1, YSE::T_GUI);
    REQUIRE(obj.ArgumentCount() == 1);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.left.gotList);

    // And two good ones bring it back.
    obj.GetInlet(1)->SetList("1 3", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.gotList);
    CHECK(rig.left.listValue == "c b a");
  }

  // ─── indexmap ───────────────────────────────────────────────────────────────

  TEST_CASE("zl indexmap: re-picks the list in the order the map names (#524)") {
    Rig rig;
    gZl obj;
    obj.SetParams("indexmap 3 1 2");
    rig.Wire(obj);

    CHECK(obj.ArgumentCount() == 3);
    obj.GetInlet(0)->SetList("a b c", YSE::T_GUI);
    CHECK(rig.left.listValue == "c a b");
    CHECK_FALSE(rig.right.gotList);

    // Elementwise, so a map may name an item twice or leave one out — the
    // result is as long as the map, not as long as the list.
    obj.GetInlet(1)->SetList("2 2 2 1", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.listValue == "b b b a");

    obj.GetInlet(1)->SetList("3", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    // One atom out leaves as the symbol it spells, the family's convention.
    CHECK(rig.left.listValue == "c");
  }

  TEST_CASE("zl indexmap: an index naming no item drops its own element (#524)") {
    // Unlike `swap`, which refuses the whole exchange: a map is a list of
    // independent picks, so the ones that can be honoured still are.
    Rig rig;
    gZl obj;
    obj.SetParams("indexmap");
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("2 9 0 1 -3", YSE::T_GUI);
    obj.GetInlet(0)->SetList("a b c", YSE::T_GUI);
    CHECK(rig.left.listValue == "b a");

    // A map naming nothing that exists sends nothing at all rather than an
    // empty message.
    rig.reset();
    obj.GetInlet(1)->SetList("7 8", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.left.gotList);
    CHECK_FALSE(rig.left.gotInt);

    // And with no map at all the object stores its input and says nothing.
    rig.reset();
    gZl bare;
    Rig bareRig;
    bare.SetParams("indexmap");
    bareRig.Wire(bare);
    bare.GetInlet(0)->SetList("a b c", YSE::T_GUI);
    CHECK(bare.Stored() == 3);
    CHECK_FALSE(bareRig.left.gotList);
  }

  // ─── mth (#525) ─────────────────────────────────────────────────────────────

  TEST_CASE("zl mth: picks a 0-based item, remainder out the right outlet (#525)") {
    Rig rig;
    gZl obj;
    obj.SetParams("mth 1");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("10 20 30 40", YSE::T_GUI);
    CHECK(rig.left.gotInt);
    CHECK(rig.left.intValue == 20);
    // The right outlet says exactly what `nth`'s does: everything but the pick.
    CHECK(rig.right.gotList);
    CHECK(rig.right.listValue == "10 30 40");

    // Index 0 is the first item, which is the whole content of the mode.
    rig.reset();
    obj.GetInlet(1)->SetInt(0, YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.intValue == 10);
    CHECK(rig.right.listValue == "20 30 40");
  }

  TEST_CASE("zl mth: is nth shifted by exactly one, at both ends of the list (#525)") {
    // The object's single 0-based mode, and the one place an off-by-one would
    // hide. Pinned as an equivalence rather than as two separate expectations:
    // `mth k` and `nth k+1` must name the same item for every k, and the two
    // must go out of range one step apart.
    Rig zeroBased;
    Rig oneBased;
    gZl mth;
    gZl nth;
    mth.SetParams("mth");
    nth.SetParams("nth");
    zeroBased.Wire(mth);
    oneBased.Wire(nth);

    for (int k = 0; k < 4; k++) {
      zeroBased.reset();
      oneBased.reset();
      mth.GetInlet(1)->SetInt(k, YSE::T_GUI);
      nth.GetInlet(1)->SetInt(k + 1, YSE::T_GUI);
      mth.GetInlet(0)->SetList("a b c d", YSE::T_GUI);
      nth.GetInlet(0)->SetList("a b c d", YSE::T_GUI);
      CHECK(zeroBased.left.listValue == oneBased.left.listValue);
      CHECK(zeroBased.right.listValue == oneBased.right.listValue);
      CHECK_FALSE(zeroBased.left.listValue.empty());
    }

    // And they run out one step apart: index 4 is past the end of a four-item
    // list for `mth` and the last item for `nth`, index 0 the reverse.
    zeroBased.reset();
    mth.GetInlet(1)->SetInt(4, YSE::T_GUI);
    mth.GetInlet(0)->SetList("a b c d", YSE::T_GUI);
    CHECK_FALSE(zeroBased.left.gotList);
    CHECK_FALSE(zeroBased.right.gotList);

    zeroBased.reset();
    mth.GetInlet(1)->SetInt(-1, YSE::T_GUI);
    mth.GetInlet(0)->SetList("a b c d", YSE::T_GUI);
    CHECK_FALSE(zeroBased.left.gotList);
  }

  // ─── slice (#525) ───────────────────────────────────────────────────────────

  TEST_CASE("zl slice: cuts the list in two, right outlet first (#525)") {
    std::vector<char> log;
    OrderSink left;
    OrderSink right;
    left.log = &log;
    left.tag = 'L';
    right.log = &log;
    right.tag = 'R';

    gZl obj;
    obj.SetParams("slice 2");
    TestHelpers::Wire(obj, 0, left);
    TestHelpers::Wire(obj, 1, right);

    obj.GetInlet(0)->SetList("a b c d", YSE::T_GUI);
    REQUIRE(log.size() == 2);
    // Max says so explicitly for this mode: "Lists are sent out the right
    // outlet first".
    CHECK(log[0] == 'R');
    CHECK(log[1] == 'L');
    CHECK(left.lastList == "a b");
    CHECK(right.lastList == "c d");
  }

  TEST_CASE("zl slice: the count is clamped to the list, not refused (#525)") {
    // The argument is a *count* rather than an index, so unlike `nth` there is
    // no such thing as one that names nothing: every value cuts the list
    // somewhere, and the two ends are a whole list plus an empty remainder.
    Rig rig;
    gZl obj;
    obj.SetParams("slice");
    rig.Wire(obj);

    // 0: everything is "the rest".
    obj.GetInlet(1)->SetInt(0, YSE::T_GUI);
    obj.GetInlet(0)->SetList("a b c", YSE::T_GUI);
    CHECK_FALSE(rig.left.gotList);
    CHECK(rig.right.listValue == "a b c");

    // Negative reads as 0 rather than counting backwards: a negative count is
    // not a direction, it is a number of items that cannot exist.
    rig.reset();
    obj.GetInlet(1)->SetInt(-5, YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.left.gotList);
    CHECK(rig.right.listValue == "a b c");

    // Past the end: the whole list left, nothing right.
    rig.reset();
    obj.GetInlet(1)->SetInt(9, YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.listValue == "a b c");
    CHECK_FALSE(rig.right.gotList);

    // A one-item piece leaves as the value it spells, not as a list of one.
    rig.reset();
    obj.GetInlet(1)->SetInt(1, YSE::T_GUI);
    obj.GetInlet(0)->SetList("11 22 33", YSE::T_GUI);
    CHECK(rig.left.gotInt);
    CHECK(rig.left.intValue == 11);
    CHECK(rig.right.listValue == "22 33");
  }

  // ─── sub (#525) ─────────────────────────────────────────────────────────────

  TEST_CASE("zl sub: reports the 1-based position of each occurrence (#525)") {
    Rig rig;
    gZl obj;
    obj.SetParams("sub");
    rig.Wire(obj);

    // The pattern arrives cold on the right inlet and emits nothing.
    obj.GetInlet(1)->SetList("b c", YSE::T_GUI);
    CHECK(obj.ArgumentAtoms() == 2);
    CHECK_FALSE(rig.left.gotList);
    CHECK_FALSE(rig.right.gotInt);

    obj.GetInlet(0)->SetList("a b c d b c", YSE::T_GUI);
    // `b c` starts at item 2 and again at item 5 — 1-based, which is exactly
    // what `nth` takes.
    CHECK(rig.left.gotList);
    CHECK(rig.left.listValue == "2 5");
    CHECK(rig.right.gotInt);
    CHECK(rig.right.intValue == 2);
  }

  TEST_CASE("zl sub: occurrences may overlap (#525)") {
    // "The position of each occurrence" is a search, and a search that skipped
    // the overlapping one would under-report exactly the repetitive material a
    // patch searches a list for.
    Rig rig;
    gZl obj;
    obj.SetParams("sub 1 1");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("1 1 1 1", YSE::T_GUI);
    CHECK(rig.left.listValue == "1 2 3");
    CHECK(rig.right.intValue == 3);
  }

  TEST_CASE("zl sub: no match answers 0, no pattern answers nothing (#525)") {
    // The two silences a patch has to be able to tell apart, and the reason the
    // count goes out even when it is 0: the left outlet says nothing in either
    // case.
    Rig rig;
    gZl obj;
    obj.SetParams("sub");
    rig.Wire(obj);

    // No pattern set yet: unconfigured, so nothing at all.
    obj.GetInlet(0)->SetList("a b c", YSE::T_GUI);
    CHECK_FALSE(rig.left.gotList);
    CHECK_FALSE(rig.right.gotInt);

    // A pattern that is not there: searched, found none.
    rig.reset();
    obj.GetInlet(1)->SetList("x y", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.left.gotList);
    CHECK(rig.right.gotInt);
    CHECK(rig.right.intValue == 0);

    // A pattern longer than the list is the same answer, not a wild read.
    rig.reset();
    obj.GetInlet(1)->SetList("a b c d e", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.right.intValue == 0);
    CHECK_FALSE(rig.left.gotList);
  }

  TEST_CASE("zl sub: matches by atom, so 1 and 1.0 are the same number (#525)") {
    // The same number/symbol split the sort makes, and the two have to agree:
    // a list that has been through an arithmetic object routinely spells its
    // integers with a decimal point.
    Rig rig;
    gZl obj;
    obj.SetParams("sub");
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("1.0", YSE::T_GUI);
    obj.GetInlet(0)->SetList("5 1 5", YSE::T_GUI);
    CHECK(rig.left.gotInt);
    CHECK(rig.left.intValue == 2);
    CHECK(rig.right.intValue == 1);

    // A symbol never matches a number that merely spells the same characters
    // the other way round, and a symbol match is by characters.
    rig.reset();
    obj.GetInlet(1)->SetList("hi", YSE::T_GUI);
    obj.GetInlet(0)->SetList("hi there hi", YSE::T_GUI);
    CHECK(rig.left.listValue == "1 3");

    rig.reset();
    obj.GetInlet(1)->SetList("hi", YSE::T_GUI);
    obj.GetInlet(0)->SetList("high hit", YSE::T_GUI);
    // A prefix is not the atom: atoms match whole or not at all.
    CHECK(rig.right.intValue == 0);
    CHECK_FALSE(rig.left.gotInt);
  }

  // ─── lookup (#525) ──────────────────────────────────────────────────────────

  TEST_CASE("zl lookup: reads the right inlet's table by 1-based index (#525)") {
    Rig rig;
    gZl obj;
    obj.SetParams("lookup");
    rig.Wire(obj);

    // The table arrives cold and emits nothing.
    obj.GetInlet(1)->SetList("do re mi fa", YSE::T_GUI);
    CHECK(obj.ArgumentAtoms() == 4);
    CHECK_FALSE(rig.left.gotList);

    // A single index in is a single entry out, as the value it spells.
    obj.GetInlet(0)->SetInt(3, YSE::T_GUI);
    CHECK(rig.left.gotList);
    CHECK(rig.left.listValue == "mi");

    // A list of indices is a list of entries, in the order asked for, and an
    // entry may be named twice.
    rig.reset();
    obj.GetInlet(0)->SetList("4 1 1", YSE::T_GUI);
    CHECK(rig.left.listValue == "fa do do");

    // The table is not consumed: a bang looks the same indices up again.
    rig.reset();
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.listValue == "fa do do");

    // Single result, so the right outlet says nothing at all.
    CHECK_FALSE(rig.right.gotList);
    CHECK_FALSE(rig.right.gotInt);
  }

  TEST_CASE("zl lookup: a bad index drops its own element, as indexmap does (#525)") {
    // Elementwise, because the stored list is a run of independent lookups —
    // the refusal rule `indexmap` already follows, and `lookup` is `indexmap`
    // with the two lists swapped.
    Rig rig;
    gZl obj;
    obj.SetParams("lookup");
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("do re mi", YSE::T_GUI);
    obj.GetInlet(0)->SetList("2 0 9 hello 1", YSE::T_GUI);
    // 0 is before the first entry, 9 is past the last, and `hello` is not an
    // index at all; the two that name something still arrive.
    CHECK(rig.left.listValue == "re do");

    // Nothing survivable at all sends nothing rather than an empty message.
    rig.reset();
    obj.GetInlet(0)->SetList("0 42", YSE::T_GUI);
    CHECK_FALSE(rig.left.gotList);
    CHECK_FALSE(rig.left.gotInt);

    // No table yet is the unconfigured case, and is also silent.
    Rig bare;
    gZl empty;
    empty.SetParams("lookup");
    bare.Wire(empty);
    empty.GetInlet(0)->SetList("1 2", YSE::T_GUI);
    CHECK_FALSE(bare.left.gotList);
  }

  TEST_CASE("zl lookup: is indexmap with the two lists swapped (#525)") {
    // The claim the mode is built on, and the reason it is 1-based like the
    // rest of the object rather than 0-based like Max's: the same two lists
    // fed to the two modes the two ways round must give the same answer, or a
    // patch that reaches for whichever one fits its cords gets a different
    // result for no reason it can see.
    Rig viaLookup;
    Rig viaIndexMap;
    gZl lookup;
    gZl indexmap;
    lookup.SetParams("lookup");
    indexmap.SetParams("indexmap");
    viaLookup.Wire(lookup);
    viaIndexMap.Wire(indexmap);

    lookup.GetInlet(1)->SetList("a b c d", YSE::T_GUI);
    lookup.GetInlet(0)->SetList("3 1 4", YSE::T_GUI);

    indexmap.GetInlet(1)->SetList("3 1 4", YSE::T_GUI);
    indexmap.GetInlet(0)->SetList("a b c d", YSE::T_GUI);

    CHECK(viaLookup.left.listValue == "c a d");
    CHECK(viaLookup.left.listValue == viaIndexMap.left.listValue);
  }

  TEST_CASE("zl: a symbolic right-inlet list is an argument, not a malformed one (#525)") {
    // The two halves of the right inlet are refused differently on purpose: the
    // numeric argument survives a list with no numbers in it, while the atoms
    // are simply whatever last arrived — for `sub` and `lookup` a list of
    // symbols is the ordinary argument rather than a mistake.
    gZl obj;
    obj.SetParams("swap 1 3");

    obj.GetInlet(1)->SetList("do re mi", YSE::T_GUI);
    CHECK(obj.ArgumentCount() == 2);
    CHECK(obj.ArgumentAt(0) == 1);
    CHECK(obj.ArgumentAt(1) == 3);
    CHECK(obj.ArgumentAtoms() == 3);

    // And a numeric list sets both.
    obj.GetInlet(1)->SetList("7 8", YSE::T_GUI);
    CHECK(obj.ArgumentCount() == 2);
    CHECK(obj.ArgumentAt(0) == 7);
    CHECK(obj.ArgumentAtoms() == 2);

    // A bare number is a list of one here too, so a `sub` told 3 searches for
    // the one-item list `3`.
    obj.GetInlet(1)->SetInt(3, YSE::T_GUI);
    CHECK(obj.ArgumentCount() == 1);
    CHECK(obj.ArgumentAtoms() == 1);
  }

  TEST_CASE("zl: the creation arguments carry a symbolic table too (#525)") {
    // Max's `function-list` argument for `lookup`, and the reason the creation
    // arguments are read as atoms as well as as numbers.
    Rig rig;
    gZl obj;
    obj.SetParams("lookup do re mi");
    rig.Wire(obj);

    CHECK(obj.CurrentMode() == Mode::LOOKUP);
    CHECK(obj.ArgumentAtoms() == 3);
    // No numbers in it, so the numeric argument is honestly empty rather than
    // holding whatever a previous SetParams left.
    CHECK(obj.ArgumentCount() == 0);

    obj.GetInlet(0)->SetList("2 3", YSE::T_GUI);
    CHECK(rig.left.listValue == "re mi");

    // A re-type is a full reconfiguration: the old table does not survive it.
    obj.SetParams("sub 60 64");
    CHECK(obj.CurrentMode() == Mode::SUB);
    CHECK(obj.ArgumentAtoms() == 2);
    CHECK(obj.ArgumentCount() == 2);
    CHECK(obj.ArgumentAt(0) == 60);
  }

  TEST_CASE("zl: a list on the right inlet with no numbers leaves the argument standing (#524)") {
    // A cord that delivers an occasional symbol should not silently un-point a
    // `swap`.
    Rig rig;
    gZl obj;
    obj.SetParams("swap 1 3");
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("hello there", YSE::T_GUI);
    CHECK(obj.ArgumentCount() == 2);
    CHECK(obj.ArgumentAt(0) == 1);
    CHECK(obj.ArgumentAt(1) == 3);

    obj.GetInlet(0)->SetList("a b c", YSE::T_GUI);
    CHECK(rig.left.listValue == "c b a");
  }

  // ─── thin (#526) ────────────────────────────────────────────────────────────

  TEST_CASE("zl thin: drops every repeat after the first, keeping the order (#526)") {
    // Max: "all the elements of the input list which are not duplicates." The
    // survivors stay where they were rather than coming back sorted, which is
    // what the stability of the shared sort buys — see the class notes.
    Rig rig;
    gZl obj;
    obj.SetParams("thin");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("3 1 3 2 1 3", YSE::T_GUI);
    CHECK(rig.left.listValue == "3 1 2");
    // A single result: the right outlet says nothing at all.
    CHECK_FALSE(rig.right.gotList);
    CHECK_FALSE(rig.right.gotInt);
    CHECK_FALSE(rig.right.gotBang);

    // Symbols too, by their characters.
    obj.GetInlet(0)->SetList("do re do mi re", YSE::T_GUI);
    CHECK(rig.left.listValue == "do re mi");

    // The same atom equality the sort and `sub` use: 1 and 1.0 are the same
    // number, so the second spelling is the duplicate.
    obj.GetInlet(0)->SetList("1 1.0 2", YSE::T_GUI);
    CHECK(rig.left.listValue == "1 2");

    // A result of one atom leaves as the value it spells, not as a list of one.
    rig.reset();
    obj.GetInlet(0)->SetList("5 5 5", YSE::T_GUI);
    CHECK(rig.left.gotInt);
    CHECK(rig.left.intValue == 5);
    CHECK_FALSE(rig.left.gotList);

    // Nothing stored is nothing to send.
    rig.reset();
    obj.GetInlet(0)->SetList("zlclear", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.left.gotList);
    CHECK_FALSE(rig.left.gotInt);
  }

  TEST_CASE("zl thin: a full-length list of repeats collapses to its distinct atoms (#526)") {
    // The size at which a nested scan would cost sixty-five thousand
    // comparisons, which is why membership goes through an ordering.
    Rig rig;
    gZl obj;
    obj.SetParams("thin");
    rig.Wire(obj);

    // Digits() wraps at 9, so a 256-atom list holds each of 1..9 many times
    // over and the first nine positions are already all of them.
    obj.GetInlet(0)->SetList(Digits(AtomList::MAX_ATOMS), YSE::T_GUI);
    CHECK(rig.left.listValue == "1 2 3 4 5 6 7 8 9");
  }

  // ─── sect (#526) ────────────────────────────────────────────────────────────

  TEST_CASE("zl sect: sends what the two lists share, once each (#526)") {
    Rig rig;
    gZl obj;
    obj.SetParams("sect");
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("62 64 66", YSE::T_GUI);
    obj.GetInlet(0)->SetList("60 62 64 65", YSE::T_GUI);
    CHECK(rig.left.listValue == "62 64");
    // Something in common, so no bang.
    CHECK_FALSE(rig.right.gotBang);

    // A set: an atom the stored list holds twice is still sent once, and the
    // order is the stored list's rather than the argument's.
    obj.GetInlet(0)->SetList("64 62 64", YSE::T_GUI);
    CHECK(rig.left.listValue == "64 62");

    // ...and an atom the *argument* holds twice does not double it either.
    obj.GetInlet(1)->SetList("62 62 64", YSE::T_GUI);
    obj.GetInlet(0)->SetList("62 64", YSE::T_GUI);
    CHECK(rig.left.listValue == "62 64");
  }

  TEST_CASE("zl sect: nothing in common bangs the right outlet (#526)") {
    // Max: "the right outlet outputs a bang if the two input lists share no
    // common elements." The left outlet is silent on an empty result, so the
    // bang is the only thing that tells a patch the object ran at all.
    Rig rig;
    gZl obj;
    obj.SetParams("sect");
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("62 64", YSE::T_GUI);
    obj.GetInlet(0)->SetList("70 71 72", YSE::T_GUI);
    CHECK(rig.right.gotBang);
    CHECK_FALSE(rig.left.gotList);
    CHECK_FALSE(rig.left.gotInt);

    // No second list at all is an empty intersection too — there is nothing to
    // have in common with.
    Rig bare;
    gZl empty;
    empty.SetParams("sect");
    bare.Wire(empty);
    empty.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    CHECK(bare.right.gotBang);
    CHECK_FALSE(bare.left.gotList);
  }

  // ─── union (#526) ───────────────────────────────────────────────────────────

  TEST_CASE("zl union: adds the two lists together, shared atoms appearing once (#526)") {
    // Max: "contains the contents of both input lists. If the left and right
    // inlets contain any items in common, only one symbol will be output."
    Rig rig;
    gZl obj;
    obj.SetParams("union");
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("3 4 5", YSE::T_GUI);
    obj.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    CHECK(rig.left.listValue == "1 2 3 4 5");
    CHECK_FALSE(rig.right.gotList);
    CHECK_FALSE(rig.right.gotBang);

    // A set on both sides: repeats inside either list collapse too.
    obj.GetInlet(1)->SetList("2 2 9", YSE::T_GUI);
    obj.GetInlet(0)->SetList("1 1 2", YSE::T_GUI);
    CHECK(rig.left.listValue == "1 2 9");

    // With nothing on the right it is `thin`, and with nothing stored it is the
    // right inlet's list thinned.
    Rig alone;
    gZl one;
    one.SetParams("union");
    alone.Wire(one);
    one.GetInlet(0)->SetList("1 1 2", YSE::T_GUI);
    CHECK(alone.left.listValue == "1 2");

    Rig other;
    gZl two;
    two.SetParams("union");
    other.Wire(two);
    two.GetInlet(1)->SetList("a b a", YSE::T_GUI);
    two.GetInlet(0)->SetList("zlclear", YSE::T_GUI);
    two.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(other.left.listValue == "a b");
  }

  // ─── unique and filter (#526) ───────────────────────────────────────────────

  TEST_CASE(
      "zl unique: removes the atoms the right inlet names, keeping the rest as it is (#526)") {
    // Max: "items from the left-input-list which were not present in the
    // right-input-list." A filter rather than a set operation, so duplicates
    // and positions survive.
    Rig rig;
    gZl obj;
    obj.SetParams("unique");
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("2 4", YSE::T_GUI);
    obj.GetInlet(0)->SetList("1 2 3 4 5", YSE::T_GUI);
    CHECK(rig.left.listValue == "1 3 5");
    // A single result: nothing on the right.
    CHECK_FALSE(rig.right.gotList);
    CHECK_FALSE(rig.right.gotInt);

    // Repeats of a surviving atom all survive — this is not `thin`.
    obj.GetInlet(0)->SetList("1 1 2 1", YSE::T_GUI);
    CHECK(rig.left.listValue == "1 1 1");

    // Everything removed is an empty result, and an empty result sends nothing.
    rig.reset();
    obj.GetInlet(0)->SetList("2 4 2", YSE::T_GUI);
    CHECK_FALSE(rig.left.gotList);
    CHECK_FALSE(rig.left.gotInt);

    // Nothing to remove is the list itself, which is a meaningful answer rather
    // than an unconfigured one — unlike `sub`, which has nothing to search for.
    Rig bare;
    gZl empty;
    empty.SetParams("unique");
    bare.Wire(empty);
    empty.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    CHECK(bare.left.listValue == "1 2 3");
  }

  TEST_CASE("zl filter: selects as unique does and reports the 1-based positions (#526)") {
    // The two modes are one selection in Max, and the right outlet is the whole
    // difference: `filter` says *where* the survivors were, which is what makes
    // it compose with `nth`.
    Rig rig;
    gZl obj;
    obj.SetParams("filter 61");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("60 61 62 63", YSE::T_GUI);
    CHECK(rig.left.listValue == "60 62 63");
    CHECK(rig.right.listValue == "1 3 4");

    // The same selection a `unique` with the same argument makes.
    Rig same;
    gZl other;
    other.SetParams("unique 61");
    same.Wire(other);
    other.GetInlet(0)->SetList("60 61 62 63", YSE::T_GUI);
    CHECK(same.left.listValue == rig.left.listValue);
    // ...and the right outlet is where they part company.
    CHECK_FALSE(same.right.gotList);

    // One survivor: a position leaves as the int it spells, not a list of one.
    rig.reset();
    obj.GetInlet(1)->SetList("60 62 63", YSE::T_GUI);
    obj.GetInlet(0)->SetList("60 61 62 63", YSE::T_GUI);
    CHECK(rig.right.gotInt);
    CHECK(rig.right.intValue == 2);
    CHECK(rig.left.gotInt);
    CHECK(rig.left.intValue == 61);

    // Nothing survives: both outlets are silent, the family's empty-result rule.
    rig.reset();
    obj.GetInlet(0)->SetList("60 62", YSE::T_GUI);
    CHECK_FALSE(rig.left.gotInt);
    CHECK_FALSE(rig.left.gotList);
    CHECK_FALSE(rig.right.gotInt);
    CHECK_FALSE(rig.right.gotList);
  }

  TEST_CASE("zl filter: the positions arrive before the filtered list (#526)") {
    // Max's right-to-left rule, and here it is what lets a patch point a second
    // object at the positions before the list that sets it running arrives —
    // `sort`'s index map, in the shape this mode gives it.
    std::vector<char> log;
    OrderSink left;
    OrderSink right;
    left.log = &log;
    left.tag = 'L';
    right.log = &log;
    right.tag = 'R';

    gZl obj;
    obj.SetParams("filter 61");
    TestHelpers::Wire(obj, 0, left);
    TestHelpers::Wire(obj, 1, right);

    obj.GetInlet(0)->SetList("60 61 62", YSE::T_GUI);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == 'R');
    CHECK(log[1] == 'L');
  }

  // ─── compare (#526) ─────────────────────────────────────────────────────────

  TEST_CASE("zl compare: answers 1 or 0, and says where two lists differ (#526)") {
    Rig rig;
    gZl obj;
    obj.SetParams("compare");
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("1 2 3", YSE::T_GUI);
    obj.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    CHECK(rig.left.gotInt);
    CHECK(rig.left.intValue == 1);
    // No differing positions to name, so the right outlet says nothing.
    CHECK_FALSE(rig.right.gotInt);
    CHECK_FALSE(rig.right.gotList);

    // One position differs, and it leaves as the int it spells.
    rig.reset();
    obj.GetInlet(0)->SetList("1 9 3", YSE::T_GUI);
    CHECK(rig.left.intValue == 0);
    CHECK(rig.right.gotInt);
    CHECK(rig.right.intValue == 2);

    // Several, 1-based, as list text.
    rig.reset();
    obj.GetInlet(0)->SetList("9 2 9", YSE::T_GUI);
    CHECK(rig.left.intValue == 0);
    CHECK(rig.right.listValue == "1 3");

    // The same atom equality the rest of the object uses: 1 and 1.0 match.
    rig.reset();
    obj.GetInlet(0)->SetList("1.0 2 3", YSE::T_GUI);
    CHECK(rig.left.intValue == 1);
  }

  TEST_CASE("zl compare: a length difference differs at every position past the shorter (#526)") {
    // The mode asks whether two lists are the *same list*, so a missing tail is
    // a difference rather than a separate kind of answer.
    Rig rig;
    gZl obj;
    obj.SetParams("compare 1 2 3");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("1 2", YSE::T_GUI);
    CHECK(rig.left.intValue == 0);
    CHECK(rig.right.gotInt);
    CHECK(rig.right.intValue == 3);

    rig.reset();
    obj.GetInlet(0)->SetList("1 2 3 4 5", YSE::T_GUI);
    CHECK(rig.left.intValue == 0);
    CHECK(rig.right.listValue == "4 5");
  }

  // ─── change (#526) ──────────────────────────────────────────────────────────

  TEST_CASE("zl change: passes a list on only when it is not the one before it (#526)") {
    Rig rig;
    gZl obj;
    obj.SetParams("change");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    CHECK(rig.left.listValue == "1 2 3");
    CHECK(rig.right.gotInt);
    CHECK(rig.right.intValue == 1);

    // The same list again is not news, and the 0 is the only thing sent — which
    // is what makes the left outlet's silence readable.
    rig.reset();
    obj.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    CHECK_FALSE(rig.left.gotList);
    CHECK(rig.right.gotInt);
    CHECK(rig.right.intValue == 0);

    rig.reset();
    obj.GetInlet(0)->SetList("1 2 4", YSE::T_GUI);
    CHECK(rig.left.listValue == "1 2 4");
    CHECK(rig.right.intValue == 1);

    // A bang answers 0: the stored list has already become the reference, so
    // asking again is asking about the same list twice.
    rig.reset();
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK_FALSE(rig.left.gotList);
    CHECK(rig.right.intValue == 0);
  }

  TEST_CASE("zl change: the right inlet primes the list it compares against (#526)") {
    // Max: the right inlet "receives lists that set the comparison reference".
    // So the reference is the mode's argument, living where every other mode's
    // argument lives — and `change` is the one mode that then keeps it up to
    // date itself.
    Rig rig;
    gZl obj;
    obj.SetParams("change");
    rig.Wire(obj);

    obj.GetInlet(1)->SetList("1 2 3", YSE::T_GUI);
    CHECK(obj.ArgumentAtoms() == 3);
    // Setting it emits nothing, the right inlet being cold.
    CHECK_FALSE(rig.right.gotInt);

    obj.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    CHECK_FALSE(rig.left.gotList);
    CHECK(rig.right.intValue == 0);

    // ...and the creation arguments prime it too.
    Rig primed;
    gZl armed;
    armed.SetParams("change 5 6");
    primed.Wire(armed);
    armed.GetInlet(0)->SetList("5 6", YSE::T_GUI);
    CHECK_FALSE(primed.left.gotList);
    CHECK(primed.right.intValue == 0);
  }

  TEST_CASE("zl change: the two readings of the right inlet's list stay in step (#526)") {
    // `change` writes the reference itself, and the numeric reading of the
    // right inlet's list has to follow it rather than describing a list that is
    // no longer there.
    gZl obj;
    obj.SetParams("change 7 8");
    CHECK(obj.ArgumentCount() == 2);
    CHECK(obj.ArgumentAt(0) == 7);

    obj.GetInlet(0)->SetList("do re mi", YSE::T_GUI);
    CHECK(obj.ArgumentAtoms() == 3);
    // No numbers in the new reference, so the numeric reading is honestly empty
    // rather than still holding 7 8.
    CHECK(obj.ArgumentCount() == 0);
    CHECK(obj.Argument() == 0);

    obj.GetInlet(0)->SetList("3 4 5", YSE::T_GUI);
    CHECK(obj.ArgumentCount() == 3);
    CHECK(obj.ArgumentAt(0) == 3);
  }

  TEST_CASE("zl change: the flag arrives before the list (#526)") {
    std::vector<char> log;
    OrderSink left;
    OrderSink right;
    left.log = &log;
    left.tag = 'L';
    right.log = &log;
    right.tag = 'R';

    gZl obj;
    obj.SetParams("change");
    TestHelpers::Wire(obj, 0, left);
    TestHelpers::Wire(obj, 1, right);

    obj.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == 'R');
    CHECK(log[1] == 'L');
  }

  // ─── mode changes at run time ───────────────────────────────────────────────

  // ─── the structural modes (#527) ────────────────────────────────────────────

  TEST_CASE("zl reg: holds a list, and the right inlet primes it silently (#527)") {
    Rig rig;
    gZl obj;
    obj.SetParams("reg");
    rig.Wire(obj);

    // Max: "a list received in the left inlet is sent out the left outlet
    // immediately."
    obj.GetInlet(0)->SetList("60 64 67", YSE::T_GUI);
    CHECK(rig.left.gotList);
    CHECK(rig.left.listValue == "60 64 67");
    CHECK_FALSE(rig.right.gotList);
    CHECK_FALSE(rig.right.gotBang);

    // "A bang sends the stored list out the left outlet" — and it is the same
    // list rather than a consumed one, so banging twice sends it twice.
    rig.reset();
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.listValue == "60 64 67");
    rig.reset();
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.listValue == "60 64 67");

    // "A list received in the right inlet is stored" — stored, and *not* sent.
    // This is the one mode for which the cold inlet carries contents rather
    // than an argument.
    rig.reset();
    obj.GetInlet(1)->SetList("72 76", YSE::T_GUI);
    CHECK_FALSE(rig.left.gotList);
    CHECK_FALSE(rig.left.gotInt);
    CHECK(obj.Stored() == 2);

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.listValue == "72 76");

    // A bare number on the cold inlet is a list of one there too.
    rig.reset();
    obj.GetInlet(1)->SetInt(5, YSE::T_GUI);
    CHECK_FALSE(rig.left.gotInt);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.gotInt);
    CHECK(rig.left.intValue == 5);

    // The creation arguments prime it, being the same slot.
    gZl primed;
    Rig primedRig;
    primed.SetParams("reg do re mi");
    primedRig.Wire(primed);
    primed.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(primedRig.left.listValue == "do re mi");
  }

  TEST_CASE("zl iter: sends the list out in chunks, the last one short (#527)") {
    // Max: "sent out the left outlet as a series of lists consisting of the
    // number of items specified"; "the final list may be shorter". Several
    // messages from one stimulus, which is why this needs a tallying sink.
    TallyRig rig;
    gZl obj;
    obj.SetParams("iter 2");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("1 2 3 4 5", YSE::T_GUI);
    REQUIRE(rig.left.got.size() == 3);
    CHECK(rig.left.got[0] == "l:1 2");
    CHECK(rig.left.got[1] == "l:3 4");
    // The short last chunk holds one atom, and one atom leaves as the value it
    // spells rather than as a list of one — the family's transport rule.
    CHECK(rig.left.got[2] == "i:5");
    CHECK(rig.right.got.empty());

    // A bang re-runs it over the same list: the mode reads the input register
    // rather than consuming it.
    rig.reset();
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.got.size() == 3);

    // A chunk size that divides the list leaves no short chunk.
    rig.reset();
    obj.GetInlet(1)->SetInt(5, YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(rig.left.got.size() == 1);
    CHECK(rig.left.got[0] == "l:1 2 3 4 5");

    // No chunk size is not a chunk size of one: the object is unconfigured and
    // stays quiet, which is `sub`-without-a-pattern's rule.
    rig.reset();
    obj.GetInlet(1)->SetInt(0, YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.got.empty());
  }

  TEST_CASE("zl join: sends the two lists one after the other (#527)") {
    Rig rig;
    gZl obj;
    obj.SetParams("join 4 5");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    CHECK(rig.left.listValue == "1 2 3 4 5");
    CHECK_FALSE(rig.right.gotList);

    // The right inlet is cold: it re-points the second half without emitting.
    rig.reset();
    obj.GetInlet(1)->SetList("a b", YSE::T_GUI);
    CHECK_FALSE(rig.left.gotList);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.listValue == "1 2 3 a b");

    // With nothing in the right inlet the result is the stored list, not
    // silence: a list with nothing appended to it is the list.
    gZl alone;
    Rig aloneRig;
    alone.SetParams("join");
    aloneRig.Wire(alone);
    alone.GetInlet(0)->SetList("1 2", YSE::T_GUI);
    CHECK(aloneRig.left.listValue == "1 2");
  }

  TEST_CASE("zl lace: interleaves the two lists and keeps the tail of the longer (#527)") {
    Rig rig;
    gZl obj;
    obj.SetParams("lace 3 5.3 2.4");
    rig.Wire(obj);

    // Max's own example: left 6.2 5.6 3.8, right 3 5.3 2.4, out 6.2 3 5.6 5.3
    // 3.8 2.4.
    obj.GetInlet(0)->SetList("6.2 5.6 3.8", YSE::T_GUI);
    CHECK(rig.left.listValue == "6.2 3 5.6 5.3 3.8 2.4");
    CHECK_FALSE(rig.right.gotList);

    // Uneven lists interleave as far as the shorter goes and the tail of the
    // longer follows rather than being dropped — which is what makes `delace`
    // able to give both lists back.
    rig.reset();
    obj.GetInlet(1)->SetList("a b", YSE::T_GUI);
    obj.GetInlet(0)->SetList("1 2 3 4", YSE::T_GUI);
    CHECK(rig.left.listValue == "1 a 2 b 3 4");

    rig.reset();
    obj.GetInlet(1)->SetList("a b c d", YSE::T_GUI);
    obj.GetInlet(0)->SetList("1 2", YSE::T_GUI);
    CHECK(rig.left.listValue == "1 a 2 b c d");
  }

  TEST_CASE("zl delace: pulls a laced list apart, right outlet first (#527)") {
    // Max's own example run backwards: 6.2 3 5.6 5.3 3.8 2.4 gives 6.2 5.6 3.8
    // left and 3 5.3 2.4 right.
    std::vector<char> log;
    OrderSink left;
    OrderSink right;
    left.log = &log;
    left.tag = 'L';
    right.log = &log;
    right.tag = 'R';

    gZl obj;
    obj.SetParams("delace");
    TestHelpers::Wire(obj, 0, left);
    TestHelpers::Wire(obj, 1, right);

    obj.GetInlet(0)->SetList("6.2 3 5.6 5.3 3.8 2.4", YSE::T_GUI);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == 'R');
    CHECK(log[1] == 'L');
    CHECK(left.lastList == "6.2 5.6 3.8");
    CHECK(right.lastList == "3 5.3 2.4");

    // An odd-length list leaves the extra atom on the left, which is where
    // `lace` would have taken it from.
    log.clear();
    obj.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    CHECK(left.lastList == "1 3");
    CHECK(right.lastKind == OrderSink::INT);
    CHECK(right.lastInt == 2);

    // A one-atom list has no odd positions at all, so the right outlet stays
    // silent rather than sending an empty message.
    log.clear();
    obj.GetInlet(0)->SetInt(9, YSE::T_GUI);
    REQUIRE(log.size() == 1);
    CHECK(log[0] == 'L');
    CHECK(left.lastInt == 9);
  }

  TEST_CASE("zl ecils: cuts the list in two counting from the end (#527)") {
    // Max: "the first list contains the number of items specified by the
    // argument beginning from the end of the list … and is sent out the right
    // outlet." `slice` measured from the other end.
    std::vector<char> log;
    OrderSink left;
    OrderSink right;
    left.log = &log;
    left.tag = 'L';
    right.log = &log;
    right.tag = 'R';

    gZl obj;
    obj.SetParams("ecils 2");
    TestHelpers::Wire(obj, 0, left);
    TestHelpers::Wire(obj, 1, right);

    obj.GetInlet(0)->SetList("1 2 3 4 5", YSE::T_GUI);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == 'R');
    CHECK(log[1] == 'L');
    CHECK(left.lastList == "1 2 3");
    CHECK(right.lastList == "4 5");

    // A count rather than an index, so it is clamped to the list rather than
    // refused — `slice`'s rule, and the two modes agree about it.
    log.clear();
    obj.GetInlet(1)->SetInt(99, YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(log.size() == 1);
    CHECK(log[0] == 'R');
    CHECK(right.lastList == "1 2 3 4 5");

    log.clear();
    obj.GetInlet(1)->SetInt(0, YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(log.size() == 1);
    CHECK(log[0] == 'L');
    CHECK(left.lastList == "1 2 3 4 5");
  }

  TEST_CASE("zl group: sends each complete group and keeps the remainder (#527)") {
    TallyRig rig;
    gZl obj;
    obj.SetParams("group 3");
    rig.Wire(obj);

    // Nothing until the group is complete.
    obj.GetInlet(0)->SetList("1 2", YSE::T_GUI);
    CHECK(rig.left.got.empty());
    CHECK(obj.Pending() == 2);

    // "The left outlet sends the specified quantity of items; remaining
    // elements stay stored."
    obj.GetInlet(0)->SetList("3 4", YSE::T_GUI);
    REQUIRE(rig.left.got.size() == 1);
    CHECK(rig.left.got[0] == "l:1 2 3");
    CHECK(obj.Pending() == 1);

    // Every complete group a single message carries, not just the first — an
    // object that emitted one per message would fall behind on long lists.
    rig.reset();
    obj.GetInlet(0)->SetList("5 6 7 8 9", YSE::T_GUI);
    REQUIRE(rig.left.got.size() == 2);
    CHECK(rig.left.got[0] == "l:4 5 6");
    CHECK(rig.left.got[1] == "l:7 8 9");
    CHECK(obj.Pending() == 0);
    CHECK(rig.right.got.empty());
  }

  TEST_CASE("zl group: a bang flushes the partial group (#527)") {
    // Max: "bang outputs the most recent stored items". The bang is not a
    // re-run here — it is the flush, and it empties the accumulator, because a
    // flush that left the atoms behind would send them again as part of the
    // next complete group.
    TallyRig rig;
    gZl obj;
    obj.SetParams("group 4");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    REQUIRE(rig.left.got.empty());

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(rig.left.got.size() == 1);
    CHECK(rig.left.got[0] == "l:1 2 3");
    CHECK(obj.Pending() == 0);

    // Nothing left to flush: silence rather than an empty message.
    rig.reset();
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.got.empty());

    // With no group size at all the atoms simply accumulate — there is nothing
    // to compare their number against — and a bang still gets them out.
    rig.reset();
    gZl loose;
    TallyRig looseRig;
    loose.SetParams("group");
    looseRig.Wire(loose);
    loose.GetInlet(0)->SetList("1 2 3 4 5 6", YSE::T_GUI);
    CHECK(looseRig.left.got.empty());
    CHECK(loose.Pending() == 6);
    loose.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(looseRig.left.got.size() == 1);
    CHECK(looseRig.left.got[0] == "l:1 2 3 4 5 6");
  }

  TEST_CASE("zl stream: keeps a sliding window and reports the shortfall (#527)") {
    Rig rig;
    gZl obj;
    obj.SetParams("stream 3");
    rig.Wire(obj);

    // Filling: nothing out the left outlet, and the right one says how many
    // more atoms the window still wants — the signal that makes the left
    // outlet's silence readable.
    obj.GetInlet(0)->SetInt(1, YSE::T_GUI);
    CHECK_FALSE(rig.left.gotInt);
    CHECK_FALSE(rig.left.gotList);
    CHECK(rig.right.gotInt);
    CHECK(rig.right.intValue == 2);

    rig.reset();
    obj.GetInlet(0)->SetInt(2, YSE::T_GUI);
    CHECK(rig.right.intValue == 1);
    CHECK_FALSE(rig.left.gotList);

    rig.reset();
    obj.GetInlet(0)->SetInt(3, YSE::T_GUI);
    CHECK(rig.right.intValue == 0);
    CHECK(rig.left.listValue == "1 2 3");

    // Sliding, not re-collecting: every further arrival sends the last three.
    rig.reset();
    obj.GetInlet(0)->SetInt(4, YSE::T_GUI);
    CHECK(rig.left.listValue == "2 3 4");
    CHECK(obj.Pending() == 3);

    // A bang re-sends the window without sliding it — the accumulating modes'
    // bang consumes or re-reads, never pushes.
    rig.reset();
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.listValue == "2 3 4");
    CHECK(obj.Pending() == 3);

    // A whole list pushes all of its atoms at once, so a list longer than the
    // window leaves only its tail.
    rig.reset();
    obj.GetInlet(0)->SetList("7 8 9 10 11", YSE::T_GUI);
    CHECK(rig.left.listValue == "9 10 11");

    // No window length is an unconfigured object: silent on both outlets, and
    // holding nothing, so a length arriving later starts clean.
    rig.reset();
    obj.GetInlet(1)->SetInt(0, YSE::T_GUI);
    obj.GetInlet(0)->SetInt(5, YSE::T_GUI);
    CHECK_FALSE(rig.left.gotList);
    CHECK_FALSE(rig.right.gotInt);
    CHECK(obj.Pending() == 0);

    // A window as wide as the whole accumulator still slides. Room is made
    // before the atoms are collected, so the store never has to refuse the very
    // ones whose job is to push its oldest out — get that backwards and the
    // window freezes the moment it fills, which is the widest window's only
    // failure mode and the one nobody would look for.
    Rig fullRig;
    gZl full;
    full.SetParams("4 stream 4");
    fullRig.Wire(full);

    full.GetInlet(0)->SetList("1 2 3 4", YSE::T_GUI);
    CHECK(fullRig.left.listValue == "1 2 3 4");
    fullRig.reset();
    full.GetInlet(0)->SetInt(5, YSE::T_GUI);
    CHECK(fullRig.left.listValue == "2 3 4 5");
    fullRig.reset();
    full.GetInlet(0)->SetList("6 7", YSE::T_GUI);
    CHECK(fullRig.left.listValue == "4 5 6 7");
    CHECK(full.Dropped() == 0);
  }

  TEST_CASE("zl group: a long list is collected in bites and drained between them (#527)") {
    // The accumulator is bounded by the working maximum list length, and a
    // leftover partial group plus a full list is more than that. Collecting in
    // one go would refuse the tail — atoms the object has ample room for, since
    // it hands most of them straight back out. Driven at a limit of 4 so the
    // boundary is reachable in a few messages rather than in hundreds.
    TallyRig rig;
    gZl obj;
    obj.SetParams("4 group 3");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("1 2 3 4", YSE::T_GUI);
    REQUIRE(rig.left.got.size() == 1);
    CHECK(rig.left.got[0] == "l:1 2 3");
    REQUIRE(obj.Pending() == 1);

    // A remainder of 1 plus a full 4 is 5 atoms through a store that holds 4.
    rig.reset();
    obj.GetInlet(0)->SetList("5 6 7 8", YSE::T_GUI);
    REQUIRE(rig.left.got.size() == 1);
    CHECK(rig.left.got[0] == "l:4 5 6");
    CHECK(obj.Pending() == 2);
    CHECK(obj.Dropped() == 0);

    // And the two it is still holding are the ones it should be.
    rig.reset();
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(rig.left.got.size() == 1);
    CHECK(rig.left.got[0] == "l:7 8");
  }

  TEST_CASE("zl queue: a bang pops the oldest atom, and bangs the right outlet when empty (#527)") {
    Rig rig;
    gZl obj;
    obj.SetParams("queue");
    rig.Wire(obj);

    // Pushing is not popping: an arrival stores and sends nothing at all.
    obj.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    CHECK_FALSE(rig.left.gotInt);
    CHECK_FALSE(rig.left.gotList);
    CHECK(obj.Pending() == 3);

    // "Functions as a first-in-first-out (FIFO) stack; it outputs the oldest
    // message received" — one atom at a time, which is the unit everything in
    // this object works in.
    for (int expected = 1; expected <= 3; expected++) {
      rig.reset();
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
      CHECK(rig.left.gotInt);
      CHECK(rig.left.intValue == expected);
      CHECK_FALSE(rig.right.gotBang);
    }
    CHECK(obj.Pending() == 0);

    // Empty: a bang out the right outlet, which is `sect`'s answer to the same
    // question and what lets a patch drain the queue by banging until it
    // answers.
    rig.reset();
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.right.gotBang);
    CHECK_FALSE(rig.left.gotInt);

    // Symbols go through as symbols.
    rig.reset();
    obj.GetInlet(0)->SetList("do re", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.gotList);
    CHECK(rig.left.listValue == "do");
  }

  TEST_CASE("zl stack: a bang pops the newest atom (#527)") {
    Rig rig;
    gZl obj;
    obj.SetParams("stack");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    CHECK_FALSE(rig.left.gotInt);
    CHECK(obj.Pending() == 3);

    // "Last-in-first-out … it outputs the most recently received message."
    for (int expected = 3; expected >= 1; expected--) {
      rig.reset();
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
      CHECK(rig.left.gotInt);
      CHECK(rig.left.intValue == expected);
    }

    rig.reset();
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.right.gotBang);
  }

  TEST_CASE("zl: the accumulating modes share one store, and zlclear empties it (#527)") {
    // Sharing is deliberate: the four hold the same thing and differ only in
    // when and from which end they consume it, so a live mode switch keeps the
    // material rather than silently starting a second buffer.
    Rig rig;
    gZl obj;
    obj.SetParams("queue");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("1 2 3 4", YSE::T_GUI);
    REQUIRE(obj.Pending() == 4);

    obj.GetInlet(0)->SetList("mode stack", YSE::T_GUI);
    REQUIRE(obj.CurrentMode() == Mode::STACK);
    CHECK(obj.Pending() == 4);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.intValue == 4);

    // `zlclear` is Max's "reinitializes the zl object", and the accumulator is
    // contents rather than configuration — so it goes, and the mode stays.
    obj.GetInlet(0)->SetList("zlclear", YSE::T_GUI);
    CHECK(obj.Pending() == 0);
    CHECK(obj.Stored() == 0);
    CHECK(obj.CurrentMode() == Mode::STACK);

    rig.reset();
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.right.gotBang);

    // Re-typing the creation arguments empties it too, that being a rebuild.
    obj.GetInlet(0)->SetList("1 2", YSE::T_GUI);
    REQUIRE(obj.Pending() == 2);
    obj.SetParams("");
    CHECK(obj.Pending() == 0);
  }

  TEST_CASE("zl queue: pushing and popping forever costs no storage (#527)") {
    // The reason `AtomList::Keep` exists. The backing text is append-only
    // everywhere else, so a queue that only ever shifted its table would walk
    // off the 1024-character ceiling and start refusing atoms it has room for.
    // 400 rounds of two four-character atoms is 3200 characters.
    Rig rig;
    gZl obj;
    obj.SetParams("queue");
    rig.Wire(obj);

    for (int round = 0; round < 400; round++) {
      obj.GetInlet(0)->SetList("1234 5678", YSE::T_GUI);
      rig.reset();
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
      REQUIRE(rig.left.gotInt);
      REQUIRE(rig.left.intValue == 1234);
      rig.reset();
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
      REQUIRE(rig.left.intValue == 5678);
    }
    CHECK(obj.Pending() == 0);
    CHECK(obj.Dropped() == 0);
  }

  TEST_CASE("zl: the accumulator is bounded by the working maximum list length (#527)") {
    // A queue nobody empties stops taking atoms rather than growing — the
    // object's refuse-and-count rule, applied to the one store that survives a
    // message.
    Rig rig;
    gZl obj;
    obj.SetParams("4 queue");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    obj.GetInlet(0)->SetList("4 5 6", YSE::T_GUI);
    CHECK(obj.Pending() == 4);
    CHECK(obj.Dropped() >= 2);
  }

  TEST_CASE("zl: 'mode <name>' switches the mode and keeps the stored list (#523)") {
    // The whole reason the mode is a message as well as an argument: re-typing
    // the arguments rebuilds the object and empties it, which is what re-typing
    // does in Max, while this keeps the list a patch has already sent.
    Rig rig;
    gZl obj;
    obj.SetParams("len");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("5 6 7", YSE::T_GUI);
    REQUIRE(rig.left.intValue == 3);

    rig.reset();
    obj.GetInlet(0)->SetList("mode rev", YSE::T_GUI);
    CHECK(obj.CurrentMode() == Mode::REV);
    // The mode change itself emits nothing.
    CHECK_FALSE(rig.left.gotInt);
    CHECK_FALSE(rig.left.gotList);
    CHECK(obj.Stored() == 3);

    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.gotList);
    CHECK(rig.left.listValue == "7 6 5");
  }

  TEST_CASE("zl: with no mode the object stores its input and emits nothing (#523)") {
    Rig rig;
    gZl obj;
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    CHECK(obj.Stored() == 3);
    CHECK_FALSE(rig.left.gotInt);
    CHECK_FALSE(rig.left.gotList);
    CHECK_FALSE(rig.right.gotList);

    // ...and the list is still there when a mode arrives.
    obj.GetInlet(0)->SetList("mode len", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(rig.left.gotInt);
    CHECK(rig.left.intValue == 3);
  }

  // ─── overflow ───────────────────────────────────────────────────────────────

  TEST_CASE("zl: an over-long list loses its tail and is counted, not truncated silently (#523)") {
    Rig rig;
    gZl obj;
    obj.SetParams("4 len");
    rig.Wire(obj);

    CHECK(obj.Dropped() == 0);
    obj.GetInlet(0)->SetList("1 2 3 4 5 6 7", YSE::T_GUI);
    // The head is kept: four items fitted, three were refused.
    CHECK(rig.left.intValue == 4);
    CHECK(obj.Dropped() == 3);

    // The counter is monotonic and reports the refusal a log line cannot,
    // this being a path the audio callback takes.
    obj.GetInlet(0)->SetList("1 2 3 4 5", YSE::T_GUI);
    CHECK(obj.Dropped() == 4);

    // Within the limit nothing is counted.
    obj.GetInlet(0)->SetList("1 2", YSE::T_GUI);
    CHECK(obj.Dropped() == 4);
    CHECK(rig.left.intValue == 2);
  }

  TEST_CASE("zl: the atom ceiling holds even with no narrower limit (#523)") {
    Rig rig;
    gZl obj;
    obj.SetParams("len");
    rig.Wire(obj);

    // Single-character atoms are the case where the *atom* ceiling bites first
    // — 266 of them span 266 characters of backing text, well inside the 1024
    // the text ceiling allows — so this measures the 256-atom bound on its own.
    obj.GetInlet(0)->SetList(Digits(AtomList::MAX_ATOMS + 10), YSE::T_GUI);
    CHECK(rig.left.intValue == (int)AtomList::MAX_ATOMS);
    CHECK(obj.Dropped() == 10);
  }

  // ─── real-time behaviour ────────────────────────────────────────────────────

  TEST_CASE("zl: Calculate() emits nothing (#523)") {
    // The object is driven by its inlets; one that emitted here would re-send
    // on every DSP tick from a stimulus no patch sent.
    Rig rig;
    gZl obj;
    obj.SetParams("len");
    rig.Wire(obj);

    obj.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    rig.reset();
    for (int i = 0; i < 8; i++)
      obj.Calculate(YSE::T_DSP);
    CHECK_FALSE(rig.left.gotInt);
    CHECK_FALSE(rig.left.gotList);
    CHECK_FALSE(rig.right.gotList);
  }

  TEST_CASE("zl: the message path allocates nothing (#523)") {
    if (!TestHelpers::probeCountsAllocations()) return;
    // The claim covers a path that builds list text, so it only means anything
    // if the probe can see a std::string's own allocations (issue #697).
    if (!TestHelpers::probeSeesStringAllocations()) return;

    Rig rig;
    gZl obj;
    obj.SetParams("nth 2");
    rig.Wire(obj);

    // Warm every buffer the path touches — including the sinks', which are
    // test scaffolding rather than the object under test.
    obj.GetInlet(0)->SetList("111 222 333 444", YSE::T_GUI);
    obj.GetInlet(0)->SetList("mode rev", YSE::T_GUI);
    obj.GetInlet(0)->SetList("111 222 333 444", YSE::T_GUI);
    obj.GetInlet(0)->SetList("mode len", YSE::T_GUI);
    obj.GetInlet(0)->SetBang(YSE::T_GUI);

    const std::string listText = "111 222 333 444";
    const std::string modeRev = "mode rev";
    const std::string modeNth = "mode nth";
    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetList(listText, YSE::T_GUI);
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
      obj.GetInlet(0)->SetList(modeRev, YSE::T_GUI);
      obj.GetInlet(0)->SetList(listText, YSE::T_GUI);
      obj.GetInlet(0)->SetList(modeNth, YSE::T_GUI);
      obj.GetInlet(1)->SetInt(2, YSE::T_GUI);
      obj.GetInlet(0)->SetList(listText, YSE::T_GUI);
      obj.GetInlet(0)->SetInt(5, YSE::T_GUI);
      obj.GetInlet(0)->SetFloat(2.5f, YSE::T_GUI);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  TEST_CASE("zl: the reordering modes allocate nothing either (#524)") {
    if (!TestHelpers::probeCountsAllocations()) return;
    if (!TestHelpers::probeSeesStringAllocations()) return;

    // The claim the whole design rests on: the order array, the merge scratch
    // and the scratch list are members reserved when the object was built, so a
    // sort of a full-length list on the audio thread touches no allocator.
    Rig rig;
    gZl obj;
    obj.SetParams("sort");
    rig.Wire(obj);

    std::string wide;
    for (std::size_t i = 0; i < AtomList::MAX_ATOMS; i++) {
      if (i > 0) wide.push_back(' ');
      wide += std::to_string(AtomList::MAX_ATOMS - i);
    }

    const std::string modeRot = "mode rot";
    const std::string modeScramble = "mode scramble";
    const std::string modeSwap = "mode swap";
    const std::string modeIndexMap = "mode indexmap";
    const std::string modeSort = "mode sort";
    const std::string seed = "zlseed 5";
    const std::string pair = "2 4";
    const std::string map = "3 1 2 2";

    // Warm every buffer the paths touch, the sinks' included.
    for (const std::string* word : {&modeRot, &modeScramble, &modeSwap, &modeIndexMap, &modeSort}) {
      obj.GetInlet(0)->SetList(*word, YSE::T_GUI);
      obj.GetInlet(1)->SetList(map, YSE::T_GUI);
      obj.GetInlet(0)->SetList(wide, YSE::T_GUI);
    }
    obj.GetInlet(0)->SetList(seed, YSE::T_GUI);
    obj.GetInlet(1)->SetList(pair, YSE::T_GUI);

    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetList(modeRot, YSE::T_GUI);
      obj.GetInlet(1)->SetInt(3, YSE::T_GUI);
      obj.GetInlet(0)->SetList(wide, YSE::T_GUI);

      obj.GetInlet(0)->SetList(modeScramble, YSE::T_GUI);
      obj.GetInlet(0)->SetList(seed, YSE::T_GUI);
      obj.GetInlet(0)->SetBang(YSE::T_GUI);

      obj.GetInlet(0)->SetList(modeSort, YSE::T_GUI);
      obj.GetInlet(0)->SetBang(YSE::T_GUI);

      obj.GetInlet(0)->SetList(modeSwap, YSE::T_GUI);
      obj.GetInlet(1)->SetList(pair, YSE::T_GUI);
      obj.GetInlet(0)->SetBang(YSE::T_GUI);

      obj.GetInlet(0)->SetList(modeIndexMap, YSE::T_GUI);
      obj.GetInlet(1)->SetList(map, YSE::T_GUI);
      obj.GetInlet(0)->SetBang(YSE::T_GUI);

      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  TEST_CASE("zl: the extraction modes allocate nothing either (#525)") {
    if (!TestHelpers::probeCountsAllocations()) return;
    if (!TestHelpers::probeSeesStringAllocations()) return;

    // #525 added a third AtomList (the right inlet's list) and a KMP failure
    // array, and both are members reserved when the object was built — so a
    // full-length search on the audio thread touches no allocator either.
    Rig rig;
    gZl obj;
    obj.SetParams("sub");
    rig.Wire(obj);

    // The worst case the KMP walk exists for: a full-length list of identical
    // atoms searched for a long run of the same atom.
    std::string repeated;
    for (std::size_t i = 0; i < AtomList::MAX_ATOMS; i++) {
      if (i > 0) repeated.push_back(' ');
      repeated.push_back('1');
    }
    std::string pattern;
    for (std::size_t i = 0; i < AtomList::MAX_ATOMS / 2; i++) {
      if (i > 0) pattern.push_back(' ');
      pattern.push_back('1');
    }

    const std::string modeMth = "mode mth";
    const std::string modeSlice = "mode slice";
    const std::string modeSub = "mode sub";
    const std::string modeLookup = "mode lookup";

    // Warm every buffer the paths touch, the sinks' included.
    for (const std::string* word : {&modeMth, &modeSlice, &modeSub, &modeLookup}) {
      obj.GetInlet(0)->SetList(*word, YSE::T_GUI);
      obj.GetInlet(1)->SetList(pattern, YSE::T_GUI);
      obj.GetInlet(1)->SetInt(3, YSE::T_GUI);
      obj.GetInlet(0)->SetList(repeated, YSE::T_GUI);
    }

    {
      TestHelpers::ProbeScope probe;
      obj.GetInlet(0)->SetList(modeSub, YSE::T_GUI);
      obj.GetInlet(1)->SetList(pattern, YSE::T_GUI);
      obj.GetInlet(0)->SetList(repeated, YSE::T_GUI);

      obj.GetInlet(0)->SetList(modeLookup, YSE::T_GUI);
      obj.GetInlet(1)->SetList(repeated, YSE::T_GUI);
      obj.GetInlet(0)->SetList(pattern, YSE::T_GUI);

      obj.GetInlet(0)->SetList(modeMth, YSE::T_GUI);
      obj.GetInlet(1)->SetInt(3, YSE::T_GUI);
      obj.GetInlet(0)->SetList(repeated, YSE::T_GUI);

      obj.GetInlet(0)->SetList(modeSlice, YSE::T_GUI);
      obj.GetInlet(0)->SetBang(YSE::T_GUI);

      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  TEST_CASE("zl: the set modes allocate nothing either (#526)") {
    if (!TestHelpers::probeCountsAllocations()) return;
    if (!TestHelpers::probeSeesStringAllocations()) return;

    // The claim the set group's design rests on: the two orderings, the
    // first-occurrence marks and the scratch list are members reserved when the
    // object was built, so intersecting, uniting and comparing two full-length
    // lists on the audio thread touches no allocator — including `union`, which
    // is the one mode that copies atoms rather than reordering them.
    Rig rig;
    gZl obj;
    obj.SetParams("sect");
    rig.Wire(obj);

    // Two full-length lists that overlap in half their atoms, so every path
    // through the membership search is taken. Digits() wraps at 9, so both
    // lists are also full of repeats and the first-occurrence marking runs at
    // full length too.
    const std::string wide = Digits(AtomList::MAX_ATOMS);
    std::string other;
    for (std::size_t i = 0; i < AtomList::MAX_ATOMS; i++) {
      if (i > 0) other.push_back(' ');
      other.push_back((char)('5' + (i % 9)));
    }

    const std::string modeSect = "mode sect";
    const std::string modeUnion = "mode union";
    const std::string modeUnique = "mode unique";
    const std::string modeThin = "mode thin";
    const std::string modeFilter = "mode filter";
    const std::string modeCompare = "mode compare";
    const std::string modeChange = "mode change";

    // Warm every buffer the paths touch, the sinks' included.
    for (const std::string* word :
         {&modeSect, &modeUnion, &modeUnique, &modeThin, &modeFilter, &modeCompare, &modeChange}) {
      obj.GetInlet(0)->SetList(*word, YSE::T_GUI);
      obj.GetInlet(1)->SetList(other, YSE::T_GUI);
      obj.GetInlet(0)->SetList(wide, YSE::T_GUI);
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
    }

    {
      TestHelpers::ProbeScope probe;
      for (const std::string* word : {&modeSect, &modeUnion, &modeUnique, &modeThin, &modeFilter,
                                      &modeCompare, &modeChange}) {
        obj.GetInlet(0)->SetList(*word, YSE::T_GUI);
        obj.GetInlet(1)->SetList(other, YSE::T_GUI);
        obj.GetInlet(0)->SetList(wide, YSE::T_GUI);
        obj.GetInlet(0)->SetBang(YSE::T_GUI);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  TEST_CASE("zl: the structural modes allocate nothing either (#527)") {
    if (!TestHelpers::probeCountsAllocations()) return;
    if (!TestHelpers::probeSeesStringAllocations()) return;

    // #527 added a fourth AtomList — the accumulator the collecting modes share
    // — and one operation that *shortens* a list rather than rebuilding it.
    // Both have to be allocation-free or a `queue` on the audio thread would
    // reach the allocator once per pop, which is the one thing the whole design
    // exists to avoid.
    Rig rig;
    gZl obj;
    obj.SetParams("reg");
    rig.Wire(obj);

    const std::string wide = Digits(AtomList::MAX_ATOMS);
    const std::string other = Digits(AtomList::MAX_ATOMS / 2);

    const std::string modeReg = "mode reg";
    const std::string modeIter = "mode iter";
    const std::string modeJoin = "mode join";
    const std::string modeLace = "mode lace";
    const std::string modeDelace = "mode delace";
    const std::string modeEcils = "mode ecils";
    const std::string modeGroup = "mode group";
    const std::string modeStream = "mode stream";
    const std::string modeQueue = "mode queue";
    const std::string modeStack = "mode stack";
    const std::string clear = "zlclear";

    // One pass drives every path the probe will then re-run: the buffers it
    // warms are the object's render string, the accumulator's text and the
    // sinks', which are test scaffolding rather than the object under test.
    for (int pass = 0; pass < 2; pass++) {
      std::unique_ptr<TestHelpers::ProbeScope> probe;
      if (pass == 1) probe = std::make_unique<TestHelpers::ProbeScope>();

      for (const std::string* word : {&modeReg, &modeJoin, &modeLace, &modeDelace, &modeEcils}) {
        obj.GetInlet(0)->SetList(*word, YSE::T_GUI);
        obj.GetInlet(1)->SetList(other, YSE::T_GUI);
        obj.GetInlet(0)->SetList(wide, YSE::T_GUI);
        obj.GetInlet(0)->SetBang(YSE::T_GUI);
      }

      // The multi-send mode, at its worst: a full-length list cut into chunks
      // of one, so the send path runs 256 times inside a single message.
      obj.GetInlet(0)->SetList(modeIter, YSE::T_GUI);
      obj.GetInlet(1)->SetInt(1, YSE::T_GUI);
      obj.GetInlet(0)->SetList(wide, YSE::T_GUI);

      // The accumulating four, each pushed past its store and drained again, so
      // the Keep path runs in both directions.
      obj.GetInlet(0)->SetList(clear, YSE::T_GUI);
      obj.GetInlet(0)->SetList(modeGroup, YSE::T_GUI);
      obj.GetInlet(1)->SetInt(7, YSE::T_GUI);
      obj.GetInlet(0)->SetList(wide, YSE::T_GUI);
      obj.GetInlet(0)->SetBang(YSE::T_GUI);

      obj.GetInlet(0)->SetList(modeStream, YSE::T_GUI);
      obj.GetInlet(1)->SetInt(4, YSE::T_GUI);
      obj.GetInlet(0)->SetList(wide, YSE::T_GUI);
      obj.GetInlet(0)->SetList(other, YSE::T_GUI);
      obj.GetInlet(0)->SetBang(YSE::T_GUI);

      obj.GetInlet(0)->SetList(clear, YSE::T_GUI);
      obj.GetInlet(0)->SetList(modeQueue, YSE::T_GUI);
      obj.GetInlet(0)->SetList(other, YSE::T_GUI);
      for (int i = 0; i < 20; i++)
        obj.GetInlet(0)->SetBang(YSE::T_GUI);

      obj.GetInlet(0)->SetList(modeStack, YSE::T_GUI);
      obj.GetInlet(0)->SetList(other, YSE::T_GUI);
      for (int i = 0; i < 20; i++)
        obj.GetInlet(0)->SetBang(YSE::T_GUI);

      obj.GetInlet(0)->SetList(clear, YSE::T_GUI);
      if (pass == 1) CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("zl: params survive a DumpJSON / ParseJSON round trip (#523)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_ZL, "64 nth 2") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".zl") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".zl");
    CHECK(copy->GetParams() == std::string("64 nth 2"));
    CHECK(copy->GetInputs() == 2);
    CHECK(copy->GetOutputs() == 2);
  }

  // ─── end to end, through a real patcher graph ───────────────────────────────

  TEST_CASE("zl: picks an item and routes the remainder onward, in a real patch (#523)") {
    // The use case the object exists for, run through the real thing: a list
    // arrives, `.zl nth` splits it into "the one I asked for" and "the rest",
    // and the two halves reach two different downstream objects. Nothing short
    // of the whole chain proves that — a standalone rig can assert on the text
    // an outlet carried, but not that the patcher delivered the two halves down
    // two different cords in the documented order.
    //
    // Sinks before the patcher: the patcher is torn down first, while the
    // inlets it is wired to still exist.
    std::vector<char> log;
    OrderSink item;
    OrderSink rest;
    item.log = &log;
    item.tag = 'I';
    rest.log = &log;
    rest.tag = 'R';
    YSE::pHandle itemHandle(&item);
    YSE::pHandle restHandle(&rest);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* zl = p.CreateObject(YSE::OBJ::G_ZL, "nth 2");
    REQUIRE(zl != nullptr);
    p.Connect(zl, 0, &itemHandle, 0);
    p.Connect(zl, 1, &restHandle, 0);

    zl->SetListData(0, "60 64 67 72");
    REQUIRE(log.size() == 2);
    CHECK(log[0] == 'R');
    CHECK(log[1] == 'I');
    CHECK(item.lastKind == OrderSink::INT);
    CHECK(item.lastInt == 64);
    CHECK(rest.lastKind == OrderSink::LIST);
    CHECK(rest.lastList == "60 67 72");

    // The index arrives down a cord into the cold inlet and re-points the pick
    // without emitting; the next bang applies it to the list already held.
    log.clear();
    zl->SetIntData(1, 4);
    CHECK(log.empty());
    zl->SetBang(0);
    CHECK(item.lastInt == 72);
    CHECK(rest.lastList == "60 64 67");
  }

  TEST_CASE("zl: a mode switch mid-patch changes what the same list produces (#523)") {
    // The live-coding case, through the real graph: one `.zl` fed by one
    // source, told a new mode by a message, and asked for the same list back.
    MultiSink out;
    YSE::pHandle outHandle(&out);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* zl = p.CreateObject(YSE::OBJ::G_ZL, "len");
    REQUIRE(zl != nullptr);
    p.Connect(zl, 0, &outHandle, 0);

    zl->SetListData(0, "a b c d e");
    CHECK(out.gotInt);
    CHECK(out.intValue == 5);

    out.reset();
    zl->SetListData(0, "mode rev");
    CHECK_FALSE(out.gotInt);
    CHECK_FALSE(out.gotList);

    zl->SetBang(0);
    CHECK(out.gotList);
    CHECK(out.listValue == "e d c b a");
  }

  TEST_CASE("zl: a sort feeds its index map to an indexmap, in a real patch (#524)") {
    // The idiom the reordering group exists for, run through the real thing:
    // sort the pitches, and use the map the sort publishes to put the durations
    // beside them into the same new order. Nothing short of the whole chain
    // proves it — a standalone rig can assert on the text an outlet carried,
    // but not that the patcher delivered the map down a cord into a second
    // object's cold inlet *before* anything asked that object to fire.
    //
    // Sinks before the patcher: the patcher is torn down first, while the
    // inlets it is wired to still exist.
    MultiSink pitches;
    MultiSink durations;
    YSE::pHandle pitchHandle(&pitches);
    YSE::pHandle durationHandle(&durations);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* sort = p.CreateObject(YSE::OBJ::G_ZL, "sort");
    YSE::pHandle* apply = p.CreateObject(YSE::OBJ::G_ZL, "indexmap");
    REQUIRE(sort != nullptr);
    REQUIRE(apply != nullptr);

    // The sorted pitches go to one sink; the map goes into the indexmap's cold
    // right inlet.
    p.Connect(sort, 0, &pitchHandle, 0);
    p.Connect(sort, 1, apply, 1);
    p.Connect(apply, 0, &durationHandle, 0);

    sort->SetListData(0, "67 60 72 64");
    CHECK(pitches.gotList);
    CHECK(pitches.listValue == "60 64 67 72");
    // 60 was second, 64 fourth, 67 first, 72 third — and the map reached the
    // indexmap without making it emit, its right inlet being cold.
    CHECK_FALSE(durations.gotList);

    // The parallel list, put into the same order by the map that came down the
    // cord.
    apply->SetListData(0, "100 200 300 400");
    CHECK(durations.gotList);
    CHECK(durations.listValue == "200 400 100 300");
  }

  TEST_CASE("zl: a rotate is re-pointed live and re-run by a bang, in a real patch (#524)") {
    // The generative case: one `.zl rot` holding a rhythm, told a new offset
    // down a cord, and asked for the same rhythm back rotated the other way.
    MultiSink out;
    YSE::pHandle outHandle(&out);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* zl = p.CreateObject(YSE::OBJ::G_ZL, "rot 1");
    REQUIRE(zl != nullptr);
    p.Connect(zl, 0, &outHandle, 0);

    zl->SetListData(0, "1 0 0 1 0 0 1 0");
    CHECK(out.gotList);
    CHECK(out.listValue == "0 1 0 0 1 0 0 1");

    out.reset();
    zl->SetIntData(1, -2);
    CHECK_FALSE(out.gotList);
    zl->SetBang(0);
    CHECK(out.listValue == "0 1 0 0 1 0 1 0");

    // A mode switch mid-patch turns the same stored rhythm into a shuffle, and
    // a seed makes that shuffle replayable.
    zl->SetListData(0, "mode scramble");
    zl->SetListData(0, "zlseed 12345");
    zl->SetBang(0);
    const std::string first = out.listValue;
    zl->SetListData(0, "zlseed 12345");
    zl->SetBang(0);
    CHECK(out.listValue == first);
  }

  TEST_CASE("zl: a multi-number argument survives a DumpJSON / ParseJSON round trip (#524)") {
    // `swap` and `indexmap` widened the creation arguments from one number to a
    // run of them, so the save/load path has to carry the whole run.
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_ZL, "indexmap 3 1 2") != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(copy->GetParams() == std::string("indexmap 3 1 2"));

    // And the reloaded object really carries the map, rather than only the text
    // that spells it.
    MultiSink out;
    YSE::pHandle outHandle(&out);
    loaded.Connect(copy, 0, &outHandle, 0);
    copy->SetListData(0, "a b c");
    CHECK(out.gotList);
    CHECK(out.listValue == "c a b");
  }

  TEST_CASE("zl: a sub feeds its positions to an nth, in a real patch (#525)") {
    // The idiom the extraction group exists for, and the reason every position
    // this object reports is 1-based: `sub` finds *where*, `nth` fetches
    // *what*, and the one is wired straight into the other. Nothing short of
    // the whole chain proves it — a standalone rig can assert on the text an
    // outlet carried, but not that the patcher delivered the position down a
    // cord into a second object's cold inlet and that the pick then landed on
    // the item the search actually found.
    //
    // Sinks before the patcher: the patcher is torn down first, while the
    // inlets it is wired to still exist.
    MultiSink found;
    MultiSink picked;
    YSE::pHandle foundHandle(&found);
    YSE::pHandle pickedHandle(&picked);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* search = p.CreateObject(YSE::OBJ::G_ZL, "sub 64");
    YSE::pHandle* fetch = p.CreateObject(YSE::OBJ::G_ZL, "nth");
    REQUIRE(search != nullptr);
    REQUIRE(fetch != nullptr);

    // The position goes into the `nth`'s cold right inlet; the count goes to a
    // sink of its own.
    p.Connect(search, 0, fetch, 1);
    p.Connect(search, 1, &foundHandle, 0);
    p.Connect(fetch, 0, &pickedHandle, 0);

    search->SetListData(0, "60 62 64 65");
    // One occurrence, at 1-based position 3 — and it reached the `nth` without
    // making it emit, its right inlet being cold.
    CHECK(found.gotInt);
    CHECK(found.intValue == 1);
    CHECK_FALSE(picked.gotInt);

    // The very list that was searched, picked at the position the search
    // reported: the round trip lands back on 64.
    fetch->SetListData(0, "60 62 64 65");
    CHECK(picked.gotInt);
    CHECK(picked.intValue == 64);

    // A pattern that is not there says so on the right outlet and leaves the
    // `nth` pointing where it was.
    found.reset();
    search->SetListData(0, "70 71 72");
    CHECK(found.gotInt);
    CHECK(found.intValue == 0);
  }

  TEST_CASE("zl: a lookup turns a stream of indices into table entries, in a real patch (#525)") {
    // The use case `lookup` exists for — a table set once down one cord and
    // read over and over by numbers arriving down another — plus the live mode
    // switch to `slice`, which cuts the same stored list in two across two
    // different downstream objects.
    MultiSink head;
    MultiSink tail;
    YSE::pHandle headHandle(&head);
    YSE::pHandle tailHandle(&tail);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* read = p.CreateObject(YSE::OBJ::G_ZL, "lookup");
    REQUIRE(read != nullptr);

    p.Connect(read, 0, &headHandle, 0);
    p.Connect(read, 1, &tailHandle, 0);

    read->SetListData(1, "do re mi fa sol");
    CHECK_FALSE(head.gotList);

    read->SetIntData(0, 5);
    CHECK(head.gotList);
    CHECK(head.listValue == "sol");
    // A single result: the right outlet stays silent.
    CHECK_FALSE(tail.gotList);

    head.reset();
    read->SetListData(0, "1 3 3 2");
    CHECK(head.listValue == "do mi mi re");

    // Live mode switch: the same stored indices are now just a list to cut.
    head.reset();
    read->SetListData(0, "mode slice");
    read->SetIntData(1, 2);
    read->SetBang(0);
    CHECK(head.listValue == "1 3");
    CHECK(tail.listValue == "3 2");
  }

  TEST_CASE("zl: a symbolic argument survives a DumpJSON / ParseJSON round trip (#525)") {
    // `sub` and `lookup` widened the creation arguments from a run of numbers
    // to a run of anything, so the save/load path has to carry symbols too.
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_ZL, "lookup do re mi") != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(copy->GetParams() == std::string("lookup do re mi"));

    // And the reloaded object really carries the table, rather than only the
    // text that spells it.
    MultiSink out;
    YSE::pHandle outHandle(&out);
    loaded.Connect(copy, 0, &outHandle, 0);
    copy->SetListData(0, "3 1");
    CHECK(out.gotList);
    CHECK(out.listValue == "mi do");
  }

  TEST_CASE("zl: a sect feeds a change, so a patch hears only what is new (#526)") {
    // The use case the set group exists for, run through the real thing: keep
    // the notes that are in the scale, and pass them on only when the set of
    // them is not the one that went past last time. Nothing short of the whole
    // chain proves it — a standalone rig can assert on the text an outlet
    // carried, but not that the patcher delivered one object's result into the
    // next object's *hot* inlet and that the second one then held its tongue.
    //
    // Sinks before the patcher: the patcher is torn down first, while the
    // inlets it is wired to still exist.
    MultiSink notes;
    MultiSink changed;
    YSE::pHandle noteHandle(&notes);
    YSE::pHandle changeHandle(&changed);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* scale = p.CreateObject(YSE::OBJ::G_ZL, "sect 60 62 64 65 67 69 71");
    YSE::pHandle* fresh = p.CreateObject(YSE::OBJ::G_ZL, "change");
    REQUIRE(scale != nullptr);
    REQUIRE(fresh != nullptr);

    p.Connect(scale, 0, fresh, 0);
    p.Connect(fresh, 0, &noteHandle, 0);
    p.Connect(fresh, 1, &changeHandle, 0);

    scale->SetListData(0, "60 61 62 63 64");
    CHECK(notes.gotList);
    CHECK(notes.listValue == "60 62 64");
    CHECK(changed.intValue == 1);

    // A different chord that lands on the same three scale degrees: the `sect`
    // sends the same list, and the `change` swallows it.
    notes.reset();
    changed.reset();
    scale->SetListData(0, "60 66 62 68 64");
    CHECK_FALSE(notes.gotList);
    CHECK(changed.gotInt);
    CHECK(changed.intValue == 0);

    // A genuinely new set gets through.
    notes.reset();
    changed.reset();
    scale->SetListData(0, "65 67 69");
    CHECK(notes.listValue == "65 67 69");
    CHECK(changed.intValue == 1);

    // Nothing in the scale at all: the `sect` bangs its right outlet rather
    // than sending an empty list on, so the `change` never fires and the patch
    // keeps the last set it was given.
    notes.reset();
    changed.reset();
    scale->SetListData(0, "61 63 66");
    CHECK_FALSE(notes.gotList);
    CHECK_FALSE(changed.gotInt);
  }

  TEST_CASE("zl: a filter feeds its positions to an nth, in a real patch (#526)") {
    // The reason `filter` reports positions at all, and the reason they are
    // 1-based: what it reports is what `nth` takes, exactly as `sub`'s
    // positions are. Here the filter is asked which item is *not* one of the
    // expected ones, and the `nth` fetches it back out of the list.
    MultiSink where;
    MultiSink picked;
    YSE::pHandle whereHandle(&where);
    YSE::pHandle pickedHandle(&picked);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* odd = p.CreateObject(YSE::OBJ::G_ZL, "filter 60 62 64");
    YSE::pHandle* fetch = p.CreateObject(YSE::OBJ::G_ZL, "nth");
    REQUIRE(odd != nullptr);
    REQUIRE(fetch != nullptr);

    p.Connect(odd, 1, fetch, 1);
    p.Connect(odd, 1, &whereHandle, 0);
    p.Connect(fetch, 0, &pickedHandle, 0);

    odd->SetListData(0, "60 62 63 64");
    // 63 is the one that is not in the set, at 1-based position 3 — and the
    // position reached the `nth` without making it emit, its right inlet being
    // cold.
    CHECK(where.gotInt);
    CHECK(where.intValue == 3);
    CHECK_FALSE(picked.gotInt);

    // The very list that was filtered, picked at the position the filter
    // reported: the round trip lands back on 63.
    fetch->SetListData(0, "60 62 63 64");
    CHECK(picked.gotInt);
    CHECK(picked.intValue == 63);
  }

  TEST_CASE("zl: a set mode's argument survives a DumpJSON / ParseJSON round trip (#526)") {
    // The set modes read the whole creation-argument run as the other list, so
    // the save/load path has to carry it — symbols included, a scale of note
    // names being exactly what a patch types here.
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_ZL, "sect do re mi") != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(copy->GetParams() == std::string("sect do re mi"));

    // And the reloaded object really carries the list, rather than only the
    // text that spells it.
    MultiSink out;
    YSE::pHandle outHandle(&out);
    loaded.Connect(copy, 0, &outHandle, 0);
    copy->SetListData(0, "fa mi do sol");
    CHECK(out.gotList);
    CHECK(out.listValue == "mi do");
  }

  TEST_CASE("zl: a group chunks a stream into chords, in a real patch (#527)") {
    // The use case the accumulating group exists for, run through the real
    // thing: single notes arrive one at a time down a cord and leave as chords
    // of three. Nothing short of the whole chain proves it — the state that
    // makes it work lives *between* messages, so a test that sent one list and
    // looked at one outlet could not see the mode at all.
    //
    // Sinks before the patcher: the patcher is torn down first, while the
    // inlets it is wired to still exist.
    TallySink chords;
    YSE::pHandle chordHandle(&chords);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* group = p.CreateObject(YSE::OBJ::G_ZL, "group 3");
    REQUIRE(group != nullptr);
    p.Connect(group, 0, &chordHandle, 0);

    group->SetIntData(0, 60);
    group->SetIntData(0, 64);
    CHECK(chords.got.empty());
    group->SetIntData(0, 67);
    REQUIRE(chords.got.size() == 1);
    CHECK(chords.got[0] == "l:60 64 67");

    // And it keeps going across messages rather than starting over: the fourth
    // note begins the next chord.
    group->SetIntData(0, 72);
    group->SetListData(0, "76 79 83");
    REQUIRE(chords.got.size() == 2);
    CHECK(chords.got[1] == "l:72 76 79");

    // 83 is still held — a partial chord — and a bang is what ends the phrase
    // without waiting for it to fill.
    group->SetBang(0);
    REQUIRE(chords.got.size() == 3);
    CHECK(chords.got[2] == "i:83");

    // Emptied by the flush, so the next note starts a fresh chord rather than
    // completing the old one.
    group->SetBang(0);
    CHECK(chords.got.size() == 3);
  }

  TEST_CASE("zl: a lace is pulled back apart by a delace, in a real patch (#527)") {
    // The pair, wired the way a patch wires them: two parallel lists merged
    // into one cord and separated again at the other end. Only the real graph
    // shows that the two halves land on two different downstream objects — a
    // standalone rig can see the text an outlet carried, not the routing.
    MultiSink evens;
    MultiSink odds;
    YSE::pHandle evenHandle(&evens);
    YSE::pHandle oddHandle(&odds);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* lace = p.CreateObject(YSE::OBJ::G_ZL, "lace 100 200 300");
    YSE::pHandle* delace = p.CreateObject(YSE::OBJ::G_ZL, "delace");
    REQUIRE(lace != nullptr);
    REQUIRE(delace != nullptr);

    p.Connect(lace, 0, delace, 0);
    p.Connect(delace, 0, &evenHandle, 0);
    p.Connect(delace, 1, &oddHandle, 0);

    lace->SetListData(0, "60 64 67");
    CHECK(evens.gotList);
    CHECK(evens.listValue == "60 64 67");
    CHECK(odds.gotList);
    CHECK(odds.listValue == "100 200 300");
  }

  TEST_CASE("zl: a queue is drained by bangs until the right outlet answers, in a real patch "
            "(#527)") {
    // What a queue is *for*: a patch pushes a burst of values in and pulls them
    // out one at a time on its own clock, banging until the right outlet says
    // there is nothing left. Two outlets, two downstream objects, and the store
    // surviving between messages — none of it visible without the real graph.
    TallySink values;
    TallySink empty;
    YSE::pHandle valueHandle(&values);
    YSE::pHandle emptyHandle(&empty);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* queue = p.CreateObject(YSE::OBJ::G_ZL, "queue");
    REQUIRE(queue != nullptr);
    p.Connect(queue, 0, &valueHandle, 0);
    p.Connect(queue, 1, &emptyHandle, 0);

    queue->SetListData(0, "60 64 67");
    CHECK(values.got.empty());
    CHECK(empty.got.empty());

    // Drain it, and one bang too many.
    for (int i = 0; i < 4; i++)
      queue->SetBang(0);

    REQUIRE(values.got.size() == 3);
    CHECK(values.got[0] == "i:60");
    CHECK(values.got[1] == "i:64");
    CHECK(values.got[2] == "i:67");
    REQUIRE(empty.got.size() == 1);
    CHECK(empty.got[0] == "bang");

    // Refilling starts the queue again rather than the object having become
    // inert — the store is empty, not gone.
    values.reset();
    queue->SetListData(0, "72");
    queue->SetBang(0);
    REQUIRE(values.got.size() == 1);
    CHECK(values.got[0] == "i:72");
  }

  TEST_CASE("zl: a reg holds a chord for a later bang, in a real patch (#527)") {
    // The register idiom: one cord primes the object silently and another asks
    // for what it is holding. The silence of the cold inlet is the whole point,
    // and it is only assertable with the cord actually in place.
    MultiSink out;
    YSE::pHandle outHandle(&out);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* source = p.CreateObject(YSE::OBJ::G_ZL, "rev");
    YSE::pHandle* reg = p.CreateObject(YSE::OBJ::G_ZL, "reg");
    REQUIRE(source != nullptr);
    REQUIRE(reg != nullptr);

    // The reversed list goes down a cord into the register's cold inlet.
    p.Connect(source, 0, reg, 1);
    p.Connect(reg, 0, &outHandle, 0);

    source->SetListData(0, "60 64 67");
    // Primed, and silent — the cold inlet stores without emitting.
    CHECK_FALSE(out.gotList);

    reg->SetBang(0);
    CHECK(out.gotList);
    CHECK(out.listValue == "67 64 60");

    // And a list at the hot inlet passes straight through, replacing what is
    // held: Max's "sent out the left outlet immediately".
    out.reset();
    reg->SetListData(0, "72 76");
    CHECK(out.listValue == "72 76");
    out.reset();
    reg->SetBang(0);
    CHECK(out.listValue == "72 76");
  }

  TEST_CASE("zl: a structural mode's argument survives a DumpJSON / ParseJSON round trip (#527)") {
    // The accumulating modes read their argument as a length, so the save/load
    // path has to carry it — a `group` that came back without its group size
    // would collect for ever and emit nothing.
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_ZL, "group 2") != nullptr);
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(copy->GetParams() == std::string("group 2"));

    // And the reloaded object really groups, rather than only remembering the
    // text that spells the argument.
    MultiSink out;
    YSE::pHandle outHandle(&out);
    loaded.Connect(copy, 0, &outHandle, 0);
    copy->SetIntData(0, 1);
    CHECK_FALSE(out.gotList);
    copy->SetIntData(0, 2);
    CHECK(out.gotList);
    CHECK(out.listValue == "1 2");
  }

} // TEST_SUITE
