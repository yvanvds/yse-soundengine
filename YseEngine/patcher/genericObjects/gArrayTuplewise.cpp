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
      "Combines two named arrays position by position with an expression, such as $f1 + $f2, "
      "and outputs the results as one list. The result is as long as the shorter array.");
  ADD_CATEGORY(pCategory::ARRAY);
  INLET_DOC(0, "trigger",
            "Bang, or the left array's reference \"array <name>\": combine the two arrays. "
            "Refused when the expression is missing or did not compile.",
            "");
  INLET_DOC(1, "right reference",
            "Accepts the right array's reference \"array <name>\" and ignores it, so its "
            "reference outlet can be wired here. Anything else is refused.",
            "");
  OUTLET_DOC(0, "combined",
             "One result per position, as a list. Where either element is a word, the left "
             "element is passed through. A single result leaves as an int, float or symbol.",
             "");
  OUTLET_DOC(1, "empty", "Bang when either array is empty.", "");
  PARAM_DOC("left", "",
            "Name of the left array: the .array with this name in the same patcher. Empty uses "
            "a private, empty array.",
            "any identifier");
  PARAM_DOC("right", "",
            "Name of the right array, found the same way. Empty uses a private, empty array.",
            "any identifier");
  PARAM_DOC("expression", "",
            "The expression: the rest of the arguments, in .expr syntax. $f1 or $i1 is the left "
            "element, $f2 or $i2 the right element and $f3 or $i3 their position. A malformed "
            "expression is logged and the object then does nothing.",
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
