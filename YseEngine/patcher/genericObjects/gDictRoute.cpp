// `.dict.route` (issue #777). See gDictRoute.h for the design; this file is
// one bounded presence scan and one SendList of a reference the object
// already owns.
#include "gDictRoute.h"

#include <cstring>

#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

using namespace YSE::PATCHER;

#define className gDictRoute

namespace {

  constexpr std::size_t kReferenceLength = sizeof(kDictReferenceWord) - 1;

  // Doc strings, hoisted out of CONSTRUCT() because they are long enough that
  // the constructor stops being readable with them inline — gDict's
  // arrangement. Kept at namespace scope so ShapePorts can re-document the
  // ports it rebuilds on a re-parse.
  constexpr char kInletDoc[] =
      "A bang routes the bound dictionary: it is tested for each key argument in order, and the "
      "reference \"dictionary <name>\" leaves the outlet of the leftmost key present — exactly "
      "one outlet fires per trigger, and a dictionary holding none of the keys leaves the "
      "rightmost outlet instead. \"dictionary <name>\" does the same when it names the "
      "dictionary bound by the creation argument — the message a .dict's reference outlet emits "
      "on a bang, so wiring that outlet here gives Max's own gesture: bang the dict, and it "
      "dispatches itself. A reference naming anything else, any other message, or a trigger "
      "arriving while a route is already in flight — a cord looped back from an outlet, or "
      "another thread — is refused and counted rather than logged, since this inlet may be the "
      "audio thread. An unnamed .dict.route is inert: its private dictionary has no name to "
      "pass on, so a trigger routes nothing, silently.";

  constexpr char kRejectDoc[] =
      "The dictionary's reference, when none of the key arguments is present in it — Max's "
      "rightmost outlet, present whatever the argument count. The reference is unchanged, so "
      "chaining this into the next .dict.route carries on testing the dictionary the first one "
      "saw.";

  constexpr char kNameDoc[] =
      "The dictionary's shared name, addressed as \"<patcherName>.<name>\" — the dictionary a "
      ".dict of the same name in this patcher holds. Resolved once, on the control thread, "
      "which is why no message re-points it at run time. Empty leaves the object inert: a "
      "private dictionary has no name to pass on, so there is nothing to route.";

  constexpr char kKeysDoc[] =
      "One key per match outlet, in order, plus a rightmost reject outlet. A key is a path: "
      "usually one top-level segment (\"voice\"), but \"voice::1\" tests two levels down the "
      "same way — presence means an entry at the path or under it (\"<path>::...\"), the "
      "family's bounded sub-tree scan. The leftmost key present wins, and a key repeated in "
      "the argument list uses the leftmost of its outlets only. An empty token, or a key past "
      "128 characters (which no stored entry can spell), matches nothing and costs an "
      "unreachable outlet. With no keys at all the object has only the reject outlet — Max "
      "documents no default key for dict.route, and inventing one would put a branch in a "
      "patch that did not ask for one.";

  // Whether the dictionary holds an entry at `key` or under it — the
  // presence question one match outlet asks. **The caller holds the store's
  // guard.** A bounded scan of at most MAX_ENTRIES rows, allocation-free, so
  // it is safe on whichever thread the trigger was dispatched on. An empty
  // key matches nothing: no stored entry has an empty path, and treating it
  // as a universal prefix would make an empty argument a match-all.
  bool KeyPresent(const dictStore& store, const std::string& key) {
    if (key.empty() || key.size() > dictStore::KEY_CAPACITY) return false;
    for (std::size_t i = 0; i < store.count; i++) {
      const std::string& path = store.entries[i].key;
      if (path.size() < key.size()) continue;
      if (std::memcmp(path.data(), key.data(), key.size()) != 0) continue;
      // At the path exactly, or under it: the next two characters are the
      // separator. Anything else ("voicing" against "voice") is a different
      // key that merely shares a prefix.
      if (path.size() == key.size()) return true;
      if (path.size() >= key.size() + 2 && path[key.size()] == ':' && path[key.size() + 1] == ':')
        return true;
    }
    return false;
  }

} // namespace

