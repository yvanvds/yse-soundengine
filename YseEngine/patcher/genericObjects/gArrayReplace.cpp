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
      "Replaces occurrences of a value — Max's array.replace on the name-addressed value model "
      ".array settled: the array is bound from the creation argument, \".array.replace <name> "
      "[<find>] [<replace>]\", because an array is addressed by name and never passed down a "
      "cord. EVERY occurrence is replaced — the use case is retuning every occurrence of one "
      "pitch, and first-only is already .array.indexof into .array's own set — and equality is "
      "the spelling: an int 7 matches the element \"7\", a float 7. matches \"7.\", and the two "
      "are different elements. An int, a float or a symbol on the find inlet stores the find "
      "value and replaces now; a bang re-replaces with the stored values, and the message an "
      ".array's reference outlet emits on a bang does the same — the family's gesture. The "
      "replacement inlet stores its value silently, the cold half of the idiom. The number of "
      "elements rewritten leaves the count outlet before the reference — right to left — which "
      "is the only way a patch can tell replaced-nothing from replaced-everything: a replace "
      "that matched nothing still landed and announces with count 0, where a refused one (a "
      "lost try-lock, a trigger before both values exist) emits nothing. The whole replace is "
      "one hold of the store's guard, and nothing renumbers: every element keeps its position, "
      "only its spelling changes.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "find",
            "An int, a float or a symbol stores the find value and replaces every occurrence of "
            "it now; a bang re-replaces with the stored find and replacement. Equality is the "
            "spelling — 7 matches \"7\", 7. matches \"7.\", and the two are different elements — "
            "and the match is case-sensitive, as every comparison in this patcher is. \"array "
            "<name>\" replaces with the stored values when it names the array bound by the "
            "creation argument — the message an .array's reference outlet emits on a bang, the "
            "family's gesture. A trigger before both values exist is refused whole and stores "
            "nothing: malformed, not a miss. A multi-atom list, a non-finite float, an atom "
            "past 64 characters, or a reference naming anything else is refused and counted "
            "rather than logged, since this inlet may be the audio thread.",
            "one atom, at most 64 characters");
  INLET_DOC(1, "replacement",
            "An int, a float or a symbol stores the replacement, silently — the cold half of "
            "the idiom, .array.fill's count inlet with an atom where the number was: nothing is "
            "rewritten until the find inlet triggers. One atom, at most 64 characters; a "
            "non-finite float is refused (it has no spelling that reads back), and a reference "
            "is two atoms and refused with every other multi-atom list — an identity is not an "
            "element.",
            "one atom, at most 64 characters");
  INLET_DOC(2, "array reference",
            "\"array <name>\" is accepted silently when it names the array bound by the "
            "creation argument, so a patch may wire the array's reference outlet across. It "
            "sets nothing — the binding is the creation argument, resolved on the control "
            "thread — and anything else is refused and counted.",
            "");
  OUTLET_DOC(0, "reference",
             "The bound array's reference, \"array <name>\", sent after a replace that landed — "
             "the way an array leaves an object on the value model, so wiring it onward chains "
             "the family: into .array.length it proves a replace never resizes. Sent after the "
             "count outlet, Max's outlets firing right to left. A refused replace emits "
             "nothing, and an unnamed object stays silent — the rewrite happens, but there is "
             "no name to pass on.",
             "");
  OUTLET_DOC(1, "count",
             "How many elements were rewritten, sent first — right to left — because it is the "
             "only way a patch can tell replaced-nothing from replaced-everything: a replace "
             "that matched nothing still landed and reports 0, where a refused one reports "
             "nothing at all. Replacing a value with itself lands too, and counts its "
             "occurrences. An unnamed (private) array counts as well — an answer is a value, "
             "not an identity.",
             "0-256");
  PARAM_DOC("name", "",
            "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an "
            ".array of the same name in this patcher holds. Resolved once, on the control "
            "thread, which is why no message re-points it at run time. Empty rewrites a "
            "private, empty array: the replace lands with count 0 and no reference leaves.",
            "any identifier");
  PARAM_DOC("find", "",
            "The initial stored find value — what a bang replaces before any value has arrived "
            "on the find inlet. One atom, exactly as every value the inlet takes; absent means "
            "no value, and a trigger before one arrives is refused.",
            "one atom, at most 64 characters");
  PARAM_DOC("replace", "",
            "The initial stored replacement — what occurrences of the find value become. One "
            "atom, at most 64 characters; absent means no value, and a trigger before one "
            "arrives is refused: a replace with nothing to write is malformed, where erasing "
            "is .array's own delete.",
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
