// `.dict.serialize` (issue #778). See gDictSerialize.h for the design; this
// file is the snapshot, the compact bounded JSON emitter, and one SendList.
#include "gDictSerialize.h"

#include <cstring>

#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

using namespace YSE::PATCHER;

#define className gDictSerialize

namespace {

  // Doc strings, hoisted out of CONSTRUCT() because they are long enough that
  // the constructor stops being readable with them inline — gDict's
  // arrangement.
  constexpr char kInletDoc[] =
      "A bang serialises the bound dictionary: the whole dictionary leaves the outlet as one "
      "list message holding a single-line compact JSON object. \"dictionary <name>\" does the "
      "same when it names the dictionary bound by the creation argument — the message a .dict's "
      "reference outlet emits on a bang, so wiring that outlet here gives Max's own gesture: "
      "bang the dict, out comes its text. A reference naming anything else, any other message, "
      "or a trigger arriving while a document is already being composed — a cord looped back "
      "from the outlet, or another thread — is refused and counted rather than logged, since "
      "this inlet may be the audio thread.";

  constexpr char kOutletDoc[] =
      "The document: one JSON object on one line, no whitespace outside strings — \"::\" paths "
      "expanded into real nesting, members in storage order of first appearance, values typed by "
      "the patcher's own classifier (a single numeric token as a number, a multi-token value as "
      "an array, an empty value as \"\", anything else as an escaped string), an empty "
      "dictionary as {}. Exactly the document DictToJson builds for a saved patch, and exactly "
      "what .dict.deserialize parses back — the dictionary's interchange form. The document is "
      "a snapshot of the dictionary as it stood at the trigger. A finished document longer than "
      "255 characters — the longest list payload the patcher's value queue carries — is refused "
      "whole and counted, never truncated: a truncated document is not a shorter document, it "
      "parses as a different one. Route a dictionary that large through its parts instead "
      "(.dict.iter), or keep it in the store it lives in.";

  constexpr char kNameDoc[] =
      "The dictionary's shared name, addressed as \"<patcherName>.<name>\" — the dictionary a "
      ".dict of the same name in this patcher holds. Resolved once, on the control thread, "
      "which is why no message re-points it at run time. Empty binds a private, empty "
      "dictionary — the document is then {}.";

} // namespace

CONSTRUCT() {
  // One inlet, as in Max. No int or float handler: a bare number names no
  // dictionary — the family's rule.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);

  // The document outlet.
  ADD_OUT_LIST;

  ADD_PARAM(dictName);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // A private, empty store to start with, so `store` is never null and no
  // message handler needs a null check. Rebind() trades it for a shared one
  // as soon as there is both a name and a patcher to prefix it with.
  Rebind();

  // The one allocation the send path would otherwise need, taken here on the
  // control thread: the finished document is copied into this for SendList.
  emit.reserve(DOCUMENT_CAPACITY + 1);

  ADD_DESCRIPTION(
      "Serialises a dictionary to text — Max's dict.serialize ('convert a dictionary into a "
      "single symbol') on the name-addressed value model .dict settled. The write half of the "
      "interchange pair whose read half is .dict.deserialize: a dictionary becomes a JSON "
      "string a host, a .textedit, a .s/.r pair or the live-coding DSL can carry — what makes a "
      "dictionary the patcher's interchange format rather than only its store. The dictionary "
      "is bound from the creation argument, \".dict.serialize <name>\", resolved once on the "
      "control thread; a bang, or the dictionary's reference message \"dictionary <name>\", "
      "sends the whole dictionary out the outlet as one list message holding a single-line "
      "compact JSON object — \"::\" paths expanded into real nesting, members in storage order "
      "of first appearance, no whitespace outside strings, values typed by the patcher's own "
      "classifier: a single numeric token as a number, a multi-token value as an array, an "
      "empty value as \"\", anything else as an escaped string. Exactly the document DictToJson "
      "builds for a saved patch, so what leaves here parses anywhere JSON parses. The document "
      "is a snapshot of the dictionary as it stood at the trigger, composed by a bounded "
      "allocation-free emitter, so no message path allocates, locks or blocks and Calculate() "
      "does nothing. A finished document longer than 255 characters — the longest list payload "
      "the patcher's value queue carries — is refused whole and counted, never truncated: a "
      "truncated JSON document parses as a different dictionary or none at all. Only the "
      "creation argument persists across a save; the dictionary's contents persist with the "
      ".dict that owns them.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "serialize", kInletDoc, "");
  OUTLET_DOC(0, "json", kOutletDoc, "");
  PARAM_DOC("name", "", kNameDoc, "any identifier");
}

// ─── creation arguments ───────────────────────────────────────────────────────

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
void gDictSerialize::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  Rebind();
}

void gDictSerialize::RefreshBinding() {
  Rebind();
}

void gDictSerialize::Rebind() {
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
  (void)inlet;
  Serialize(thread);
}

LIST_IN(ListIn) {
  (void)inlet;
  // The dictionary's reference triggers the serialisation — the message its
  // .dict emits on a bang. Anything else, including a reference to a
  // dictionary this object is not bound to, is refused: resolving an
  // unrecognised name means the registry's mutex, and this may be the audio
  // thread.
  if (DictReferenceNames(value, dictName)) {
    Serialize(thread);
    return;
  }
  Refuse();
}

// ─── the document ─────────────────────────────────────────────────────────────

