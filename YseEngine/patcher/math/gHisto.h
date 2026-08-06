#pragma once
#include <array>
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Histogram of the numbers received — ``.histo`` (issue #458).
     *
     *  Keeps one running count per value and reports it as the list
     *  ``<value> <count>``. Where ``.anal`` (#457) counts *successions* — how
     *  often one number followed another — this counts *occurrences*: the
     *  zeroth-order statistic of the stream against ``.anal``'s first-order
     *  one. A patch uses it to notice what it has already played, and to weight
     *  what it plays next away from (or towards) that.
     *
     *  ### Shape
     *
     *  Two inlets, two outlets:
     *
     *  - inlet 0 — the stream. A number here is counted.
     *  - inlet 1 — a query. A number here is *not* counted; its bin is only
     *    read. Max's right inlet, verbatim.
     *  - outlet 0 (list) — ``<value> <count>``.
     *  - outlet 1 (bang) — the number was outside the histogram's range and so
     *    was neither counted nor readable. See *Range*.
     *
     *  Max splits the report over two outlets, the number on the left and its
     *  count on the right, and relies on right-to-left outlet order to keep the
     *  two together. One list outlet says the same thing in one message, which
     *  is both the patcher's existing convention (``.anal`` reports its triple
     *  the same way) and the reason a downstream object cannot observe the pair
     *  half-updated.
     *
     *  ### Messages on inlet 0
     *
     *  - int / float — the next number of the stream: its bin is incremented and
     *    the new count reported. A float is truncated, as everywhere else in
     *    this family.
     *  - bang — re-reports the most recently *counted* number and its current
     *    count, without counting anything. Before any number has been counted
     *    that is ``0 0``, which is Max's "outputs 0 if no number has been
     *    received previously". A query on inlet 1 does not change what a bang
     *    reports: it does not touch the histogram, so it does not touch the
     *    object's idea of where the stream is.
     *  - ``clear`` — zeroes every bin. The most recently counted number is kept,
     *    so a bang straight after a ``clear`` honestly reports ``<that number>
     *    0`` rather than pretending nothing was ever received.
     *  - ``dump`` — sends one ``<value> <count>`` list out outlet 0 for every
     *    **non-empty** bin, in ascending value order. Not a Max message; it is
     *    here because the headless patcher has no console, and it skips the
     *    empty bins because a full sweep of a 128-bin histogram is 128 messages
     *    of which the interesting ones are usually a handful.
     *
     *  ### Range
     *
     *  The creation parameter ``size`` names how many bins there are, so the
     *  values the object accepts are ``[0, size - 1]`` — Max's default of 128
     *  meaning "a MIDI note number arrives unchanged", exactly as it does there.
     *
     *  A number outside that range is **ignored**: not counted, not reported on
     *  outlet 0, and outlet 1 bangs instead. This is Max's behaviour ("numbers
     *  outside this range are disregarded") and it is also the only defensible
     *  one here, for the same reason ``.anal`` drops rather than evicts. The
     *  alternatives are worse in specific ways: clipping into range (which is
     *  what ``.anal`` does to its *inputs*) would pile every stray value onto
     *  the boundary bin and report a mode the stream never had, and growing the
     *  array would allocate on a message path. So the object refuses the number
     *  and says so on an outlet a patch can watch — which matters more here than
     *  in Max, where a stray number is at least visible in the editor.
     *
     *  ``size`` is clamped into ``[1, CAPACITY]``. ``CAPACITY`` is 4096, the
     *  same ceiling ``.urn`` (#454) uses and for the same reason: it lets the
     *  bins be a fixed member array.
     *
     *  ### Overflow
     *
     *  A bin saturates at ``MAX_COUNT`` and stays there rather than wrapping.
     *  A wrap would turn the most-seen value into the least-seen one, which is
     *  the single worst answer a histogram can give; a saturated bin is merely
     *  imprecise, and only after a billion occurrences of one value. The running
     *  total across all bins saturates the same way.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing at all — the object is driven by its inlets,
     *  not by the DSP tick — and nothing anywhere allocates a container, takes a
     *  lock, or blocks. Counting or querying a number is one bounds check and
     *  one array slot; ``clear`` is a bounded sweep of the fixed array, and
     *  ``dump`` a bounded sweep of ``size`` bins. There is no search and no
     *  loop whose length depends on the data.
     *
     *  The one honest caveat is shared with every list-sending object in the
     *  patcher: an outlet list is a ``std::string``, so emitting one may
     *  allocate. ``pListArgs.h``'s ``FormatIntList`` builds it in a single pass
     *  over a stack buffer, which keeps a two-small-number report inside
     *  small-string optimisation, but the message path is a control-thread path
     *  by construction and is documented as such rather than pretended
     *  otherwise.
     *
     *  ### Threading
     *
     *  The bins are atomics and the object expects a single writer, like
     *  ``TransitionTable``. Reads (``CountOf``, ``Total``, a query on inlet 1)
     *  are wait-free and may run on the audio thread; every word read is a whole
     *  ``int``, so the worst a concurrent reader sees is the histogram as it
     *  stood a moment ago.
     */
    PATCHER_CLASS(gHisto, YSE::OBJ::G_HISTO)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(Bang)
    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

    _PARM_PARSE

    _HAS_GUI

    /** @brief Largest honoured size, and the fixed number of bins. */
    static constexpr int CAPACITY = 4096;
    /** @brief Ceiling a single bin (and the total) saturates at. */
    static constexpr int MAX_COUNT = 1 << 30;

    /** @brief Number of bins; inputs outside ``[0, Size() - 1]`` are refused. */
    int Size() const {
      return size.load();
    }

    /** @brief How often @p value has been counted. 0 when out of range. */
    int CountOf(int value) const;

    /** @brief How many numbers have been counted in total. */
    int Total() const {
      return total.load();
    }

    /**
     *  @brief Saturating add — how a bin and the running total climb.
     *
     *  Public because it *is* the overflow behaviour: reaching ``MAX_COUNT``
     *  through the inlet would take a billion messages, so the ceiling is
     *  asserted here instead of by driving the object to it.
     */
    static int AddSaturating(int current, int delta);

  private:
    // Sends `<value> <count>` out outlet 0. Control thread: formats a string.
    void Report(int value, int count, YSE::THREAD thread);
    // Sends every non-empty bin out outlet 0, ascending.
    void Dump(YSE::THREAD thread);
    // Zeroes every bin and the total. Bounded by CAPACITY, no allocation.
    void ClearBins();
    // True when `value` names a bin of this histogram.
    bool InRange(int value) const {
      return value >= 0 && value < size.load();
    }
    // Creation parameter: how many bins. Max's default is 128.
    aInt size{128};
    // Most recently counted number — what a bang re-reports.
    aInt last{0};
    // Sum of every bin, saturating like a bin does.
    aInt total{0};

    // One count per value. Only bins below `size` are ever reachable.
    std::array<aInt, CAPACITY> bins{};
  };
}
}