CONSTRUCT() {
  // One inlet, as in Max. No int or float handler: a bare number names no
  // dictionary — the family's rule.
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
  RefreshReference();

  // The outlets are built by ShapePorts(), because the argument list *is*
  // the outlet list — gRoute's arrangement.
  ShapePorts();

  ADD_DESCRIPTION(
      "Routes a dictionary by the keys it holds — Max's dict.route on the name-addressed value "
      "model .dict settled: the dictionary is bound from the first creation argument and every "
      "argument after it is a key declaring one outlet, plus a rightmost reject — \".dict.route "
      "<name> <key> [<key> ...]\" — because a dictionary is addressed by name and never passed "
      "down a cord. The dispatcher for structured messages: a patch that receives dictionaries "
      "of several shapes sends each to the part of the graph that understands it, which is what "
      ".route does for list text and this object does for dictionaries. A trigger — a bang, or "
      "the bound dictionary's \"dictionary <name>\" reference, the message a .dict's reference "
      "outlet emits — tests the dictionary for each key in argument order and sends the "
      "reference, never the contents, out the outlet of the leftmost key present: the routing "
      "decision is about which dictionary, and the receiver still binds the name itself. "
      "Exactly one outlet fires per trigger; a dictionary holding none of the keys leaves the "
      "rightmost outlet with its reference unchanged, so a chain of .dict.route objects strings "
      "together with each reject feeding the next inlet. A key is a path — \"voice\" tests a "
      "top-level key, \"voice::1\" tests two levels down — and presence means an entry at the "
      "path or under it, the family's bounded sub-tree scan. With no keys the object has only "
      "the reject outlet, since Max documents no default key for dict.route. The decision is a "
      "bounded scan under the store's try-lock guard, released before the send, and the "
      "reference is built once per rebind, so no message path allocates, locks or blocks and "
      "Calculate() does nothing. A trigger that loses the guard, a reference naming an unbound "
      "dictionary, and a trigger arriving while a route is in flight are refused and counted "
      "rather than resolved, a registry lookup being a mutex on whatever thread the message "
      "arrived on. Only the creation arguments persist across a save; the dictionary's contents "
      "persist with the .dict that owns them.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "route", kInletDoc, "");
  PARAM_DOC("name", "", kNameDoc, "any identifier");
  PARAM_DOC("keys", "", kKeysDoc, "key paths");
}

// ─── creation arguments ───────────────────────────────────────────────────────

// A re-parse must not leave half of the previous configuration standing:
// Parameters::Set() calls this before parsing and returns early on an empty
// argument string, so this is what makes SetParams("") a real reset —
// dropping the name and the keys, going back to an inert object with only
// the reject outlet. gDict's rule, plus gRoute's for the ports.
PARM_CLEAR() {
  dictName.clear();
  keyArgs.clear();
  Rebind();
  RefreshReference();
  ShapePorts();
}

PARM_PARSE() {
  Rebind();
  RefreshReference();
  ShapePorts();
}

// `parent` is a patcherImplementation by construction (the patcher hands
// itself to every object via SetParent); the cast mirrors gDict's.
void gDictRoute::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  Rebind();
}

void gDictRoute::RefreshBinding() {
  Rebind();
}

