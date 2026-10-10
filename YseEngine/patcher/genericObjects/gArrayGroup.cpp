#include "gArrayGroup.h"
#include "../pAtomList.h"
#include "../pObjectList.hpp"

#include <cstddef>
#include <string>

using namespace YSE::PATCHER;
#define className gArrayGroup

namespace {

  // Doc strings, hoisted out of the constructor because they are long enough
  // that the constructor stops being readable with them inline — gArray's
  // arrangement.
  constexpr char kTriggerInletDoc[] =
      "Bang, or the bound array's reference \"array <name>\": send the groups, then bang the "
      "done outlet. A trigger that arrives while the groups are being sent is refused.";
  constexpr char kReferenceInletDoc[] =
      "Accepts the bound array's reference \"array <name>\" and ignores it, so the array's "
      "reference outlet can be wired here. Anything else is refused.";
  constexpr char kGroupOutletDoc[] =
      "One message per distinct value, in order of first appearance, holding the value as many "
      "times as it occurs. A value that occurs once leaves as a single int, float or symbol.";
  constexpr char kDoneOutletDoc[] = "Bang after the last group, also when the array is empty.";

} // namespace

gArrayGroup::gArrayGroup() : gArrayEndsBase() {
  // The trigger inlet, hot, and the reference inlet, cold — the family's
  // shape for a read-only object driven by a bang. No int or float method: a
  // bare number names no array, and an object that streamed up to 256
  // messages on any stray number that reached it would be a trap rather than
  // a convenience — .uzi's reasoning, .array.iter's precedent.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  // The group outlet, then the done outlet. ANY on the groups: a bucket of
  // one leaves as the int, float or symbol it spells, a bucket of several as
  // one list — SendAtoms' rule. The done bang is sent last — the carry
  // exception to right-to-left; see the header.
  ADD_OUT_ANY;
  ADD_OUT_BANG;

  // The allocation SendAtoms' render would otherwise need, taken here on the
  // control thread.
  AtomList::ReserveRender(emitScratch);

  ADD_DESCRIPTION(
      "Groups the equal elements of a named array. A bang sends one list per distinct value, "
      "holding that value as many times as it occurs, then bangs the done outlet.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "group", kGroupOutletDoc, "");
  OUTLET_DOC(1, "done", kDoneOutletDoc, "");
  PARAM_DOC("name", "",
            "Name of the array to use: the .array with this name in the same patcher. Empty "
            "uses a private, empty array.",
            "any identifier");
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  Group(thread);
}

LIST_IN(ListIn) {
  if (inlet == 1) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — gDictSlice's inlet rule, the family's shape.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference groups — the message its .array emits on a bang,
  // the family's gesture. Anything else, including a reference naming an
  // array this object is not bound to, is refused: resolving an unrecognised
  // name means the registry's mutex, and this may be the audio thread.
  if (ArrayReferenceNames(value, arrayName)) {
    Group(thread);
    return;
  }
  Refuse();
}

void gArrayGroup::Group(YSE::THREAD thread) {
  // The re-entrancy guard, held across the whole grouping: this object emits
  // in a loop, so a cord from either outlet back to the inlet re-enters here
  // from inside the grouping, and letting it through would rewrite the
  // snapshot being bucketed. The loser — the loop-back, or another thread —
  // is dropped and counted rather than made to spin, this being a path the
  // audio callback takes.
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    Refuse();
    return;
  }

  // The snapshot: the array as it stands right now, copied out under its
  // guard into rows reserved at construction — bounded assigns, no
  // allocation. This is the whole of the time the guard is held, so the
  // buckets' sends run with no guard at all and a downstream write into this
  // same array is never the one thing the try-lock drops.
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

  // The bucketing: one first-occurrence scan, .zl thin's order. Equality is
  // the whole spelling — the family's byte compare — so a bucket needs no
  // member list, only its text (the first occurrence) and how often it
  // occurs. Bounded quadratic: at most 256 elements searched against at most
  // 256 buckets, each compare at most 64 characters.
  std::size_t groups = 0;
  for (std::size_t i = 0; i < snapshot.count; i++) {
    std::size_t g = 0;
    while (g < groups && snapshot.elements[firstAt[g]] != snapshot.elements[i])
      g++;
    if (g < groups) {
      memberCount[g]++;
    } else {
      firstAt[groups] = (std::uint16_t)i;
      memberCount[groups] = 1;
      groups++;
    }
  }

  // Whole buckets or nothing, proven before the first send: once a bucket
  // has left it cannot be unsaid, so a grouping any bucket of which outruns
  // what a cord carries is refused whole here — a bucket that lost members
  // would lie about the value's frequency. The backing-text bound is the
  // binding one: a bucket holds at most the store's 256 elements, which is
  // exactly the atom ceiling, but 256 copies of a long element outrun the
  // text. .array.sect's whole-refusal rule (#792).
  for (std::size_t g = 0; g < groups; g++) {
    const std::size_t chars = snapshot.elements[firstAt[g]].size() * memberCount[g];
    if (chars > AtomList::TEXT_CAPACITY) {
      Refuse();
      busy.store(false, std::memory_order_release);
      return;
    }
  }

  // One send per bucket, first-occurrence order, each completing in full
  // before the next — the value repeated into a fixed AtomList and sent
  // through SendAtoms, so a bucket of one leaves as the int, float or symbol
  // it spells and a bucket of several as one list. The fit was proven above,
  // so no Add here can refuse.
  for (std::size_t g = 0; g < groups; g++) {
    emitList.Clear();
    for (std::size_t n = 0; n < memberCount[g]; n++)
      emitList.Add(snapshot.elements[firstAt[g]]);
    SendAtoms(outputs[0], emitList, emitScratch, thread);
  }

  // The done bang, last — the carry exception to right-to-left — and even
  // for an empty array, .uzi's rule for a count of zero. A refused grouping
  // never reaches here, so a done bang always means a completed grouping.
  outputs[1].SendBang(thread);

  busy.store(false, std::memory_order_release);
}
