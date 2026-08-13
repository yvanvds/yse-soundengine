#include "gDict.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

#include <cstdlib>
#include <cstring>
#include <string>

using namespace YSE::PATCHER;
#define className gDict

namespace {

  // The separator, and its length, without spelling `2` anywhere.
  constexpr std::size_t kSeparatorLength = sizeof(kDictPathSeparator) - 1;
  constexpr std::size_t kReferenceLength = sizeof(kDictReferenceWord) - 1;

  // Reads the key path at `offset`, leaving `offset` on the first character
  // after it. False when there is no path there. No allocation — the caller
  // works with the slice rather than with a copy of it.
  bool ReadPath(const std::string& text, std::size_t& offset, std::size_t& begin,
                std::size_t& length) {
    std::size_t i = offset;
    while (i < text.size() && IsSelectorSeparator(text[i]))
      i++;
    if (i >= text.size()) return false;
    begin = i;
    while (i < text.size() && !IsSelectorSeparator(text[i]))
      i++;
    length = i - begin;
    offset = i;
    return true;
  }

  // ─── value typing, on the way into JSON ─────────────────────────────────────
  //
  // Control thread only (DictToJson is). The token has already been agreed to
  // be a whole finite number by ReadNumericToken; what is left is to spell it
  // as JSON without going through the float the patcher classified it with.
  // `strtod` / `strtoll` rather than that float because a stored "0.1" must come
  // back as 0.1 rather than as the double nearest to the float nearest to 0.1,
  // and an integer past 2^24 must not be rounded on the way through.
  nlohmann::json TokenToJson(const char* text, std::size_t length) {
    float number = 0.f;
    if (!ReadNumericToken(text, length, number)) {
      return nlohmann::json(std::string(text, length));
    }
    const std::string token(text, length);
    if (TokenLooksLikeFloat(text, length)) {
      return nlohmann::json(std::strtod(token.c_str(), nullptr));
    }
    return nlohmann::json(static_cast<std::int64_t>(std::strtoll(token.c_str(), nullptr, 10)));
  }

  // A stored value is list text. One token becomes the scalar it spells, several
  // become an array of those, and nothing at all becomes an empty string — which
  // is what `set <key>` with no value stored.
  nlohmann::json ValueToJson(const std::string& value) {
    std::size_t begin = 0;
    std::size_t i = 0;
    int tokens = 0;
    std::size_t firstBegin = 0;
    std::size_t firstLength = 0;

    // One pass to count, so the single-token case never builds an array.
    while (i < value.size()) {
      while (i < value.size() && IsSelectorSeparator(value[i]))
        i++;
      if (i >= value.size()) break;
      begin = i;
      while (i < value.size() && !IsSelectorSeparator(value[i]))
        i++;
      if (tokens == 0) {
        firstBegin = begin;
        firstLength = i - begin;
      }
      tokens++;
    }

    if (tokens == 0) return nlohmann::json(std::string());
    if (tokens == 1) return TokenToJson(value.c_str() + firstBegin, firstLength);

    nlohmann::json list = nlohmann::json::array();
    i = 0;
    while (i < value.size()) {
      while (i < value.size() && IsSelectorSeparator(value[i]))
        i++;
      if (i >= value.size()) break;
      begin = i;
      while (i < value.size() && !IsSelectorSeparator(value[i]))
        i++;
      list.push_back(TokenToJson(value.c_str() + begin, i - begin));
    }
    return list;
  }

  // ─── value typing, on the way back out of JSON ──────────────────────────────

  void AppendScalarText(const nlohmann::json::value_type& value, std::string& out) {
    if (value.is_null()) return;
    if (value.is_string()) {
      out += value.get<std::string>();
      return;
    }
    if (value.is_boolean()) {
      out += value.get<bool>() ? '1' : '0';
      return;
    }
    if (value.is_number_integer() || value.is_number_unsigned()) {
      out += std::to_string(value.get<std::int64_t>());
      return;
    }
    // A real, spelled the way JSON spells it — so a stored "120." comes back as
    // "120.0". The same number, and stable from the second round trip on.
    out += value.dump();
  }

