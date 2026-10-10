#pragma once
#include "../pObject.h"
#include "gTransitionTable.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Transition histogram over an input stream — ``.anal`` (issue
     *         #457).
     *
     *  The learning half of the Markov pair. Every number that arrives is
     *  counted as a *pair* with the number before it, and the running count of
     *  that pair is emitted as the list ``<previous> <current> <count>``. That
     *  is precisely the list ``.prob`` (#456) stores a transition from, so a
     *  patch cord from this outlet to a ``.prob`` inlet is the whole
     *  integration: play a phrase into ``.anal``, and the ``.prob`` on the
     *  other end of the cord has learned the phrase's first-order style and
     *  will improvise in it.
     *
     *  The two agree because they share ``TransitionTable``: ``.anal``
     *  accumulates with ``Add`` and reports the total, ``.prob`` installs that
     *  total with ``Set``. Since ``Set`` *replaces*, re-sending the same pair as
     *  its count grows leaves the two tables holding the same numbers rather
     *  than the receiver drifting to double-counted weights.
     *
     *  ### Shape
     *
     *  One inlet. Two outlets:
     *
     *  - 0 (list) — ``<previous> <current> <count>``, one per number received
     *    (and one per stored pair in answer to ``dump``).
     *  - 1 (bang) — the pair could not be counted because the table is full.
     *    See *Overflow*.
     *
     *  ### Messages on inlet 0
     *
     *  - int / float — the next number of the stream. A float is truncated, as
     *    everywhere else in this family. Values are clipped into
     *    ``[0, limit]``, Max's behaviour and the reason ``limit`` defaults to
     *    128: a MIDI note number arrives unchanged.
     *  - ``clear`` — forgets every counted pair but *keeps* the last number
     *    received, so it still serves as the next pair's predecessor. Max is
     *    explicit about that asymmetry, and it is what lets a patch restart the
     *    statistics mid-phrase without inventing a seam.
     *  - ``reset`` — the mirror image: forgets the last number received, keeping
     *    the counts. The next number arrives with no predecessor and so is
     *    stored silently, exactly as the very first number is.
     *  - ``dump`` — sends every stored pair out outlet 0, in insertion order.
     *    Not a Max message; Max's ``anal`` reports only as it learns. It is here
     *    because the headless patcher has no console and because a learned table
     *    is worth replaying into a ``.prob`` that was created after the fact —
     *    the entries have the same shape as the live reports, so the same patch
     *    cord carries them.
     *
     *  The first number after construction, after ``reset``, or after a
     *  parameter reload has no predecessor: it is remembered and nothing is
     *  emitted. Max: "the first time a number is received, there has been no
     *  previous number, so nothing happens."
     *
     *  ### Overflow
     *
     *  The table holds ``TransitionTable::CAPACITY`` (1024) *distinct* pairs,
     *  and a stream over the default range can name far more than that
     *  (129 x 129). Once the table is full a pair that is already in it keeps
     *  counting normally; a pair that is not is **dropped** — outlet 0 says
     *  nothing about it and outlet 1 bangs.
     *
     *  Dropping is the only defensible choice of the three available. Growing
     *  the table means allocating on a message path. Evicting some other pair
     *  means the object silently reports a distribution that no input ever had,
     *  and the least-recently-seen entry is exactly the rare transition a
     *  histogram exists to notice. Reporting a count of zero would be worse
     *  still, because a downstream ``.prob`` would store the zero and mark a
     *  transition that does occur as one that never does. So the object stops
     *  learning new pairs and says so, on an outlet a patch can watch.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing at all — the object is driven by its inlet,
     *  not by the DSP tick — and nothing anywhere allocates a container, takes a
     *  lock, or blocks. Counting a pair is two bounded scans of the stored
     *  entries (one to accumulate, one to read the new total back), each O(n) in
     *  the entries actually stored and hard-bounded by the capacity.
     *
     *  The one honest caveat is shared with every list-sending object in the
     *  patcher: an outlet list is a ``std::string``, so emitting one may
     *  allocate. ``pListArgs.h``'s ``FormatIntList`` builds it in a single pass
     *  over a stack buffer, which keeps the usual three-small-number entry
     *  inside small-string optimisation, but the message path is a control-thread
     *  path by construction and is documented as such rather than pretended
     *  otherwise.
     *
     *  ### Threading
     *
     *  The last-number slot is read and replaced with one ``exchange``, so two
     *  numbers arriving on two threads take two *different* predecessors rather
     *  than both pairing against the same stale one. The table's own publication
     *  rules do the rest; see TransitionTable.
     */
    PATCHER_CLASS(gAnal, YSE::OBJ::G_ANAL)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

    _PARM_PARSE

    _HAS_GUI

    /** @brief Largest input limit Max documents, and this object's ceiling. */
    static constexpr int MAX_LIMIT = 16384;

    /** @brief Distinct pairs counted so far — how full the table is. */
    int Entries() const {
      return table.Size();
    }

    /** @brief How often @p from was immediately followed by @p to. */
    int CountOf(int from, int to) const {
      return table.WeightOf(from, to);
    }

    /** @brief Value inputs are clipped to; the creation parameter, clamped. */
    int Limit() const {
      return limit.load();
    }

  private:
    // Sends every stored pair out outlet 0. Control thread: formats strings.
    void Dump(YSE::THREAD thread);
    // Clips an incoming number into [0, limit], as Max's anal does.
    int ClipInput(int value) const;

    // `previous` holds the last number received, which after clipping is never
    // negative — so one sentinel in the same word says "nothing yet" and the
    // read-and-replace stays a single atomic operation.
    static constexpr int NO_PREVIOUS = -1;

    // Creation parameter: the top of the input range. Max's default is 128.
    aInt limit{128};
    // Last number received, or NO_PREVIOUS.
    aInt previous{NO_PREVIOUS};

    TransitionTable table;
  };
}
}
