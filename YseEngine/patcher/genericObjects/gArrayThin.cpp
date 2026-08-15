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
      "Thins an array by removing near-duplicate neighbours — issue #807's reading of Max's "
      "array.thin on the name-addressed value model .array settled: the array is bound from the "
      "creation argument, \".array.thin <name> [<tolerance>]\", because an array is addressed "
      "by name and never passed down a cord. Max's array.thin is the wholesale dedupe "
      ".array.unique already is here, so this is the NEIGHBOUR thin instead — one scan "
      "comparing each element with the last survivor before it, the repeats and near-repeats "
      "dropped, distinct values kept in order however often they recur later — the operation "
      "that decimates a control stream sampled faster than anything downstream needs. "
      "Tolerance 0, the default, is the exact thin: the family's byte compare, so symbols thin "
      "too and 7 and 7. stay distinct. A positive tolerance widens \"near\" for the pairs that "
      "can carry a distance: both elements reading as numbers and no further apart than the "
      "tolerance — measured against the last SURVIVOR, not the original predecessor, so a "
      "drift climbing in steps inside the tolerance is kept, not erased. A pair that is not "
      "wholly numeric falls back to the byte compare at any tolerance. A bang thins now; the "
      "removed count leaves the count outlet before the reference — right to left — so a patch "
      "can tell thinned-nothing (count 0, landed) from a refused thin (a lost try-lock, a "
      "malformed tolerance), which emits nothing. The whole thin is one hold of the store's "
      "guard — survivors close ranks in original order, each moved at most once — and yes, it "
      "renumbers under every other object on the name, exactly as .array.remove does: "
      "renumbering is what a removal is.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger",
            "A bang thins the array now: one scan against the last survivor, near-duplicate "
            "neighbours removed, under one hold of the store's guard. \"array <name>\" does the "
            "same when it names the array bound by the creation argument — the message an "
            ".array's reference outlet emits on a bang, the family's gesture. A reference "
            "naming anything else, or any other message, is refused and counted rather than "
            "logged, since this inlet may be the audio thread — as is a trigger while the "
            "stored tolerance is malformed (negative, a seed only the creation string can "
            "plant).",
            "");
  INLET_DOC(1, "tolerance",
            "An int or a float stores the tolerance, silently — the cold half of the idiom: "
            "nothing is thinned until the trigger asks. 0 restores the exact thin (the "
            "family's byte compare); a positive value calls a neighbour pair near when both "
            "elements read as numbers no further apart than this. Negative and non-finite are "
            "refused before they are stored — never clamped: a distance cannot be negative, "
            "and a non-finite tolerance would call everything near.",
            "0 or greater, finite");
  INLET_DOC(2, "array reference",
            "\"array <name>\" is accepted silently when it names the array bound by the "
            "creation argument, so a patch may wire the array's reference outlet across. It "
            "sets nothing — the binding is the creation argument, resolved on the control "
            "thread — and anything else is refused and counted.",
            "");
  OUTLET_DOC(0, "reference",
             "The bound array's reference, \"array <name>\", sent after a thin that landed — "
             "the way an array leaves an object on the value model, so wiring it onward chains "
             "the family: into .array.length it reports the thinned depth. Sent after the "
             "count outlet, Max's outlets firing right to left. A refused thin emits nothing, "
             "and an unnamed object stays silent — the thin happens, but there is no name to "
             "pass on.",
             "");
  OUTLET_DOC(1, "removed",
             "How many elements went, sent first — right to left — because it is the only way "
             "a patch can tell thinned-nothing from thinned-a-lot: a thin that removed nothing "
             "still landed and reports 0, where a refused one reports nothing at all. An "
             "unnamed (private) array counts as well — an answer is a value, not an identity.",
             "0-255");
  PARAM_DOC("name", "",
            "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an "
            ".array of the same name in this patcher holds. Resolved once, on the control "
            "thread, which is why no message re-points it at run time. Empty thins a private, "
            "empty array: the thin lands with count 0 and no reference leaves.",
            "any identifier");
  PARAM_DOC("tolerance", "0",
            "The initial tolerance — how far apart two numeric neighbours may sit and still "
            "count as duplicates. Absent (0) means the exact thin, the family's byte compare "
            "over every atom kind. Negative is malformed: it is refused at the trigger, never "
            "clamped, until the tolerance inlet stores a real one.",
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
