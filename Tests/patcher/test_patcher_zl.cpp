// Tests for `.zl` (issue #523) — the patcher's list-processing object, and the
// design gate for the whole list group.
//
// Three things are being pinned here, and only the first of them is about the
// three modes this issue ships:
//
//   - **the modes**: `len`, `rev` and `nth`, including `nth`'s two outlets and
//     the right-before-left order Max guarantees for them;
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
    gZl obj;
    obj.SetParams("scramble");
    CHECK(obj.CurrentMode() == Mode::NONE);

    // And a mode message naming a mode that is not implemented yet leaves the
    // mode where it was, rather than falling back to another one.
    obj.SetParams("rev");
    REQUIRE(obj.CurrentMode() == Mode::REV);
    obj.GetInlet(0)->SetList("mode sort", YSE::T_GUI);
    CHECK(obj.CurrentMode() == Mode::REV);
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

} // TEST_SUITE
