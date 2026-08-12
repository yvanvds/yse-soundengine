#include "gMatrixCtrl.h"
#include "../../implementations/logImplementation.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

#define className gMatrixCtrl

namespace {

  // Characters one `<column> <row> <value>` list can need without reallocating:
  // two ints, a formatted float and two separators.
  constexpr std::size_t kTripleCapacity =
      (2 * (std::size_t)FORMAT_INT_WIDTH) + (std::size_t)kExprValueTextMax + 2;

  // An int as text, through the patcher's one int formatter rather than
  // std::to_string. Control-thread only (the construction-time clamp logs).
  std::string IntText(int value) {
    char digits[FORMAT_INT_WIDTH];
    const std::size_t written = WriteInt(value, digits);
    return std::string(digits, written);
  }

  // The bounds of the token starting at or after `from`, or false when there is
  // none. Walked in place rather than through substr: this runs on whichever
  // thread the message arrived on.
  bool NextToken(const char* text, std::size_t length, std::size_t from, std::size_t& begin,
                 std::size_t& end) {
    begin = from;
    while (begin < length && IsSelectorSeparator(text[begin]))
      begin++;
    end = begin;
    while (end < length && !IsSelectorSeparator(text[end]))
      end++;
    return end > begin;
  }

  // The next token that reads as a number, advancing `from` past it. Tokens that
  // are not numbers are *skipped* rather than ending the walk, which is
  // ExprParseFloatList's policy and the one `.multislider` walks a long list
  // with.
  bool NextNumber(const char* text, std::size_t length, std::size_t& from, float& out) {
    std::size_t begin = 0;
    std::size_t end = 0;
    while (NextToken(text, length, from, begin, end)) {
      from = end;
      if (ReadNumericToken(text + begin, end - begin, out)) return true;
    }
    return false;
  }

  // How many numbers the whole message holds. Read before anything is stored,
  // because the count is what tells the whole state from Max's cell triple — and
  // read by walking the text a second time rather than into a buffer, since the
  // whole state of a 256x256 grid is 65536 numbers and may arrive down a cord on
  // the audio thread.
  std::size_t CountNumbers(const char* text, std::size_t length) {
    std::size_t cursor = 0;
    std::size_t count = 0;
    float number = 0.f;
    while (NextNumber(text, length, cursor, number))
      count++;
    return count;
  }

  // Whether the `length` characters at `text` are exactly `word`. The command
  // words are compared in place for the same reason the tokens are walked in
  // place: a list may arrive on the audio thread.
  bool TokenIs(const char* text, std::size_t length, const char* word, std::size_t wordLength) {
    if (length != wordLength) return false;
    for (std::size_t i = 0; i < length; i++) {
      if (text[i] != word[i]) return false;
    }
    return true;
  }

  constexpr char kInDoc[] =
      "The grid, and the only inlet. It takes a bang and a list and nothing else: Max documents no "
      "int or float method - a matrixctrl is driven by lists and by the mouse, and there is no "
      "mouse here - so those are declined outright rather than swallowed, which keeps "
      "GetAcceptedTypes() reporting the real contract. A list of three numbers is Max's cell "
      "message '<column> <row> <value>' and sets that cell; a list of exactly columns*rows numbers "
      "is the whole state and sets every cell, which is issue #551's round trip and the string "
      "GetGuiValue() produced; 'set <index> <value>' sets one cell by flat index, #551's cell "
      "write, where the index is column*rows+row. Those two bare forms collide on exactly one "
      "shape - a three-cell grid - and there the whole state wins, because GuiValueIsSettable() is "
      "an unconditional promise and the triple has 'set' standing behind it. Max's own 'set' is "
      "not "
      "ported: it takes the same '<column> <row> <value>' its list method takes, so one keyword "
      "would carry a two-dimensional address and a flat index at once, told apart only by how many "
      "numbers follow; and it differs from the list method only by being silent, which is what "
      "cannot be ported, since headless the outlet is the only way a change reaches anything. "
      "'clear' sets every cell to 0, 'getrow <row>' and 'getcolumn <column>' send that line's "
      "values out outlet 1, and a bang dumps the whole grid. Every write emits. A cell outside the "
      "grid, or a list of any other length, is ignored rather than folded onto a real cell, and "
      "every value is clamped into the minimum-maximum bounds. Not here: everything about drawing "
      "and the mouse (the picture and image messages, disablecell / enablecell), 'dictionary "
      "<name>' which names a Max dictionary object this patcher has none of, and Max's attributes "
      "including the one/column, one/row and one/matrix exclusivity modes - the patcher has no "
      "attribute mechanism, and a rule that silently cleared cells a patch had just set would need "
      "one to turn it off again.";