void gDictRoute::Rebind() {
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

void gDictRoute::RefreshReference() {
  reference.clear();
  if (dictName.empty()) return;
  reference.reserve(kReferenceLength + 1 + dictName.size());
  reference += kDictReferenceWord;
  reference += ' ';
  reference += dictName;
}

void gDictRoute::ShapePorts() {
  // Rebuilt rather than resized, because the reject outlet has to stay
  // rightmost: appending a match outlet would put it after the reject and
  // every saved cord past that point would land on the wrong port. Safe
  // because every caller runs before the object is wired or published — the
  // constructor, and the two parameter callbacks, which
  // patcherImplementation::CreateObjectUnlocked runs before AssignGraphIds.
  // A *live* SetParams never reaches here on a published object —
  // registering the callbacks makes ParamsNeedRebuild() true, so #234
  // replaces the object.
  outputs.clear();

  // Every token kept, empty ones included, so an index into the arguments
  // is an index into `outputs` — gRoute's rule for an empty selector. An
  // empty or over-long key can never be present, so it costs an unreachable
  // outlet and nothing else.
  keys.clear();
  keys.reserve(keyArgs.size());
  for (const std::string& token : keyArgs) {
    keys.push_back(token);

    // LIST, because the reference is a list message — the kind every other
    // reference outlet in the family carries.
    ADD_OUT_LIST;
    outputs.back().SetDoc(
        token.empty() ? "empty" : token,
        "The dictionary's reference, when \"" + token +
            "\" is the leftmost key argument present in it — an entry at that path or under "
            "it. What leaves is \"dictionary <name>\", never the contents: the receiver binds "
            "the name itself. A key repeated in the argument list uses the leftmost of its "
            "outlets only.",
        token);
  }

  // Max's rightmost outlet, present whatever the argument count. The
  // reference leaves it unchanged, which is what lets a chain of
  // .dict.route objects be strung together with each reject feeding the
  // next inlet.
  ADD_OUT_LIST;
  outputs.back().SetDoc("none", kRejectDoc, "");
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  Route(thread);
}

LIST_IN(ListIn) {
  (void)inlet;
  // The dictionary's reference triggers the route — the message its .dict
  // emits on a bang. Anything else, including a reference to a dictionary
  // this object is not bound to, is refused: resolving an unrecognised name
  // means the registry's mutex, and this may be the audio thread.
  if (DictReferenceNames(value, dictName)) {
    Route(thread);
    return;
  }
  Refuse();
}

// ─── the route ────────────────────────────────────────────────────────────────

void gDictRoute::Route(YSE::THREAD thread) {
  // An unnamed object is inert: its private dictionary has no name to pass
  // on — gDict's rule for an unnamed reference — so there is nothing any
  // outlet could carry. Silent rather than counted, because the wiring is
  // not an error, merely incomplete.
  if (reference.empty()) return;

  // The re-entrancy guard, held across decision and send: the emitted
  // reference is itself a trigger, so an outlet wired back into the inlet —
  // directly or round a chain — would recurse without bound on whatever
  // thread the trigger arrived on. The loser is dropped and counted rather
  // than made to spin, this being a path the audio callback takes.
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    Refuse();
    return;
  }

  // The decision, under the store's guard alone: the leftmost key present.
  // A bounded scan — at most MAX_ENTRIES rows per key — and nothing else,
  // so the guard is held for exactly the question. Released before the
  // send, which runs the whole downstream subgraph and may well store into
  // this same dictionary; inside the guard that store would be the one
  // thing the try-lock drops.
  int hit = -1;
  {
    const dictStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      busy.store(false, std::memory_order_release);
      return;
    }
    for (std::size_t i = 0; i < keys.size(); i++) {
      if (KeyPresent(*store, keys[i])) {
        hit = (int)i;
        break;
      }
    }
  }

  // Exactly one outlet fires per trigger — the matched one, or the reject,
  // which is always outputs.back(). The send is of a string the object
  // already owns, with no guard but `busy` held.
  routed.fetch_add(1, std::memory_order_relaxed);
  if (hit >= 0) {
    outputs[(std::size_t)hit].SendList(reference, thread);
  } else {
    outputs.back().SendList(reference, thread);
  }

  busy.store(false, std::memory_order_release);
}

// ─── diagnostics ──────────────────────────────────────────────────────────────

std::string gDictRoute::KeyAt(std::size_t index) const {
  if (index >= keys.size()) return std::string();
  return keys[index];
}

#undef className
