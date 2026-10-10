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
      "Bang, or the first array's reference \"array <name>\": send all the arrays as one list.";
  constexpr char kFlattenSourcesInletDoc[] =
      "Accepts the reference \"array <name>\" of any array after the first and ignores it, so "
      "their reference outlets can be wired here. Anything else is refused.";
  constexpr char kFlattenEmptyOutletDoc[] = "Bang when every array is empty.";
  constexpr char kFlattenFirstParamDoc[] =
      "Name of the first array: the .array with this name in the same patcher. Empty uses a "
      "private, empty array.";
  constexpr char kFlattenOthersParamDoc[] =
      "The names of up to 15 more arrays, in the order their elements are sent. With more than "
      "16 arrays in all, every bang is refused.";

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
      "Outputs the elements of several named arrays as one list, in the order the arrays are "
      "named, up to 16 arrays. None of the arrays is changed.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kFlattenTriggerInletDoc, "");
  INLET_DOC(1, "sources reference", kFlattenSourcesInletDoc, "");
  OUTLET_DOC(0, "flattened",
             "Every array's elements, in the order the arrays are named, as one list. Repeats "
             "are kept. A single element leaves as an int, float or symbol.",
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
      address = p->ScopedAddress(moreNames[i]);
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
