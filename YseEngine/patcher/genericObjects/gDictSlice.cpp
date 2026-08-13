#include "gDictSlice.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

#include <cstring>
#include <string>

using namespace YSE::PATCHER;
#define className gDictSlice

namespace {

  // The separator, and its length, without spelling `2` anywhere. gDict.cpp's
  // arrangement.
  constexpr std::size_t kSeparatorLength = sizeof(kDictPathSeparator) - 1;
  constexpr std::size_t kReferenceLength = sizeof(kDictReferenceWord) - 1;

  // Doc strings, hoisted out of CONSTRUCT() because they are long enough that
  // the constructor stops being readable with them inline — gDict's
  // arrangement.
  constexpr char kSourceInletDoc[] =
      "A bang splits the source dictionary at the path: entries under it go into the slice "
      "target with the prefix stripped, everything else into the remainder target, and each "
      "target's reference leaves its outlet, right to left. \"dictionary <name>\" does the same "
      "when it names the source dictionary bound by the first creation argument — the message a "
      ".dict's reference outlet emits on a bang, so wiring that outlet here gives Max's own "
      "gesture. A reference naming anything else, or any other message, is refused and counted "
      "rather than logged, since this inlet may be the audio thread.";
  constexpr char kSliceInletDoc[] =
      "\"dictionary <name>\" is accepted silently when it names the slice target bound by the "
      "second creation argument, so a patch may wire the target's reference outlet across. It "
      "sets nothing — the binding is the creation argument, resolved on the control thread — "
      "and anything else is refused and counted.";
  constexpr char kRemainderInletDoc[] =
      "\"dictionary <name>\" is accepted silently when it names the remainder target bound by "
      "the third creation argument, so a patch may wire the target's reference outlet across. "
      "It sets nothing — the binding is the creation argument, resolved on the control thread — "
      "and anything else is refused and counted.";
  constexpr char kSliceOutletDoc[] =
      "\"dictionary <slice>\" after a split — the reference the dict.* family binds, carrying "
      "the local name so the receiving object prefixes it with its own patcher's name. Sent "
      "after the remainder outlet, Max's right-to-left order. Silent for an unnamed slice "
      "target, which has no name to pass on.";
  constexpr char kRemainderOutletDoc[] =
      "\"dictionary <remainder>\" after a split, sent first — Max's right-to-left order. Silent "
      "for an unnamed remainder target, which has no name to pass on.";

} // namespace

CONSTRUCT() {
  // Three inlets: the left one triggers, the other two only acknowledge the
  // reference each is already bound to. gDictGroup's shape, with one
  // acknowledging inlet per bound target.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);
  ADD_IN_2;
  REG_LIST_IN(ListIn);

  // The slice outlet, then the remainder outlet — sent right to left, so the
  // remainder is announced before the slice, Max's universal order.
  ADD_OUT_LIST;
  ADD_OUT_LIST;

  ADD_PARAM(sourceName);
  ADD_PARAM(sliceName);
  ADD_PARAM(remainderName);
  ADD_PARAM(slicePath);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // Private, empty stores to start with, so no pointer is ever null and no
  // message handler needs a null check. Rebind() trades each for a shared
  // one as soon as there is both a name and a patcher to prefix it with.
  Rebind();
  RefreshReferences();

  ADD_DESCRIPTION(
      "Splits a dictionary at a key path — Max's dict.slice on the name-addressed value model "
      ".dict settled: all three dictionaries are bound from the creation arguments, "
      "\".dict.slice <source> <slice> <remainder> [<path>]\", because a dictionary is addressed "
      "by name and never passed down a cord. A bang, or the source dictionary's reference "
      "message \"dictionary <name>\", partitions the source: every entry under the path — its "
      "key begins \"<path>::\" — replaces the slice target with the prefix stripped, so the "
      "sub-tree becomes a dictionary rooted at itself, and every other entry (including a leaf "
      "stored at exactly the path, which is a value, not a sub-tree) replaces the remainder "
      "target unchanged. Each target's reference then leaves its outlet, remainder first — "
      "Max's right-to-left order. An empty path matches nothing, so the slice comes out empty "
      "and the remainder is the whole source.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "slice", kSourceInletDoc, "");
  INLET_DOC(1, "slice reference", kSliceInletDoc, "");
  INLET_DOC(2, "remainder reference", kRemainderInletDoc, "");
  OUTLET_DOC(0, "slice reference", kSliceOutletDoc, "");
  OUTLET_DOC(1, "remainder reference", kRemainderOutletDoc, "");
  PARAM_DOC("source", "",
            "The source dictionary's shared name, addressed as \"<patcherName>.<name>\" — the "
            "dictionary a .dict of the same name in this patcher holds. Resolved once, on the "
            "control thread, which is why no message re-points it at run time. Empty splits a "
            "private, empty dictionary.",
            "any identifier");
  PARAM_DOC("slice", "",
            "The slice target's shared name, bound exactly as the source. A split replaces its "
            "contents whole with the entries under the path, prefix stripped. The result goes "
            "into a bound dictionary rather than a new anonymous one because there is no way to "
            "hand a fresh dictionary's identity down a cord.",
            "any identifier");
  PARAM_DOC("remainder", "",
            "The remainder target's shared name, bound exactly as the source. A split replaces "
            "its contents whole with every entry not under the path, keys unchanged — the other "
            "half of the partition, what .dict.strip would leave behind.",
            "any identifier");
  PARAM_DOC("path", "",
            "The key path the source is split at: \"voice::1\" slices every \"voice::1::...\" "
            "entry into the slice target under the \"...\" left after the prefix is stripped. A "
            "leaf stored at exactly the path is a value, not a sub-tree, and stays in the "
            "remainder. Empty matches nothing — there is no sub-tree above the root.",
            "any key path");
}

// A re-parse must not leave half of the previous configuration standing:
// Parameters::Set() calls this before parsing and returns early on an empty
// argument string, so this is what makes SetParams("") a real reset —
// dropping all four arguments and going back to three private dictionaries.
// gDict's rule.
PARM_CLEAR() {
  sourceName.clear();
  sliceName.clear();
  remainderName.clear();
  slicePath.clear();
  Rebind();
  RefreshReferences();
}

PARM_PARSE() {
  Rebind();
  RefreshReferences();
}

// `parent` is a patcherImplementation by construction (the patcher hands
// itself to every object via SetParent); the cast mirrors gDict's.
void gDictSlice::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  Rebind();
}

