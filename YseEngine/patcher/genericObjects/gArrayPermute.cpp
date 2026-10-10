#include "gArrayPermute.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

namespace {

  // True when nothing but whitespace is left from `offset` on. gArrayAt's
  // reader; not shared, for the reason gArray.cpp's ElementToJson gives —
  // exporting a file-local helper out of a shipped object costs more than the
  // repetition.
  bool AtEnd(const std::string& text, std::size_t offset) {
    for (std::size_t i = offset; i < text.size(); i++) {
      if (!IsSelectorSeparator(text[i])) return false;
    }
    return true;
  }

  // Doc strings shared by the four objects, hoisted out of the constructors
  // because they are long enough that a constructor stops being readable with
  // them inline — gArray's arrangement. What differs per object stays with
  // that object below.
  constexpr char kReferenceInletDoc[] =
      "Accepts the bound array's reference \"array <name>\" and ignores it, so the array's "
      "reference outlet can be wired here. Anything else is refused.";
  constexpr char kReferenceOutletDoc[] =
      "The array's reference \"array <name>\" after each reorder, so further .array objects can "
      "be chained. Nothing is sent when the object has no name.";
  constexpr char kNameParamDoc[] =
      "Name of the array to reorder: the .array with this name in the same patcher. Empty uses "
      "a private, empty array.";
  constexpr char kScrambleTriggerInletDoc[] =
      "Bang, or the bound array's reference \"array <name>\": shuffle the array into a new "
      "random order.";
  constexpr char kScrambleSeedInletDoc[] =
      "An int restarts the random sequence. A non-zero seed repeats the same shuffles every "
      "run; 0 gives an unpredictable sequence. A float is truncated.";
  constexpr char kScrambleOrderOutletDoc[] =
      "The order applied: for each new position, the position the element came from. Sent "
      "before the reference. Send it to .array.indexmap to put another array in the same order. "
      "Nothing is sent for an empty array.";
  constexpr char kScrambleSeedParamDoc[] =
      "Random seed. A non-zero seed repeats the same shuffles every run; 0 gives an "
      "unpredictable sequence.";

} // namespace

// ─── the shared base ──────────────────────────────────────────────────────────

#define className gArrayPermuteBase

void gArrayPermuteBase::ParamsChanged() {
  RefreshReference();
}

void gArrayPermuteBase::RefreshReference() {
  reference.clear();
  if (arrayName.empty()) return;
  reference.reserve(sizeof(kArrayReferenceWord) + arrayName.size());
  reference += kArrayReferenceWord;
  reference += ' ';
  reference += arrayName;
}

void gArrayPermuteBase::ApplyOrderLocked(std::size_t count) {
  // The first pass: copy every pick out, in order, into the scratch table
  // this object owns — the source and the destination are the same table, so
  // picking straight into the store would read elements a previous pick
  // already overwrote. Bounded assigns into storage both tables reserved at
  // construction — nothing here allocates. The past-the-end skip is
  // unreachable for an order computed under this same hold, and kept all the
  // same: the helper's contract is gArrayIndexMap's, not the caller's
  // arithmetic.
  std::size_t landed = 0;
  for (std::size_t i = 0; i < count; i++) {
    if (order[i] >= store->count) continue;
    scratch.elements[landed++].assign(store->elements[order[i]]);
  }

  // The second pass: the scratch table becomes the array, and the slots a
  // shorter result vacates are cleared behind the new count — ArrayEraseAt's
  // hygiene.
  for (std::size_t i = 0; i < landed; i++)
    store->elements[i].assign(scratch.elements[i]);
  for (std::size_t i = landed; i < store->count; i++)
    store->elements[i].clear();
  store->count = landed;
}

void gArrayPermuteBase::Announce(YSE::THREAD thread) {
  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well act on this same array, and inside the guard that access
  // would be the one thing the try-lock drops. An unnamed object has no
  // name to pass on — the permutation happened, the announcement is simply
  // empty.
  if (reference.empty()) return;
  outputs[0].SendList(reference, thread);
}

// ─── .array.reverse ───────────────────────────────────────────────────────────

#undef className
#define className gArrayReverse

gArrayReverse::gArrayReverse() : gArrayPermuteBase() {
  // The trigger inlet, hot, and the reference inlet, cold — the remover
  // twins' shape: a reversal is asked for with a bang, never parameterised,
  // so there is no int or float method anywhere.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  // The reference, always list text.
  ADD_OUT_LIST;

  ADD_DESCRIPTION(
      "Reverses the order of the elements of a named array, then sends the array's reference.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger",
            "Bang, or the bound array's reference \"array <name>\": reverse the array.", "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "reference", kReferenceOutletDoc, "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
}

BANG_IN(BangIn) {
  (void)inlet;
  Flip(thread);
}

LIST_IN(ListIn) {
  if (inlet == 1) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — the binding is the creation argument, resolved on the
    // control thread, so there is nothing to set. gDictSlice's inlet rule.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference reverses — the message its .array emits on a bang,
  // so wiring that outlet here gives the family's gesture. Anything else,
  // including a reference naming an array this object is not bound to, is
  // refused: resolving an unrecognised name means the registry's mutex, and
  // this may be the audio thread.
  if (ArrayReferenceNames(value, arrayName)) {
    Flip(thread);
    return;
  }
  Refuse();
}

void gArrayReverse::Flip(YSE::THREAD thread) {
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    // Counted down from the end — the whole of what a reversal is. Computed
    // from the length this same hold just read, so no entry can miss.
    const std::size_t size = store->count;
    for (std::size_t i = 0; i < size; i++)
      order[i] = static_cast<std::uint16_t>(size - 1 - i);
    ApplyOrderLocked(size);
  }
  Announce(thread);
}

