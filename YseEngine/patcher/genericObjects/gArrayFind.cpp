#include "gArrayFind.h"
#include "../math/gExprEval.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

#include <cstddef>
#include <cstring>
#include <string>

using namespace YSE::PATCHER;

namespace {

  // Doc strings shared by the two objects, hoisted out of the constructors
  // because they are long enough that a constructor stops being readable with
  // them inline — gArray's arrangement. What differs per object stays with
  // that object below.
  constexpr char kValueInletDoc[] =
      "An int, a float or a symbol searches for the element that spells it and stores the value; "
      "a bang re-searches with the stored value. Equality is the spelling — an int 7 finds the "
      "element \"7\", a float 7. finds \"7.\", and the two are different elements — and the "
      "match is case-sensitive, as every comparison in this patcher is. \"array <name>\" "
      "searches with the stored value when it names the array bound by the first creation "
      "argument — the message an .array's reference outlet emits on a bang, so wiring that "
      "outlet here gives the family's gesture. A multi-atom list is refused whole: an element "
      "is one atom, so only one atom can be held, and a sub-array match is not offered. A bang "
      "before any value has arrived, an atom past 64 characters, or a reference naming anything "
      "else is refused and counted rather than logged, since this inlet may be the audio "
      "thread.";
  constexpr char kReferenceInletDoc[] =
      "\"array <name>\" is accepted silently when it names the array bound by the creation "
      "argument, so a patch may wire the array's reference outlet across. It sets nothing — "
      "the binding is the creation argument, resolved on the control thread — and anything "
      "else is refused and counted.";
  constexpr char kNameParamDoc[] =
      "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an .array "
      "of the same name in this patcher holds. Resolved once, on the control thread, which is "
      "why no message re-points it at run time. Empty searches a private, empty array: every "
      "search misses.";
  constexpr char kValueParamDoc[] =
      "The initial stored value — what a bang searches for before any value has arrived on the "
      "inlet. One atom, exactly as every value the inlet takes; absent means no value, and a "
      "bang before one arrives is refused.";

} // namespace

// ─── the shared base ──────────────────────────────────────────────────────────

#define className gArrayFindBase

gArrayFindBase::gArrayFindBase() : gArrayEndsBase() {
  // The value inlet, hot, and the reference inlet, cold — gArrayAt's shape
  // with an atom where the index was: a value searches, a bang re-searches
  // with the stored one.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  ADD_PARAM(seedValue);
}

// A re-parse must not leave half of the previous configuration standing: the
// seed drops with the name, and the base's hook rebinds and re-syncs the
// stored value through ParamsChanged. gArrayPositionBase's arrangement.
void gArrayFindBase::ClearParams() {
  seedValue.clear();
  gArrayEndsBase::ClearParams();
}

void gArrayFindBase::ParamsChanged() {
  // The live stored value follows the seed argument. Control thread only,
  // but under the store's guard all the same: a message may be reading or
  // replacing the value on another thread this very moment. A lost guard
  // keeps the previous value, counted.
  const std::size_t length = seedValue.size();
  const arrayStoreGuard guard(store->busy);
  if (!guard.Held()) {
    Refuse();
    return;
  }
  if (length == 0 || length > ELEMENT_CAPACITY) {
    // An absent argument means no value. An over-long one cannot be an
    // element at all, so it becomes no value too — and is counted, where
    // the absent case is simply the object's initial state.
    if (length > ELEMENT_CAPACITY) Refuse();
    storedLength = 0;
    storedValue[0] = '\0';
    return;
  }
  std::memcpy(storedValue, seedValue.data(), length);
  storedValue[length] = '\0';
  storedLength = length;
}

std::string gArrayFindBase::Value() const {
  const arrayStoreGuard guard(store->busy);
  if (!guard.Held()) return std::string();
  return std::string(storedValue, storedLength);
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  SearchStored(thread);
}

INT_IN(IntIn) {
  (void)inlet;
  char text[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
  // ExprFormatValue cannot fail on an int, but the check keeps a formatting
  // failure counted rather than silently searching for an empty value.
  if (written <= 0) {
    Refuse();
    return;
  }
  SearchFor(text, static_cast<std::size_t>(written), thread);
}

FLOAT_IN(FloatIn) {
  (void)inlet;
  // A non-finite float has no spelling that reads back — ExprFormatValue
  // renders it as "0." — so searching for it would quietly become a search
  // for something else. Refused, with the negated range test so a NaN takes
  // this branch too; the end-writers' rule, for the same reason.
  if (!(value >= -3.402823466e38f && value <= 3.402823466e38f)) {
    Refuse();
    return;
  }
  char text[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  if (written <= 0) {
    Refuse();
    return;
  }
  SearchFor(text, static_cast<std::size_t>(written), thread);
}

LIST_IN(ListIn) {
  if (inlet == 1) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — the binding is the creation argument, resolved on the
    // control thread, so there is nothing to set. gDictSlice's inlet rule.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference searches with the stored value — the message its
  // .array emits on a bang, so wiring that outlet here gives the family's
  // gesture: bang the array, out comes the stored value's position. No
  // ambiguity with data: a search value is one atom and a reference is two,
  // so the reference can never be a value this inlet would accept.
  if (ArrayReferenceNames(value, arrayName)) {
    SearchStored(thread);
    return;
  }

  // One atom is a value; more than one is refused whole — an element is one
  // atom, so only one atom can be held, and a sub-array match is a match
  // over a sequence of positions that renumbering writes would tear (see
  // the class notes). That covers a reference naming an array this object
  // is not bound to as well: resolving an unrecognised name means the
  // registry's mutex, and this may be the audio thread.
  std::size_t begin = 0;
  while (begin < value.size() && IsSelectorSeparator(value[begin]))
    begin++;
  std::size_t end = begin;
  while (end < value.size() && !IsSelectorSeparator(value[end]))
    end++;
  std::size_t tail = end;
  while (tail < value.size() && IsSelectorSeparator(value[tail]))
    tail++;
  if (begin == end || tail != value.size()) {
    Refuse();
    return;
  }
  const std::size_t length = end - begin;
  if (length > ELEMENT_CAPACITY) {
    // Longer than any element can be, so it can never match — refused as
    // malformed rather than reported as a miss, arrayStore's refusal rule.
    Refuse();
    return;
  }
  SearchFor(value.data() + begin, length, thread);
}

void gArrayFindBase::SearchFor(const char* text, std::size_t length, YSE::THREAD thread) {
  bool found = false;
  std::size_t position = 0;
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      // Refused whole: neither the value nor an answer — the stored value
      // is guarded by this same hold, so a message that cannot take it
      // cannot move it either.
      Refuse();
      return;
    }
    std::memcpy(storedValue, text, length);
    storedValue[length] = '\0';
    storedLength = length;
    position = ArrayFind(*store, storedValue, storedLength);
    found = position < store->count;
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well write into this same array, and inside the guard that
  // write would be the one thing the try-lock drops.
  Report(found, position, thread);
}

void gArrayFindBase::SearchStored(YSE::THREAD thread) {
  bool found = false;
  bool missing = false;
  std::size_t position = 0;
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    if (storedLength == 0) {
      // No value has ever arrived and no argument seeded one — malformed,
      // not a miss: a miss is a well-formed value the array happens not to
      // hold, where this ask holds nothing to look for.
      missing = true;
    } else {
      position = ArrayFind(*store, storedValue, storedLength);
      found = position < store->count;
    }
  }

  if (missing) {
    Refuse();
    return;
  }
  Report(found, position, thread);
}

