#include "gAnal.h"
#include "../pListArgs.h"
#include <string>

using namespace YSE::PATCHER;

#define className gAnal

namespace {

  // A transition entry is exactly three numbers: from, to, count.
  constexpr int ENTRY_ITEMS = 3;

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_LIST_IN(SetList);

  ADD_OUT_LIST;
  ADD_OUT_BANG;

  ADD_PARAM(limit);
  REG_PARM_PARSE;

  ADD_DESCRIPTION(
      "Transition histogram — counts which number follows which. Every number received is counted "
      "as a pair with the number before it, and the running count of that pair comes out as the "
      "list '<previous> <current> <count>'. That is exactly the list .prob stores a transition "
      "from, so a patch cord from this outlet into a .prob is the whole integration: play a phrase "
      "in here and the .prob on the other end has learned its first-order style and will improvise "
      "in it. The two agree because .anal accumulates the count and .prob replaces the weight with "
      "it, so re-sending a growing pair never double-counts. The first number received has no "
      "predecessor and is only remembered. Inputs are clipped into [0, limit], which is why the "
      "limit defaults to 128 — a MIDI note number arrives unchanged. 'clear' forgets the counts "
      "but "
      "keeps the last number as the next pair's predecessor, 'reset' does the opposite, and 'dump' "
      "sends every stored pair out outlet 0 so a table learned earlier can be replayed into a "
      ".prob "
      "created later. The table holds 1024 distinct pairs: once it is full, pairs already in it "
      "keep "
      "counting and a new one is dropped with a bang on outlet 1 rather than evicting somebody "
      "else's statistics or reporting a count that no input produced.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in",
            "Int or float to add the next number of the stream / list 'clear' to forget the "
            "counted pairs but keep the last number / list 'reset' to forget the last number but "
            "keep the counts / list 'dump' to send every stored pair out outlet 0.",
            "0 to limit");
  OUTLET_DOC(0, "pair",
             "'<previous> <current> <count>' — how often this succession has been seen. Silent for "
             "the first number, and for a new pair that no longer fits the table.",
             "");
  OUTLET_DOC(1, "full",
             "Bangs when a pair could not be counted because the table already holds its 1024 "
             "distinct pairs.",
             "");
  PARAM_DOC("limit", "128",
            "Top of the input range; incoming numbers are clipped into [0, limit]. Clamped to "
            "1-16384.",
            "1-16384");
}

PARM_PARSE() {
  // Runs on the control thread after the creation parameters were parsed. Max
  // documents 16384 as the ceiling; below 1 there would be a single legal input
  // value and no transition to speak of, so that is the floor.
  int clamped = limit.load();
  if (clamped < 1) clamped = 1;
  if (clamped > MAX_LIMIT) clamped = MAX_LIMIT;
  limit.store(clamped);
}

int gAnal::ClipInput(int value) const {
  const int top = limit.load();
  if (value < 0) return 0;
  if (value > top) return top;
  return value;
}

void gAnal::Dump(YSE::THREAD thread) {
  int entry[ENTRY_ITEMS] = {0, 0, 0};
  // Bounded by the table's capacity. Entry() re-reads the published count each
  // time, so a pair counted while the dump runs is either fully visible or not
  // visible at all.
  for (int i = 0; table.Entry(i, entry[0], entry[1], entry[2]); ++i)
    outputs[0].SendList(FormatIntList(entry, ENTRY_ITEMS), thread);
}

INT_IN(SetInt) {
  (void)inlet; // only inlet 0 registers handlers

  const int current = ClipInput(value);
  // Read the predecessor and install this number in its place as one operation,
  // so two numbers arriving on two threads cannot pair against the same one.
  const int from = previous.exchange(current);

  // Max: "the first time a number is received, there has been no previous
  // number, so nothing happens". Same after `reset`.
  if (from == NO_PREVIOUS) return;

  if (!table.Add(from, current, 1)) {
    // The table is full and this pair is not in it. Dropped rather than
    // evicting another pair or reporting a count no input produced — see the
    // class docs. Outlet 1 is how a patch notices that learning has stopped.
    outputs[1].SendBang(thread);
    return;
  }

  // Read the new total back rather than tracking it here: Add saturates at the
  // table's ceiling, so the stored weight is the only authority on what the
  // count now is.
  const int entry[ENTRY_ITEMS] = {from, current, table.WeightOf(from, current)};
  outputs[0].SendList(FormatIntList(entry, ENTRY_ITEMS), thread);
}

FLOAT_IN(SetFloat) {
  // Int object: a float counts as the same number, truncated, as in Max.
  SetInt(static_cast<int>(value), inlet, thread);
}

LIST_IN(SetList) {
  if (inlet != 0) return;

  if (value == "clear") {
    // Max: "erases the memory entirely, but retains the most recently received
    // number to use as the next 'previous' value" — so `previous` is untouched.
    table.Clear();
    return;
  }

  if (value == "reset") {
    // The mirror image: the counts stay, the predecessor goes, and the next
    // number arrives as if it were the first.
    previous.store(NO_PREVIOUS);
    return;
  }

  if (value == "dump") {
    Dump(thread);
    return;
  }

  // Anything else is not a message this object knows. Ignored rather than
  // guessed at.
}

GUI_VALUE() {
  // How much has been learned. The last number received is the other candidate,
  // but it is already visible on the outlet, and the table filling up is the
  // state a patch cannot otherwise see.
  return std::to_string(table.Size());
}
