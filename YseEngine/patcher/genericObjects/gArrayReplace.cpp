#include "gArrayReplace.h"
#include "../math/gExprEval.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

#include <cstddef>
#include <cstring>
#include <string>

using namespace YSE::PATCHER;

namespace {

  // Scan one token out of `value`: `begin`/`length` receive the token, and
  // the return is false when the message is empty or holds more than one
  // atom — gArrayFindBase's one-atom rule, hoisted because both value inlets
  // apply it.
  bool SingleAtom(const std::string& value, std::size_t& begin, std::size_t& length) {
    std::size_t start = 0;
    while (start < value.size() && IsSelectorSeparator(value[start]))
      start++;
    std::size_t end = start;
    while (end < value.size() && !IsSelectorSeparator(value[end]))
      end++;
    std::size_t tail = end;
    while (tail < value.size() && IsSelectorSeparator(value[tail]))
      tail++;
    if (start == end || tail != value.size()) return false;
    begin = start;
    length = end - start;
    return true;
  }

} // namespace

#define className gArrayReplace

gArrayReplace::gArrayReplace() : gArrayEndsBase() {
  // The find inlet, hot; the replacement inlet and the reference inlet, cold
  // — gArrayFill's arrangement: the ask on the left, configuration on the
  // right, with gArrayFindBase's stored-value semantics on the hot side.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);
  ADD_IN_2;
  REG_LIST_IN(ListIn);

  // The reference, always list text; the count, always an int. Sent right to
  // left: the count first, then the reference.
  ADD_OUT_LIST;
  ADD_OUT_INT;

  // After gArrayEndsBase's name — the second and third creation arguments.
  ADD_PARAM(findSeed);
  ADD_PARAM(replaceSeed);

  ADD_DESCRIPTION(
      "Replaces every occurrence of a value in a named array with another value. Sends how "
      "many elements were replaced, then the array's reference.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "find",
            "An int, float or word: replace every element spelled this way, and remember the "
            "value. A bang replaces with the remembered values, and so does the bound array's "
            "reference. A list of more than one item is refused.",
            "one atom, at most 64 characters");
  INLET_DOC(1, "replacement",
            "An int, float or word to write in place of each match. Stored for the next "
            "replace; nothing changes until the left inlet triggers.",
            "one atom, at most 64 characters");
  INLET_DOC(2, "array reference",
            "Accepts the bound array's reference \"array <name>\" and ignores it, so the "
            "array's reference outlet can be wired here. Anything else is refused.",
            "");
  OUTLET_DOC(0, "reference",
             "The array's reference \"array <name>\" after each replace, also when nothing "
             "matched. Sent after outlet 1. Nothing is sent when the object has no name.",
             "");
  OUTLET_DOC(1, "count",
             "How many elements were replaced; 0 when none matched. Sent before the reference.",
             "0-256");
  PARAM_DOC("name", "",
            "Name of the array to change: the .array with this name in the same patcher. Empty "
            "uses a private, empty array.",
            "any identifier");
  PARAM_DOC("find", "", "The value a bang replaces before one arrives on the left inlet.",
            "one atom, at most 64 characters");
  PARAM_DOC("replace", "",
            "The value written in place of each match. Until one is set here or on the middle "
            "inlet, every trigger is refused.",
            "one atom, at most 64 characters");
}

// A re-parse must not leave half of the previous configuration standing: both
// seeds drop with the name, and the base's hook rebinds and re-syncs the
// stored values through ParamsChanged. gArrayFindBase's arrangement, twice.
PARM_CLEAR() {
  findSeed.clear();
  replaceSeed.clear();
  gArrayEndsBase::ClearParams();
}

