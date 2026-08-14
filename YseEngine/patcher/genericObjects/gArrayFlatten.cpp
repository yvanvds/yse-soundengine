#include "gArrayFlatten.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

#include <cstddef>
#include <memory>
#include <string>

using namespace YSE::PATCHER;

namespace {

  // Doc strings long enough that a constructor stops being readable with
  // them inline — gArray's arrangement.
  constexpr char kFlattenTriggerInletDoc[] =
      "A bang asks for the flattened sequence — every named array's elements in argument "
      "order, everything kept, each source read under its own guard alone and the result sent "
      "after the last is released. \"array <name>\" does the same when it names the first "
      "array bound by the creation arguments — the message an .array's reference outlet emits "
      "on a bang, so wiring that outlet here gives the family's gesture. A reference naming "
      "anything else, or any other message, is refused and counted rather than logged, since "
      "this inlet may be the audio thread.";
  constexpr char kFlattenSourcesInletDoc[] =
      "\"array <name>\" is accepted silently when it names one of the trailing arrays bound by "
      "the creation arguments, so a patch may wire every source's reference outlet across as "
      "it would in Max. It sets nothing — the binding is the creation line, resolved on the "
      "control thread — and anything else, the first array's name included, is refused and "
      "counted: each inlet is bound to its own side.";
  constexpr char kFlattenEmptyOutletDoc[] =
      "Bang when the flatten selected nothing — every named array empty, or unnamed and "
      "therefore private. \"No data\" is a state a patch must be able to route on, not an "
      "error. A lost try-lock on any store is a counted refusal instead: the sources' state "
      "is unknown, so neither outlet fires.";
  constexpr char kFlattenFirstParamDoc[] =
      "The first array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an "
      ".array of the same name in this patcher holds. Resolved once, on the control thread, "
      "which is why no message re-points it at run time. Empty reads a private, empty array on "
      "that side.";
  constexpr char kFlattenOthersParamDoc[] =
      "The remaining arrays' shared names, each bound exactly as the first — the group the "
      "flatten collapses, in this order. What stands where Max's nesting stood: an element is "
      "one atom, so an array cannot hold an array, and a group of named arrays is the value "
      "model's arrays-of-arrays. At most sixteen arrays in all; a creation line that spells "
      "more binds nothing beyond the first and every ask is refused whole until a re-parse — "
      "a flatten that silently dropped an array would be truncation by another name.";

} // namespace

#define className gArrayFlatten

gArrayFlatten::gArrayFlatten() : gArrayEndsBase() {
  // The trigger inlet, hot, and the sources reference inlet, cold — the
  // pair's shape: the result is asked for with a bang, never addressed, so
  // there is no int or float method anywhere.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  // ANY on the result outlet: one element leaves as the atom it spells,
  // several as list text — SendAtoms' rule. The empty outlet is always a
  // bang.
  ADD_OUT_ANY;
  ADD_OUT_BANG;

  // After gArrayEndsBase's first name — the trailing creation arguments,
  // absorbed whole: the last parameter of a list type takes the remainder
  // of the creation line.
  ADD_PARAM(moreNames);

  // The allocation the send path would otherwise need, taken here on the
  // control thread.
  AtomList::ReserveRender(emitScratch);

  ADD_DESCRIPTION(
      "Flattens several named arrays into one sequence — Max's array.flatten on the "
      "name-addressed value model .array settled. An element is one atom, so an array cannot "
      "hold an array; what stands where Max's nesting stood is a group of named arrays, and a "
      "name is resolvable only on the control thread — so the names are creation arguments, "
      "\".array.flatten <first> <second> ... <last>\", every one bound once and none "
      "re-pointable from a message. The result is the arrays' elements in argument order, "
      "everything kept — repeats included, order preserved, .array.concat's keep-everything "
      "walk over as many as sixteen arrays — leaving as the list it spells, never as a new "
      "named array, with every source read under its own guard alone so no two guards are "
      "ever held at once and \".array.flatten seq seq\" answers the sequence doubled. Nothing "
      "is ever written to any store. An empty result bangs the empty outlet; a result that "
      "outruns what a cord carries, a lost try-lock, or a creation line spelling more than "
      "sixteen arrays is refused whole and counted — a partial flatten would be truncation by "
      "another name.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger", kFlattenTriggerInletDoc, "");
  INLET_DOC(1, "sources reference", kFlattenSourcesInletDoc, "");
  OUTLET_DOC(0, "flattened",
             "The named arrays' elements in argument order, as the list they spell — one "
             "element as the int, float or symbol it is, several as list text, never as a new "
             "named array. Everything kept: repeats included, order preserved, no thinning. A "
             "result that outruns what a cord carries is refused whole and counted — a partial "
             "flatten would be truncation by another name. Silent when every source is empty: "
             "the empty outlet carries that half of the answer.",
             "");
  OUTLET_DOC(1, "empty", kFlattenEmptyOutletDoc, "");
  PARAM_DOC("first", "", kFlattenFirstParamDoc, "any identifier");
  PARAM_DOC("others", "", kFlattenOthersParamDoc, "up to 15 identifiers");
}

