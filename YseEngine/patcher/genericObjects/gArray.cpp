#include "gArray.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

#include <cstdlib>
#include <cstring>
#include <string>

using namespace YSE::PATCHER;
#define className gArray

namespace {

  constexpr std::size_t kReferenceLength = sizeof(kArrayReferenceWord) - 1;

  // Reads one whitespace-separated token at `offset`, leaving `offset` on the
  // first character after it. False when there is no token there. No allocation
  // — the caller works with the slice rather than with a copy of it.
  bool ReadToken(const std::string& text, std::size_t& offset, std::size_t& begin,
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

  // Reads an index argument. False when there is no integer there or when it is
  // negative: an index is a position, never a count from the end. See the
  // indexing note on gArray — the rule is decided once for the whole array.*
  // family rather than per object.
  bool ReadIndex(const std::string& text, std::size_t& offset, std::size_t& out) {
    int value = 0;
    if (!ReadIntArgAt(text, offset, value)) return false;
    if (value < 0) return false;
    out = static_cast<std::size_t>(value);
    return true;
  }

  // True when nothing but whitespace is left from `offset` on.
  bool AtEnd(const std::string& text, std::size_t offset) {
    for (std::size_t i = offset; i < text.size(); i++) {
      if (!IsSelectorSeparator(text[i])) return false;
    }
    return true;
  }

  // ─── element typing, on the way into JSON ───────────────────────────────────
  //
  // Control thread only (ArrayToJson is). `strtod` / `strtoll` rather than the
  // float the patcher classified the token with, because a stored "0.1" must
  // come back as 0.1 rather than as the double nearest the float nearest 0.1,
  // and an integer past 2^24 must not be rounded on the way through.
  //
  // gDict.cpp's TokenToJson does the same job for the single-token case of a
  // dict value. Not shared: this is the whole of the typing here, where the dict
  // has to reach it through a list-text splitter it owns, and exporting a
  // ten-line internal out of a shipped object to save ten lines is a worse
  // trade than the repetition.
  nlohmann::json ElementToJson(const std::string& element) {
    float number = 0.f;
    if (!ReadNumericToken(element, number)) return nlohmann::json(element);
    if (TokenLooksLikeFloat(element)) {
      return nlohmann::json(std::strtod(element.c_str(), nullptr));
    }
    return nlohmann::json(static_cast<std::int64_t>(std::strtoll(element.c_str(), nullptr, 10)));
  }

  // ─── element typing, on the way back out of JSON ────────────────────────────
  //
  // An element is one atom, so a nested array or object has no spelling here and
  // is skipped by the caller. Null is skipped too: an array with a hole in it is
  // not something the type can express.
  bool JsonElementText(const nlohmann::json::value_type& value, std::string& out) {
    out.clear();
    if (value.is_null() || value.is_array() || value.is_object()) return false;
    if (value.is_string()) {
      out = value.get<std::string>();
      // An empty string is not an atom — there is nothing to spell it with, and
      // a stored empty element would render as nothing at all.
      return !out.empty();
    }
    if (value.is_boolean()) {
      out = value.get<bool>() ? "1" : "0";
      return true;
    }
    if (value.is_number_integer() || value.is_number_unsigned()) {
      out = std::to_string(value.get<std::int64_t>());
      return true;
    }
    // A real, spelled the way JSON spells it — so a stored "120." comes back as
    // "120.0". The same number, and stable from the second round trip on.
    out = value.dump();
    return true;
  }

  // Doc strings, hoisted out of CONSTRUCT() because they are long enough that
  // the constructor stops being readable with them inline — .coll's arrangement.
  constexpr char kInletDoc[] =
      "append, insert, set, get, delete, clear, getsize or getvalue (see the notes). A bang sends "
      "the array's reference out of outlet 1. A refused message changes nothing.";
  constexpr char kDataDoc[] =
      "The element \"get\" fetched, the length \"getsize\" reports, or the whole array for "
      "\"getvalue\". Numbers leave as an int or a float by their spelling, anything else as a "
      "symbol.";
  constexpr char kReferenceDoc[] =
      "\"array <name>\" on a bang: the reference the other .array objects accept. Nothing is sent "
      "when the array has no name.";
  constexpr char kMissDoc[] = "Bang when \"get\" names an index the array does not have.";

} // namespace

// ─── the store ────────────────────────────────────────────────────────────────

arrayStore::arrayStore() {
  // The whole table, taken here on the control thread. Nothing on a message path
  // ever resizes it or grows a string inside it, which is what makes a push from
  // a rendering graph allocation-free.
  elements.resize(MAX_ELEMENTS);
  for (std::string& element : elements)
    element.reserve(ELEMENT_CAPACITY + 1);
}

std::size_t YSE::PATCHER::ArrayFind(const arrayStore& store, const char* element,
                                    std::size_t length) {
  for (std::size_t i = 0; i < store.count; i++) {
    if (store.elements[i].size() != length) continue;
    if (std::memcmp(store.elements[i].data(), element, length) == 0) return i;
  }
  return store.count;
}

bool YSE::PATCHER::ArraySetAt(arrayStore& store, std::size_t index, const char* element,
                              std::size_t length) {
  if (index >= store.count) return false;
  if (length == 0 || length > arrayStore::ELEMENT_CAPACITY) return false;
  store.elements[index].assign(element, length);
  return true;
}

bool YSE::PATCHER::ArrayInsertAt(arrayStore& store, std::size_t index, const char* element,
                                 std::size_t length) {
  // `index == count` is the append, and the only index past the last element
  // that names a real position.
  if (index > store.count) return false;
  if (length == 0 || length > arrayStore::ELEMENT_CAPACITY) return false;
  if (store.count >= arrayStore::MAX_ELEMENTS) return false;

  // Downwards, so an element is never overwritten before it has been moved.
  for (std::size_t i = store.count; i > index; i--)
    store.elements[i].assign(store.elements[i - 1]);
  store.elements[index].assign(element, length);
  store.count++;
  return true;
}

void YSE::PATCHER::ArrayEraseAt(arrayStore& store, std::size_t index) {
  if (index >= store.count) return;
  for (std::size_t i = index + 1; i < store.count; i++)
    store.elements[i - 1].assign(store.elements[i]);
  store.count--;
  store.elements[store.count].clear();
}

bool YSE::PATCHER::ArrayReferenceNames(const char* text, std::size_t length,
                                       const std::string& name) {
  if (name.empty()) return false;
  if (length != kReferenceLength + 1 + name.size()) return false;
  if (std::memcmp(text, kArrayReferenceWord, kReferenceLength) != 0) return false;
  if (text[kReferenceLength] != ' ') return false;
  return std::memcmp(text + kReferenceLength + 1, name.data(), name.size()) == 0;
}

// ─── JSON ─────────────────────────────────────────────────────────────────────

void YSE::PATCHER::ArrayToJson(const arrayStore& store, nlohmann::json::value_type& out) {
  out = nlohmann::json::array();
  for (std::size_t i = 0; i < store.count; i++)
    out.push_back(ElementToJson(store.elements[i]));
}

void YSE::PATCHER::ArrayFromJson(const nlohmann::json::value_type& in, arrayStore& store) {
  store.count = 0;
  if (!in.is_array()) return;
  std::string text;
  for (const auto& element : in) {
    if (!JsonElementText(element, text)) continue;
    // Over-long elements and elements past MAX_ELEMENTS are dropped and the rest
    // of the document still loads — .coll's rule for an over-long record.
    ArrayAppend(store, text.c_str(), text.size());
  }
}

// ─── the object ───────────────────────────────────────────────────────────────

CONSTRUCT() {
  // One inlet, as in Max. Everything the object does arrives here as a message
  // word, which is why there is no int or float handler: a bare number names
  // neither an index nor a command.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);

  // ANY on the data outlet: what leaves it is an int, a float or a list
  // depending on what was stored and on which message asked. The reference
  // outlet is always list text, and the miss outlet is always a bang.
  ADD_OUT_ANY;
  ADD_OUT_LIST;
  ADD_OUT_BANG;

  ADD_PARAM(arrayName);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // A private store to start with, so `store` is never null and no message
  // handler needs a null check. Rebind() trades it for a shared one as soon as
  // there is both a name and a patcher to prefix it with.
  Rebind();
  RefreshReference();

  // The allocation the send paths would otherwise need, taken here on the
  // control thread.
  AtomList::ReserveRender(emitScratch);

  ADD_DESCRIPTION(
      "An ordered list of values, shared by every .array with the same name in a patcher. "
      "Messages add, change, fetch and remove elements by zero-based index; a bang sends the "
      "array's reference, which the other .array objects accept.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "in", kInletDoc, "at most 256 elements");
  OUTLET_DOC(0, "data", kDataDoc, "");
  OUTLET_DOC(1, "reference", kReferenceDoc, "");
  OUTLET_DOC(2, "miss", kMissDoc, "");
  PARAM_DOC("name", "",
            "The shared name: every .array with this name in the patcher uses the same "
            "contents. Fixed when the object is created. Empty gives this object a private "
            "array.",
            "any identifier");
}

// A re-parse must not leave half of the previous configuration standing:
// Parameters::Set() calls this before parsing and returns early on an empty
// argument string, so this is the only thing that makes SetParams("") a real
// reset — dropping the shared name and going back to a private array.
PARM_CLEAR() {
  arrayName.clear();
  Rebind();
  RefreshReference();
}

PARM_PARSE() {
  Rebind();
  RefreshReference();
}

// `parent` is a patcherImplementation by construction (the patcher hands itself
// to every object via SetParent); the cast mirrors gValue's, gColl's and
// gDict's.
void gArray::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  Rebind();
}

void gArray::RefreshBinding() {
  Rebind();
}

void gArray::Rebind() {
  // No name, or no patcher to prefix it with, means no address — and no address
  // means a private array. See the class documentation for why an unnamed
  // .array does not pool on "patcher.<patcherName>.".
  std::string address;
  if (!arrayName.empty() && parent != nullptr) {
    auto* p = static_cast<patcherImplementation*>(parent);
    address = p->ScopedAddress(arrayName);
  }

  // Unchanged binding: keep the store, and with it everything in it. A live
  // SetParams that leaves the name alone must not empty the array, and neither
  // must the second Rebind() a Set() makes (clear, then parse).
  if (store != nullptr && address == boundAddress) return;

  bool created = false;
  if (address.empty()) {
    store = std::make_shared<arrayStore>();
    created = true;
  } else {
    store = AcquireNamedStore<arrayStore>(address, created);
  }
  boundAddress = address;
  // Only the object that brought the array into existence restores saved
  // contents into it; one joining an established name adopts what is there.
  createdStore = created;
}

void gArray::RefreshReference() {
  reference.clear();
  if (arrayName.empty()) return;
  reference.reserve(kReferenceLength + 1 + arrayName.size());
  reference += kArrayReferenceWord;
  reference += ' ';
  reference += arrayName;
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  // The reference, and nothing else. Emitting the contents here would make a
  // bang mean two things and would hand a patch a dump it never asked for —
  // `getvalue` is the message that asks.
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
  if (MatchWord(value, "append", 6, argOffset)) {
    HandleAppend(value, argOffset);
    return;
  }
  if (MatchWord(value, "insert", 6, argOffset)) {
    HandleInsert(value, argOffset);
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
  if (value == "getvalue") {
    HandleGetValue(thread);
    return;
  }
  // Everything else, including a message word with no argument at all. Counted
  // rather than logged: this inlet may be the audio thread.
  Refuse();
}

void gArray::HandleSet(const std::string& text, std::size_t argOffset) {
  std::size_t index = 0;
  if (!ReadIndex(text, argOffset, index)) {
    Refuse();
    return;
  }

  std::size_t begin = 0;
  std::size_t length = 0;
  if (!ReadToken(text, argOffset, begin, length)) {
    Refuse();
    return;
  }
  // Exactly one element: an element is one atom, so a `set` naming several would
  // have to invent a meaning for the rest. Refused whole rather than storing the
  // first and dropping the others, which would be truncation by another name.
  if (!AtEnd(text, argOffset)) {
    Refuse();
    return;
  }

  const arrayStoreGuard guard(store->busy);
  if (!guard.Held()) {
    Refuse();
    return;
  }
  if (!ArraySetAt(*store, index, text.c_str() + begin, length)) Refuse();
}

void gArray::HandleGet(const std::string& text, std::size_t argOffset, YSE::THREAD thread) {
  std::size_t index = 0;
  if (!ReadIndex(text, argOffset, index)) {
    Refuse();
    return;
  }

  bool found = false;
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    if (index < store->count) {
      const std::string& stored = store->elements[index];
      fetchedLength = stored.size();
      std::memcpy(fetched, stored.data(), fetchedLength);
      fetched[fetchedLength] = '\0';
      found = true;
    }
  }

  // Outside the guard on purpose: the send runs the whole downstream graph,
  // which may well push onto this same array, and inside the guard that push
  // would be the one thing the try-lock drops.
  if (!found) {
    outputs[2].SendBang(thread);
    return;
  }
  SendAtom(outputs[0], fetched, fetchedLength, emitScratch, thread);
}

void gArray::HandleAppend(const std::string& text, std::size_t argOffset) {
  const arrayStoreGuard guard(store->busy);
  if (!guard.Held()) {
    Refuse();
    return;
  }

  std::size_t begin = 0;
  std::size_t length = 0;
  bool sawToken = false;
  // Every token is an element: `append 0 4 7` adds three, which is the list to
  // array conversion and the inverse of what `getvalue` sends.
  while (ReadToken(text, argOffset, begin, length)) {
    sawToken = true;
    // An over-long element, or a full array. The list loses its tail rather than
    // its head — AtomList's rule, and the failure that costs a patch least.
    if (!ArrayAppend(*store, text.c_str() + begin, length)) Refuse();
  }
  // `append` with nothing but whitespace after it.
  if (!sawToken) Refuse();
}

void gArray::HandleInsert(const std::string& text, std::size_t argOffset) {
  std::size_t index = 0;
  if (!ReadIndex(text, argOffset, index)) {
    Refuse();
    return;
  }

  const arrayStoreGuard guard(store->busy);
  if (!guard.Held()) {
    Refuse();
    return;
  }

  std::size_t begin = 0;
  std::size_t length = 0;
  std::size_t added = 0;
  bool sawToken = false;
  while (ReadToken(text, argOffset, begin, length)) {
    sawToken = true;
    // Each element goes after the last one inserted, so `insert 0 a b c` leaves
    // a, b, c in the order they were written rather than reversed.
    if (ArrayInsertAt(*store, index + added, text.c_str() + begin, length)) {
      added++;
    } else {
      Refuse();
    }
  }
  if (!sawToken) Refuse();
}

void gArray::HandleDelete(const std::string& text, std::size_t argOffset) {
  std::size_t index = 0;
  if (!ReadIndex(text, argOffset, index)) {
    Refuse();
    return;
  }

  const arrayStoreGuard guard(store->busy);
  if (!guard.Held()) {
    Refuse();
    return;
  }
  if (index >= store->count) {
    Refuse();
    return;
  }
  ArrayEraseAt(*store, index);
}

void gArray::HandleClear() {
  const arrayStoreGuard guard(store->busy);
  if (!guard.Held()) {
    Refuse();
    return;
  }
  // The rows keep their reserved storage; only the live count is dropped, so a
  // clear cannot be what makes the next push allocate.
  for (std::size_t i = 0; i < store->count; i++)
    store->elements[i].clear();
  store->count = 0;
}

void gArray::HandleGetSize(YSE::THREAD thread) {
  std::size_t size = 0;
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    size = store->count;
  }
  outputs[0].SendInt(static_cast<int>(size), thread);
}

