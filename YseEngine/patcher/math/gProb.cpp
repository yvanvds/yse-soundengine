#include "gProb.h"
#include "../pListArgs.h"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gProb

namespace {

  // A transition list is exactly three numbers. Read one more than that so a
  // longer list can be told apart from a well-formed one and rejected rather
  // than silently truncated.
  constexpr int TRANSITION_ITEMS = 3;
  constexpr int READ_AHEAD = TRANSITION_ITEMS + 1;

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(Bang);
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_LIST_IN(SetList);

  ADD_IN_1;
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);

  ADD_OUT_INT;
  ADD_OUT_BANG;
  ADD_OUT_LIST;

  ADD_PARAM(reset);
  ADD_PARAM(seed);
  REG_PARM_PARSE;

  ADD_DESCRIPTION(
      "A Markov chain of weighted transitions between numbered states. Each bang jumps from the "
      "current state to a next one, chosen by weight, and sends it.");
  ADD_CATEGORY(pCategory::RANDOM);
  INLET_DOC(0, "control",
            "A bang makes a jump. An int or float sets the current state without sending. A list "
            "'<from> <to> <weight>' stores a transition; also 'clear', 'dump', 'reset <n>' and "
            "'seed <n>'.",
            "");
  INLET_DOC(1, "seed", "Restarts the random sequence with this seed.", "any int");
  OUTLET_DOC(0, "out", "The state jumped to.", "any int");
  OUTLET_DOC(1, "stuck",
             "A bang when the current state has no way out. The chain then goes back to the "
             "reset state.",
             "bang");
  OUTLET_DOC(2, "dump", "One '<from> <to> <weight>' list per stored transition, on 'dump'.", "");
  PARAM_DOC("reset", "0", "The state the chain starts in and returns to after a dead end.",
            "any int");
  PARAM_DOC("seed", "0", "Random seed. Any other value than 0 gives the same walk on every run.",
            "any int");
}

PARM_PARSE() {
  // Runs on the control thread after the creation parameters were parsed.
  // Seeding here rather than in the constructor is what lets a saved patch
  // replay its walk: the seed is not known until the parameter string is read.
  rng.Seed(static_cast<UInt>(seed.load()));
  // The walk departs from the fallback until something sets it otherwise, so
  // the reset parameter has to reach `current` as well.
  current.store(reset.load());
}

void gProb::Dump(YSE::THREAD thread) {
  int entry[TRANSITION_ITEMS] = {0, 0, 0};
  // Bounded by the table's capacity. Entry() re-reads the published count each
  // time, so an entry appended while the dump runs is either fully visible or
  // not visible at all. FormatIntList builds the line in one pass over a stack
  // buffer rather than materialising a std::to_string temporary per term.
  for (int i = 0; table.Entry(i, entry[0], entry[1], entry[2]); ++i)
    outputs[2].SendList(FormatIntList(entry, TRANSITION_ITEMS), thread);
}

INT_IN(SetInt) {
  (void)thread;
  if (inlet == 0) {
    // Max: "int — sets (but does not send out) the current number value." It
    // says where the next bang departs from, nothing more.
    current.store(value);
    return;
  }
  // Max's right inlet is the seed here, as on .decide. A live override:
  // DumpJSON keeps the creation parameter, so a patch reloads with the seed it
  // was saved with.
  rng.Seed(static_cast<UInt>(value));
}

FLOAT_IN(SetFloat) {
  // Int object: a float sets the same fields, truncated, as in Max.
  SetInt(static_cast<int>(value), inlet, thread);
}

LIST_IN(SetList) {
  if (inlet != 0) return;

  if (value == "clear") {
    table.Clear();
    return;
  }

  if (value == "dump") {
    Dump(thread);
    return;
  }

  int argument = 0;
  if (value.compare(0, 6, "reset ") == 0) {
    if (ReadIntArg(value, 6, argument)) reset.store(argument);
    return;
  }

  if (value.compare(0, 5, "seed ") == 0) {
    // The same restart the right inlet gives, spelled the way .drunk, .urn and
    // .decide spell it, so the family reads consistently in a saved patch.
    if (ReadIntArg(value, 5, argument)) rng.Seed(static_cast<UInt>(argument));
    return;
  }

  // Anything else has to be a transition entry: exactly three numbers. Reading
  // one extra is what rejects a four-number list instead of quietly keeping its
  // first three.
  int items[READ_AHEAD] = {0, 0, 0, 0};
  if (ReadIntList(value, 0, items, READ_AHEAD) != TRANSITION_ITEMS) return;
  // A full table drops the entry; there is nowhere to report that to, and the
  // alternative — evicting somebody else's transition — silently rewrites the
  // chain the patch asked for.
  (void)table.Set(items[0], items[1], items[2]);
}

BANG_IN(Bang) {
  (void)inlet; // only inlet 0 registers a bang handler

  // Always exactly one draw, dead end or not, so that editing the table does
  // not shift the stream position of every bang after it and a seeded walk
  // stays replayable.
  const auto draw = static_cast<UInt>(rng.Next() >> 32);

  int next = 0;
  if (!table.Pick(current.load(), draw, next)) {
    // Dead end: report it, and put the walk back on the fallback so the *next*
    // bang has somewhere to depart from. Not retried here — searching for a
    // state with an exit is a loop with no bound on it.
    current.store(reset.load());
    outputs[1].SendBang(thread);
    return;
  }

  current.store(next);
  outputs[0].SendInt(next, thread);
}

GUI_VALUE() {
  return std::to_string(current.load());
}
