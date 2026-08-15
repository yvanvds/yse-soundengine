#include "gArrayTuplewise.h"
#include "../../implementations/logImplementation.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"

#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

// How many `$` slots the expression may reference: the left element, the
// right element and their position. A placeholder past $3 names an operand
// that can never exist — gArrayExprBase's varLimit, for this object's three.
static constexpr int kTuplewiseVarLimit = 3;

gArrayTuplewise::gArrayTuplewise() : gArraySetOpBase(false) {
  // After the base's two names — the trailing LIST param absorbs every
  // remaining creation argument, so a whitespace-bearing expression survives
  // Parameters::Set intact. gArrayExprBase's arrangement, behind two names
  // instead of one.
  ADD_PARAM(expression);

  ADD_DESCRIPTION(
      "Combines two arrays element by element — issue #808's reading of Max's array.tuplewise "
      "on the name-addressed value model .array settled: Max's own array.tuplewise collects a "
      "stream into arrays of a set length, where #808 specifies the element-wise combiner "
      "instead — the vector arithmetic .vexpr does for lists, applied to stored arrays. Both "
      "arrays are bound from the creation arguments, \".array.tuplewise <left> <right> "
      "<expression>\", because an array is addressed by name and never passed down a cord, and "
      "neither may be re-pointed from a message. The operation is the expression family's "
      "spelling, never a second one: the remaining creation arguments, compiled once on the "
      "control thread by .expr's own compiler — $f1/$i1 the left element, $f2/$i2 the right "
      "element, $f3/$i3 their position — so \"$f1 + $f2\" adds two arrays position by position. "
      "The result is as long as the shorter array (.vexpr's rule for unequal lists), each "
      "wholly numeric pair's result typed by the expression's C type and the left element "
      "passed through unchanged for a pair either side of which is a symbol — the family's "
      "positional pass-through. The result leaves as the list it spells, never as a new named "
      "array, and an empty result bangs the empty outlet. The left array is copied out under "
      "its guard and the result built against the right under that guard alone, so no two "
      "guards are ever held at once and \".array.tuplewise seq seq $f1 + $f2\" doubles the "
      "array instead of tripping over its own try-lock. A malformed expression fails loudly "
      "when the parameter is set and the object then refuses every trigger, counted.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "trigger",
            "A bang asks for the combination — the expression compiled from the creation "
            "arguments, applied to the two bound arrays position by position as they stood at "
            "the trigger, the left copied out under its guard, the result built against the "
            "right under that guard alone, and sent after every guard is released. \"array "
            "<name>\" does the same when it names the left array bound by the first creation "
            "argument — the message an .array's reference outlet emits on a bang, the family's "
            "gesture. A reference naming anything else, any other message, or a trigger on an "
            "object whose expression did not compile, is refused and counted rather than "
            "logged, since this inlet may be the audio thread.",
            "");
  INLET_DOC(1, "right reference",
            "\"array <name>\" is accepted silently when it names the right array bound by the "
            "second creation argument, so a patch may wire both reference outlets across as it "
            "would in Max. It sets nothing — the binding is the creation argument, resolved on "
            "the control thread — and anything else is refused and counted.",
            "");
  OUTLET_DOC(0, "combined",
             "The element-by-element combination as the list it spells — one element as the "
             "int, float or symbol it is, several as list text, never as a new named array. "
             "Position i is the expression over (left[i], right[i], i), typed by the "
             "expression's C type, and the result is as long as the shorter array; a pair "
             "either side of which is a symbol passes the left element through unchanged, so "
             "every position still answers a position of the left array. A result that outruns "
             "what a cord carries is refused whole and counted — a partial combination would "
             "misalign every position after the cut. Silent when the result is empty: the "
             "empty outlet carries that half of the answer.",
             "");
  OUTLET_DOC(1, "empty",
             "Bang when there was nothing to combine — either array empty or unnamed "
             "(private), so no position holds a pair. \"No data\" is a state a patch must be "
             "able to route on, not an error. A lost try-lock on either store, or a trigger on "
             "an object whose expression did not compile, is a counted refusal instead: the "
             "result is unknown, so neither outlet fires.",
             "");
  PARAM_DOC("left", "",
            "The left array's shared name, addressed as \"<patcherName>.<name>\" — the "
            "sequence an .array of the same name in this patcher holds. Resolved once, on the "
            "control thread, which is why no message re-points it at run time. Empty reads a "
            "private, empty array on that side: every ask bangs the empty outlet.",
            "any identifier");
  PARAM_DOC("right", "",
            "The right array's shared name, bound exactly as the left one. Empty reads a "
            "private, empty array on that side: every ask bangs the empty outlet.",
            "any identifier");
  PARAM_DOC("expression", "",
            "The combining expression — every creation argument after the two names, compiled "
            "once on the control thread by .expr's own compiler, so read .expr for the "
            "operators, the functions and the C type rules. $f1/$i1 is the left element ($f "
            "reads it as a float, $i as an int), $f2/$i2 the right element and $f3/$i3 their "
            "position; a placeholder past $3 is rejected at parse time. The expression only "
            "ever runs for a pair whose two elements both read as numbers — a symbol has no "
            "number — and the left element passes through unchanged for the pairs it never "
            "sees. A malformed expression is reported to the log when the parameter is set, "
            "and the object then refuses every trigger, counted; absent, the object refuses "
            "silently until given work.",
            "any expression over $1..$3");
}

