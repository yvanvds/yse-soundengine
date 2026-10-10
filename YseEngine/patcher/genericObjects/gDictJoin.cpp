#include "gDictJoin.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

#include <cstring>
#include <string>

using namespace YSE::PATCHER;
#define className gDictJoin

namespace {

  constexpr std::size_t kReferenceLength = sizeof(kDictReferenceWord) - 1;

  // Doc strings, hoisted out of CONSTRUCT() because they are long enough that
  // the constructor stops being readable with them inline — gDict's
  // arrangement.
  constexpr char kLeftInletDoc[] =
      "A bang joins the two dictionaries into the target and sends the target's reference. So "
      "does the left dictionary's reference 'dictionary <name>'.";
  constexpr char kRightInletDoc[] =
      "Accepts the right dictionary's reference and does nothing with it, so its .dict can be "
      "wired here.";
  constexpr char kOutletDoc[] = "'dictionary <target>' after each join. Silent for an unnamed "
                                "target.";

} // namespace

CONSTRUCT() {
  // Two inlets, as in Max: the left one triggers, the right one only
  // acknowledges the reference it is already bound to. gDictCompare's shape.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  ADD_OUT_LIST;

  ADD_PARAM(leftName);
  ADD_PARAM(rightName);
  ADD_PARAM(targetName);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // Private, empty stores to start with, so no pointer is ever null and no
  // message handler needs a null check. Rebind() trades each for a shared
  // one as soon as there is both a name and a patcher to prefix it with.
  Rebind();
  RefreshReference();

  ADD_DESCRIPTION("Merges two named dictionaries into a target dictionary: the left one's entries, "
                  "overlaid with the right one's. Where both hold a key path, the right value "
                  "wins.");
  ADD_CATEGORY(pCategory::DICT);
  INLET_DOC(0, "join", kLeftInletDoc, "");
  INLET_DOC(1, "right reference", kRightInletDoc, "");
  OUTLET_DOC(0, "reference", kOutletDoc, "");
  PARAM_DOC("left", "", "The name of the base dictionary. Empty joins an empty dictionary.",
            "any identifier");
  PARAM_DOC("right", "",
            "The name of the dictionary laid over the base: its value wins on a shared key path. "
            "Empty joins an empty dictionary.",
            "any identifier");
  PARAM_DOC("target", "",
            "The name of the dictionary that receives the result. Each join replaces its "
            "contents. It may be one of the two sources.",
            "any identifier");
}

// A re-parse must not leave half of the previous configuration standing:
// Parameters::Set() calls this before parsing and returns early on an empty
// argument string, so this is what makes SetParams("") a real reset — dropping
// all three names and going back to private dictionaries. gDict's rule.
PARM_CLEAR() {
  leftName.clear();
  rightName.clear();
  targetName.clear();
  Rebind();
  RefreshReference();
}

PARM_PARSE() {
  Rebind();
  RefreshReference();
}

// `parent` is a patcherImplementation by construction (the patcher hands
// itself to every object via SetParent); the cast mirrors gDict's.
void gDictJoin::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  Rebind();
}

void gDictJoin::RefreshBinding() {
  Rebind();
}

void gDictJoin::Rebind() {
  // No name, or no patcher to prefix it with, means no address — and no
  // address means a private, empty dictionary on that side. See gDict.h for
  // why an unnamed side does not pool on "patcher.<patcherName>.".
  auto* p = static_cast<patcherImplementation*>(parent);

  std::string leftAddress;
  if (!leftName.empty() && p != nullptr) leftAddress = p->ScopedAddress(leftName);
  std::string rightAddress;
  if (!rightName.empty() && p != nullptr) rightAddress = p->ScopedAddress(rightName);
  std::string targetAddress;
  if (!targetName.empty() && p != nullptr) targetAddress = p->ScopedAddress(targetName);

  // Unchanged binding: keep the store. A live SetParams that leaves a name
  // alone must not re-anchor that side, and neither must the second Rebind()
  // a Set() makes (clear, then parse).
  bool created = false;
  if (leftStore == nullptr || leftAddress != boundLeftAddress) {
    leftStore = leftAddress.empty() ? std::make_shared<dictStore>()
                                    : AcquireNamedStore<dictStore>(leftAddress, created);
    boundLeftAddress = leftAddress;
  }
  if (rightStore == nullptr || rightAddress != boundRightAddress) {
    rightStore = rightAddress.empty() ? std::make_shared<dictStore>()
                                      : AcquireNamedStore<dictStore>(rightAddress, created);
    boundRightAddress = rightAddress;
  }
  if (targetStore == nullptr || targetAddress != boundTargetAddress) {
    targetStore = targetAddress.empty() ? std::make_shared<dictStore>()
                                        : AcquireNamedStore<dictStore>(targetAddress, created);
    boundTargetAddress = targetAddress;
  }
}

