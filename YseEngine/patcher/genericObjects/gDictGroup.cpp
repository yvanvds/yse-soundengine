#include "gDictGroup.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

#include <cstring>
#include <string>

using namespace YSE::PATCHER;
#define className gDictGroup

namespace {

  // The separator, and its length, without spelling `2` anywhere. gDict.cpp's
  // arrangement.
  constexpr std::size_t kSeparatorLength = sizeof(kDictPathSeparator) - 1;
  constexpr std::size_t kReferenceLength = sizeof(kDictReferenceWord) - 1;

  // Doc strings, hoisted out of CONSTRUCT() because they are long enough that
  // the constructor stops being readable with them inline — gDict's
  // arrangement.
  constexpr char kSourceInletDoc[] =
      "A bang groups the source dictionary's entries into the target and sends the target's "
      "reference out the outlet. \"dictionary <name>\" does the same when it names the source "
      "dictionary bound by the first creation argument — the message a .dict's reference outlet "
      "emits on a bang, so wiring that outlet here gives Max's own gesture. A reference naming "
      "anything else, or any other message, is refused and counted rather than logged, since this "
      "inlet may be the audio thread.";
  constexpr char kTargetInletDoc[] =
      "\"dictionary <name>\" is accepted silently when it names the target dictionary bound by "
      "the second creation argument, so a patch may wire the target's reference outlet across. It "
      "sets nothing — the binding is the creation argument, resolved on the control thread — and "
      "anything else is refused and counted.";
  constexpr char kOutletDoc[] =
      "\"dictionary <target>\" after a grouping — the reference the dict.* family binds, carrying "
      "the local name so the receiving object prefixes it with its own patcher's name. Silent for "
      "an unnamed target, which has no name to pass on.";

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

  ADD_PARAM(sourceName);
  ADD_PARAM(targetName);
  ADD_PARAM(groupKey);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // Private, empty stores to start with, so neither pointer is ever null and
  // no message handler needs a null check. Rebind() trades each for a shared
  // one as soon as there is both a name and a patcher to prefix it with.
  Rebind();
  RefreshReference();

  ADD_DESCRIPTION(
      "Groups a dictionary's entries by a value — Max's dict.group on the name-addressed value "
      "model .dict settled: both dictionaries are bound from the creation arguments, "
      "\".dict.group <source> <target> [<key>]\", because a dictionary is addressed by name and "
      "never passed down a cord. A bang, or the source dictionary's reference message "
      "\"dictionary <name>\", replaces the target with the grouped entries, each written as "
      "\"<groupValue>::<originalPath>\" — the dictionary of dictionaries the flat store expresses "
      "as a path prefix. With a <key> the source is read as a table of records (the first path "
      "segment names the record) and each entry groups under the value its record stores at that "
      "key; without one each entry groups under its own value, turning a flat table into an "
      "index. An entry without a group, or one whose composed path outgrows the 128-character "
      "key capacity, is refused whole and counted rather than truncated.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "group", kSourceInletDoc, "");
  INLET_DOC(1, "target reference", kTargetInletDoc, "");
  OUTLET_DOC(0, "reference", kOutletDoc, "");
  PARAM_DOC("source", "",
            "The source dictionary's shared name, addressed as \"<patcherName>.<name>\" — the "
            "dictionary a .dict of the same name in this patcher holds. Resolved once, on the "
            "control thread, which is why no message re-points it at run time. Empty groups a "
            "private, empty dictionary.",
            "any identifier");
  PARAM_DOC("target", "",
            "The target dictionary's shared name, bound exactly as the source. A grouping "
            "replaces its contents whole. The result goes into a bound dictionary rather than a "
            "new anonymous one because there is no way to hand a fresh dictionary's identity "
            "down a cord.",
            "any identifier");
  PARAM_DOC("key", "",
            "The key each record's group is read from: with \"instrument\" every "
            "\"<record>::...\" entry groups under the value at \"<record>::instrument\", and a "
            "record without that entry is skipped and counted. Empty groups each entry by its "
            "own value instead, turning a flat table into an index.",
            "any key path segment");
}

// A re-parse must not leave half of the previous configuration standing:
// Parameters::Set() calls this before parsing and returns early on an empty
// argument string, so this is what makes SetParams("") a real reset — dropping
// all three arguments and going back to two private dictionaries. gDict's
// rule.
PARM_CLEAR() {
  sourceName.clear();
  targetName.clear();
  groupKey.clear();
  Rebind();
  RefreshReference();
}

PARM_PARSE() {
  Rebind();
  RefreshReference();
}

// `parent` is a patcherImplementation by construction (the patcher hands
// itself to every object via SetParent); the cast mirrors gDict's.
void gDictGroup::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  Rebind();
}

void gDictGroup::RefreshBinding() {
  Rebind();
}

