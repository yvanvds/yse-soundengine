// `.dict.deserialize` (issue #771). See gDictDeserialize.h for the design;
// this file is a wait-free submit, a block-poll install, and one SendList.
#include "gDictDeserialize.h"

#include "../../implementations/logImplementation.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include "../patcherImplementation.h"

using namespace YSE::PATCHER;

#define className gDictDeserialize

namespace {

  constexpr std::size_t kReferenceLength = sizeof(kDictReferenceWord) - 1;

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
      "The document: one JSON object as a single list message — exactly what .dict.serialize "
      "emits, and anything else JSON calls an object. It is handed to the background pool to "
      "parse (nlohmann allocates, and this inlet may be the audio thread) and, a block later, "
      "replaces the bound dictionary whole — \"::\" paths flattened back out of the nesting by "
      "the same DictFromJson a saved patch loads through, so a serialise/deserialise round trip "
      "reproduces the dictionary. A document longer than 255 characters — the longest list "
      "payload the patcher's value queue carries — is refused whole and counted, never "
      "truncated: the prefix of a JSON document is a different document or none. A document "
      "arriving while the previous one is still in flight is refused and counted too, and one "
      "that fails to parse is counted and changes nothing — a bad document never costs a "
      "dictionary its contents. 'read [file]' is the document past that bound: the file loads "
      "through the patcher's file scheduler — the request claims a patcher-owned slot, the disk "
      "work runs on the background pool, and the bytes are handed to the same parse slot the "
      "inlet uses, so a file document installs and announces exactly as an inlet document does, "
      "up to the scheduler slot's 128 KiB (a larger file is refused whole by the scheduler, "
      "never truncated). A bare 'read' reuses the last path given. No bang and no bare number: "
      "a bang carries no document, and a number names nothing.";

  constexpr char kOutletDoc[] =
      "\"dictionary <name>\" once a document has been parsed and installed — the reference the "
      "dict.* family binds, .dict.pack's gesture, so the rest of the family can pick the result "
      "up the moment it exists. Emitted from the patcher's block poll, one block after the "
      "document arrived, because the parse finishes on the background pool and has no cord to "
      "arrive on. Silent for an unnamed dictionary, which has no name to pass on, and silent on "
      "a failed parse, which installed nothing.";

  constexpr char kNameDoc[] =
      "The dictionary the parsed document fills, addressed as \"<patcherName>.<name>\" — the "
      "dictionary a .dict of the same name in this patcher holds. Resolved once, on the control "
      "thread, which is why no message re-points it at run time. Empty binds a private "
      "dictionary: the document still loads, but there is no name to announce.";

} // namespace

