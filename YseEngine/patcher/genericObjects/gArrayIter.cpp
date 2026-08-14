#include "gArrayIter.h"
#include "../pAtomList.h"
#include "../pObjectList.hpp"

#include <cstddef>
#include <string>

using namespace YSE::PATCHER;
#define className gArrayIter

namespace {

  // Doc strings, hoisted out of the constructor because they are long enough
  // that the constructor stops being readable with them inline — gArray's
  // arrangement.
  constexpr char kTriggerInletDoc[] =
      "A bang walks the bound array: one send per element out the element outlet, first to "
      "last, each typed the way the patcher spells it, then one bang out the done outlet. "
      "\"array <name>\" does the same when it names the array bound by the creation argument — "
      "the message an .array's reference outlet emits on a bang, so wiring that outlet here "
      "gives the family's gesture. A reference naming anything else, any other message, or a "
      "trigger arriving while a walk is already running — a cord looped back from an outlet, or "
      "another thread — is refused and counted rather than logged, since this inlet may be the "
      "audio thread.";
  constexpr char kReferenceInletDoc[] =
      "\"array <name>\" is accepted silently when it names the array bound by the creation "
      "argument, so a patch may wire the array's reference outlet across without triggering a "
      "walk. It sets nothing — the binding is the creation argument, resolved on the control "
      "thread — and anything else is refused and counted.";
  constexpr char kElementOutletDoc[] =
      "One element per send, first to last, typed the way the patcher spells it: a numeric "
      "element leaves as an int or a float by its spelling and anything else as a symbol, so "
      "each reaches the inlets an uncollected value would have reached. The walk is a snapshot "
      "of the array as it stood at the trigger, so an insert or remove arriving mid-walk — "
      "including from this outlet's own subgraph — renumbers the array but not the walk in "
      "flight: every element held at the trigger is emitted exactly once. Each send completes "
      "in full, the whole subgraph behind this outlet, before the next element leaves — up to "
      "256 sends per trigger, on whichever thread sent it.";
  constexpr char kDoneOutletDoc[] =
      "Bang after the last element — 'all the elements have been sent', the exception to "
      "right-to-left that Max states for uzi's carry. It fires even for an empty or unnamed "
      "(private) array, so the 'and afterwards, do this' branch is never silently skipped; a "
      "refused walk emits no done bang.";

} // namespace

gArrayIter::gArrayIter() : gArrayEndsBase() {
  // The trigger inlet, hot, and the reference inlet, cold — the family's
  // shape for a read-only object driven by a bang. No int or float method: a
  // bare number names no array, and an object that streamed up to 256
  // messages on any stray number that reached it would be a trap rather than
  // a convenience — .uzi's reasoning for ignoring unrecognised stimuli.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  // The element outlet, then the done outlet. ANY on the elements: what
  // leaves is an int, a float or a symbol by each element's spelling. The
  // done bang is sent last — the carry exception to right-to-left; see the
  // header.
  ADD_OUT_ANY;
  ADD_OUT_BANG;

  // The allocation SendAtom's symbol path would otherwise need, taken here
  // on the control thread.
  AtomList::ReserveRender(emitScratch);

  ADD_DESCRIPTION(
      "Outputs an array's elements one at a time — Max's array.iter on the name-addressed "
      "value model .array settled: the array is bound from the creation argument, \".array.iter "
      "<name>\", because an array is addressed by name and never passed down a cord. A bang, or "
      "the array's reference message \"array <name>\", emits every element in order out the "
      "element outlet — one send per element, each typed the way the patcher spells it, so a "
      "numeric element reaches the inlets an uncollected value would have reached — then a bang "
      "out the done outlet: the .uzi/.iter shape applied to stored data, and the object every "
      "'do this for each element' patch is built from. The walk is a snapshot of the array as "
      "it stood at the trigger: an insert or remove arriving mid-walk renumbers the array but "
      "never the walk in flight — every element held at the trigger is emitted exactly once — "
      "and two .array.iter on one name walk independently, each over its own snapshot. A "
      "trigger arriving mid-walk is refused and counted, as .uzi refuses a re-entrant start.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "element", kElementOutletDoc, "");
  OUTLET_DOC(1, "done", kDoneOutletDoc, "");
  PARAM_DOC("name", "",
            "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an "
            ".array of the same name in this patcher holds. Resolved once, on the control "
            "thread, which is why no message re-points it at run time. Empty walks a private, "
            "empty array: no elements, just the done bang.",
            "any identifier");
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  Walk(thread);
}

LIST_IN(ListIn) {
  if (inlet == 1) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — gDictSlice's inlet rule, the family's shape.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference walks — the message its .array emits on a bang,
  // the family's gesture. Anything else, including a reference naming an
  // array this object is not bound to, is refused: resolving an unrecognised
  // name means the registry's mutex, and this may be the audio thread.
  if (ArrayReferenceNames(value, arrayName)) {
    Walk(thread);
    return;
  }
  Refuse();
}

void gArrayIter::Walk(YSE::THREAD thread) {
  // The re-entrancy guard, held across the whole walk: this object emits in
  // a loop, so a cord from either outlet back to the inlet re-enters here
  // from inside the walk, and letting it through would restart the walk and
  // rewrite the snapshot being walked. The loser — the loop-back, or another
  // thread — is dropped and counted rather than made to spin, this being a
  // path the audio callback takes.
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    Refuse();
    return;
  }

  // The snapshot: the array as it stands right now, copied out under its
  // guard into rows reserved at construction — bounded assigns, no
  // allocation. This is the whole of the time the guard is held, so the
  // elements' sends run with no guard at all and a downstream write into
  // this same array is never the one thing the try-lock drops.
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      busy.store(false, std::memory_order_release);
      return;
    }
    snapshot.count = store->count;
    for (std::size_t i = 0; i < store->count; i++)
      snapshot.elements[i].assign(store->elements[i]);
  }

  // One send per element, first to last, each completing in full before the
  // next — what makes the object a serialiser rather than a scatter, .iter's
  // property. Through SendAtom, so each element leaves as the int, float or
  // symbol it spells — the patcher's transport rule, and the whole point of
  // emitting one at a time.
  for (std::size_t i = 0; i < snapshot.count; i++)
    SendAtom(outputs[0], snapshot.elements[i].data(), snapshot.elements[i].size(), emitScratch,
             thread);

  // The done bang, last — the carry exception to right-to-left — and even
  // for an empty array, .uzi's rule for a count of zero. A refused walk
  // never reaches here, so a done bang always means a completed walk.
  outputs[1].SendBang(thread);

  busy.store(false, std::memory_order_release);
}
