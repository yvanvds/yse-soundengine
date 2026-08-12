#include "gMultiSlider.h"
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

#define className gMultiSlider

namespace {

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
  // ExprParseFloatList's policy — the one .rslider parses its two ends with, and
  // the one this object would have used had a 1024-cell list not wanted walking
  // in place instead of into a stack buffer.
  bool NextNumber(const char* text, std::size_t length, std::size_t& from, float& out) {
    std::size_t begin = 0;
    std::size_t end = 0;
    while (NextToken(text, length, from, begin, end)) {
      from = end;
      if (ReadNumericToken(text + begin, end - begin, out)) return true;
    }
    return false;
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
      "The bank, and the only inlet — everything here emits, which is what a hot inlet in this "
      "patcher does. A list of numbers sets the cells from its first 1024 numbers and resizes the "
      "bank to how many there were, which is Max's list method plus its 'listresize' attribute; an "
      "int or a float sets *every* live cell to that number, Max's int/float method; a bang "
      "re-sends. 'set <index> <value>' writes one cell and leaves the rest — Max's own message and "
      "issue #551's cell write, which for this object are the same message. 'fetch <index>' sends "
      "that cell's value out outlet 1. An index outside the live cells is dropped rather than "
      "folded onto a real cell, and every value is clamped into the minimum-maximum bounds. Max's "
      "silent forms ('setlist', 'select') are not here, because a hot inlet cannot be silent and "
      "the plain list already sets the bank; nor are its reductions ('sum', 'minimum', 'maximum', "
      "'quantiles', 'normalize'), which are operations on the list this object already hands out "
      "whole — '.zl' is where list processing lives. 'range' / 'setmin' / 'setmax' are not "
      "messages "
      "either: bounds are creation parameters in this family, as they are for .dial, .incdec and "
      ".rslider.";

  constexpr char kBankDoc[] =
      "The whole bank as one list, in cell order, sent after every message the inlet accepts. It "
      "is "
      "spelled exactly as GetGuiValue() spells it, so a host reading the state and a patch "
      "receiving it downstream cannot disagree about what it says, and it is the string inlet 0 "
      "takes straight back (issue #551). One sample of the state is taken at the top of the send, "
      "so a cell moved by a message arriving down one of the cords mid-send cannot make the list "
      "describe two different banks.";

  constexpr char kCellDoc[] =
      "One cell's value, and only in answer to a 'fetch <index>' — Max's message and Max's right "
      "outlet. It is sent from the handler, so it lands *before* the bank does on outlet 0, which "
      "is the patcher's right-to-left ordering. A fetch of an index outside the live cells sends "
      "nothing, the family's rule for a question with no answer. The index is not echoed with the "
      "value: the patch asked for it, so it already knows it.";

} // namespace

CONSTRUCT() {
  // Every cell explicitly at rest. The vector's own construction value-
  // initialises them, but an atomic that is merely default-constructed holds no
  // defined value under C++17, and "every live cell always holds a number" is
  // the whole premise of a bank that can grow back into cells it once hid.
  for (std::size_t i = 0; i < MAX_CELLS; i++)
    cells[i].store(0.f, std::memory_order_relaxed);

  ADD_IN_0;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);
  REG_BANG_IN(BangIn);

  ADD_OUT_LIST; // 0: the whole bank
  ADD_OUT_FLOAT; // 1: one cell, from `fetch`

  // In place before Register() hands the fields to the parameter system: a
  // creation argument overwrites them, no argument leaves them alone. 8 rather
  // than Max's 1 — see the class comment. The bounds are the family's 0-127.
  size = (int)DEFAULT_CELLS;
  minimum = 0.f;
  maximum = 127.f;

  ADD_PARAM(size);
  ADD_PARAM(minimum);
  ADD_PARAM(maximum);

  listText.reserve(LIST_CAPACITY);