void gDictSerialize::Serialize(YSE::THREAD thread) {
  // The re-entrancy guard, held across snapshot, composition and send: a
  // trigger looping back from the outlet — or arriving from another thread
  // mid-composition — would rewrite the snapshot and the compose buffer
  // under themselves. The loser is dropped and counted rather than made to
  // spin, this being a path the audio callback takes.
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    Refuse();
    return;
  }

  // The snapshot: the dictionary as it stands right now, copied out under
  // its guard into rows reserved at construction — bounded assigns, no
  // allocation. Released before anything else happens, so the store is never
  // held across the send and a concurrent `set` never loses its try-lock to
  // a serialisation. gDictIter's move, and what makes the document atomic.
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

  std::memset(consumed, 0, sizeof(bool) * snapshot.count);
  composeLength = 0;
  fit = true;

  Append("{", 1);
  EmitLevel(0);
  Append("}", 1);

  // Refusal, never truncation: a document the compose buffer cut, or one
  // past what a list payload can carry, is not sent at all — a truncated
  // JSON document parses as a different dictionary or none. See the header
  // for the whole decision.
  if (!fit || composeLength > DOCUMENT_CAPACITY) {
    Refuse();
    busy.store(false, std::memory_order_release);
    return;
  }

  // The send, with no guard but `busy` held: `emit` was reserved at
  // construction, so the assign is a bounded copy, and the whole downstream
  // subgraph runs inside SendList — a loop-back into this object's inlet is
  // refused by `busy` above.
  emit.assign(compose, composeLength);
  emitted.fetch_add(1, std::memory_order_relaxed);
  outputs[0].SendList(emit, thread);

  busy.store(false, std::memory_order_release);
}

void gDictSerialize::EmitLevel(std::size_t prefixLength) {
  // Which member of this level is being emitted — the comma goes in front of
  // every one but the first, which is all the lookahead compact JSON needs.
  bool first = true;

  for (std::size_t i = 0; i < snapshot.count; i++) {
    if (consumed[i]) continue;
    const std::string& key = snapshot.entries[i].key;
    if (key.size() < prefixLength) continue;
    if (prefixLength > 0 && std::memcmp(key.data(), prefix, prefixLength) != 0) continue;

    // The next path segment: up to the first "::" after the prefix. An empty
    // one — "a::" run out, or "::b" starting on the separator — is not a path
    // this store can express as JSON, so the entry is skipped rather than
    // producing an unnamed member. DictToJson's rule.
    const std::size_t remain = key.size() - prefixLength;
    std::size_t segLen = remain;
    for (std::size_t s = 0; s + 1 < remain; s++) {
      if (key[prefixLength + s] == ':' && key[prefixLength + s + 1] == ':') {
        segLen = s;
        break;
      }
    }
    if (segLen == 0) {
      consumed[i] = true;
      continue;
    }
    const char* seg = key.data() + prefixLength;
    const bool isLeaf = (segLen == remain);

    if (isLeaf) {
      // A leaf, and the earliest entry of its segment (the scan is storage
      // order): it wins over any later-stored "seg::..." sub-tree, which is
      // DictToJson's collision rule — storage order decides, .coll's rule
      // for a duplicate address. The losers are consumed unemitted.
      if (!first) Append(",", 1);
      if (!DictAppendStringJson(seg, segLen, compose, composeLength, COMPOSE_CAPACITY)) {
        fit = false;
      }
      Append(":", 1);
      if (!DictAppendValueJson(snapshot.entries[i].value, compose, composeLength, COMPOSE_CAPACITY,
                               ",", 1)) {
        fit = false;
      }
      first = false;
      consumed[i] = true;

      for (std::size_t j = i + 1; j < snapshot.count; j++) {
        if (consumed[j]) continue;
        const std::string& other = snapshot.entries[j].key;
        if (other.size() < prefixLength + segLen + 2) continue;
        if (std::memcmp(other.data(), key.data(), prefixLength + segLen) != 0) continue;
        if (other[prefixLength + segLen] != ':' || other[prefixLength + segLen + 1] != ':')
          continue;
        consumed[j] = true;
      }
      continue;
    }

    // A branch, and the earliest of its segment: a later-stored leaf of the
    // same name loses to it — the mirror collision, same rule.
    for (std::size_t j = i + 1; j < snapshot.count; j++) {
      if (consumed[j]) continue;
      const std::string& other = snapshot.entries[j].key;
      if (other.size() != prefixLength + segLen) continue;
      if (std::memcmp(other.data(), key.data(), prefixLength + segLen) != 0) continue;
      consumed[j] = true;
    }

    if (!first) Append(",", 1);
    if (!DictAppendStringJson(seg, segLen, compose, composeLength, COMPOSE_CAPACITY)) {
      fit = false;
    }
    Append(":{", 2);

    // Descend: the prefix grows by this segment and its separator, and every
    // entry under it — this one included — is consumed by the recursion.
    // Bounded: the prefix never outgrows a key that already fit KEY_CAPACITY,
    // and the depth never exceeds the segments such a key can spell. A branch
    // whose members all turn out invalid closes as "seg":{} — DictToJson's
    // document for the same store.
    std::memcpy(prefix + prefixLength, seg, segLen);
    prefix[prefixLength + segLen] = ':';
    prefix[prefixLength + segLen + 1] = ':';
    EmitLevel(prefixLength + segLen + 2);

    Append("}", 1);
    first = false;
  }
}

void gDictSerialize::Append(const char* text, std::size_t length) {
  if (composeLength + length > COMPOSE_CAPACITY - 2) {
    fit = false;
    return;
  }
  std::memcpy(compose + composeLength, text, length);
  composeLength += length;
}

#undef className