void gArray::HandleGetValue(YSE::THREAD thread) {
  emitList.Clear();
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    for (std::size_t i = 0; i < store->count; i++) {
      // Refused rather than truncated when the elements together outrun the
      // working buffer — an array of 256 long symbols spells more list text than
      // a cord carries — and counted for the reason every other refusal here is.
      if (!emitList.Add(store->elements[i].data(), store->elements[i].size())) Refuse();
    }
  }
  // Outside the guard, and through SendAtoms so that an array of one element
  // sends that element rather than a list of one — the patcher's transport rule.
  SendAtoms(outputs[0], emitList, emitScratch, thread);
}

// ─── persistence ──────────────────────────────────────────────────────────────

bool gArray::DumpState(nlohmann::json::value_type& json) {
  // Control thread — patcherImplementation::DumpJSON holds mtx — but the guard
  // is still taken, because a message may be arriving from a rendering graph
  // while the patch is being saved. Through saveGuard, which waits that message
  // out rather than skipping the contents (issue #940).
  //
  // Every .array bound to a shared sequence writes the contents, not one
  // nominated owner: they are all reading one table, so the copies are
  // identical, and a single writer would mean the array silently stopped being
  // saved the day that one object was deleted from the patch. Restoring is where
  // the duplication is resolved — see RestoreState.
  const saveGuard guard(store->busy);
  if (!guard.Held()) return false;
  if (store->count == 0) return true;

  nlohmann::json contents;
  ArrayToJson(*store, contents);
  json["contents"] = contents;
  return true;
}

void gArray::RestoreState(const nlohmann::json::value_type& json) {
  // Only the object that created the array fills it. Within one patch that makes
  // the first .array of a name the one that loads and its siblings adopt what it
  // loaded; across patchers it means a patch opened into an engine where its
  // name is already live joins the running array rather than resetting it under
  // whatever is already using it. `.coll`'s rule, for `.coll`'s reason.
  if (!createdStore) return;

  const auto stored = json.find("contents");
  if (stored == json.end() || !stored->is_array()) return;

  const arrayStoreGuard guard(store->busy);
  if (!guard.Held()) return;
  ArrayFromJson(*stored, *store);
}

// ─── diagnostics ──────────────────────────────────────────────────────────────

std::string gArray::ElementAt(std::size_t index) const {
  const arrayStoreGuard guard(store->busy);
  if (!guard.Held()) return std::string();
  if (index >= store->count) return std::string();
  return store->elements[index];
}
