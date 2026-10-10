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
      "An int, float or word searches for that value and remembers it; a bang searches for the "
      "remembered value again, and so does the bound array's reference. A list of more than one "
      "item is refused.";
  constexpr char kReferenceInletDoc[] =
      "Accepts the bound array's reference \"array <name>\" and ignores it, so the array's "
      "reference outlet can be wired here. Anything else is refused.";
  constexpr char kNameParamDoc[] =
      "Name of the array to search: the .array with this name in the same patcher. Empty "
      "searches a private, empty array.";
  constexpr char kValueParamDoc[] =
      "The value a bang searches for before any value arrives. Without it, a bang is refused "
      "until a value has arrived.";

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
      "Outputs the position of the first element of a named array that matches a value, or -1 "
      "when no element matches.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "value", kValueInletDoc, "one atom");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "position",
             "The position of the first matching element, or -1 when no element matches.",
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
      "Looks up a value in a named array. The position of the first matching element leaves "
      "the left outlet; when no element matches, the right outlet bangs.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "value", kValueInletDoc, "one atom");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "position", "The position of the first matching element.", "0-255");
  OUTLET_DOC(1, "miss", "Bang when no element matches.", "");
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
