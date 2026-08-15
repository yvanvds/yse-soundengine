#pragma once
#include "../math/gExprEval.h"
#include "../pObject.h"
#include "gArray.h"
#include "gArrayEnds.h"
#include "gArraySetOps.h"
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Combine two arrays element by element — issue #808's reading of
     *         Max's ``array.tuplewise`` on the name-addressed value model
     *         ``.array`` settled (#548).
     *
     *  The vector arithmetic: "add these two arrays together, position by
     *  position" — what a patch does with ``.vexpr`` on lists, applied to
     *  stored arrays. Max's own ``array.tuplewise`` is a stream-side
     *  collector — it gathers incoming elements into arrays of a set length —
     *  where #808 specifies the element-wise combiner instead, so that is
     *  what this object is: position ``i`` of the result is the expression
     *  over ``(left[i], right[i], i)``.
     *
     *  ### Two names bound at creation — ``gArraySetOpBase``, inherited whole
     *
     *  An array is addressed by name and never passed down a cord (see
     *  gArray.h for the whole argument), so ``.array.tuplewise <left>
     *  <right> <expression>`` binds **both** names on the control thread,
     *  on ``gArraySetOpBase``'s body: resolved in ``SetParent`` /
     *  ``PARM_PARSE`` / ``RefreshBinding``, re-anchored by
     *  ``patcherImplementation::SetName`` on a rename, neither re-pointable
     *  from a message, an unbound side reading a private, empty array of its
     *  own. The trigger inlet honours only the left array's reference, the
     *  right inlet only acknowledges the right one, and the same-name case —
     *  ``.array.tuplewise seq seq $f1 + $f2`` doubling an array — works
     *  because no two guards are ever held at once: the left array is copied
     *  out under its guard into the base's pre-allocated snapshot, released,
     *  and the result is built against the right store under *that* guard
     *  alone. Either guard lost is the operation dropped whole and counted,
     *  the store's rule.
     *
     *  ### The operation is the expression family's spelling — #808's blocker,
     *  resolved
     *
     *  #808 said the operation itself is the question the expression family
     *  answers, and #799 answered it: the per-element computation is **text**,
     *  compiled once on the control thread by ``ExprProgram``
     *  (``math/gExprEval.h``), the compiler behind ``.expr`` and ``.vexpr`` —
     *  never a second spelling for the same thing. The expression is every
     *  creation argument after the two names; ``$f1`` / ``$i1`` binds the
     *  **left** element, ``$f2`` / ``$i2`` the **right** element and
     *  ``$f3`` / ``$i3`` their position — the operand order the object's own
     *  name spells — so ``$f1 + $f2`` is the vector sum and
     *  ``$f1 * pow(2, $f2 / 12.)`` a frequency array scaled by an interval
     *  array. A placeholder past ``$3`` is rejected at parse time, loudly; a
     *  malformed expression fails at parse time, loudly, and the object then
     *  **refuses every trigger**, counted (#799's rule — never a silent 0
     *  emitted as data); an *absent* expression refuses silently until the
     *  object is given work.
     *
     *  ### The pairing, written down
     *
     *  - **The result is as long as the shorter array** — pairing stops where
     *    either side runs out, ``.vexpr``'s rule for lists of unequal length.
     *    Padding would invent operands, and a miss is a miss everywhere else
     *    in the family.
     *  - **The expression runs for a pair when both elements read as
     *    numbers** — ``ReadNumericToken``'s strict yes/no, the family's
     *    population rule (#799, decision 3). A pair the expression cannot see
     *    — either side a symbol — **passes the left element through
     *    unchanged**: the operation is positional, so the left element stands
     *    in for the missing result exactly as ``.array.expr``'s pass-through
     *    does, every position of the result still answering a position of the
     *    left array. Coercing the symbol to 0 would be silent corruption;
     *    skipping the pair would misalign every position after it.
     *
     *  ### The result leaves as list text, never as a new named array
     *
     *  The value model's form of "a new array" is the list it spells
     *  (``SendAtoms`` — one element as the atom it is, several as list text),
     *  lossless into another ``.array`` by the store's one-atom-per-element
     *  rule; creating a named array from a message would mean resolving a
     *  name on a message path. A numeric pair's result leaves typed by the
     *  expression's C type (``$i1 + $i2`` emits ints, ``$f1 * 0.5`` floats).
     *  An empty result — either array empty or unnamed, so there are no pairs
     *  — bangs the **empty outlet**: "no data" is a state a patch must be
     *  able to route on, not an error. A result that outruns what a cord
     *  carries is refused whole and counted — ``.array.at``'s whole-reply
     *  rule, the base's own: a partial combination would misalign every
     *  position after the cut.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — every object here is driven by its
     *  inlet, the family's rule. No message path allocates, locks or blocks:
     *  both names are resolved and the expression compiled on the control
     *  thread, the snapshot and the result are fixed members the base
     *  reserved at construction, and ``ExprProgram::Evaluate`` is a walk of
     *  the compiled program over a fixed-size stack in the caller's frame —
     *  no allocation, no lock, no exception, no recursion — which is why
     *  running it under the right store's guard is legitimate where an outlet
     *  send never is. Refusals are counted, never logged.
     */
    class gArrayTuplewise : public gArraySetOpBase {
    public:
      gArrayTuplewise();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_TUPLEWISE;
      }
      CREATE(gArrayTuplewise)

      /// Empty when the current expression compiled and fits the three
      /// placeholders; otherwise what was logged. Control thread only.
      const std::string& CompileError() const {
        return compileError;
      }

      /// True when a trigger has a program to run — a successful compile
      /// whose placeholders all exist here. False refuses every trigger.
      bool ProgramReady() const {
        return program.Valid();
      }

    protected:
      // Extends the base's hook so a re-parse drops the program along with
      // both names: SetParams("") must not keep combining with whatever the
      // previous arguments compiled. gArrayExprBase's rule.
      void ClearParams() override;

      // The base re-anchors the right binding here; the override adds the
      // one compile — gArrayExprBase's arrangement on the two-array body.
      void ParamsChanged() override;

      // The combination itself, on the base's hook: walk the pairs up to the
      // shorter side, the expression over the wholly numeric ones, the left
      // element through for the rest. The caller holds the right store's
      // guard — and only that one; Evaluate is a fixed-stack walk, which is
      // why running it under the guard is legitimate. False when there is no
      // program to run, or the result outran what a cord carries — the
      // caller then refuses whole.
      bool CollectLocked() override;

    private:
      // Walk the compiled program for one pair. The caller may hold the
      // store's guard: Evaluate is a fixed-stack walk, nothing more.
      ExprValue Eval(float left, float right, std::size_t position) const {
        float vars[kExprMaxVars] = {};
        vars[0] = left;
        vars[1] = right;
        vars[2] = static_cast<float>(position);
        return program.Evaluate(vars);
      }

      // Glue the tokens back together and compile — the one compile, control
      // thread only. gArrayExprBase's CompileExpression, over this object's
      // three placeholders: logs at E_ERROR and leaves the program invalid
      // on any failure, and an absent expression is left invalid silently.
      void CompileExpression();

      // The raw creation arguments after the two names, one token per space.
      // Registered as the trailing LIST param so a whitespace-bearing
      // expression survives Parameters::Set intact; CompileExpression glues
      // it back together, gExpr's arrangement.
      std::vector<std::string> expression;

      ExprProgram program;

      // What CompileError() answers — program.Error() plus the failures the
      // program cannot carry (a placeholder past $3).
      std::string compileError;
    };

  } // namespace PATCHER
} // namespace YSE
