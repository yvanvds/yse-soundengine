#include "gArrayRegexp.h"
#include "../../implementations/logImplementation.h"
#include "../pAtomList.h"
#include "../pObjectList.hpp"

#include <cstddef>
#include <string>

using namespace YSE::PATCHER;
#define className gArrayRegexp

namespace {

  // Doc strings, hoisted out of the constructor because they are long enough
  // that the constructor stops being readable with them inline — gArray's
  // arrangement.
  constexpr char kTriggerInletDoc[] =
      "Bang, or the bound array's reference \"array <name>\": match every element against the "
      "pattern. Refused when the pattern is missing or did not compile.";
  constexpr char kReferenceInletDoc[] =
      "Accepts the bound array's reference \"array <name>\" and ignores it, so the array's "
      "reference outlet can be wired here. Anything else is refused.";
  constexpr char kMatchedOutletDoc[] =
      "The matching elements, in order, as one list; a single match leaves as an int, float or "
      "symbol.";
  constexpr char kNoMatchOutletDoc[] = "Bang when no element matches, or the array is empty.";
  constexpr char kNameParamDoc[] =
      "Name of the array to search: the .array with this name in the same patcher. Empty "
      "searches a private, empty array.";
  constexpr char kPatternParamDoc[] =
      "The regular expression, as one word in .regexp syntax; write \\s for a space. A "
      "malformed pattern is logged and the object then does nothing.";

} // namespace

gArrayRegexp::gArrayRegexp() : gArrayEndsBase() {
  // The trigger inlet, hot, and the reference inlet, cold — gArrayStatsBase's
  // shape: the operation is asked for with a bang, never addressed, so there
  // is no int or float method anywhere. A bare number names no array.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  // The matched outlet, then the no-match outlet. ANY on the matches: one
  // matched element leaves as the int, float or symbol it spells, several as
  // one list — SendAtoms' rule. The no-match half is always a bang.
  ADD_OUT_ANY;
  ADD_OUT_BANG;

  // After gArrayEndsBase's name — the pattern, one token.
  ADD_PARAM(pattern);

  // The allocation SendAtoms' render would otherwise need, taken here on the
  // control thread.
  AtomList::ReserveRender(emitScratch);

  ADD_DESCRIPTION(
      "Outputs the elements of a named array that match a regular expression, as one list. "
      "When nothing matches, the right outlet bangs. The array itself is not changed.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "matched", kMatchedOutletDoc, "");
  OUTLET_DOC(1, "no match", kNoMatchOutletDoc, "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("pattern", "", kPatternParamDoc, "any pattern");
}

// A re-parse must not leave half of the previous configuration standing:
// SetParams("") — and the clear half of every Set() — drops the program along
// with the name, so a trigger after a reset refuses rather than matching
// whatever the previous arguments compiled. gArrayExprBase's rule.
void gArrayRegexp::ClearParams() {
  pattern.clear();
  program.Clear();
  compileError.clear();
  gArrayEndsBase::ClearParams();
}

void gArrayRegexp::ParamsChanged() {
  CompilePattern();
}

void gArrayRegexp::CompilePattern() {
  compileError.clear();

  // No pattern at all — a bare or name-only object, or the clear half of a
  // re-parse. Nothing malformed to report: the object simply refuses
  // triggers until it is given work. gArrayExprBase's rule.
  if (pattern.empty()) {
    program.Clear();
    return;
  }

  // The one compile. Control thread, at construction or on SetParams; a live
  // SetParams cannot reach here in place because the registered clear/parse
  // callbacks make ParamsNeedRebuild() true (issue #234). A failure is loud —
  // .regexp's rule: a malformed pattern must fail here, at parse time, rather
  // than silently at trigger time. Compile() already left the program
  // invalid, so every trigger then refuses — never "matched nothing", which
  // is one of this object's honest answers and must stay one.
  if (!program.Compile(pattern)) {
    compileError = program.Error();
    INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + Type() + " cannot compile \"" +
                                          pattern + "\": " + compileError);
  }
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  Match(thread);
}

LIST_IN(ListIn) {
  if (inlet == 1) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — gDictSlice's inlet rule, the family's shape.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference matches — the message its .array emits on a bang,
  // the family's gesture. Anything else, including a reference naming an
  // array this object is not bound to, is refused: resolving an unrecognised
  // name means the registry's mutex, and this may be the audio thread.
  if (ArrayReferenceNames(value, arrayName)) {
    Match(thread);
    return;
  }
  Refuse();
}

void gArrayRegexp::Match(YSE::THREAD thread) {
  // No program, no match: an absent or malformed pattern was reported when
  // the parameter was set (see CompilePattern), and a trigger on the
  // misconfigured object is a counted refusal — never a no-match bang, which
  // would be a plausible-looking wrong answer.
  if (!program.Valid()) {
    Refuse();
    return;
  }

  // The whole RT budget for this trigger: one counter shared by every
  // element's match, so the scan as a whole is bounded by a number chosen
  // here rather than by the shape of the pattern.
  int budget = kArrayRegexpStepBudget;
  bool refused = false;
  emitList.Clear();
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    // One bounded pass under one hold — the family's mid-walk answer: there
    // is no walk to be in the middle of, so the answer is the array as it
    // stood at the trigger. Search is a fixed-stack, budget-bounded walk of
    // the compiled program, which is why running it under the guard is
    // legitimate where a send never is (gArrayExprBase's argument for
    // Evaluate).
    for (std::size_t i = 0; i < store->count; i++) {
      const std::string& element = store->elements[i];
      RegexMatch found;
      if (!program.Search(element.c_str(), (int)element.size(), 0, found, budget)) {
        // A miss with the budget dry may be an unfinished match, not a miss
        // — the two are indistinguishable past that point, so the honest
        // answer is no answer: refuse the whole ask.
        if (budget <= 0) {
          refused = true;
          break;
        }
        continue;
      }
      // Whole answer or nothing — .array.sect's rule: a matched list that
      // lost members would lie about what matched, so an element that does
      // not fit refuses the whole ask rather than shortening it.
      if (!emitList.Add(element.c_str(), element.size())) {
        refused = true;
        break;
      }
    }
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well write into this same array, and inside the guard that
  // write would be the one thing the try-lock drops.
  if (refused) {
    Refuse();
    return;
  }
  if (emitList.Empty()) {
    outputs[1].SendBang(thread);
    return;
  }
  // Through SendAtoms, so one matched element leaves as the value it is
  // rather than as a list of one — the patcher's transport rule.
  SendAtoms(outputs[0], emitList, emitScratch, thread);
}
