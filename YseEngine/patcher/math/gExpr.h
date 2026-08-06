#pragma once
#include "../pObject.h"
#include "gExprEval.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Control-rate expression evaluation ``.expr`` (issue #449).
     *
     *  Max's ``expr``: one object in place of an arbitrarily large tangle of
     *  arithmetic boxes. The creation argument is a C-like expression whose
     *  ``$i1``-``$i9`` / ``$f1``-``$f9`` placeholders name the inlets, and the
     *  number of inlets is one past the highest placeholder the expression
     *  mentions (one when it mentions none, so the object can still be banged).
     *  Inlet 0 is the hot one, exactly as everywhere else in the family: the
     *  cold inlets store, inlet 0 stores *and* evaluates.
     *
     *  The whole point of the object is that the expensive half happens once.
     *  ExprProgram::Compile() runs in ParseParams — the control thread, at
     *  construction or on ``SetParams`` — and lowers the text into a flat
     *  postfix program. Calculate() only walks that program over a fixed-size
     *  stack: no parsing, no allocation, no lock, no exception, no I/O. A
     *  ``SetParams`` on a live object cannot mutate it in place either; the
     *  registered clear/parse callbacks make ParamsNeedRebuild() true, so the
     *  patcher takes the structural-replacement route from issue #234 and the
     *  audio thread never sees a half-compiled program.
     *
     *  A malformed expression fails at parse time, loudly: the error goes to
     *  the log at E_ERROR, the program is left empty and evaluation yields 0.
     *  Nothing is deferred to render time. CompileError() exposes the same
     *  message to callers and tests.
     *
     *  The single outlet is typed ANY because the result's C type is the
     *  expression's: ``$i1 + 1`` sends an int, ``$f1 + 1`` sends a float. See
     *  gExprEval.h for the full language, the type rules and the list of
     *  Max features (table access, ``random``/``noise``) that are rejected at
     *  compile time rather than silently accepted.
     */
    PATCHER_CLASS(gExpr, YSE::OBJ::G_EXPR)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)
    _BANG_IN(SetBang)
    _LIST_IN(SetList)

    _PARM_CLEAR
    _PARM_PARSE

  public:
    /// Empty when the current expression compiled; otherwise the parse error
    /// that was logged. Control thread only.
    const std::string& CompileError() const {
      return program.Error();
    }
    /// The compiled program, for tests and for anything that wants to know
    /// which inlets the expression actually reads.
    const ExprProgram& Program() const {
      return program;
    }

  private:
    // Shared by the float and int inlet handlers: every inlet stores into the
    // same float slot, and $iN decides at evaluation time whether to truncate.
    void Store(float value, int inlet);

    // The raw creation argument, one token per space. Registered as a LIST
    // param so a whitespace-bearing expression survives Parameters::Set
    // intact; ParseParams glues it back together.
    std::vector<std::string> expression;

    ExprProgram program;

    // Per-inlet stored values. Written by the inlet handlers, read by
    // Calculate; sized for the nine inlets Max allows so a program compiled
    // for fewer can never index past the end.
    float vars[kExprMaxVars] = {};
  };
}
}
