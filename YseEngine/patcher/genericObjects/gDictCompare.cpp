#include "gDictCompare.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

#include <cstring>
#include <string>

using namespace YSE::PATCHER;
#define className gDictCompare

namespace {

  // Doc strings, hoisted out of CONSTRUCT() because they are long enough that
  // the constructor stops being readable with them inline — gDict's
  // arrangement.
  constexpr char kLeftInletDoc[] =
      "A bang compares the two bound dictionaries and sends the verdict out the outlet. "
      "\"dictionary <name>\" does the same when it names the left dictionary bound by the first "
      "creation argument — the message a .dict's reference outlet emits on a bang, so wiring that "
      "outlet here gives Max's own gesture. A reference naming anything else, or any other "
      "message, is refused and counted rather than logged, since this inlet may be the audio "
      "thread.";
  constexpr char kRightInletDoc[] =
      "\"dictionary <name>\" is accepted silently when it names the right dictionary bound by the "
      "second creation argument, so a patch may wire both reference outlets across as it would in "
      "Max. It sets nothing — the binding is the creation argument, resolved on the control "
      "thread — and anything else is refused and counted.";
  constexpr char kOutletDoc[] =
      "The verdict: 1 when the two dictionaries hold the same entries — the same number of them, "
      "every key path present in both with identical value text, storage order ignored — and 0 "
      "when they do not.";

} // namespace

CONSTRUCT() {
  // Two inlets, as in Max: the left one triggers, the right one only
  // acknowledges the reference it is already bound to.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  ADD_OUT_INT;

  ADD_PARAM(leftName);
  ADD_PARAM(rightName);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // Private, empty stores to start with, so neither pointer is ever null and
  // no message handler needs a null check. Rebind() trades each for a shared
  // one as soon as there is both a name and a patcher to prefix it with.
  Rebind();

  ADD_DESCRIPTION(
      "Compares two dictionaries — Max's dict.compare on the name-addressed value model .dict "
      "settled: both dictionaries are bound from the creation arguments, \".dict.compare <left> "
      "<right>\", because a dictionary is addressed by name and never passed down a cord. A bang, "
      "or the left dictionary's reference message \"dictionary <name>\", sends 1 when the two "
      "hold the same entries and 0 when they do not — the same number of entries and every key "
      "path present in both with identical value text, storage order ignored. The comparison "
      "answers \"did anything change?\" without diffing key by key: a patch that keeps a "
      "dictionary of live parameters compares the preset it just loaded against what is "
      "running.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "compare", kLeftInletDoc, "");
  INLET_DOC(1, "right reference", kRightInletDoc, "");
  OUTLET_DOC(0, "equal", kOutletDoc, "0 or 1");
  PARAM_DOC("left", "",
            "The left dictionary's shared name, addressed as \"<patcherName>.<name>\" — the "
            "dictionary a .dict of the same name in this patcher holds. Resolved once, on the "
            "control thread, which is why no message re-points it at run time. Empty compares a "
            "private, empty dictionary.",
            "any identifier");
  PARAM_DOC("right", "",
            "The right dictionary's shared name, bound exactly as the left one. Empty compares a "
            "private, empty dictionary.",
            "any identifier");
}

// A re-parse must not leave half of the previous configuration standing:
// Parameters::Set() calls this before parsing and returns early on an empty
// argument string, so this is what makes SetParams("") a real reset — dropping
// both names and going back to two private dictionaries. gDict's rule.
PARM_CLEAR() {
  leftName.clear();
  rightName.clear();
  Rebind();
}

PARM_PARSE() {
  Rebind();
}

// `parent` is a patcherImplementation by construction (the patcher hands
// itself to every object via SetParent); the cast mirrors gDict's.
void gDictCompare::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  Rebind();
}

void gDictCompare::RefreshBinding() {
  Rebind();
}

void gDictCompare::Rebind() {
  // No name, or no patcher to prefix it with, means no address — and no
  // address means a private, empty dictionary on that side. See gDict.h for
  // why an unnamed side does not pool on "<patcherName>.".
  auto* p = static_cast<patcherImplementation*>(parent);

  std::string leftAddress;
  if (!leftName.empty() && p != nullptr) leftAddress = p->Name() + "." + leftName;
  std::string rightAddress;
  if (!rightName.empty() && p != nullptr) rightAddress = p->Name() + "." + rightName;

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
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  Compare(thread);
}

LIST_IN(ListIn) {
  if (inlet == 0) {
    // The left dictionary's reference triggers the comparison — the message
    // its .dict emits on a bang. Anything else, including a reference to a
    // dictionary this object is not bound to, is refused: resolving an
    // unrecognised name means the registry's mutex, and this may be the
    // audio thread.
    if (DictReferenceNames(value, leftName)) {
      Compare(thread);
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

void gDictCompare::Compare(YSE::THREAD thread) {
  // Copy the left dictionary out under its guard, so the right store's guard
  // is never nested inside it: two guards at once would put a lock-ordering
  // obligation on every pair of objects naming the same two dictionaries, and
  // the same-store case would trip over its own try-lock. Bounded assigns
  // into rows reserved at construction — no allocation.
  {
    const dictStoreGuard guard(leftStore->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    snapshot.count = leftStore->count;
    for (std::size_t i = 0; i < leftStore->count; i++) {
      snapshot.entries[i].key.assign(leftStore->entries[i].key);
      snapshot.entries[i].value.assign(leftStore->entries[i].value);
    }
  }

  // The same entries, order-insensitively: with the counts equal and keys
  // unique within a store, "every left entry is in the right with the same
  // value" is the whole of equality. A bounded scan per entry — at most
  // MAX_ENTRIES squared comparisons of pre-sized rows.
  bool equal = false;
  {
    const dictStoreGuard guard(rightStore->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    equal = snapshot.count == rightStore->count;
    for (std::size_t i = 0; equal && i < snapshot.count; i++) {
      const dictEntry& entry = snapshot.entries[i];
      const std::size_t at = DictFind(*rightStore, entry.key.data(), entry.key.size());
      equal = at < rightStore->count && rightStore->entries[at].value == entry.value;
    }
  }

  // Outside both guards on purpose: the send runs the whole downstream graph,
  // which may well store into either of these dictionaries.
  outputs[0].SendInt(equal ? 1 : 0, thread);
}
