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
      "Weighted transition table — a first-order Markov chain. The list '<from> <to> <weight>' "
      "records that going from one state to another carries that relative likelihood, and a bang "
      "makes one weighted jump from the current state and emits where it landed, which becomes the "
      "state the next bang departs from. For any one state the weights of its outgoing transitions "
      "are summed and each one's share of that sum is its probability, so 3 4 1 means 37.5% / 50% "
      "/ 12.5%; a state may transition to itself. A weight of 0 keeps the pair in the table but "
      "makes it unreachable, which is how a transition is switched off, and negative weights clamp "
      "to 0. A state with no reachable transition is a dead end: outlet 0 stays silent, outlet 1 "
      "bangs, and the current state moves to the 'reset' fallback so the next bang can recover — "
      "one bang is one attempt, never a search. 'clear' forgets the table, 'dump' sends every "
      "entry "
      "out outlet 2 in insertion order, and an int sets the current state without emitting "
      "anything. The table is a fixed 1024-entry array and the weighted choice is a bounded "
      "prefix-sum scan rather than a rejection loop, so nothing on any path allocates or runs "
      "unbounded. A non-zero seed makes the whole walk reproducible across runs; exactly one "
      "random "
      "draw is taken per bang. Pairs with .anal, which builds the same table from an input "
      "stream.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(
      0, "control",
      "Bang to make a weighted transition / int or float to set the current state without "
      "emitting / list '<from> <to> <weight>' to store a transition / list 'clear' to forget "
      "the table / list 'dump' to send it out outlet 2 / list 'reset <n>' to set the dead-end "
      "fallback state / list 'seed <n>' to restart the random sequence.",
      "");
  INLET_DOC(1, "seed", "Restarts the random sequence; 0 takes an arbitrary stream.", "any int");
  OUTLET_DOC(0, "out", "The state jumped to. Silent when the current state is a dead end.",
             "any int");
  OUTLET_DOC(1, "stuck",
             "Bangs when the current state has no reachable transition; the walk then reverts to "
             "the reset state.",
             "");
  OUTLET_DOC(2, "dump",
             "One '<from> <to> <weight>' list per stored transition, in response to "
             "'dump'.",
             "");
  PARAM_DOC("reset", "0",
            "State the walk reverts to when it reaches a dead end, and the state it starts in.",
            "any int");
  PARAM_DOC("seed", "0",
            "Random seed; non-zero replays the same walk every run, 0 picks an arbitrary stream.",
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
  int from = 0;
  int to = 0;
  int weight = 0;
  // Bounded by the table's capacity. Entry() re-reads the published count each
  // time, so an entry appended while the dump runs is either fully visible or
  // not visible at all.
  for (int i = 0; table.Entry(i, from, to, weight); ++i) {
    outputs[2].SendList(
        std::to_string(from) + " " + std::to_string(to) + " " + std::to_string(weight), thread);
  }
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