void gDictJoin::RefreshReference() {
  reference.clear();
  if (targetName.empty()) return;
  reference.reserve(kReferenceLength + 1 + targetName.size());
  reference += kDictReferenceWord;
  reference += ' ';
  reference += targetName;
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  Join(thread);
}

LIST_IN(ListIn) {
  if (inlet == 0) {
    // The left dictionary's reference triggers the join — the message its
    // .dict emits on a bang. Anything else, including a reference to a
    // dictionary this object is not bound to, is refused: resolving an
    // unrecognised name means the registry's mutex, and this may be the
    // audio thread.
    if (DictReferenceNames(value, leftName)) {
      Join(thread);
      return;
    }
    Refuse();
    return;
  }

  // Inlet 1: the right dictionary's reference is acknowledged and nothing
  // more — the binding is the creation argument, resolved on the control
  // thread, so there is nothing to set.
  if (DictReferenceNames(value, rightName)) return;
  Refuse();
}

void gDictJoin::Join(YSE::THREAD thread) {
  // The join is assembled in the object's own merge buffer, one source guard
  // at a time, so no store's guard is ever nested inside another's: two
  // guards at once would put a lock-ordering obligation on every pair of
  // objects naming the same dictionaries, and any aliasing among the three
  // names would trip over its own try-lock. Bounded assigns into rows
  // reserved at construction — no allocation.
  //
  // The left dictionary first, copied whole: the base of the join.
  {
    const dictStoreGuard guard(leftStore->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    merged.count = leftStore->count;
    for (std::size_t i = 0; i < leftStore->count; i++) {
      merged.entries[i].key.assign(leftStore->entries[i].key);
      merged.entries[i].value.assign(leftStore->entries[i].value);
    }
  }

  // Then the right dictionary, merged over it: DictStoreAt replaces an entry
  // of the same key path, which is exactly "the right overwrites the left".
  // Two full dictionaries can join to more than MAX_ENTRIES — an entry past
  // the bound is refused whole and counted, never truncated, and the rest of
  // the join still lands: the partial merge the docs promise.
  {
    const dictStoreGuard guard(rightStore->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    for (std::size_t i = 0; i < rightStore->count; i++) {
      const dictEntry& entry = rightStore->entries[i];
      if (!DictStoreAt(merged, entry.key.data(), entry.key.size(), entry.value.data(),
                       entry.value.size())) {
        Refuse();
      }
    }
  }

  // Both sources are fully read before the target is written, which is what
  // makes ".dict.join a b a" — and every other aliasing — safe. A join
  // replaces the target whole: re-running it is idempotent, and a stale
  // entry from the last run cannot survive into this one. The rows keep
  // their reserved storage; only the live count is dropped.
  {
    const dictStoreGuard guard(targetStore->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    for (std::size_t i = 0; i < targetStore->count; i++) {
      targetStore->entries[i].key.clear();
      targetStore->entries[i].value.clear();
    }
    targetStore->count = merged.count;
    for (std::size_t i = 0; i < merged.count; i++) {
      targetStore->entries[i].key.assign(merged.entries[i].key);
      targetStore->entries[i].value.assign(merged.entries[i].value);
    }
  }

  // Outside every guard on purpose: the send runs the whole downstream
  // graph, which may well store into any of these dictionaries. An unnamed
  // target has no name to pass on — gDict's rule for an unnamed reference.
  if (reference.empty()) return;
  outputs[0].SendList(reference, thread);
}
