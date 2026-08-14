#include "gArraySetOps.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

#include <cstddef>
#include <memory>
#include <string>

using namespace YSE::PATCHER;

namespace {

  // Doc strings shared by the three objects, hoisted out of the constructors
  // because they are long enough that a constructor stops being readable with
  // them inline — gArray's arrangement. What differs per object stays with
  // that object below.
  constexpr char kPairTriggerInletDoc[] =
      "A bang asks for the result — the set operation over the two bound arrays as they stood "
      "at the trigger, the left copied out under its guard, the result built against the right "
      "under that guard alone, and sent after every guard is released. \"array <name>\" does "
      "the same when it names the left array bound by the first creation argument — the message "
      "an .array's reference outlet emits on a bang, so wiring that outlet here gives the "
      "family's gesture. A reference naming anything else, or any other message, is refused and "
      "counted rather than logged, since this inlet may be the audio thread.";
  constexpr char kRightReferenceInletDoc[] =
      "\"array <name>\" is accepted silently when it names the right array bound by the second "
      "creation argument, so a patch may wire both reference outlets across as it would in Max. "
      "It sets nothing — the binding is the creation argument, resolved on the control thread — "
      "and anything else is refused and counted.";
  constexpr char kPairEmptyOutletDoc[] =
      "Bang when the operation selected nothing — for .array.sect two arrays with no element in "
      "common, for .array.union two empty (or unnamed, private) arrays. \"No data\" is a state "
      "a patch must be able to route on, not an error — routing on an empty intersection is "
      "half the point of asking. A lost try-lock on either store is a counted refusal instead: "
      "the arrays' state is unknown, so neither outlet fires.";
  constexpr char kLeftParamDoc[] =
      "The left array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an "
      ".array of the same name in this patcher holds. Resolved once, on the control thread, "
      "which is why no message re-points it at run time. Empty reads a private, empty array on "
      "that side.";
  constexpr char kRightParamDoc[] =
      "The right array's shared name, bound exactly as the left one. Empty reads a private, "
      "empty array on that side.";

} // namespace

// ─── the shared base ──────────────────────────────────────────────────────────

#define className gArraySetOpBase

gArraySetOpBase::gArraySetOpBase(bool intersect) : gArrayEndsBase(), intersect(intersect) {
  // The trigger inlet, hot, and the right reference inlet, cold —
  // gDictCompare's shape: the left inlet asks, the right one only
  // acknowledges the array it is already bound to. The result is asked for
  // with a bang, never addressed, so there is no int or float method
  // anywhere.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  // ANY on the result outlet: one element leaves as the atom it spells,
  // several as list text — SendAtoms' rule. The empty outlet is always a
  // bang.
  ADD_OUT_ANY;
  ADD_OUT_BANG;

  // After gArrayEndsBase's name — the second creation argument.
  ADD_PARAM(rightName);

  // A private, empty right store to start with, so `rightStore` is never
  // null and no message handler needs a null check — the base constructor
  // has already done the same for the left side.
  RebindRight();

  // The allocation the send path would otherwise need, taken here on the
  // control thread.
  AtomList::ReserveRender(emitScratch);
}

// A re-parse must not leave half of the previous configuration standing:
// SetParams("") — and the clear half of every Set() — drops the right name
// along with the left. gArraySliceBase's rule for its bounds.
PARM_CLEAR() {
  rightName.clear();
  gArrayEndsBase::ClearParams();
}

// The base calls this after ClearParams / ParseParams have re-read the
// names — the moment the right binding follows the left one, which the base's
// own Rebind() has just re-anchored.
void gArraySetOpBase::ParamsChanged() {
  RebindRight();
}

void gArraySetOpBase::SetParent(pObject* newParent) {
  gArrayEndsBase::SetParent(newParent);
  RebindRight();
}

void gArraySetOpBase::RefreshBinding() {
  gArrayEndsBase::RefreshBinding();
  RebindRight();
}

