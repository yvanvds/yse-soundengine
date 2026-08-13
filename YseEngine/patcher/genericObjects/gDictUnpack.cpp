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
      "A bang unpacks the bound dictionary: each key-path argument is looked up and its value "
      "leaves its outlet, right to left, each send completing in full before the next one "
      "starts. \"dictionary <name>\" does the same when it names the dictionary bound by the "
      "creation argument — the message a .dict's reference outlet emits on a bang and .dict.pack "
      "emits on a pack, so wiring either here gives the pair gesture: pack on one side, and the "
      "values pop out the other. A path the dictionary does not hold sends nothing on its outlet "
      "rather than a zero, .dict's miss rule, and an entry holding nothing sends nothing either. "
      "A reference naming anything else, any other message, or a trigger arriving while an "
      "unpack is already running — a cord looped back from an outlet, or another thread — is "
      "refused and counted rather than logged, since this inlet may be the audio thread.";

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

  ADD_DESCRIPTION(
      "Outputs a dictionary's values on separate outlets — Max's dict.unpack on the "
      "name-addressed value model .dict settled: the dictionary is bound from the first creation "
      "argument and every argument after it is a key path declaring one outlet — \".dict.unpack "
      "in freq gain pan\" — because a dictionary is addressed by name and never passed down a "
      "cord. The dictionary counterpart of .unpack, and .dict.pack's inverse: a structure "
      "arriving as one reference becomes several separate values a patch can wire individually, "
      "so a .dict.pack wired into a .dict.unpack of the same name and paths is the identity. A "
      "trigger — a bang, or the bound dictionary's \"dictionary <name>\" reference, the message "
      "a .dict's reference outlet emits on a bang and .dict.pack emits on a pack — looks each "
      "path up and sends its value out its outlet, right to left, each send completing in full "
      "before the next one starts, so the right-hand values land in cold inlets before the "
      "leftmost sets the result off. Each outlet sends through the family's SendAtom: a value "
      "spelling a single number leaves as the int or float it spells, and anything else — a "
      "symbol, or a whole list, since a dictionary value is list text — leaves as one list "
      "message, .dict.pack's 'a list is a value, not a spread' read backwards. A path the "
      "dictionary does not hold sends nothing on its outlet rather than a zero, .dict's miss "
      "rule, and an entry holding nothing sends nothing either. Paths nest with \"::\", so "
      "\"voice::1::freq\" reads one entry three levels deep. The read is a snapshot: every "
      "path's value is copied out under the store's try-lock guard before anything is sent, so "
      "a set arriving mid-unpack — including from an outlet's own subgraph — changes the "
      "dictionary but not the unpack in flight, and a trigger that loses the guard is refused "
      "whole and counted rather than firing with half a snapshot. A reference naming an unbound "
      "dictionary is refused rather than resolved, a registry lookup being a mutex on whatever "
      "thread the message arrived on. Calculate() does nothing and no message path allocates, "
      "locks or blocks.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "unpack", kInletDoc, "");
  PARAM_DOC("name", "",
            "The dictionary's shared name — the first creation argument, addressed as "
            "\"<patcherName>.<name>\", the dictionary a .dict of the same name in this patcher "
            "holds. Resolved once, on the control thread, which is why no message re-points it "
            "at run time. Empty reads a private, empty dictionary: every path misses and "
            "nothing is sent.",
            "any identifier");
  PARAM_DOC("keys", "",
            "One key path per outlet, in order — the shape of the unpacked dictionary, "
            ".dict.pack's convention read backwards so the pair reads as a pair. Paths nest "
            "with \"::\", Max's own separator, so \"voice::1::freq\" reads one entry three "
            "levels deep. At most 256 paths of at most 128 characters each; a path that does "
            "not fit declares no outlet and is logged, parameter parsing being control-thread "
            "only. A duplicate path is two outlets reading one entry, and both fire. With no "
            "paths the object has no outlets and a trigger reads nothing.",
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
        "The value stored at \"" + key +
            "\" when the object is triggered, typed by its spelling: a single number leaves as "
            "the int or float it spells, anything else — a symbol, or a whole list — leaves as "
            "one list message. Fires right to left with every other outlet, each send "
            "completing in full before the next. A path the dictionary does not hold, or an "
            "entry holding nothing, sends nothing here rather than a zero.",
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
