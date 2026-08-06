#include "gUrn.h"
#include "../pListArgs.h"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gUrn

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(Bang);
  REG_LIST_IN(SetList);

  ADD_IN_1;
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);

  ADD_OUT_INT;
  ADD_OUT_BANG;

  ADD_PARAM(limit);
  ADD_PARAM(seed);
  REG_PARM_PARSE;

  // A usable bag before any parameter string arrives: the default limit of 1
  // makes this a single-element permutation, but the invariant that bag[0,
  // limit) is a permutation of [0, limit) has to hold from construction on,
  // because a bang may arrive before SetParams ever does.
  Refill();

  ADD_DESCRIPTION(
      "Random numbers without repetition. Each bang emits a value from [0, limit) that has not "
      "been emitted yet, so every value appears exactly once before any of them repeats — a "
      "shuffled deck rather than a die, which is what keeps generative sample and pitch selection "
      "free of the immediate repeats that read as a mistake. Once every value has been handed out "
      "the urn is empty: further bangs emit nothing on outlet 0 and bang outlet 1 instead. 'clear' "
      "refills and reshuffles it, and an int on inlet 1 sets a new limit and refills. The bag "
      "holds "
      "a whole shuffled permutation and a bang is a cursor step through it, so a draw costs the "
      "same whether the urn is full or has one value left — no rejection sampling, no unbounded "
      "retry loop. A non-zero seed makes the order reproducible across runs; seed 0 takes an "
      "arbitrary stream. The limit is clamped to 1-4096.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, "control",
            "Bang to draw the next unused value / list 'clear' to refill and reshuffle the urn / "
            "list 'seed <n>' to restart the random sequence and reshuffle what is left.",
            "");
  INLET_DOC(1, "limit", "Sets the exclusive upper bound and refills the urn.", "1-4096");
  OUTLET_DOC(0, "out",
             "Next value not yet drawn since the last refill. Silent once the urn is "
             "empty.",
             "0 to limit-1");
  OUTLET_DOC(1, "empty", "Bangs when a bang arrives and every value has already been drawn.", "");
  PARAM_DOC("limit", "1",
            "Exclusive upper bound of the drawn values; setting it refills the urn. Clamped to "
            "1-4096.",
            "1-4096");
  PARAM_DOC("seed", "0",
            "Random seed; non-zero replays the same orders every run, 0 picks an arbitrary stream.",
            "any int");
}

PARM_PARSE() {
  // Runs on the control thread after the creation parameters were parsed.
  // Seeding here rather than in the constructor is what lets a saved patch
  // replay its cycle: the seed is not known until the parameter string is read.
  // The refill has to follow the seeding, since it is the refill that draws.
  rng.Seed(static_cast<UInt>(seed.load()));
  SetLimit(limit.load());
}

void gUrn::ShuffleFrom(int first) {
  const int n = limit.load();
  if (first < 0) first = 0;

  // Fisher-Yates, walking down from the end and stopping at `first`: every
  // ordering of bag[first, n) is equally likely, and bag[0, first) — the values
  // already handed out — is not touched. Exactly one draw per swap, so a seeded
  // object shuffles identically on every run.
  for (int i = n - 1; i > first; --i) {
    const int span = (i - first) + 1;
    const int j = first + static_cast<int>(rng.Bounded(static_cast<UInt>(span)));
    const std::uint16_t held = bag[static_cast<std::size_t>(i)];
    bag[static_cast<std::size_t>(i)] = bag[static_cast<std::size_t>(j)];
    bag[static_cast<std::size_t>(j)] = held;
  }
}

void gUrn::Refill() {
  const int n = limit.load();
  for (int i = 0; i < n; ++i)
    bag[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(i);

  // Reset the cursor before shuffling, so a bang that lands mid-refill reads a
  // slot that is at worst stale rather than one past the end of the bag.
  drawn.store(0);
  ShuffleFrom(0);
}

void gUrn::SetLimit(int value) {
  // Max caps the limit at 4096 and offers no fewer than one value; the cap is
  // also this object's fixed capacity, so the clamp is what keeps the bag
  // allocation-free.
  int clamped = value;
  if (clamped < 1) clamped = 1;
  if (clamped > CAPACITY) clamped = CAPACITY;

  limit.store(clamped);
  // A range change invalidates the drawn set — keeping it would let a value
  // inside the new range never appear at all — so Max refills here, and so do
  // we.
  Refill();
}

INT_IN(SetInt) {
  (void)thread;
  if (inlet == 1) SetLimit(value);
}

FLOAT_IN(SetFloat) {
  // Int object: a float sets the same field, truncated, as in Max.
  SetInt(static_cast<int>(value), inlet, thread);
}

LIST_IN(SetList) {
  (void)thread;
  if (inlet != 0) return;

  if (value == "clear") {
    // Every value undrawn again, in a *new* order: the stream carries on rather
    // than restarting, so consecutive cycles differ. Max repeats the same cycle
    // unless it is also sent `seed 0`.
    Refill();
    return;
  }

  if (value.compare(0, 5, "seed ") == 0) {
    int argument = 0;
    if (!ReadIntArg(value, 5, argument)) return;
    rng.Seed(static_cast<UInt>(argument));
    // Reshuffle only what has not been drawn. Max's `seed` does not empty the
    // urn, and reshuffling the whole bag would hand out values that this cycle
    // already produced. This is a live override: DumpJSON keeps the creation
    // parameter, so a patch reloads with the seed it was saved with.
    ShuffleFrom(drawn.load());
  }
}

BANG_IN(Bang) {
  const int n = limit.load();

  // Claim the next slot with a compare-exchange rather than load / store: an
  // inlet handler runs synchronously on whichever thread sent the message, so
  // two bangs can meet inside one object. Claiming the cursor is what makes the
  // no-repetition guarantee survive that — two threads take two *different*
  // positions in the permutation. The loop also stops the cursor from running
  // past `limit`, so an urn left banging while empty cannot overflow it.
  int position = drawn.load();
  do {
    if (position >= n) {
      // Empty: outlet 0 stays silent and outlet 1 reports the end of the cycle.
      outputs[1].SendBang(thread);
      return;
    }
  } while (!drawn.compare_exchange_weak(position, position + 1));

  const int value = static_cast<int>(bag[static_cast<std::size_t>(position)]);
  lastValue.store(value);
  outputs[0].SendInt(value, thread);
}

GUI_VALUE() {
  return std::to_string(lastValue.load());
}
