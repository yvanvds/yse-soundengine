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
      "\"array <name>\" is accepted silently when it names the array bound by the first creation "
      "argument, so a patch may wire the array's reference outlet across. It sets nothing — the "
      "binding is the creation argument, resolved on the control thread — and anything else is "
      "refused and counted.";
  constexpr char kReferenceOutletDoc[] =
      "The bound array's reference, \"array <name>\", sent after a permutation that landed — the "
      "way an array leaves an object on the value model, so wiring it onward chains the family: "
      "into .array.at it fetches from the new order. A lost try-lock emits nothing (one counted "
      "refusal, nothing changed), and an unnamed object stays silent — the permutation happens, "
      "but there is no name to pass on.";
  constexpr char kNameParamDoc[] =
      "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an .array "
      "of the same name in this patcher holds. Resolved once, on the control thread, which is "
      "why no message re-points it at run time. Empty permutes a private, empty array: the "
      "trigger lands, but nothing shares it and no reference leaves.";
  constexpr char kScrambleTriggerInletDoc[] =
      "A bang shuffles the bound array in place — a fresh random order every time, from the "
      "per-object sequence the seed argument starts, so two bangs give two shuffles of the "
      "array rather than a shuffle of a shuffle only in the sense that the second acts on the "
      "first's result, exactly as Max's. An empty array shuffles to itself and still announces. "
      "\"array <name>\" does the same when it names the array bound by the first creation "
      "argument — the message an .array's reference outlet emits on a bang, the family's "
      "gesture. A reference naming anything else, or any other message, is refused and counted "
      "rather than logged, since this inlet may be the audio thread.";
  constexpr char kScrambleSeedInletDoc[] =
      "An int restarts the random sequence, silently — a non-zero seed replays the same "
      "shuffles every run, 0 takes an arbitrary stream, .urn's contract and Max's @seed 0. A "
      "float truncates to an int first; a non-finite one is refused rather than quietly "
      "becoming seed 0. The author's creation argument is not rewritten: a run-time reseed is "
      "the patch's business, the saved seed the author's.";
  constexpr char kScrambleOrderOutletDoc[] =
      "The applied order — the picks as zero-based indices into the array as it stood at the "
      "trigger, Max's \"scrambled index\" list — sent before the reference, Max's "
      "right-to-left rule and .zl sort's idiom: feed this to an .array.indexmap's map inlet "
      "and the reference to its trigger, and a parallel array lands in the same new order. A "
      "one-element order leaves as the int it spells, which .array.indexmap's map inlet "
      "accepts as the one-entry map; an empty array publishes no order.";
  constexpr char kScrambleSeedParamDoc[] =
      "Random seed for the shuffle order; non-zero replays the same shuffles every run, 0 "
      "picks an arbitrary stream.";

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
      "Reverses an array's order — Max's array.reverse on the name-addressed value model .array "
      "settled: the array is bound from the creation argument, \".array.reverse <name>\", "
      "because an array is addressed by name and never passed down a cord. A bang reverses the "
      "bound array in place — element i becomes element length-1-i, the whole reversal one hold "
      "of the store's guard through a scratch table the object owns, so the order applied is "
      "the array as it stood at the trigger. The message an .array's reference outlet emits on "
      "a bang triggers the same reversal, the family's gesture, and a reversal that lands "
      "emits the array's reference, so the family chains. An empty array reverses to itself "
      "and still announces.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger",
            "A bang reverses the bound array in place; an empty array reverses to itself and "
            "still announces. \"array <name>\" does the same when it names the array bound by "
            "the creation argument — the message an .array's reference outlet emits on a bang, "
            "the family's gesture. A reference naming anything else, or any other message, is "
            "refused and counted rather than logged, since this inlet may be the audio thread.",
            "");
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
      "Rotates an array by a signed amount, wrapping — Max's array.rotate on the name-addressed "
      "value model .array settled: the array is bound from the creation argument, "
      "\".array.rotate <name> [<amount>]\", because an array is addressed by name and never "
      "passed down a cord. This is where the wrapping the base type refuses actually lives — "
      "an index is a position, decided once on the store, and this object exists to provide "
      "the alternative — so the amount is a signed distance: any magnitude is legal, taken "
      "modulo the length so a whole turn is the identity, positive rotates toward the end with "
      "the element pushed past the last position wrapping to the front, negative toward the "
      "start, and 0 rotates by nothing and still announces. A bang rotates by the stored "
      "amount, seeded by the second creation argument and replaced silently by an int on the "
      "amount inlet; an int on the trigger rotates by that amount at the moment it arrives and "
      "stores nothing. The whole rotation is one hold of the store's guard through a scratch "
      "table the object owns; a rotation that lands emits the array's reference, so the "
      "family chains.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger",
            "A bang rotates the bound array by the stored amount — the amount the last int on "
            "the amount inlet stored, seeded by the second creation argument (0 when absent). "
            "An int rotates by that amount at the moment it arrives and stores nothing — a "
            "bang that replayed the last inline amount would be hidden state — a float "
            "truncates to an int first, and a list spelling exactly one signed int is the "
            "amount it spells, kept equivalent to the int. \"array <name>\" rotates by the "
            "stored amount when it names the array bound by the creation argument — the "
            "family's gesture. Anything else is refused and counted rather than logged, since "
            "this inlet may be the audio thread.",
            "any int");
  INLET_DOC(1, "amount",
            "An int stores the amount the next bang rotates by, silently — the cold half of "
            "the Max idiom. Negative is legal here, the whole point of the object: positive "
            "rotates toward the end, negative toward the start, any magnitude modulo the "
            "length. A float truncates to an int first; a non-finite one is refused rather "
            "than quietly becoming 0.",
            "any int");
  INLET_DOC(2, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "reference", kReferenceOutletDoc, "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("amount", "0",
            "The initial stored amount — what a bang rotates by before any int has moved it. "
            "Signed: positive toward the end, negative toward the start, any magnitude modulo "
            "the length.",
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
      "Reorders an array at random — Max's array.scramble on the name-addressed value model "
      ".array settled: the array is bound from the creation argument, \".array.scramble <name> "
      "[<seed>]\", because an array is addressed by name and never passed down a cord. A bang "
      "shuffles the bound array in place — Fisher-Yates from a per-object random sequence, so "
      "a non-zero seed replays the same shuffles every run and 0 takes an arbitrary stream — "
      "the whole shuffle one hold of the store's guard through a scratch table the object "
      "owns. A shuffle that lands publishes the applied order as zero-based indices out the "
      "order outlet, then emits the array's reference: feed the order to an .array.indexmap's "
      "map inlet and the reference to its trigger, and a parallel array lands in the same new "
      "order, .zl sort's idiom. Max ships array.scramble and array.shuffle as one object under "
      "two names, and so does this port: .array.shuffle is this same object.");
  ADD_CATEGORY(pCategory::GENERIC);
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
      "Shuffles an array's elements — Max's array.shuffle on the name-addressed value model "
      ".array settled: the array is bound from the creation argument, \".array.shuffle <name> "
      "[<seed>]\", because an array is addressed by name and never passed down a cord. A bang "
      "shuffles the bound array in place — Fisher-Yates from a per-object random sequence, so "
      "a non-zero seed replays the same shuffles every run and 0 takes an arbitrary stream — "
      "the whole shuffle one hold of the store's guard through a scratch table the object "
      "owns. A shuffle that lands publishes the applied order as zero-based indices out the "
      "order outlet, then emits the array's reference: feed the order to an .array.indexmap's "
      "map inlet and the reference to its trigger, and a parallel array lands in the same new "
      "order, .zl sort's idiom. Max ships array.shuffle and array.scramble as one object under "
      "two names, and so does this port: .array.scramble is this same object.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger", kScrambleTriggerInletDoc, "");
  INLET_DOC(1, "seed", kScrambleSeedInletDoc, "any int");
  INLET_DOC(2, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "reference", kReferenceOutletDoc, "");
  OUTLET_DOC(1, "order", kScrambleOrderOutletDoc, "0-255 each");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("seed", "0", kScrambleSeedParamDoc, "any int");
}
