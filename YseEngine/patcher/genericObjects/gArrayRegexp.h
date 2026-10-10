#pragma once
#include "../pAtomList.h"
#include "../pObject.h"
#include "gArray.h"
#include "gArrayEnds.h"
#include "gRegexEngine.h"
#include <cstddef>
#include <string>

namespace YSE {
  namespace PATCHER {

    /// VM steps one trigger may spend across every element's match — the same
    /// number ``.regexp`` grants one message (``kRegexpStepBudget``), chosen
    /// here on purpose: one trigger of ``.array.regexp`` never costs whichever
    /// thread it arrived on more matching than one ``.regexp`` message already
    /// may. One counter is shared by the whole scan, so the worst case is this
    /// number rather than 256 times it; a scan that runs dry is refused whole
    /// and counted — see the class notes on whole-answer-or-nothing.
    constexpr int kArrayRegexpStepBudget = 200000;

    /**
     *  @brief Keep the elements of an array a regular expression matches —
     *         issue #803's reading of Max's ``array.regexp`` on the
     *         name-addressed value model ``.array`` settled (#548).
     *
     *  The text filter the family was missing: ``.array.filter`` keeps the
     *  numeric elements an *expression* accepts and structurally cannot see a
     *  symbol (#799's population rule), where this object matches every
     *  element — numeric or not — as the text that spells it, which is the
     *  operation an array of file names or OSC addresses needs. A bang, or
     *  the bound array's reference ``array <name>``, matches every element
     *  against the pattern compiled from the creation arguments and sends the
     *  ones it matched as one typed message out the matched outlet — order
     *  kept, repeats kept, one match leaving as the int, float or symbol it
     *  spells (``SendAtoms``' rule). When nothing matched — an empty or
     *  unnamed (private) array included — the no-match outlet bangs instead:
     *  "no data" is a state a patch must be able to route on, not an error,
     *  ``.array.sect``'s empty outlet for ``.array.sect``'s reason.
     *
     *  A deliberate divergence, written down as the family's rule requires:
     *  Max's own ``array.regexp`` treats the incoming array as **one subject
     *  buffer** — bytes, offsets, per-match subarrays and a substitution over
     *  the whole of it. On the value model an array never travels down a cord
     *  and is not a byte buffer; #803 specifies the per-element match ("match
     *  an array's elements against a pattern"), and that is what is built.
     *  The reporting half — per-element matched substrings, capture groups,
     *  substitution — is deliberately not duplicated here either: that is
     *  ``.regexp`` (#452), and it is already composable per element as
     *  ``.array.iter`` into ``.regexp``, where the array-level filter is the
     *  operation the family could not spell in one message —
     *  ``.array.replace``'s composability reasoning, applied once more.
     *
     *  ### The pattern, and the engine it runs on
     *
     *  ``.array.regexp <name> <pattern>`` — the pattern is one token (the
     *  parameter tokenizer splits on spaces: write ``\s`` or ``\x20`` for a
     *  space) and the language is ``RegexProgram``'s PCRE-flavoured core,
     *  documented on gRegexEngine.h. #803 asks what the patcher already has
     *  before any dependency is added, and the answer is that engine: #452
     *  built the bounded matcher precisely because ``std::regex`` allocates,
     *  throws and backtracks without limit at *match* time, so no new regex
     *  facility and no ``std::regex`` appear here.
     *
     *  Compiled **once, on the control thread**, in ``ParseParams`` —
     *  ``.regexp``'s arrangement, and a live ``SetParams`` takes the
     *  structural-replacement route (#234) because the registered clear/parse
     *  callbacks make ``ParamsNeedRebuild()`` true. A malformed pattern fails
     *  there, loudly — logged at ``E_ERROR`` — and the object then **refuses
     *  every trigger**, counted: ``.array.filter``'s rule rather than
     *  ``.regexp``'s pass-through, because "matched nothing" is one of this
     *  object's honest answers and a misconfigured object must not be able to
     *  spell it. An absent pattern — a bare or name-only object — is inert
     *  the same way, silently: nothing malformed to report, the object simply
     *  refuses triggers until given work.
     *
     *  ### What a match is
     *
     *  ``RegexProgram::Search`` from position 0 — the pattern matches an
     *  element when it matches **anywhere** in it, ``.regexp``'s own reading,
     *  so ``wav`` matches ``kick.wav`` and ``wavetable`` alike and the
     *  anchors ``^`` and ``$`` are how a whole-element match is spelled
     *  (``\.wav$`` for the file-name use). A numeric element is matched as
     *  its text — ``60`` the characters ``60``, ``7.`` the characters
     *  ``7.`` — the same spelling-is-identity rule the family's byte compare
     *  applies.
     *
     *  ### Whole answer, or nothing
     *
     *  The matched list is an answer about **membership** — which elements
     *  the pattern accepted — so it is never sent as a fragment of itself,
     *  ``.array.sect``'s whole-refusal rule (#792) rather than the tail-loss
     *  rule of the serialising senders: a matched list that lost members
     *  would lie about what matched. Three refusals, each counted, each
     *  emitting nothing:
     *
     *  - **a lost try-lock** — the array's state is unknown;
     *  - **a matched list that cannot leave whole** — past what a cord
     *    carries (``AtomList``'s bounds);
     *  - **a scan that ran out of budget** — the step counter above, shared
     *    across every element's match, so an adversarial pattern (the
     *    exponential ``(a+)+b`` family) costs a number chosen here rather
     *    than one chosen by whoever typed the pattern. Past the point the
     *    budget dies a miss is indistinguishable from an unfinished match, so
     *    the honest answer is no answer.
     *
     *  ### One guard hold — the concurrent-write answer
     *
     *  The whole scan — every element's match and its collection into the
     *  emit list — happens under **one hold of the store's guard**, and the
     *  send after it is released: the family's mid-walk answer, there is no
     *  walk to be in the middle of, so the answer is the array as it stood at
     *  the trigger and a write the send triggers changes what the *next*
     *  trigger sees. Running the matcher under the guard is legitimate for
     *  the reason running ``ExprProgram::Evaluate`` there is (#799):
     *  ``Search`` is a fixed-stack, budget-bounded walk of an already-built
     *  program — no allocation, no lock, no exception, no recursion — where
     *  an outlet send runs the whole downstream graph and never belongs
     *  inside.
     *
     *  ### Binding, shape and real-time behaviour
     *
     *  All of ``gArrayEndsBase``, unchanged: bound from the first creation
     *  argument on the control thread, an ``array <name>`` message honoured
     *  only when it names the array already bound (``ArrayReferenceNames``'
     *  bounded compare), ``patcherImplementation::SetName`` re-anchoring
     *  through the shared base's virtual ``RefreshBinding``, and an unnamed
     *  object reading a private, empty array of its own. The ask is
     *  ``gArrayStatsBase``'s shape: trigger hot, reference inlet cold and
     *  acknowledging silently, no int or float method anywhere — a bare
     *  number names no array.
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlet, the
     *  family's rule. No message path allocates, locks or blocks: the name is
     *  resolved and the pattern compiled on the control thread, the scan is
     *  the VM over storage the program already owns, the matched elements are
     *  collected into an ``AtomList`` reserved at construction and rendered
     *  into a scratch reserved alongside it, and refusals are counted
     *  (``Dropped()``), never logged.
     */
    class gArrayRegexp : public gArrayEndsBase {
    public:
      gArrayRegexp();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_REGEXP;
      }
      CREATE(gArrayRegexp)