void gArraySetOpBase::RebindRight() {
  // The exact mirror of gArrayEndsBase::Rebind over the second name. No
  // name, or no patcher to prefix it with, means no address — and no address
  // means a private, empty array on that side. See gArray.h for why an
  // unnamed side does not pool on "<patcherName>.".
  std::string address;
  if (!rightName.empty() && parent != nullptr) {
    auto* p = static_cast<patcherImplementation*>(parent);
    address = p->Name() + "." + rightName;
  }

  // Unchanged binding: keep the store. A live SetParams that leaves the name
  // alone must not re-anchor it, and neither must the second rebind a Set()
  // makes (clear, then parse).
  if (rightStore != nullptr && address == boundRightAddress) return;

  bool created = false;
  rightStore = address.empty() ? std::make_shared<arrayStore>()
                               : AcquireNamedStore<arrayStore>(address, created);
  boundRightAddress = address;
}

// ─── the pair's messages ──────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  Ask(thread);
}

LIST_IN(ListIn) {
  if (inlet == 1) {
    // The right reference inlet acknowledges the array it is already bound
    // to and nothing more — the binding is the creation argument, resolved
    // on the control thread, so there is nothing to set. gDictCompare's
    // inlet rule.
    if (!ArrayReferenceNames(value, rightName)) Refuse();
    return;
  }

  // The left array's reference asks for the result — the message its .array
  // emits on a bang, the family's gesture. Anything else, including a
  // reference naming an array this object is not bound to on this side, is
  // refused: resolving an unrecognised name means the registry's mutex, and
  // this may be the audio thread.
  if (ArrayReferenceNames(value, arrayName)) {
    Ask(thread);
    return;
  }
  Refuse();
}

void gArraySetOpBase::Ask(YSE::THREAD thread) {
  // Copy the left array out under its guard, so the right store's guard is
  // never nested inside it: two guards at once would put a lock-ordering
  // obligation on every pair of objects naming the same two arrays, and the
  // same-store case — ".array.union chord chord" — would trip over its own
  // try-lock and refuse every ask. Bounded assigns into strings reserved at
  // construction — no allocation. gDictCompare's arrangement, whole.
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    snapshot.count = store->count;
    for (std::size_t i = 0; i < store->count; i++)
      snapshot.elements[i].assign(store->elements[i]);
  }

  bool collected = false;
  result.Clear();
  {
    const arrayStoreGuard guard(rightStore->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    collected = CollectLocked();
  }

  // Outside both guards on purpose: a send runs the whole downstream graph,
  // which may well write into either of these arrays, and inside a guard
  // that write would be the one thing the try-lock drops.
  if (!collected) {
    // The one refusal a well-formed ask can still meet: the result spells
    // more atoms or text than a cord carries — a union of two large arrays
    // can. Refused whole — a partial set would be a lie about membership,
    // .array.at's whole-reply rule.
    Refuse();
    return;
  }
  if (result.Empty()) {
    outputs[1].SendBang(thread);
    return;
  }
  SendAtoms(outputs[0], result, emitScratch, thread);
}

bool gArraySetOpBase::CollectLocked() {
  // The left half: the snapshot thinned — each element once, at its first
  // occurrence (ArrayFind reports the first position, so "this is the first"
  // is one bounded scan) — and, for sect, only the elements the right store
  // also holds. Equality is the spelling, ArrayFind's byte compare.
  for (std::size_t i = 0; i < snapshot.count; i++) {
    const std::string& element = snapshot.elements[i];
    if (ArrayFind(snapshot, element.data(), element.size()) != i) continue;
    if (intersect && ArrayFind(*rightStore, element.data(), element.size()) == rightStore->count) {
      continue;
    }
    if (!result.Add(element.data(), element.size())) return false;
  }
  if (intersect) return true;

  // Union's right half: the right store's elements the left does not hold,
  // thinned the same way — .zl union's "the right half loses whatever the
  // left already has".
  for (std::size_t j = 0; j < rightStore->count; j++) {
    const std::string& element = rightStore->elements[j];
    if (ArrayFind(*rightStore, element.data(), element.size()) != j) continue;
    if (ArrayFind(snapshot, element.data(), element.size()) != snapshot.count) continue;
    if (!result.Add(element.data(), element.size())) return false;
  }
  return true;
}

// ─── .array.union ─────────────────────────────────────────────────────────────

