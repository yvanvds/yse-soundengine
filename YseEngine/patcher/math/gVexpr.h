#pragma once
#include "../pObject.h"
#include "gExprEval.h"

namespace YSE {
  namespace PATCHER {

    /// Longest list ``.vexpr`` stores per inlet, and therefore the longest one
    /// it can send. Max's ``vexpr`` has a ``maxsize`` attribute that defaults
    /// to 256; the YSE patcher has no attribute mechanism, so the default is
    /// the whole story and the buffers are sized once, on the control thread,
    /// rather than grown while a list is being mapped.
    constexpr int kVexprMaxList = 256;

    /**
     *  @brief The list counterpart of ``.expr`` — ``.vexpr`` (issue #450).
     *
     *  Max's ``vexpr``: the same C-like expression language, but every inlet
     *  holds a *list* and the expression is evaluated once per element. One
     *  object replaces an iteration loop, so scaling a chord, transposing a
     *  motif or bending a set of breakpoints is a single box.
     *
     *  Everything about the language, the type rules and the answers given for
     *  the cases C leaves undefined is ``.expr``'s: both objects share
     *  ExprProgram in gExprEval.h, and the split of work is the same. The
     *  expression is compiled once, in ParseParams, on the control thread;
     *  Calculate() only walks the compiled program, once per element, over a
     *  stack in its own frame. The output list is built into a buffer reserved
     *  at construction, so the hot path allocates nothing, takes no lock,
     *  throws nothing and touches no I/O.
     *
     *  ### How the lists line up
     *
     *  - An ``int`` or a ``float`` on an inlet is a one-item list, as in Max.
     *  - An inlet holding one item **broadcasts**: that value is used for
     *    every element. This is Max's ``scalarmode 1``, and it is what the
     *    issue asks for — it is also the only reading that makes ``$f1 * $f2``
     *    with a list on the left and a number on the right mean "scale the
     *    list". Max defaults the attribute off; the YSE patcher has no
     *    attributes, so ``.vexpr`` simply always broadcasts.
     *  - Otherwise the element count is the length of the *shortest* list, as
     *    in Max — but only over the inlets the expression actually reads, so a
     *    stale value sitting on a gap inlet cannot truncate the result.
     *  - One element out is sent as an int or a float rather than as a
     *    one-item list, which is what Max does and what the downstream
     *    arithmetic objects expect.
     *
     *  Inlet 0 is hot, exactly as everywhere else in the family: the cold
     *  inlets store their list, inlet 0 stores *and* evaluates. A bang on
     *  inlet 0 re-evaluates from the stored lists. Note the one real
     *  difference from ``.expr``, which is the whole point of the object: a
     *  list on ``.expr``'s inlet 0 fills all of its inlets, while a list on
     *  ``.vexpr``'s inlet 0 is inlet 0's data.
     *
     *  A malformed expression fails at parse time and loudly, again as in
     *  ``.expr``: the message goes to the log at E_ERROR, the program is left
     *  empty, and the object then sends 0.
     */
    PATCHER_CLASS(gVexpr, YSE::OBJ::G_VEXPR)
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
    /// How many items the inlet is currently holding, for tests. Always at
    /// least 1: an inlet that has received nothing holds a single 0.
    int StoredCount(int inlet) const {
      if (inlet < 0 || inlet >= (int)inputs.size()) return 0;
      return counts[inlet];
    }

  private:
    // Replace inlet `inlet`'s list with the single value `value`. Shared by
    // the float and int handlers: every inlet stores floats, and $iN decides
    // at evaluation time whether to truncate.
    void Store(float value, int inlet);

    // Size the per-inlet buffers to the current inlet count and reset them to
    // a single 0. Control thread only — it allocates.
    void ResizeStorage();

    // Start of inlet `inlet`'s slice of `lists`.
    float* Slot(int inlet) {
      return lists.data() + (std::size_t)inlet * kVexprMaxList;
    }
    const float* Slot(int inlet) const {
      return lists.data() + (std::size_t)inlet * kVexprMaxList;
    }

    // The raw creation argument, one token per space. Registered as a LIST
    // param so a whitespace-bearing expression survives Parameters::Set
    // intact; ParseParams glues it back together.
    std::vector<std::string> expression;

    ExprProgram program;

    // Per-inlet lists, laid out as inputs.size() slices of kVexprMaxList
    // floats. Allocated in ResizeStorage on the control thread and only ever
    // read and written — never resized — from Calculate.
    std::vector<float> lists;

    // How many items of each slice are live. Never 0: an inlet that has
    // received nothing holds a single 0, which is what Max evaluates too.
    int counts[kExprMaxVars] = {};

    // The outgoing list, reserved at construction for the longest one that can
    // be built, so appending to it on the hot path cannot reallocate.
    std::string result;
  };
} // namespace PATCHER
} // namespace YSE
