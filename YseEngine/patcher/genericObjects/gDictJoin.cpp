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
      "A bang joins the two bound dictionaries into the target and sends the target's reference "
      "out the outlet. \"dictionary <name>\" does the same when it names the left dictionary "
      "bound by the first creation argument — the message a .dict's reference outlet emits on a "
      "bang, so wiring that outlet here gives Max's own gesture. A reference naming anything "
      "else, or any other message, is refused and counted rather than logged, since this inlet "
      "may be the audio thread.";
  constexpr char kRightInletDoc[] =
      "\"dictionary <name>\" is accepted silently when it names the right dictionary bound by "
      "the second creation argument, so a patch may wire both reference outlets across as it "
      "would in Max. It sets nothing — the binding is the creation argument, resolved on the "
      "control thread — and anything else is refused and counted.";
  constexpr char kOutletDoc[] =
      "\"dictionary <target>\" after a join — the reference the dict.* family binds, carrying "
      "the local name so the receiving object prefixes it with its own patcher's name. Silent "
      "for an unnamed target, which has no name to pass on.";

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

  ADD_DESCRIPTION(
      "Merges two dictionaries into one — Max's dict.join on the name-addressed value model "
      ".dict settled: all three dictionaries are bound from the creation arguments, \".dict.join "
      "<left> <right> <target>\", because a dictionary is addressed by name and never passed "
      "down a cord. A bang, or the left dictionary's reference message \"dictionary <name>\", "
      "replaces the target with the left dictionary's entries overlaid by the right's — on a "
      "colliding key path the right dictionary overwrites the left, Max's own rule, so the left "
      "argument is the base and the right the override. Layering a preset over a base is the use "
      "case: both sources survive the join, and \".dict.join a b a\" spells the in-place merge "
      "when a patch wants one. A join past the 256-entry capacity is refused entry by entry and "
      "counted rather than truncated, so a partial merge is possible — whatever fits, left "
      "entries first, is the result.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "join", kLeftInletDoc, "");
  INLET_DOC(1, "right reference", kRightInletDoc, "");
  OUTLET_DOC(0, "reference", kOutletDoc, "");
  PARAM_DOC("left", "",
            "The left dictionary's shared name — the base of the join, addressed as "
            "\"<patcherName>.<name>\", the dictionary a .dict of the same name in this patcher "
            "holds. Resolved once, on the control thread, which is why no message re-points it "
            "at run time. Empty joins a private, empty dictionary.",
            "any identifier");
  PARAM_DOC("right", "",
            "The right dictionary's shared name, bound exactly as the left one — the override: "
            "on a colliding key path its value wins. Empty joins a private, empty dictionary.",
            "any identifier");
  PARAM_DOC("target", "",
            "The target dictionary's shared name, bound exactly as the sources. A join replaces "
            "its contents whole. The result goes into a bound dictionary rather than a new "
            "anonymous one because there is no way to hand a fresh dictionary's identity down a "
            "cord; naming a source as the target merges in place.",
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
  // why an unnamed side does not pool on "<patcherName>.".
  auto* p = static_cast<patcherImplementation*>(parent);

  std::string leftAddress;
  if (!leftName.empty() && p != nullptr) leftAddress = p->Name() + "." + leftName;
  std::string rightAddress;
  if (!rightName.empty() && p != nullptr) rightAddress = p->Name() + "." + rightName;
  std::string targetAddress;
  if (!targetName.empty() && p != nullptr) targetAddress = p->Name() + "." + targetName;

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