  void JsonLeafText(const nlohmann::json::value_type& value, std::string& out) {
    out.clear();
    if (!value.is_array()) {
      AppendScalarText(value, out);
      return;
    }
    for (const auto& element : value) {
      // A nested array or object inside an array has no list-text spelling;
      // dropping it keeps the rest of the list readable.
      if (element.is_array() || element.is_object()) continue;
      if (!out.empty()) out += ' ';
      AppendScalarText(element, out);
    }
  }

  // Walk a nested document, writing one flat "a::b::c" entry per leaf.
  void FlattenJson(const nlohmann::json::value_type& node, std::string& path, dictStore& store) {
    for (auto it = node.begin(); it != node.end(); ++it) {
      const std::size_t mark = path.size();
      if (!path.empty()) path += kDictPathSeparator;
      path += it.key();

      if (it->is_object()) {
        FlattenJson(*it, path, store);
      } else {
        std::string text;
        JsonLeafText(*it, text);
        // Over-long paths and values are dropped and the rest of the document
        // still loads — .coll's rule for an over-long record.
        DictStoreAt(store, path.c_str(), path.size(), text.c_str(), text.size());
      }
      path.resize(mark);
    }
  }

  // Doc strings, hoisted out of CONSTRUCT() because they are long enough that
  // the constructor stops being readable with them inline — .coll's arrangement.
  constexpr char kInletDoc[] =
      "\"set <path> <value...>\" stores a value; \"get <path>\" fetches one out outlet 0 or bangs "
      "outlet 2 when there is none; \"delete <path>\" removes one; \"clear\" empties the "
      "dictionary; \"getsize\" reports the entry count and \"getkeys\" the top-level keys out "
      "outlet 0. A bang emits this dictionary's reference out outlet 1. Paths nest with \"::\", "
      "Max's own separator, so \"voice::1::freq\" is one entry three levels deep. Anything "
      "refused — an unknown message, a path past 128 characters, a value past 256, or a store "
      "into a full dictionary — is counted rather than logged, since this inlet may be the audio "
      "thread.";
  constexpr char kDataDoc[] =
      "The value fetched by \"get\", typed the way the patcher spells it: a single numeric token "
      "leaves as an int or a float by its spelling and anything else as a list. Also the int "
      "\"getsize\" reports and the key list \"getkeys\" reports.";
  constexpr char kReferenceDoc[] =
      "\"dictionary <name>\" on a bang — the reference the dict.* family binds. It carries the "
      "*local* name, because the object receiving it prefixes that with its own patcher's name "
      "exactly as this one does. Silent for an unnamed dictionary, which has no name to pass on.";
  constexpr char kMissDoc[] =
      "Bang when a \"get\" names a path the dictionary does not hold — the miss, kept off outlet 0 "
      "so a patch can tell \"no such key\" from a key holding nothing.";

} // namespace

// ─── the store ────────────────────────────────────────────────────────────────

dictStore::dictStore() {
  // The whole table, taken here on the control thread. Nothing on a message
  // path ever resizes it or grows a string inside it, which is what makes a
  // store from a rendering graph allocation-free.
  entries.resize(MAX_ENTRIES);
  for (dictEntry& entry : entries) {
    entry.key.reserve(KEY_CAPACITY + 1);
    entry.value.reserve(VALUE_CAPACITY + 1);
  }
}

std::size_t YSE::PATCHER::DictFind(const dictStore& store, const char* key, std::size_t keyLength) {
  for (std::size_t i = 0; i < store.count; i++) {
    const dictEntry& entry = store.entries[i];
    if (entry.key.size() != keyLength) continue;
    if (std::memcmp(entry.key.data(), key, keyLength) == 0) return i;
  }
  return store.count;
}