#undef className
#define className gArrayUnion

gArrayUnion::gArrayUnion() : gArraySetOpBase(false) {
  ADD_DESCRIPTION(
      "Outputs the elements held by either of two arrays — Max's array.union on the "
      "name-addressed value model .array settled: both arrays are bound from the creation "
      "arguments, \".array.union <left> <right>\", because an array is addressed by name and "
      "never passed down a cord, and neither may be re-pointed from a message. A set operation "
      "produces a set — .zl union's semantics: the left array with its repeats dropped, then "
      "the right array's elements the left does not hold, each element once at its first "
      "occurrence, equality by the spelling (7 and 7. are different elements). The result "
      "leaves as the list it spells, never as a new named array — creating one would resolve a "
      "name on a message path — and two empty arrays bang the empty outlet. The left array is "
      "copied out under its guard and the result built against the right under that guard "
      "alone, so no two guards are ever held at once and \".array.union chord chord\" answers "
      "instead of tripping over its own try-lock. \"Everything either hand plays\" — the "
      "combining half of the harmonic patch .array.sect is the filtering half of.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger", kPairTriggerInletDoc, "");
  INLET_DOC(1, "right reference", kRightReferenceInletDoc, "");
  OUTLET_DOC(0, "union",
             "The elements held by either array, as the list they spell — one element as the "
             "int, float or symbol it is, several as list text, never as a new named array. The "
             "left array thinned, then the right array's elements the left does not hold, each "
             "element once at its first occurrence. A result that outruns what a cord carries "
             "is refused whole and counted — a partial set would be a lie about membership. "
             "Silent when both arrays are empty: the empty outlet carries that half of the "
             "answer.",
             "");
  OUTLET_DOC(1, "empty", kPairEmptyOutletDoc, "");
  PARAM_DOC("left", "", kLeftParamDoc, "any identifier");
  PARAM_DOC("right", "", kRightParamDoc, "any identifier");
}

// ─── .array.sect ──────────────────────────────────────────────────────────────

#undef className
#define className gArraySect

gArraySect::gArraySect() : gArraySetOpBase(true) {
  ADD_DESCRIPTION(
      "Outputs the elements held by both of two arrays — Max's array.sect on the name-addressed "
      "value model .array settled: both arrays are bound from the creation arguments, "
      "\".array.sect <left> <right>\", because an array is addressed by name and never passed "
      "down a cord, and neither may be re-pointed from a message. A set operation produces a "
      "set — .zl sect's semantics: the left array's elements the right also holds, in the left "
      "array's order, each element once at its first occurrence, equality by the spelling (7 "
      "and 7. are different elements). The result leaves as the list it spells, never as a new "
      "named array, and an empty intersection bangs the empty outlet — routing on \"nothing in "
      "common\" is half the point of asking. The left array is copied out under its guard and "
      "tested against the right under that guard alone, so no two guards are ever held at once. "
      "\"Which notes are in both chords\" — the operation a harmonic or scale patch is written "
      "from.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger", kPairTriggerInletDoc, "");
  INLET_DOC(1, "right reference", kRightReferenceInletDoc, "");
  OUTLET_DOC(0, "intersection",
             "The elements held by both arrays, as the list they spell — one element as the "
             "int, float or symbol it is, several as list text, never as a new named array. The "
             "left array's order, each element once at its first occurrence. A result that "
             "outruns what a cord carries is refused whole and counted. Silent when the "
             "intersection is empty: the empty outlet carries that half of the answer.",
             "");
  OUTLET_DOC(1, "empty", kPairEmptyOutletDoc, "");
  PARAM_DOC("left", "", kLeftParamDoc, "any identifier");
  PARAM_DOC("right", "", kRightParamDoc, "any identifier");
}

// ─── .array.unique ────────────────────────────────────────────────────────────

#undef className
#define className gArrayUnique

