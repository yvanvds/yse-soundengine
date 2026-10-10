#include "gArrayAt.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

#include <string>

using namespace YSE::PATCHER;
#define className gArrayAt

namespace {

  // Reads an index argument out of list text. False when there is no integer
  // there or when it is negative: an index is a position, never a count from
  // the end — arrayStore's rule, decided once for the family. gArray.cpp's
  // reader; not shared, for the reason its ElementToJson gives — exporting a
  // file-local helper out of a shipped object costs more than the repetition.
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

  // Doc strings, hoisted out of CONSTRUCT() because they are long enough that
  // the constructor stops being readable with them inline — gArray's
  // arrangement.
  constexpr char kIndexInletDoc[] =
      "An int fetches the element at that position and remembers the position; a bang fetches "
      "at the remembered position, and so does the bound array's reference. A list of positions "
      "fetches them all at once. A float is truncated; a negative position is refused.";
  constexpr char kReferenceInletDoc[] =
      "Accepts the bound array's reference \"array <name>\" and ignores it, so the array's "
      "reference outlet can be wired here. Anything else is refused.";
  constexpr char kElementDoc[] =
      "The element, as an int, float or symbol by its spelling. A list of positions gives one "
      "list, in the order asked.";
  constexpr char kMissDoc[] =
      "Bang when a position is not in the array. For a list of positions, one bang if any of "
      "them is missing, and nothing leaves outlet 0.";

} // namespace

CONSTRUCT() {
  // The index inlet, hot, and the reference inlet, cold — one inlet for the
  // index and a second for the reference, the family's shape for an object
  // that consumes a reference rather than producing one.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  // ANY on the element outlet: what leaves it is an int, a float, a symbol or
  // a list depending on what was stored and how much was asked for. The miss
  // outlet is always a bang.
  ADD_OUT_ANY;
  ADD_OUT_BANG;

  ADD_PARAM(arrayName);
  ADD_PARAM(index);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // A private, empty store to start with, so `store` is never null and no
  // message handler needs a null check. Rebind() trades it for a shared one
  // as soon as there is both a name and a patcher to prefix it with.
  Rebind();

  // The allocation the send path would otherwise need, taken here on the
  // control thread.
  AtomList::ReserveRender(emitScratch);

  ADD_DESCRIPTION(
      "Outputs the element at a position in a named array. Send a position to fetch it, or a "
      "list of positions to fetch several at once; a bang fetches the last position again. A "
      "position the array does not have bangs the miss outlet.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "index", kIndexInletDoc, "0-255");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "element", kElementDoc, "");
  OUTLET_DOC(1, "miss", kMissDoc, "");
  PARAM_DOC("name", "",
            "Name of the array to use: the .array with this name in the same patcher. Empty "
            "uses a private, empty array.",
            "any identifier");
  PARAM_DOC("index", "0", "The position a bang fetches before any position arrives.", "0-255");
}

// A re-parse must not leave half of the previous configuration standing:
// Parameters::Set() calls this before parsing and returns early on an empty
// argument string, so this is what makes SetParams("") a real reset —
// dropping the name and index and going back to a private array. gArray's
// rule.
PARM_CLEAR() {
  arrayName.clear();
  index.store(0, std::memory_order_relaxed);
  Rebind();
}

PARM_PARSE() {
  Rebind();
}

// `parent` is a patcherImplementation by construction (the patcher hands
// itself to every object via SetParent); the cast mirrors gArray's.
void gArrayAt::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  Rebind();
}

void gArrayAt::RefreshBinding() {
  Rebind();
}

void gArrayAt::Rebind() {
  // No name, or no patcher to prefix it with, means no address — and no
  // address means a private, empty array. See gArray.h for why an unnamed
  // object does not pool on "patcher.<patcherName>.".
  std::string address;
  if (!arrayName.empty() && parent != nullptr) {
    auto* p = static_cast<patcherImplementation*>(parent);
    address = p->ScopedAddress(arrayName);
  }

  // Unchanged binding: keep the store. A live SetParams that leaves the name
  // alone must not re-anchor it, and neither must the second Rebind() a
  // Set() makes (clear, then parse).
  if (store != nullptr && address == boundAddress) return;

  bool created = false;
  store = address.empty() ? std::make_shared<arrayStore>()
                          : AcquireNamedStore<arrayStore>(address, created);
  boundAddress = address;
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  FetchAtIndex(thread);
}

