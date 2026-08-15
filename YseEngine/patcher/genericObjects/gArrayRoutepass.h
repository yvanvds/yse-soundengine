#pragma once
#include "../pObject.h"
#include "gArray.h"
#include "gArrayEnds.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Route an array by the values it holds — issue #804's reading of
     *         Max's ``array.routepass`` on the name-addressed value model
     *         ``.array`` settled (#548).
     *
     *  The dispatcher for arrays: a patch that keeps state in named sequences
     *  sends each down the branch that understands it, which is what
     *  ``.route`` does for list text, ``.dict.route`` does for dictionaries,
     *  and this object does for arrays — ``.route``'s job with the array as
     *  the selector. ``gDictRoute`` is the model, including its
     *  rightmost-outlet-is-the-reject rule; what is matched is the **presence
     *  of a value** among the array's elements rather than a leading
     *  selector, and what leaves an outlet is the array's **reference**,
     *  never its contents — the routing decision is about *which branch*, and
     *  the receiver still binds the name itself.
     *
     *  ### The arguments are the name, then the values
     *
     *  An array never travels down a cord — an ``OUT_TYPE`` carries a value,
     *  not an identity (see gArray.h for the whole argument) — so the array
     *  is **bound from the first creation argument**, on the control thread:
     *  ``.array.routepass <name> <value> [<value> ...]`` resolves one
     *  ``shared_ptr<arrayStore>`` in ``SetParent`` / ``PARM_PARSE`` /
     *  ``RefreshBinding``, all of ``gArrayEndsBase``, unchanged. Every
     *  argument after the name is a value to test for, and the outlet count
     *  comes from that list, ``.routepass``'s argument-driven shape: one
     *  outlet per value plus the rightmost reject. With **no value
     *  arguments** the object has the reject outlet and nothing else —
     *  ``.routepass``'s own bare shape, since Max documents no default key
     *  for ``array.routepass`` and inventing one would put a branch in a
     *  patch that did not ask for one. At most ``MAX_SELECTORS`` values are
     *  held, the ceiling ``.routepass`` and ``.sel`` already use; tokens past
     *  it are dropped.
     *
     *  Every kept token gets an outlet, empty ones included, so an index
     *  into the arguments is an index into the outlets — ``gDictRoute``'s
     *  rule. An empty token, or one past ``ELEMENT_CAPACITY`` (which no
     *  stored element can ever spell), matches nothing and costs an
     *  unreachable outlet.
     *
     *  ### What a match is
     *
     *  ``ArrayFind`` under the store's guard — the presence question, asked
     *  for each value in argument order, and the **leftmost value present
     *  wins**: exactly one outlet fires per trigger, ``.routepass``'s rule,
     *  and a value repeated in the argument list uses the leftmost of its
     *  outlets only. The comparison is the family's byte compare —
     *  spelling is identity, so ``5`` and ``5.`` are different elements and
     *  a value matches only the element that spells it. That is a deliberate
     *  divergence from the scalar ``.routepass``, whose numeric selectors
     *  widen an int to a float: an array element *is* its spelling
     *  (gArray.h's element model), and the object that asks a numeric
     *  question of an array is ``.array.expr``'s family, not this one.
     *
     *  Two more deliberate divergences from Max, written down as the
     *  family's rule requires:
     *
     *  - Max's ``match`` attribute picks *where* the key is looked for —
     *    first element, last element, or anywhere. The behaviour ported is
     *    **anywhere** — #804's "route a message by the values an array
     *    holds" — because that is the one the value model could not already
     *    spell: routing on the first element alone is ``.array.at <name> 0``
     *    into the scalar ``.routepass``, one cord.
     *  - Max's special keys (``emptystring``, ``emptyarray``, ``<empty>``)
     *    are not ported: an empty or unnamed-store array simply holds none
     *    of the values and leaves the reject, and "empty" is already
     *    routable as ``.array.length`` into ``.sel 0``.
     *
     *  ### What arrives, and what leaves
     *
     *  - **A bang routes**: the bound array is tested for each value in
     *    argument order, and the reference ``array <name>`` leaves the
     *    outlet of the leftmost value present — or the rightmost reject when
     *    it holds none of them, reference intact, so a chain of
     *    ``.array.routepass`` objects strings together with each reject
     *    feeding the next inlet, every stage testing the array the first one
     *    saw.
     *  - **``array <name>`` routes too** — the message an ``.array``'s
     *    reference outlet emits on a bang, so wiring that outlet here gives
     *    Max's own gesture: bang the array, and it dispatches itself.
     *    Honoured only when it names the array already bound
     *    (``ArrayReferenceNames``' bounded compare) and refused and counted
     *    otherwise, because resolving an unrecognised name means the
     *    registry's mutex on whatever thread the message arrived on.
     *  - **``array <name>`` on the reference inlet is acknowledged
     *    silently** when it names the bound array — the family's cold
     *    inlet, ``gArrayStatsBase``'s shape — and anything else there is
     *    refused and counted. No int or float method anywhere: a bare
     *    number names no array.
     *  - **An unnamed ``.array.routepass`` is inert**: its private array
     *    has no name to pass on (``gArray``'s rule for an unnamed
     *    reference), so a trigger routes nothing and sends nothing —
     *    silently, since the wiring is not an error, merely incomplete.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlet, the
     *  family's rule. No message path allocates, locks or blocks: the name
     *  is resolved on the control thread, the decision is at most
     *  ``MAX_SELECTORS`` bounded scans of at most
     *  ``arrayStore::MAX_ELEMENTS`` elements under **one hold of the
     *  store's guard** — released before the send, which runs the whole
     *  downstream subgraph and may well write into this same array — and
     *  the reference is built once per re-parse, so the send is of a string
     *  the object already owns. A trigger that loses the store's try-lock
     *  is refused whole and counted rather than made to wait.
     *
     *  A test-and-set guard is held across decision and send —
     *  ``gDictRoute``'s, for ``gDictRoute``'s reason: the emitted reference
     *  is itself a trigger, so an outlet wired back into the inlet —
     *  directly or round a chain — would recurse without bound on whatever
     *  thread the trigger arrived on. The loser is refused and counted
     *  rather than made to spin.
     *
     *  ### What persists
     *
     *  The creation arguments, because they are creation arguments. The
     *  array's contents persist with the ``.array`` that owns them.
     */
    class gArrayRoutepass : public gArrayEndsBase {
    public:
      gArrayRoutepass();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_ROUTEPASS;
      }
      CREATE(gArrayRoutepass)

      /**
       *  @brief Most value arguments — and so at most ``MAX_SELECTORS + 1``
       *         outlets.
       *
       *  256, the ceiling ``.routepass``, ``.sel``, ``.trigger`` and
       *  ``.router`` already use, and also ``arrayStore::MAX_ELEMENTS`` —
       *  every value costs an outlet, and tokens past the ceiling are
       *  dropped.
       */
      static constexpr int MAX_SELECTORS = 256;

      /** @brief How many value arguments — and so how many match outlets —
       *         the object built. The outlet count is one more: the
       *         reject. */
      std::size_t SelectorCount() const {
        return selectors.size();
      }

      /** @brief The value of match outlet @p index, or empty out of range.
       *         Diagnostics and tests; control thread only. */
      std::string SelectorAt(std::size_t index) const;

      /** @brief The message a route emits — ``"array <name>"``, or empty
       *         for an unnamed object, which has no name to pass on. */
      const std::string& Reference() const {
        return reference;
      }

      /** @brief Triggers that completed a routing decision and sent the
       *         reference. Diagnostics and tests. */
      std::uint64_t Routed() const {
        return routed.load(std::memory_order_relaxed);
      }

      void BangIn(int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      // Extends gArrayEndsBase's hook so a re-parse drops the values along
      // with the name: SetParams("") must leave the bare, reject-only object
      // behind rather than one still holding the previous branches.
      void ClearParams() override;

      // Rebuild the selectors, the reference and the outlets after a
      // re-parse — control thread only, the base's hook.
      void ParamsChanged() override;

    private:
      // Rebuild the outlets from the current selectors, docs included: one
      // per value plus the rightmost reject. Control thread only — the
      // constructor and the parameter hooks, all of which run before the
      // object is wired or published; a *live* SetParams never reaches here
      // on a published object, because the registered callbacks make
      // ParamsNeedRebuild() true and #234 replaces the object. gRoute's
      // rule.
      void ShapePorts();

      // Rebuild `reference` from the current name. Control thread only.
      void RefreshReference();

      // The routing itself: pick the leftmost value present under one hold
      // of the store's guard, release, send the reference out the matched
      // outlet — or the reject. Refuses (counted) on a lost guard or a
      // re-entrant trigger; inert (silent) while the object is unnamed.
      void Route(YSE::THREAD thread);

      // The values as typed — everything after the name. LIST parameter,
      // control thread only.
      std::vector<std::string> selectorArgs;

      // The values, parallel to the match outlets — every kept token, empty
      // ones included, so an index into the arguments is an index into
      // `outputs`. Built by ParamsChanged and never resized by a message
      // handler; a message path only ever reads the characters.
      std::vector<std::string> selectors;

      // "array <name>", built once per re-parse so a route is a send of a
      // string the object already owns rather than a concatenation on
      // whichever thread the trigger arrived on.
      std::string reference;

      // The re-entrancy guard, held across decision and send: the emitted
      // reference is itself a trigger, so an outlet wired back into the
      // inlet would recurse without bound. The loser is dropped and counted
      // rather than made to spin — gDictRoute's guard, for gDictRoute's
      // reason.
      std::atomic<bool> busy{false};

      std::atomic<std::uint64_t> routed{0};
    };

  } // namespace PATCHER
} // namespace YSE