CONSTRUCT() {
  // One inlet, as in Max. List only: the document is the trigger, a bang
  // carries no document, and a bare number names nothing — the family's rule.
  ADD_IN_0;
  REG_LIST_IN(ListIn);

  // The reference outlet.
  ADD_OUT_LIST;

  ADD_PARAM(dictName);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // A private store to start with, so `store` is never null and no message
  // handler needs a null check. Rebind() trades it for a shared one as soon
  // as there is both a name and a patcher to prefix it with.
  Rebind();
  RefreshReference();

  // The file route's one allocation, taken here on the control thread: a
  // `read` may arrive on the audio callback, so remembering its path has to
  // reuse storage that already exists (issue #840).
  readPath.reserve(fileScheduler::PATH_CAPACITY);

  // This object's slot in the process-wide parse table. Claimed here on the
  // control thread — the first claim of a slot allocates its staging store —
  // and held for the object's life. A full table is worth a log line, said
  // here where a message path could only count it: the object still exists,
  // but every document it is handed will be refused.
  slot = DictParser().Claim();
  if (slot == 0) {
    INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + YSE::OBJ::G_DICT_DESERIALIZE +
                                          " could not claim a parse slot (" +
                                          std::to_string(dictParser::CAPACITY) +
                                          " in use); this object will refuse every document");
  }

  ADD_DESCRIPTION(
      "Builds a dictionary from serialised text — Max's dict.deserialize on the name-addressed "
      "value model .dict settled. The read half of the interchange pair whose write half is "
      ".dict.serialize: a JSON string arriving from a host, a .textedit, a .s/.r pair or the "
      "live-coding DSL becomes a dictionary the patch can address. The target dictionary is "
      "bound from the creation argument, \".dict.deserialize <name>\", resolved once on the "
      "control thread; a list message holding one JSON object — exactly what .dict.serialize "
      "emits — replaces that dictionary whole, \"::\" paths flattened back out of the nesting, "
      "and the reference \"dictionary <name>\" leaves the outlet so the rest of the family can "
      "pick the result up. The parse runs on the background pool — nlohmann allocates without "
      "bound, and the inlet may be the audio thread — so the inlet is a wait-free hand-off and "
      "the result is installed by the patcher's block poll one block later, .midiinfo's "
      "arrangement for a result with no cord to arrive on. A document past 255 characters — the "
      "longest list payload the patcher's value queue carries — is refused whole and counted, "
      "never truncated; a document that fails to parse is counted and changes nothing, so a bad "
      "document never costs a dictionary its contents. A larger document takes the file route: "
      "'read [file]' loads a JSON file through the patcher's file scheduler — background disk "
      "work, the same background parse, the same block-poll install — lifting the bound to the "
      "scheduler slot's 128 KiB, past which a file is refused whole. Only the creation argument "
      "persists across a save; the dictionary's contents persist with the .dict that owns "
      "them.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "json", kInletDoc, "at most 255 characters inline; a file up to 128 KiB");
  OUTLET_DOC(0, "reference", kOutletDoc, "");
  PARAM_DOC("name", "", kNameDoc, "any identifier");
}

gDictDeserialize::~gDictDeserialize() {
  // Give the slot back. Release never joins — a queued job finds the slot
  // FREE and does nothing — so this is safe on the background reclaimer,
  // which is where a deleted patcher object's destructor actually runs.
  DictParser().Release(slot);
}

// ─── creation arguments ───────────────────────────────────────────────────────

// A re-parse must not leave half of the previous configuration standing:
// Parameters::Set() calls this before parsing and returns early on an empty
// argument string, so this is what makes SetParams("") a real reset —
// dropping the name and going back to a private dictionary. gDict's rule.
PARM_CLEAR() {
  dictName.clear();
  Rebind();
  RefreshReference();
}

PARM_PARSE() {
  Rebind();
  RefreshReference();
}

// `parent` is a patcherImplementation by construction (the patcher hands
// itself to every object via SetParent); the cast mirrors gDict's.
void gDictDeserialize::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  // Control thread, and the one place the patcher's file table can be built:
  // a `read` arriving later on the audio thread has to find it already there
  // (issues #683, #840).
  EnableFileIO();
  Rebind();
}

void gDictDeserialize::RefreshBinding() {
  Rebind();
}

