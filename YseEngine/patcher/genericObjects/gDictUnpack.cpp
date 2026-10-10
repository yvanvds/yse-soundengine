// `.dict.unpack` (issue #781). See gDictUnpack.h for the design; this file
// is one bounded snapshot and a right-to-left row of SendAtom calls.
#include "gDictUnpack.h"
#include "../../implementations/logImplementation.h"
#include "../pAtomList.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

#include <cstring>
#include <string>

using namespace YSE::PATCHER;
#define className gDictUnpack

namespace {

  // An int as text, through the patcher's one int formatter rather than
  // std::to_string. Control-thread only (the construction-time logs).
  std::string IntText(int value) {
    char digits[YSE::PATCHER::FORMAT_INT_WIDTH];
    const std::size_t written = YSE::PATCHER::WriteInt(value, digits);
    return std::string(digits, written);
  }

  // Doc strings, hoisted out of CONSTRUCT() because they are long enough that
  // the constructor stops being readable with them inline — gDict's
  // arrangement.
  constexpr char kInletDoc[] =
      "A bang sends the value of each key path out of its outlet, right to left. So does the "
      "dictionary's reference 'dictionary <name>'.";

} // namespace

CONSTRUCT() {
  // One inlet, as in Max. No int or float handler: a bare number names no
  // dictionary — the family's rule, and an object that fanned out up to 256
  // sends on any stray number that reached it would be a trap rather than a
  // convenience.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);

  ADD_PARAM(dictName);
  ADD_PARAM(keyArgs);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // A private, empty store to start with, so `store` is never null and no
  // message handler needs a null check. Rebind() trades it for a shared one
  // as soon as there is both a name and a patcher to prefix it with.
  Rebind();

  // Every outlet is built by ShapePorts(), because the argument list *is*
  // the outlet list — gDictPack's arrangement, read in the opposite
  // direction.
  ShapePorts();

  // The one allocation the object makes outside its slots, and it happens
  // here rather than on a trigger: SendAtom's symbol path renders into
  // storage that is already long enough, on whichever thread the trigger
  // came in on — routinely the audio callback.
  AtomList::ReserveRender(render);

  // The read is a snapshot taken under the store's try-lock guard before
  // anything is sent, into slots sized at parse time, so no message path
  // allocates, locks or blocks and Calculate() does nothing. gDictUnpack.h
  // has the whole design.
  ADD_DESCRIPTION("Sends the values at a named dictionary's key paths out of separate outlets, one "
                  "outlet per key path argument, right to left. The inverse of .dict.pack.");
  ADD_CATEGORY(pCategory::DICT);
  INLET_DOC(0, "unpack", kInletDoc, "");
  PARAM_DOC("name", "",
            "The name of the dictionary to read. Empty reads an empty dictionary, so nothing is "
            "sent.",
            "any identifier");
  PARAM_DOC("keys", "",
            "One key path per outlet, in order. Paths nest with '::'. A path longer than 128 "
            "characters is logged and gets no outlet.",
            "key paths");
}

// ─── the creation arguments ───────────────────────────────────────────────────

// A re-parse must not leave half of the previous configuration standing:
// Parameters::Set() calls this before parsing and returns early on an empty
// argument string, so this is what makes SetParams("") a real reset —
// dropping the name and the keys, going back to a private store and no
// outlets. gDict's rule, plus gDictPack's for the ports.
PARM_CLEAR() {
  dictName.clear();
  keyArgs.clear();
  Rebind();
  ShapePorts();
}

PARM_PARSE() {
  Rebind();
  ShapePorts();
}

// `parent` is a patcherImplementation by construction (the patcher hands
// itself to every object via SetParent); the cast mirrors gDict's.
void gDictUnpack::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  Rebind();
}

void gDictUnpack::RefreshBinding() {
  Rebind();
}