// ─── .array.rotate ────────────────────────────────────────────────────────────

#undef className
#define className gArrayRotate

gArrayRotate::gArrayRotate() : gArrayPermuteBase() {
  // The trigger inlet, hot; the amount inlet and the reference inlet, cold —
  // .array.insert's arrangement, the Max idiom kept: configuration on the
  // right, the ask on the left.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  ADD_IN_2;
  REG_LIST_IN(ListIn);

  // The reference, always list text.
  ADD_OUT_LIST;

  // After gArrayEndsBase's name — the second creation argument.
  ADD_PARAM(amount);

  ADD_DESCRIPTION(
      "Rotates the elements of a named array by a number of positions, wrapping around the "
      "ends. Positive amounts move elements toward the end, negative toward the start.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger",
            "A bang rotates by the stored amount, and so does the bound array's reference. An "
            "int rotates by that amount instead, without storing it. A float is truncated.",
            "any int");
  INLET_DOC(1, "amount",
            "Sets the amount for the next bang. Any int; positive moves elements toward the "
            "end, negative toward the start. A float is truncated.",
            "any int");
  INLET_DOC(2, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "reference", kReferenceOutletDoc, "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("amount", "0", "The amount a bang rotates by before one arrives on the middle inlet.",
            "any int");
}

// A re-parse must not leave half of the previous configuration standing:
// SetParams("") — and the clear half of every Set() — drops the stored
// amount back to 0 along with the name. gArrayPositionBase's rule.
PARM_CLEAR() {
  amount.store(0, std::memory_order_relaxed);
  gArrayEndsBase::ClearParams();
}

BANG_IN(BangIn) {
  (void)inlet;
  Rotate(amount.load(std::memory_order_relaxed), thread);
}

INT_IN(IntIn) {
  if (inlet == 1) {
    // Stored silently, negative included — see the class notes: the amount
    // is a signed distance, not a position, so the family's negative-index
    // refusal deliberately does not apply here.
    amount.store(value, std::memory_order_relaxed);
    return;
  }
  // An int on the trigger is applied at the moment it arrives and stores
  // nothing — gArrayIndexMap's trigger rule.
  Rotate(value, thread);
}

FLOAT_IN(FloatIn) {
  // Max's float method on an int attribute is "convert to int", .table's
  // precedent. The negated range test keeps a NaN or an infinity — which
  // ExprToInt folds to 0 — from quietly becoming an amount of 0.
  if (!(value >= -2147483648.f && value < 2147483648.f)) {
    Refuse();
    return;
  }
  IntIn(ExprToInt(value), inlet, thread);
}

LIST_IN(ListIn) {
  if (inlet == 2) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — gDictSlice's inlet rule.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  if (ArrayReferenceNames(value, arrayName)) {
    // The array's reference rotates by the stored amount — the message its
    // .array emits on a bang, the family's gesture.
    Rotate(amount.load(std::memory_order_relaxed), thread);
    return;
  }

  // A list spelling exactly one signed int is the amount it spells, applied
  // at once — kept equivalent to the int, gArrayIndexMap's equivalence, so
  // an amount-producing outlet still lands. Anything else — more atoms, a
  // symbol, a reference naming an array this object is not bound to — is
  // refused whole.
  int by = 0;
  std::size_t offset = 0;
  if (!ReadIntArgAt(value, offset, by) || !AtEnd(value, offset)) {
    Refuse();
    return;
  }
  Rotate(by, thread);
}

void gArrayRotate::Rotate(int by, YSE::THREAD thread) {
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    const std::size_t size = store->count;
    if (size != 0) {
      // Modulo the length, in 64 bits so INT_MIN cannot overflow the
      // negation — gZl's OrderRotate, with the same direction: positive
      // rotates toward the end, so the element that was last comes out
      // first.
      long long shift = static_cast<long long>(by) % static_cast<long long>(size);
      if (shift < 0) shift += static_cast<long long>(size);
      const auto distance = static_cast<std::size_t>(shift);
      for (std::size_t i = 0; i < size; i++)
        order[i] = static_cast<std::uint16_t>((i + size - distance) % size);
      ApplyOrderLocked(size);
    }
  }
  Announce(thread);
}