void gDictDeserialize::Rebind() {
  // No name, or no patcher to prefix it with, means no address — and no
  // address means a private dictionary. See gDict.h for why an unnamed
  // object does not pool on "<patcherName>.".
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

void gDictDeserialize::RefreshReference() {
  reference.clear();
  if (dictName.empty()) return;
  reference.reserve(kReferenceLength + 1 + dictName.size());
  reference += kDictReferenceWord;
  reference += ' ';
  reference += dictName;
}

// ─── the document, in ─────────────────────────────────────────────────────────

LIST_IN(ListIn) {
  (void)inlet;
  (void)thread;
  // The file route first (issue #840): "read [file]" is a claim on a
  // fileScheduler slot, never a document — and never ambiguous, since a
  // document this object can accept is a JSON object and so starts with '{'.
  std::size_t wordBegin = 0;
  std::size_t wordEnd = 0;
  if (FirstToken(value.c_str(), value.size(), wordBegin, wordEnd) &&
      TokenIs(value.c_str() + wordBegin, wordEnd - wordBegin, "read", 4)) {
    std::size_t begin = wordEnd;
    std::size_t end = value.size();
    Trim(value.c_str(), begin, end);
    RequestFile(value.c_str() + begin, end - begin);
    return;
  }

  // The wait-free hand-off: one CAS, one bounded copy, one lock-free push —
  // nothing here parses, because this may be the audio thread. Refused and
  // counted: no slot at all (the table was full at construction), a document
  // past what a list payload carries (refused whole, never truncated — the
  // prefix of a JSON document is a different document), or a slot still busy
  // with the previous document.
  if (slot == 0 || value.size() > DOCUMENT_CAPACITY) {
    Refuse();
    return;
  }
  if (!DictParser().Submit(slot, value.c_str(), value.size())) {
    Refuse();
  }
}

// ─── the file route ───────────────────────────────────────────────────────────

void gDictDeserialize::RequestFile(const char* name, std::size_t length) {
  // A claim on a patcher-owned slot and nothing more: whichever thread is
  // dispatching, no file is opened here (fileScheduler's whole reason). All
  // refusals are counted rather than logged, since this may be the audio
  // callback.
  fileScheduler* io = FileIO();
  if (io == nullptr) {
    // A standalone object has no patcher and so no plumbing.
    Refuse();
    return;
  }
  if (name != nullptr && length > 0) {
    if (length >= fileScheduler::PATH_CAPACITY) {
      Refuse();
      return;
    }
    // assign() into a string reserved at construction reuses its storage.
    readPath.assign(name, length);
  }
  // A bare `read` reuses the last name given; nothing named yet, and no
  // dialog to ask with. And with no parse slot, the bytes could never be
  // parsed, so the request is not worth a scheduler slot either.
  if (readPath.empty() || slot == 0) {
    Refuse();
    return;
  }
  if (!io->RequestRead(this, FILE_TAG_READ, readPath.c_str(), readPath.size())) {
    Refuse();
  }
}

void gDictDeserialize::DeliverFileResult(const fileResult& result, YSE::THREAD thread) {
  (void)thread;
  if (result.op != FILE_OP::READ || result.tag != FILE_TAG_READ) return;
  if (!result.ok || result.bytes == nullptr) {
    // No such file, or one larger than a fileScheduler slot — refused whole
    // by the scheduler, never truncated. A failure like a malformed document:
    // counted, nothing installed, the dictionary untouched.
    failed.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  // The bytes are scheduler-owned and valid only for this callback, and
  // Submit copies them into the parse slot before returning — the same
  // wait-free hand-off the inlet makes, minus the transport bound: the parse
  // slot carries what a fileScheduler slot reads (issue #840). From here the
  // file route is the inlet route: the parse runs on the pool, and the block
  // poll installs the document and announces the reference.
  if (slot == 0 || !DictParser().Submit(slot, result.bytes, result.byteCount)) {
    Refuse();
  }
}

// ─── the result, out ──────────────────────────────────────────────────────────

CALC() {
  // Audio thread, from the top of the block (WantsBlockPoll). Its only job is
  // to collect a parse that finished on the background pool — a result with
  // no cord to arrive on — install it, and announce it.
  if (slot == 0) return;

  dictParser& parser = DictParser();
  // One acquire load in the common case — what it costs to have this object
  // in a patch at all. Also the recovery point for a submit whose push never
  // reached the pool.
  if (!parser.HasResult(slot)) return;

  bool ok = false;
  {
    // The install: bounded assigns into rows the store reserved at
    // construction, under its guard, released before the send. A lost guard
    // leaves the result in the slot for the next block — nothing to refuse,
    // there is a later block to try again in.
    const dictStoreGuard guard(store->busy);
    if (!guard.Held()) return;
    if (!parser.Consume(slot, *store, ok)) return;
  }

  if (!ok) {
    // Malformed, or not a JSON object. The store is untouched — a bad
    // document never costs a dictionary its contents — and nothing is
    // announced, because nothing happened.
    failed.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  parsed.fetch_add(1, std::memory_order_relaxed);
  // The announcement, with no guard held: the send runs the whole downstream
  // subgraph, which may well read — or refill — this same dictionary.
  // Silent for an unnamed dictionary, which has no name to pass on.
  if (!reference.empty()) {
    outputs[0].SendList(reference, thread);
  }
}

#undef className
