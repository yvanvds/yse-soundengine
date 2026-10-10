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

  ADD_DESCRIPTION("Counts how often each number follows the one before it, and sends '<previous> "
                  "<current> <count>' for every pair it counts. Wire it into .prob to make .prob "
                  "play in the style of what it heard.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "in",
            "Number: count it as following the previous number. 'clear': forget the counts. "
            "'reset': forget the previous number. 'dump': send every stored pair.",
            "0 to limit");
  OUTLET_DOC(0, "pair", "'<previous> <current> <count>': how often this pair has been seen.", "");
  OUTLET_DOC(1, "full",
             "Bang when a new pair cannot be stored because 1024 different pairs already are.", "");
  PARAM_DOC("limit", "128",
            "Highest number accepted; numbers are limited to 0 to limit. Clamped to 1-16384.",
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
