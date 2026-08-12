// Tests for `.kslider` — Max's kslider, "output pitch and velocity from a
// keyboard display" (issue #555), and the fixed-N case of the structured GUI
// value protocol from issue #551.
//
// Three claims, and the tests are organised around them:
//
//   - **it is a keyboard, and the keyboard is the state.** 128 GUI cells, one
//     per MIDI pitch, each holding that key's velocity with 0 meaning the key
//     is up. So the cell count never moves, cell i is pitch i, and a host draws
//     key i held exactly when cell i is non-zero.
//
//   - **it plays notes, in the pair every note object in the patcher speaks.**
//     Pitch out outlet 0 and velocity out outlet 1, right to left, with the
//     velocity for a bare pitch coming from the cold inlet — `.flush`'s and
//     `.stripnote`'s shape. A velocity of 0 is a release.
//
//   - **the bulk messages emit what *changed*.** A whole-state write sends
//     attacks for keys that went down and releases for keys that came up and
//     nothing for the ones that did not move; `clear` releases what was held; a
//     bang re-sends what is held. That is the one place this object departs
//     from `.matrixctrl`'s full replay, and the reason is that downstream of a
//     synth a replay is a retrigger rather than a repaint — so it is pinned
//     from both directions.
//
// End-to-end cases build a real patcher through the public API — real
// CreateObject, real Connect, driven through pHandle the way a host drives a
// control, read back through the downstream objects' own handles — because that
// composition is what a host actually runs. Plus the JSON round trip, the doc
// metadata and an allocation probe over every message path.
//
// No audio device required.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "patcher/guiObjects/gKSlider.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::MultiSink;
using TestHelpers::OrderSink;
using YSE::PATCHER::gKSlider;
using YSE::PATCHER::patcherImplementation;
using YSE::PATCHER::Register;

namespace {

  constexpr int kKeys = gKSlider::PITCHES;

  // A whole keyboard as the object spells it: 128 velocities, space separated,
  // in pitch order. `held` names the keys that are down and what they hold.
  std::string Keyboard(const std::vector<std::pair<int, int>>& held) {
    std::vector<int> cells((std::size_t)kKeys, 0);
    for (const auto& key : held)
      cells[(std::size_t)key.first] = key.second;

    std::string out;
    for (int i = 0; i < kKeys; i++) {
      if (i > 0) out.push_back(' ');
      out += std::to_string(cells[(std::size_t)i]);
    }
    return out;
  }