// A re-parse must not leave half of the previous configuration standing:
// SetParams("") — and the clear half of every Set() — drops the trailing
// names along with the first. gArraySetOpBase's rule for its right name.
PARM_CLEAR() {
  moreNames.clear();
  gArrayEndsBase::ClearParams();
}

// The base calls this after ClearParams / ParseParams have re-read the
// names — the moment the trailing bindings follow the first one, which the
// base's own Rebind() has just re-anchored.
void gArrayFlatten::ParamsChanged() {
  RebindExtras();
}

void gArrayFlatten::SetParent(pObject* newParent) {
  gArrayEndsBase::SetParent(newParent);
  RebindExtras();
}

void gArrayFlatten::RefreshBinding() {
  gArrayEndsBase::RefreshBinding();
  RebindExtras();
}

void gArrayFlatten::RebindExtras() {
  // More arrays than the slot table holds is the configuration refused
  // whole: nothing beyond the first is bound, and Ask() refuses — counted
  // there, at the moment a patch can observe it. See the class notes.
  overflowed = moreNames.size() > MAX_SOURCES - 1;
  const std::size_t wanted = overflowed ? 0 : moreNames.size();

  for (std::size_t i = 0; i < wanted; i++) {
    // The exact mirror of gArrayEndsBase::Rebind over each trailing name.
    // No patcher to prefix with means no address — and no address means a
    // private, empty array on that side. (A parsed token is never empty.)
    std::string address;
    if (!moreNames[i].empty() && parent != nullptr) {
      auto* p = static_cast<patcherImplementation*>(parent);
      address = p->Name() + "." + moreNames[i];
    }

    // Unchanged binding: keep the store. A live SetParams that leaves a
    // name alone must not re-anchor it, and neither must the second rebind
    // a Set() makes (clear, then parse).
    if (extras[i].store != nullptr && address == extras[i].address) continue;

    bool created = false;
    extras[i].store = address.empty() ? std::make_shared<arrayStore>()
                                      : AcquireNamedStore<arrayStore>(address, created);
    extras[i].address = address;
  }

  // Slots past the current configuration release their stores — a shorter
  // creation line must not keep the longer one's arrays alive.
  for (std::size_t i = wanted; i < MAX_SOURCES - 1; i++) {
    extras[i].store.reset();
    extras[i].address.clear();
  }
  extraCount = wanted;
}

std::string gArrayFlatten::ExtraName(std::size_t index) const {
  if (index >= extraCount) return std::string();
  return moreNames[index];
}

std::string gArrayFlatten::ExtraAddress(std::size_t index) const {
  if (index >= extraCount) return std::string();
  return extras[index].address;
}

BANG_IN(BangIn) {
  (void)inlet;
  Ask(thread);
}

LIST_IN(ListIn) {
  if (inlet == 1) {
    // The sources inlet acknowledges the arrays it is already bound to and
    // nothing more — at most MAX_SOURCES - 1 bounded compares, never a
    // registry lookup. The first array's name belongs to the trigger inlet:
    // each inlet is bound to its own side, .array.concat's rule.
    for (std::size_t i = 0; i < extraCount; i++) {
      if (ArrayReferenceNames(value, moreNames[i])) return;
    }
    Refuse();
    return;
  }

  // The first array's reference asks for the flattened sequence — the
  // message its .array emits on a bang, the family's gesture. Anything
  // else, a trailing array's reference included, is refused: resolving an
  // unrecognised name means the registry's mutex, and this may be the audio
  // thread.
  if (ArrayReferenceNames(value, arrayName)) {
    Ask(thread);
    return;
  }
  Refuse();
}

void gArrayFlatten::Ask(YSE::THREAD thread) {
  // A creation line that outran the slot table is the configuration refused
  // whole — no partial flatten leaves, however the ask arrived.
  if (overflowed) {
    Refuse();
    return;
  }

  // Every source under its own guard alone, in argument order — no snapshot
  // and no two guards at once, so one array named twice answers doubled
  // instead of tripping over its own try-lock. Each hold is a moment; a
  // write landing between two of them shows exactly as it would had the ask
  // arrived after it. Any guard lost, or a result past what a cord carries,
  // refuses the ask whole: a flatten missing an array in the middle would
  // be truncation by another name.
  bool collected = true;
  result.Clear();
  for (std::size_t source = 0; source <= extraCount && collected; source++) {
    arrayStore* table = source == 0 ? store.get() : extras[source - 1].store.get();
    const arrayStoreGuard guard(table->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    for (std::size_t i = 0; i < table->count; i++) {
      const std::string& element = table->elements[i];
      if (!result.Add(element.data(), element.size())) {
        collected = false;
        break;
      }
    }
  }

  // Outside every guard on purpose: a send runs the whole downstream graph,
  // which may well write into any of these arrays, and inside a guard that
  // write would be the one thing the try-lock drops.
  if (!collected) {
    Refuse();
    return;
  }
  if (result.Empty()) {
    outputs[1].SendBang(thread);
    return;
  }
  SendAtoms(outputs[0], result, emitScratch, thread);
}