gArrayUnique::gArrayUnique() : gArrayEndsBase() {
  // The trigger inlet, hot, and the reference inlet, cold — gArrayStatsBase's
  // shape: the result is asked for with a bang, never addressed, so there is
  // no int or float method anywhere.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  // ANY on the result outlet, a bang on the empty one — the pair's shape.
  ADD_OUT_ANY;
  ADD_OUT_BANG;

  // The allocation the send path would otherwise need, taken here on the
  // control thread.
  AtomList::ReserveRender(emitScratch);

  ADD_DESCRIPTION(
      "Outputs an array with its repeated elements dropped — Max's array.unique on the "
      "name-addressed value model .array settled: the array is bound from the creation "
      "argument, \".array.unique <name>\", because an array is addressed by name and never "
      "passed down a cord. Each element once, at the position of its first occurrence — .zl "
      "thin's selection, under Max's array.unique name for it — with equality by the spelling "
      "(7 and 7. are different elements). The result leaves as the list it spells, never as a "
      "new named array, and the shared store is read, only: .array itself is where a patch "
      "would write the thinned result back. An empty or unnamed (private) array bangs the "
      "empty outlet. The whole selection happens under one hold of the store's guard, so the "
      "result is the array as it stood at the trigger.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger",
            "A bang asks for the thinned array — every element once, at its first occurrence, "
            "collected under one hold of the store's guard and sent after it is released. "
            "\"array <name>\" does the same when it names the array bound by the creation "
            "argument — the message an .array's reference outlet emits on a bang, the family's "
            "gesture. A reference naming anything else, or any other message, is refused and "
            "counted rather than logged, since this inlet may be the audio thread.",
            "");
  INLET_DOC(1, "array reference",
            "\"array <name>\" is accepted silently when it names the array bound by the "
            "creation argument, so a patch may wire the array's reference outlet across. It "
            "sets nothing — the binding is the creation argument, resolved on the control "
            "thread — and anything else is refused and counted.",
            "");
  OUTLET_DOC(0, "unique",
             "The array with its repeats dropped, as the list it spells — one element as the "
             "int, float or symbol it is, several as list text, never as a new named array. "
             "Each element at the position of its first occurrence, equality by the spelling. "
             "A result that outruns what a cord carries is refused whole and counted. Silent "
             "on an empty array: the empty outlet carries that half of the answer.",
             "");
  OUTLET_DOC(1, "empty",
             "Bang when the array holds nothing to thin — an empty or unnamed (private) "
             "array. \"No data\" is a state a patch must be able to route on, not an error. A "
             "lost try-lock is a counted refusal instead: the array's state is unknown, so "
             "neither outlet fires.",
             "");
  PARAM_DOC("name", "",
            "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an "
            ".array of the same name in this patcher holds. Resolved once, on the control "
            "thread, which is why no message re-points it at run time. Empty reads a private, "
            "empty array: every ask bangs the empty outlet.",
            "any identifier");
}

BANG_IN(BangIn) {
  (void)inlet;
  Ask(thread);
}

LIST_IN(ListIn) {
  if (inlet == 1) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — gDictSlice's inlet rule.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference asks for the thinned array — the message its
  // .array emits on a bang, the family's gesture. Anything else is refused:
  // resolving an unrecognised name means the registry's mutex, and this may
  // be the audio thread.
  if (ArrayReferenceNames(value, arrayName)) {
    Ask(thread);
    return;
  }
  Refuse();
}

void gArrayUnique::Ask(YSE::THREAD thread) {
  bool collected = true;
  result.Clear();
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    // One bounded pass: an element joins the result only at its first
    // occurrence — ArrayFind reports the first position, so "this is the
    // first" is one scan — and the shared store is never written or
    // reordered. Equality is the spelling, ArrayFind's byte compare.
    for (std::size_t i = 0; i < store->count && collected; i++) {
      const std::string& element = store->elements[i];
      if (ArrayFind(*store, element.data(), element.size()) != i) continue;
      collected = result.Add(element.data(), element.size());
    }
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well write into this same array, and inside the guard that
  // write would be the one thing the try-lock drops.
  if (!collected) {
    // The result spells more text than a cord carries — an array of only
    // distinct 64-character elements can. Refused whole and counted,
    // .array.at's whole-reply rule.
    Refuse();
    return;
  }
  if (result.Empty()) {
    outputs[1].SendBang(thread);
    return;
  }
  SendAtoms(outputs[0], result, emitScratch, thread);
}