  // Every key up — what a fresh object reads back as.
  std::string Silent() {
    return Keyboard({});
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("kslider: type name, port counts and outlet types (#555)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_KSLIDER);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".kslider");
    // Two inlets, `.flush`'s and `.stripnote`'s: pitch and velocity. Max has one
    // because its kslider is clicked and the click supplies a velocity.
    CHECK(h->GetInputs() == 2);
    // Two outlets, Max's: pitch and velocity, as ints.
    CHECK(h->GetOutputs() == 2);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::INT);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::INT);
  }

  TEST_CASE("kslider: registry name and validity (#555)") {
    CHECK(YSE::patcher::IsValidObject(".kslider"));
    auto names = Register().AllNames();
    bool found = false;
    for (const auto& n : names) {
      if (n == ".kslider") found = true;
    }
    CHECK(found);
  }

  TEST_CASE("kslider: a bare object is 128 keys, all of them up (#555)") {
    gKSlider board;
    CHECK(board.GetGuiValueCount() == (unsigned int)kKeys);
    CHECK(board.Held() == 0);
    CHECK(board.GetGuiValue() == Silent());
    // The default velocity a bare pitch is played at — not `.flush`'s 0, which
    // would make every bare pitch a release.
    CHECK(board.Velocity() == gKSlider::DEFAULT_VELOCITY);
  }

  // ─── playing a key ──────────────────────────────────────────────────────────

  TEST_CASE("kslider: a bare pitch presses a key at the stored velocity (#555)") {
    MultiSink pitchSink, velocitySink;
    gKSlider board;
    TestHelpers::Wire(board, 0, pitchSink);
    TestHelpers::Wire(board, 1, velocitySink);

    board.GetInlet(0)->SetInt(60, YSE::T_GUI);
    CHECK(pitchSink.intValue == 60);
    CHECK(velocitySink.intValue == gKSlider::DEFAULT_VELOCITY);
    CHECK(board.VelocityOf(60) == gKSlider::DEFAULT_VELOCITY);
    CHECK(board.Held() == 1);
    CHECK(board.GetGuiValue() == Keyboard({{60, gKSlider::DEFAULT_VELOCITY}}));
  }

  TEST_CASE("kslider: the right inlet sets the velocity and sends nothing (#555)") {
    // `.flush`'s and `.stripnote`'s cold inlet: only a pitch completes a note.
    MultiSink pitchSink, velocitySink;
    gKSlider board;
    TestHelpers::Wire(board, 0, pitchSink);
    TestHelpers::Wire(board, 1, velocitySink);

    board.GetInlet(1)->SetInt(42, YSE::T_GUI);
    CHECK_FALSE(pitchSink.gotInt);
    CHECK_FALSE(velocitySink.gotInt);
    CHECK(board.Velocity() == 42);

    board.GetInlet(0)->SetInt(64, YSE::T_GUI);
    CHECK(pitchSink.intValue == 64);
    CHECK(velocitySink.intValue == 42);
  }

  TEST_CASE("kslider: a pair on the left inlet is Max's list method (#555)") {
    // `.flush`'s inlet distribution on one cord — the shape `.midiparse`'s note
    // outlet sends.
    MultiSink pitchSink, velocitySink;
    gKSlider board;
    TestHelpers::Wire(board, 0, pitchSink);
    TestHelpers::Wire(board, 1, velocitySink);

    board.GetInlet(0)->SetList("60 100", YSE::T_GUI);
    CHECK(pitchSink.intValue == 60);
    CHECK(velocitySink.intValue == 100);
    CHECK(board.VelocityOf(60) == 100);
    // The velocity stuck, exactly as if it had arrived on inlet 1.
    CHECK(board.Velocity() == 100);

    // Velocity 0 is a release, which is how the whole MIDI world spells one.
    board.GetInlet(0)->SetList("60 0", YSE::T_GUI);
    CHECK(pitchSink.intValue == 60);
    CHECK(velocitySink.intValue == 0);
    CHECK(board.VelocityOf(60) == 0);
    CHECK(board.Held() == 0);
  }

  TEST_CASE("kslider: a single-token list is the pitch it spells (#555)") {
    // What a `.m 60` arrives as — `.flush`'s reading of the same message.
    gKSlider board;
    board.GetInlet(0)->SetList("67", YSE::T_GUI);
    CHECK(board.VelocityOf(67) == gKSlider::DEFAULT_VELOCITY);
  }

  TEST_CASE("kslider: the velocity lands before the pitch (#555)") {
    // Right to left, and load-bearing: everything downstream that takes a pair
    // takes velocity on a cold inlet and pitch on a hot one, so a pitch sent
    // first would carry the previous note's velocity.
    std::vector<char> log;
    OrderSink pitchSink, velocitySink;
    pitchSink.log = &log;
    pitchSink.tag = 'p';
    velocitySink.log = &log;
    velocitySink.tag = 'v';

    gKSlider board;
    TestHelpers::Wire(board, 0, pitchSink);
    TestHelpers::Wire(board, 1, velocitySink);

    board.GetInlet(0)->SetInt(60, YSE::T_GUI);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == 'v');
    CHECK(log[1] == 'p');
  }

  TEST_CASE("kslider: several keys are held at once (#555)") {
    // Max's mode attribute is not ported — see the header. The keyboard is
    // always polyphonic, and issue #551's 128-number restore needs it to be.
    gKSlider board;
    board.GetInlet(0)->SetList("60 100", YSE::T_GUI);
    board.GetInlet(0)->SetList("64 90", YSE::T_GUI);
    board.GetInlet(0)->SetList("67 80", YSE::T_GUI);
    CHECK(board.Held() == 3);
    CHECK(board.GetGuiValue() == Keyboard({{60, 100}, {64, 90}, {67, 80}}));
  }

  TEST_CASE("kslider: a pitch off the keyboard is dropped, not clamped (#555)") {
    // Unlike `.flush`, which passes an untrackable pitch through because it
    // watches a cord: this object is a source, so a pitch it cannot display is
    // a key it cannot press, and clamping would light the wrong one.
    MultiSink pitchSink;
    gKSlider board;
    TestHelpers::Wire(board, 0, pitchSink);

    board.GetInlet(0)->SetInt(128, YSE::T_GUI);
    CHECK_FALSE(pitchSink.gotInt);
    board.GetInlet(0)->SetInt(-1, YSE::T_GUI);
    CHECK_FALSE(pitchSink.gotInt);
    CHECK(board.Held() == 0);
    CHECK(board.GetGuiValue() == Silent());
  }

  TEST_CASE("kslider: velocities are clamped into 0-127 (#555)") {
    // A cell holds a MIDI velocity, so it may not hold a number one could not
    // be — which is where this object parts from `.flush`, a watcher that
    // passes what it is handed.
    MultiSink velocitySink;
    gKSlider board;
    TestHelpers::Wire(board, 1, velocitySink);

    board.GetInlet(0)->SetList("60 500", YSE::T_GUI);
    CHECK(velocitySink.intValue == gKSlider::MAX_VELOCITY);
    CHECK(board.VelocityOf(60) == gKSlider::MAX_VELOCITY);

    board.GetInlet(1)->SetInt(-9, YSE::T_GUI);
    CHECK(board.Velocity() == 0);
  }

  TEST_CASE("kslider: a float pitch and a float velocity are truncated (#555)") {
    gKSlider board;
    board.GetInlet(1)->SetFloat(99.7f, YSE::T_GUI);
    CHECK(board.Velocity() == 99);
    board.GetInlet(0)->SetFloat(60.9f, YSE::T_GUI);
    CHECK(board.VelocityOf(60) == 99);
    CHECK(board.VelocityOf(61) == 0);
  }

  // ─── the bulk messages ──────────────────────────────────────────────────────

  TEST_CASE("kslider: a bang re-sends every held key, ascending (#555)") {
    std::vector<char> log;
    OrderSink pitchSink;
    pitchSink.log = &log;
    pitchSink.tag = 'p';

    gKSlider board;
    TestHelpers::Wire(board, 0, pitchSink);
    board.GetInlet(0)->SetList("67 80", YSE::T_GUI);
    board.GetInlet(0)->SetList("60 100", YSE::T_GUI);
    log.clear();
    pitchSink.count = 0;

    board.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(pitchSink.count == 2);
    // Ascending pitch order, so the last one seen is the higher key.
    CHECK(pitchSink.lastInt == 67);
    // A re-send changes nothing.
    CHECK(board.Held() == 2);
    CHECK(board.GetGuiValue() == Keyboard({{60, 100}, {67, 80}}));
  }

  TEST_CASE("kslider: \"clear\" releases every held key (#555)") {
    // Max's message, and `.flush`'s bang rather than `.flush`'s `clear`: the
    // outlet is the only way the change reaches the synth that is sounding
    // them, so a keyboard that forgot its notes silently would leave them
    // hanging with nothing able to find them again.
    MultiSink pitchSink, velocitySink;
    gKSlider board;
    TestHelpers::Wire(board, 0, pitchSink);
    TestHelpers::Wire(board, 1, velocitySink);

    board.GetInlet(0)->SetList("60 100", YSE::T_GUI);
    board.GetInlet(0)->SetList("64 90", YSE::T_GUI);
    velocitySink.reset();

    board.GetInlet(0)->SetList("clear", YSE::T_GUI);
    CHECK(velocitySink.intValue == 0);
    CHECK(pitchSink.intValue == 64);
    CHECK(board.Held() == 0);
    CHECK(board.GetGuiValue() == Silent());

    // Nothing is held, so a second clear sends nothing — a key is released
    // exactly once, `.flush`'s rule.
    pitchSink.reset();
    board.GetInlet(0)->SetList("clear", YSE::T_GUI);
    CHECK_FALSE(pitchSink.gotInt);
  }

  TEST_CASE("kslider: a whole-state write emits only the keys that moved (#555)") {
    // The one place this object departs from `.matrixctrl`'s full replay, and
    // the reason: 120-odd `<pitch> 0` releases for keys nobody touched, plus a
    // re-attack of the ones already sounding, is a retrigger downstream of a
    // synth rather than a repaint.
    OrderSink pitchSink;
    gKSlider board;
    TestHelpers::Wire(board, 0, pitchSink);

    board.GetInlet(0)->SetList("60 100", YSE::T_GUI);
    board.GetInlet(0)->SetList("64 90", YSE::T_GUI);
    pitchSink.count = 0;

    // 60 keeps its velocity, 64 comes up, 67 goes down. One key unchanged, one
    // released, one attacked — so exactly two sends.
    const std::string restore = Keyboard({{60, 100}, {67, 80}});
    board.GetInlet(0)->SetList(restore, YSE::T_GUI);
    CHECK(pitchSink.count == 2);
    CHECK(board.GetGuiValue() == restore);
    CHECK(board.Held() == 2);
  }

  TEST_CASE("kslider: a whole-state write of the same keyboard emits nothing (#555)") {
    // The other direction of the same claim: nothing moved, so nothing is an
    // event.
    OrderSink pitchSink;
    gKSlider board;
    TestHelpers::Wire(board, 0, pitchSink);

    const std::string state = Keyboard({{60, 100}, {64, 90}});
    board.GetInlet(0)->SetList(state, YSE::T_GUI);
    REQUIRE(pitchSink.count == 2);

    pitchSink.count = 0;
    board.GetInlet(0)->SetList(state, YSE::T_GUI);
    CHECK(pitchSink.count == 0);
    CHECK(board.GetGuiValue() == state);
  }

  TEST_CASE("kslider: a list of any other length addresses nothing (#555)") {
    gKSlider board;
    board.GetInlet(0)->SetList("60 100", YSE::T_GUI);
    const std::string before = board.GetGuiValue();

    board.GetInlet(0)->SetList("1 2 3", YSE::T_GUI);
    board.GetInlet(0)->SetList("1 2 3 4 5", YSE::T_GUI);
    board.GetInlet(0)->SetList("", YSE::T_GUI);
    board.GetInlet(0)->SetList("   ", YSE::T_GUI);
    CHECK(board.GetGuiValue() == before);
  }

  // ─── the GUI value protocol (#551) ──────────────────────────────────────────

  TEST_CASE("kslider: it is a fixed 128-cell settable GUI object (#555)") {
    gKSlider board;
    CHECK(board.GuiValueIsSettable());
    CHECK(board.GetGuiValueCount() == 128u);

    board.GetInlet(0)->SetList("60 100", YSE::T_GUI);
    CHECK(board.GetGuiValueAt(60) == "100");
    CHECK(board.GetGuiValueAt(59) == "0");
    CHECK(board.GetGuiValueAt(0) != board.GetGuiValue());
  }

  TEST_CASE("kslider: the cell count never moves (#555)") {
    // The whole difference from `.multislider`: a keyboard is 128 keys because
    // MIDI is, so no message and no parameter can re-count it.
    gKSlider board;
    board.SetParams("30");
    CHECK(board.GetGuiValueCount() == 128u);
    board.GetInlet(0)->SetList("60 100", YSE::T_GUI);
    CHECK(board.GetGuiValueCount() == 128u);
  }

  TEST_CASE("kslider: cell reads past the end answer \"\" rather than indexing (#555)") {
    gKSlider board;
    CHECK(board.GetGuiValueAt(128).empty());
    CHECK(board.GetGuiValueAt(9999).empty());
    CHECK(board.GetGuiValueAt(0xFFFFFFFFu).empty());
  }

  TEST_CASE("kslider: \"set <index> <value>\" writes one key (#555)") {
    // Max's `set` and issue #551's cell write are the same message here, the
    // index being the pitch — the happy case `.multislider` also found and
    // `.rslider` could not.
    MultiSink pitchSink, velocitySink;
    gKSlider board;
    TestHelpers::Wire(board, 0, pitchSink);
    TestHelpers::Wire(board, 1, velocitySink);

    board.GetInlet(0)->SetList("set 72 55", YSE::T_GUI);
    CHECK(pitchSink.intValue == 72);
    CHECK(velocitySink.intValue == 55);
    CHECK(board.GetGuiValueAt(72) == "55");

    // Out of range is dropped, never folded onto a real key.
    pitchSink.reset();
    board.GetInlet(0)->SetList("set 128 55", YSE::T_GUI);
    board.GetInlet(0)->SetList("set -1 55", YSE::T_GUI);
    CHECK_FALSE(pitchSink.gotInt);

    // A word that merely starts with "set" is not the keyword — it is a
    // two-number list, so it plays a note.
    board.GetInlet(0)->SetList("settle 61 40", YSE::T_GUI);
    CHECK(board.GetGuiValueAt(61) == "40");
  }

  TEST_CASE("kslider: its own GetGuiValue round-trips through inlet 0 (#555)") {
    // The promise GuiValueIsSettable() makes, and the whole of what `.preset`
    // needs: read one string, send it back as an ordinary list later, get the
    // keyboard back.
    gKSlider board;
    board.GetInlet(0)->SetList("60 100", YSE::T_GUI);
    board.GetInlet(0)->SetList("64 90", YSE::T_GUI);
    board.GetInlet(0)->SetList("67 80", YSE::T_GUI);
    const std::string stored = board.GetGuiValue();

    board.GetInlet(0)->SetList("clear", YSE::T_GUI);
    REQUIRE(board.GetGuiValue() == Silent());

    board.GetInlet(0)->SetList(stored, YSE::T_GUI);
    CHECK(board.GetGuiValue() == stored);
    CHECK(board.Held() == 3);
  }

  // ─── end to end, through the public patcher API ─────────────────────────────

  TEST_CASE("kslider: a host plays a real patch through pHandle (#555)") {
    // The issue's use case as a host builds it: a keyboard wired to a pitch
    // readout and a velocity readout, driven through the public handle API and
    // read back through the readouts' own handles. No internal pokes.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* board = p.CreateObject(YSE::OBJ::G_KSLIDER, "64");
    YSE::pHandle* pitchOut = p.CreateObject(YSE::OBJ::G_INT);
    YSE::pHandle* velocityOut = p.CreateObject(YSE::OBJ::G_INT);
    REQUIRE(board != nullptr);
    REQUIRE(pitchOut != nullptr);
    REQUIRE(velocityOut != nullptr);
    p.Connect(board, 0, pitchOut, 0);
    p.Connect(board, 1, velocityOut, 0);

    // The creation argument is the velocity a bare pitch is played at.
    board->SetIntData(0, 60);
    CHECK(pitchOut->GetGuiValue() == "60");
    CHECK(velocityOut->GetGuiValue() == "64");

    // A pair, the way `.midiparse` sends one.
    board->SetListData(0, "72 110");
    CHECK(pitchOut->GetGuiValue() == "72");
    CHECK(velocityOut->GetGuiValue() == "110");

    // What a host polls to draw it.
    CHECK(board->GuiValueIsSettable());
    CHECK(board->GetGuiValueCount() == 128u);
    CHECK(board->GetGuiValueAt(60) == "64");
    CHECK(board->GetGuiValueAt(72) == "110");
    CHECK(board->GetGuiValue() == Keyboard({{60, 64}, {72, 110}}));

    // `clear` releases them both downstream, so the synth on the other end of
    // the cord learns of it.
    board->SetListData(0, "clear");
    CHECK(velocityOut->GetGuiValue() == "0");
    CHECK(board->GetGuiValue() == Silent());
  }

  TEST_CASE("kslider: a stored GUI value restores the keyboard on a fresh patch (#555)") {
    // What `.preset` will do: read the string out of one patcher and push it
    // into the equivalent object in another. Nothing but the public API.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* from = src.CreateObject(YSE::OBJ::G_KSLIDER);
    REQUIRE(from != nullptr);
    from->SetListData(0, "60 100");
    from->SetListData(0, "64 90");
    const std::string stored = from->GetGuiValue();

    YSE::patcher dst;
    dst.create(2);
    YSE::pHandle* to = dst.CreateObject(YSE::OBJ::G_KSLIDER);
    REQUIRE(to != nullptr);
    REQUIRE(to->GetGuiValue() != stored);

    to->SetListData(0, stored);
    CHECK(to->GetGuiValue() == stored);
  }

  TEST_CASE("kslider: params survive a DumpJSON / ParseJSON round trip (#555)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_KSLIDER, "77") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".kslider") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".kslider"));
    CHECK(h->GetParams() == std::string("77"));

    // The velocity has to still *work*, not merely still be a string. Held keys
    // are run-time state and are deliberately not saved — a reload brings back
    // a keyboard with no key down.
    CHECK(h->GetGuiValue() == Silent());
    h->SetIntData(0, 60);
    CHECK(h->GetGuiValueAt(60) == "77");
  }

  TEST_CASE("kslider: a live velocity change rides the scalar plan, not a rebuild (#555)") {
    // The one parameter is a scalar and the object registers no clear/parse
    // callbacks, so SetParams on a running patcher must defer to the audio
    // thread rather than replace the object (issue #234) — which is also what
    // makes the held keys survive it.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* board = p.CreateObject(YSE::OBJ::G_KSLIDER, "64");
    REQUIRE(board != nullptr);

    MultiSink sink;
    YSE::pHandle sinkHandle(&sink);
    p.Connect(board, 1, &sinkHandle, 0);

    board->SetIntData(0, 60);
    CHECK(sink.intValue == 64);

    const std::size_t retiredBefore = p.PendingRetired();
    const unsigned int idBefore = board->GetID();
    board->SetParams("30");
    CHECK(p.PendingRetired() == retiredBefore);
    CHECK(board->GetID() == idBefore);

    p.Calculate(YSE::T_DSP);
    board->SetIntData(0, 62);
    CHECK(sink.intValue == 30);
    // The object survived, so the key played before the change is still down.
    CHECK(board->GetID() == idBefore);
    CHECK(board->GetGuiValueAt(60) == "64");
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the parameter order, which is what a
  // binding generator and a saved patch both key on.

  TEST_CASE("kslider: documents itself as GUI with one param (#555)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::G_KSLIDER));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GUI);
    CHECK(obj->NumInputs() == 2);
    CHECK(obj->NumOutputs() == 2);

    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == "velocity");
  }

  // ─── real time ──────────────────────────────────────────────────────────────

  TEST_CASE("kslider: no message path allocates (#555)") {
    // Including the whole-state write, which is the widest: it walks its 128
    // numbers in place rather than copying the message, and records what moved
    // in a fixed stack array. The outlets carry ints, so no send builds a
    // string at all. The counter is read inside the scope and asserted outside
    // it, since doctest's own machinery allocates on first use.
    //
    // **The messages are built as strings before the scope opens, never passed
    // as literals inside it**, and that is not tidiness. `inlet::SetList` takes
    // a `const std::string&`, so a literal at the call site materialises a
    // temporary — a heap allocation whenever the text outgrows the
    // implementation's small-string buffer, and that buffer is *not* the same
    // width everywhere: 15 characters on libstdc++, 22 on libc++. A
    // 17-character list literal therefore costs nothing on the Windows/libc++
    // build and one allocation on the Linux/libstdc++ one, which is a probe
    // that passes locally and fails in CI while the object under test is
    // innocent. Hoisting the strings removes the test rig from the measurement
    // so the count is the object's alone. (Issue #554 learned this the hard
    // way.) One of them is deliberately longer than *both* buffers.
    const std::string warmPair = "60 100";
    const std::string warmSet = "set 64 90";
    const std::string warmClear = "clear";
    const std::string warmLong = "67 80 ignored ignored"; // 21 chars: past libstdc++'s buffer
    const std::string pair = "62 101";
    const std::string cellWrite = "set 65 91";
    const std::string clearAll = "clear";
    const std::string single = "69";
    const std::string wholeState = Keyboard({{60, 100}, {64, 90}, {67, 80}});

    MultiSink pitchSink, velocitySink;
    gKSlider board;
    TestHelpers::Wire(board, 0, pitchSink);
    TestHelpers::Wire(board, 1, velocitySink);

    // Warm every path.
    board.GetInlet(0)->SetList(warmPair, YSE::T_GUI);
    board.GetInlet(0)->SetList(warmSet, YSE::T_GUI);
    board.GetInlet(0)->SetList(warmLong, YSE::T_GUI);
    board.GetInlet(0)->SetBang(YSE::T_GUI);
    board.GetInlet(0)->SetList(warmClear, YSE::T_GUI);
    board.GetInlet(0)->SetInt(60, YSE::T_GUI);
    board.GetInlet(0)->SetFloat(61.5f, YSE::T_GUI);
    board.GetInlet(1)->SetInt(90, YSE::T_GUI);
    board.GetInlet(1)->SetFloat(91.5f, YSE::T_GUI);
    board.GetInlet(1)->SetList(warmPair, YSE::T_GUI);
    REQUIRE(pitchSink.gotInt);
    REQUIRE(velocitySink.gotInt);
    // Deliberately *not* warmed with a 128-number list: the whole-state path
    // inside the probe must be measured, not replayed over already-grown state.
    REQUIRE(board.Held() > 0);

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      board.GetInlet(0)->SetList(pair, YSE::T_GUI);
      board.GetInlet(0)->SetList(cellWrite, YSE::T_GUI);
      board.GetInlet(0)->SetList(single, YSE::T_GUI);
      board.GetInlet(0)->SetInt(70, YSE::T_GUI);
      board.GetInlet(0)->SetFloat(71.25f, YSE::T_GUI);
      board.GetInlet(1)->SetInt(88, YSE::T_GUI);
      board.GetInlet(0)->SetBang(YSE::T_GUI);
      // The widest message the object accepts — 128 numbers, walked in place.
      board.GetInlet(0)->SetList(wholeState, YSE::T_GUI);
      board.GetInlet(0)->SetList(clearAll, YSE::T_GUI);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);
    // The wide write really did happen, so the zero above is not a vacuous
    // pass: three keys went down and the clear took them all off again.
    CHECK(board.Held() == 0);
    CHECK(pitchSink.intValue == 67);
  }

} // TEST_SUITE("patcher")
