// `.dict.serialize` (issue #778). See gDictSerialize.h for the design; this
// file is the snapshot, the compact bounded JSON emitter, and one SendList —
// or, for `write <file>` (issue #840), one RequestWrite in the send's place.
#include "gDictSerialize.h"

#include <cstring>

#include "../pObjectList.hpp"
#include "../pSelector.h"
#include "../patcherImplementation.h"

using namespace YSE::PATCHER;

#define className gDictSerialize

namespace {

  // The bounds of the first token of `text`, or false when there is none.
  // Walked in place rather than through substr — gTextfile's helpers, for
  // gTextfile's reason: this runs on whichever thread the message arrived on.
  bool FirstToken(const char* text, std::size_t length, std::size_t& begin, std::size_t& end) {
    begin = 0;
    while (begin < length && IsSelectorSeparator(text[begin]))
      begin++;
    end = begin;
    while (end < length && !IsSelectorSeparator(text[end]))
      end++;
    return end > begin;
  }

  // Trim the separators off both ends of [begin, end).
  void Trim(const char* text, std::size_t& begin, std::size_t& end) {
    while (begin < end && IsSelectorSeparator(text[begin]))
      begin++;
    while (end > begin && IsSelectorSeparator(text[end - 1]))
      end--;
  }

  // Whether the `length` characters at `text` are exactly `word`.
  bool TokenIs(const char* text, std::size_t length, const char* word, std::size_t wordLength) {
    if (length != wordLength) return false;
    for (std::size_t i = 0; i < length; i++) {
      if (text[i] != word[i]) return false;
    }
    return true;
  }

  // Doc strings, hoisted out of CONSTRUCT() because they are long enough that
  // the constructor stops being readable with them inline — gDict's
  // arrangement.
  constexpr char kInletDoc[] =
      "A bang sends the dictionary as JSON text. So does the dictionary's reference 'dictionary "
      "<name>'. 'write <file>' saves the same JSON to a file instead; a bare 'write' writes to "
      "the last file again.";

  constexpr char kOutletDoc[] =
      "The dictionary as one line of compact JSON, in one list message. Nothing is sent when the "
      "text would be longer than 255 characters.";

  constexpr char kNameDoc[] =
      "The name of the dictionary to convert. Empty converts an empty dictionary, which gives {}.";

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

  // The allocations the message paths would otherwise need, taken here on
  // the control thread: the finished document is copied into `emit` for
  // SendList, the compose buffer is a fileScheduler slot's worth of document
  // (#840), and a `write`'s path is remembered in storage that already
  // exists.
  emit.reserve(DOCUMENT_CAPACITY + 1);
  compose = std::make_unique<char[]>(COMPOSE_CAPACITY);
  writePath.reserve(fileScheduler::PATH_CAPACITY);

  // The document is a snapshot composed by a bounded, allocation-free
  // emitter (DictAppendValueJson and friends in gDict.cpp), so no message
  // path allocates, locks or blocks and Calculate() does nothing. 'write'
  // hands the bytes to the patcher's fileScheduler. gDictSerialize.h has the
  // whole design.
  ADD_DESCRIPTION("Sends a named dictionary as one line of JSON text. 'write <file>' saves the "
                  "same JSON to a file instead, for a dictionary too large to send as a "
                  "message.");
  ADD_CATEGORY(pCategory::DICT);
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
  // Control thread, and the one place the patcher's file table can be built:
  // a `write` arriving later on the audio thread has to find it already
  // there (issues #683, #840).
  EnableFileIO();
  Rebind();
}

void gDictSerialize::RefreshBinding() {
  Rebind();
}

void gDictSerialize::Rebind() {
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
  (void)inlet;
  Serialize(thread);
}