void gDictSlice::RefreshBinding() {
  Rebind();
}

void gDictSlice::Rebind() {
  // No name, or no patcher to prefix it with, means no address — and no
  // address means a private, empty dictionary on that side. See gDict.h for
  // why an unnamed side does not pool on "<patcherName>.".
  auto* p = static_cast<patcherImplementation*>(parent);

  std::string sourceAddress;
  if (!sourceName.empty() && p != nullptr) sourceAddress = p->Name() + "." + sourceName;
  std::string sliceAddress;
  if (!sliceName.empty() && p != nullptr) sliceAddress = p->Name() + "." + sliceName;
  std::string remainderAddress;
  if (!remainderName.empty() && p != nullptr) remainderAddress = p->Name() + "." + remainderName;

  // Unchanged binding: keep the store. A live SetParams that leaves a name
  // alone must not re-anchor that side, and neither must the second Rebind()
  // a Set() makes (clear, then parse).
  bool created = false;
  if (sourceStore == nullptr || sourceAddress != boundSourceAddress) {
    sourceStore = sourceAddress.empty() ? std::make_shared<dictStore>()
                                        : AcquireNamedStore<dictStore>(sourceAddress, created);
    boundSourceAddress = sourceAddress;
  }
  if (sliceStore == nullptr || sliceAddress != boundSliceAddress) {
    sliceStore = sliceAddress.empty() ? std::make_shared<dictStore>()
                                      : AcquireNamedStore<dictStore>(sliceAddress, created);
    boundSliceAddress = sliceAddress;
  }
  if (remainderStore == nullptr || remainderAddress != boundRemainderAddress) {
    remainderStore = remainderAddress.empty()
                         ? std::make_shared<dictStore>()
                         : AcquireNamedStore<dictStore>(remainderAddress, created);
    boundRemainderAddress = remainderAddress;
  }
}

void gDictSlice::RefreshReferences() {
  sliceReference.clear();
  if (!sliceName.empty()) {
    sliceReference.reserve(kReferenceLength + 1 + sliceName.size());
    sliceReference += kDictReferenceWord;
    sliceReference += ' ';
    sliceReference += sliceName;
  }
  remainderReference.clear();
  if (!remainderName.empty()) {
    remainderReference.reserve(kReferenceLength + 1 + remainderName.size());
    remainderReference += kDictReferenceWord;
    remainderReference += ' ';
    remainderReference += remainderName;
  }
}

