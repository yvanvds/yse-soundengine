#include "gArrayIndexMap.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

#include <cstddef>
#include <string>

using namespace YSE::PATCHER;
#define className gArrayIndexMap

namespace {

  // Reads an index argument out of list text. False when there is no integer
  // there or when it is negative: an index is a position, never a count from
  // the end — arrayStore's rule, decided once for the family. gArrayAt's
  // reader; not shared, for the reason gArray.cpp's ElementToJson gives —
  // exporting a file-local helper out of a shipped object costs more than the
  // repetition.
  bool ReadIndex(const std::string& text, std::size_t& offset, std::size_t& out) {
    int value = 0;
    if (!ReadIntArgAt(text, offset, value)) return false;
    if (value < 0) return false;
    out = static_cast<std::size_t>(value);
    return true;
  }

  // True when nothing but whitespace is left from `offset` on.
  bool AtEnd(const std::string& text, std::size_t offset) {
    for (std::size_t i = offset; i < text.size(); i++) {
      if (!IsSelectorSeparator(text[i])) return false;
    }
    return true;
  }

  // Doc strings, hoisted out of the constructor because they are long enough
  // that it stops being readable with them inline — gArray's arrangement.
  constexpr char kTriggerInletDoc[] =
      "A bang applies the stored map, and so does the bound array's reference. A list of "
      "positions, or a single int, is applied at once and not stored. A float is truncated; a "
      "negative position refuses the whole list.";
  constexpr char kMapInletDoc[] =
      "A list of positions, or a single int, to store for the next bang. A float is truncated. "
      "A list with a negative position, or more than 256 entries, is refused.";
  constexpr char kReferenceInletDoc[] =
      "Accepts the bound array's reference \"array <name>\" and ignores it, so the array's "
      "reference outlet can be wired here. Anything else is refused.";
  constexpr char kOutletDoc[] =
      "The array's reference \"array <name>\" after each reorder, so further .array objects can "
      "be chained. Nothing is sent when the object has no name.";

} // namespace

