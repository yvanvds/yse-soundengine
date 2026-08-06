#include "gVexpr.h"
#include "../pObjectList.hpp"
#include "../../implementations/logImplementation.h"

using namespace YSE::PATCHER;

#define className gVexpr

namespace {

  // Inlet labels follow the placeholders rather than the ports: an expression
  // author counts $1..$9, not 0..8.
  const char* const kInletLabels[] = {"$1", "$2", "$3", "$4", "$5", "$6", "$7", "$8", "$9"};

} // namespace

CONSTRUCT() {
  // Room for all nine inlets up front, so ParseParams' emplace_back can never
  // reallocate — the peers hold pointers into this vector. See gExpr's
  // constructor for the full argument.
  inputs.reserve(kExprMaxVars);

  // The left inlet always exists — an expression that names no placeholder at
  // all can still be banged. Inlets 1..8 are created in ParseParams, once the
  // expression says how many of them the patch needs.
  ADD_IN_0;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  REG_BANG_IN(SetBang);
  REG_LIST_IN(SetList);

  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // ANY rather than LIST: a one-element result leaves as an int or a float,
  // exactly as in Max, and only a longer one leaves as a list.
  ADD_OUT_ANY;

  ADD_PARAM(expression);

  // The one allocation the output path would otherwise need. Every element is
  // at most kExprValueTextMax - 1 characters and they are joined by single
  // spaces, so this is an upper bound on any list the object can send.
  result.reserve((std::size_t)kVexprMaxList * (std::size_t)kExprValueTextMax);

  ResizeStorage();

  ADD_DESCRIPTION(
      "Evaluates a C-like mathematical expression once for every element of a list, so one object "
      "replaces an iteration loop: scaling a chord, transposing a motif or bending a set of "
      "breakpoints becomes a single box. The expression is the creation argument and the language "
      "is exactly .expr's - its $i1-$i9 and $f1-$f9 placeholders name the inlets ($i reads as an "
      "int, $f as a float), the object gets one inlet past the highest placeholder used, and the "
      "operators, the functions and the C type rules are the same, so read .expr for the details. "
      "The difference is the data: every inlet holds a list rather than a single value, and an int "
      "or a float is simply a one-item list. An inlet holding one item is broadcast across all the "
      "elements (Max's scalarmode 1), so a list on one inlet and a number on another scales the "
      "list; otherwise the number of results is the length of the shortest list among the inlets "
      "the expression actually reads. Inlet 0 is hot: it stores its list and evaluates, while the "
      "other inlets only store. A bang on inlet 0 re-evaluates from the stored lists. A single "
      "result is sent as an int or a float, and a longer one as a list. Lists are held at up to "
      "256 items per inlet (Max's default maxsize) and anything past that is dropped. The "
      "expression is compiled once, when the parameter is set; evaluation only walks the compiled "
      "form and writes into a buffer reserved up front, so it allocates nothing and takes no lock. "
      "A malformed expression is reported to the log at parse time and the object then sends 0.");
  ADD_CATEGORY(pCategory::MATH);
  INLET_DOC(0, kInletLabels[0],
            "List for the $i1 / $f1 placeholders — stores and fires the evaluation. An int or a "
            "float is stored as a one-item list, which is then broadcast across every element. "
            "Also accepts a bang, which re-evaluates from the stored lists.",
            "any list of floats");
  OUTLET_DOC(0, "out",
             "One result per element. A single result is sent as an int or a float following the "
             "expression's type; two or more are sent as a list.",
             "any list of floats");
  PARAM_DOC("expression", "",
            "The expression to evaluate for every element, e.g. \"$f1 * $f2\". Placeholders "
            "$i1-$i9 and $f1-$f9 name the inlets and determine how many there are.",
            "any expression");
}

void gVexpr::ResizeStorage() {
  // Control thread only: this is the object's one allocation, and it happens
  // at construction or in ParseParams, never while a list is being mapped.
  lists.assign((std::size_t)inputs.size() * (std::size_t)kVexprMaxList, 0.f);
  for (int i = 0; i < kExprMaxVars; i++) {
    counts[i] = 1; // "never received" evaluates as a single 0, as in Max
  }
}

PARM_CLEAR() {
  // Back to the bare object the constructor built: one inlet, no program. The
  // stored lists go too — a list received for an inlet that the new expression
  // re-uses for something else would otherwise leak across.
  while (inputs.size() > 1) {
    inputs.pop_back();
  }
  expression.clear();
  program.Clear();
  ResizeStorage();
}

