#include "gRandom.h"
#include "../pListArgs.h"
#include "../../utils/misc.hpp"

using namespace YSE::PATCHER;

#define className gRandom

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(Bang);
  REG_LIST_IN(SetList);

  ADD_IN_1;
  REG_INT_IN(SetIntRange);
  REG_FLOAT_IN(SetFloatRange);

  ADD_PARAM(range);
  ADD_PARAM(seed);
  REG_PARM_PARSE;

  ADD_OUT_INT;

  range = 2;

  ADD_DESCRIPTION(
      "Sends a random integer from 0 to range-1 on each bang, every value equally likely.");
  ADD_CATEGORY(pCategory::RANDOM);
  INLET_DOC(0, "bang", "A bang sends a new random number. 'seed <n>' restarts the random sequence.",
            "bang");
  INLET_DOC(1, "range", "Sets the range for the next numbers. A float is truncated.", "1+");
  OUTLET_DOC(0, "out", "A random integer from 0 to range-1.", "0 to range-1");
  PARAM_DOC("range", "2", "How many different values can come out.", "1+");
  PARAM_DOC("seed", "0",
            "Random seed. Any other value than 0 gives the same numbers on every run; 0 uses the "
            "engine's generator.",
            "any int");
}

PARM_PARSE() {
  // Runs on the control thread after the creation parameters were parsed: the
  // seed is not known before, which is why the constructor cannot seed.
  Reseed(seed.load());
}

void gRandom::Reseed(int value) {
  if (value != 0) {
    // Seed first, then publish: a draw that sees the flag sees a fresh stream.
    rng.Seed(static_cast<UInt>(value));
    ownStream.store(true, std::memory_order_release);
  } else {
    ownStream.store(false, std::memory_order_release);
  }
}

// Inlet 0 is not left to Calculate(): every message there would run it, and
// `seed <n>` must not send a number.
BANG_IN(Bang) {
  const int r = range.load();
  int value = 0;
  // A range of 0 or less sends 0 (YSE::Random's rule) on both paths. The
  // own-stream path still draws, so a seeded sequence takes exactly one draw
  // per bang whatever the range.
  if (ownStream.load(std::memory_order_acquire)) {
    const UInt drawn = rng.Bounded(r > 0 ? static_cast<UInt>(r) : 0U);
    value = static_cast<int>(drawn);
  } else {
    value = YSE::Random(r);
  }
  outputs[0].SendInt(value, thread);
}

LIST_IN(SetList) {
  if (inlet != 0) return;
  int argument = 0;
  if (value.compare(0, 5, "seed ") == 0 && ReadIntArg(value, 5, argument)) {
    // A live override: DumpJSON keeps the creation parameter, so a patch
    // reloads with the seed it was saved with, not with one sent at runtime.
    Reseed(argument);
  }
}

INT_IN(SetIntRange) {
  range = value;
}

FLOAT_IN(SetFloatRange) {
  range = (int)value;
}
