#pragma once
#include "../math/gExprEval.h"
#include "../pObject.h"
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief A bank of values as one object — ``.multislider`` (issue #554).
     *
     *  Max's ``multislider``, "an array of sliders". The patcher's controls all
     *  hold *one* number — ``.slider`` a position, ``.dial`` a value,
     *  ``.incdec`` a step, ``.rslider`` a pair — and the only way to hold a
     *  list today is ``.l``, which is one opaque blob with no way to reach an
     *  element. A step sequencer's levels, a graphic EQ's bands, a spectral
     *  envelope and a set of voice gains are all the same shape: N numbers that
     *  belong together, driven and drawn as one control but addressed one at a
     *  time.
     *
     *  **Not ``.table``, and both stay.** Issue #554 asks for the distinction to
     *  be said out loud. ``.table`` (#498) is *storage*: a dense int array a
     *  patch reads and writes by address, whose contents are saved with the
     *  patch and which sends one value at a time. This is a *control surface*:
     *  the host draws it and the user drives it, the whole bank leaves as one
     *  list on every message, the values are floats inside the object's own
     *  bounds, and the state is what ``GetGuiValue()`` reports rather than
     *  something a patch has to ask for. They overlap the way a fader bank
     *  overlaps a wavetable, which is to say not at all in practice.
     *
     *  ### N, and the whole of why this object is not a lock
     *
     *  The bank is ``std::vector<std::atomic<float>>``, allocated whole at
     *  ``MAX_CELLS`` on the control thread and **never resized**. ``size`` is
     *  not a capacity, it is a *bound*: how many of those cells are live. That
     *  single decision is what makes every question the audio thread could ask
     *  answerable without a lock, a copy or a rebuild:
     *
     *   - A walk samples the count once at the top and indexes below it. The
     *     count is always ≤ the capacity and the array never moves, so a count
     *     that changes mid-walk cannot free, move or invalidate a cell — it can
     *     only change what the *next* walk sees. There is nothing to hold a
     *     lock against, and ``.table``'s ``storeGuard`` is not needed either:
     *     that guard exists because a table's entries are read and written as a
     *     *group* (a sum, a quantile, an armed value plus its flag), while here
     *     every cell is an independent scalar, exactly as ``.slider``'s and
     *     ``.rslider``'s are.
     *   - Nothing structural depends on N. The inlet and the two outlets are
     *     the same whatever the count is, so a resize is not a graph edit and
     *     needs no ``GraphState`` swap. Compare ``.pack``, where the argument
     *     list *is* the inlet count and a re-parse must rebuild.
     *
     *  N is established two ways, and both are that one atomic store:
     *
     *   - the ``size`` creation argument. Max keeps this in its ``size``
     *     attribute and the patcher has no attribute mechanism, so it becomes a
     *     parameter — ``.table``'s reconciliation of the same problem. All
     *     three parameters are scalars and the object registers no clear/parse
     *     callbacks, so ``Parameters::NeedsRebuild()`` is false and a live
     *     ``SetParams`` rides the wait-free scalar plan (issue #234): the new
     *     count lands on the audio thread at the top of the next block, and the
     *     object — with every value in it — survives.
     *   - a list whose length differs from the current count. That is Max's
     *     ``listresize`` attribute, which defaults to on, and it is why Max's
     *     ``size`` default is 1: the object is meant to take the shape of the
     *     first list it is given. It is portable here precisely because a
     *     resize is one store; the cells are filled *before* the new count is
     *     published (a release store, read with an acquire load), so a reader
     *     that sees a longer bank sees the values that made it longer.
     *
     *  **Existing values survive a resize, in both directions.** Shrinking
     *  hides the tail rather than clearing it, and growing reveals it again
     *  (cells never written hold 0), so shortening a 16-step sequence to 8 and
     *  back does not cost the last 8 steps. The alternative — zeroing the tail
     *  on every shrink — would be an O(N) write on whichever thread the resize
     *  arrived on, and a resize arrives on the audio thread by both routes
     *  above. It would also buy nothing: a bank is cleared by sending it a 0,
     *  which is Max's float method and one message away.
     *
     *  The default count is **8** rather than Max's 1. Max can default to 1
     *  because ``listresize`` reshapes the object from its first list; with a
     *  ``size`` argument in the patch text, a 1-cell multislider is a
     *  ``.slider`` with a longer name, and 8 is the smallest count at which the
     *  object is doing its job.
     *
     *  ### The grammar
     *
     *  One inlet, hot, and everything on it emits — the patcher's rule for a
     *  hot inlet, which ``.rslider`` states and which is why Max's silent forms
     *  are not carried over (see below):
     *
     *   - a **list** of numbers sets the bank from the first ``MAX_CELLS`` of
     *     them and resizes to how many there were (Max's list method plus
     *     ``listresize``);
     *   - an **int or float** sets *every* live cell to that number, which is
     *     Max's int/float method;
     *   - ``set <index> <value>`` writes one cell. Max's own message and issue
     *     #551's cell write are the same message here — a coincidence worth
     *     noting, because on ``.rslider`` they collided and Max's
     *     ``set <min> <max>`` had to be dropped;
     *   - ``fetch <index>`` sends that cell's value out outlet 1, Max's
     *     message and Max's outlet;
     *   - a **bang** re-sends the bank.
     *
     *  **Outlets.** Outlet 0 carries the whole bank as one list — the object's
     *  state, spelled exactly as ``GetGuiValue()`` spells it, so a host reading
     *  the state and a patch receiving it downstream cannot disagree. Outlet 1
     *  carries a single value, and only from ``fetch``; it is sent from the
     *  handler, so it lands before the bank does — the patcher's right-to-left
     *  ordering, without ``Calculate()`` needing to remember a pending index
     *  across a re-entrant send.
     *
     *  **Bounds.** ``minimum`` and ``maximum`` are the range every cell is
     *  clamped into — Max's ``setminmax``, named to match ``.dial``,
     *  ``.incdec`` and ``.rslider``, and taken as an ordered pair so a reversed
     *  argument still bounds against the right two numbers. Clamping happens on
     *  the way out as well as on the way in (``.incdec``'s rule): a live
     *  re-range is visible immediately, and a ``.multislider 4 20 20000`` that
     *  has never been touched reads back as four 20s rather than the four 0s it
     *  was constructed with. Max's ``range`` / ``setmin`` / ``setmax`` messages
     *  are therefore not ported — bounds are creation parameters in this family,
     *  not messages.
     *
     *  ### Deliberately not here
     *
     *  ``setlist`` and ``select``, which set the bank *without output*: a hot
     *  inlet in this patcher calculates after every message it accepts, so
     *  silence is not on offer, and the plain list already does the setting.
     *  This is ``.rslider``'s answer to Max's silent ``set``, for its reason.
     *
     *  ``sum``, ``minimum``, ``maximum``, ``quantiles`` and ``normalize``, which
     *  are reductions and rescalings of a list this object already hands out
     *  whole on outlet 0. ``.zl`` is the patcher's list-processing object and
     *  already carries ``sum``; a second implementation living inside a control
     *  would be a second answer to the same question.
     *
     *  Everything about drawing — ``drawbars``, ``drawlines``, ``setstyle``,
     *  ``orientation``, ``thickness``, ``signed``, ``spacing``, every colour and
     *  shadow attribute, ``candycane``, ``peakreset``, ``scrollclear``,
     *  ``interp`` — and everything about the mouse — ``contdata``, ``echo``,
     *  ``copy`` / ``paste`` / ``pastereplace``. The YSE patcher is headless and
     *  issue #554 names any editor representation a non-goal; how a host chooses
     *  to render the cells it polls is the host's business.
     *
     *  Max's ceiling of 4096 sliders is lowered to ``MAX_CELLS`` = 1024. This
     *  object emits its *whole* state as one list on every message, so the send
     *  buffer has to be reserved for the worst case at construction or the send
     *  path allocates; at 4096 cells that is ~132 KB per object for a control
     *  surface. 1024 is 33 KB, still four times any bank a person drives, and
     *  the dense store for arrays past that is ``.table``.
     *
     *  ### What persists
     *
     *  The parameters, and nothing else — no ``DumpState`` override. The live
     *  count and the values are run-time state, which pObject.h states the rule
     *  for: "a parameter is what the object was *created* with, not what it has
     *  since been told". So ``size`` is the count the object is *built* with,
     *  a list may change the live count afterwards, and a reload brings back the
     *  built shape. ``.rslider``'s two ends are not saved either; the form a
     *  host stores live state in is ``GetGuiValue()``, which is what ``.preset``
     *  is for.
     *
     *  ### Real time
     *
     *  No path allocates, locks or blocks. The list handler reads its keyword in
     *  place and walks its numbers token by token rather than into a
     *  ``MAX_CELLS``-wide stack buffer, because a 1024-number list may well
     *  arrive on the audio thread down a cord and a 4 KB burst on that stack is
     *  not free. ``Calculate()`` renders each cell with ``ExprFormatValue`` into
     *  a stack buffer and appends into a string reserved at construction, filled
     *  immediately before the send rather than kept between sends (``.funnel``'s
     *  re-entrancy lesson). Cells are read and written ``relaxed``: they are
     *  independent scalars and the protocol explicitly permits a torn *frame*.
     *  Only the count carries ordering, and only in the one direction that
     *  matters — filled cells before a longer bank is announced.
     */
    PATCHER_CLASS(gMultiSlider, YSE::OBJ::G_MULTISLIDER)
    _NO_MESSAGES
    _DO_CALCULATE

    _BANG_IN(BangIn)
    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    _HAS_GUI_CELLS

    /**
     *  @brief Most cells the bank can hold — 1024, a quarter of Max's 4096.
     *
     *  The ceiling, not the default: the array is allocated at this size once
     *  and ``size`` only decides how much of it is live. See the class comment
     *  on why it is lower than Max's — the whole bank is emitted as one list, so
     *  the send buffer is reserved for this many cells at construction.
     */
    static constexpr std::size_t MAX_CELLS = 1024;

    /** @brief Cells with no ``size`` creation argument. Not Max's 1 — see the
     *         class comment. */
    static constexpr std::size_t DEFAULT_CELLS = 8;

    /** @brief Fewest cells the bank can be bounded to. A zero-cell bank could
     *         neither be read nor drawn. */
    static constexpr std::size_t MIN_CELLS = 1;

    /** @brief How many cells are live right now. Clamped into
     *         [MIN_CELLS, MAX_CELLS] on the way out, so a hostile ``size``
     *         argument bounds rather than indexes. */
    unsigned int Cells() const;

  private:
    // Characters the whole-bank list can need without reallocating: every cell
    // rendered at full width plus one separator each.
    static constexpr std::size_t LIST_CAPACITY = MAX_CELLS * ((std::size_t)kExprValueTextMax + 1);

    // Clamp `value` into the ordered [minimum, maximum] bounds.
    float Bound(float value) const;

    // Cell `index`, bounded — what every read hands out. Range-checked by the
    // caller against Cells(); this one only ever sees a live index.
    float CellAt(unsigned int index) const;

    // Store into cell `index`, bounded. Range-checked against the *capacity*
    // rather than the count, because the whole-state write fills cells before
    // it publishes the count that makes them live.
    void StoreCell(unsigned int index, float value);

    // Render `value` into `out` (at least kExprValueTextMax bytes) the way the
    // list outlet spells it, so the GUI value and the message carry the same
    // text. Returns how many characters were written.
    static std::size_t Render(float value, char* out);

    // The bank. Allocated whole on the control thread and never resized — the
    // class comment's first section is entirely about why. Built from a prvalue
    // because a vector of atomics can be neither assigned nor resized
    // afterwards; C++17 constructs it in place.
    std::vector<std::atomic<float>> cells = std::vector<std::atomic<float>>(MAX_CELLS);

    // How many cells are live. A registered parameter *and* live state: the
    // creation argument sets it through the scalar plan (issue #234) and a list
    // of a different length changes it afterwards, both being this one store.
    // Held raw as written and clamped in Cells().
    std::atomic<int> size;

    // Written by the parameter system (the scalar plan applies them on the
    // audio thread at the top of a block), read by every path. Plain fields,
    // exactly as .rslider and .incdec hold their bounds.
    float minimum;
    float maximum;

    // The outlet-0 text, built into memory reserved at construction so the send
    // path never allocates.
    std::string listText;
  };
}
}
