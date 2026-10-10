#include "gArrayCompare.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

#include <cstddef>
#include <memory>
#include <string>

using namespace YSE::PATCHER;

namespace {

  constexpr std::size_t kReferenceLength = sizeof(kArrayReferenceWord) - 1;

  // Copy @p from into @p to whole — bounded assigns into rows arrayStore's
  // own constructor reserved, so it is safe under the guard on whichever
  // thread the message arrived on. The caller holds `from`'s guard.
  void CopyStore(const arrayStore& from, arrayStore& to) {
    to.count = from.count;
    for (std::size_t i = 0; i < from.count; i++)
      to.elements[i].assign(from.elements[i]);
  }

  // True when @p left and @p right hold the same elements in the same order —
  // count and spelling, the pair's shared meaning of equality (Max's "value
  // and order"). Bounded byte compares; the caller holds whatever guards make
  // the two sides stable.
  bool StoresEqual(const arrayStore& left, const arrayStore& right) {
    if (left.count != right.count) return false;
    for (std::size_t i = 0; i < left.count; i++) {
      if (left.elements[i] != right.elements[i]) return false;
    }
    return true;
  }

} // namespace

// ─── .array.change ────────────────────────────────────────────────────────────

#define className gArrayChange

gArrayChange::gArrayChange() : gArrayEndsBase() {
  // The trigger inlet, hot, and the baseline inlet, cold. The poll is asked
  // for with a bang, never addressed, so there is no int or float method
  // anywhere — the family's shape, with the one departure documented in the
  // header: inlet 1 re-baselines (silently) rather than merely acknowledging.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  // The reference outlet — always list text, gArrayEndsWriter's outlet — and
  // the verdict outlet, an int because 1 and 0 are the only values it can
  // carry, the scalar .change's edge outlets' rule.
  ADD_OUT_LIST;
  ADD_OUT_INT;

  ADD_DESCRIPTION(
      "Reports whether a named array has changed since it was last polled. Each bang sends 1 or "
      "0, and on a change also sends the array's reference, so work further down only runs when "
      "something moved.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger",
            "Bang, or the bound array's reference \"array <name>\": compare the array with the "
            "last state seen, then remember the array as it is now.",
            "");
  INLET_DOC(1, "set baseline",
            "The bound array's reference \"array <name>\" makes the array's current contents the "
            "last state seen, without output. Anything else is refused.",
            "");
  OUTLET_DOC(0, "reference",
             "\"array <name>\" when the poll found a change, sent after outlet 1. Nothing is sent "
             "when the object has no name.",
             "");
  OUTLET_DOC(1, "changed",
             "1 when the array differs from the last state seen, 0 when it does not. Sent on "
             "every poll.",
             "0 or 1");
  PARAM_DOC("name", "",
            "Name of the array to watch: the .array with this name in the same patcher. Empty "
            "watches a private, empty array.",
            "any identifier");
}

// The reference follows the name and the baseline resets — a re-parse must
// not keep comparing against the previous binding's contents, and whatever
// the newly bound array holds is news this object has not reported.
void gArrayChange::ParamsChanged() {
  RefreshReference();
  snapshot.count = 0;
}

void gArrayChange::RefreshBinding() {
  gArrayEndsBase::RefreshBinding();
  // A rename moved the address prefix, so the object now watches a different
  // array; the local name — and with it the reference — is unchanged.
  snapshot.count = 0;
}

void gArrayChange::RefreshReference() {
  reference.clear();
  if (arrayName.empty()) return;
  reference.reserve(kReferenceLength + 1 + arrayName.size());
  reference += kArrayReferenceWord;
  reference += ' ';
  reference += arrayName;
}

BANG_IN(BangIn) {
  (void)inlet;
  Poll(thread);
}

LIST_IN(ListIn) {
  if (inlet == 1) {
    // The baseline inlet: the bound array's reference re-baselines silently —
    // Max's right inlet, the scalar .change's set. Anything else, including a
    // reference naming an array this object is not bound to, is refused:
    // resolving an unrecognised name means the registry's mutex, and this may
    // be the audio thread.
    if (ArrayReferenceNames(value, arrayName)) {
      Rebase();
      return;
    }
    Refuse();
    return;
  }

  // The array's reference polls — the message its .array emits on a bang,
  // the family's gesture.
  if (ArrayReferenceNames(value, arrayName)) {
    Poll(thread);
    return;
  }
  Refuse();
}

void gArrayChange::Poll(YSE::THREAD thread) {
  bool changed = false;
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    // Compare and re-baseline under the same hold, so the baseline that
    // replaces the old one is exactly the state the verdict is about — no
    // other thread can write between the compare and the copy.
    changed = !StoresEqual(*store, snapshot);
    if (changed) CopyStore(*store, snapshot);
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well write into this same array, and inside the guard that
  // write would be the one thing the try-lock drops. Right to left — the
  // verdict before the reference, the scalar .change's order — so whatever
  // the reference triggers downstream already sees the matching report.
  outputs[1].SendInt(changed ? 1 : 0, thread);
  if (!changed || reference.empty()) return;
  outputs[0].SendList(reference, thread);
}