// ─── .array.indexof ───────────────────────────────────────────────────────────

#undef className
#define className gArrayIndexOf

gArrayIndexOf::gArrayIndexOf() : gArrayFindBase() {
  // One outlet, always an int: the miss travels in-band as -1, which is
  // this object's whole difference from .array.index.
  ADD_OUT_INT;

  ADD_DESCRIPTION(
      "Outputs the position of a value — Max's array.indexof on the name-addressed value model "
      ".array settled: the array is bound from the creation argument, \".array.indexof <name> "
      "[<value>]\", because an array is addressed by name and never passed down a cord. An int, "
      "a float or a symbol searches for the element that spells it and stores the value; a bang "
      "re-searches with the stored value, and the message an .array's reference outlet emits on "
      "a bang does the same, so wiring that outlet here gives the family's gesture. The answer "
      "is one int: the first matching position, or -1 on a miss — Max's own answer, the one a "
      "patch can test with .sel -1 or feed straight into .array.at. Every position is not "
      "reported: the reply stays one usable int, and renumbering writes make a set of "
      "positions stale as a set where a single position is simply a position. The search is "
      "one scan under one hold of the store's guard, so the answer is the array as it stood at "
      "the trigger; .array.index is the same search with the miss split onto an outlet.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "value", kValueInletDoc, "one atom");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "position",
             "The first position holding the searched value, or -1 when no element spells it — "
             "the miss in-band, so one int leaves per answered search. Zero-based, read under "
             "one hold of the store's guard, sent after it is released. An empty or unnamed "
             "(private) array misses every search.",
             "-1, or 0-255");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("value", "", kValueParamDoc, "one atom");
}

void gArrayIndexOf::Report(bool found, std::size_t position, YSE::THREAD thread) {
  outputs[0].SendInt(found ? static_cast<int>(position) : -1, thread);
}

// ─── .array.index ─────────────────────────────────────────────────────────────

#undef className
#define className gArrayIndex

gArrayIndex::gArrayIndex() : gArrayFindBase() {
  // The family's split: the position outlet answers a hit, the miss outlet
  // a miss — whether is which outlet fired, where is the int.
  ADD_OUT_INT;
  ADD_OUT_BANG;

  ADD_DESCRIPTION(
      "Outputs whether, and where, a value is held — the membership half of the search pair on "
      "the name-addressed value model .array settled: the array is bound from the creation "
      "argument, \".array.index <name> [<value>]\", because an array is addressed by name and "
      "never passed down a cord. An int, a float or a symbol searches for the element that "
      "spells it and stores the value; a bang re-searches with the stored value, and the "
      "message an .array's reference outlet emits on a bang does the same — the family "
      "gesture. The first matching position leaves the position outlet on a hit and a miss "
      "bangs the miss outlet — .array.at's split, so whether is which outlet fired and where "
      "is the int, and the membership test becomes a cord choice rather than a comparison: "
      "wire the position outlet to the held branch and the miss outlet to the not-held branch, "
      "no .sel -1 in between. The search is one scan under one hold of the store's guard; "
      ".array.indexof is the same search answered in-band as -1.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "value", kValueInletDoc, "one atom");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "position",
             "The first position holding the searched value — the value is held, and this is "
             "where. Zero-based, read under one hold of the store's guard, sent after it is "
             "released. Silent on a miss: the miss outlet carries that half of the answer.",
             "0-255");
  OUTLET_DOC(1, "miss",
             "Bang when no element spells the searched value — the value is not held. Kept off "
             "the position outlet so a patch can route on membership without comparing: an "
             "empty or unnamed (private) array misses every search.",
             "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("value", "", kValueParamDoc, "one atom");
}

void gArrayIndex::Report(bool found, std::size_t position, YSE::THREAD thread) {
  if (found) {
    outputs[0].SendInt(static_cast<int>(position), thread);
    return;
  }
  outputs[1].SendBang(thread);
}
