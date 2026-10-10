#pragma once
#include "../math/gExprEval.h"
#include "../pAtomList.h"
#include "../pObject.h"
#include "gArray.h"
#include "gArrayEnds.h"
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Shared body for the per-element expression family of ``array.*``
     *         — ``.array.expr``, ``.array.map``, ``.array.filter``,
     *         ``.array.reduce``, ``.array.every``, ``.array.some`` and
     *         ``.array.foreach`` (issue #799).
     *
     *  The family that decides what "a function" is in a patcher with no
     *  lambda: an object cannot take a callback — there is no closure to pass
     *  down a cord — so the per-element computation is **text**, and the
     *  patcher already owns an evaluator for exactly that:
     *  ``ExprProgram`` (``math/gExprEval.h``), the compiler behind ``.expr``
     *  and ``.vexpr``. Nothing here parses anything; every object is one walk
     *  of that compiled program per element. #799 filed the seven objects as
     *  one issue because they share four unanswered questions, and the whole
     *  reason this base exists is that the answers are written once:
     *
     *  ### 1. The expression is a creation argument
     *
     *  ``.array.map <name> <expression>`` — compiled once, in ``ParseParams``
     *  on the control thread, ``.expr``'s own arrangement and the portable
     *  answer the issue names: ``ExprProgram::Compile`` allocates, so a
     *  message path may never reach it, and a live ``SetParams`` takes the
     *  structural-replacement route (#234) because the registered clear/parse
     *  callbacks make ``ParamsNeedRebuild()`` true. A malformed expression
     *  fails **at parse time, loudly** — the error goes to the log at
     *  ``E_ERROR``, ``.expr``'s rule — and the object then **refuses every
     *  trigger**, counted. Not ``.expr``'s evaluate-to-0: its 0 goes down a
     *  cord, where this family's would be *written into a shared array* or
     *  compact one to nothing, and a misconfigured object silently rewriting
     *  data is the one thing worse than one that refuses.
     *
     *  ### 2. What the element is bound to
     *
     *  ``$f1`` / ``$i1`` is the element — ``.expr``'s spelling, the issue's
     *  own suggestion — and ``$f2`` / ``$i2`` is its **position** in the
     *  array, which is what makes ``$i2 % 2 == 0`` a filter over the even
     *  positions and ``$i2 * 2`` a map to a ramp. ``.array.reduce`` alone
     *  shifts by one: ``$1`` is the **accumulator**, ``$2`` the element and
     *  ``$3`` the position, the (acc, value) order every fold spells.
     *  An expression referencing a placeholder past what its object binds is
     *  rejected at parse time, loudly — an inlet it names can never exist.
     *
     *  ### 3. A symbol element has no number, so the expression never runs
     *  for it
     *
     *  The population an arithmetic expression sees is the numeric elements —
     *  an element is numeric when ``ReadNumericToken`` reads the whole of it
     *  as one finite number, the classifier ``AtomList`` and the statistics
     *  family (#790) already use. Refusing a whole message over one symbol
     *  would make the family unusable on the mixed arrays everything else
     *  happily holds, and coercing a symbol to 0 would be silent corruption.
     *  What happens to the element the expression never saw is then each
     *  object's own operation, decided together so the seven cannot disagree:
     *
     *  - ``.array.expr``, ``.array.map`` and ``.array.foreach`` **pass it
     *    through unchanged** — the operations are positional, so the element
     *    itself stands in for the missing result. That keeps every position
     *    stable, and it is what makes ``.array.expr``'s list spell exactly
     *    the array ``.array.map`` would have produced.
     *  - ``.array.filter`` **does not keep it**: the kept elements are the
     *    ones the expression accepted, and it cannot accept what it cannot
     *    see. A mixed array filtered on ``$f1 > 60`` answers with numbers.
     *  - ``.array.reduce``, ``.array.every`` and ``.array.some`` **skip it**
     *    — the fold and the quantifiers run over the numeric population,
     *    exactly as ``.array.mean`` does (#790).
     *
     *  ### 4. ``map`` writes back, ``filter`` compacts in place
     *
     *  The issue's last question, and the family answers it by having both
     *  forms: ``.array.expr`` **emits** the transformed list and mutates
     *  nothing, so ``.array.map`` **writes back** into the array — deciding
     *  otherwise would make the two the same object — and ``.array.filter``
     *  compacts in place beside it, the mutating pair announcing the
     *  reference ``array <name>`` afterwards so the family chains
     *  (``.array.fill``'s outlet, #794). Yes, a compaction renumbers under
     *  every other object on the name — exactly as ``.array.remove`` and
     *  ``.array.sort`` already do; renumbering is what a mutating operation
     *  on a sequence *is*, and the whole rewrite is **one hold of the
     *  store's guard**, so no reader ever sees a half-mapped array and a
     *  writer on another thread loses the try-lock, dropped and counted
     *  (the store's rule).
     *
     *  ### The binding, and the trigger
     *
     *  Everything about the array is ``gArrayEndsBase``'s, inherited whole:
     *  bound from the first creation argument on the control thread, an
     *  ``array <name>`` message honoured only when it names the array already
     *  bound (``ArrayReferenceNames``' bounded compare), an unnamed object
     *  acting on a private, empty array of its own, and refusals counted,
     *  never logged. The ask is ``gArrayStatsBase``'s shape: a bang or the
     *  bound array's reference on the hot inlet triggers, the cold inlet
     *  acknowledges the reference silently, and there is no int or float
     *  method anywhere — a bare number names no array.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — every object here is driven by its
     *  inlet, the family's rule. No message path allocates, locks or blocks:
     *  the name is resolved and the expression compiled on the control
     *  thread, and ``ExprProgram::Evaluate`` is a walk of the compiled
     *  program over a fixed-size stack in the caller's frame — no allocation,
     *  no lock, no exception, no recursion — which is why running it *under*
     *  the store's guard is legitimate where an outlet send never is.
     */
    class gArrayExprBase : public gArrayEndsBase {
    public:
      /// Empty when the current expression compiled and fits this object's
      /// placeholders; otherwise what was logged. Control thread only.
      const std::string& CompileError() const {
        return compileError;
      }

      /// True when a trigger has a program to run — a successful compile
      /// whose placeholders all exist here. False refuses every trigger.
      bool ProgramReady() const {
        return program.Valid();
      }

      void BangIn(int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      // `varLimit` is how many `$` slots this object binds: 2 for the
      // element-wise six (element, position), 3 for reduce (accumulator,
      // element, position). An expression reaching past it is rejected at
      // parse time.
      explicit gArrayExprBase(int varLimit);

      // Extends gArrayEndsBase's hook so a re-parse drops the program along
      // with the name: SetParams("") must not keep evaluating whatever the
      // previous arguments compiled. gArrayFill's rule.
      void ClearParams() override;

      // Recompile after a re-parse. A subclass with derived state of its own
      // (the mutating pair's reference message) overrides and calls through.
      void ParamsChanged() override;

      // The operation itself — what a bang and the reference gesture both
      // come down to. Runs on whichever thread the trigger arrived on; the
      // base has already refused the trigger when the program is not ready.
      virtual void Trigger(YSE::THREAD thread) = 0;

      // Walk the compiled program for one element. The caller may hold the
      // store's guard: Evaluate is a fixed-stack walk, nothing more.
      ExprValue Eval(float element, std::size_t position) const {
        float vars[kExprMaxVars] = {};
        vars[0] = element;
        vars[1] = static_cast<float>(position);
        return program.Evaluate(vars);
      }

      // The reduce spelling: $1 the accumulator, $2 the element, $3 the
      // position.
      ExprValue EvalFold(float accumulator, float element, std::size_t position) const {
        float vars[kExprMaxVars] = {};
        vars[0] = accumulator;
        vars[1] = element;
        vars[2] = static_cast<float>(position);
        return program.Evaluate(vars);
      }

      /// True when an evaluated result is nonzero — the predicate reading
      /// filter, every and some share, in the expression's own C type so an
      /// int 0 and a float 0. both read false.
      static bool Accepts(const ExprValue& value) {
        return value.isInt ? value.i != 0 : value.f != 0.f;
      }

    private:
      // What a bang and the reference gesture both come down to: refuse when
      // there is no program to run — see the class notes on why a
      // misconfigured object refuses rather than evaluating to 0 — and hand
      // a ready one to the subclass's Trigger.
      void Ask(YSE::THREAD thread);

      // Glue the tokens back together and compile — the one compile, control
      // thread only. Logs at E_ERROR and leaves the program invalid on any
      // failure, including a placeholder past `varLimit`. An *absent*
      // expression — a bare or name-only object, or the clear half of a
      // re-parse — is left invalid silently: there is nothing malformed to
      // report, the object simply refuses triggers until given work.
      void CompileExpression();

      // The raw creation argument after the name, one token per space.
      // Registered as the trailing LIST param so a whitespace-bearing
      // expression survives Parameters::Set intact; CompileExpression glues
      // it back together, gExpr's arrangement.
      std::vector<std::string> expression;

      ExprProgram program;

      // What CompileError() answers — program.Error() plus the failures the
      // program cannot carry (a placeholder past varLimit).
      std::string compileError;

      const int varLimit;
    };

    /**
     *  @brief Evaluate an expression per element and output the results as
     *         one message — Max's ``array.expr`` on the value model
     *         ``.array`` settled (issue #799).
     *
     *  ``.array.map``'s emitting twin: the same per-element evaluation, but
     *  the array is never touched — the results leave as the list they
     *  spell, ``.array.tolist``'s send with the transformation applied. A
     *  numeric element leaves as the expression's result, typed by the
     *  expression's C type (``$i1 + 1`` emits ints, ``$f1 * 0.5`` floats); a
     *  symbol element passes through unchanged, so the output list spells
     *  exactly the array ``.array.map`` would have produced and every
     *  position survives the trip. One element leaves as the int, float or
     *  symbol it is rather than as a list of one — ``SendAtoms``' rule. The
     *  collection happens under one hold of the store's guard and the send
     *  after it is released, so the answer is the array as it stood at the
     *  trigger. An empty or unnamed (private) array bangs the empty outlet
     *  instead; a result list past what a cord carries loses its tail, every
     *  lost element a counted refusal — ``getvalue``'s rule, the list-text
     *  senders' shared answer (#796).
     */
    class gArrayExpr : public gArrayExprBase {
    public:
      gArrayExpr();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_EXPR;
      }
      CREATE(gArrayExpr)

    protected:
      void Trigger(YSE::THREAD thread) override;

    private:
      // The results, collected under the guard and sent after it is
      // released — gArrayConvertBase's working list, for its reason.
      AtomList emitList;

      // Render buffer for the outlet, reserved to AtomList::RENDER_CAPACITY
      // at construction.
      std::string emitScratch;
    };

    /**
     *  @brief The mutating pair's shared body — ``.array.map`` rewrites
     *         every element, ``.array.filter`` keeps the accepted ones
     *         (issue #799).
     *
     *  Both act entirely inside **one hold of the store's guard** — the
     *  family's mid-walk answer: there is no walk to be in the middle of, so
     *  the array goes from untouched to finished in one step and no reader
     *  ever sees it half-done. Both announce the bound array's reference,
     *  ``array <name>``, after a rewrite that landed — the way an array
     *  leaves an object on the value model, ``.array.fill``'s outlet — and
     *  an unnamed object stays silent: the rewrite happens, but there is no
     *  name to pass on. A refused trigger (a lost try-lock, a program that
     *  never compiled) announces nothing.
     */
    class gArrayExprMutateBase : public gArrayExprBase {
    public:
      /** @brief The message the outlet emits after a rewrite that landed —
       *         ``"array <name>"``, or empty for an unnamed object. */
      const std::string& Reference() const {
        return reference;
      }

    protected:
      explicit gArrayExprMutateBase(int varLimit);

      void ParamsChanged() override;

      void Trigger(YSE::THREAD thread) override;

      // The rewrite itself. The caller holds the store's guard.
      virtual void ApplyLocked() = 0;

    private:
      // Rebuild `reference` from the current name. Control thread only.
      void RefreshReference();

      // "array <name>", built once per rebind so a landed rewrite is a send
      // of a string the object already owns rather than a concatenation on
      // whichever thread the trigger arrived on.
      std::string reference;
    };

    /**
     *  @brief Apply an expression to every element of an array, writing the
     *         results back — Max's ``array.map`` on the value model
     *         ``.array`` settled (issue #799).
     *
     *  The issue's "does map write back or emit?", answered: **it writes
     *  back** — ``.array.expr`` is the emitting form, and two objects doing
     *  the same thing would be one object twice. Every numeric element is
     *  replaced by the expression's result, spelled the way the patcher
     *  spells a number (``ExprFormatValue`` — an int result stays visibly an
     *  int, a float keeps its point); a symbol element stays exactly where
     *  and what it was, so the array's length and every position survive.
     *  The whole rewrite is one hold of the store's guard, then the
     *  reference announces. Read ``gArrayExprMutateBase`` for the announce
     *  and ``gArrayExprBase`` for the expression, the binding and the guard.
     */
    class gArrayMap : public gArrayExprMutateBase {
    public:
      gArrayMap();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_MAP;
      }
      CREATE(gArrayMap)

    protected:
      void ApplyLocked() override;
    };

    /**
     *  @brief Keep the elements of an array an expression accepts — Max's
     *         ``array.filter`` on the value model ``.array`` settled
     *         (issue #799).
     *
     *  The issue's "does filter compact in place or produce a list?",
     *  answered: **it compacts in place** — the mutating counterpart of
     *  ``.array.expr``'s emit, exactly as ``.array.map`` is of its map. An
     *  element is kept when the expression evaluates nonzero for it —
     *  ``.if``'s truth, in the expression's own C type — and the kept
     *  elements close ranks in their original order. A symbol element is
     *  not kept: the kept elements are the ones the expression accepted,
     *  and it cannot accept what it cannot see (the family's population
     *  rule, decision 3 on the base). Yes, that renumbers under every other
     *  object on the name — renumbering is what a removal *is*, exactly as
     *  ``.array.remove``; the whole compaction is one hold of the store's
     *  guard, then the reference announces. Read ``gArrayExprMutateBase``
     *  and ``gArrayExprBase`` for the rest.
     */
    class gArrayFilter : public gArrayExprMutateBase {
    public:
      gArrayFilter();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_FILTER;
      }
      CREATE(gArrayFilter)

    protected:
      void ApplyLocked() override;
    };

    /**
     *  @brief Fold an array to one value with an expression — Max's
     *         ``array.reduce`` on the value model ``.array`` settled
     *         (issue #799).
     *
     *  The one object whose expression binds three things: ``$1`` the
     *  accumulator, ``$2`` the element, ``$3`` its position — so
     *  ``$f1 + $f2`` is a sum, ``$f1 * $f2`` a product and
     *  ``max($f1, $f2)`` a running maximum. The fold runs over the numeric
     *  population in order; the **accumulator starts as the first numeric
     *  element** and the expression runs from the second on — the rule
     *  JavaScript's ``reduce`` applies without an initial value, and the
     *  only seed that leaves ``min``, ``max`` and a product meaning what
     *  they say (a fixed 0 would break every fold that is not a sum). One
     *  numeric element answers itself, typed by its spelling; the result of
     *  a real fold is typed by the expression's C type. A population with
     *  nothing in it — an empty or unnamed array, or one holding only
     *  symbols — bangs the empty outlet instead: the fold of nothing does
     *  not exist, and a sentinel would be indistinguishable from a real
     *  answer (``.array.mean``'s outlet, #790). The whole fold is one hold
     *  of the store's guard, the answer sent after release.
     */
    class gArrayReduce : public gArrayExprBase {
    public:
      gArrayReduce();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_REDUCE;
      }
      CREATE(gArrayReduce)

    protected:
      void Trigger(YSE::THREAD thread) override;

    private:
      // The answer, computed under the guard and sent after it is released.
      ExprValue result;
    };

    /**
     *  @brief The quantifier pair's shared body — ``.array.every`` and
     *         ``.array.some`` are one scan with the short-circuit flipped
     *         (issue #799).
     *
     *  Both ask the expression about each numeric element in turn — the
     *  population rule, decision 3 on the base — and both answer an int, 1
     *  or 0, out their single outlet: a verdict is a truth, ``.==``'s
     *  output, not a piece of the data. ``every`` answers 0 at the first
     *  element the expression rejects and 1 past the last; ``some`` answers
     *  1 at the first it accepts and 0 past the last. An empty population —
     *  an empty or unnamed array, or one holding only symbols — answers the
     *  quantifier's own identity: **every answers 1** (a claim about
     *  nothing is vacuously true, the logician's rule and JavaScript's
     *  ``[].every``) and **some answers 0** (nothing satisfied it). That is
     *  why the pair has no empty outlet where the fold needs one: a
     *  quantifier always has an answer. One hold of the store's guard, the
     *  verdict sent after release.
     */
    class gArrayQuantifierBase : public gArrayExprBase {
    protected:
      // `wantAll` is the whole difference between .array.every and
      // .array.some.
      explicit gArrayQuantifierBase(bool wantAll);

      void Trigger(YSE::THREAD thread) override;

    private:
      const bool wantAll;
    };

    /**
     *  @brief Report whether every element of an array satisfies an
     *         expression — Max's ``array.every`` on the value model
     *         ``.array`` settled (issue #799).
     *
     *  Read ``gArrayQuantifierBase`` for the scan, the vacuous answer and
     *  the guard, and ``gArrayExprBase`` for the expression and the binding.
     */
    class gArrayEvery : public gArrayQuantifierBase {
    public:
      gArrayEvery();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_EVERY;
      }
      CREATE(gArrayEvery)
    };

    /**
     *  @brief Report whether any element of an array satisfies an
     *         expression — Max's ``array.some`` on the value model
     *         ``.array`` settled (issue #799).
     *
     *  Read ``gArrayQuantifierBase`` for the scan, the vacuous answer and
     *  the guard, and ``gArrayExprBase`` for the expression and the binding.
     */
    class gArraySome : public gArrayQuantifierBase {
    public:
      gArraySome();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_SOME;
      }
      CREATE(gArraySome)
    };

    /**
     *  @brief Run an expression for each element of an array, outputting one
     *         result at a time — Max's ``array.foreach`` on the value model
     *         ``.array`` settled (issue #799).
     *
     *  ``.array.iter`` with the expression applied in flight — the "do this
     *  for each element" object where the *this* is the expression rather
     *  than the downstream patch: one send per element, first to last, then
     *  one bang out the done outlet, last (the carry exception to
     *  right-to-left, ``.uzi``'s rule). A numeric element leaves as the
     *  expression's result, typed by the expression's C type; a symbol
     *  element leaves unchanged, as the symbol it is — the pass-through,
     *  decision 3 on the base — so the stream spells ``.array.map``'s
     *  result serialised. The done bang fires even for an empty or unnamed
     *  array (a loop that does not run is not an error) but never for a
     *  refused walk.
     *
     *  Everything structural is ``.array.iter``'s (#798), inherited
     *  deliberately because the hazards are identical: the walk is a
     *  **snapshot** — the trigger copies the array out under one hold of
     *  the store's guard into rows the object pre-allocated at construction,
     *  releases, and walks the snapshot, so a renumbering write arriving
     *  mid-walk (including from the elements' own subgraph) moves the store
     *  and never the walk in flight, and two on one name walk independently
     *  (``.coll``'s per-object pointer rule). A trigger arriving mid-walk —
     *  the loop-back cord, or another thread — is refused and counted under
     *  a test-and-set guard held across the whole walk, ``.uzi``'s
     *  re-entrant start rule, and a refused walk emits no done bang. Up to
     *  256 subgraph traversals per trigger, on whichever thread sent it —
     *  ``.array.iter``'s documented message budget.
     */
    class gArrayForeach : public gArrayExprBase {
    public:
      gArrayForeach();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_FOREACH;
      }
      CREATE(gArrayForeach)

      /** @brief Most elements one trigger can emit —
       *         ``arrayStore::MAX_ELEMENTS``. */
      static constexpr std::size_t MAX_ITEMS = arrayStore::MAX_ELEMENTS;

    protected:
      void Trigger(YSE::THREAD thread) override;

    private:
      // The array as it stood at the trigger, copied out under the store's
      // guard so no renumbering write can move rows under the cursor —
      // gArrayIter's snapshot, for gArrayIter's reason. Allocated whole by
      // arrayStore's own constructor, on the control thread, once.
      arrayStore snapshot;

      // Render buffer for the element outlet; only the symbol pass-through
      // touches it.
      std::string emitScratch;

      // The re-entrancy guard, held across the whole walk — gArrayIter's,
      // for gArrayIter's reason: a trigger looping back from either outlet
      // would rewrite the snapshot being walked. The loser is dropped and
      // counted rather than made to spin.
      std::atomic<bool> busy{false};
    };

  } // namespace PATCHER
} // namespace YSE
