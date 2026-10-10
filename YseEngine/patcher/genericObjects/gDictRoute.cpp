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
      "A bang routes the dictionary: its reference leaves the outlet of the first key argument "
      "it holds, or the rightmost outlet when it holds none. So does the dictionary's reference "
      "'dictionary <name>'.";

  constexpr char kRejectDoc[] =
      "The dictionary's reference, when it holds none of the keys. Wire it into the next "
      ".dict.route to test more keys.";

  constexpr char kNameDoc[] =
      "The name of the dictionary to route. Empty makes the object do nothing.";

  constexpr char kKeysDoc[] =
      "One key per outlet, in order. A key may be a path ('voice::1'); it matches an entry at "
      "that path or under it. The rightmost outlet is always added for no match.";

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

  // The decision is a bounded scan under the store's try-lock guard,
  // released before the send, and the reference is built once per rebind, so
  // no message path allocates, locks or blocks and Calculate() does nothing.
  // gDictRoute.h has the whole design.
  ADD_DESCRIPTION("Sends a named dictionary's reference out of the outlet of the first key "
                  "argument it holds, or out of the rightmost outlet when it holds none. Use it "
                  "to send dictionaries of different shapes to different parts of a patch.");
  ADD_CATEGORY(pCategory::DICT);
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
    outputs.back().SetDoc(token.empty() ? "empty" : token,
                          "The dictionary's reference, when '" + token +
                              "' is the first key argument it holds, at that path or under it.",
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
