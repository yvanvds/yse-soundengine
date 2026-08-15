// `.array.routepass` (issue #804). See gArrayRoutepass.h for the design;
// this file is one bounded presence scan and one SendList of a reference the
// object already owns.
#include "gArrayRoutepass.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

#include <cstddef>
#include <string>

using namespace YSE::PATCHER;
#define className gArrayRoutepass

namespace {

  constexpr std::size_t kReferenceLength = sizeof(kArrayReferenceWord) - 1;

  // Doc strings, hoisted out of the constructor because they are long enough
  // that the constructor stops being readable with them inline — gArray's
  // arrangement. Kept at namespace scope so ShapePorts can re-document the
  // ports it rebuilds on a re-parse.
  constexpr char kTriggerInletDoc[] =
      "A bang routes the bound array: it is tested for each value argument in order, and the "
      "reference \"array <name>\" leaves the outlet of the leftmost value present — exactly one "
      "outlet fires per trigger, and an array holding none of the values leaves the rightmost "
      "reject outlet instead. \"array <name>\" does the same when it names the array bound by "
      "the creation argument — the message an .array's reference outlet emits on a bang, so "
      "wiring that outlet here gives Max's own gesture: bang the array, and it dispatches "
      "itself. A reference naming anything else, any other message, or a trigger arriving "
      "while a route is already in flight — a cord looped back from an outlet, or another "
      "thread — is refused and counted rather than logged, since this inlet may be the audio "
      "thread. An unnamed .array.routepass is inert: its private array has no name to pass on, "
      "so a trigger routes nothing, silently.";
  constexpr char kReferenceInletDoc[] =
      "\"array <name>\" is accepted silently when it names the array bound by the creation "
      "argument, so a patch may wire the array's reference outlet across. It sets nothing — "
      "the binding is the creation argument, resolved on the control thread — and anything "
      "else is refused and counted.";
  constexpr char kRejectDoc[] =
      "The array's reference, when none of the value arguments is an element of it — Max's "
      "rightmost outlet, present whatever the argument count. The reference is unchanged, so "
      "chaining this into the next .array.routepass carries on testing the array the first one "
      "saw. An empty array — a private, unnamed store included — holds none of the values, so "
      "it always leaves here.";
  constexpr char kNameParamDoc[] =
      "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an .array "
      "of the same name in this patcher holds. Resolved once, on the control thread, which is "
      "why no message re-points it at run time. Empty leaves the object inert: a private array "
      "has no name to pass on, so there is nothing to route.";
  constexpr char kValuesParamDoc[] =
      "One value per match outlet, in order, plus a rightmost reject outlet. A value matches "
      "when some element of the array spells it exactly — the family's byte compare, so 5 and "
      "5.0 are different values and a symbol matches by its text. The leftmost value present "
      "wins, and a value repeated in the argument list uses the leftmost of its outlets only. "
      "An empty token, or one past 64 characters (which no stored element can spell), matches "
      "nothing and costs an unreachable outlet. With no values at all the object has only the "
      "reject outlet — Max documents no default key for array.routepass, and inventing one "
      "would put a branch in a patch that did not ask for one. At most 256 are held.";

  std::string MatchDoc(const std::string& selector) {
    return "The array's reference, when \"" + selector +
           "\" is the leftmost value argument the array holds an element spelling. What leaves "
           "is \"array <name>\", never the contents: the receiver binds the name itself. A "
           "value repeated in the argument list uses the leftmost of its outlets only.";
  }

} // namespace