// ─── the randomising pair ─────────────────────────────────────────────────────

#undef className
#define className gArrayScrambleBase

gArrayScrambleBase::gArrayScrambleBase() : gArrayPermuteBase() {
  // The trigger inlet, hot; the seed inlet and the reference inlet, cold —
  // .array.rotate's arrangement with a seed where the amount was.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  ADD_IN_2;
  REG_LIST_IN(ListIn);

  // The reference, then the applied order — both list text. The order is
  // sent first (Max's right-to-left rule); see the class notes for the
  // .array.indexmap idiom that ordering exists for.
  ADD_OUT_LIST;
  ADD_OUT_LIST;

  // After gArrayEndsBase's name — the second creation argument.
  ADD_PARAM(seed);

  // The allocation the order send would otherwise need, taken here on the
  // control thread.
  AtomList::ReserveRender(orderRender);
}

void gArrayScrambleBase::ParamsChanged() {
  gArrayPermuteBase::ParamsChanged();
  // Seeding here rather than in the constructor is what lets a saved patch
  // replay its shuffles: the seed is not known until the parameter string is
  // read. gUrn's arrangement.
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
  Shuffle(thread);
}

INT_IN(IntIn) {
  (void)inlet;
  // A run-time reseed restarts the sequence and rewrites nothing: the seed
  // parameter stays the author's argument, gArrayFindBase's split between a
  // seed and the live state. RandomSource::Seed is safe on whichever thread
  // this is.
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
    // nothing more — gDictSlice's inlet rule.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference shuffles — the message its .array emits on a
  // bang, the family's gesture. Anything else, including a reference naming
  // an array this object is not bound to, is refused: resolving an
  // unrecognised name means the registry's mutex, and this may be the audio
  // thread.
  if (ArrayReferenceNames(value, arrayName)) {
    Shuffle(thread);
    return;
  }
  Refuse();
}

void gArrayScrambleBase::Shuffle(YSE::THREAD thread) {
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    const std::size_t size = store->count;
    for (std::size_t i = 0; i < size; i++)
      order[i] = static_cast<std::uint16_t>(i);

    // Fisher-Yates downwards: every permutation equally likely (up to the
    // residual bias RandomSource::Bounded documents), one draw per element
    // moved, and a bounded number of them — a rejection loop would make the
    // running time unbounded, which does not belong on a path the audio
    // callback takes. gZl's OrderScramble.
    for (std::size_t i = size; i > 1; i--) {
      const auto j = static_cast<std::size_t>(rng.Bounded(static_cast<UInt>(i)));
      const std::uint16_t moved = order[i - 1];
      order[i - 1] = order[j];
      order[j] = moved;
    }
    ApplyOrderLocked(size);

    // The applied order as a list, built under the same hold — the order
    // table is guarded state, so the send below must not read it after the
    // guard is released. AddInt cannot refuse here: 256 entries of at most
    // three digits fit both of the list's ceilings.
    orderOut.Clear();
    for (std::size_t i = 0; i < size; i++)
      orderOut.AddInt(static_cast<int>(order[i]));
  }

  // Right before left — the order, then the reference — so a second
  // .array.indexmap's map is in place before the reference sets it running.
  // Outside the guard, as every send is; an empty order (an empty array)
  // sends nothing at all, SendAtoms' rule.
  SendAtoms(outputs[1], orderOut, orderRender, thread);
  Announce(thread);
}

// ─── .array.scramble ──────────────────────────────────────────────────────────

#undef className
#define className gArrayScramble

gArrayScramble::gArrayScramble() : gArrayScrambleBase() {
  ADD_DESCRIPTION(
      "Shuffles the elements of a named array into a random order, and sends the order it "
      "applied so another array can be put in the same order. The same object as "
      ".array.shuffle.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kScrambleTriggerInletDoc, "");
  INLET_DOC(1, "seed", kScrambleSeedInletDoc, "any int");
  INLET_DOC(2, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "reference", kReferenceOutletDoc, "");
  OUTLET_DOC(1, "order", kScrambleOrderOutletDoc, "0-255 each");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("seed", "0", kScrambleSeedParamDoc, "any int");
}

// ─── .array.shuffle ───────────────────────────────────────────────────────────

#undef className
#define className gArrayShuffle

gArrayShuffle::gArrayShuffle() : gArrayScrambleBase() {
  ADD_DESCRIPTION(
      "Shuffles the elements of a named array into a random order, and sends the order it "
      "applied so another array can be put in the same order. The same object as "
      ".array.scramble.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kScrambleTriggerInletDoc, "");
  INLET_DOC(1, "seed", kScrambleSeedInletDoc, "any int");
  INLET_DOC(2, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "reference", kReferenceOutletDoc, "");
  OUTLET_DOC(1, "order", kScrambleOrderOutletDoc, "0-255 each");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("seed", "0", kScrambleSeedParamDoc, "any int");
}