LIST_IN(ListIn) {
  (void)inlet;
  // The file route first (issue #840): "write [file]" hands the document to
  // the fileScheduler instead of the outlet. Never ambiguous — the only
  // other message this inlet honours starts with "dictionary".
  std::size_t wordBegin = 0;
  std::size_t wordEnd = 0;
  if (FirstToken(value.c_str(), value.size(), wordBegin, wordEnd) &&
      TokenIs(value.c_str() + wordBegin, wordEnd - wordBegin, "write", 5)) {
    std::size_t begin = wordEnd;
    std::size_t end = value.size();
    Trim(value.c_str(), begin, end);
    WriteFile(value.c_str() + begin, end - begin);
    return;
  }

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

bool gDictSerialize::ComposeDocument() {
  // The snapshot: the dictionary as it stands right now, copied out under
  // its guard into rows reserved at construction — bounded assigns, no
  // allocation. Released before anything else happens, so the store is never
  // held across the send and a concurrent `set` never loses its try-lock to
  // a serialisation. gDictIter's move, and what makes the document atomic.
  {
    const dictStoreGuard guard(store->busy);
    if (!guard.Held()) return false;
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

  // A document the compose buffer cut is no document at all — and the buffer
  // is a full fileScheduler slot wide (#840), so `fit` failing means no
  // delivery route could have carried it either.
  return fit;
}

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

  // Refusal, never truncation: a document the compose buffer cut, or one
  // past what a list payload can carry, is not sent at all — a truncated
  // JSON document parses as a different dictionary or none. See the header
  // for the whole decision.
  if (!ComposeDocument() || composeLength > DOCUMENT_CAPACITY) {
    Refuse();
    busy.store(false, std::memory_order_release);
    return;
  }

  // The send, with no guard but `busy` held: `emit` was reserved at
  // construction, so the assign is a bounded copy, and the whole downstream
  // subgraph runs inside SendList — a loop-back into this object's inlet is
  // refused by `busy` above.
  emit.assign(compose.get(), composeLength);
  emitted.fetch_add(1, std::memory_order_relaxed);
  outputs[0].SendList(emit, thread);

  busy.store(false, std::memory_order_release);
}

// ─── the file route ───────────────────────────────────────────────────────────

void gDictSerialize::WriteFile(const char* name, std::size_t length) {
  // A claim on a patcher-owned slot at the end, and no disk work anywhere:
  // whichever thread is dispatching, no file is opened here (fileScheduler's
  // whole reason). All refusals are counted rather than logged, since this
  // may be the audio callback.
  fileScheduler* io = FileIO();
  if (io == nullptr) {
    // A standalone object has no patcher and so no plumbing.
    Refuse();
    return;
  }

  // The same guard Serialize holds, for the same reason — and it also covers
  // `writePath`, which a concurrent `write` would otherwise rewrite mid-read.
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    Refuse();
    return;
  }

  if (name != nullptr && length > 0) {
    if (length >= fileScheduler::PATH_CAPACITY) {
      Refuse();
      busy.store(false, std::memory_order_release);
      return;
    }
    // assign() into a string reserved at construction reuses its storage.
    writePath.assign(name, length);
  }
  // A bare `write` reuses the last name given; nothing named yet, and no
  // dialog to ask with.
  if (writePath.empty()) {
    Refuse();
    busy.store(false, std::memory_order_release);
    return;
  }

  // The document, composed exactly as the outlet's would be — but bounded by
  // the scheduler slot rather than by a list payload: the compose buffer is
  // a full slot wide, so `fit` failing here means the document is past
  // BYTES_CAPACITY and is refused whole, never truncated (#840).
  if (!ComposeDocument()) {
    Refuse();
    busy.store(false, std::memory_order_release);
    return;
  }

  // RequestWrite copies the bytes into its slot before returning — the
  // requesting object may be gone before they reach the disk — so `busy` can
  // drop the moment this returns. A full table, or a payload the scheduler
  // will not hold, refuses; counted, like every other loss on this path.
  if (!io->RequestWrite(this, FILE_TAG_WRITE, writePath.c_str(), writePath.size(), compose.get(),
                        composeLength)) {
    Refuse();
  }
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
      if (!DictAppendStringJson(seg, segLen, compose.get(), composeLength, COMPOSE_CAPACITY)) {
        fit = false;
      }
      Append(":", 1);
      if (!DictAppendValueJson(snapshot.entries[i].value, compose.get(), composeLength,
                               COMPOSE_CAPACITY, ",", 1)) {
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
    if (!DictAppendStringJson(seg, segLen, compose.get(), composeLength, COMPOSE_CAPACITY)) {
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
  std::memcpy(compose.get() + composeLength, text, length);
  composeLength += length;
}

#undef className
