#include "gArrayThin.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"

#include <cstddef>
#include <cstring>
#include <string>

using namespace YSE::PATCHER;

namespace {

  // The widest finite float, spelled the way the family's other negated
  // range tests spell it: a tolerance past it — or a NaN, which fails every
  // comparison — is not a distance.
  constexpr float kMaxFinite = 3.402823466e38f;

} // namespace

#define className gArrayThin

gArrayThin::gArrayThin() : gArrayEndsBase() {
  // The trigger inlet, hot; the tolerance inlet and the reference inlet,
  // cold — gArrayEndsRemover's trigger (a thin is asked for with a bang,
  // never addressed, so no int or float method on the hot side) with
  // gArrayFill's cold configuration beside it.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  ADD_IN_2;
  REG_LIST_IN(ListIn);

  // The reference, always list text; the removed count, always an int. Sent
  // right to left: the count first, then the reference.
  ADD_OUT_LIST;
  ADD_OUT_INT;

  // After gArrayEndsBase's name — the second creation argument.
  ADD_PARAM(tolerance);

  ADD_DESCRIPTION(
      "Removes elements of a named array that repeat the element kept before them. With a "
      "tolerance, numbers that close to it count as repeats too, which thins out a stream of "
      "values that barely change.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger",
            "Bang, or the bound array's reference \"array <name>\": thin the array now.", "");
  INLET_DOC(1, "tolerance",
            "Sets the largest difference between two numbers that still counts as a repeat. 0 "
            "means exact repeats only. Negative values are refused.",
            "0 or greater, finite");
  INLET_DOC(2, "array reference",
            "Accepts the bound array's reference \"array <name>\" and ignores it, so the "
            "array's reference outlet can be wired here. Anything else is refused.",
            "");
  OUTLET_DOC(0, "reference",
             "The array's reference \"array <name>\" after each thin, so further .array objects "
             "can be chained. Sent after outlet 1. Nothing is sent when the object has no name.",
             "");
  OUTLET_DOC(1, "removed",
             "How many elements were removed; 0 when none. Sent before the reference.", "0-255");
  PARAM_DOC("name", "",
            "Name of the array to thin: the .array with this name in the same patcher. Empty "
            "uses a private, empty array.",
            "any identifier");
  PARAM_DOC("tolerance", "0",
            "The largest difference between two numbers that still counts as a repeat. 0 means "
            "exact repeats only.",
            "0 or greater, finite");
}

// A re-parse must not leave half of the previous configuration standing:
// SetParams("") drops the stored tolerance back to 0 — the exact thin —
// along with the name. gArrayStream's rule.
PARM_CLEAR() {
  tolerance.store(0.f, std::memory_order_relaxed);
  gArrayEndsBase::ClearParams();
}

void gArrayThin::ParamsChanged() {
  RefreshReference();
}

void gArrayThin::RefreshReference() {
  reference.clear();
  if (arrayName.empty()) return;
  reference.reserve(sizeof(kArrayReferenceWord) + arrayName.size());
  reference += kArrayReferenceWord;
  reference += ' ';
  reference += arrayName;
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  Thin(thread);
}

INT_IN(IntIn) {
  (void)inlet;
  // Stored silently — the cold half of the idiom. An int is a distance as
  // readily as a float: 1 thins semitone-close neighbours.
  StoreTolerance(static_cast<float>(value));
}

FLOAT_IN(FloatIn) {
  (void)inlet;
  StoreTolerance(value);
}

LIST_IN(ListIn) {
  if (inlet == 2) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — the binding is the creation argument, resolved on the
    // control thread, so there is nothing to set. gDictSlice's inlet rule.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference thins — the message its .array emits on a bang, so
  // wiring that outlet here gives the family's gesture: bang the array, out
  // comes the thinned depth. Anything else, including a reference naming an
  // array this object is not bound to, is refused: resolving an unrecognised
  // name means the registry's mutex, and this may be the audio thread.
  // gArrayEndsRemover's rule.
  if (ArrayReferenceNames(value, arrayName)) {
    Thin(thread);
    return;
  }
  Refuse();
}