INT_IN(IntIn) {
  (void)inlet;
  // Negative is refused whole — the index does not move either, so a bang
  // after the refusal fetches what it would have fetched before it.
  if (value < 0) {
    Refuse();
    return;
  }
  index.store(value, std::memory_order_relaxed);
  const std::size_t position = static_cast<std::size_t>(value);
  Fetch(&position, 1, thread);
}

FLOAT_IN(FloatIn) {
  (void)inlet;
  // Max's float method on an int attribute is "convert to int", .table's
  // precedent here. The range test keeps a NaN or an infinity — which
  // ExprToInt folds to 0 — from quietly fetching element 0.
  if (!(value >= 0.f && value < 2147483648.f)) {
    Refuse();
    return;
  }
  IntIn(ExprToInt(value), inlet, thread);
}

LIST_IN(ListIn) {
  if (inlet == 1) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — the binding is the creation argument, resolved on the
    // control thread, so there is nothing to set. gDictSlice's inlet rule.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference fetches at the stored index — the message its
  // .array emits on a bang. Anything else naming an array this object is not
  // bound to is refused: resolving an unrecognised name means the registry's
  // mutex, and this may be the audio thread.
  if (ArrayReferenceNames(value, arrayName)) {
    FetchAtIndex(thread);
    return;
  }

  // A list of indices, answered whole. Parsed before the guard is taken;
  // refused whole — nothing fetched, nothing sent — when anything in it is
  // not a non-negative integer, or there are more indices than a reply
  // could carry. The stored index deliberately does not move: a list is a
  // compound fetch answered at the moment it arrives, not a cursor move,
  // and a bang that replayed the last *list* would be hidden state no patch
  // can see.
  std::size_t count = 0;
  std::size_t offset = 0;
  while (!AtEnd(value, offset)) {
    if (count >= MAX_INDICES || !ReadIndex(value, offset, requested[count])) {
      Refuse();
      return;
    }
    count++;
  }
  if (count == 0) {
    Refuse();
    return;
  }
  Fetch(requested, count, thread);
}

void gArrayAt::FetchAtIndex(YSE::THREAD thread) {
  // A negative stored index — which only a creation argument can plant, the
  // inlet refuses one before storing it — is a refusal, not a miss:
  // negative is malformed, where a miss is a well-formed position the array
  // happens not to have.
  const int at = index.load(std::memory_order_relaxed);
  if (at < 0) {
    Refuse();
    return;
  }
  const std::size_t position = static_cast<std::size_t>(at);
  Fetch(&position, 1, thread);
}

void gArrayAt::Fetch(const std::size_t* positions, std::size_t count, YSE::THREAD thread) {
  bool miss = false;
  bool refused = false;
  emitList.Clear();
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    for (std::size_t i = 0; i < count && !miss; i++) {
      if (positions[i] >= store->count) miss = true;
    }
    if (!miss) {
      for (std::size_t i = 0; i < count && !refused; i++) {
        const std::string& element = store->elements[positions[i]];
        // The one refusal a well-formed fetch can still meet: the named
        // elements together spell more list text than a cord carries.
        // Refused whole — a partial reply would misalign every position
        // after the cut, which is truncation by another name.
        if (!emitList.Add(element.data(), element.size())) refused = true;
      }
    }
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well write into this same array, and inside the guard that
  // write would be the one thing the try-lock drops.
  if (miss) {
    outputs[1].SendBang(thread);
    return;
  }
  if (refused) {
    Refuse();
    return;
  }
  // Through SendAtoms, so one element leaves as the int, float or symbol it
  // spells rather than as a list of one — the patcher's transport rule.
  SendAtoms(outputs[0], emitList, emitScratch, thread);
}
