// `.array.deserialize` (issue #797). See gArrayDeserialize.h for the design;
// this file is a wait-free submit, a block-poll install, and one SendList.
#include "gArrayDeserialize.h"

#include "../../implementations/logImplementation.h"
#include "../pObjectList.hpp"

#include <string>

using namespace YSE::PATCHER;

#define className gArrayDeserialize

namespace {

  constexpr std::size_t kReferenceLength = sizeof(kArrayReferenceWord) - 1;

  // Doc strings, hoisted out of the constructor because they are long enough
  // that it stops being readable with them inline — the family's arrangement.
  constexpr char kInletDoc[] =
      "A JSON array as one message, for example [60,62.5,\"kick\"]. Text longer than 255 "
      "characters is refused, and so is a document that arrives while the previous one is still "
      "being parsed.";

  constexpr char kOutletDoc[] =
      "\"array <name>\" once the parsed contents are in the array, one audio block after the "
      "document arrived. Nothing is sent when parsing fails or the object has no name.";

  constexpr char kNameDoc[] =
      "Name of the array to fill: the .array with this name in the same patcher. Empty fills a "
      "private array.";

} // namespace

gArrayDeserialize::gArrayDeserialize() : gArrayEndsBase() {
  // One inlet, as in Max. List only: the document is the trigger, a bang
  // carries no document, and a bare number is not a JSON array — the family's
  // rule, applied to an object whose only trigger is its payload.
  ADD_IN_0;
  REG_LIST_IN(ListIn);

  // The reference outlet.
  ADD_OUT_LIST;

  RefreshReference();

  // This object's slot in the process-wide parse table. Claimed here on the
  // control thread — the first claim of a slot allocates its staging store —
  // and held for the object's life. A full table is worth a log line, said
  // here where a message path could only count it: the object still exists,
  // but every document it is handed will be refused.
  slot = ArrayParser().Claim();
  if (slot == 0) {
    INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + YSE::OBJ::G_ARRAY_DESERIALIZE +
                                          " could not claim a parse slot (" +
                                          std::to_string(arrayParser::CAPACITY) +
                                          " in use); this object will refuse every document");
  }

  ADD_DESCRIPTION(
      "Fills a named array from a JSON array such as [60,62.5,\"kick\"], replacing its contents. "
      "The text is parsed in the background, and the array's reference is sent once the new "
      "contents are in place.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "json", kInletDoc, "at most 255 characters");
  OUTLET_DOC(0, "reference", kOutletDoc, "");
  PARAM_DOC("name", "", kNameDoc, "any identifier");
}

gArrayDeserialize::~gArrayDeserialize() {
  // Give the slot back. Release never joins — a queued job finds the slot
  // FREE and does nothing — so this is safe on the background reclaimer,
  // which is where a deleted patcher object's destructor actually runs.
  ArrayParser().Release(slot);
}

void gArrayDeserialize::ParamsChanged() {
  RefreshReference();
}

void gArrayDeserialize::RefreshReference() {
  reference.clear();
  if (arrayName.empty()) return;
  reference.reserve(kReferenceLength + 1 + arrayName.size());
  reference += kArrayReferenceWord;
  reference += ' ';
  reference += arrayName;
}

// ─── the document, in ─────────────────────────────────────────────────────────

LIST_IN(ListIn) {
  (void)inlet;
  (void)thread;
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
  if (!ArrayParser().Submit(slot, value.c_str(), value.size())) {
    Refuse();
  }
}

// ─── the result, out ──────────────────────────────────────────────────────────

CALC() {
  // Audio thread, from the top of the block (WantsBlockPoll). Its only job is
  // to collect a parse that finished on the background pool — a result with
  // no cord to arrive on — install it, and announce it.
  if (slot == 0) return;

  arrayParser& parser = ArrayParser();
  // One acquire load in the common case — what it costs to have this object
  // in a patch at all. Also the recovery point for a submit whose push never
  // reached the pool.
  if (!parser.HasResult(slot)) return;

  bool ok = false;
  {
    // The install: bounded assigns into elements the store reserved at
    // construction, under its guard, released before the send. A lost guard
    // leaves the result in the slot for the next block — nothing to refuse,
    // there is a later block to try again in.
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) return;
    if (!parser.Consume(slot, *store, ok)) return;
  }

  if (!ok) {
    // Malformed, or not a JSON array. The store is untouched — a bad document
    // never costs an array its contents — and nothing is announced, because
    // nothing happened.
    failed.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  parsed.fetch_add(1, std::memory_order_relaxed);
  // The announcement, with no guard held: the send runs the whole downstream
  // subgraph, which may well read — or refill — this same array. Silent for
  // an unnamed array, which has no name to pass on.
  if (!reference.empty()) {
    outputs[0].SendList(reference, thread);
  }
}

#undef className
