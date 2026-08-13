// Tests for `.nodes` — Max's nodes, "interpolate between positioned nodes"
// (issue #562), and the patcher's morph controller.
//
// Four claims, and the tests are organised around them:
//
//   - **the algorithm is the object.** Each node has a position and a radius;
//     an incoming (x, y) produces a Euclidean distance per node on outlet 1 and
//     a normalised weight per node on outlet 0. The weights sum to 1 wherever
//     the cursor is inside at least one node and are all 0 where it is inside
//     none, with no fallback to the nearest node — that pair of properties is
//     what makes it a crossfade rather than a proximity readout, so both halves
//     are pinned, including the NaN and zero-radius edges that could poison the
//     shared sum.
//
//   - **two numbers are a cursor and three are a node.** One hot inlet carries
//     both, and the split is arithmetic rather than a heuristic. That is what
//     lets a single inlet also satisfy the GUI value protocol's write half, so
//     the boundary is tested from both sides: a two-number list must never
//     reshape the field, and the shortest possible field (one node, three
//     numbers) must never be read as a cursor.
//
//   - **N is variable, and a resize costs nothing and loses nothing.** The
//     count comes from the `count` argument and from any whole-field list, both
//     being one atomic store into banks allocated whole at construction, so a
//     live SetParams rides the wait-free scalar plan (#234) rather than
//     replacing the object and the node positions survive it.
//
//   - **it is an N-cell GUI object whose cell is a node.** GetGuiValueCount()
//     is the node count, a cell is that node's x,y,radius triple, and inlet 0
//     takes the whole string straight back — count included.
//
// End-to-end cases build a real patcher through the public API — real
// CreateObject, real Connect, driven through pHandle the way a host drives a
// control, read back through the downstream objects' own handles — because that
// composition is what a host actually runs. Plus the JSON round trip, the doc
// metadata and an allocation probe over every message path.
//
// No audio device required.

#include <doctest/doctest.h>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "patcher/guiObjects/gNodes.h"
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
using YSE::PATCHER::gNodes;
using YSE::PATCHER::patcherImplementation;
using YSE::PATCHER::Register;

namespace {

  // The text the object spells a float with — ExprFormatValue's shortest
  // round-tripping form, which is what both list outlets and the GUI value
  // carry.
  std::string Text(float value) {
    char buffer[YSE::PATCHER::kExprValueTextMax];
    const int written = YSE::PATCHER::ExprFormatValue(YSE::PATCHER::ExprValue::Float(value), buffer,
                                                      YSE::PATCHER::kExprValueTextMax);
    return std::string(buffer, written > 0 ? (std::size_t)written : 0);
  }

  // A list as the object spells it: every value, space separated, in order.
  std::string List(std::initializer_list<float> values) {
    std::string out;
    for (float value : values) {
      if (!out.empty()) out.push_back(' ');
      out += Text(value);
    }
    return out;
  }