// An entry is *under* the path when its key begins "<path>::" with at least
// one character after the separator — so an empty path matches nothing
// (there is no sub-tree above the root), a key that merely begins like the
// path ("voicecard" against "voice") is no match at all, and a key ending at
// the separator ("voice::1::") roots no sub-tree entry and stays in the
// remainder.
bool gDictSlice::UnderPath(const dictEntry& entry) const {
  const std::size_t pathLength = slicePath.size();
  return pathLength != 0 && entry.key.size() > pathLength + kSeparatorLength &&
         std::memcmp(entry.key.data(), slicePath.data(), pathLength) == 0 &&
         std::memcmp(entry.key.data() + pathLength, kDictPathSeparator, kSeparatorLength) == 0;
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  Slice(thread);
}

LIST_IN(ListIn) {
  if (inlet == 0) {
    // The source dictionary's reference triggers the split — the message its
    // .dict emits on a bang. Anything else, including a reference to a
    // dictionary this object is not bound to, is refused: resolving an
    // unrecognised name means the registry's mutex, and this may be the
    // audio thread.
    if (DictReferenceNames(value, sourceName)) {
      Slice(thread);
      return;
    }
    Refuse();
    return;
  }

  // Inlets 1 and 2: each target's reference is acknowledged and nothing more
  // — the binding is the creation argument, resolved on the control thread,
  // so there is nothing to set.
  if (DictReferenceNames(value, inlet == 1 ? sliceName : remainderName)) return;
  Refuse();
}

void gDictSlice::Slice(YSE::THREAD thread) {
  // Copy the source dictionary out under its guard, so no target store's
  // guard is ever nested inside it: two guards at once would put a
  // lock-ordering obligation on every pair of objects naming the same two
  // dictionaries, and the degenerate bindings (the source as its own slice
  // or remainder) would trip over their own try-lock. Bounded assigns into
  // rows reserved at construction — no allocation.
  {
    const dictStoreGuard guard(sourceStore->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    snapshot.count = sourceStore->count;
    for (std::size_t i = 0; i < sourceStore->count; i++) {
      snapshot.entries[i].key.assign(sourceStore->entries[i].key);
      snapshot.entries[i].value.assign(sourceStore->entries[i].value);
    }
  }

  // The slice half: entries under the path, the prefix and its separator
  // stripped — pointer arithmetic into keys the snapshot already owns, so
  // nothing here allocates. A split replaces the target whole — re-running
  // it is idempotent, and a stale entry from the last run cannot survive
  // into this one. The rows keep their reserved storage; only the live
  // count is dropped.
  const std::size_t skip = slicePath.size() + kSeparatorLength;
  {
    const dictStoreGuard guard(sliceStore->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    for (std::size_t i = 0; i < sliceStore->count; i++) {
      sliceStore->entries[i].key.clear();
      sliceStore->entries[i].value.clear();
    }
    sliceStore->count = 0;

    for (std::size_t i = 0; i < snapshot.count; i++) {
      const dictEntry& entry = snapshot.entries[i];
      if (!UnderPath(entry)) continue;
      // Stripping never lengthens a key, the partition never grows the entry
      // count, and UnderPath guarantees a non-empty stripped path — so this
      // store cannot actually refuse. The branch keeps the store's contract
      // visible (counted) rather than assumed.
      if (!DictStoreAt(*sliceStore, entry.key.data() + skip, entry.key.size() - skip,
                       entry.value.data(), entry.value.size())) {
        Refuse();
      }
    }
  }

  // The remainder half: everything else, keys unchanged — including an
  // entry stored at exactly the path, which is a leaf value, not a
  // sub-tree. When both targets bind one name this second write replaces
  // the first, the remainder winning; the snapshot design keeps even that
  // case guard-safe.
  {
    const dictStoreGuard guard(remainderStore->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    for (std::size_t i = 0; i < remainderStore->count; i++) {
      remainderStore->entries[i].key.clear();
      remainderStore->entries[i].value.clear();
    }
    remainderStore->count = 0;

    for (std::size_t i = 0; i < snapshot.count; i++) {
      const dictEntry& entry = snapshot.entries[i];
      if (UnderPath(entry)) continue;
      // Unchanged keys into a table no fuller than the source's: this store
      // cannot actually refuse either, and the branch is the same kept
      // contract.
      if (!DictStoreAt(*remainderStore, entry.key.data(), entry.key.size(), entry.value.data(),
                       entry.value.size())) {
        Refuse();
      }
    }
  }

  // Outside every guard on purpose: a send runs the whole downstream graph,
  // which may well store into any of these dictionaries. Remainder before
  // slice — Max's universal right-to-left outlet order — and silent per
  // outlet for an unnamed target, which has no name to pass on. gDict's
  // rule for an unnamed reference.
  if (!remainderReference.empty()) outputs[1].SendList(remainderReference, thread);
  if (!sliceReference.empty()) outputs[0].SendList(sliceReference, thread);
}
