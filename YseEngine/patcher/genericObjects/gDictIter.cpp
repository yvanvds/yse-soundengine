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
      "A bang walks the dictionary: one pair per entry out of the left outlet, then a bang out "
      "of the right one. So does the dictionary's reference 'dictionary <name>'.";
  constexpr char kPairDoc[] =
      "One entry per message: its whole '::' path followed by its value, in the order the "
      "entries were first written. An entry with an empty value leaves as its path alone.";
  constexpr char kDoneDoc[] = "Bang after the last pair, also for an empty dictionary.";

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

  ADD_DESCRIPTION("Walks a named dictionary, sending one '<path> <value>' list per entry, then a "
                  "bang from the right outlet when it is done.");
  ADD_CATEGORY(pCategory::DICT);
  INLET_DOC(0, "walk", kInletDoc, "");
  OUTLET_DOC(0, "pair", kPairDoc, "");
  OUTLET_DOC(1, "done", kDoneDoc, "");
  PARAM_DOC("name", "",
            "The name of the dictionary to walk. Empty walks an empty dictionary: no pairs, only "
            "the done bang.",
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
  // unnamed object does not pool on "patcher.<patcherName>.".
  std::string address;
  if (!dictName.empty() && parent != nullptr) {
    auto* p = static_cast<patcherImplementation*>(parent);
    address = p->ScopedAddress(dictName);
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