      /// True when a pattern compiled and a trigger will match. False refuses
      /// every trigger — see the class notes on why a misconfigured object
      /// refuses rather than matching nothing.
      bool Valid() const {
        return program.Valid();
      }

      /// Empty when the current pattern compiled, or when none is set;
      /// otherwise the compile error that was logged. Control thread only.
      const std::string& CompileError() const {
        return compileError;
      }

      void BangIn(int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      // Extends gArrayEndsBase's hook so a re-parse drops the program along
      // with the name: SetParams("") must not keep matching whatever the
      // previous arguments compiled. gArrayExprBase's rule.
      void ClearParams() override;

      // Recompile after a re-parse — the one compile, control thread only.
      void ParamsChanged() override;

    private:
      // The scan itself: match every element under one hold of the store's
      // guard, collecting the matched ones; release, then send — the typed
      // list out the matched outlet, or a bang out the no-match outlet when
      // nothing matched. Refuses (counted) on a lost guard, an invalid or
      // absent pattern, a dry step budget or a matched list that cannot
      // leave whole — see the class notes on each.
      void Match(YSE::THREAD thread);

      // Compile `pattern` into `program`. Control thread only; logs at
      // E_ERROR and leaves the program invalid on failure. An absent pattern
      // leaves it invalid silently.
      void CompilePattern();

      // The regular expression, one token — the second creation argument.
      std::string pattern;

      // Built by CompilePattern from the parameter above, read by the message
      // path. Control thread writes, and only while the object is unpublished
      // (the registered clear/parse callbacks make ParamsNeedRebuild() true).
      RegexProgram program;
      std::string compileError;

      // The matched elements, collected under the guard and sent after it is
      // released — gArrayExpr's working list, for gArrayExpr's reason.
      AtomList emitList;

      // Render buffer for the matched outlet, reserved to
      // AtomList::RENDER_CAPACITY at construction.
      std::string emitScratch;
    };

  } // namespace PATCHER
} // namespace YSE