gArrayIndexMap::gArrayIndexMap() : gArrayEndsBase() {
  // The trigger inlet, hot; the map inlet and the reference inlet, cold —
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
  REG_LIST_IN(ListIn);
  ADD_IN_2;
  REG_LIST_IN(ListIn);

  // The reference, always list text.
  ADD_OUT_LIST;

  // After gArrayEndsBase's name — the LIST parameter last, absorbing the
  // remaining tokens as the seed map.
  ADD_PARAM(seedMap);

  ADD_DESCRIPTION(
      "Reorders a named array by a list of positions: the new array holds the elements at "
      "those positions, in that order. Positions may repeat or be left out.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "0-255 each");
  INLET_DOC(1, "index map", kMapInletDoc, "0-255 each");
  INLET_DOC(2, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "reference", kOutletDoc, "");
  PARAM_DOC("name", "",
            "Name of the array to reorder: the .array with this name in the same patcher. "
            "Empty uses a private, empty array.",
            "any identifier");
  PARAM_DOC("map", "",
            "The positions a bang applies before a map arrives on the middle inlet. Without a "
            "map, a bang is refused.",
            "0-255 each");
}

// A re-parse must not leave half of the previous configuration standing: the
// seed drops with the name, and the base's hook rebinds and re-syncs the
// stored map through ParamsChanged. gArrayFindBase's arrangement.
void gArrayIndexMap::ClearParams() {
  seedMap.clear();
  gArrayEndsBase::ClearParams();
}

void gArrayIndexMap::ParamsChanged() {
  RefreshReference();

  // The live stored map follows the seed arguments. Parsed into a local
  // first — control thread only, so the stack afford it — then committed
  // under the store's guard all the same: a message may be reading or
  // replacing the map on another thread this very moment. A lost guard
  // keeps the previous map, counted. gArrayFindBase's rule for its stored
  // value.
  std::size_t parsed[MAX_INDICES];
  std::size_t count = 0;
  bool malformed = false;
  for (const std::string& token : seedMap) {
    // Skip the empty tokens Parameters::Set leaves behind for a run of
    // spaces.
    if (token.empty()) continue;
    std::size_t offset = 0;
    if (count >= MAX_INDICES || !ReadIndex(token, offset, parsed[count]) || !AtEnd(token, offset)) {
      malformed = true;
      break;
    }
    count++;
  }

  const arrayStoreGuard guard(store->busy);
  if (!guard.Held()) {
    Refuse();
    return;
  }
  if (malformed) {
    // A token that is not a non-negative integer cannot be an index at all,
    // so the whole argument becomes "no map" — counted, where the absent
    // case is simply the object's initial state. Whole-or-nothing, the
    // family's rule for a compound that cannot be taken entirely.
    Refuse();
    storedCount = 0;
    return;
  }
  for (std::size_t i = 0; i < count; i++)
    storedMap[i] = parsed[i];
  storedCount = count;
}

void gArrayIndexMap::RefreshReference() {
  reference.clear();
  if (arrayName.empty()) return;
  reference.reserve(sizeof(kArrayReferenceWord) + arrayName.size());
  reference += kArrayReferenceWord;
  reference += ' ';
  reference += arrayName;
}

std::size_t gArrayIndexMap::MapSize() const {
  const arrayStoreGuard guard(store->busy);
  if (!guard.Held()) return 0;
  return storedCount;
}

int gArrayIndexMap::MapAt(std::size_t index) const {
  const arrayStoreGuard guard(store->busy);
  if (!guard.Held() || index >= storedCount) return -1;
  return static_cast<int>(storedMap[index]);
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  ApplyStored(thread);
}

INT_IN(IntIn) {
  // Negative is refused whole — malformed everywhere in the family — and on
  // the map inlet the stored map does not move either, so a bang after the
  // refusal applies what it would have applied before it.
  if (value < 0) {
    Refuse();
    return;
  }
  const std::size_t position = static_cast<std::size_t>(value);
  if (inlet == 1) {
    StoreMap(&position, 1);
    return;
  }
  // A single int on the trigger is the one-entry map it spells, applied at
  // the moment it arrives — kept equivalent to the one-atom list, so a
  // map-producing outlet that sends its single entry as an int still lands.
  ApplyMap(&position, 1, thread);
}

FLOAT_IN(FloatIn) {
  // Max's float method on an int attribute is "convert to int", .table's
  // precedent. The range test keeps a NaN or an infinity — which ExprToInt
  // folds to 0 — from quietly becoming index 0.
  if (!(value >= 0.f && value < 2147483648.f)) {
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

  if (inlet == 0 && ArrayReferenceNames(value, arrayName)) {
    // The array's reference applies the stored map — the message its .array
    // emits on a bang, so wiring that outlet here gives the family's
    // gesture: bang the array, out comes the reordered array's reference.
    // (On the map inlet the same words are not a map: they fail the index
    // parse below and are refused — an identity is not data.)
    ApplyStored(thread);
    return;
  }

  // A list of indices, taken whole or refused whole — nothing stored,
  // nothing applied — when anything in it is not a non-negative integer or
  // there are more entries than a map may hold. That covers a reference
  // naming an array this object is not bound to as well: resolving an
  // unrecognised name means the registry's mutex, and this may be the audio
  // thread.
  std::size_t count = 0;
  std::size_t offset = 0;
  while (!AtEnd(value, offset)) {
    if (count >= MAX_INDICES || !ReadIndex(value, offset, requested[count])) {
      Refuse();
      return;
    }
    count++;
  }
  if (count == 0) {
    Refuse();
    return;
  }
  if (inlet == 1) {
    StoreMap(requested, count);
    return;
  }
  ApplyMap(requested, count, thread);
}

// ─── the reorder ──────────────────────────────────────────────────────────────

void gArrayIndexMap::StoreMap(const std::size_t* positions, std::size_t count) {
  // Under the store's guard, as every access to the stored map is: the
  // apply that reads it is already inside a hold, so there is no second
  // flag to order against. A lost guard refuses the message whole and the
  // map does not move — gArrayFindBase's rule for its stored value.
  const arrayStoreGuard guard(store->busy);
  if (!guard.Held()) {
    Refuse();
    return;
  }
  for (std::size_t i = 0; i < count; i++)
    storedMap[i] = positions[i];
  storedCount = count;
}

void gArrayIndexMap::ApplyStored(YSE::THREAD thread) {
  bool missing = false;
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    if (storedCount == 0) {
      // No map has ever arrived and no argument seeded one — malformed, not
      // a reorder to nothing: gArrayFindBase's rule for a bang before any
      // value. .array's own "clear" is the object that empties on purpose.
      missing = true;
    } else {
      ReorderLocked(storedMap, storedCount);
    }
  }
  if (missing) {
    Refuse();
    return;
  }
  Announce(thread);
}

void gArrayIndexMap::ApplyMap(const std::size_t* positions, std::size_t count, YSE::THREAD thread) {
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    ReorderLocked(positions, count);
  }
  Announce(thread);
}

void gArrayIndexMap::ReorderLocked(const std::size_t* positions, std::size_t count) {
  // The first pass: copy every pick out, in map order, into the scratch
  // table this object owns — the source and the destination are the same
  // table, so picking straight into the store would read elements a
  // previous pick already overwrote. An index naming no element contributes
  // nothing (AssignOrder's rule, #787's spec), so the result is as long as
  // what landed. Bounded assigns into storage both tables reserved at
  // construction — nothing here allocates.
  std::size_t landed = 0;
  for (std::size_t i = 0; i < count; i++) {
    if (positions[i] >= store->count) continue;
    scratch.elements[landed++].assign(store->elements[positions[i]]);
  }

  // The second pass: the scratch table becomes the array, and the slots the
  // shorter result vacates are cleared behind the new count — ArrayEraseAt's
  // hygiene.
  for (std::size_t i = 0; i < landed; i++)
    store->elements[i].assign(scratch.elements[i]);
  for (std::size_t i = landed; i < store->count; i++)
    store->elements[i].clear();
  store->count = landed;
}

void gArrayIndexMap::Announce(YSE::THREAD thread) {
  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well act on this same array, and inside the guard that access
  // would be the one thing the try-lock drops. An unnamed object has no
  // name to pass on — the reorder happened, the announcement is simply
  // empty.
  if (reference.empty()) return;
  outputs[0].SendList(reference, thread);
}
