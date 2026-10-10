#include "gArrayExpr.h"
#include "../../implementations/logImplementation.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"

#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

namespace {

  // Doc strings shared by the seven objects, hoisted out of the constructors
  // because they are long enough that a constructor stops being readable with
  // them inline — gArray's arrangement. What differs per object stays with
  // that object below.
  constexpr char kTriggerInletDoc[] =
      "Bang, or the bound array's reference \"array <name>\": run the expression over the "
      "array. Refused when the expression did not compile.";
  constexpr char kReferenceInletDoc[] =
      "Accepts the bound array's reference \"array <name>\" and ignores it, so the array's "
      "reference outlet can be wired here. Anything else is refused.";
  constexpr char kNameParamDoc[] =
      "Name of the array to use: the .array with this name in the same patcher. Empty uses a "
      "private, empty array.";
  constexpr char kExprParamDoc[] =
      "The expression: the rest of the arguments, in .expr syntax. $f1 or $i1 is the element (as "
      "a float or an int), $f2 or $i2 its position. It only runs for numeric elements. A "
      "malformed expression is logged and the object then does nothing.";
  constexpr char kFoldParamDoc[] =
      "The expression: the rest of the arguments, in .expr syntax. $f1 or $i1 is the running "
      "result, $f2 or $i2 the element and $f3 or $i3 its position. It only runs for numeric "
      "elements. A malformed expression is logged and the object then does nothing.";
  constexpr char kMutateOutletDoc[] =
      "The array's reference \"array <name>\" after each change, so further .array objects can "
      "be chained. Nothing is sent when the object has no name.";

} // namespace

// ─── the shared base ──────────────────────────────────────────────────────────

#define className gArrayExprBase

gArrayExprBase::gArrayExprBase(int varLimit) : gArrayEndsBase(), varLimit(varLimit) {
  // The trigger inlet, hot, and the reference inlet, cold — gArrayStatsBase's
  // shape: the operation is asked for with a bang, never addressed, so there
  // is no int or float method anywhere. A bare number names no array, and on
  // the streaming object it would be .uzi's trap besides.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);
  ADD_IN_1;
  REG_LIST_IN(ListIn);

  // After gArrayEndsBase's name — the trailing LIST param absorbs every
  // remaining creation argument, so a whitespace-bearing expression survives
  // Parameters::Set intact. gExpr's arrangement, behind the name.
  ADD_PARAM(expression);
}

// A re-parse must not leave half of the previous configuration standing:
// SetParams("") — and the clear half of every Set() — drops the program along
// with the name, so a trigger after a reset refuses rather than running
// whatever the previous arguments compiled. gArrayFill's rule.
void gArrayExprBase::ClearParams() {
  expression.clear();
  program.Clear();
  compileError.clear();
  gArrayEndsBase::ClearParams();
}

void gArrayExprBase::ParamsChanged() {
  CompileExpression();
}

void gArrayExprBase::CompileExpression() {
  // Parameters::Set split the argument string on spaces; glue it back
  // together before compiling. Whitespace is not significant inside an
  // expression, and GetParams() still returns the string the caller passed,
  // so a DumpJSON round trip is exact. gExpr's arrangement.
  compileError.clear();
  std::string source;
  for (std::size_t i = 0; i < expression.size(); i++) {
    if (i != 0) source += ' ';
    source += expression[i];
  }

  // No expression at all — a bare or name-only object, or the clear half of
  // a re-parse. Nothing malformed to report: the object simply refuses
  // triggers until it is given work.
  if (source.empty()) {
    program.Clear();
    return;
  }

  // The one compile. Control thread, at construction or on SetParams; a live
  // SetParams cannot reach here in place because the registered clear/parse
  // callbacks make ParamsNeedRebuild() true (issue #234). A failure is loud —
  // .expr's rule: a malformed expression must fail here, at parse time,
  // rather than silently at trigger time.
  if (!program.Compile(source)) {
    compileError = program.Error();
    INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + Type() + " cannot compile \"" +
                                          source + "\": " + compileError);
    return;
  }

  // A placeholder past what this object binds names an inlet that can never
  // exist — $3 in a map, $4 in a fold. Rejected here, loudly, rather than
  // silently evaluating the 0 an unset var holds.
  if (program.InletCount() > varLimit) {
    compileError = "the expression may only reference $1..$" + std::to_string(varLimit) + " here";
    INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + Type() + " cannot use \"" +
                                          source + "\": " + compileError);
    program.Clear();
    return;
  }
}