void gArrayChange::Rebase() {
  const arrayStoreGuard guard(store->busy);
  if (!guard.Held()) {
    Refuse();
    return;
  }
  CopyStore(*store, snapshot);
  // Nothing is emitted — if a re-baseline announced itself it would be a
  // spelling of the poll and would have no reason to exist.
}

// ─── .array.compare ───────────────────────────────────────────────────────────

#undef className
#define className gArrayCompare

gArrayCompare::gArrayCompare() : gArrayEndsBase() {
  // The compare inlet, hot, and the right reference inlet, cold —
  // gDictCompare's shape: the left inlet asks, the right one only
  // acknowledges the array it is already bound to.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  ADD_OUT_INT;

  // After gArrayEndsBase's name — the second creation argument.
  ADD_PARAM(rightName);

  // A private, empty right store to start with, so `rightStore` is never
  // null and no message handler needs a null check — the base constructor
  // has already done the same for the left side.
  RebindRight();

  ADD_DESCRIPTION(
      "Compares two named arrays. A bang sends 1 when they hold the same elements in the same "
      "order, and 0 when they do not.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "compare",
            "Bang, or the left array's reference \"array <name>\": compare the two arrays.", "");
  INLET_DOC(1, "right reference",
            "Accepts the right array's reference \"array <name>\" and ignores it, so its "
            "reference outlet can be wired here. Anything else is refused.",
            "");
  OUTLET_DOC(0, "equal",
             "1 when both arrays have the same length and the same element at every position, "
             "otherwise 0. Two empty arrays are equal.",
             "0 or 1");
  PARAM_DOC("left", "",
            "Name of the left array: the .array with this name in the same patcher. Empty uses "
            "a private, empty array.",
            "any identifier");
  PARAM_DOC("right", "",
            "Name of the right array, found the same way. Empty uses a private, empty array.",
            "any identifier");
}

// A re-parse must not leave half of the previous configuration standing:
// SetParams("") — and the clear half of every Set() — drops the right name
// along with the left. gArraySetOpBase's rule.
PARM_CLEAR() {
  rightName.clear();
  gArrayEndsBase::ClearParams();
}

// The base calls this after ClearParams / ParseParams have re-read the
// names — the moment the right binding follows the left one, which the
// base's own Rebind() has just re-anchored.
void gArrayCompare::ParamsChanged() {
  RebindRight();
}

void gArrayCompare::SetParent(pObject* newParent) {
  gArrayEndsBase::SetParent(newParent);
  RebindRight();
}

void gArrayCompare::RefreshBinding() {
  gArrayEndsBase::RefreshBinding();
  RebindRight();
}

void gArrayCompare::RebindRight() {
  // The exact mirror of gArrayEndsBase::Rebind over the second name. No
  // name, or no patcher to prefix it with, means no address — and no address
  // means a private, empty array on that side. See gArray.h for why an
  // unnamed side does not pool on "patcher.<patcherName>.".
  std::string address;
  if (!rightName.empty() && parent != nullptr) {
    auto* p = static_cast<patcherImplementation*>(parent);
    address = p->ScopedAddress(rightName);
  }

  // Unchanged binding: keep the store. A live SetParams that leaves the name
  // alone must not re-anchor it, and neither must the second rebind a Set()
  // makes (clear, then parse).
  if (rightStore != nullptr && address == boundRightAddress) return;

  bool created = false;
  rightStore = address.empty() ? std::make_shared<arrayStore>()
                               : AcquireNamedStore<arrayStore>(address, created);
  boundRightAddress = address;
}

BANG_IN(BangIn) {
  (void)inlet;
  Compare(thread);
}

LIST_IN(ListIn) {
  if (inlet == 1) {
    // The right reference inlet acknowledges the array it is already bound
    // to and nothing more — the binding is the creation argument, resolved
    // on the control thread, so there is nothing to set. gDictCompare's
    // inlet rule.
    if (!ArrayReferenceNames(value, rightName)) Refuse();
    return;
  }

  // The left array's reference compares — the message its .array emits on a
  // bang, the family's gesture. Anything else, including a reference naming
  // an array this object is not bound to on this side, is refused: resolving
  // an unrecognised name means the registry's mutex, and this may be the
  // audio thread.
  if (ArrayReferenceNames(value, arrayName)) {
    Compare(thread);
    return;
  }
  Refuse();
}

void gArrayCompare::Compare(YSE::THREAD thread) {
  // Copy the left array out under its guard, so the right store's guard is
  // never nested inside it: two guards at once would put a lock-ordering
  // obligation on every pair of objects naming the same two arrays, and the
  // same-store case — ".array.compare seq seq" — would trip over its own
  // try-lock and refuse every ask. Bounded assigns into strings reserved at
  // construction — no allocation. gDictCompare's arrangement, whole.
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    CopyStore(*store, snapshot);
  }

  bool equal = false;
  {
    const arrayStoreGuard guard(rightStore->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    equal = StoresEqual(snapshot, *rightStore);
  }

  // Outside both guards on purpose: the send runs the whole downstream
  // graph, which may well write into either of these arrays, and inside a
  // guard that write would be the one thing the try-lock drops.
  outputs[0].SendInt(equal ? 1 : 0, thread);
}