  constexpr char kCellOutDoc[] =
      "Cells, as '<column> <row> <value>' - Max's left outlet, and deliberately the same three "
      "numbers in the same order that .matrix and .router take on their control inlet, so wiring "
      "this outlet into either crossbar makes the control the routing with no glue in between. One "
      "list for a one-cell write; every cell, in index order, for a whole-state write, a 'clear' "
      "or "
      "a bang, which Max words as 'dumps its current state in lists of three values for each cell "
      "pair'. That a restore replays the whole grid is the point rather than a cost: it is what "
      "carries the restored state on into the crossbar this control drives. A dump running already "
      "is not nested inside itself - these triples are exactly what inlet 0 accepts, so wiring the "
      "outlet back is one cord away.";

  constexpr char kLineOutDoc[] =
      "One row's or one column's values as a single list, and only in answer to a 'getrow <row>' "
      "or "
      "'getcolumn <column>' - Max's messages and Max's right outlet. A row is sent in column order "
      "and a column in row order, both ascending. A query naming a line the grid does not have "
      "sends nothing, the family's rule for a question with no answer. The index is not echoed "
      "back "
      "with the values: the patch asked for it, so it already knows it. Silent otherwise - no cell "
      "write ever leaves here.";

} // namespace

CONSTRUCT() {
  // One inlet, hot, taking a bang and a list. No int and no float: Max
  // documents neither, and a handler that swallowed them could not say so.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);

  ADD_OUT_LIST; // 0: `<column> <row> <value>`
  ADD_OUT_LIST; // 1: a row or a column, in answer to a query

  // The grid is built by ShapeGrid(), so a saved `.matrixctrl 16 4 0 1` comes
  // back sixteen columns by four rows. The clear callback is what makes
  // `SetParams("")` return the object to the no-argument shape rather than
  // leaving the previous dimensions in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // Both axes default to DEFAULT_AXIS and the bounds to 0-1. Also the shape
  // ClearParams() restores.
  ShapeGrid();

  ADD_DESCRIPTION(
      "A grid of cell states as one object - Max's matrixctrl, whose cells are addressed as "
      "'<column> <row> <value>' and whose left outlet speaks the same three numbers. A step "
      "sequencer's grid, a drum pattern and a routing patchbay are all an N-by-M table of small "
      "values, driven and drawn as one control but addressed one cell at a time. It is worth "
      "having next to .matrix and .router rather than being a two-dimensional .multislider "
      "precisely because of those three numbers: .matrix sets a cell with the bare list '<inlet> "
      "<outlet> <gain>' and .router with '<inlet> <outlet> <on>', so wiring outlet 0 into either "
      "crossbar's control inlet makes the control the routing, one object holding the state and "
      "the other switching the messages, with no glue between them. Issue #551's GUI state is a "
      "list, so the grid is flattened column-major: cell (column, row) is at column*rows+row, the "
      "row moving fastest. That is the lexicographic rank of the address as Max writes it - the "
      "last coordinate written varies quickest, so nothing has to remember that the object "
      "reverses them - and it is the layout .matrix already uses for its own gain table, so a cell "
      "index here and the offset of the cell it drives there are the same number. Max's 'set' is "
      "not ported: it takes the same '<column> <row> <value>' its list method takes, so the "
      "keyword would carry a two-dimensional address while the protocol's carries a flat index, "
      "told apart only by how many numbers follow, and a mistyped 'set 2 3 1' that lost its value "
      "would silently write flat cell 2. Nothing is lost by dropping it, because Max's 'set' "
      "differs from its list method only by being silent, and silence is what cannot be ported: "
      "Max can afford a quiet write because its cells are drawn, while here the outlet is the only "
      "way a change reaches anything, and an object whose purpose is to drive a crossbar that "
      "learns of a change only by being told cannot have a form that tells nothing. So every write "
      "emits. A bare list is the whole state at exactly columns*rows numbers and Max's cell triple "
      "at exactly 3; they collide only on a three-cell grid, where the whole state wins because "
      "GuiValueIsSettable() is an unconditional promise while the triple has 'set <index> <value>' "
      "standing behind it. Dimensions come from the creation arguments and nowhere else - one list "
      "length cannot recover two of them, since twelve numbers are 3-by-4 or 4-by-3 - so unlike "
      ".multislider there is no listresize and no live bound: the cells are allocated exactly "
      "columns*rows wide by the parameter callbacks on the control thread before the object is "
      "wired or published, and a live SetParams replaces the object through the graph swap, which "
      "is honest for a change that alters what every index means. That is also why the ceiling can "
      "stay at the family's 256 per axis, .matrix's MAX_PORTS, so every crossbar the patcher can "
      "build has a control able to address all of it: an 8-by-8 grid costs 256 bytes, not the "
      "ceiling. .multislider had to lower its own ceiling because it emits its whole state as one "
      "list and reserves a send buffer for the worst case; this object emits one three-number list "
      "per cell, so its send buffer is one triple wide whatever the grid is, and the only place "
      "the ceiling is felt is GetGuiValue(), which the protocol defines as one call and one "
      "allocation - which is exactly what the per-cell reads exist for. The inlet takes a bang and "
      "a list and declines int and float, which Max does not document, the way .matrix, .router "
      "and .decode decline what they have no meaning for. Outlet 0 carries cells as triples, "
      "outlet 1 a row or a column in answer to 'getrow' / 'getcolumn'. Cells are clamped into the "
      "minimum-maximum bounds, defaulting to 0-1: Max's default range of 2 is two states per cell, "
      "read here as off and on, and where Max counts integer states because its cells are clicked "
      "up through them one at a time, headless there is no click and the cells take the family's "
      "float bounds instead - which also lets the control hold a fractional .matrix gain that an "
      "integer state count could not. Not modelled: everything about drawing and the mouse, "
      "'dictionary <name>' naming a Max dictionary object this patcher has none of, and Max's "
      "attributes generally, including the one/column, one/row and one/matrix exclusivity modes, "
      "since the patcher has no attribute mechanism and an exclusivity rule that silently cleared "
      "cells a patch had just set would need one to turn it off again. Nothing on any path "
      "allocates, locks or blocks: the grid, both send buffers and every port are built on the "
      "control thread, the list handler compares its keywords and walks its numbers in place, and "
      "every send fills a string reserved at construction immediately before it is sent.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "grid", kInDoc, "minimum-maximum");
  OUTLET_DOC(0, "cell", kCellOutDoc, "column row value");
  OUTLET_DOC(1, "line", kLineOutDoc, "minimum-maximum");
  PARAM_DOC(
      "columns", "8",
      "The grid, in Max's address order: how many columns, how many rows, then the lowest and "
      "highest value a cell can hold. Each axis is clamped to 1-256 and defaults to 8 - Max "
      "documents no default for its rows / columns attributes, and 8 is the smallest grid doing "
      "something a single control could not. The bounds default to 0-1, Max's default range of 2 "
      "read as off and on. Both dimensions are creation arguments and nothing else changes them: "
      "one list length cannot recover two of them, so there is no .multislider-style listresize, "
      "and the storage is allocated exactly columns*rows wide on the control thread before the "
      "object is published. Registering the parameter callbacks therefore makes a live SetParams "
      "replace the object rather than resize it under the audio thread, which is the honest answer "
      "for a change that alters what every cell index means. A float dimension is truncated; "
      "anything that is not a whole finite number is ignored and leaves that default in place. The "
      "cell values are not parameters and do not survive a save - GetGuiValue() is the form a host "
      "stores them in, and inlet 0 takes it straight back.",
      "1-256, optional rows 1-256, optional minimum, optional maximum");
}

// ─── the grid ─────────────────────────────────────────────────────────────────

void gMatrixCtrl::ShapeGrid() {
  // Rebuilt rather than resized: both dimensions come from the arguments, and
  // the cells have to agree with them. Safe because every caller runs before the
  // object is wired or published — the constructor, and the two parameter
  // callbacks, which patcherImplementation::CreateObjectUnlocked runs before
  // AssignGraphIds. A *live* SetParams never reaches here on a published object:
  // registering the callbacks makes ParamsNeedRebuild() true, so #234 replaces
  // the object instead.
  int axes[2] = {DEFAULT_AXIS, DEFAULT_AXIS};
  int requested[2] = {DEFAULT_AXIS, DEFAULT_AXIS};
  bool clamped[2] = {false, false};
  int read = 0;

  minimum = DEFAULT_MINIMUM;
  maximum = DEFAULT_MAXIMUM;

  for (const std::string& token : creationArgs) {
    if (read >= 4) break;

    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens; an empty argument is not a number.
    if (token.empty()) continue;

    float number = 0.f;
    // Strict on purpose, as the rest of the family is: ExprParseFloatList would
    // read `5abc` as 5 and fold `1e999` to 0, and neither answers "is this
    // creation argument a number at all".
    if (!ReadNumericToken(token, number)) continue;

    if (read == 2) {
      // The bounds are coefficients rather than counts and are taken as written;
      // Bound() orders them, so a reversed pair still bounds against the right
      // two numbers.
      minimum = number;
      read++;
      continue;
    }
    if (read == 3) {
      maximum = number;
      read++;
      continue;
    }

    requested[read] = ExprToInt(number);
    axes[read] = requested[read];
    if (axes[read] > MAX_AXIS) axes[read] = MAX_AXIS;
    if (axes[read] < MIN_AXIS) axes[read] = MIN_AXIS;
    clamped[read] = (requested[read] != axes[read]);
    read++;
  }

  // The control thread, before the object is wired or published, so this is the
  // one place a clamp can be *said* rather than merely made observable through
  // Columns() and Rows().
  static const char* const kAxes[2] = {"column", "row"};
  for (int axis = 0; axis < 2; axis++) {
    if (!clamped[axis]) continue;
    INTERNAL::LogImpl().emit(E_WARNING, std::string("patcher: .matrixctrl ") + kAxes[axis] +
                                            " count " + IntText(requested[axis]) + " is outside " +
                                            IntText(MIN_AXIS) + "-" + IntText(MAX_AXIS) +
                                            "; clamped to " + IntText(axes[axis]));
  }

  columns = axes[0];
  rows = axes[1];
  cellCount = (std::size_t)columns * (std::size_t)rows;

  // Replaced whole rather than resized — a vector of atomics can be neither
  // resized nor element-assigned, and there is nothing to preserve across a
  // re-dimension, since the shape is what every index is relative to. Every cell
  // is then stored explicitly at rest: an atomic that is merely
  // default-constructed holds no defined value under C++17, and "every cell
  // always holds a number" is what lets a bare grid be read and dumped.
  cells = std::make_unique<std::atomic<float>[]>(cellCount);
  for (std::size_t i = 0; i < cellCount; i++)
    cells[i].store(0.f, std::memory_order_relaxed);

  // The two allocations a send would otherwise need. The line buffer is sized
  // for the longer axis, which is the longest list outlet 1 can carry.
  tripleText.reserve(kTripleCapacity);
  const std::size_t longest = (std::size_t)(columns > rows ? columns : rows);
  lineText.reserve(longest * ((std::size_t)kExprValueTextMax + 1));
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave the no-argument
  // object behind rather than one still holding the previous dimensions.
  creationArgs.clear();
  ShapeGrid();
}

PARM_PARSE() {
  ShapeGrid();
}

std::size_t gMatrixCtrl::IndexOf(int column, int row) const {
  // Out of range answers the cell count rather than an index, so every caller
  // range-checks by comparing against Cells() and none of them has to repeat the
  // two bounds tests.
  if (column < 0 || column >= columns) return cellCount;
  if (row < 0 || row >= rows) return cellCount;
  // Column-major: the row moves fastest, which is the order the address is
  // written in and the order `.matrix` lays its own table out in.
  return ((std::size_t)column * (std::size_t)rows) + (std::size_t)row;
}

float gMatrixCtrl::Bound(float value) const {
  // The bounds are taken as an ordered pair, `.rslider`'s and `.incdec`'s rule,
  // so a reversed argument pair still bounds against the right two numbers. A
  // NaN fails both compares and is answered with the lower bound rather than
  // passed through, since every value this object reports must be inside the
  // range it documents.
  const float lowBound = minimum < maximum ? minimum : maximum;
  const float highBound = minimum < maximum ? maximum : minimum;
  if (!(value >= lowBound)) return lowBound;
  if (value > highBound) return highBound;
  return value;
}

float gMatrixCtrl::CellAt(std::size_t index) const {
  return Bound(cells[index].load(std::memory_order_relaxed));
}

void gMatrixCtrl::StoreCell(std::size_t index, float value) {
  // A cell the grid does not have is not an error to report, it is a message
  // with nothing to address — `.gate`'s, `.spray`'s, `.matrix`'s and
  // `.router`'s reading of an index out of range.
  if (index >= cellCount) return;
  cells[index].store(Bound(value), std::memory_order_relaxed);
}

float gMatrixCtrl::Value(int column, int row) const {
  const std::size_t index = IndexOf(column, row);
  if (index >= cellCount) return 0.f;
  return CellAt(index);
}

std::size_t gMatrixCtrl::Render(float value, char* out) {
  const int written = ExprFormatValue(ExprValue::Float(value), out, kExprValueTextMax);
  return written > 0 ? (std::size_t)written : 0;
}

// ─── output ───────────────────────────────────────────────────────────────────

void gMatrixCtrl::SendCell(int column, int row, YSE::THREAD thread) {
  const std::size_t index = IndexOf(column, row);
  if (index >= cellCount) return;

  // Refilled per cell into memory reserved at construction, so a dump of any
  // size touches no allocator. Filled immediately before its send, so that a
  // patch re-entering inside that send cannot have left the buffer holding the
  // inner message's text by the time this one is read (`.funnel`'s lesson).
  char digits[FORMAT_INT_WIDTH];
  std::size_t written = WriteInt(column, digits);
  tripleText.assign(digits, written);
  tripleText.push_back(' ');
  written = WriteInt(row, digits);
  tripleText.append(digits, written);
  tripleText.push_back(' ');

  char text[kExprValueTextMax];
  written = Render(CellAt(index), text);
  tripleText.append(text, written);

  outputs[0].SendList(tripleText, thread);
}

void gMatrixCtrl::SendAll(YSE::THREAD thread) {
  // Outlet 0's triples are exactly what inlet 0 accepts, so a patch can wire the
  // control back into itself in one cord. Without this a triple arriving there
  // during a dump would start a second full dump inside the first, and each of
  // *its* lines a third: the send-depth guard bounds the depth of that, but the
  // breadth is the cell count per level and multiplies. `.matrix`'s guard, for
  // `.matrix`'s reason.
  if (dumping) return;
  dumping = true;

  // Ascending by column and then by row, which is ascending flat index — the
  // order GetGuiValue() spells the state in, so the dump and the bulk read
  // describe the grid the same way round.
  for (int column = 0; column < columns; column++) {
    for (int row = 0; row < rows; row++)
      SendCell(column, row, thread);
  }

  dumping = false;
}

void gMatrixCtrl::SendLine(bool wholeColumn, int index, YSE::THREAD thread) {
  // Max: "sends the values of the cells in the column designated by the number
  // out its right outlet", and the same for a row.
  const int count = wholeColumn ? rows : columns;

  lineText.clear();
  char text[kExprValueTextMax];
  for (int i = 0; i < count; i++) {
    const std::size_t cell = wholeColumn ? IndexOf(index, i) : IndexOf(i, index);
    if (cell >= cellCount) return;
    if (i > 0) lineText.push_back(' ');
    const std::size_t written = Render(CellAt(cell), text);
    lineText.append(text, written);
  }

  outputs[1].SendList(lineText, thread);
}

// ─── inlets ───────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  // Max: "dumps its current state in lists of three values for each cell pair".
  if (inlet != 0) return;
  SendAll(thread);
}

