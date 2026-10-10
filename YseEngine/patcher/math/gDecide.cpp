#include "gDecide.h"
#include "../pListArgs.h"
#include <string>

using namespace YSE::PATCHER;

#define className gDecide

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

  ADD_PARAM(seed);
  REG_PARM_PARSE;

  ADD_DESCRIPTION(
      "A coin flip: each bang sends 0 or 1 with equal chance. Use it for random branching, "
      "fills and gating.");
  ADD_CATEGORY(pCategory::RANDOM);
  INLET_DOC(0, "control",
            "A bang, int or float flips the coin. 'seed <n>' restarts the random sequence.", "");
  INLET_DOC(1, "seed", "Restarts the random sequence with this seed.", "any int");
  OUTLET_DOC(0, "out", "0 or 1.", "0 or 1");
  PARAM_DOC("seed", "0", "Random seed. Any other value than 0 gives the same flips on every run.",
            "any int");
}

PARM_PARSE() {
  // Runs on the control thread after the creation parameters were parsed.
  // Seeding here rather than in the constructor is what lets a saved patch
  // replay its flips: the seed is not known until the parameter string is read.
  rng.Seed(static_cast<UInt>(seed.load()));
}

void gDecide::Flip(YSE::THREAD thread) {
  // Bounded(2) is exactly unbiased: the multiply-shift reduction's residual
  // bias is below bound / 2^32, and 2 divides 2^32 with nothing left over. It
  // reads the top bit of an avalanched SplitMix64 word, so the flips are
  // independent of each other as well as individually fair.
  const int value = static_cast<int>(rng.Bounded(2));
  lastValue.store(value);
  outputs[0].SendInt(value, thread);
}

BANG_IN(Bang) {
  (void)inlet; // only inlet 0 registers a bang handler
  Flip(thread);
}

INT_IN(SetInt) {
  if (inlet == 0) {
    // Max: "int — In left inlet: Same as bang." The value itself is discarded;
    // it is the arrival that flips the coin.
    Flip(thread);
    return;
  }
  // Max's right inlet is the seed. A live override: DumpJSON keeps the creation
  // parameter, so the patch reloads with the seed it was saved with.
  rng.Seed(static_cast<UInt>(value));
}

FLOAT_IN(SetFloat) {
  // Int object: a float behaves as the truncated int would, as in Max.
  SetInt(static_cast<int>(value), inlet, thread);
}

LIST_IN(SetList) {
  if (inlet != 0) return;

  if (value.compare(0, 5, "seed ") == 0) {
    // The same restart the right inlet gives, spelled the way .drunk and .urn
    // spell it, so the family reads consistently in a saved patch.
    int argument = 0;
    if (ReadIntArg(value, 5, argument)) rng.Seed(static_cast<UInt>(argument));
  }
}

GUI_VALUE() {
  return std::to_string(lastValue.load());
}