bool YSE::PATCHER::DictStoreAt(dictStore& store, const char* key, std::size_t keyLength,
                               const char* value, std::size_t valueLength) {
  if (keyLength == 0 || keyLength > dictStore::KEY_CAPACITY) return false;
  if (valueLength > dictStore::VALUE_CAPACITY) return false;

  const std::size_t at = DictFind(store, key, keyLength);
  if (at < store.count) {
    // Replacing keeps the entry where it is, so storage order — which is what
    // `getkeys` and a JSON dump report — is the order paths were first written.
    store.entries[at].value.assign(value, valueLength);
    return true;
  }

  if (store.count >= dictStore::MAX_ENTRIES) return false;
  store.entries[store.count].key.assign(key, keyLength);
  store.entries[store.count].value.assign(value, valueLength);
  store.count++;
  return true;
}

void YSE::PATCHER::DictEraseAt(dictStore& store, std::size_t position) {
  if (position >= store.count) return;
  for (std::size_t i = position + 1; i < store.count; i++) {
    store.entries[i - 1].key.assign(store.entries[i].key);
    store.entries[i - 1].value.assign(store.entries[i].value);
  }
  store.count--;
  store.entries[store.count].key.clear();
  store.entries[store.count].value.clear();
}

bool YSE::PATCHER::DictReferenceNames(const char* text, std::size_t length,
                                      const std::string& name) {
  if (name.empty()) return false;
  if (length != kReferenceLength + 1 + name.size()) return false;
  if (std::memcmp(text, kDictReferenceWord, kReferenceLength) != 0) return false;
  if (text[kReferenceLength] != ' ') return false;
  return std::memcmp(text + kReferenceLength + 1, name.data(), name.size()) == 0;
}

// ─── JSON, the nesting half of the design ─────────────────────────────────────

void YSE::PATCHER::DictToJson(const dictStore& store, nlohmann::json::value_type& out) {
  out = nlohmann::json::object();

  for (std::size_t i = 0; i < store.count; i++) {
    const std::string& key = store.entries[i].key;
    nlohmann::json::value_type* node = &out;
    std::size_t at = 0;

    while (true) {
      const std::size_t separator = key.find(kDictPathSeparator, at);
      const std::string segment =
          (separator == std::string::npos) ? key.substr(at) : key.substr(at, separator - at);
      // An empty segment ("a::", "::b") is not a path this store can express as
      // JSON, so the entry is skipped rather than producing an unnamed member.
      if (segment.empty()) break;

      if (separator == std::string::npos) {
        // A leaf whose name is already a sub-tree — "a" stored beside "a::b" —
        // is skipped rather than destroying the sub-tree. Storage order decides
        // which of the two wins, which is .coll's rule for a duplicate address.
        const auto existing = node->find(segment);
        if (existing == node->end() || !existing->is_object()) {
          (*node)[segment] = ValueToJson(store.entries[i].value);
        }
        break;
      }

      nlohmann::json::value_type& child = (*node)[segment];
      if (child.is_null()) child = nlohmann::json::object();
      // The mirror case: a branch whose name is already a leaf. Same rule.
      if (!child.is_object()) break;
      node = &child;
      at = separator + kSeparatorLength;
    }
  }
}

void YSE::PATCHER::DictFromJson(const nlohmann::json::value_type& in, dictStore& store) {
  store.count = 0;
  if (!in.is_object()) return;
  std::string path;
  path.reserve(dictStore::KEY_CAPACITY + 1);
  FlattenJson(in, path, store);
}

// ─── the object ───────────────────────────────────────────────────────────────