void gArrayReplace::ParamsChanged() {
  RefreshReference();

  // The live stored values follow the seed arguments. Control thread only,
  // but under the store's guard all the same: a message may be reading or
  // replacing them on another thread this very moment. A lost guard keeps
  // the previous values, counted. An absent argument means no value; an
  // over-long one cannot be an element at all, so it becomes no value too —
  // and is counted, where the absent case is simply the object's initial
  // state. gArrayFindBase's sync, once per seed.
  const arrayStoreGuard guard(store->busy);
  if (!guard.Held()) {
    Refuse();
    return;
  }
  if (findSeed.empty() || findSeed.size() > ELEMENT_CAPACITY) {
    if (findSeed.size() > ELEMENT_CAPACITY) Refuse();
    storedFindLength = 0;
    storedFind[0] = '\0';
  } else {
    std::memcpy(storedFind, findSeed.data(), findSeed.size());
    storedFind[findSeed.size()] = '\0';
    storedFindLength = findSeed.size();
  }
  if (replaceSeed.empty() || replaceSeed.size() > ELEMENT_CAPACITY) {
    if (replaceSeed.size() > ELEMENT_CAPACITY) Refuse();
    storedReplaceLength = 0;
    storedReplace[0] = '\0';
  } else {
    std::memcpy(storedReplace, replaceSeed.data(), replaceSeed.size());
    storedReplace[replaceSeed.size()] = '\0';
    storedReplaceLength = replaceSeed.size();
  }
}

void gArrayReplace::RefreshReference() {
  reference.clear();
  if (arrayName.empty()) return;
  reference.reserve(sizeof(kArrayReferenceWord) + arrayName.size());
  reference += kArrayReferenceWord;
  reference += ' ';
  reference += arrayName;
}

std::string gArrayReplace::FindValue() const {
  const arrayStoreGuard guard(store->busy);
  if (!guard.Held()) return std::string();
  return std::string(storedFind, storedFindLength);
}

std::string gArrayReplace::ReplaceValue() const {
  const arrayStoreGuard guard(store->busy);
  if (!guard.Held()) return std::string();
  return std::string(storedReplace, storedReplaceLength);
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  ReplaceStored(thread);
}

INT_IN(IntIn) {
  char text[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
  // ExprFormatValue cannot fail on an int, but the check keeps a formatting
  // failure counted rather than silently acting on an empty value.
  if (written <= 0) {
    Refuse();
    return;
  }
  if (inlet == 1) {
    StoreReplacement(text, static_cast<std::size_t>(written));
    return;
  }
  ReplaceWith(text, static_cast<std::size_t>(written), thread);
}

FLOAT_IN(FloatIn) {
  // A non-finite float has no spelling that reads back — ExprFormatValue
  // renders it as "0." — so matching or storing it would quietly become a
  // different value. Refused, with the negated range test so a NaN takes
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
  if (inlet == 1) {
    StoreReplacement(text, static_cast<std::size_t>(written));
    return;
  }
  ReplaceWith(text, static_cast<std::size_t>(written), thread);
}

LIST_IN(ListIn) {
  if (inlet == 2) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — the binding is the creation argument, resolved on the
    // control thread, so there is nothing to set. gDictSlice's inlet rule.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  if (inlet == 1) {
    // One atom is a replacement; more than one is refused whole — an element
    // is one atom. A reference is two atoms and lands with the rest of the
    // refusals: an identity is not an element, gArrayEndsWriter's rule.
    std::size_t begin = 0;
    std::size_t length = 0;
    if (!SingleAtom(value, begin, length) || length > ELEMENT_CAPACITY) {
      Refuse();
      return;
    }
    StoreReplacement(value.data() + begin, length);
    return;
  }

  // The array's reference replaces with the stored values — the message its
  // .array emits on a bang, so wiring that outlet here gives the family's
  // gesture. Only the bound name can be recognised at all — resolving an
  // unrecognised name means the registry's mutex, and this may be the audio
  // thread. No ambiguity with data: a find value is one atom and a reference
  // is two.
  if (ArrayReferenceNames(value, arrayName)) {
    ReplaceStored(thread);
    return;
  }

  // One atom is a find value; more than one is refused whole — an element is
  // one atom, so only one atom can be matched, and a sub-array match is a
  // match over a sequence of positions that renumbering writes would tear
  // (gArrayFindBase's rule). That covers a reference naming an array this
  // object is not bound to as well.
  std::size_t begin = 0;
  std::size_t length = 0;
  if (!SingleAtom(value, begin, length) || length > ELEMENT_CAPACITY) {
    Refuse();
    return;
  }
  ReplaceWith(value.data() + begin, length, thread);
}