// A re-parse must not leave half of the previous configuration standing:
// SetParams("") — and the clear half of every Set() — drops the program along
// with both names, so a trigger after a reset refuses rather than running
// whatever the previous arguments compiled. gArrayExprBase's rule, on the
// two-array body.
void gArrayTuplewise::ClearParams() {
  expression.clear();
  program.Clear();
  compileError.clear();
  gArraySetOpBase::ClearParams();
}

// The base re-anchors the right binding; the override adds the one compile —
// both on the control thread, at construction or on SetParams (a live
// SetParams takes the structural-replacement route, issue #234, because the
// registered LIST param makes ParamsNeedRebuild() true).
void gArrayTuplewise::ParamsChanged() {
  gArraySetOpBase::ParamsChanged();
  CompileExpression();
}

void gArrayTuplewise::CompileExpression() {
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

  // No expression at all — a bare or names-only object, or the clear half of
  // a re-parse. Nothing malformed to report: the object simply refuses
  // triggers until it is given work.
  if (source.empty()) {
    program.Clear();
    return;
  }

  // The one compile. A failure is loud — .expr's rule: a malformed
  // expression must fail here, at parse time, rather than silently at
  // trigger time.
  if (!program.Compile(source)) {
    compileError = program.Error();
    INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + Type() + " cannot compile \"" +
                                          source + "\": " + compileError);
    return;
  }

  // A placeholder past the left element, the right element and the position
  // names an operand that can never exist. Rejected here, loudly, rather
  // than silently evaluating the 0 an unset var holds.
  if (program.InletCount() > kTuplewiseVarLimit) {
    compileError =
        "the expression may only reference $1..$" + std::to_string(kTuplewiseVarLimit) + " here";
    INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + Type() + " cannot use \"" +
                                          source + "\": " + compileError);
    program.Clear();
    return;
  }
}

bool gArrayTuplewise::CollectLocked() {
  // No program, no combination: a malformed or absent expression was
  // reported when the parameter was set (see CompileExpression), and a
  // trigger on the misconfigured object is a counted refusal — the base
  // refuses whole on false — never a silent 0 emitted as data, #799's rule.
  if (!program.Valid()) return false;

  // The pairs, up to the shorter side — .vexpr's rule: padding would invent
  // operands. The expression runs for a pair when both elements read as
  // numbers (the family's population rule); the left element stands in,
  // unchanged, for a pair the expression cannot see, so every position of
  // the result answers a position of the left array. Evaluate is a
  // fixed-stack walk, which is why running it under the right store's guard
  // is legitimate where a send never is.
  const std::size_t count = snapshot.count < rightStore->count ? snapshot.count : rightStore->count;
  for (std::size_t i = 0; i < count; i++) {
    const std::string& left = snapshot.elements[i];
    const std::string& right = rightStore->elements[i];
    float leftValue = 0.f;
    float rightValue = 0.f;
    if (ReadNumericToken(left, leftValue) && ReadNumericToken(right, rightValue)) {
      const ExprValue value = Eval(leftValue, rightValue, i);
      const bool added = value.isInt ? result.AddInt(value.i) : result.AddFloat(value.f);
      if (!added) return false;
    } else if (!result.Add(left.data(), left.size())) {
      return false;
    }
  }
  return true;
}
