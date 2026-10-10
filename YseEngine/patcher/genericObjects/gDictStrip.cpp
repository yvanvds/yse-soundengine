// `.dict.strip` (issue #780). See gDictStrip.h for the design; this file is
// one guarded back-to-front erase scan and one SendList.
#include "gDictStrip.h"

#include <cstring>
#include <string>

#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

using namespace YSE::PATCHER;
#define className gDictStrip

namespace {

  // The separator, and its length, without spelling `2` anywhere. gDict.cpp's
  // arrangement.
  constexpr std::size_t kSeparatorLength = sizeof(kDictPathSeparator) - 1;
  constexpr std::size_t kReferenceLength = sizeof(kDictReferenceWord) - 1;

  // Doc strings, hoisted out of CONSTRUCT() because they are long enough that
  // the constructor stops being readable with them inline — gDict's
  // arrangement.
  constexpr char kInletDoc[] =
      "A bang removes every entry under the path and sends the dictionary's reference. So does "
      "the dictionary's reference 'dictionary <name>'.";
  constexpr char kOutletDoc[] =
      "'dictionary <name>' after each strip. Silent for an unnamed dictionary.";
  constexpr char kNameDoc[] =
      "The name of the dictionary to edit. Empty strips an empty dictionary.";
  constexpr char kPathDoc[] =
      "The key path whose entries are removed: 'voice::1' removes every 'voice::1::...' entry. "
      "An entry at exactly the path is kept. Empty removes nothing.";

} // namespace

CONSTRUCT() {
  // One inlet, as in Max. No int or float handler: a bare number names no
  // dictionary — the family's rule.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);

  // The reference outlet.
  ADD_OUT_LIST;

  ADD_PARAM(dictName);
  ADD_PARAM(stripPath);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // A private, empty store to start with, so `store` is never null and no
  // message handler needs a null check. Rebind() trades it for a shared one
  // as soon as there is both a name and a patcher to prefix it with.
  Rebind();
  RefreshReference();

  ADD_DESCRIPTION("Removes every entry under a key path from a named dictionary, in place, then "
                  "sends its reference. An entry stored at exactly the path is kept.");
  ADD_CATEGORY(pCategory::DICT);
  INLET_DOC(0, "strip", kInletDoc, "");
  OUTLET_DOC(0, "dictionary reference", kOutletDoc, "");
  PARAM_DOC("name", "", kNameDoc, "any identifier");
  PARAM_DOC("path", "", kPathDoc, "any key path");
}

// ─── creation arguments ───────────────────────────────────────────────────────

// A re-parse must not leave half of the previous configuration standing:
// Parameters::Set() calls this before parsing and returns early on an empty
// argument string, so this is what makes SetParams("") a real reset —
// dropping both arguments and going back to a private dictionary. gDict's
// rule.
PARM_CLEAR() {
  dictName.clear();
  stripPath.clear();
  Rebind();
  RefreshReference();
}

PARM_PARSE() {
  Rebind();
  RefreshReference();
}

// `parent` is a patcherImplementation by construction (the patcher hands
// itself to every object via SetParent); the cast mirrors gDict's.
void gDictStrip::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  Rebind();
}

void gDictStrip::RefreshBinding() {
  Rebind();
}

void gDictStrip::Rebind() {
  // No name, or no patcher to prefix it with, means no address — and no
  // address means a private, empty dictionary. See gDict.h for why an
  // unnamed object does not pool on "patcher.<patcherName>.".
  auto* p = static_cast<patcherImplementation*>(parent);

  std::string address;
  if (!dictName.empty() && p != nullptr) address = p->ScopedAddress(dictName);

  // Unchanged binding: keep the store. A live SetParams that leaves the name
  // alone must not re-anchor, and neither must the second Rebind() a Set()
  // makes (clear, then parse).
  if (store != nullptr && address == boundAddress) return;
  bool created = false;
  store = address.empty() ? std::make_shared<dictStore>()
                          : AcquireNamedStore<dictStore>(address, created);
  boundAddress = address;
}

void gDictStrip::RefreshReference() {
  reference.clear();
  if (dictName.empty()) return;
  reference.reserve(kReferenceLength + 1 + dictName.size());
  reference += kDictReferenceWord;
  reference += ' ';
  reference += dictName;
}

// An entry is *under* the path when its key begins "<path>::" with at least
// one character after the separator — so an empty path matches nothing
// (there is no sub-tree above the root), a key that merely begins like the
// path ("voices" against "voice") is no match at all, and an entry stored at
// exactly the path is a leaf value, not a sub-tree, and survives.
// gDictSlice::UnderPath's rule, because a strip removes exactly what a slice
// of the same path takes.
bool gDictStrip::UnderPath(const dictEntry& entry) const {
  const std::size_t pathLength = stripPath.size();
  return pathLength != 0 && entry.key.size() > pathLength + kSeparatorLength &&
         std::memcmp(entry.key.data(), stripPath.data(), pathLength) == 0 &&
         std::memcmp(entry.key.data() + pathLength, kDictPathSeparator, kSeparatorLength) == 0;
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  Strip(thread);
}

LIST_IN(ListIn) {
  // The dictionary's reference triggers the strip — the message its .dict
  // emits on a bang. Anything else, including a reference to a dictionary
  // this object is not bound to, is refused: resolving an unrecognised name
  // means the registry's mutex, and this may be the audio thread.
  if (DictReferenceNames(value, dictName)) {
    Strip(thread);
    return;
  }
  Refuse();
}

void gDictStrip::Strip(YSE::THREAD thread) {
  // One dictionary, one guard — the two-guard problem .dict.slice's snapshot
  // solves does not arise, so the erase happens in place. Back to front,
  // because DictEraseAt closes the gap by moving the rows above the erased
  // position down: a forward cursor would step over the row that just slid
  // into its place, and every row below the cursor stays where it was.
  // DictEraseAt moves rows by `assign` into storage they already own — no
  // allocation. A guard another thread holds refuses the whole strip,
  // counted, so the dictionary is never left half-stripped.
  {
    const dictStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    for (std::size_t i = store->count; i > 0; i--) {
      if (UnderPath(store->entries[i - 1])) DictEraseAt(*store, i - 1);
    }
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well store into this same dictionary. Silent for an unnamed
  // dictionary, which has no name to pass on — gDict's rule for an unnamed
  // reference.
  if (!reference.empty()) outputs[0].SendList(reference, thread);
}