void gDictGroup::Rebind() {
  // No name, or no patcher to prefix it with, means no address — and no
  // address means a private, empty dictionary on that side. See gDict.h for
  // why an unnamed side does not pool on "<patcherName>.".
  auto* p = static_cast<patcherImplementation*>(parent);

  std::string sourceAddress;
  if (!sourceName.empty() && p != nullptr) sourceAddress = p->Name() + "." + sourceName;
  std::string targetAddress;
  if (!targetName.empty() && p != nullptr) targetAddress = p->Name() + "." + targetName;

  // Unchanged binding: keep the store. A live SetParams that leaves a name
  // alone must not re-anchor that side, and neither must the second Rebind()
  // a Set() makes (clear, then parse).
  bool created = false;
  if (sourceStore == nullptr || sourceAddress != boundSourceAddress) {
    sourceStore = sourceAddress.empty() ? std::make_shared<dictStore>()
                                        : AcquireNamedStore<dictStore>(sourceAddress, created);
    boundSourceAddress = sourceAddress;
  }
  if (targetStore == nullptr || targetAddress != boundTargetAddress) {
    targetStore = targetAddress.empty() ? std::make_shared<dictStore>()
                                        : AcquireNamedStore<dictStore>(targetAddress, created);
    boundTargetAddress = targetAddress;
  }
}

void gDictGroup::RefreshReference() {
  reference.clear();
  if (targetName.empty()) return;
  reference.reserve(kReferenceLength + 1 + targetName.size());
  reference += kDictReferenceWord;
  reference += ' ';
  reference += targetName;
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  Group(thread);
}

LIST_IN(ListIn) {
  if (inlet == 0) {
    // The source dictionary's reference triggers the grouping — the message
    // its .dict emits on a bang. Anything else, including a reference to a
    // dictionary this object is not bound to, is refused: resolving an
    // unrecognised name means the registry's mutex, and this may be the
    // audio thread.
    if (DictReferenceNames(value, sourceName)) {
      Group(thread);
      return;
    }
    Refuse();
    return;
  }

  // Inlet 1: the target dictionary's reference is acknowledged and nothing
  // more — the binding is the creation argument, resolved on the control
  // thread, so there is nothing to set.
  if (DictReferenceNames(value, targetName)) return;
  Refuse();
}

void gDictGroup::Group(YSE::THREAD thread) {
  // Copy the source dictionary out under its guard, so the target store's
  // guard is never nested inside it: two guards at once would put a
  // lock-ordering obligation on every pair of objects naming the same two
  // dictionaries, and the same-store case would trip over its own try-lock.
  // Bounded assigns into rows reserved at construction — no allocation.
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

  {
    const dictStoreGuard guard(targetStore->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }

    // A grouping replaces the target whole — re-running it is idempotent, and
    // a stale entry from the last run cannot survive into this one. The rows
    // keep their reserved storage; only the live count is dropped.
    for (std::size_t i = 0; i < targetStore->count; i++) {
      targetStore->entries[i].key.clear();
      targetStore->entries[i].value.clear();
    }
    targetStore->count = 0;

    for (std::size_t i = 0; i < snapshot.count; i++) {
      const dictEntry& entry = snapshot.entries[i];

      // The group value: the record's <key> entry, resolved against the
      // snapshot so no second guard is needed — or the entry's own value when
      // no key was given.
      const char* group = entry.value.data();
      std::size_t groupLength = entry.value.size();
      if (!groupKey.empty()) {
        const std::size_t separator = entry.key.find(kDictPathSeparator);
        const std::size_t segmentLength =
            (separator == std::string::npos) ? entry.key.size() : separator;
        const std::size_t lookupLength = segmentLength + kSeparatorLength + groupKey.size();
        // A lookup path past the capacity cannot name an entry the store
        // holds, so the record has no group either way.
        if (lookupLength > dictStore::KEY_CAPACITY) {
          Refuse();
          continue;
        }
        std::memcpy(lookup, entry.key.data(), segmentLength);
        std::memcpy(lookup + segmentLength, kDictPathSeparator, kSeparatorLength);
        std::memcpy(lookup + segmentLength + kSeparatorLength, groupKey.data(), groupKey.size());
        const std::size_t at = DictFind(snapshot, lookup, lookupLength);
        if (at >= snapshot.count) {
          // No <key> entry in this record: the entry has no group, and
          // inventing one would file it somewhere the patch never asked for.
          Refuse();
          continue;
        }
        group = snapshot.entries[at].value.data();
        groupLength = snapshot.entries[at].value.size();
      }

      // An empty group would compose "::<path>" — an empty path segment,
      // which is not a path this store can express as JSON.
      if (groupLength == 0) {
        Refuse();
        continue;
      }

      // "<groupValue>::<originalPath>". Prepending the group can overflow
      // KEY_CAPACITY where the input did not — refused whole and counted,
      // never truncated.
      const std::size_t composedLength = groupLength + kSeparatorLength + entry.key.size();
      if (composedLength > dictStore::KEY_CAPACITY) {
        Refuse();
        continue;
      }
      std::memcpy(composed, group, groupLength);
      std::memcpy(composed + groupLength, kDictPathSeparator, kSeparatorLength);
      std::memcpy(composed + groupLength + kSeparatorLength, entry.key.data(), entry.key.size());
      if (!DictStoreAt(*targetStore, composed, composedLength, entry.value.data(),
                       entry.value.size())) {
        Refuse();
      }
    }
  }

  // Outside both guards on purpose: the send runs the whole downstream graph,
  // which may well store into either of these dictionaries. An unnamed target
  // has no name to pass on — gDict's rule for an unnamed reference.
  if (reference.empty()) return;
  outputs[0].SendList(reference, thread);
}
