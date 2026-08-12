// Tests for `.matrixctrl` — Max's matrixctrl (issue #559), and the
// two-dimensional case of the structured GUI value protocol from issue #551.
//
// Four claims, and the tests are organised around them:
//
//   - **a 2-D address lands on a 1-D protocol, column-major.** Cell
//     (column, row) is at `column * rows + row`, which is the lexicographic
//     rank of the address as Max writes it and the layout `.matrix` uses for
//     its own gain table. Pinned from both ends: the triple that sets a cell
//     and the flat index that reads it must name the same cell.
//
//   - **the two bare-list forms are told apart by length, and the protocol
//     wins the tie.** A list is Max's `<column> <row> <value>` at three
//     numbers and the whole state at `columns * rows` of them; on a three-cell
//     grid, where they collide, `GuiValueIsSettable()`'s unconditional promise
//     takes precedence.
//
//   - **every write emits, as `<column> <row> <value>`.** That is what makes
//     the object worth having next to `.matrix` / `.router` rather than being
//     a 2-D `.multislider`, so the headline case is driven end to end: a real
//     `.matrixctrl` wired into a real `.matrix`, switching real messages.
//
//   - **the shape is a creation parameter and nothing else changes it.** One
//     list length cannot recover two dimensions, so there is no `listresize`
//     analogue; the storage is exactly `columns * rows` wide and is built on
//     the control thread.
//
// Plus the JSON round trip, the doc metadata, the dump's re-entrancy guard and
// an allocation probe over every message path.
//
// No audio device required.

#include <doctest/doctest.h>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "patcher/guiObjects/gMatrixCtrl.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::gMatrixCtrl;
using YSE::PATCHER::Register;

namespace {

  // The text the object spells a value with — ExprFormatValue's shortest
  // round-tripping form, which both outlets and the GUI value carry.
  std::string Text(float value) {
    char buffer[YSE::PATCHER::kExprValueTextMax];
    const int written = YSE::PATCHER::ExprFormatValue(YSE::PATCHER::ExprValue::Float(value), buffer,
                                                      YSE::PATCHER::kExprValueTextMax);
    return std::string(buffer, written > 0 ? (std::size_t)written : 0);
  }

  // One cell as outlet 0 spells it.
  std::string Triple(int column, int row, float value) {
    return std::to_string(column) + " " + std::to_string(row) + " " + Text(value);
  }

  // The grid as the GUI value spells it: every cell, space separated, in flat
  // index order.
  std::string Grid(std::initializer_list<float> values) {
    std::string out;
    for (float value : values) {
      if (!out.empty()) out.push_back(' ');
      out += Text(value);
    }
    return out;
  }

  std::string Repeated(float value, int count) {
    std::string out;
    for (int i = 0; i < count; i++) {
      if (!out.empty()) out.push_back(' ');
      out += Text(value);
    }
    return out;
  }

  // Records every list that arrives, in order, so a dump can be asserted as the
  // sequence it is rather than by its last line.
  struct DumpSink : YSE::PATCHER::pObject {
    std::vector<std::string> lines;

    DumpSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterBang([this](int, YSE::THREAD) { lines.push_back("<bang>"); });
      inputs.back().RegisterInt([this](int, int, YSE::THREAD) { lines.push_back("<int>"); });
      inputs.back().RegisterFloat([this](float, int, YSE::THREAD) { lines.push_back("<float>"); });
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { lines.push_back(v); });
    }
    const char* Type() const override {
      return "dump_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    std::string Joined() const {
      std::string out;
      for (std::size_t i = 0; i < lines.size(); i++) {
        if (i > 0) out += " | ";
        out += lines[i];
      }
      return out;
    }
  };

  // Asks the control for another dump every time a line of one arrives, a
  // bounded number of times. A matrixctrl whose outlet is wired back to its own
  // inlet is the re-entrancy case, and it needs a receiver that stops asking
  // rather than a bare patch cord, which would not terminate.
  struct FeedbackSink : YSE::PATCHER::pObject {
    gMatrixCtrl* target = nullptr;
    int budget = 0;
    int count = 0;

    FeedbackSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList([this](const std::string&, int, YSE::THREAD t) {
        count++;
        if (budget > 0 && target != nullptr) {
          budget--;
          target->GetInlet(0)->SetBang(t);
        }
      });
    }
    const char* Type() const override {
      return "feedback_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── shape ──────────────────────────────────────────────────────────────────

  TEST_CASE("matrixctrl: type name, port counts and outlet types (#559)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_MATRIXCTRL);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".matrixctrl");
    // One inlet, Max's.
    CHECK(h->GetInputs() == 1);
    // Two outlets, Max's: cells as triples, and a queried row or column.
    CHECK(h->GetOutputs() == 2);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::LIST);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::LIST);
  }

  TEST_CASE("matrixctrl: registry name and validity (#559)") {
    CHECK(YSE::patcher::IsValidObject(".matrixctrl"));
    auto names = Register().AllNames();
    bool found = false;
    for (const auto& n : names) {
      if (n == ".matrixctrl") found = true;
    }
    CHECK(found);
  }

  TEST_CASE("matrixctrl: a bare object is an 8x8 grid at rest (#559)") {
    gMatrixCtrl grid;
    CHECK(grid.Columns() == 8);
    CHECK(grid.Rows() == 8);
    CHECK(grid.Cells() == 64u);
    CHECK(grid.GetGuiValueCount() == 64u);
    CHECK(grid.GetGuiValue() == Repeated(0.f, 64));
  }

  TEST_CASE("matrixctrl: the dimensions are clamped, not indexed (#559)") {
    gMatrixCtrl grid;
    grid.SetParams("0 0");
    CHECK(grid.Columns() == 1);
    CHECK(grid.Rows() == 1);
    grid.SetParams("-5 -9");
    CHECK(grid.Columns() == 1);
    CHECK(grid.Rows() == 1);
    grid.SetParams("9999 3");
    CHECK(grid.Columns() == gMatrixCtrl::MAX_AXIS);
    CHECK(grid.Rows() == 3);
    // Not a number at all leaves the default in place, as the rest of the
    // family does.
    grid.SetParams("wide tall");
    CHECK(grid.Columns() == gMatrixCtrl::DEFAULT_AXIS);
    CHECK(grid.Rows() == gMatrixCtrl::DEFAULT_AXIS);
  }

  TEST_CASE("matrixctrl: SetParams(\"\") returns the object to the no-argument shape (#559)") {
    gMatrixCtrl grid;
    grid.SetParams("3 5 0 7");
    REQUIRE(grid.Cells() == 15u);
    grid.SetParams("");
    CHECK(grid.Columns() == gMatrixCtrl::DEFAULT_AXIS);
    CHECK(grid.Rows() == gMatrixCtrl::DEFAULT_AXIS);
    CHECK(grid.GetGuiValue() == Repeated(0.f, 64));
  }

  // ─── the address ────────────────────────────────────────────────────────────

  TEST_CASE("matrixctrl: the flat index is column-major (#559)") {
    // `column * rows + row`, so the row moves fastest: the lexicographic rank of
    // the address as Max writes it, and the layout .matrix lays its own gain
    // table out in. Pinned on a non-square grid, since a square one cannot tell
    // a transposition from the truth.
    gMatrixCtrl grid;
    grid.SetParams("3 2 0 1"); // three columns, two rows
    REQUIRE(grid.Cells() == 6u);

    CHECK(grid.IndexOf(0, 0) == 0u);
    CHECK(grid.IndexOf(0, 1) == 1u);
    CHECK(grid.IndexOf(1, 0) == 2u);
    CHECK(grid.IndexOf(2, 1) == 5u);
    // Outside the grid on either axis answers the cell count rather than an
    // index that would fold onto a real cell.
    CHECK(grid.IndexOf(3, 0) == grid.Cells());
    CHECK(grid.IndexOf(0, 2) == grid.Cells());
    CHECK(grid.IndexOf(-1, 0) == grid.Cells());

    // And the two write forms agree about which cell that is.
    grid.GetInlet(0)->SetList("1 0 1", YSE::T_GUI); // Max's <column> <row> <value>
    CHECK(grid.GetGuiValueAt(2) == Text(1.f));
    CHECK(grid.Value(1, 0) == doctest::Approx(1.f));
    CHECK(grid.GetGuiValue() == Grid({0.f, 0.f, 1.f, 0.f, 0.f, 0.f}));

    grid.GetInlet(0)->SetList("set 5 1", YSE::T_GUI); // the protocol's flat index
    CHECK(grid.Value(2, 1) == doctest::Approx(1.f));
  }

  TEST_CASE("matrixctrl: \"<column> <row> <value>\" sets one cell and echoes it (#559)") {
    DumpSink cells;
    gMatrixCtrl grid;
    grid.SetParams("4 4 0 1");
    TestHelpers::Wire(grid, 0, cells);

    grid.GetInlet(0)->SetList("2 3 1", YSE::T_GUI);
    CHECK(grid.Value(2, 3) == doctest::Approx(1.f));
    // Column-major, so cell (2, 3) of a four-row grid is flat index 11.
    CHECK(grid.GetGuiValueAt(11) == Text(1.f));
    CHECK(grid.GetGuiValueAt(10) == Text(0.f));
    // One list out, and it is the cell that changed — Max's left outlet.
    CHECK(cells.Joined() == Triple(2, 3, 1.f));

    // A cell the grid does not have is ignored rather than folded onto a real
    // one, and says nothing.
    cells.lines.clear();
    grid.GetInlet(0)->SetList("4 0 1", YSE::T_GUI);
    grid.GetInlet(0)->SetList("0 9 1", YSE::T_GUI);
    grid.GetInlet(0)->SetList("-1 0 1", YSE::T_GUI);
    CHECK(cells.lines.empty());
    CHECK(grid.GetGuiValue() == Repeated(0.f, 11) + " " + Text(1.f) + " " + Repeated(0.f, 4));
  }

  TEST_CASE("matrixctrl: \"set <index> <value>\" writes one cell by flat index (#559)") {
    // Issue #551's cell write. It echoes the *address*, not the index, because
    // the address is what the crossbar downstream understands.
    DumpSink cells;
    gMatrixCtrl grid;
    grid.SetParams("3 2 0 1");
    TestHelpers::Wire(grid, 0, cells);

    grid.GetInlet(0)->SetList("set 2 1", YSE::T_GUI);
    CHECK(grid.Value(1, 0) == doctest::Approx(1.f));
    CHECK(cells.Joined() == Triple(1, 0, 1.f));

    // Out of range is dropped, never folded onto a real cell, and says nothing.
    cells.lines.clear();
    grid.GetInlet(0)->SetList("set 6 1", YSE::T_GUI);
    grid.GetInlet(0)->SetList("set -1 1", YSE::T_GUI);
    CHECK(cells.lines.empty());
    CHECK(grid.GetGuiValue() == Grid({0.f, 0.f, 1.f, 0.f, 0.f, 0.f}));
  }

  TEST_CASE("matrixctrl: Max's silent \"set <column> <row> <value>\" is not ported (#559)") {
    // The keyword is the protocol's, and only the protocol's: a three-number
    // `set` is not read as a 2-D address, because one keyword carrying two
    // addressing schemes told apart by arity is a thing a patch has to know
    // rather than read. Nothing is lost — the bare list carries the addressing,
    // and Max's `set` differed from it only by being silent.
    DumpSink cells;
    gMatrixCtrl grid;
    grid.SetParams("4 4 0 1");
    TestHelpers::Wire(grid, 0, cells);

    grid.GetInlet(0)->SetList("set 1 2 1", YSE::T_GUI);
    // Read as `set <index> <value>` with a stray trailing number: cell 1 takes
    // the 2, clamped to the maximum. It is emphatically *not* cell (1, 2).
    CHECK(grid.Value(0, 1) == doctest::Approx(1.f));
    CHECK(grid.Value(1, 2) == doctest::Approx(0.f));

    // A word that merely starts with "set" is not the keyword. The word is
    // skipped, as every non-numeric token in a list is, and its three numbers
    // are Max's cell message — .multislider's reading of "settle 1 2".
    cells.lines.clear();
    grid.GetInlet(0)->SetList("settle 3 3 1", YSE::T_GUI);
    CHECK(grid.Value(3, 3) == doctest::Approx(1.f));
    CHECK(cells.Joined() == Triple(3, 3, 1.f));
  }

  // ─── the whole state, and the one ambiguity ─────────────────────────────────

  TEST_CASE("matrixctrl: a list of columns*rows numbers is the whole state (#559)") {
    DumpSink cells;
    gMatrixCtrl grid;
    grid.SetParams("2 2 0 1");
    TestHelpers::Wire(grid, 0, cells);

    grid.GetInlet(0)->SetList("1 0 0 1", YSE::T_GUI);
    CHECK(grid.GetGuiValue() == Grid({1.f, 0.f, 0.f, 1.f}));
    CHECK(grid.Value(0, 0) == doctest::Approx(1.f));
    CHECK(grid.Value(1, 1) == doctest::Approx(1.f));
    // The echo is the whole grid, in index order — that replay is what carries a
    // restored state on into the crossbar the control drives.
    CHECK(cells.Joined() == Triple(0, 0, 1.f) + " | " + Triple(0, 1, 0.f) + " | " +
                                Triple(1, 0, 0.f) + " | " + Triple(1, 1, 1.f));
  }

  TEST_CASE("matrixctrl: a list of any other length addresses nothing (#559)") {
    DumpSink cells;
    gMatrixCtrl grid;
    grid.SetParams("2 2 0 1");
    grid.GetInlet(0)->SetList("1 1 1 1", YSE::T_GUI);
    TestHelpers::Wire(grid, 0, cells);

    grid.GetInlet(0)->SetList("0 1", YSE::T_GUI);
    grid.GetInlet(0)->SetList("0 1 0 1 0", YSE::T_GUI);
    grid.GetInlet(0)->SetList("", YSE::T_GUI);
    grid.GetInlet(0)->SetList("   ", YSE::T_GUI);
    CHECK(cells.lines.empty());
    CHECK(grid.GetGuiValue() == Repeated(1.f, 4));
  }

  TEST_CASE("matrixctrl: on a three-cell grid the whole state wins the tie (#559)") {
    // The one shape where Max's triple and issue #551's whole state are the same
    // three numbers. GuiValueIsSettable() is an unconditional promise and the
    // triple has `set <index> <value>` standing behind it, so the promise wins.
    gMatrixCtrl grid;
    grid.SetParams("1 3 0 1"); // one column, three rows
    REQUIRE(grid.Cells() == 3u);

    grid.GetInlet(0)->SetList("1 0 1", YSE::T_GUI);
    // Whole state: cells 0 and 2 on. Not "column 1, row 0" — there is no column
    // 1 — and not "column 0 row 1" either.
    CHECK(grid.GetGuiValue() == Grid({1.f, 0.f, 1.f}));

    // And the cell form is still reachable there, by the flat index.
    grid.GetInlet(0)->SetList("set 1 1", YSE::T_GUI);
    CHECK(grid.GetGuiValue() == Repeated(1.f, 3));
  }

  // ─── clear, queries and the dump ────────────────────────────────────────────

  TEST_CASE("matrixctrl: \"clear\" zeroes every cell and replays the grid (#559)") {
    DumpSink cells;
    gMatrixCtrl grid;
    grid.SetParams("2 2 0 1");
    grid.GetInlet(0)->SetList("1 1 1 1", YSE::T_GUI);
    TestHelpers::Wire(grid, 0, cells);

    grid.GetInlet(0)->SetList("clear", YSE::T_GUI);
    CHECK(grid.GetGuiValue() == Repeated(0.f, 4));
    CHECK(cells.lines.size() == 4);
    CHECK(cells.Joined() == Triple(0, 0, 0.f) + " | " + Triple(0, 1, 0.f) + " | " +
                                Triple(1, 0, 0.f) + " | " + Triple(1, 1, 0.f));
  }

  TEST_CASE("matrixctrl: a bang dumps the whole grid as triples (#559)") {
    // Max: "dumps its current state in lists of three values for each cell
    // pair". Ascending by column and then by row, which is ascending flat index,
    // so the dump and GetGuiValue() describe the grid the same way round.
    DumpSink cells;
    gMatrixCtrl grid;
    grid.SetParams("2 3 0 1");
    grid.GetInlet(0)->SetList("1 2 1", YSE::T_GUI);
    TestHelpers::Wire(grid, 0, cells);

    grid.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(cells.lines.size() == 6);
    CHECK(cells.lines[0] == Triple(0, 0, 0.f));
    CHECK(cells.lines[3] == Triple(1, 0, 0.f));
    CHECK(cells.lines[5] == Triple(1, 2, 1.f));
  }

  TEST_CASE("matrixctrl: \"getrow\" and \"getcolumn\" answer on outlet 1 (#559)") {
    DumpSink cells, lines;
    gMatrixCtrl grid;
    grid.SetParams("3 2 0 1");
    TestHelpers::Wire(grid, 0, cells);
    TestHelpers::Wire(grid, 1, lines);

    grid.GetInlet(0)->SetList("1 0 1", YSE::T_GUI); // column 1, row 0
    cells.lines.clear();

    // A row is sent in column order: row 0 is (0,0) (1,0) (2,0).
    grid.GetInlet(0)->SetList("getrow 0", YSE::T_GUI);
    REQUIRE(lines.lines.size() == 1);
    CHECK(lines.lines[0] == Grid({0.f, 1.f, 0.f}));

    // A column is sent in row order: column 1 is (1,0) (1,1).
    grid.GetInlet(0)->SetList("getcolumn 1", YSE::T_GUI);
    REQUIRE(lines.lines.size() == 2);
    CHECK(lines.lines[1] == Grid({1.f, 0.f}));

    // A query changes nothing, so nothing leaves outlet 0.
    CHECK(cells.lines.empty());

    // A line the grid does not have has no answer and sends nothing.
    grid.GetInlet(0)->SetList("getrow 2", YSE::T_GUI);
    grid.GetInlet(0)->SetList("getcolumn 3", YSE::T_GUI);
    grid.GetInlet(0)->SetList("getrow -1", YSE::T_GUI);
    grid.GetInlet(0)->SetList("getrow", YSE::T_GUI);
    CHECK(lines.lines.size() == 2);
  }

  TEST_CASE("matrixctrl: a dump does not nest inside itself (#559)") {
    // Outlet 0's triples are exactly what inlet 0 accepts, so wiring the control
    // back into itself is one cord away — .matrix's guard, for .matrix's reason.
    // Without it every line of a running dump would start a dump of its own.
    FeedbackSink loop;
    gMatrixCtrl grid;
    grid.SetParams("2 2 0 1");
    loop.target = &grid;
    loop.budget = 8;
    TestHelpers::Wire(grid, 0, loop);

    grid.GetInlet(0)->SetBang(YSE::T_GUI);
    // Four cells, four lines. Every one of them asked for a dump of its own and
    // was refused, so the count is the grid rather than the grid squared.
    CHECK(loop.count == 4);
  }

  // ─── the bounds ─────────────────────────────────────────────────────────────

  TEST_CASE("matrixctrl: every cell is clamped into the bounds (#559)") {
    gMatrixCtrl grid;
    grid.SetParams("2 2 0 1");
    grid.GetInlet(0)->SetList("-5 0.5 7 1", YSE::T_GUI);
    CHECK(grid.GetGuiValue() == Grid({0.f, 0.5f, 1.f, 1.f}));
  }

  TEST_CASE("matrixctrl: reversed bounds still bound against the same numbers (#559)") {
    gMatrixCtrl grid;
    grid.SetParams("2 1 100 0");
    grid.GetInlet(0)->SetList("-50 500", YSE::T_GUI);
    CHECK(grid.GetGuiValue() == Grid({0.f, 100.f}));
  }

  TEST_CASE("matrixctrl: an untouched grid reads back inside its bounds (#559)") {
    // .incdec's rule — bounding on the way out as well as in — and its reason:
    // the cells start at 0, which a `.matrixctrl 2 2 1 8` does not contain.
    gMatrixCtrl grid;
    grid.SetParams("2 2 1 8");
    CHECK(grid.GetGuiValue() == Repeated(1.f, 4));
    CHECK(grid.GetGuiValueAt(3) == Text(1.f));
    // And `clear` reaches the nearer end of a range that excludes 0 rather than
    // storing a value the object could not otherwise hold.
    grid.GetInlet(0)->SetList("clear", YSE::T_GUI);
    CHECK(grid.GetGuiValue() == Repeated(1.f, 4));
  }

  // ─── the GUI value protocol (#551) ──────────────────────────────────────────

  TEST_CASE("matrixctrl: it is an N-cell settable GUI object (#559)") {
    gMatrixCtrl grid;
    grid.SetParams("3 2 0 1");
    CHECK(grid.GuiValueIsSettable());
    CHECK(grid.GetGuiValueCount() == 6u);

    grid.GetInlet(0)->SetList("0 1 1 0 0 1", YSE::T_GUI);
    CHECK(grid.GetGuiValueAt(0) == Text(0.f));
    CHECK(grid.GetGuiValueAt(1) == Text(1.f));
    CHECK(grid.GetGuiValueAt(5) == Text(1.f));
    CHECK(grid.GetGuiValueAt(0) != grid.GetGuiValue());
  }

  TEST_CASE("matrixctrl: cell reads past the end answer \"\" rather than indexing (#559)") {
    gMatrixCtrl grid;
    grid.SetParams("2 2 0 1");
    CHECK(grid.GetGuiValueAt(4).empty());
    CHECK(grid.GetGuiValueAt(50).empty());
    CHECK(grid.GetGuiValueAt(0xFFFFFFFFu).empty());
  }

  TEST_CASE("matrixctrl: its own GetGuiValue round-trips through inlet 0 (#559)") {
    // The promise GuiValueIsSettable() makes, and the whole of what `.preset`
    // needs: read one string, send it back as an ordinary list later, get the
    // grid back.
    gMatrixCtrl grid;
    grid.SetParams("3 2 0 1");
    grid.GetInlet(0)->SetList("1 0 0 1 1 0", YSE::T_GUI);
    const std::string stored = grid.GetGuiValue();

    grid.GetInlet(0)->SetList("clear", YSE::T_GUI);
    REQUIRE(grid.GetGuiValue() == Repeated(0.f, 6));

    grid.GetInlet(0)->SetList(stored, YSE::T_GUI);
    CHECK(grid.GetGuiValue() == stored);
    CHECK(grid.Value(0, 0) == doctest::Approx(1.f));
    CHECK(grid.Value(2, 0) == doctest::Approx(1.f));
  }

  // ─── end to end, through the public patcher API ─────────────────────────────

  TEST_CASE("matrixctrl: a host drives a real patch through pHandle (#559)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* grid = p.CreateObject(YSE::OBJ::G_MATRIXCTRL, "2 2 0 1");
    YSE::pHandle* cells = p.CreateObject(YSE::OBJ::G_LIST);
    YSE::pHandle* lines = p.CreateObject(YSE::OBJ::G_LIST);
    REQUIRE(grid != nullptr);
    REQUIRE(cells != nullptr);
    REQUIRE(lines != nullptr);
    p.Connect(grid, 0, cells, 0);
    p.Connect(grid, 1, lines, 0);

    // Max's cell message, through the public path.
    grid->SetListData(0, "1 0 1");
    CHECK(cells->GetGuiValue() == Triple(1, 0, 1.f));

    // The whole state, which is the protocol's write path.
    grid->SetListData(0, "0 1 1 0");
    CHECK(grid->GetGuiValue() == Grid({0.f, 1.f, 1.f, 0.f}));
    // The last line of the replay is the last cell.
    CHECK(cells->GetGuiValue() == Triple(1, 1, 0.f));

    grid->SetListData(0, "getcolumn 0");
    CHECK(lines->GetGuiValue() == Grid({0.f, 1.f}));

    // What a host polls to draw it.
    CHECK(grid->GuiValueIsSettable());
    CHECK(grid->GetGuiValueCount() == 4u);
    CHECK(grid->GetGuiValueAt(1) == Text(1.f));
    CHECK(grid->GetGuiValueAt(4).empty());
  }

  TEST_CASE("matrixctrl: it drives a real .matrix crossbar (#559)") {
    // The headline use, and the reason the object is worth having next to
    // `.matrix` rather than being a two-dimensional `.multislider`: the triples
    // it emits are exactly what a crossbar takes on its control inlet, so the
    // control *is* the routing with nothing in between.
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* grid = p.CreateObject(YSE::OBJ::G_MATRIXCTRL, "2 2 0 1");
    YSE::pHandle* crossbar = p.CreateObject(YSE::OBJ::G_MATRIX, "2 2");
    YSE::pHandle* left = p.CreateObject(YSE::OBJ::G_FLOAT);
    YSE::pHandle* right = p.CreateObject(YSE::OBJ::G_FLOAT);
    REQUIRE(grid != nullptr);
    REQUIRE(crossbar != nullptr);
    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);

    // The control into the crossbar's control inlet, and the crossbar's two
    // routable outlets into two readouts.
    p.Connect(grid, 0, crossbar, 0);
    p.Connect(crossbar, 0, left, 0);
    p.Connect(crossbar, 1, right, 0);

    // Read through `.f`'s own display, which spells a float its way rather than
    // this object's, so the readouts are compared as numbers.
    auto Readout = [](YSE::pHandle* h) { return std::stof(h->GetGuiValue()); };

    // Nothing is connected yet, so nothing routes.
    crossbar->SetFloatData(1, 3.f); // routable inlet 0
    CHECK(Readout(left) == doctest::Approx(0.f));
    CHECK(Readout(right) == doctest::Approx(0.f));

    // Cell (column 0, row 1) on — which the crossbar reads as inlet 0 reaching
    // outlet 1 at a gain of 1, the same three numbers in the same order.
    grid->SetListData(0, "0 1 1");
    crossbar->SetFloatData(1, 5.f);
    CHECK(Readout(left) == doctest::Approx(0.f));
    CHECK(Readout(right) == doctest::Approx(5.f));

    // A whole-state restore replays every cell into the crossbar, which is what
    // makes the echo the point rather than a cost: after it, inlet 0 reaches
    // outlet 0 as well.
    grid->SetListData(0, "1 1 0 0");
    crossbar->SetFloatData(1, 7.f);
    CHECK(Readout(left) == doctest::Approx(7.f));
    CHECK(Readout(right) == doctest::Approx(7.f));

    // And a cell turned off disconnects it again — a gain of 0 is Max's
    // disconnect, so the control's "off" and the crossbar's "not connected" are
    // the same message.
    grid->SetListData(0, "0 0 0");
    crossbar->SetFloatData(1, 9.f);
    CHECK(Readout(left) == doctest::Approx(7.f));
    CHECK(Readout(right) == doctest::Approx(9.f));
  }

  TEST_CASE("matrixctrl: a stored GUI value restores the grid on a fresh patch (#559)") {
    // What `.preset` will do: read the string out of one patcher and push it
    // into the equivalent object in another, through nothing but the public API.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* from = src.CreateObject(YSE::OBJ::G_MATRIXCTRL, "3 2 0 1");
    REQUIRE(from != nullptr);
    from->SetListData(0, "1 0 0 1 1 0");
    const std::string stored = from->GetGuiValue();

    YSE::patcher dst;
    dst.create(2);
    YSE::pHandle* to = dst.CreateObject(YSE::OBJ::G_MATRIXCTRL, "3 2 0 1");
    REQUIRE(to != nullptr);
    REQUIRE(to->GetGuiValue() != stored);

    to->SetListData(0, stored);
    CHECK(to->GetGuiValue() == stored);
  }

  TEST_CASE("matrixctrl: params survive a DumpJSON / ParseJSON round trip (#559)") {
    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_MATRIXCTRL, "3 2 1 8") != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".matrixctrl") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".matrixctrl"));
    CHECK(h->GetParams() == std::string("3 2 1 8"));

    // The shape and the bounds have to still *work*, not merely still be a
    // string: six cells, all of them clamped up to the minimum.
    CHECK(h->GetGuiValueCount() == 6u);
    CHECK(h->GetGuiValue() == Repeated(1.f, 6));
  }

  TEST_CASE("matrixctrl: the cells are run-time state, not parameters (#559)") {
    // pObject.h's rule: a parameter is what the object was created with, not
    // what it has since been told. GetGuiValue() is the form a host stores the
    // cells in.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_MATRIXCTRL, "2 2 0 1");
    REQUIRE(h != nullptr);
    h->SetListData(0, "1 1 1 1");
    CHECK(h->GetParams() == std::string("2 2 0 1"));
  }

  TEST_CASE("matrixctrl: a re-dimension takes effect through the handle (#559)") {
    // The dimensions are creation parameters, so changing them is a structural
    // change: the object is rebuilt rather than resized under the audio thread,
    // and the grid comes back in the new shape.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_MATRIXCTRL, "2 2 0 1");
    REQUIRE(h != nullptr);
    REQUIRE(h->GetGuiValueCount() == 4u);

    h->SetParams("4 3 0 1");
    CHECK(h->GetParams() == std::string("4 3 0 1"));
    CHECK(h->GetGuiValueCount() == 12u);
    CHECK(h->GetGuiValue() == Repeated(0.f, 12));
    // Still one inlet and two outlets — nothing structural about the *ports*
    // depends on the grid, so a saved patch's cords still land where they did.
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 2);
  }

  // ─── documentation ──────────────────────────────────────────────────────────
  // test_doc_coverage.cpp already asserts non-empty docs for every registered
  // object; this pins the category and the parameter, which is what a binding
  // generator and a saved patch both key on.

  TEST_CASE("matrixctrl: documents itself as GUI with one creation parameter (#559)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::G_MATRIXCTRL));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GUI);
    CHECK(obj->NumInputs() == 1);
    CHECK(obj->NumOutputs() == 2);

    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == "columns");
  }

  // ─── real time ──────────────────────────────────────────────────────────────

  TEST_CASE("matrixctrl: no message path allocates (#559)") {
    // Including the whole-state write and the dump, which are the ones that
    // could: the list handler compares its keywords and walks its numbers in
    // place rather than into a buffer the size of the grid, and every send fills
    // a string reserved at construction. The counter is read inside the scope
    // and asserted outside it, since doctest's own machinery allocates on first
    // use.
    MultiSink cells, lines;
    gMatrixCtrl grid;
    grid.SetParams("3 2 0 1");
    TestHelpers::Wire(grid, 0, cells);
    TestHelpers::Wire(grid, 1, lines);

    // Warm every path, and the sinks' own buffers at the longest length the
    // probe below will send them.
    grid.GetInlet(0)->SetList("1 0 1", YSE::T_GUI);
    grid.GetInlet(0)->SetList("1 0 0 1 1 0", YSE::T_GUI);
    grid.GetInlet(0)->SetList("set 3 1", YSE::T_GUI);
    grid.GetInlet(0)->SetList("getrow 1", YSE::T_GUI);
    grid.GetInlet(0)->SetList("getcolumn 2", YSE::T_GUI);
    grid.GetInlet(0)->SetList("clear", YSE::T_GUI);
    grid.GetInlet(0)->SetBang(YSE::T_GUI);
    REQUIRE(cells.gotList);
    REQUIRE(lines.gotList);

    int count = -1;
    {
      TestHelpers::ProbeScope probe;
      grid.GetInlet(0)->SetList("2 1 1", YSE::T_GUI);
      grid.GetInlet(0)->SetList("0 1 1 0 0 1", YSE::T_GUI);
      grid.GetInlet(0)->SetList("set 0 1", YSE::T_GUI);
      grid.GetInlet(0)->SetList("getrow 0", YSE::T_GUI);
      grid.GetInlet(0)->SetList("getcolumn 1", YSE::T_GUI);
      grid.GetInlet(0)->SetList("clear", YSE::T_GUI);
      grid.GetInlet(0)->SetBang(YSE::T_GUI);
      count = TestHelpers::g_alloc_count.load();
    }
    CHECK(count == 0);
  }

} // TEST_SUITE("patcher")
