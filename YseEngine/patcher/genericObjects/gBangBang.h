#pragma once
#include "../pObject.h"
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Fan one input out as a bang from many outlets, right to left —
     *         ``.bangbang`` (issue #467).
     *
     *  Max's ``bangbang``: "Outputs bang messages out of each outlet (in
     *  right-to-left order) when it receives any input. The number of outlets
     *  is determined by an argument."
     *
     *  The name is ``.bangbang`` only. Max's short spelling is ``b``, and
     *  ``.b`` is already this patcher's button.
     *
     *  ### The ordering guarantee
     *
     *  The same one ``.trigger`` states, and for the same reason: outlet *n-1*
     *  is sent first and outlet 0 last, and each send **completes in full** —
     *  the whole subgraph hanging off that outlet, depth first — before the
     *  next one starts. That falls out of the patcher's synchronous send path,
     *  where ``outlet::SendBang`` walks its target list calling
     *  ``inlet::SetBang`` directly with no queue in between, so it holds on the
     *  audio thread (through the pinned ``GraphState`` of #226, which changes
     *  *which* adjacency list is walked, not that it is walked in order) as
     *  well as the control thread.
     *
     *  Fan-out order *within* a single outlet is deliberately not guaranteed,
     *  as in Max; the answer to needing it is another ``.bangbang``.
     *
     *  ### How this differs from ``.trigger b b b``
     *
     *  It was worth checking rather than assuming, because the two are not
     *  simply spellings of each other. What comes *out* is identical — N bangs,
     *  right to left, one per outlet, for any input — so ``.bangbang 3`` and
     *  ``.trigger b b b`` are behaviourally interchangeable. Three things
     *  differ, all on the argument side:
     *
     *  1. **The argument is a count, not a list of formats.** ``.bangbang 3``
     *     asks for three outlets; ``.trigger 3`` asks for *one* outlet that
     *     emits the int constant 3 on every input. The same token means
     *     opposite things in the two objects, which is the whole reason
     *     ``.bangbang`` is worth a separate box rather than an alias.
     *  2. **The ceiling is Max's, and it is different.** Max documents
     *     ``bangbang``'s outlet count as "any number between 1 and 40" and
     *     documents no limit at all for ``trigger`` (this patcher caps that one
     *     at 256). So ``.bangbang 100`` is 40 outlets, where the equivalent
     *     ``.trigger`` argument list would be 100.
     *  3. **A float argument is truncated, not rejected.** Max: "Floats are
     *     converted to ints", so ``.bangbang 3.7`` has three outlets.
     *
     *  ``.trigger`` also cannot be *given* a count — the outlet total is the
     *  argument total — so a patch whose outlet count is generated (from a
     *  saved parameter, say) can say it in one token here and not there.
     *
     *  ### The argument
     *
     *  One optional number: the outlet count, clamped into ``[1, 40]``.
     *  Max's range, and both ends are load-bearing — an object with no outlets
     *  could receive input and do nothing observable at all, which is not a
     *  shape Max lets you build.
     *
     *  The reference does not state a default. Two is used, which is Max's own
     *  no-argument shape and the documented default of ``.trigger``, the object
     *  this one is the degenerate case of; a single-outlet default would make a
     *  bare ``.bangbang`` a ``.b`` with no blink, which is not an object anyone
     *  types on purpose. An argument that is not a number falls back to the
     *  same default rather than to one outlet, so a typo costs the object its
     *  count and not its usefulness.
     *
     *  The token is read through the shared strict ``ReadNumericToken``, not
     *  through ``ExprParseFloatList``: the expression reader skips what it
     *  cannot parse, so ``3abc`` would come back as 3, and folds a non-finite
     *  reading to 0, so ``1e999`` would come back as a count of zero. Both are
     *  wrong for deciding whether a creation argument is a number at all.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing. The object is driven by its inlet, and a
     *  ``Calculate()`` that emitted would re-fire the whole fan-out on every
     *  DSP tick after the inlet last ran — the rule ``.sel``, ``.trigger``,
     *  ``.route`` and ``.past`` establish.
     *
     *  No path allocates, locks or blocks. The outlets are built on the control
     *  thread by the constructor and the parameter callbacks, all of which run
     *  before the object is wired or published, and no message handler touches
     *  the vector at all. Emitting is a reverse walk doing one ``SendBang`` per
     *  outlet, carrying no payload and so needing no conversion, no formatting
     *  and no string.
     *
     *  ### Why there is no shared base with ``.trigger``
     *
     *  ``.trigger`` (#466) concluded its emit machinery was not worth sharing,
     *  and building this object confirms it. The whole of the emitter here is a
     *  three-line countdown calling ``SendBang``; ``.trigger``'s is a countdown
     *  around an eight-arm switch over a per-outlet slot table it needs and
     *  this object has none of. A common base could only hold the countdown
     *  itself, which would cost a virtual call per outlet — on the send path,
     *  which is exactly where this family refuses to trade — to save three
     *  lines, and would couple two objects whose argument grammars are not
     *  merely different but assign opposite meanings to the same token. The
     *  reuse that *was* worth it is the small shared pieces: ``ReadNumericToken``
     *  and ``OutletLabel`` in ``pListArgs.h``, and ``TestHelpers::OrderSink``
     *  on the test side.
     */
    PATCHER_CLASS(gBangBang, YSE::OBJ::G_BANGBANG)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(SetBang)
    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Fewest outlets the object will build.
     *
     *  1, the bottom of Max's documented range. An object with no outlets could
     *  take input and produce nothing observable.
     */
    static constexpr int MIN_OUTLETS = 1;

    /**
     *  @brief Most outlets the object will build.
     *
     *  40 — Max: "The number of outlets can be any number between 1 and 40."
     *  Deliberately *not* ``.trigger``'s 256: that ceiling is this patcher's
     *  invention for an object Max gives no limit, where this one is Max's own
     *  stated range for this object.
     */
    static constexpr int MAX_OUTLETS = 40;

    /** @brief Max's shape for a bare ``.bangbang``, and what a bad argument
     *         falls back to. */
    static constexpr int DEFAULT_OUTLETS = 2;

    /** @brief How many outlets the object has — always in ``[1, 40]``. */
    int OutletCount() const {
      return outletCount;
    }

  private:
    // The whole object: walk the outlets right to left and bang each one.
    // Shared by all four inlet handlers, so there is exactly one place the
    // firing order is decided.
    void EmitAll(THREAD thread);

    // Rebuild the outlets from `outletCount`, docs included. Control thread
    // only: called from the constructor and from the parameter callbacks, all
    // of which run before the object is wired or published.
    void ShapePorts();

    // The creation argument, as text. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::string outletArg;

    // The resolved outlet count, always in [MIN_OUTLETS, MAX_OUTLETS]. Sized
    // by ParseParams() / ClearParams() before the object is published and never
    // written by a message handler, so the emitter's walk cannot race anything.
    int outletCount = DEFAULT_OUTLETS;
  };
}
}
