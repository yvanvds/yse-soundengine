#include "gDrunk.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <string>

using namespace YSE::PATCHER;

#define className gDrunk

namespace {

  // The largest magnitude limit that is honoured. A step can never usefully be
  // larger than the range anyway, and capping here keeps every intermediate
  // below 2^31 so the draw arithmetic cannot overflow.
  constexpr I64 MAX_STEP_MAGNITUDE = 1 << 30;

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

  ADD_IN_2;
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);

  ADD_OUT_INT;

  ADD_PARAM(range);
  ADD_PARAM(stepSize);
  ADD_PARAM(seed);
  REG_PARM_PARSE;

  ADD_DESCRIPTION(
      "A random walk: each bang moves the value up or down by a small random step and sends it, "
      "kept within 0 to range-1. Successive values drift rather than jump.");
  ADD_CATEGORY(pCategory::RANDOM);
  INLET_DOC(0, "control",
            "A bang takes a step. An int or float sets the position and sends it. 'set <n>' sets "
            "it without sending; 'seed <n>' restarts the random sequence.",
            "0 to range-1");
  INLET_DOC(1, "range", "The walk stays below this value.", "1+");
  INLET_DOC(2, "stepSize",
            "Steps are smaller than this value. A negative value also rules out a zero step.",
            "any int");
  OUTLET_DOC(0, "out", "The position of the walk.", "0 to range-1");
  PARAM_DOC("range", "128", "The walk stays below this value.", "1+");
  PARAM_DOC("stepSize", "2", "Steps are smaller than this value; negative rules out a zero step.",
            "any int");
  PARAM_DOC("seed", "0", "Random seed. Any other value than 0 gives the same walk on every run.",
            "any int");
}

PARM_PARSE() {
  // Runs on the control thread after the creation parameters were parsed.
  // Reseeding here rather than in the constructor is what lets a saved patch
  // replay its walk: the seed is not known until the parameter string is read.
  rng.Seed(static_cast<UInt>(seed.load()));
  currentValue.store(ClipToRange(currentValue.load()));
}

int gDrunk::ClipToRange(I64 value) const {
  // range is the *exclusive* upper bound, so the last legal value is range-1.
  // A range of 1 or less contains only 0, which is also the answer for a range
  // given as 0 or negative.
  I64 high = static_cast<I64>(range.load()) - 1;
  if (high < 0) high = 0;

  if (value < 0) return 0;
  if (value > high) return static_cast<int>(high);
  return static_cast<int>(value);
}

int gDrunk::DrawStep(int limit) {
  // Max reads the step size as an exclusive magnitude limit — the step taken
  // is always strictly smaller than it — and uses the sign to say whether a
  // zero step is allowed. `span` is how many distinct non-zero magnitudes that
  // leaves.
  const I64 asWide = static_cast<I64>(limit);
  I64 magnitude = (asWide < 0) ? -asWide : asWide; // widened: -INT_MIN overflows int
  if (magnitude > MAX_STEP_MAGNITUDE) magnitude = MAX_STEP_MAGNITUDE;
  const Int span = static_cast<Int>(magnitude - 1);

  if (span <= 0) {
    // |step| < 1 leaves only the zero step — and a negative limit forbids even
    // that, so the walk simply stands still. Still draw, so that the number of
    // draws per bang stays one whatever the step size and a seeded sequence
    // does not shift when the step size changes.
    (void)rng.Next();
    return 0;
  }

  if (limit >= 0) {
    // Zero allowed: 2*span+1 outcomes, symmetric around it.
    return rng.Between(-span, span + 1);
  }

  // Zero excluded: 2*span outcomes. The lower half maps to [-span, -1], the
  // upper half to [1, span].
  const Int draw = static_cast<Int>(rng.Bounded(static_cast<UInt>(2 * span)));
  return (draw < span) ? (draw - span) : (draw - span + 1);
}

int gDrunk::MoveTo(int value) {
  const int clipped = ClipToRange(static_cast<I64>(value));
  currentValue.store(clipped);
  return clipped;
}

void gDrunk::Store(int value, int inlet, YSE::THREAD thread) {
  switch (inlet) {
  case 0:
    // Max emits on a value set as well as on a bang, the same way .counter
    // does, so a patch can jump the walk and hear where it landed.
    outputs[0].SendInt(MoveTo(value), thread);
    break;
  case 1:
    range.store(value);
    // Keep the stored position honest straight away rather than only at the
    // next bang, so the reported value never sits outside the current range.
    currentValue.store(ClipToRange(currentValue.load()));
    break;
  case 2:
    stepSize.store(value);
    break;
  default:
    break;
  }
}

INT_IN(SetInt) {
  Store(value, inlet, thread);
}

FLOAT_IN(SetFloat) {
  // Int object: a float sets the same fields, truncated, as in Max.
  Store(static_cast<int>(value), inlet, thread);
}

LIST_IN(SetList) {
  if (inlet != 0) return;

  int argument = 0;
  if (value.compare(0, 4, "set ") == 0) {
    // Sets the position without emitting — Max's `set`.
    if (ReadIntArg(value, 4, argument)) MoveTo(argument);
    return;
  }
  if (value.compare(0, 5, "seed ") == 0) {
    // Restarts the sequence. This is a live override: what a DumpJSON keeps is
    // the creation parameter, so a patch reloads with the seed it was saved
    // with, not with one sent at runtime.
    if (ReadIntArg(value, 5, argument)) rng.Seed(static_cast<UInt>(argument));
  }
}

BANG_IN(Bang) {
  const int step = DrawStep(stepSize.load());

  // Compare-exchange rather than load / add / store: an inlet handler runs
  // synchronously on whichever thread sent the message, so a GUI-driven set and
  // an audio-thread bang can meet here. The loop reuses the step already drawn,
  // which keeps the promise of exactly one draw per bang. Uncontended it is a
  // single CAS; contention needs two threads inside the same object at once.
  int expected = currentValue.load();
  int next = 0;
  do {
    next = ClipToRange(static_cast<I64>(expected) + step);
  } while (!currentValue.compare_exchange_weak(expected, next));

  outputs[0].SendInt(next, thread);
}

GUI_VALUE() {
  return std::to_string(currentValue.load());
}
