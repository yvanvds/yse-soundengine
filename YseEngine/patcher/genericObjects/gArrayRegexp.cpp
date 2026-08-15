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
      "A bang matches every element of the bound array against the pattern and sends the "
      "matched ones as one typed message out the matched outlet — or bangs the no-match outlet "
      "when nothing matched. \"array <name>\" does the same when it names the array bound by "
      "the creation argument — the message an .array's reference outlet emits on a bang, so "
      "wiring that outlet here gives the family's gesture. A reference naming anything else, "
      "any other message, or a trigger on an object whose pattern is absent or did not compile, "
      "is refused and counted rather than logged, since this inlet may be the audio thread.";
  constexpr char kReferenceInletDoc[] =
      "\"array <name>\" is accepted silently when it names the array bound by the creation "
      "argument, so a patch may wire the array's reference outlet across. It sets nothing — the "
      "binding is the creation argument, resolved on the control thread — and anything else is "
      "refused and counted.";
  constexpr char kMatchedOutletDoc[] =
      "The elements the pattern matched, as one typed message — order kept, repeats kept, one "
      "match leaving as the int, float or symbol it spells and several as one list, SendAtoms' "
      "rule. The answer is the array as it stood at the trigger: the whole scan is one hold of "
      "the store's guard, and the send happens after it is released. A matched list is an "
      "answer about membership, so it never leaves as a fragment of itself: one that cannot "
      "leave whole (past what a cord carries), a scan that ran out of matching budget, and a "
      "lost try-lock each refuse the whole ask, counted, nothing sent on either outlet.";
  constexpr char kNoMatchOutletDoc[] =
      "Bang when the ask landed and nothing matched — an empty or unnamed (private) array "
      "included. \"No data\" is a state a patch must be able to route on, not an error — "
      ".array.sect's empty outlet, for .array.sect's reason. A refused ask is a counted "
      "refusal instead: neither outlet fires.";
  constexpr char kNameParamDoc[] =
      "The array's shared name, addressed as \"<patcherName>.<name>\" — the sequence an .array "
      "of the same name in this patcher holds. Resolved once, on the control thread, which is "
      "why no message re-points it at run time. Empty matches over a private, empty array: "
      "always the no-match bang.";
  constexpr char kPatternParamDoc[] =
      "The regular expression, as one token — write \\s or \\x20 rather than a literal space, "
      "which the parameter tokenizer would eat. The language is .regexp's: a PCRE-flavoured "
      "core with classes, shorthands, anchors, quantifiers, alternation and groups, compiled "
      "once on the control thread by the same bounded engine — read .regexp for the full "
      "grammar and what it rejects. An element matches when the pattern matches anywhere in "
      "it; anchor with ^ and $ for a whole-element match. A malformed pattern is reported to "
      "the log when the parameter is set, and the object then refuses every trigger, counted.";

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
      "Keeps the elements of an array a regular expression matches — issue #803's reading of "
      "Max's array.regexp on the name-addressed value model .array settled: the array is bound "
      "from the creation argument, \".array.regexp <name> <pattern>\", because an array is "
      "addressed by name and never passed down a cord, and the pattern is one token, compiled "
      "once on the control thread by .regexp's own bounded engine — read .regexp for the "
      "grammar; no std::regex, which allocates, throws and backtracks without limit at match "
      "time. A bang, or the array's reference message \"array <name>\", matches every element "
      "as the text that spells it — the pattern matches anywhere in the element, so anchor "
      "with ^ and $ for a whole-element match — and sends the matched ones as one typed "
      "message, order kept, repeats kept; when nothing matched, the no-match outlet bangs "
      "instead. The text filter .array.filter cannot be, since its expression never sees a "
      "symbol; the per-element reporting — matched substrings, capture groups, substitution — "
      "is deliberately .regexp's job, already composable as .array.iter into .regexp, where "
      "Max's own array.regexp treats the array as one subject buffer, a byte-level operation "
      "the value model does not have. The whole scan is one hold of the store's guard with a "
      "step budget shared across every element, and the answer never leaves as a fragment of "
      "itself: a matched list past what a cord carries, a scan that ran out of budget, and a "
      "lost try-lock each refuse the whole ask, counted. A malformed pattern is reported when "
      "the parameter is set and the object then refuses every trigger.");
  ADD_CATEGORY(pCategory::GENERIC);
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
