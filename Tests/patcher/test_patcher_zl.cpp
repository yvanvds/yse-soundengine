// Tests for `.zl` (issues #523, #524) — the patcher's list-processing object,
// and the design gate for the whole list group.
//
// Three things are being pinned here, and only the first of them is about the
// modes themselves:
//
//   - **the modes**: `len`, `rev` and `nth` from #523, including `nth`'s two
//     outlets and the right-before-left order Max guarantees for them; and the
//     reordering group from #524 — `rot`, `scramble`, `sort`, `swap` and
//     `indexmap`, including the index map `sort` publishes, the 1-based
//     numbering that lets it be fed straight back into an `indexmap`, and the
//     seeded shuffle that makes `scramble` assertable at all;
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
#include <cstdint>
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
    // `queue` and `median` belong to mode groups that are not ported yet — the
    // words this test needs are whichever ones are still unimplemented, and it
    // moved off `scramble` / `sort` when #524 implemented them.
    gZl obj;
    obj.SetParams("queue");
    CHECK(obj.CurrentMode() == Mode::NONE);

    // And a mode message naming a mode that is not implemented yet leaves the
    // mode where it was, rather than falling back to another one.
    obj.SetParams("rev");
    REQUIRE(obj.CurrentMode() == Mode::REV);
    obj.GetInlet(0)->SetList("mode median", YSE::T_GUI);
    CHECK(obj.CurrentMode() == Mode::REV);
  }

  TEST_CASE("zl: every mode word round trips through ReadMode and ModeName (#524)") {
    // The two halves of the vocabulary have to agree: a spelling ReadMode knows
    // and ModeName does not is a mode a patch can select and the documentation
    // cannot name.
    const Mode all[] = {Mode::LEN,  Mode::REV,      Mode::NTH,  Mode::ROT,
                        Mode::SORT, Mode::SCRAMBLE, Mode::SWAP, Mode::INDEXMAP,
                        Mode::MTH,  Mode::SLICE,    Mode::SUB,  Mode::LOOKUP};
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

  // ─── mode changes at run time ───────────────────────────────────────────────

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

} // TEST_SUITE