LIST_IN(ListIn) {
  // Registered on inlet 0 only, but routed anyway: a later inlet must never
  // start writing the grid because someone added a handler above.
  if (inlet != 0) return;

  const char* text = value.c_str();
  const std::size_t length = value.size();

  std::size_t begin = 0;
  std::size_t end = 0;
  // An empty list has nothing to address and is not a bang.
  if (!NextToken(text, length, 0, begin, end)) return;

  if (TokenIs(text + begin, end - begin, "set", 3)) {
    // "set <index> <value>" — issue #551's cell write, and *only* that. Max's
    // own `set` takes `<column> <row> <value>`, which the bare list carries
    // over; see the class comment for why one keyword is not made to mean both.
    std::size_t cursor = end;
    float index = 0.f;
    float cellValue = 0.f;
    if (!NextNumber(text, length, cursor, index)) return;
    if (!NextNumber(text, length, cursor, cellValue)) return;
    // Range-checked rather than indexed, as the protocol requires: an index
    // outside the grid — or a NaN, which fails the first compare — is dropped.
    // A fractional index truncates towards zero, as every other index in the
    // patcher does.
    if (!(index >= 0.f)) return;
    const std::size_t at = (std::size_t)ExprToInt(index);
    if (at >= cellCount) return;
    StoreCell(at, cellValue);
    // Echoed as the address Max speaks, which is the address the crossbar
    // downstream understands, whichever of the two forms wrote it.
    SendCell((int)(at / (std::size_t)rows), (int)(at % (std::size_t)rows), thread);
    return;
  }

  if (TokenIs(text + begin, end - begin, "clear", 5)) {
    // Max: "sets the value of all cells to 0". Bounded on the way in like every
    // other write, so a grid whose range excludes 0 clears to the nearer end of
    // it rather than to a value it could not otherwise hold.
    for (std::size_t i = 0; i < cellCount; i++)
      StoreCell(i, 0.f);
    SendAll(thread);
    return;
  }

  if (TokenIs(text + begin, end - begin, "getrow", 6) ||
      TokenIs(text + begin, end - begin, "getcolumn", 9)) {
    const bool wholeColumn = (end - begin) == 9;
    std::size_t cursor = end;
    float index = 0.f;
    if (!NextNumber(text, length, cursor, index)) return;
    if (!(index >= 0.f)) return;
    const int at = ExprToInt(index);
    if (at >= (wholeColumn ? columns : rows)) return;
    SendLine(wholeColumn, at, thread);
    return;
  }

  // A bare list is one of two things, and its length is what says which. The
  // whole state — the exact string GetGuiValue() produced — is `cellCount`
  // numbers; Max's cell message is three. They collide only on a three-cell
  // grid, and there the whole state wins, because GuiValueIsSettable() is an
  // unconditional promise while the triple has `set <index> <value>` standing
  // behind it. Any other length addresses nothing and is ignored.
  const std::size_t numbers = CountNumbers(text, length);

  if (numbers == cellCount) {
    std::size_t cursor = 0;
    float number = 0.f;
    for (std::size_t i = 0; i < cellCount; i++) {
      if (!NextNumber(text, length, cursor, number)) break;
      StoreCell(i, number);
    }
    // The whole grid, which is what carries a restored state on into the
    // crossbar this control drives.
    SendAll(thread);
    return;
  }

  if (numbers == 3) {
    // Max: "sets cells in the matrixctrl object using the format <horizontal-
    // coordinate vertical-coordinate value>".
    std::size_t cursor = 0;
    float column = 0.f;
    float row = 0.f;
    float cellValue = 0.f;
    if (!NextNumber(text, length, cursor, column)) return;
    if (!NextNumber(text, length, cursor, row)) return;
    if (!NextNumber(text, length, cursor, cellValue)) return;
    // NaN fails these compares, so it never reaches ExprToInt.
    if (!(column >= 0.f) || !(row >= 0.f)) return;
    const int atColumn = ExprToInt(column);
    const int atRow = ExprToInt(row);
    const std::size_t index = IndexOf(atColumn, atRow);
    if (index >= cellCount) return;
    StoreCell(index, cellValue);
    SendCell(atColumn, atRow, thread);
  }
}

// ─── the GUI value protocol (issue #551) ──────────────────────────────────────

GUI_VALUE() {
  // A local rather than one of the send buffers: those belong to the message
  // path on the audio thread, and this runs on the host thread. One call, one
  // allocation — which is what the protocol asks a bulk read to be, and the one
  // place the grid's ceiling is felt.
  std::string out;
  out.reserve(cellCount * ((std::size_t)kExprValueTextMax + 1));

  char text[kExprValueTextMax];
  for (std::size_t i = 0; i < cellCount; i++) {
    if (i > 0) out.push_back(' ');
    const std::size_t written = Render(CellAt(i), text);
    out.append(text, written);
  }
  return out;
}

GUI_VALUE_COUNT() {
  return (unsigned int)cellCount;
}

GUI_VALUE_AT() {
  // Range-checked against the count rather than indexed, as the protocol
  // requires: past the end is "", never the whole state again.
  if ((std::size_t)index >= cellCount) return std::string();
  char text[kExprValueTextMax];
  const std::size_t written = Render(CellAt((std::size_t)index), text);
  return std::string(text, written);
}

#undef className