CONSTRUCT() {
  // One inlet, as in Max. Everything the object does arrives here as a message
  // word, which is why there is no int or float handler: a bare number names
  // neither a key nor a command.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);

  // ANY on the data outlet: what leaves it is an int, a float or a list
  // depending on what was stored. The reference outlet is always list text, and
  // the miss outlet is always a bang.
  ADD_OUT_ANY;
  ADD_OUT_LIST;
  ADD_OUT_BANG;

  ADD_PARAM(dictName);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // A private store to start with, so `store` is never null and no message
  // handler needs a null check. Rebind() trades it for a shared one as soon as
  // there is both a name and a patcher to prefix it with.
  Rebind();
  RefreshReference();

  // The allocations the send paths would otherwise need, taken here on the
  // control thread. RENDER_CAPACITY rather than VALUE_CAPACITY because the same
  // buffer carries the whole `getkeys` list.
  AtomList::ReserveRender(emitScratch);

  ADD_DESCRIPTION(
      "A nested key/value dictionary shared by name — Max's dict. Paths nest with \"::\", so "
      "\"voice::1::freq\" is one entry three levels deep, and the contents are a proper nested "
      "JSON object when the patch is saved. The dictionary is addressed as "
      "\"<patcherName>.<name>\", the same address form .s, .r, .value and .coll use, so every "
      ".dict of one name shares one dictionary and two patchers given one name share theirs. "
      "A bang emits \"dictionary <name>\" out outlet 1 — the reference the dict.* family binds. "
      "Dictionaries are addressed by name rather than passed down a cord: an outlet carries a "
      "value, never an identity, so the name is what travels and the store is resolved once, on "
      "the control thread. An unnamed .dict keeps a dictionary of its own.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "in", kInletDoc, "at most 256 entries");
  OUTLET_DOC(0, "data", kDataDoc, "");
  OUTLET_DOC(1, "reference", kReferenceDoc, "");
  OUTLET_DOC(2, "miss", kMissDoc, "");
  PARAM_DOC("name", "",
            "Max's shared context: all .dict objects of this name share their contents, through a "
            "store addressed as \"<patcherName>.<name>\". Empty gives this object a dictionary of "
            "its own rather than pooling it with every other unnamed .dict in the patcher. The "
            "name is resolved once, on the control thread, which is why there is no message that "
            "re-points a .dict at another name at run time.",
            "any identifier");
}

// A re-parse must not leave half of the previous configuration standing:
// Parameters::Set() calls this before parsing and returns early on an empty
// argument string, so this is the only thing that makes SetParams("") a real
// reset — dropping the shared name and going back to a private dictionary.
PARM_CLEAR() {
  dictName.clear();
  Rebind();
  RefreshReference();
}

PARM_PARSE() {
  Rebind();
  RefreshReference();
}

// `parent` is a patcherImplementation by construction (the patcher hands itself
// to every object via SetParent); the cast mirrors gValue's and gColl's.
void gDict::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  Rebind();
}

void gDict::RefreshBinding() {
  Rebind();
}

void gDict::Rebind() {
  // No name, or no patcher to prefix it with, means no address — and no address
  // means a private dictionary. See the class documentation for why an unnamed
  // .dict does not pool on "<patcherName>.".
  std::string address;
  if (!dictName.empty() && parent != nullptr) {
    auto* p = static_cast<patcherImplementation*>(parent);
    address = p->Name() + "." + dictName;
  }

  // Unchanged binding: keep the store, and with it everything in it. A live
  // SetParams that leaves the name alone must not empty the dictionary, and
  // neither must the second Rebind() a Set() makes (clear, then parse).
  if (store != nullptr && address == boundAddress) return;

  bool created = false;
  if (address.empty()) {
    store = std::make_shared<dictStore>();
    created = true;
  } else {
    store = AcquireNamedStore<dictStore>(address, created);
  }
  boundAddress = address;
  // Only the object that brought the dictionary into existence restores saved
  // contents into it; one joining an established name adopts what is there.
  createdStore = created;
}

