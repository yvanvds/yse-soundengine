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
      "Bang, or the bound array's reference \"array <name>\": send every element, then bang the "
      "done outlet. A trigger that arrives while the elements are being sent is refused.";
  constexpr char kReferenceInletDoc[] =
      "Accepts the bound array's reference \"array <name>\" and ignores it, so the array's "
      "reference outlet can be wired here. Anything else is refused.";
  constexpr char kElementOutletDoc[] =
      "One element per message, first to last, as an int, float or symbol by its spelling.";
  constexpr char kDoneOutletDoc[] = "Bang after the last element, also when the array is empty.";

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
      "Sends the elements of a named array one at a time, first to last, then bangs its done "
      "outlet. Use it to do something for each element.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "element", kElementOutletDoc, "");
  OUTLET_DOC(1, "done", kDoneOutletDoc, "");
  PARAM_DOC("name", "",
            "Name of the array to use: the .array with this name in the same patcher. Empty "
            "uses a private, empty array.",
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
