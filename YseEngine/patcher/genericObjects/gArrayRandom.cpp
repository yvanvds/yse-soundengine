#include "gArrayRandom.h"
#include "../math/gExprEval.h"
#include "../pAtomList.h"
#include "../pObjectList.hpp"

#include <cstddef>
#include <cstring>
#include <string>

using namespace YSE::PATCHER;
#define className gArrayRandom

gArrayRandom::gArrayRandom() : gArrayEndsBase() {
  // The trigger inlet, hot; the seed inlet and the reference inlet, cold —
  // .array.scramble's arrangement, with a pick where the shuffle was. A pick
  // is asked for with a bang, never addressed — .array.at is the object that
  // takes a position — so there is no int or float method on the trigger.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  ADD_IN_2;
  REG_LIST_IN(ListIn);

  // ANY on the element outlet: what leaves it is an int, a float or a symbol
  // by the picked element's spelling — gArrayEndsRemover's shape. The empty
  // outlet is always a bang.
  ADD_OUT_ANY;
  ADD_OUT_BANG;

  // After gArrayEndsBase's name — the second creation argument.
  ADD_PARAM(seed);

  // The allocation the send path would otherwise need, taken here on the
  // control thread.
  AtomList::ReserveRender(emitScratch);

  ADD_DESCRIPTION(
      "Outputs a randomly chosen element of a named array. A seed makes the sequence of picks "
      "repeat on every run.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger",
            "Bang, or the bound array's reference \"array <name>\": send one element picked at "
            "random.",
            "");
  INLET_DOC(1, "seed",
            "An int restarts the random sequence. A non-zero seed repeats the same picks every "
            "run; 0 gives an unpredictable sequence. A float is truncated.",
            "any int");
  INLET_DOC(2, "array reference",
            "Accepts the bound array's reference \"array <name>\" and ignores it, so the "
            "array's reference outlet can be wired here. Anything else is refused.",
            "");
  OUTLET_DOC(0, "element", "The picked element, as an int, float or symbol by its spelling.", "");
  OUTLET_DOC(1, "empty", "Bang when the array is empty.", "");
  PARAM_DOC("name", "",
            "Name of the array to pick from: the .array with this name in the same patcher. "
            "Empty uses a private, empty array.",
            "any identifier");
  PARAM_DOC("seed", "0",
            "Random seed. A non-zero seed repeats the same picks every run; 0 gives an "
            "unpredictable sequence.",
            "any int");
}

void gArrayRandom::ParamsChanged() {
  // Seeding here rather than in the constructor is what lets a saved patch
  // replay its picks: the seed is not known until the parameter string is
  // read. gArrayScrambleBase's arrangement.
  rng.Seed(static_cast<UInt>(seed.load(std::memory_order_relaxed)));
}

// A re-parse must not leave half of the previous configuration standing:
// SetParams("") drops the seed back to 0 — an arbitrary stream — along with
// the name. gArrayPositionBase's rule.
PARM_CLEAR() {
  seed.store(0, std::memory_order_relaxed);
  gArrayEndsBase::ClearParams();
}

BANG_IN(BangIn) {
  (void)inlet;
  Pick(thread);
}

INT_IN(IntIn) {
  (void)inlet;
  // A run-time reseed restarts the sequence and rewrites nothing: the seed
  // parameter stays the author's argument, gArrayScrambleBase's split
  // between a seed and the live state. RandomSource::Seed is safe on
  // whichever thread this is.
  rng.Seed(static_cast<UInt>(value));
}

FLOAT_IN(FloatIn) {
  // Max's float method on an int attribute is "convert to int", .table's
  // precedent. The negated range test keeps a NaN or an infinity — which
  // ExprToInt folds to 0 — from quietly becoming seed 0.
  if (!(value >= -2147483648.f && value < 2147483648.f)) {
    Refuse();
    return;
  }
  IntIn(ExprToInt(value), inlet, thread);
}

LIST_IN(ListIn) {
  if (inlet == 2) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — the binding is the creation argument, resolved on the
    // control thread, so there is nothing to set. gDictSlice's inlet rule.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference picks — the message its .array emits on a bang,
  // the family's gesture. Anything else, including a reference naming an
  // array this object is not bound to, is refused: resolving an unrecognised
  // name means the registry's mutex, and this may be the audio thread.
  if (ArrayReferenceNames(value, arrayName)) {
    Pick(thread);
    return;
  }
  Refuse();
}

void gArrayRandom::Pick(YSE::THREAD thread) {
  bool empty = false;
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      // The array's state is unknown, so neither outlet fires — and no draw
      // is taken, which keeps a seeded stream aligned with the elements
      // actually picked.
      Refuse();
      return;
    }
    const std::size_t size = store->count;
    if (size == 0) {
      empty = true;
    } else {
      // The draw and the read under the same hold that read the length, so
      // the position indexes exactly the length this hold saw and a pick can
      // never miss. One draw per element that leaves — never for an empty
      // array — the property that makes a seeded sequence replayable.
      const auto at = static_cast<std::size_t>(rng.Bounded(static_cast<UInt>(size)));
      const std::string& element = store->elements[at];
      fetchedLength = element.size();
      std::memcpy(fetched, element.data(), fetchedLength);
      fetched[fetchedLength] = '\0';
    }
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well write into this same array, and inside the guard that
  // write would be the one thing the try-lock drops.
  if (empty) {
    outputs[1].SendBang(thread);
    return;
  }
  // Through SendAtom, so the element leaves as the int, float or symbol it
  // spells — the patcher's transport rule.
  SendAtom(outputs[0], fetched, fetchedLength, emitScratch, thread);
}