void gDict::RefreshReference() {
  reference.clear();
  if (dictName.empty()) return;
  reference.reserve(kReferenceLength + 1 + dictName.size());
  reference += kDictReferenceWord;
  reference += ' ';
  reference += dictName;
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  // The reference, and nothing else. Emitting the contents here would make a
  // bang mean two things and would hand a patch a dump it never asked for.
  if (reference.empty()) return;
  outputs[1].SendList(reference, thread);
}

LIST_IN(ListIn) {
  std::size_t argOffset = 0;

  if (MatchWord(value, "set", 3, argOffset)) {
    HandleSet(value, argOffset);
    return;
  }
  if (MatchWord(value, "get", 3, argOffset)) {
    HandleGet(value, argOffset, thread);
    return;
  }
  if (MatchWord(value, "delete", 6, argOffset)) {
    HandleDelete(value, argOffset);
    return;
  }
  if (value == "clear") {
    HandleClear();
    return;
  }
  if (value == "getsize") {
    HandleGetSize(thread);
    return;
  }
  if (value == "getkeys") {
    HandleGetKeys(thread);
    return;
  }
  // Everything else, including "set" with no path at all. Counted rather than
  // logged: this inlet may be the audio thread.
  Refuse();
}

void gDict::HandleSet(const std::string& text, std::size_t argOffset) {
  std::size_t begin = 0;
  std::size_t length = 0;
  if (!ReadPath(text, argOffset, begin, length)) {
    Refuse();
    return;
  }

  // The rest of the message is the value, whitespace-trimmed at both ends, so
  // "set a 1 2 3" stores the list "1 2 3" and "set a" stores nothing at all.
  std::size_t valueBegin = argOffset;
  while (valueBegin < text.size() && IsSelectorSeparator(text[valueBegin]))
    valueBegin++;
  std::size_t valueEnd = text.size();
  while (valueEnd > valueBegin && IsSelectorSeparator(text[valueEnd - 1]))
    valueEnd--;

  const dictStoreGuard guard(store->busy);
  if (!guard.Held()) {
    Refuse();
    return;
  }
  if (!DictStoreAt(*store, text.c_str() + begin, length, text.c_str() + valueBegin,
                   valueEnd - valueBegin)) {
    Refuse();
  }
}

void gDict::HandleGet(const std::string& text, std::size_t argOffset, YSE::THREAD thread) {
  std::size_t begin = 0;
  std::size_t length = 0;
  if (!ReadPath(text, argOffset, begin, length)) {
    Refuse();
    return;
  }

  bool found = false;
  {
    const dictStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    const std::size_t at = DictFind(*store, text.c_str() + begin, length);
    if (at < store->count) {
      const std::string& stored = store->entries[at].value;
      fetchedLength = stored.size();
      std::memcpy(fetched, stored.data(), fetchedLength);
      fetched[fetchedLength] = '\0';
      found = true;
    }
  }

  // Outside the guard on purpose: the send runs the whole downstream graph,
  // which may well store into this same dictionary, and inside the guard that
  // store would be the one thing the try-lock drops.
  if (!found) {
    outputs[2].SendBang(thread);
    return;
  }
  if (fetchedLength == 0) {
    // A key holding nothing is still a hit — the miss outlet is what says
    // "no such key". Nothing leaves the data outlet, which is the .sprintf rule
    // that an object with nothing to say says nothing.
    return;
  }
  SendAtom(outputs[0], fetched, fetchedLength, emitScratch, thread);
}

void gDict::HandleDelete(const std::string& text, std::size_t argOffset) {
  std::size_t begin = 0;
  std::size_t length = 0;
  if (!ReadPath(text, argOffset, begin, length)) {
    Refuse();
    return;
  }

  const dictStoreGuard guard(store->busy);
  if (!guard.Held()) {
    Refuse();
    return;
  }
  const std::size_t at = DictFind(*store, text.c_str() + begin, length);
  if (at >= store->count) {
    Refuse();
    return;
  }
  DictEraseAt(*store, at);
}