void gArrayThin::StoreTolerance(float value) {
  // 0 is the exact thin, so it stores; a negative distance has no meaning
  // and a non-finite one would call everything near — both refused before
  // they are stored, never clamped, and the stored tolerance does not move:
  // a trigger after the refusal thins to the distance it would have thinned
  // to before it. The negated range test sends a NaN down this branch too.
  if (!(value >= 0.f && value <= kMaxFinite)) {
    Refuse();
    return;
  }
  tolerance.store(value, std::memory_order_relaxed);
}

// ─── the thin itself ──────────────────────────────────────────────────────────

void gArrayThin::Thin(YSE::THREAD thread) {
  // A malformed tolerance is a seed only the creation string can plant — the
  // inlet refuses one before storing it — and it refuses the trigger whole:
  // malformed, not a miss, and never clamped. gArrayStream's rule for an
  // out-of-range creation argument.
  const float tol = tolerance.load(std::memory_order_relaxed);
  if (!(tol >= 0.f && tol <= kMaxFinite)) {
    Refuse();
    return;
  }

  std::size_t removed = 0;
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    removed = ThinLocked(tol);
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well act on this same array, and inside the guard that access
  // would be the one thing the try-lock drops.
  Announce(removed, thread);
}

std::size_t gArrayThin::ThinLocked(float tol) {
  const std::size_t old = store->count;
  if (old < 2) return 0;

  // The first element has nothing before it, so it always survives — and it
  // is the first baseline. Its numeric reading is parsed once and carried
  // forward, as every survivor's is, so the scan reads each element exactly
  // once: one bounded copy into a stack buffer and one strtof, no
  // allocation (ReadNumericToken).
  std::size_t kept = 1;
  float survivorNumber = 0.f;
  bool survivorNumeric =
      ReadNumericToken(store->elements[0].data(), store->elements[0].size(), survivorNumber);

  for (std::size_t i = 1; i < old; i++) {
    const std::string& element = store->elements[i];
    const std::string& survivor = store->elements[kept - 1];
    float number = 0.f;
    const bool numeric = ReadNumericToken(element.data(), element.size(), number);

    // Near, against the last survivor: the same spelling — an exact
    // duplicate is the nearest duplicate there is, so it goes at any
    // tolerance — or, when a tolerance is set and both sides read as
    // numbers, a distance inside it. A pair that is not wholly numeric has
    // no distance to measure, so the byte compare is the whole of it.
    bool near = element.size() == survivor.size() &&
                std::memcmp(element.data(), survivor.data(), element.size()) == 0;
    if (!near && tol > 0.f && numeric && survivorNumeric) {
      const float distance =
          number >= survivorNumber ? number - survivorNumber : survivorNumber - number;
      near = distance <= tol;
    }
    if (near) continue;

    // A keeper: close ranks in original order — a bounded assign into
    // storage the store reserved at construction, a position never moving
    // up — and it becomes the baseline the next element is measured
    // against. gArrayFilter's compaction.
    if (kept != i) store->elements[kept].assign(element);
    survivorNumeric = numeric;
    survivorNumber = number;
    kept++;
  }

  // Clear the vacated slots behind the new count — ArrayEraseAt's hygiene,
  // gArrayFilter's apply.
  for (std::size_t i = kept; i < old; i++)
    store->elements[i].clear();
  store->count = kept;
  return old - kept;
}

void gArrayThin::Announce(std::size_t removed, YSE::THREAD thread) {
  // The count first — right to left, so the number has arrived wherever it
  // is wired by the time the reference triggers the family downstream — and
  // count 0 is an answer, not a refusal: the ask was well-formed, the array
  // simply holds no near-duplicate neighbours. An unnamed object still
  // counts (an answer is a value, not an identity) but has no name to pass
  // on — the thin happened, the reference is simply empty.
  outputs[1].SendInt(static_cast<int>(removed), thread);
  if (reference.empty()) return;
  outputs[0].SendList(reference, thread);
}
