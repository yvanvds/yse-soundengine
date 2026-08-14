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
      "Outputs only when the array has changed — Max's array.change on the name-addressed value "
      "model .array settled: the array is bound from the creation argument, \".array.change "
      "<name>\", because an array is addressed by name and never passed down a cord. A bang "
      "compares the array against the baseline this object keeps, under one hold of the store's "
      "guard, replacing the baseline whenever they differ; the changed outlet then reports 1 or "
      "0 on every poll, and on a change the reference outlet also emits \"array <name>\", so "
      "wiring it onward gates the family — into an .array.iter trigger it means \"walk only "
      "when something moved\". Changed means count or spelling at any position, in order (7 and "
      "7. differ, 10 20 is not 20 10); the baseline starts as the empty array, the scalar "
      ".change's creation-argument rule, so an array that already has contents is news on the "
      "first poll. \"array <name>\" on the baseline inlet re-baselines silently — Max's right "
      "inlet, and the scalar .change's set: it moves the object's idea of current without "
      "telling anybody. The guard a patch puts in front of expensive downstream work.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger",
            "A bang polls: the bound array is compared against the baseline under one hold of "
            "the store's guard, and the baseline is replaced by what was found whenever they "
            "differ. The changed outlet reports 1 or 0 on every poll; the reference outlet "
            "emits only on a change. \"array <name>\" does the same when it names the array "
            "bound by the creation argument — the message an .array's reference outlet emits on "
            "a bang, the family's gesture. A reference naming anything else, or any other "
            "message, is refused and counted rather than logged, since this inlet may be the "
            "audio thread. A lost try-lock is a counted refusal and neither outlet fires: the "
            "array's state is unknown, and a comparator that guessed would corrupt the very "
            "state it exists to track.",
            "");
  INLET_DOC(1, "set baseline",
            "\"array <name>\" naming the bound array makes the array's current contents the "
            "baseline and emits nothing — Max's right inlet, which stores without generating "
            "output, and the scalar .change's set: it re-synchronises the object with a state "
            "that arrived by some other route, so the next poll answers 0 unless something "
            "moves again. Anything else is refused and counted. The one departure from the "
            "family's acknowledge-only inlet 1, made because doing this silently is exactly the "
            "Max semantics being ported.",
            "");
  OUTLET_DOC(0, "reference",
             "The bound array's reference, \"array <name>\", sent only when the poll found a "
             "change — the way an array leaves an object on the value model, so wiring it "
             "onward gates the family behind \"something actually moved\". Silent on an "
             "unchanged poll, which is the object's whole point, and silent on an unnamed "
             "(private) object, which has no name to pass on. Fires after the changed outlet, "
             "the scalar .change's right-to-left order, so whatever it triggers downstream "
             "already sees the matching report.",
             "");
  OUTLET_DOC(1, "changed",
             "The verdict, on every poll that got an answer: 1 when the array differs from the "
             "baseline — count or spelling at any position, in order — and 0 when it does not. "
             "Max's right outlet. Nothing at all on a refused poll: a lost try-lock leaves the "
             "array's state unknown, so no verdict is honest.",
             "0 or 1");
  PARAM_DOC("name", "",
            "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an "
            ".array of the same name in this patcher holds. Resolved once, on the control "
            "thread, which is why no message re-points it at run time. Empty watches a private, "
            "empty array: every poll answers 0 and the reference outlet never fires. A re-parse "
            "and a patcher rename both reset the baseline to the empty array — the object then "
            "watches a different binding, and whatever that array holds is news.",
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
      "Reports whether two arrays hold the same thing — Max's array.compare on the "
      "name-addressed value model .array settled: both arrays are bound from the creation "
      "arguments, \".array.compare <left> <right>\", because an array is addressed by name and "
      "never passed down a cord, and neither may be re-pointed from a message. A bang, or the "
      "left array's reference \"array <name>\", sends 1 when the two are equal and 0 when they "
      "are not — equal meaning the same number of elements and a byte-identical element at "
      "every position, Max's \"value and order\": 7 and 7. differ, and 10 20 is not 20 10, "
      "because an array is a sequence. The left array is copied out under its guard and the "
      "verdict decided against the right store under that guard alone — gDictCompare's "
      "arrangement — so no two guards are ever held at once and \".array.compare seq seq\" "
      "answers 1 instead of tripping over its own try-lock. With .array.change it is the "
      "comparison pair: the guards a patch puts in front of expensive downstream work.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "compare",
            "A bang compares the two bound arrays as they stand — the left copied out under its "
            "guard, the verdict decided against the right under that guard alone — and sends 1 "
            "or 0 out the outlet. \"array <name>\" does the same when it names the left array "
            "bound by the first creation argument — the message an .array's reference outlet "
            "emits on a bang, the family's gesture. A reference naming anything else, the right "
            "array's name included, or any other message, is refused and counted rather than "
            "logged, since this inlet may be the audio thread. A lost try-lock on either store "
            "is a counted refusal and nothing is sent: a verdict about state the object could "
            "not read would be a guess.",
            "");
  INLET_DOC(1, "right reference",
            "\"array <name>\" is accepted silently when it names the right array bound by the "
            "second creation argument, so a patch may wire both reference outlets across as it "
            "would in Max. It sets nothing — the binding is the creation argument, resolved on "
            "the control thread — and anything else is refused and counted.",
            "");
  OUTLET_DOC(0, "equal",
             "The verdict: 1 when the two arrays hold the same elements in the same order — the "
             "same count and a byte-identical spelling at every position — and 0 when they do "
             "not. Two empty (or unnamed, private) sides are equal: they hold the same "
             "nothing.",
             "0 or 1");
  PARAM_DOC("left", "",
            "The left array's shared name, addressed as \"<patcherName>.<name>\" — the sequence "
            "an .array of the same name in this patcher holds. Resolved once, on the control "
            "thread, which is why no message re-points it at run time. Empty compares a "
            "private, empty array on that side.",
            "any identifier");
  PARAM_DOC("right", "",
            "The right array's shared name, bound exactly as the left one. Empty compares a "
            "private, empty array on that side.",
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
  // unnamed side does not pool on "<patcherName>.".
  std::string address;
  if (!rightName.empty() && parent != nullptr) {
    auto* p = static_cast<patcherImplementation*>(parent);
    address = p->Name() + "." + rightName;
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