  // @p times identical nodes, spelled the way the field spells itself.
  std::string Triples(float x, float y, float radius, int times) {
    std::string out;
    for (int i = 0; i < times; i++) {
      if (!out.empty()) out.push_back(' ');
      out += List({x, y, radius});
    }
    return out;
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("nodes: type name, port counts and outlet types (#562)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_NODES);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".nodes");
    // One inlet, Max's, carrying both the cursor and the field.
    CHECK(h->GetInputs() == 1);
    // Two outlets: the weights and the distances, both lists.
    CHECK(h->GetOutputs() == 2);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::LIST);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::LIST);
  }

  TEST_CASE("nodes: registry name and validity (#562)") {
    CHECK(YSE::patcher::IsValidObject(".nodes"));
    auto names = Register().AllNames();
    bool found = false;
    for (const auto& n : names) {
      if (n == ".nodes") found = true;
    }
    CHECK(found);
  }

  TEST_CASE("nodes: a bare object is four unit nodes at the origin (#562)") {
    // The neutral blend: a cursor at the origin is inside every node by the same
    // amount, which is the honest starting state for a morph controller and the
    // one a host can draw before the user has placed anything.
    gNodes field;
    CHECK(field.Nodes() == 4u);
    CHECK(field.GetGuiValueCount() == 4u);
    CHECK(field.NodeRadius(0) == doctest::Approx(1.f));
    CHECK(field.CursorX() == doctest::Approx(0.f));
    CHECK(field.CursorY() == doctest::Approx(0.f));
    for (unsigned int i = 0; i < 4; i++) {
      CAPTURE(i);
      CHECK(field.DistanceTo(i) == doctest::Approx(0.f));
      CHECK(field.WeightOf(i) == doctest::Approx(0.25f));
    }
  }

  // ─── the algorithm ──────────────────────────────────────────────────────────

  TEST_CASE("nodes: the cursor on a node's centre gives it the whole weight (#562)") {
    MultiSink weights, distances;
    gNodes field;
    field.SetParams("2");
    TestHelpers::Wire(field, 0, weights);
    TestHelpers::Wire(field, 1, distances);

    // Two unit nodes, ten apart, so their circles do not overlap.
    field.GetInlet(0)->SetList("0 0 1 10 0 1", YSE::T_GUI);
    REQUIRE(field.Nodes() == 2u);

    field.GetInlet(0)->SetList("0 0", YSE::T_GUI);
    CHECK(distances.listValue == List({0.f, 10.f}));
    CHECK(weights.listValue == List({1.f, 0.f}));

    field.GetInlet(0)->SetList("10 0", YSE::T_GUI);
    CHECK(distances.listValue == List({10.f, 0.f}));
    CHECK(weights.listValue == List({0.f, 1.f}));
  }

  TEST_CASE("nodes: midway between two equal nodes is an even blend (#562)") {
    // The morph the object exists for: one position, a proportion of each patch.
    MultiSink weights, distances;
    gNodes field;
    field.SetParams("2");
    TestHelpers::Wire(field, 0, weights);
    TestHelpers::Wire(field, 1, distances);

    field.GetInlet(0)->SetList("0 0 10 10 0 10", YSE::T_GUI);
    field.GetInlet(0)->SetList("5 0", YSE::T_GUI);

    CHECK(distances.listValue == List({5.f, 5.f}));
    CHECK(weights.listValue == List({0.5f, 0.5f}));
  }

  TEST_CASE("nodes: the weights follow the cursor proportionally (#562)") {
    gNodes field;
    field.SetParams("2");
    field.GetInlet(0)->SetList("0 0 10 10 0 10", YSE::T_GUI);

    // A fifth of the way across: 0.8 of node 0's reach, 0.2 of node 1's, and
    // the two proximities already sum to 1 here so the normalisation is the
    // identity.
    field.GetInlet(0)->SetList("2 0", YSE::T_GUI);
    CHECK(field.DistanceTo(0) == doctest::Approx(2.f));
    CHECK(field.DistanceTo(1) == doctest::Approx(8.f));
    CHECK(field.WeightOf(0) == doctest::Approx(0.8f));
    CHECK(field.WeightOf(1) == doctest::Approx(0.2f));
  }

  TEST_CASE("nodes: the weights sum to 1 wherever the cursor is covered (#562)") {
    // The property that makes this a crossfade rather than a proximity readout —
    // asserted over an asymmetric field where nothing about the geometry makes
    // it fall out for free.
    gNodes field;
    field.SetParams("3");
    field.GetInlet(0)->SetList("0 0 10 6 0 4 3 3 8", YSE::T_GUI);

    const float probes[][2] = {{0.f, 0.f}, {3.f, 0.f}, {5.f, 1.f}, {3.f, 3.f}, {1.5f, 2.f}};
    for (const auto& probe : probes) {
      CAPTURE(probe[0]);
      CAPTURE(probe[1]);
      field.GetInlet(0)->SetList(List({probe[0], probe[1]}), YSE::T_GUI);
      const float total = field.WeightOf(0) + field.WeightOf(1) + field.WeightOf(2);
      CHECK(total == doctest::Approx(1.f));
    }
  }

  TEST_CASE("nodes: outside every node the weights are all zero (#562)") {
    // Not a renormalised nearest-node answer: a field with gaps in it is a
    // legitimate design, and this is how a patch tells "between the nodes" from
    // "outside the field". The distances are still reported, which is why both
    // outlets exist.
    MultiSink weights, distances;
    gNodes field;
    field.SetParams("2");
    TestHelpers::Wire(field, 0, weights);
    TestHelpers::Wire(field, 1, distances);

    field.GetInlet(0)->SetList("0 0 1 10 0 1", YSE::T_GUI);
    field.GetInlet(0)->SetList("5 0", YSE::T_GUI);

    CHECK(weights.listValue == List({0.f, 0.f}));
    CHECK(distances.listValue == List({5.f, 5.f}));
  }

  TEST_CASE("nodes: a node exactly on the rim weighs nothing (#562)") {
    gNodes field;
    field.SetParams("1");
    field.GetInlet(0)->SetList("0 0 4", YSE::T_GUI);
    field.GetInlet(0)->SetList("4 0", YSE::T_GUI);
    CHECK(field.DistanceTo(0) == doctest::Approx(4.f));
    CHECK(field.WeightOf(0) == doctest::Approx(0.f));
  }

  TEST_CASE("nodes: distances are Euclidean in two dimensions (#562)") {
    MultiSink distances;
    gNodes field;
    field.SetParams("2");
    TestHelpers::Wire(field, 1, distances);

    field.GetInlet(0)->SetList("3 4 1 -3 -4 1", YSE::T_GUI);
    field.GetInlet(0)->SetList("0 0", YSE::T_GUI);
    CHECK(distances.listValue == List({5.f, 5.f}));
  }

  TEST_CASE("nodes: a zero or negative radius covers nothing (#562)") {
    // How a node is muted without being deleted — and the reason the radius test
    // is written as a negation, so it holds for a NaN too.
    gNodes field;
    field.SetParams("2");
    field.GetInlet(0)->SetList("0 0 0 0 0 4", YSE::T_GUI);
    field.GetInlet(0)->SetList("0 0", YSE::T_GUI);
    CHECK(field.WeightOf(0) == doctest::Approx(0.f));
    CHECK(field.WeightOf(1) == doctest::Approx(1.f));

    field.GetInlet(0)->SetList("set 1 0 0 -3", YSE::T_GUI);
    CHECK(field.WeightOf(0) == doctest::Approx(0.f));
    CHECK(field.WeightOf(1) == doctest::Approx(0.f));
  }

  TEST_CASE("nodes: an overflowing distance cannot poison another node's weight (#562)") {
    // The shared sum is what makes this worth pinning: one non-finite proximity
    // would propagate into it and take *every* node's weight with it. A cursor
    // far enough from a node overflows the squared difference to infinity, which
    // is the only way a non-finite number can reach the arithmetic at all —
    // ReadNumericToken refuses `nan` and `inf` outright, so every coordinate
    // that gets in is finite. The proximity tests are written as negations so
    // the infinity takes the zero branch rather than becoming a NaN.
    MultiSink weights;
    gNodes field;
    field.SetParams("2");
    TestHelpers::Wire(field, 0, weights);

    field.GetInlet(0)->SetList("0 0 10 1e38 0 10", YSE::T_GUI);
    field.GetInlet(0)->SetList("-1e38 0", YSE::T_GUI);

    // Zeroes, and in particular not the empty text a NaN would render as.
    CHECK(weights.listValue == List({0.f, 0.f}));
    CHECK(field.WeightOf(0) == doctest::Approx(0.f));
    CHECK(field.WeightOf(1) == doctest::Approx(0.f));

    // And a node the cursor is genuinely inside still gets the whole weight,
    // which is what says the sum was never poisoned.
    field.GetInlet(0)->SetList("set 0 -1e38 0 10", YSE::T_GUI);
    CHECK(field.WeightOf(0) == doctest::Approx(1.f));
    CHECK(field.WeightOf(1) == doctest::Approx(0.f));
  }

  TEST_CASE("nodes: the distances land before the weights (#562)") {
    // The patcher's right-to-left ordering, so a patch has the geometry in hand
    // before the blend that was computed from it.
    std::vector<char> log;
    OrderSink weights, distances;
    weights.log = &log;
    weights.tag = 'w';
    distances.log = &log;
    distances.tag = 'd';

    gNodes field;
    field.SetParams("2");
    TestHelpers::Wire(field, 0, weights);
    TestHelpers::Wire(field, 1, distances);

    field.GetInlet(0)->SetList("0 0", YSE::T_GUI);
    REQUIRE(log.size() == 2);
    CHECK(log[0] == 'd');
    CHECK(log[1] == 'w');
  }

  TEST_CASE("nodes: a bang re-sends against the cursor as it stands (#562)") {
    MultiSink weights;
    gNodes field;
    field.SetParams("2");
    TestHelpers::Wire(field, 0, weights);

    field.GetInlet(0)->SetList("0 0 10 10 0 10", YSE::T_GUI);
    field.GetInlet(0)->SetList("5 0", YSE::T_GUI);
    weights.reset();

    field.GetInlet(0)->SetBang(YSE::T_GUI);
    CHECK(weights.gotList);
    CHECK(weights.listValue == List({0.5f, 0.5f}));
    CHECK(field.CursorX() == doctest::Approx(5.f));
  }

  // ─── two numbers versus three ───────────────────────────────────────────────

  TEST_CASE("nodes: exactly two numbers move the cursor and never the field (#562)") {
    gNodes field;
    field.SetParams("3");
    field.GetInlet(0)->SetList("1 1 2 4 4 2 7 7 2", YSE::T_GUI);
    const std::string before = field.GetGuiValue();

    field.GetInlet(0)->SetList("4 4", YSE::T_GUI);
    CHECK(field.CursorX() == doctest::Approx(4.f));
    CHECK(field.CursorY() == doctest::Approx(4.f));
    // The field is untouched — a cursor is not a one-node field with a missing
    // radius.
    CHECK(field.Nodes() == 3u);
    CHECK(field.GetGuiValue() == before);
  }

  TEST_CASE("nodes: three numbers are the shortest possible field (#562)") {
    // The other side of the same boundary. A one-node field is three numbers,
    // which is why no cursor can ever be mistaken for one.
    gNodes field;
    field.SetParams("4");
    field.GetInlet(0)->SetList("2 3 5", YSE::T_GUI);
    CHECK(field.Nodes() == 1u);
    CHECK(field.NodeX(0) == doctest::Approx(2.f));
    CHECK(field.NodeY(0) == doctest::Approx(3.f));
    CHECK(field.NodeRadius(0) == doctest::Approx(5.f));
    // And the cursor did not move with it.
    CHECK(field.CursorX() == doctest::Approx(0.f));
  }

  TEST_CASE("nodes: a whole-field list resizes the field (#562)") {
    gNodes field;
    field.SetParams("2");
    field.GetInlet(0)->SetList("0 0 1 1 1 1 2 2 1 3 3 1", YSE::T_GUI);
    CHECK(field.Nodes() == 4u);
    CHECK(field.GetGuiValueCount() == 4u);
    CHECK(field.NodeX(3) == doctest::Approx(3.f));
  }

  TEST_CASE("nodes: a trailing partial triple is ignored (#562)") {
    // Two thirds of a node is not a node, and inventing the missing number would
    // place a point the patch never gave.
    gNodes field;
    field.SetParams("4");
    field.GetInlet(0)->SetList("1 1 1 2 2 2 9 9", YSE::T_GUI);
    CHECK(field.Nodes() == 2u);
    CHECK(field.NodeX(1) == doctest::Approx(2.f));
    CHECK(field.GetGuiValue() == List({1.f, 1.f, 1.f, 2.f, 2.f, 2.f}));
  }

  TEST_CASE("nodes: an empty or single-number list changes nothing (#562)") {
    MultiSink weights;
    gNodes field;
    field.SetParams("2");
    TestHelpers::Wire(field, 0, weights);

    field.GetInlet(0)->SetList("0 0 10 10 0 10", YSE::T_GUI);
    field.GetInlet(0)->SetList("5 0", YSE::T_GUI);
    const std::string before = field.GetGuiValue();
    weights.reset();

    field.GetInlet(0)->SetList("", YSE::T_GUI);
    field.GetInlet(0)->SetList("   ", YSE::T_GUI);
    field.GetInlet(0)->SetList("7", YSE::T_GUI);
    field.GetInlet(0)->SetList("hello there", YSE::T_GUI);

    CHECK(field.Nodes() == 2u);
    CHECK(field.GetGuiValue() == before);
    CHECK(field.CursorX() == doctest::Approx(5.f));
    // They still fall through to the re-send a bang gives — the hot inlet's rule.
    CHECK(weights.listValue == List({0.5f, 0.5f}));
  }

  TEST_CASE("nodes: a field longer than the ceiling stops at it (#562)") {
    gNodes field;
    std::string huge;
    for (std::size_t i = 0; i < (gNodes::MAX_NODES + 5) * 3; i++) {
      if (i > 0) huge.push_back(' ');
      huge += "1";
    }
    field.GetInlet(0)->SetList(huge, YSE::T_GUI);
    CHECK(field.Nodes() == (unsigned int)gNodes::MAX_NODES);
  }

  TEST_CASE("nodes: the count argument is clamped, not indexed (#562)") {
    gNodes field;
    field.SetParams("0");
    CHECK(field.Nodes() == 1u);
    field.SetParams("-5");
    CHECK(field.Nodes() == 1u);
    field.SetParams("99999");
    CHECK(field.Nodes() == (unsigned int)gNodes::MAX_NODES);
  }

  TEST_CASE("nodes: shrinking hides nodes rather than clearing them (#562)") {
    // .multislider's rule and its reason: clearing would be an O(N) write on
    // whichever thread the resize arrived on, and a resize arrives on the audio
    // thread by both routes.
    gNodes field;
    field.SetParams("3");
    field.GetInlet(0)->SetList("1 1 1 2 2 2 3 3 3", YSE::T_GUI);

    field.SetParams("1");
    REQUIRE(field.Nodes() == 1u);
    CHECK(field.GetGuiValueAt(1).empty());

    field.SetParams("3");
    CHECK(field.GetGuiValue() == List({1.f, 1.f, 1.f, 2.f, 2.f, 2.f, 3.f, 3.f, 3.f}));
  }

  // ─── one node at a time ─────────────────────────────────────────────────────

  TEST_CASE("nodes: \"set <index> <x> <y>\" moves a node and keeps its radius (#562)") {
    // The drag a host performs: a node moves without re-deciding its reach.
    gNodes field;
    field.SetParams("2");
    field.GetInlet(0)->SetList("0 0 3 5 5 7", YSE::T_GUI);

    field.GetInlet(0)->SetList("set 1 9 9", YSE::T_GUI);
    CHECK(field.NodeX(1) == doctest::Approx(9.f));
    CHECK(field.NodeY(1) == doctest::Approx(9.f));
    CHECK(field.NodeRadius(1) == doctest::Approx(7.f));
    // It writes a node; it does not reshape the field.
    CHECK(field.Nodes() == 2u);
  }

  TEST_CASE("nodes: \"set <index> <x> <y> <radius>\" sets the radius too (#562)") {
    gNodes field;
    field.SetParams("2");
    field.GetInlet(0)->SetList("set 0 1 2 3", YSE::T_GUI);
    CHECK(field.NodeX(0) == doctest::Approx(1.f));
    CHECK(field.NodeY(0) == doctest::Approx(2.f));
    CHECK(field.NodeRadius(0) == doctest::Approx(3.f));
  }

  TEST_CASE("nodes: a set outside the live nodes is dropped (#562)") {
    gNodes field;
    field.SetParams("2");
    field.GetInlet(0)->SetList("0 0 1 1 1 1", YSE::T_GUI);
    const std::string before = field.GetGuiValue();

    field.GetInlet(0)->SetList("set 2 8 8 8", YSE::T_GUI);
    field.GetInlet(0)->SetList("set -1 8 8 8", YSE::T_GUI);
    field.GetInlet(0)->SetList("set 0 8", YSE::T_GUI); // no y: not a position
    CHECK(field.GetGuiValue() == before);

    // A word that merely starts with "set" is not the keyword — it is a list
    // whose first token is not a number, so its numbers are read as data.
    field.GetInlet(0)->SetList("settle 4 4", YSE::T_GUI);
    CHECK(field.CursorX() == doctest::Approx(4.f));
    CHECK(field.GetGuiValue() == before);
  }

  // ─── the GUI value protocol (#551) ──────────────────────────────────────────

  TEST_CASE("nodes: it is an N-cell settable GUI object whose cell is a node (#562)") {
    gNodes field;
    field.SetParams("2");
    field.GetInlet(0)->SetList("1 2 3 4 5 6", YSE::T_GUI);

    CHECK(field.GuiValueIsSettable());
    // The node count, not three times it: a host repainting one node wants one
    // read, and a triple is the smallest thing that describes a node.
    CHECK(field.GetGuiValueCount() == 2u);
    CHECK(field.GetGuiValueAt(0) == List({1.f, 2.f, 3.f}));
    CHECK(field.GetGuiValueAt(1) == List({4.f, 5.f, 6.f}));
    CHECK(field.GetGuiValue() == List({1.f, 2.f, 3.f, 4.f, 5.f, 6.f}));
  }

  TEST_CASE("nodes: cell reads past the end answer \"\" rather than indexing (#562)") {
    gNodes field;
    field.SetParams("3");
    CHECK(field.GetGuiValueAt(3).empty());
    CHECK(field.GetGuiValueAt(50).empty());
    CHECK(field.GetGuiValueAt(0xFFFFFFFFu).empty());
  }

  TEST_CASE("nodes: its own GetGuiValue round-trips through inlet 0 (#562)") {
    // The promise GuiValueIsSettable() makes, and the whole of what `.preset`
    // needs: read one string, send it back as an ordinary list later, get the
    // field back — the *count* included, since the string carries it.
    gNodes field;
    field.SetParams("3");
    field.GetInlet(0)->SetList("1 2 3 4 5 6 7 8 9", YSE::T_GUI);
    const std::string stored = field.GetGuiValue();

    field.GetInlet(0)->SetList("0 0 1", YSE::T_GUI);
    REQUIRE(field.Nodes() == 1u);
    REQUIRE(field.GetGuiValue() != stored);

    field.GetInlet(0)->SetList(stored, YSE::T_GUI);
    CHECK(field.GetGuiValue() == stored);
    CHECK(field.Nodes() == 3u);
    CHECK(field.GetGuiValueAt(2) == List({7.f, 8.f, 9.f}));
  }

  // ─── end to end, through the public patcher API ─────────────────────────────

  TEST_CASE("nodes: a host morphs through a real patch (#562)") {
    // The issue's use case as a host builds it: a field of three nodes wired to
    // two list readouts, driven through the public handle API and read back
    // through the readouts' own handles. No internal pokes.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* field = p.CreateObject(YSE::OBJ::G_NODES, "3");
    YSE::pHandle* weightOut = p.CreateObject(YSE::OBJ::G_LIST);
    YSE::pHandle* distanceOut = p.CreateObject(YSE::OBJ::G_LIST);
    REQUIRE(field != nullptr);
    REQUIRE(weightOut != nullptr);
    REQUIRE(distanceOut != nullptr);
    p.Connect(field, 0, weightOut, 0);
    p.Connect(field, 1, distanceOut, 0);

    // Place the field: three unit nodes on a line, four apart, so only one
    // covers the cursor at a time.
    field->SetListData(0, "0 0 1 4 0 1 8 0 1");
    CHECK(field->GetGuiValueCount() == 3u);

    // Sit on the middle one: the whole weight is its, and the readouts follow.
    field->SetListData(0, "4 0");
    CHECK(weightOut->GetGuiValue() == List({0.f, 1.f, 0.f}));
    CHECK(distanceOut->GetGuiValue() == List({4.f, 0.f, 4.f}));

    // Move into the gap: covered by nothing, so the weights go quiet while the
    // geometry is still reported.
    field->SetListData(0, "2 0");
    CHECK(weightOut->GetGuiValue() == List({0.f, 0.f, 0.f}));
    CHECK(distanceOut->GetGuiValue() == List({2.f, 2.f, 6.f}));

    // Widen the middle node through the one-node write and the gap closes.
    field->SetListData(0, "set 1 4 0 8");
    CHECK(weightOut->GetGuiValue() == List({0.f, 1.f, 0.f}));

    // What a host polls to draw it.
    CHECK(field->GuiValueIsSettable());
    CHECK(field->GetGuiValueAt(1) == List({4.f, 0.f, 8.f}));
    CHECK(field->GetGuiValueAt(3).empty());
  }

  TEST_CASE("nodes: a stored GUI value restores the field on a fresh patch (#562)") {
    // What `.preset` will do: read the string out of one patcher and push it
    // into the equivalent object in another. Nothing but the public API — and
    // the restored field takes the stored node *count* as well as the geometry.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* from = src.CreateObject(YSE::OBJ::G_NODES, "3");
    REQUIRE(from != nullptr);
    from->SetListData(0, "1 2 3 4 5 6 7 8 9");
    const std::string stored = from->GetGuiValue();

    YSE::patcher dst;
    dst.create(2);
    YSE::pHandle* to = dst.CreateObject(YSE::OBJ::G_NODES, "1");
    REQUIRE(to != nullptr);
    REQUIRE(to->GetGuiValue() != stored);

    to->SetListData(0, stored);
    CHECK(to->GetGuiValue() == stored);
    CHECK(to->GetGuiValueCount() == 3u);
  }

  TEST_CASE("nodes: params survive a DumpJSON / ParseJSON round trip (#562)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_NODES, "6") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".nodes") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".nodes"));
    CHECK(h->GetParams() == std::string("6"));

    // The count has to still *work*, not merely still be a string: six nodes,
    // all of them at the origin with unit reach, so the cursor there reads back
    // six equal sixths.
    CHECK(h->GetGuiValueCount() == 6u);
    CHECK(h->GetGuiValue() == Triples(0.f, 0.f, 1.f, 6));
  }

  TEST_CASE("nodes: a live re-count rides the scalar plan, not a rebuild (#562)") {
    // `count` is the object's only param and is a scalar, and the object
    // registers no clear/parse callbacks, so SetParams on a running patcher must
    // defer to the audio thread rather than replace the object (issue #234) —
    // which is also what makes the placed nodes survive the resize.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* field = p.CreateObject(YSE::OBJ::G_NODES, "3");
    REQUIRE(field != nullptr);

    MultiSink weights;
    YSE::pHandle sinkHandle(&weights);
    p.Connect(field, 0, &sinkHandle, 0);

    field->SetListData(0, "0 0 1 4 0 1 8 0 1");
    field->SetListData(0, "0 0");
    CHECK(weights.listValue == List({1.f, 0.f, 0.f}));

    const std::size_t retiredBefore = p.PendingRetired();
    const unsigned int idBefore = field->GetID();
    field->SetParams("2");
    CHECK(p.PendingRetired() == retiredBefore);
    CHECK(field->GetID() == idBefore);
    CHECK(field->GetParams() == "2");

    // Deferred: not visible until the audio thread has drained the plan.
    CHECK(field->GetGuiValueCount() == 3u);

    p.Calculate(YSE::T_DSP);
    CHECK(field->GetGuiValueCount() == 2u);

    // And the object survived it, so growing back finds the node it hid, with
    // the position it was given rather than a re-initialised one.
    field->SetParams("3");
    p.Calculate(YSE::T_DSP);
    CHECK(field->GetID() == idBefore);
    CHECK(field->GetGuiValueAt(2) == List({8.f, 0.f, 1.f}));
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the parameter list, which is what a
  // binding generator and a saved patch both key on.

  TEST_CASE("nodes: documents itself as GUI with one parameter (#562)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::G_NODES));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GUI);
    CHECK(obj->NumInputs() == 1);
    CHECK(obj->NumOutputs() == 2);

    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == "count");
  }

  // ─── real time ──────────────────────────────────────────────────────────────

  TEST_CASE("nodes: no message path allocates (#562)") {
    // Including both sends, which are the ones that could: each list is rendered
    // into a string reserved at construction, and the list handler walks its
    // numbers in place rather than into a MAX_NODES-wide stack buffer.
    //
    // The messages are built as strings before the scope opens, never passed as
    // literals inside it: `inlet::SetList` takes a `const std::string&`, so a
    // literal materialises a temporary — a heap allocation whenever the text is
    // longer than the implementation's small-string buffer, which is 15
    // characters on libstdc++ and 22 on libc++. Hoisting them removes the test
    // rig from the measurement so the count is the object's alone.
    //
    // The warm-up deliberately does *not* drive the field as wide as the probe
    // will. `listText` is reserved at construction precisely so the sends never
    // grow it, and a warm-up that had already sent a 64-node list would have
    // grown it anyway, leaving the probe measuring nothing.
    const std::string warmField = "0 0 2 3 0 2 6 0 2";
    const std::string warmCursor = "1 0";
    const std::string warmSet = "set 1 3 1 2";
    const std::string probeField = "1 0 2 4 0 2 7 0 2";
    const std::string probeCursor = "2 1";
    const std::string probeSet = "set 2 7 2 3";
    const std::string probeIgnored = "nothing numeric in here at all";

    // 64 nodes — twenty times the widest field the warm-up sends.
    std::string wideField;
    for (int i = 0; i < 64; i++) {
      if (i > 0) wideField.push_back(' ');
      wideField += "1 2 9";
    }

    MultiSink weights, distances;
    gNodes field;
    field.SetParams("3");
    TestHelpers::Wire(field, 0, weights);
    TestHelpers::Wire(field, 1, distances);

    // Warm every path, but only up to three nodes — see above.
    field.GetInlet(0)->SetList(warmField, YSE::T_GUI);
    field.GetInlet(0)->SetList(warmCursor, YSE::T_GUI);
    field.GetInlet(0)->SetList(warmSet, YSE::T_GUI);
    field.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(weights.gotList);
    REQUIRE(distances.gotList);

    // Size the sinks' own buffers for the widest render the probe will produce,
    // through their inlets rather than by driving the field that wide — the rig
    // must not be what the probe catches, and it must not warm the object's send
    // buffer on the way.
    const std::string sinkWarm(64 * (YSE::PATCHER::kExprValueTextMax + 1), 'x');
    weights.GetInlet(0)->SetList(sinkWarm, YSE::T_GUI);
    distances.GetInlet(0)->SetList(sinkWarm, YSE::T_GUI);

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      field.GetInlet(0)->SetList(probeField, YSE::T_GUI);
      field.GetInlet(0)->SetList(probeCursor, YSE::T_GUI);
      field.GetInlet(0)->SetList(probeSet, YSE::T_GUI);
      field.GetInlet(0)->SetList(probeIgnored, YSE::T_GUI);
      field.GetInlet(0)->SetBang(YSE::T_GUI);
      // The one the constructor's reserve exists for: twenty times wider than
      // anything the warm-up sent, so an unreserved send buffer has to grow.
      field.GetInlet(0)->SetList(wideField, YSE::T_GUI);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);
    // The wide send really did happen, so the zero above is not a vacuous pass.
    CHECK(field.Nodes() == 64u);
    CHECK(distances.listValue.size() > 100u);
  }

} // TEST_SUITE("patcher")