  ADD_DESCRIPTION(
      "A bank of values as one object - Max's multislider, 'an array of sliders'. Every other "
      "control in the patcher holds one number (.slider a position, .dial a value, .incdec a step, "
      ".rslider a pair), and the only way to hold a list today is .l, which is one opaque blob "
      "with "
      "no way to reach an element. A step sequencer's levels, a graphic EQ's bands, a spectral "
      "envelope and a set of voice gains are all N numbers driven and drawn as one control but "
      "addressed one at a time. It is deliberately not .table and both stay: .table is dense "
      "storage a patch reads and writes by address and saves with the patch, while this is a "
      "control surface the host draws and the user drives, whose whole bank leaves as one list on "
      "every message and whose state is what GetGuiValue() reports. The one inlet is hot and "
      "everything on it emits: a list sets the cells and resizes the bank to the list's length "
      "(Max's list method and its listresize attribute), an int or float sets every live cell, a "
      "bang re-sends, 'set <index> <value>' writes one cell and 'fetch <index>' sends one cell's "
      "value out outlet 1. Max's set and issue #551's cell write are the same message here, which "
      "on .rslider they could not be. Outlet 0 carries the whole bank, spelled exactly as the GUI "
      "value is spelled and accepted straight back on inlet 0. The number of cells comes from the "
      "'size' argument - Max keeps it in an attribute and the patcher has none, .table's "
      "reconciliation - and from any list of a different length; both are one atomic store, "
      "because "
      "the bank is allocated whole at 1024 cells on the control thread and never resized, so "
      "'size' "
      "is a bound and not a capacity. That is what makes a live resize need no lock, no copy and "
      "no "
      "rebuild: a walk samples the count once and can never index outside memory that exists, and "
      "nothing structural depends on N. Values survive a resize in both directions - shrinking "
      "hides the tail rather than clearing it, so shortening a sequence and lengthening it again "
      "does not cost its steps - and clearing instead would be an O(N) write on whichever thread "
      "the resize arrived on, which is the audio thread by both routes. Cells are clamped into the "
      "minimum-maximum bounds on the way out as well as in, so a live re-range shows immediately "
      "and an untouched '.multislider 4 20 20000' reads back as four 20s. The default count is 8 "
      "rather than Max's 1, which only makes sense with listresize doing the shaping. Max's "
      "ceiling "
      "of 4096 is lowered to 1024: the whole state is emitted as one list, so the send buffer is "
      "reserved for the worst case at construction, and the store for arrays past that is .table. "
      "Not ported: the silent setters (setlist, select), since a hot inlet here always calculates; "
      "the reductions (sum, minimum, maximum, quantiles, normalize), which belong to .zl on a list "
      "this object already emits whole; range / setmin / setmax, bounds being creation parameters "
      "in this family; and everything about drawing and the mouse, the patcher being headless.");
  ADD_CATEGORY(pCategory::GUI);
  INLET_DOC(0, "bank", kInDoc, "minimum-maximum");
  OUTLET_DOC(0, "bank", kBankDoc, "minimum-maximum");
  OUTLET_DOC(1, "cell", kCellDoc, "minimum-maximum");
  PARAM_DOC(
      "size", "8",
      "How many cells the bank is built with. Max keeps this in its 'size' attribute and the "
      "patcher has no attribute mechanism, so it is a parameter - the same reconciliation "
      ".table makes. Clamped to 1-1024, the array being allocated whole at 1024 cells once at "
      "construction so nothing on a message path ever has to grow it. It is a bound rather "
      "than a capacity, which is why a live SetParams rides the wait-free scalar plan (issue "
      "#234) and keeps the object and every value in it, and why a list of a different length "
      "may change the live count afterwards - Max's listresize, on by default. The parameter "
      "is therefore the count the object is *built* with; the live count, like the values, is "
      "run-time state and is not saved. The default is 8 rather than Max's 1: Max can default "
      "to 1 because listresize reshapes the object from its first list, but a 1-cell "
      "multislider written out in a patch is a .slider with a longer name.",
      "1-1024");
  PARAM_DOC("minimum", "0", "Lowest value any cell can hold.", "any float");
  PARAM_DOC("maximum", "127", "Highest value any cell can hold.", "any float");
}

// ─── the bank ─────────────────────────────────────────────────────────────────

unsigned int gMultiSlider::Cells() const {
  // Acquire, paired with the release store in the whole-state write: a reader
  // that sees a longer bank must also see the cells that made it longer.
  const int asked = size.load(std::memory_order_acquire);
  if (asked < (int)MIN_CELLS) return (unsigned int)MIN_CELLS;
  if ((std::size_t)asked > MAX_CELLS) return (unsigned int)MAX_CELLS;
  return (unsigned int)asked;
}

float gMultiSlider::Bound(float value) const {
  // The bounds are taken as an ordered pair, .rslider's and .incdec's rule, so a
  // reversed argument pair still bounds against the right two numbers. A NaN
  // fails both compares and is answered with the lower bound rather than passed
  // through, since every value this object reports must be inside the range it
  // documents.
  const float lowBound = minimum < maximum ? minimum : maximum;
  const float highBound = minimum < maximum ? maximum : minimum;
  if (!(value >= lowBound)) return lowBound;
  if (value > highBound) return highBound;
  return value;
}

float gMultiSlider::CellAt(unsigned int index) const {
  return Bound(cells[index].load(std::memory_order_relaxed));
}

void gMultiSlider::StoreCell(unsigned int index, float value) {
  // Against the capacity, not the count: the whole-state write fills cells
  // before it publishes the count that makes them live.
  if (index >= MAX_CELLS) return;
  cells[index].store(Bound(value), std::memory_order_relaxed);
}

std::size_t gMultiSlider::Render(float value, char* out) {
  const int written = ExprFormatValue(ExprValue::Float(value), out, kExprValueTextMax);
  return written > 0 ? (std::size_t)written : 0;
}

// ─── inlets ───────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  // Nothing to store; the hot inlet calculates on the way out, which is the
  // re-send.
  (void)inlet;
  (void)thread;
}

INT_IN(IntIn) {
  FloatIn((float)value, inlet, thread);
}

