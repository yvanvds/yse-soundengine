#include "gExpr.h"
#include "../pObjectList.hpp"
#include "../../implementations/logImplementation.h"

using namespace YSE::PATCHER;

#define className gExpr

namespace {

  // Inlet labels follow the placeholders rather than the ports: an expression
  // author counts $1..$9, not 0..8.
  const char* const kInletLabels[] = {"$1", "$2", "$3", "$4", "$5", "$6", "$7", "$8", "$9"};

} // namespace

CONSTRUCT() {
  // Room for all nine inlets up front, so ParseParams' emplace_back can never
  // reallocate. The patcher itself always re-parses on an unpublished object
  // (see patcherImplementation::ReplaceObjectUnlocked), but a standalone
  // object — a unit test, an embedder driving pObject directly — can be wired
  // first and re-parsed after, and a reallocation would leave the peers
  // holding inlet pointers into freed storage.
  inputs.reserve(kExprMaxVars);

  // The left inlet always exists — Max's expr can be banged even when the
  // expression names no placeholder at all. Inlets 1..8 are created in
  // ParseParams, once the expression says how many of them the patch needs.
  ADD_IN_0;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  REG_BANG_IN(SetBang);
  REG_LIST_IN(SetList);

  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // ANY rather than FLOAT: the result carries the expression's C type, so
  // `$i1 + 1` leaves as an int and `$f1 + 1` as a float, exactly as in Max.
  ADD_OUT_ANY;

  ADD_PARAM(expression);

  ADD_DESCRIPTION(
      "Evaluates a C-like mathematical expression, replacing a tangle of arithmetic objects with "
      "one line of text. The expression is the creation argument; its $i1-$i9 and $f1-$f9 "
      "placeholders name the inlets ($i reads the inlet as an int, $f as a float), and the object "
      "gets one inlet past the highest placeholder used. Inlet 0 is hot: it stores its value and "
      "evaluates, while the other inlets only store. A list on inlet 0 fills the inlets left to "
      "right and evaluates; a bang re-evaluates with the stored values. Operators are C's, with "
      "C's precedence and C's types: + - * / % on two ints give an int (so $i1/$i2 is integer "
      "division), the comparisons (< <= > >= == !=), the logic (&& || !) and the bitwise "
      "operators (& | ^ << >> ~) always give an int, and ^ is exclusive-or rather than a power - "
      "use pow(). Functions: min, max, abs, int, float, pow, sqrt, exp, ln, log, log10, sin, cos, "
      "tan, asin, acos, atan, atan2, sinh, cosh, tanh, fact, round, floor, ceil. Division by zero "
      "gives 0 and a non-finite result gives 0, the convention the rest of the math family uses. "
      "The expression is compiled once, when the parameter is set; evaluation only walks the "
      "compiled form, so it allocates nothing and takes no lock. A malformed expression is "
      "reported to the log at parse time and the object then evaluates to 0. Max's table access "
      "($s placeholders, size/sum/avg/store) and its random/noise functions are not supported and "
      "are rejected when the expression is compiled.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, kInletLabels[0],
            "Value for the $i1 / $f1 placeholders — stores and fires the evaluation. Also accepts "
            "a bang (re-evaluate with the stored values) and a list (fill the inlets left to "
            "right, then evaluate).",
            "any float");
  OUTLET_DOC(0, "out",
             "Result of the expression. An int when the expression's type is int, otherwise a "
             "float.",
             "any float");
  PARAM_DOC("expression", "",
            "The expression to evaluate, e.g. \"($f1 * 0.5) + pow($f2, 2)\". Placeholders $i1-$i9 "
            "and $f1-$f9 name the inlets and determine how many there are.",
            "any expression");
}

PARM_CLEAR() {
  // Back to the bare object the constructor built: one inlet, no program. The
  // stored inlet values go too — a value received for an inlet that the new
  // expression re-uses for something else would otherwise leak across, and Max
  // treats a placeholder that has never received a value as 0.
  while (inputs.size() > 1) {
    inputs.pop_back();
  }
  for (int i = 0; i < kExprMaxVars; i++) {
    vars[i] = 0.f;
  }
  expression.clear();
  program.Clear();
}

PARM_PARSE() {
  // Parameters::Set split the argument string on spaces; glue it back together
  // before compiling. Whitespace is not significant inside an expression, so
  // the normalised spacing is harmless — and GetParams() still returns the
  // string the caller passed, so a DumpJSON round trip is exact.
  std::string source;
  for (std::size_t i = 0; i < expression.size(); i++) {
    if (i != 0) source += ' ';
    source += expression[i];
  }

  // The one compile. Control thread, at construction or on SetParams; a live
  // SetParams cannot reach here in place because the clear/parse callbacks
  // above make ParamsNeedRebuild() true (issue #234).
  if (!program.Compile(source)) {
    INTERNAL::LogImpl().emit(E_ERROR, "patcher: .expr cannot compile \"" + source +
                                          "\": " + program.Error());
    return; // one inlet, empty program, evaluates to 0
  }

  const int wanted = program.InletCount();
  while ((int)inputs.size() < wanted) {
    const std::size_t index = inputs.size();
    inputs.emplace_back(this, false, (int)index);
    REG_FLOAT_IN(SetFloat);
    REG_INT_IN(SetInt);
    inputs.back().SetDoc(kInletLabels[index],
                         program.UsesInlet((int)index)
                             ? "Value for the $i / $f placeholders with this index — stored until "
                               "the next evaluation."
                             : "Unused by the current expression; it exists so the inlets to its "
                               "right keep their numbering.",
                         "any float");
  }
}

void gExpr::Store(float value, int inlet) {
  if (inlet >= 0 && inlet < kExprMaxVars) {
    vars[inlet] = value;
  }
}

FLOAT_IN(SetFloat) {
  Store(value, inlet);
}

INT_IN(SetInt) {
  Store((float)value, inlet);
}

BANG_IN(SetBang) {
  // Nothing to store; inlet 0 is active, so the evaluation follows.
}

LIST_IN(SetList) {
  // Max: "the items of a list received in the left inlet are treated as if
  // each had come in a different inlet". Items past the ninth are dropped, and
  // an inlet the list does not reach keeps the value it already had.
  //
  // Parsed with strtof over the characters rather than by splitting into
  // strings: a list can arrive on the audio thread, where an allocation is not
  // allowed.
  float values[kExprMaxVars];
  const int count = ExprParseFloatList(value.c_str(), values, kExprMaxVars);
  for (int i = 0; i < count; i++) {
    vars[i] = values[i];
  }
}

CALC() {
  // The entire RT budget: a walk of the pre-compiled program over a stack in
  // this frame, then one Send. No parsing, no allocation, no lock, no I/O.
  const ExprValue result = program.Evaluate(vars);
  if (result.isInt) {
    outputs[0].SendInt(result.AsInt(), thread);
  } else {
    outputs[0].SendFloat(result.AsFloat(), thread);
  }
}