// ─── the replace itself ───────────────────────────────────────────────────────

void gArrayReplace::ReplaceWith(const char* text, std::size_t length, YSE::THREAD thread) {
  std::size_t replaced = 0;
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      // Refused whole: neither the value nor a rewrite — the stored values
      // are guarded by this same hold, so a message that cannot take it
      // cannot move them either.
      Refuse();
      return;
    }
    if (storedReplaceLength == 0) {
      // No replacement to write — refused whole, and the find value is NOT
      // stored: a refused trigger must leave a later bang behaving as if it
      // never arrived. Malformed, not a miss.
      Refuse();
      return;
    }
    std::memcpy(storedFind, text, length);
    storedFind[length] = '\0';
    storedFindLength = length;
    replaced = ReplaceLocked();
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well write into this same array, and inside the guard that
  // write would be the one thing the try-lock drops.
  Announce(replaced, thread);
}

void gArrayReplace::ReplaceStored(YSE::THREAD thread) {
  std::size_t replaced = 0;
  bool missing = false;
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    if (storedFindLength == 0 || storedReplaceLength == 0) {
      // Neither value has to have arrived by inlet — the creation arguments
      // seed both — but a trigger while either is absent is malformed, not a
      // miss: a miss is a well-formed value the array happens not to hold,
      // where this ask holds nothing to look for or nothing to write.
      missing = true;
    } else {
      replaced = ReplaceLocked();
    }
  }

  if (missing) {
    Refuse();
    return;
  }
  Announce(replaced, thread);
}

void gArrayReplace::StoreReplacement(const char* text, std::size_t length) {
  // Silently — the cold half of the idiom: nothing is rewritten and nothing
  // leaves. Under the guard, because a trigger on another thread is reading
  // this buffer inside its own hold; a lost try-lock refuses rather than
  // tearing the value under a running replace.
  const arrayStoreGuard guard(store->busy);
  if (!guard.Held()) {
    Refuse();
    return;
  }
  std::memcpy(storedReplace, text, length);
  storedReplace[length] = '\0';
  storedReplaceLength = length;
}

std::size_t gArrayReplace::ReplaceLocked() {
  // The scan: every element spelling the find value is assigned the
  // replacement — EVERY occurrence, #802's decision, see the class notes. A
  // bounded loop of bounded compares and assigns into storage the store
  // reserved at construction; nothing renumbers, so no cursor and no
  // snapshot is needed — every element keeps its position, only its
  // spelling changes.
  std::size_t replaced = 0;
  for (std::size_t i = 0; i < store->count; i++) {
    const std::string& element = store->elements[i];
    if (element.size() != storedFindLength) continue;
    if (std::memcmp(element.data(), storedFind, storedFindLength) != 0) continue;
    store->elements[i].assign(storedReplace, storedReplaceLength);
    replaced++;
  }
  return replaced;
}

void gArrayReplace::Announce(std::size_t replaced, YSE::THREAD thread) {
  // The count first — right to left, so the number has arrived wherever it
  // is wired by the time the reference triggers the family downstream — and
  // count 0 is an answer, not a refusal: the ask was well-formed, the array
  // simply holds no such value. An unnamed object still counts (an answer
  // is a value, not an identity) but has no name to pass on — the rewrite
  // happened, the reference is simply empty.
  outputs[1].SendInt(static_cast<int>(replaced), thread);
  if (reference.empty()) return;
  outputs[0].SendList(reference, thread);
}