gArrayRoutepass::gArrayRoutepass() : gArrayEndsBase() {
  // The trigger inlet, hot, and the reference inlet, cold — gArrayStatsBase's
  // shape: the route is asked for with a bang, never addressed, so there is
  // no int or float method anywhere. A bare number names no array.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  // After gArrayEndsBase's name — the values, one outlet each.
  ADD_PARAM(selectorArgs);

  // The outlets are built by ShapePorts(), because the argument list *is*
  // the outlet list — gRoute's arrangement. Bare at construction: the
  // reject outlet alone.
  ShapePorts();

  ADD_DESCRIPTION(
      "Routes an array by the values it holds — Max's array.routepass on the name-addressed "
      "value model .array settled: the array is bound from the first creation argument and "
      "every argument after it is a value declaring one outlet, plus a rightmost reject — "
      "\".array.routepass <name> <value> [<value> ...]\" — because an array is addressed by "
      "name and never passed down a cord. The dispatcher for arrays: what .route does for list "
      "text and .dict.route does for dictionaries, this object does for sequences — .route's "
      "job with the array as the selector. A trigger — a bang, or the bound array's \"array "
      "<name>\" reference, the message an .array's reference outlet emits — tests the array "
      "for each value in argument order and sends the reference, never the contents, out the "
      "outlet of the leftmost value present: the routing decision is about which branch, and "
      "the receiver still binds the name itself. Exactly one outlet fires per trigger; an "
      "array holding none of the values leaves the rightmost outlet with its reference "
      "unchanged, so a chain of .array.routepass objects strings together with each reject "
      "feeding the next inlet. A value is present when some element spells it exactly — the "
      "family's byte compare, so 5 and 5.0 are different values, a deliberate divergence from "
      "the scalar .routepass's numeric widening: an array element is its spelling. Ported as "
      "the presence question — anywhere in the array, #804's reading of Max's match modes — "
      "because first-element routing is already .array.at into .routepass; Max's special empty "
      "keys are not ported, an empty array simply leaving the reject. With no values the "
      "object has only the reject outlet, since Max documents no default key. The decision is "
      "at most 256 bounded scans under one hold of the store's try-lock guard, released before "
      "the send, and the reference is built once per re-parse, so no message path allocates, "
      "locks or blocks and Calculate() does nothing. A trigger that loses the guard, a "
      "reference naming an unbound array, and a trigger arriving while a route is in flight "
      "are refused and counted rather than resolved, a registry lookup being a mutex on "
      "whatever thread the message arrived on. Only the creation arguments persist across a "
      "save; the array's contents persist with the .array that owns them.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "route", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("values", "", kValuesParamDoc, "any list of numbers and symbols");
}

// ─── creation arguments ───────────────────────────────────────────────────────

// A re-parse must not leave half of the previous configuration standing:
// SetParams("") — and the clear half of every Set() — drops the values along
// with the name, going back to an inert object with only the reject outlet.
// gArrayExprBase's rule, plus gRoute's for the ports; the base's ClearParams
// re-runs ParamsChanged, which rebuilds selectors, reference and outlets
// from the now-empty arguments.
void gArrayRoutepass::ClearParams() {
  selectorArgs.clear();
  gArrayEndsBase::ClearParams();
}

void gArrayRoutepass::ParamsChanged() {
  // Every token kept, empty ones included, so an index into the arguments
  // is an index into `outputs` — gDictRoute's rule. An empty or over-long
  // value can never be stored, so it costs an unreachable outlet and
  // nothing else. Tokens past MAX_SELECTORS are dropped — every value costs
  // an outlet, .routepass's ceiling.
  selectors.clear();
  selectors.reserve(selectorArgs.size());
  for (const std::string& token : selectorArgs) {
    if (selectors.size() >= (std::size_t)MAX_SELECTORS) break;
    selectors.push_back(token);
  }

  RefreshReference();
  ShapePorts();
}

void gArrayRoutepass::RefreshReference() {
  reference.clear();
  if (arrayName.empty()) return;
  reference.reserve(kReferenceLength + 1 + arrayName.size());
  reference += kArrayReferenceWord;
  reference += ' ';
  reference += arrayName;
}

void gArrayRoutepass::ShapePorts() {
  // Rebuilt rather than resized, because the reject outlet has to stay
  // rightmost: appending a match outlet would put it after the reject and
  // every saved cord past that point would land on the wrong port. Safe
  // because every caller runs before the object is wired or published — the
  // constructor, and the parameter hooks, which
  // patcherImplementation::CreateObjectUnlocked runs before AssignGraphIds.
  // A *live* SetParams never reaches here on a published object —
  // registering the callbacks makes ParamsNeedRebuild() true, so #234
  // replaces the object.
  outputs.clear();

  for (std::size_t i = 0; i < selectors.size(); i++) {
    // LIST, because the reference is a list message — the kind every other
    // reference outlet in the family carries.
    ADD_OUT_LIST;
    outputs.back().SetDoc(MatchOutletLabel((int)i), MatchDoc(selectors[i]), selectors[i]);
  }

  // Max's rightmost outlet, present whatever the argument count. The
  // reference leaves it unchanged, which is what lets a chain of
  // .array.routepass objects be strung together with each reject feeding
  // the next inlet.
  ADD_OUT_LIST;
  outputs.back().SetDoc("none", kRejectDoc, "");
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  Route(thread);
}

LIST_IN(ListIn) {
  if (inlet == 1) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — gDictSlice's inlet rule, the family's shape.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference routes — the message its .array emits on a bang,
  // the family's gesture. Anything else, including a reference naming an
  // array this object is not bound to, is refused: resolving an
  // unrecognised name means the registry's mutex, and this may be the audio
  // thread.
  if (ArrayReferenceNames(value, arrayName)) {
    Route(thread);
    return;
  }
  Refuse();
}

// ─── the route ────────────────────────────────────────────────────────────────

void gArrayRoutepass::Route(YSE::THREAD thread) {
  // An unnamed object is inert: its private array has no name to pass on —
  // gArray's rule for an unnamed reference — so there is nothing any outlet
  // could carry. Silent rather than counted, because the wiring is not an
  // error, merely incomplete.
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

  // The decision, under the store's guard alone: the leftmost value
  // present. At most MAX_SELECTORS bounded scans of at most MAX_ELEMENTS
  // elements, and nothing else, so the guard is held for exactly the
  // question. Released before the send, which runs the whole downstream
  // subgraph and may well write into this same array; inside the guard that
  // write would be the one thing the try-lock drops.
  int hit = -1;
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      busy.store(false, std::memory_order_release);
      return;
    }
    for (std::size_t i = 0; i < selectors.size(); i++) {
      const std::string& value = selectors[i];
      // An empty or over-long token can never be stored, so its outlet is
      // unreachable — skipped here rather than asked, since ArrayFind
      // compares against elements the store refused to hold.
      if (value.empty() || value.size() > arrayStore::ELEMENT_CAPACITY) continue;
      if (ArrayFind(*store, value.data(), value.size()) < store->count) {
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

std::string gArrayRoutepass::SelectorAt(std::size_t index) const {
  if (index >= selectors.size()) return std::string();
  return selectors[index];
}