FLOAT_IN(FloatIn) {
  if (inlet != 0) return;
  (void)thread;
  // Max's int/float method: "sets all sliders to the received number". Bounded
  // once rather than per cell, the bounds being the same for every cell.
  const unsigned int live = Cells();
  const float bounded = Bound(value);
  for (unsigned int i = 0; i < live; i++)
    cells[i].store(bounded, std::memory_order_relaxed);
}

LIST_IN(ListIn) {
  // Registered on inlet 0 only, but routed anyway: a later inlet must never
  // start writing the bank because someone added a handler above.
  if (inlet != 0) return;

  const char* text = value.c_str();
  const std::size_t length = value.size();

  std::size_t begin = 0;
  std::size_t end = 0;
  // An empty list changes nothing and falls through to the re-send a bang gives
  // — it must not resize the bank to zero cells.
  if (!NextToken(text, length, 0, begin, end)) return;

  if (TokenIs(text + begin, end - begin, "set", 3)) {
    // "set <index> <value>" — one cell. Max's own message *and* issue #551's
    // cell write; on this object they are the same thing, which they could not
    // be on .rslider.
    std::size_t cursor = end;
    float index = 0.f;
    float cellValue = 0.f;
    if (!NextNumber(text, length, cursor, index)) return;
    if (!NextNumber(text, length, cursor, cellValue)) return;
    // Range-checked against the live count rather than indexed, as the protocol
    // requires: an index outside the bank — or a NaN, which fails the first
    // compare — is dropped, never folded onto a real cell. A fractional index
    // truncates towards zero, as every other index in the patcher does.
    if (!(index >= 0.f)) return;
    const unsigned int at = (unsigned int)ExprToInt(index);
    if (at >= Cells()) return;
    StoreCell(at, cellValue);
    return;
  }

  if (TokenIs(text + begin, end - begin, "fetch", 5)) {
    // Max: "outputs the value of the numbered slider out the right outlet".
    std::size_t cursor = end;
    float index = 0.f;
    if (!NextNumber(text, length, cursor, index)) return;
    if (!(index >= 0.f)) return;
    const unsigned int at = (unsigned int)ExprToInt(index);
    if (at >= Cells()) return;
    outputs[1].SendFloat(CellAt(at), thread);
    return;
  }

  // The whole state: the exact string GetGuiValue() produced, and Max's list
  // method. Cells are filled first and the new count published afterwards with
  // a release store, so a reader that sees a longer bank sees the values that
  // made it longer; a shorter one may briefly show stale cells past its end,
  // which is the torn *frame* the protocol permits for a repaint.
  unsigned int written = 0;
  std::size_t cursor = 0;
  float number = 0.f;
  while (written < MAX_CELLS && NextNumber(text, length, cursor, number)) {
    StoreCell(written, number);
    written++;
  }
  // Nothing numeric in the list: leave the bank as it is rather than collapsing
  // it to a single cell.
  if (written == 0) return;
  // Max's listresize, on by default: "auto-resize to the incoming list length".
  size.store((int)written, std::memory_order_release);
}

// ─── the GUI value protocol (issue #551) ──────────────────────────────────────

GUI_VALUE() {
  // A local rather than listText: that buffer belongs to the send path on the
  // audio thread, and this runs on the host thread. One call, one allocation —
  // which is what the protocol asks a bulk read to be.
  const unsigned int live = Cells();
  std::string out;
  out.reserve((std::size_t)live * ((std::size_t)kExprValueTextMax + 1));

  char text[kExprValueTextMax];
  for (unsigned int i = 0; i < live; i++) {
    if (i > 0) out.push_back(' ');
    const std::size_t written = Render(CellAt(i), text);
    out.append(text, written);
  }
  return out;
}

GUI_VALUE_COUNT() {
  return Cells();
}

GUI_VALUE_AT() {
  // Range-checked against the count read *now* rather than indexed, as the
  // protocol requires: past the end is "", never the whole state again.
  if (index >= Cells()) return std::string();
  char text[kExprValueTextMax];
  const std::size_t written = Render(CellAt(index), text);
  return std::string(text, written);
}

// ─── output ───────────────────────────────────────────────────────────────────

CALC() {
  // The count is sampled once, so every cell in this list comes from one bank
  // even if a message arriving down one of the cords resizes it mid-send.
  const unsigned int live = Cells();

  // Filled immediately before the send rather than kept between sends: the send
  // path is synchronous, so a patch looping the outlet back into the inlet
  // re-enters here inside SendList, and a buffer filled any earlier would be the
  // inner message's by the time this one was read (.funnel's lesson). Into
  // memory reserved at construction, so nothing here allocates.
  listText.clear();
  char text[kExprValueTextMax];
  for (unsigned int i = 0; i < live; i++) {
    if (i > 0) listText.push_back(' ');
    const std::size_t written = Render(CellAt(i), text);
    listText.append(text, written);
  }

  outputs[0].SendList(listText, thread);
}

#undef className
