#include "gDictIter.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

#include <cstring>
#include <string>

using namespace YSE::PATCHER;
#define className gDictIter

namespace {

  // Doc strings, hoisted out of CONSTRUCT() because they are long enough that
  // the constructor stops being readable with them inline — gDict's
  // arrangement.
  constexpr char kInletDoc[] =
      "A bang walks the bound dictionary: one \"<path> <value...>\" list per entry out the pair "
      "outlet, in storage order, then one bang out the done outlet. \"dictionary <name>\" does "
      "the same when it names the dictionary bound by the creation argument — the message a "
      ".dict's reference outlet emits on a bang, so wiring that outlet here gives Max's own "
      "gesture. A reference naming anything else, any other message, or a trigger arriving while "
      "a walk is already running — a cord looped back from an outlet, or another thread — is "
      "refused and counted rather than logged, since this inlet may be the audio thread.";
  constexpr char kPairDoc[] =
      "One entry per send: the whole \"::\" path followed by the stored value's tokens, in "
      "storage order — the order paths were first written. An entry holding nothing leaves as "
      "its path alone. The walk is a snapshot of the dictionary as it stood at the trigger, so "
      "a set or delete arriving mid-walk — including from this outlet's own subgraph — changes "
      "the dictionary but not the walk in flight: every entry held at the trigger is emitted "
      "exactly once. Each send completes in full, the whole subgraph behind this outlet, before "
      "the next pair leaves — up to 256 sends per trigger, on whichever thread sent it.";
  constexpr char kDoneDoc[] =
      "Bang after the last pair — 'all the pairs have been sent', the exception to right-to-left "
      "that Max states for uzi's carry. It fires even for an empty dictionary, so the 'and "
      "afterwards, do this' branch is never silently skipped; a refused walk emits no done bang.";

} // namespace

CONSTRUCT() {
  // One inlet, as in Max. No int or float handler: a bare number names no
  // dictionary, and an object that streamed up to 256 messages on any stray
  // number that reached it would be a trap rather than a convenience —
  // .uzi's reasoning for ignoring unrecognised stimuli.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);

  // The pair outlet, then the done outlet. Left to right, with the done bang
  // sent last — the carry exception to right-to-left; see the header.
  ADD_OUT_LIST;
  ADD_OUT_BANG;

  ADD_PARAM(dictName);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // A private, empty store to start with, so `store` is never null and no
  // message handler needs a null check. Rebind() trades it for a shared one
  // as soon as there is both a name and a patcher to prefix it with.
  Rebind();

  // The widest pair a store can hold — "<path> <value>" — reserved here on
  // the control thread, so composing one on the walk never allocates.
  emit.reserve(dictStore::KEY_CAPACITY + 1 + dictStore::VALUE_CAPACITY + 1);

  ADD_DESCRIPTION(
      "Outputs a dictionary's key/value pairs one at a time — Max's dict.iter, whose summary is "
      "'stream the content of a dictionary', on the name-addressed value model .dict settled: "
      "the dictionary is bound from the creation argument, \".dict.iter <name>\", because a "
      "dictionary is addressed by name and never passed down a cord. A bang, or the "
      "dictionary's reference message \"dictionary <name>\", emits one \"<path> <value...>\" "
      "list per entry in storage order out the pair outlet, then a bang out the done outlet — "
      "the .uzi/.iter shape applied to structured data, and the object every 'do this for each "
      "entry' patch is built from. Paths are whole \"::\" paths, so what leaves here feeds "
      ".route, .zl and a set-building .prepend unchanged. The walk is a snapshot of the "
      "dictionary as it stood at the trigger: a set or delete arriving mid-walk changes the "
      "dictionary but never the walk in flight, and two .dict.iter on one name walk "
      "independently. A trigger arriving mid-walk is refused and counted, as .uzi refuses a "
      "re-entrant start.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "walk", kInletDoc, "");
  OUTLET_DOC(0, "pair", kPairDoc, "");
  OUTLET_DOC(1, "done", kDoneDoc, "");
  PARAM_DOC("name", "",
            "The dictionary's shared name, addressed as \"<patcherName>.<name>\" — the "
            "dictionary a .dict of the same name in this patcher holds. Resolved once, on the "
            "control thread, which is why no message re-points it at run time. Empty walks a "
            "private, empty dictionary: no pairs, just the done bang.",
            "any identifier");
}

// A re-parse must not leave half of the previous configuration standing:
// Parameters::Set() calls this before parsing and returns early on an empty
// argument string, so this is what makes SetParams("") a real reset —
// dropping the name and going back to a private dictionary. gDict's rule.
PARM_CLEAR() {
  dictName.clear();
  Rebind();
}

PARM_PARSE() {
  Rebind();
}

// `parent` is a patcherImplementation by construction (the patcher hands
// itself to every object via SetParent); the cast mirrors gDict's.
void gDictIter::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  Rebind();
}

void gDictIter::RefreshBinding() {
  Rebind();
}

void gDictIter::Rebind() {
  // No name, or no patcher to prefix it with, means no address — and no
  // address means a private, empty dictionary. See gDict.h for why an
  // unnamed object does not pool on "<patcherName>.".
  std::string address;
  if (!dictName.empty() && parent != nullptr) {
    auto* p = static_cast<patcherImplementation*>(parent);
    address = p->Name() + "." + dictName;
  }

  // Unchanged binding: keep the store. A live SetParams that leaves the name
  // alone must not re-anchor it, and neither must the second Rebind() a
  // Set() makes (clear, then parse).
  if (store != nullptr && address == boundAddress) return;

  bool created = false;
  store = address.empty() ? std::make_shared<dictStore>()
                          : AcquireNamedStore<dictStore>(address, created);
  boundAddress = address;
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  Walk(thread);
}

LIST_IN(ListIn) {
  // The dictionary's reference triggers the walk — the message its .dict
  // emits on a bang. Anything else, including a reference to a dictionary
  // this object is not bound to, is refused: resolving an unrecognised name
  // means the registry's mutex, and this may be the audio thread.
  if (DictReferenceNames(value, dictName)) {
    Walk(thread);
    return;
  }
  Refuse();
}

void gDictIter::Walk(YSE::THREAD thread) {
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

  // The snapshot: the dictionary as it stands right now, copied out under
  // its guard into rows reserved at construction — bounded assigns, no
  // allocation. This is the whole of the time the guard is held, so the
  // pairs' sends run with no guard at all and a downstream `set` into this
  // same dictionary is never the one thing the try-lock drops.
  {
    const dictStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      busy.store(false, std::memory_order_release);
      return;
    }
    snapshot.count = store->count;
    for (std::size_t i = 0; i < store->count; i++) {
      snapshot.entries[i].key.assign(store->entries[i].key);
      snapshot.entries[i].value.assign(store->entries[i].value);
    }
  }

  // One pair per entry, in storage order, each send completing in full
  // before the next — what makes the object a serialiser rather than a
  // scatter, .iter's property. Composed into a buffer reserved at
  // construction: a bounded assign and two appends, no allocation.
  for (std::size_t i = 0; i < snapshot.count; i++) {
    const dictEntry& entry = snapshot.entries[i];
    emit.assign(entry.key);
    if (!entry.value.empty()) {
      emit += ' ';
      emit += entry.value;
    }
    outputs[0].SendList(emit, thread);
  }

  // The done bang, last — the carry exception to right-to-left — and even
  // for an empty dictionary, .uzi's rule for a count of zero. A refused walk
  // never reaches here, so a done bang always means a completed walk.
  outputs[1].SendBang(thread);

  busy.store(false, std::memory_order_release);
}