void gDictUnpack::Rebind() {
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

void gDictUnpack::ShapePorts() {
  // Rebuilt rather than resized: the key count *is* the outlet count, so
  // the outlets, the key list and the value slots all have to agree. Safe
  // because every caller runs before the object is wired or published — the
  // constructor, and the two parameter callbacks, which
  // patcherImplementation::CreateObjectUnlocked runs before AssignGraphIds.
  keys.clear();
  slots.clear();

  // Parameters::Set splits on single spaces, so a run of them yields empty
  // tokens; an empty token declares no key. gDictPack's validation exactly,
  // so ".dict.pack <name> <paths>" and ".dict.unpack <name> <paths>" build
  // parallel ports from one argument list.
  int requested = 0;
  for (const std::string& token : keyArgs) {
    if (!token.empty()) requested++;
  }

  for (const std::string& token : keyArgs) {
    if (token.empty()) continue;
    if (keys.size() >= MAX_KEYS) break;
    // Refused, never truncated: a clipped path would silently read a key
    // nobody spelled. The skip is logged below — this is the control
    // thread, the one place a refusal can be *said*.
    if (token.size() > KEY_CAPACITY) continue;
    keys.push_back(token);
  }
  slots.resize(keys.size());

  if ((int)keys.size() != requested) {
    INTERNAL::LogImpl().emit(E_WARNING, std::string("patcher: ") + Type() + " asked for " +
                                            IntText(requested) + " key paths; only " +
                                            IntText((int)keys.size()) + " fitted (at most " +
                                            IntText((int)MAX_KEYS) + " paths of at most " +
                                            IntText((int)KEY_CAPACITY) + " characters each)");
  }

  outputs.clear();

  // One outlet per key path. ANY, because a stored value leaves as whatever
  // it spells — an int, a float, or list text — SendAtom's classification.
  for (const std::string& key : keys) {
    ADD_OUT_ANY;
    // The label is the key path itself — what this outlet reads is the one
    // thing worth saying about it.
    outputs.back().SetDoc(
        key,
        "The value at '" + key +
            "': one number leaves as an int or a float, anything else as a list. Nothing is sent "
            "when the path is missing or its value is empty.",
        "any");
  }
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  Unpack(thread);
}

LIST_IN(ListIn) {
  (void)inlet;
  // The dictionary's reference triggers the unpack — the message its .dict
  // emits on a bang and its .dict.pack emits on a pack. Anything else,
  // including a reference to a dictionary this object is not bound to, is
  // refused: resolving an unrecognised name means the registry's mutex, and
  // this may be the audio thread.
  if (DictReferenceNames(value, dictName)) {
    Unpack(thread);
    return;
  }
  Refuse();
}

// ─── the unpack ───────────────────────────────────────────────────────────────

void gDictUnpack::Unpack(YSE::THREAD thread) {
  // Nothing declared, nothing to read: with no key paths there are no
  // outlets, so the trigger is acknowledged silently — the wiring is not an
  // error, merely incomplete.
  if (keys.empty()) return;

  // The re-entrancy guard, held across snapshot and sends: this object
  // emits in a loop, so a cord from an outlet back to the inlet re-enters
  // here from inside the walk, and letting it through would rewrite the
  // very slots being sent. The loser — the loop-back, or another thread —
  // is dropped and counted rather than made to spin, this being a path the
  // audio callback takes.
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    Refuse();
    return;
  }

  // The snapshot: every path's value as it stands right now, copied out
  // under the store's guard into slots sized at parse time — bounded
  // lookups and copies, no allocation. This is the whole of the time the
  // guard is held, so the sends run with no dict guard at all and a
  // downstream `set` into this same dictionary is never the one thing the
  // try-lock drops. Refused whole on a lost guard: no outlet fires with
  // half a snapshot.
  {
    const dictStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      busy.store(false, std::memory_order_release);
      return;
    }
    for (std::size_t i = 0; i < keys.size(); i++) {
      fetchSlot& slot = slots[i];
      const std::size_t at = DictFind(*store, keys[i].data(), keys[i].size());
      if (at < store->count) {
        const std::string& stored = store->entries[at].value;
        slot.length = stored.size();
        std::memcpy(slot.text, stored.data(), slot.length);
        slot.text[slot.length] = 0;
        slot.present = true;
      } else {
        slot.length = 0;
        slot.present = false;
      }
    }
  }

  // Right to left, and each send returns only once the whole subgraph
  // behind that outlet has run — gUnpack's ordering, Max's universal order:
  // the loop counts down. A miss sends nothing rather than a zero — .dict's
  // miss rule — and an entry holding nothing sends nothing either, the rule
  // that an object with nothing to say says nothing. What does fire goes
  // through the family's shared SendAtom, so a number leaves as a number
  // and everything else as list text, verbatim.
  unpacked.fetch_add(1, std::memory_order_relaxed);
  for (std::size_t i = keys.size(); i > 0; i--) {
    const fetchSlot& slot = slots[i - 1];
    if (!slot.present || slot.length == 0) continue;
    SendAtom(outputs[i - 1], slot.text, slot.length, render, thread);
  }

  busy.store(false, std::memory_order_release);
}

// ─── diagnostics ──────────────────────────────────────────────────────────────

std::string gDictUnpack::KeyAt(std::size_t index) const {
  if (index >= keys.size()) return std::string();
  return keys[index];
}

#undef className
