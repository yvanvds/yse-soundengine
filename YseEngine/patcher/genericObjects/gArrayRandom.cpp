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
      "Outputs a random element of an array — Max's array.random on the name-addressed value "
      "model .array settled: the array is bound from the creation argument, \".array.random "
      "<name> [<seed>]\", because an array is addressed by name and never passed down a cord. "
      "A bang picks one element — the length read, the position drawn and the element copied "
      "out under one hold of the store's guard, so the draw ranges over the array as it stood "
      "at the trigger and can never miss — and the element leaves typed the way the patcher "
      "spells it. The position comes from a per-object random sequence, the source .drunk, "
      ".urn and .array.scramble share: a non-zero seed replays the same picks every run, 0 "
      "takes an arbitrary stream, and exactly one draw is taken per element that leaves. "
      "Repeats are allowed — every pick is an independent uniform draw; drawing without "
      "replacement is .urn's behaviour, a second object rather than a mode of this one. An "
      "empty or unnamed (private) array bangs the empty outlet instead, taking no draw.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger",
            "A bang picks one element at random — one guarded read at a drawn position, the "
            "element out the element outlet typed by its spelling, or the empty outlet when "
            "there is nothing to pick. \"array <name>\" does the same when it names the array "
            "bound by the first creation argument — the message an .array's reference outlet "
            "emits on a bang, so wiring that outlet here gives the family's gesture: bang the "
            "array, out comes a random element. A reference naming anything else, or any other "
            "message, is refused and counted rather than logged, since this inlet may be the "
            "audio thread.",
            "");
  INLET_DOC(1, "seed",
            "An int restarts the random sequence, silently — a non-zero seed replays the same "
            "picks every run, 0 takes an arbitrary stream, .urn's contract and Max's @seed 0. "
            "A float truncates to an int first; a non-finite one is refused rather than "
            "quietly becoming seed 0. The author's creation argument is not rewritten: a "
            "run-time reseed is the patch's business, the saved seed the author's.",
            "any int");
  INLET_DOC(2, "array reference",
            "\"array <name>\" is accepted silently when it names the array bound by the first "
            "creation argument, so a patch may wire the array's reference outlet across. It "
            "sets nothing — the binding is the creation argument, resolved on the control "
            "thread — and anything else is refused and counted.",
            "");
  OUTLET_DOC(0, "element",
             "The picked element, typed the way the patcher spells it: a numeric element "
             "leaves as an int or a float by its spelling and anything else as a symbol. "
             "Every pick is an independent uniform draw over the positions the array holds at "
             "that moment — repeats allowed, .urn's no-repeat being a different object — and "
             "the element sent is the array's as it stood at the trigger, read under one hold "
             "of the store's guard.",
             "");
  OUTLET_DOC(1, "empty",
             "Bang when there is no element to pick — an empty or unnamed (private) array. "
             "\"No data\" is a state a patch must be able to route on, not an error, and a "
             "sentinel value would be indistinguishable from a real answer — .array.pop's "
             "empty outlet, for the same reason. No draw is taken, so a seeded stream stays "
             "aligned with the elements actually picked. A lost try-lock is a counted refusal "
             "instead: the array's state is unknown, so neither outlet fires.",
             "");
  PARAM_DOC("name", "",
            "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an "
            ".array of the same name in this patcher holds. Resolved once, on the control "
            "thread, which is why no message re-points it at run time. Empty picks from a "
            "private, empty array: every ask bangs the empty outlet.",
            "any identifier");
  PARAM_DOC("seed", "0",
            "Random seed for the pick sequence; non-zero replays the same picks every run, 0 "
            "picks an arbitrary stream.",
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