PARM_PARSE() {
  // Parameters::Set split the argument string on spaces; glue it back together
  // before compiling. Whitespace is not significant inside an expression, and
  // GetParams() still returns the string the caller passed, so a DumpJSON
  // round trip is exact.
  std::string source;
  for (std::size_t i = 0; i < expression.size(); i++) {
    if (i != 0) source += ' ';
    source += expression[i];
  }

  // The one compile. Control thread, at construction or on SetParams; a live
  // SetParams cannot reach here in place because the clear/parse callbacks
  // make ParamsNeedRebuild() true (issue #234).
  if (!program.Compile(source)) {
    INTERNAL::LogImpl().emit(E_ERROR, "patcher: .vexpr cannot compile \"" + source +
                                          "\": " + program.Error());
    return; // one inlet, empty program, sends 0
  }

  const int wanted = program.InletCount();
  while ((int)inputs.size() < wanted) {
    const std::size_t index = inputs.size();
    inputs.emplace_back(this, false, (int)index);
    REG_FLOAT_IN(SetFloat);
    REG_INT_IN(SetInt);
    REG_LIST_IN(SetList);
    inputs.back().SetDoc(kInletLabels[index],
                         program.UsesInlet((int)index)
                             ? "List for the $i / $f placeholders with this index — stored until "
                               "the next evaluation, and broadcast when it holds a single item."
                             : "Unused by the current expression; it exists so the inlets to its "
                               "right keep their numbering.",
                         "any list of floats");
  }

  // Sized for the inlets the expression grew, not for all nine: a one-inlet
  // expression has no use for eight idle kilobytes.
  ResizeStorage();
}

void gVexpr::Store(float value, int inlet) {
  if (inlet < 0 || inlet >= (int)inputs.size()) return;
  Slot(inlet)[0] = value;
  counts[inlet] = 1;
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
  if (inlet < 0 || inlet >= (int)inputs.size()) return;

  // Parsed with strtof over the characters rather than by splitting into
  // strings: a list can arrive on the audio thread, where an allocation is not
  // allowed. Items past kVexprMaxList are dropped, matching the way Max's
  // maxsize truncates.
  const int count = ExprParseFloatList(value.c_str(), Slot(inlet), kVexprMaxList);

  // A list with nothing numeric in it leaves the stored list alone rather than
  // emptying the inlet — there is no such thing as a zero-element inlet here,
  // and silently substituting 0 would be worse than ignoring the message.
  if (count > 0) counts[inlet] = count;
}

CALC() {
  const int inlets = (int)inputs.size();

  // How many times to evaluate: the shortest list among the inlets the
  // expression reads. An inlet holding a single item is a scalar and does not
  // take part — it is broadcast instead.
  int elements = 0;
  for (int i = 0; i < inlets; i++) {
    if (!program.UsesInlet(i)) continue;
    const int c = counts[i];
    if (c <= 1) continue;
    if (elements == 0 || c < elements) elements = c;
  }
  if (elements < 1) elements = 1; // all scalars, or an expression with no placeholder

  // The entire RT budget from here: one walk of the pre-compiled program per
  // element over a stack in this frame, the digits appended to a buffer that
  // was reserved at construction, then one Send. No parsing, no allocation, no
  // lock, no I/O.
  float vars[kExprMaxVars] = {};
  ExprValue last;
  result.clear();

  for (int e = 0; e < elements; e++) {
    for (int i = 0; i < inlets; i++) {
      const int c = counts[i];
      // Clamped rather than assumed: a *referenced* inlet always has e < c by
      // the loop bound above, but an unreferenced one may hold a shorter list.
      const int index = (c > 1) ? (e < c ? e : c - 1) : 0;
      vars[i] = Slot(i)[index];
    }
    last = program.Evaluate(vars);

    if (elements == 1) break; // sent as a bare int or float instead

    if (e != 0) result += ' ';
    char text[kExprValueTextMax];
    const int length = ExprFormatValue(last, text, kExprValueTextMax);
    result.append(text, (std::size_t)length);
  }

  if (elements == 1) {
    // "If the input in one of the inlets was a single number rather than a
    // list ... a single result is sent out as a float rather than a list."
    if (last.isInt) {
      outputs[0].SendInt(last.AsInt(), thread);
    } else {
      outputs[0].SendFloat(last.AsFloat(), thread);
    }
    return;
  }
  outputs[0].SendList(result, thread);
}