// ─── the shared trigger ───────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  Ask(thread);
}

LIST_IN(ListIn) {
  if (inlet == 1) {
    // The reference inlet acknowledges the array it is already bound to and
    // nothing more — gDictSlice's inlet rule, the family's shape.
    if (!ArrayReferenceNames(value, arrayName)) Refuse();
    return;
  }

  // The array's reference runs the operation — the message its .array emits
  // on a bang, the family's gesture. Anything else, including a reference
  // naming an array this object is not bound to, is refused: resolving an
  // unrecognised name means the registry's mutex, and this may be the audio
  // thread.
  if (ArrayReferenceNames(value, arrayName)) {
    Ask(thread);
    return;
  }
  Refuse();
}

void gArrayExprBase::Ask(YSE::THREAD thread) {
  // No program, no operation: a malformed or absent expression was reported
  // when the parameter was set (see CompileExpression), and a trigger on the
  // misconfigured object is a counted refusal — never .expr's evaluate-to-0,
  // whose 0 goes down a cord where this family's would be written into a
  // shared array.
  if (!program.Valid()) {
    Refuse();
    return;
  }
  Trigger(thread);
}

// ─── .array.expr ──────────────────────────────────────────────────────────────

#undef className
#define className gArrayExpr

gArrayExpr::gArrayExpr() : gArrayExprBase(2) {
  // ANY on the result: what leaves is typed — the expression's ints and
  // floats, a passed-through symbol, a one-element answer as the value it is.
  // The empty outlet is always a bang.
  ADD_OUT_ANY;
  ADD_OUT_BANG;

  // The allocation the send path would otherwise need, taken here on the
  // control thread.
  AtomList::ReserveRender(emitScratch);

  ADD_DESCRIPTION(
      "Evaluates an expression for each element of a named array and sends the results as one "
      "list, for example $i1 + 12 to transpose. The array itself is not changed.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "results",
             "One result per element, in order; words pass through unchanged. A single result "
             "leaves as an int or float, several as a list.",
             "");
  OUTLET_DOC(1, "empty", "Bang when the array is empty.", "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("expression", "", kExprParamDoc, "any expression over $1 and $2");
}

void gArrayExpr::Trigger(YSE::THREAD thread) {
  std::size_t lost = 0;
  emitList.Clear();
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    // One bounded pass under one hold — the family's mid-walk answer: there
    // is no walk to be in the middle of, so the answer is the array as it
    // stood at the trigger. Evaluate is a fixed-stack walk, which is why
    // running it under the guard is legitimate where a send never is.
    for (std::size_t i = 0; i < store->count; i++) {
      const std::string& element = store->elements[i];
      float value = 0.f;
      if (ReadNumericToken(element.data(), element.size(), value)) {
        const ExprValue result = Eval(value, i);
        const bool added = result.isInt ? emitList.AddInt(result.i) : emitList.AddFloat(result.f);
        if (!added) lost++;
      } else if (!emitList.Add(element.data(), element.size())) {
        // The pass-through — decision 3 on the base: the element itself
        // stands in for the result the expression never produced.
        lost++;
      }
    }
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well write into this same array, and inside the guard that
  // write would be the one thing the try-lock drops. A list that lost its
  // tail is a shorter list, and the patch can see the count — getvalue's
  // rule, one counted refusal per lost element.
  for (std::size_t i = 0; i < lost; i++)
    Refuse();
  if (emitList.Empty()) {
    outputs[1].SendBang(thread);
    return;
  }
  // Through SendAtoms, so one result leaves as the value it is rather than
  // as a list of one — the patcher's transport rule.
  SendAtoms(outputs[0], emitList, emitScratch, thread);
}

// ─── the mutating pair's shared body ──────────────────────────────────────────

#undef className
#define className gArrayExprMutateBase

gArrayExprMutateBase::gArrayExprMutateBase(int varLimit) : gArrayExprBase(varLimit) {
  // The reference, always list text — gArrayEndsWriter's outlet.
  ADD_OUT_LIST;
}

void gArrayExprMutateBase::ParamsChanged() {
  gArrayExprBase::ParamsChanged();
  RefreshReference();
}

void gArrayExprMutateBase::RefreshReference() {
  reference.clear();
  if (arrayName.empty()) return;
  reference.reserve(sizeof(kArrayReferenceWord) + arrayName.size());
  reference += kArrayReferenceWord;
  reference += ' ';
  reference += arrayName;
}

void gArrayExprMutateBase::Trigger(YSE::THREAD thread) {
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    // The whole rewrite under one hold — the family's mid-walk answer: the
    // array goes from untouched to finished in one step, no reader ever
    // sees it half-done, and a writer on another thread loses the try-lock
    // while it runs (dropped and counted, the store's rule).
    ApplyLocked();
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well act on this same array, and inside the guard that access
  // would be the one thing the try-lock drops. An unnamed object has no
  // name to pass on — the rewrite happened, the announcement is simply
  // empty.
  if (reference.empty()) return;
  outputs[0].SendList(reference, thread);
}

// ─── .array.map ───────────────────────────────────────────────────────────────

#undef className
#define className gArrayMap

gArrayMap::gArrayMap() : gArrayExprMutateBase(2) {
  ADD_DESCRIPTION(
      "Applies an expression to every numeric element of a named array and writes the results "
      "back, for example $i1 + 12 to transpose. Words stay where they are.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "reference", kMutateOutletDoc, "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("expression", "", kExprParamDoc, "any expression over $1 and $2");
}

void gArrayMap::ApplyLocked() {
  for (std::size_t i = 0; i < store->count; i++) {
    std::string& element = store->elements[i];
    float value = 0.f;
    // The pass-through — decision 3 on the base: a symbol element stays
    // exactly where and what it was, so every position survives.
    if (!ReadNumericToken(element.data(), element.size(), value)) continue;

    // The result, spelled the way the patcher spells a number. At most
    // kExprValueTextMax - 1 characters, which always fits ELEMENT_CAPACITY,
    // and the assign lands in storage the store reserved at construction —
    // nothing here allocates.
    const ExprValue result = Eval(value, i);
    char text[kExprValueTextMax];
    const int written = ExprFormatValue(result, text, kExprValueTextMax);
    if (written <= 0) {
      // Unreachable by construction — counted all the same rather than
      // silently keeping the old element, since a partial map is the one
      // thing this object promises not to do.
      Refuse();
      continue;
    }
    element.assign(text, static_cast<std::size_t>(written));
  }
}

// ─── .array.filter ────────────────────────────────────────────────────────────

#undef className
#define className gArrayFilter

gArrayFilter::gArrayFilter() : gArrayExprMutateBase(2) {
  ADD_DESCRIPTION(
      "Removes from a named array every element for which an expression is false (zero), "
      "keeping the rest in order. Words are removed too, because the expression only sees "
      "numbers.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "reference", kMutateOutletDoc, "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("expression", "", kExprParamDoc, "any expression over $1 and $2");
}

void gArrayFilter::ApplyLocked() {
  const std::size_t old = store->count;
  std::size_t kept = 0;
  for (std::size_t i = 0; i < old; i++) {
    const std::string& element = store->elements[i];
    float value = 0.f;
    // A symbol is never accepted: the expression cannot accept what it
    // cannot see — decision 3 on the base, filter's own reading.
    if (!ReadNumericToken(element.data(), element.size(), value)) continue;
    if (!Accepts(Eval(value, i))) continue;
    // Close ranks in original order — bounded assigns into storage the
    // store reserved at construction, a position never moving up.
    if (kept != i) store->elements[kept].assign(store->elements[i]);
    kept++;
  }
  // Clear the vacated slots behind the new count — ArrayEraseAt's hygiene,
  // gArrayFill's apply.
  for (std::size_t i = kept; i < old; i++)
    store->elements[i].clear();
  store->count = kept;
}

// ─── .array.reduce ────────────────────────────────────────────────────────────

#undef className
#define className gArrayReduce

gArrayReduce::gArrayReduce() : gArrayExprBase(3) {
  // ANY on the answer: the result carries the expression's C type — or a
  // single element's own spelling. The empty outlet is always a bang.
  ADD_OUT_ANY;
  ADD_OUT_BANG;

  ADD_DESCRIPTION(
      "Combines the numbers in a named array into one value with an expression, such as "
      "$f1 + $f2 for a sum. $f1 is the result so far and $f2 the next element.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "result",
             "The final value, an int or a float. With a single numeric element, that element "
             "as it is.",
             "");
  OUTLET_DOC(1, "empty", "Bang when the array holds no numbers.", "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("expression", "", kFoldParamDoc, "any expression over $1, $2 and $3");
}

void gArrayReduce::Trigger(YSE::THREAD thread) {
  bool seeded = false;
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    // The whole fold under one hold — the family's mid-walk answer, and the
    // reason the reported answer is the array as it stood at the trigger.
    // Evaluate is a fixed-stack walk, safe under the guard.
    for (std::size_t i = 0; i < store->count; i++) {
      const std::string& element = store->elements[i];
      float value = 0.f;
      if (!ReadNumericToken(element.data(), element.size(), value)) continue;
      if (!seeded) {
        // The seed: the first numeric element, typed by its spelling so a
        // one-element fold answers the element as it is — JavaScript's
        // no-initial-value rule, the only seed that leaves min, max and a
        // product meaning what they say.
        seeded = true;
        result = TokenLooksLikeFloat(element.data(), element.size())
                     ? ExprValue::Float(value)
                     : ExprValue::Int(ExprToInt(value));
        continue;
      }
      result = EvalFold(result.AsFloat(), value, i);
    }
  }

  // Outside the guard on purpose: a send runs the whole downstream graph,
  // which may well write into this same array.
  if (!seeded) {
    outputs[1].SendBang(thread);
    return;
  }
  if (result.isInt) {
    outputs[0].SendInt(result.i, thread);
    return;
  }
  outputs[0].SendFloat(result.f, thread);
}

// ─── the quantifier pair's shared body ────────────────────────────────────────

#undef className
#define className gArrayQuantifierBase

gArrayQuantifierBase::gArrayQuantifierBase(bool wantAll) : gArrayExprBase(2), wantAll(wantAll) {
  // A verdict is a truth, not a piece of the data — one int outlet, .=='s
  // output. No empty outlet: a quantifier always has an answer, vacuously on
  // an empty population (see the header).
  ADD_OUT_INT;
}

void gArrayQuantifierBase::Trigger(YSE::THREAD thread) {
  // The quantifier's own identity: every over nothing is 1 — a claim about
  // nothing is vacuously true — and some over nothing is 0. The scan only
  // ever moves the verdict away from it, once, which is the short-circuit.
  bool verdict = wantAll;
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      return;
    }
    for (std::size_t i = 0; i < store->count; i++) {
      const std::string& element = store->elements[i];
      float value = 0.f;
      // The population rule — decision 3 on the base: a symbol is not part
      // of what the quantifier quantifies over.
      if (!ReadNumericToken(element.data(), element.size(), value)) continue;
      const bool accepted = Accepts(Eval(value, i));
      if (wantAll == accepted) continue;
      verdict = !wantAll;
      break;
    }
  }

  // Outside the guard on purpose — a send runs the whole downstream graph.
  outputs[0].SendInt(verdict ? 1 : 0, thread);
}

// ─── .array.every ─────────────────────────────────────────────────────────────

#undef className
#define className gArrayEvery

gArrayEvery::gArrayEvery() : gArrayQuantifierBase(true) {
  ADD_DESCRIPTION("Tests every number in a named array with an expression. A bang sends 1 when the "
                  "expression is true (non-zero) for all of them, and 0 when it is false for any.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "verdict",
             "1 when the expression is true for every numeric element, otherwise 0. An array "
             "without numbers gives 1.",
             "0 or 1");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("expression", "", kExprParamDoc, "any expression over $1 and $2");
}

// ─── .array.some ──────────────────────────────────────────────────────────────

#undef className
#define className gArraySome

gArraySome::gArraySome() : gArrayQuantifierBase(false) {
  ADD_DESCRIPTION(
      "Tests the numbers in a named array with an expression. A bang sends 1 when the "
      "expression is true (non-zero) for at least one of them, and 0 when it is true for none.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "verdict",
             "1 when the expression is true for at least one numeric element, otherwise 0. An "
             "array without numbers gives 0.",
             "0 or 1");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("expression", "", kExprParamDoc, "any expression over $1 and $2");
}

// ─── .array.foreach ───────────────────────────────────────────────────────────

#undef className
#define className gArrayForeach

gArrayForeach::gArrayForeach() : gArrayExprBase(2) {
  // The result outlet, then the done outlet — gArrayIter's shape. ANY on the
  // results: the expression's ints and floats, a passed-through symbol. The
  // done bang is sent last — the carry exception to right-to-left.
  ADD_OUT_ANY;
  ADD_OUT_BANG;

  // The allocation SendAtom's symbol path would otherwise need, taken here
  // on the control thread.
  AtomList::ReserveRender(emitScratch);

  ADD_DESCRIPTION(
      "Evaluates an expression for each element of a named array and sends the results one at "
      "a time, first to last, then bangs its done outlet.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger", kTriggerInletDoc, "");
  INLET_DOC(1, "array reference", kReferenceInletDoc, "");
  OUTLET_DOC(0, "result",
             "One result per element, first to last, as an int or a float. Words pass through "
             "unchanged.",
             "");
  OUTLET_DOC(1, "done", "Bang after the last result, also when the array is empty.", "");
  PARAM_DOC("name", "", kNameParamDoc, "any identifier");
  PARAM_DOC("expression", "", kExprParamDoc, "any expression over $1 and $2");
}

void gArrayForeach::Trigger(YSE::THREAD thread) {
  // The re-entrancy guard, held across the whole walk — gArrayIter's, for
  // gArrayIter's reason: this object emits in a loop, so a cord from either
  // outlet back to the inlet re-enters from inside the walk, and letting it
  // through would rewrite the snapshot being walked. The loser is dropped
  // and counted rather than made to spin.
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    Refuse();
    return;
  }

  // The snapshot: the array as it stands right now, copied out under its
  // guard into rows reserved at construction — bounded assigns, no
  // allocation. This is the whole of the time the guard is held, so the
  // results' sends run with no guard at all and a downstream write into
  // this same array is never the one thing the try-lock drops.
  {
    const arrayStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      busy.store(false, std::memory_order_release);
      return;
    }
    snapshot.count = store->count;
    for (std::size_t i = 0; i < store->count; i++)
      snapshot.elements[i].assign(store->elements[i]);
  }

  // One send per element, first to last, each completing in full before the
  // next — the serialiser's property, .iter's. A numeric element leaves as
  // the expression's result; a symbol passes through unchanged, decision 3
  // on the base, through SendAtom so it stays the symbol it is.
  for (std::size_t i = 0; i < snapshot.count; i++) {
    const std::string& element = snapshot.elements[i];
    float value = 0.f;
    if (ReadNumericToken(element.data(), element.size(), value)) {
      const ExprValue result = Eval(value, i);
      if (result.isInt) {
        outputs[0].SendInt(result.i, thread);
      } else {
        outputs[0].SendFloat(result.f, thread);
      }
      continue;
    }
    SendAtom(outputs[0], element.data(), element.size(), emitScratch, thread);
  }

  // The done bang, last — the carry exception to right-to-left — and even
  // for an empty array, .uzi's rule for a count of zero. A refused walk
  // never reaches here, so a done bang always means a completed walk.
  outputs[1].SendBang(thread);

  busy.store(false, std::memory_order_release);
}