void gDict::HandleClear() {
  const dictStoreGuard guard(store->busy);
  if (!guard.Held()) {
    Refuse();
    return;
  }
  // The rows keep their reserved storage; only the live count is dropped, so a
  // clear cannot be what makes the next store allocate.
  for (std::size_t i = 0; i < store->count; i++) {
    store->entries[i].key.clear();
    store->entries[i].value.clear();
  }
  store->count = 0;
}

void gDict::HandleGetSize(YSE::THREAD thread) {
  std::size_t size = 0;
  {
    const dictStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    size = store->count;
  }
  outputs[0].SendInt(static_cast<int>(size), thread);
}

void gDict::HandleGetKeys(YSE::THREAD thread) {
  keyScratch.Clear();
  {
    const dictStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    for (std::size_t i = 0; i < store->count; i++) {
      const std::string& key = store->entries[i].key;
      const std::size_t separator = key.find(kDictPathSeparator);
      const std::size_t length = (separator == std::string::npos) ? key.size() : separator;

      // Top-level keys, each reported once: "voice::1::freq" and
      // "voice::2::freq" are two entries under one key, and a `getkeys` that
      // said "voice voice" would be reporting storage rather than structure.
      bool seen = false;
      for (std::size_t j = 0; j < keyScratch.Size() && !seen; j++) {
        seen = keyScratch.AtomLength(j) == length &&
               std::memcmp(keyScratch.AtomText(j), key.data(), length) == 0;
      }
      if (seen) continue;
      // Refused rather than truncated when the list outgrows the working
      // buffer, and counted for the reason every other refusal here is.
      if (!keyScratch.Add(key.data(), length)) Refuse();
    }
  }
  SendAtoms(outputs[0], keyScratch, emitScratch, thread);
}

// ─── persistence ──────────────────────────────────────────────────────────────

void gDict::DumpState(nlohmann::json::value_type& json) {
  // Control thread — patcherImplementation::DumpJSON holds mtx — but the guard
  // is still taken, because a message may be arriving from a rendering graph
  // while the patch is being saved.
  //
  // Every .dict bound to a shared dictionary writes the contents, not one
  // nominated owner: they are all reading one table, so the copies are
  // identical, and a single writer would mean the dictionary silently stopped
  // being saved the day that one object was deleted from the patch. Restoring
  // is where the duplication is resolved — see RestoreState.
  const dictStoreGuard guard(store->busy);
  if (!guard.Held()) return;
  if (store->count == 0) return;

  nlohmann::json contents;
  DictToJson(*store, contents);
  json["contents"] = contents;
}

void gDict::RestoreState(const nlohmann::json::value_type& json) {
  // Only the object that created the dictionary fills it. Within one patch that
  // makes the first .dict of a name the one that loads and its siblings adopt
  // what it loaded; across patchers it means a patch opened into an engine where
  // its name is already live joins the running dictionary rather than resetting
  // it under whatever is already using it. `.coll`'s rule, for `.coll`'s reason.
  if (!createdStore) return;

  const auto stored = json.find("contents");
  if (stored == json.end() || !stored->is_object()) return;

  const dictStoreGuard guard(store->busy);
  if (!guard.Held()) return;
  DictFromJson(*stored, *store);
}

// ─── diagnostics ──────────────────────────────────────────────────────────────

std::string gDict::Lookup(const std::string& path) const {
  const dictStoreGuard guard(store->busy);
  if (!guard.Held()) return std::string();
  const std::size_t at = DictFind(*store, path.c_str(), path.size());
  if (at >= store->count) return std::string();
  return store->entries[at].value;
}

std::string gDict::KeyAt(std::size_t position) const {
  const dictStoreGuard guard(store->busy);
  if (!guard.Held()) return std::string();
  if (position >= store->count) return std::string();
  return store->entries[position].key;
}
