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
      "Coin flip. Every bang emits a 0 or a 1, chosen with equal probability — the cheapest "
      "possible branch, for probabilistic patterns, drum-machine fills and stochastic gating. An "
      "int or a float on the left inlet does the same thing as a bang, so any value source can "
      "drive it directly. The draw is unbiased rather than merely nearly so, and successive flips "
      "are uncorrelated, so the object is a fair coin and not an alternation. A non-zero seed "
      "makes "
      "the whole sequence reproducible across runs, which is what lets a generative patch replay "
      "exactly; seed 0 takes an arbitrary stream. An int on the right inlet, or the message 'seed "
      "<n>' on the left, restarts the sequence at runtime. Exactly one random draw is taken per "
      "flip.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "control",
            "Bang, int or float to flip the coin and emit the result / list 'seed <n>' to restart "
            "the random sequence.",
            "");
  INLET_DOC(1, "seed", "Restarts the random sequence; 0 takes an arbitrary stream.", "any int");
  OUTLET_DOC(0, "out", "0 or 1, chosen with equal probability.", "0 or 1");
  PARAM_DOC("seed", "0",
            "Random seed; non-zero replays the same flips every run, 0 picks an arbitrary stream.",
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
