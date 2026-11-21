#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../sampling/bufferBinding.h"
#include "../../dsp/analysis/plotData.hpp"
#include "../../dsp/sampling/sampleBuffer.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~plot``: the data and axes of a 2-D plot, wrapping
     *         ``DSP::plotData`` (issue #1171).
     *
     *  Ported from Max's ``plot~``. The engine holds the data, the host
     *  draws: the GUI value is the newest frame of every subplot, ``3 +
     *  plots * (9 + points)`` cells. It starts ``<plots> <points>
     *  <serial>``; then each subplot gives ``<domain start> <domain end>
     *  <domain scale> <range start> <range end> <range scale> <x origin> <y
     *  origin> <filter>`` (a scale is 0 linear, 1 log; the filter 0 none, 1
     *  atodb) and its ``points`` values, already filtered. ``serial``
     *  counts the frames, so a host sees when the data changed. It is not
     *  settable. The bulk read and cell 0 take the newest frame; the count
     *  and cells 1 on read the one taken.
     *
     *  - One inlet per subplot (``numplots``). A number sets the subplot's
     *    first value, a list its values from the first on. A signal cord
     *    makes the subplot show frames of the signal. ``definedomain <start>
     *    <end> [linear|log]``, ``definerange <start> <end> [linear|log]``,
     *    ``definexorigin <x>``, ``defineyorigin <y>``, ``definefilter
     *    <none|atodb>`` set its axes; ``refer <buffer> [<channel>]
     *    [<start ms>] [<end ms>]`` shows a region of a ``~buffer`` (channel
     *    from 1; ``refer`` alone stops). A cord wins over a buffer, and both
     *    over lists, which still write the values the next frame replaces.
     *    ``numpoints <n>``, ``audioframesize <n>`` and ``audioframerate
     *    <fps>`` to any inlet set those parameters.
     *  - No outlets.
     *  - Parameters: ``numpoints`` (argument 1, default 4, 1 to 4096),
     *    ``numplots`` (argument 2, default 1, 1 to 16; rebuilds the
     *    inlets), ``audioframesize`` (argument 3, default 0: the points),
     *    ``audioframerate`` (argument 4, default 15). Later messages, the
     *    data, the axes and a ``refer`` are not saved.
     *
     *  Not mapped: ``definecolor``, ``defineline``, ``definenumber``,
     *  ``definepoint``, ``definethickness``, the grids and labels
     *  (``definexgrid``, ``defineygrid``, ``definexlabels``,
     *  ``defineylabels``, ``domainlabel``, ``rangelabel``), the fonts,
     *  colours, ``margins``, ``fillhorizontalspace`` and the thinning
     *  (``thinmode``, ``thinthresh``, ``thinto``, which depend on the
     *  pixels) are drawing, left to the host; ``edit`` opens an editor
     *  window and ``mouse`` reports clicks, both the host's; ``dictionary``
     *  and ``getdictionary`` use a dictionary layout Max does not document,
     *  so no patch could carry one, and every setting has its message.
     *
     *  **Threads.** A message may come from any thread, one at a time. It
     *  is parsed in place and queued, allocation-free, in a ring the
     *  constructor allocates, which ``CALC()`` drains at the top of every
     *  block in order; two senders at once, or a ring too full for a list,
     *  drop the message and count it (``Dropped()``). Data and settings
     *  therefore show from the next block. ``refer`` binds its buffer in the
     *  background (``bufferBinding``, #1256). Frames reach the host through
     *  ``DSP::plotData``'s triple buffer; host reads take a mutex only they
     *  share, never the audio thread. The wrapper contains no sample math.
     */
    PATCHER_CLASS(dPlot, YSE::OBJ::D_PLOT)
    _NO_MESSAGES
    _DO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)
    _LIST_IN(SetList)

    // Read only: no _HAS_GUI_CELLS, which promises the write half.
    std::string GetGuiValue() override;
    unsigned int GetGuiValueCount() const override;
    std::string GetGuiValueAt(unsigned int index) override;

    void SetParent(pObject* newParent) override;

    /** @brief Messages dropped (two senders at once, or a full ring).
     *         Diagnostics and tests. */
    unsigned int Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief The plot. Only while no block runs (tests). */
    const DSP::plotData& Data() const {
      return *fn;
    }

  private:
    enum OP {
      OP_SET,
      OP_DOMAIN,
      OP_RANGE,
      OP_XORIGIN,
      OP_YORIGIN,
      OP_FILTER,
      OP_REGION,
    };

    // Rebuild the inlets, the plot, the bindings and the ring for
    // `numplots`. Control thread, before the object is wired or published:
    // the constructor and the two parameter callbacks (a live re-parse
    // rebuilds the object, #234).
    void ShapePorts();

    // Message thread: queue a command of `n` arguments, or the numbers of
    // `text` from `cursor` on as a SET.
    void Post(OP op, unsigned int plot, const Flt* args, unsigned int n);
    void PostNumbers(unsigned int plot, const std::string& text, std::size_t cursor);
    // Audio thread: apply every command queued.
    void Drain();

    std::unique_ptr<DSP::plotData> fn;
    std::vector<std::unique_ptr<bufferBinding<DSP::sampleBufferReader>>> bindings;

    aFlt numpoints;
    aFlt numplots;
    aFlt audioframesize;
    aFlt audioframerate;

    // The command ring: one sender at a time (under `sending`), the audio
    // thread drains. Positions only grow; an entry is <op> <plot> <n> and n
    // arguments.
    std::vector<Flt> ring;
    std::vector<Flt> scratch; // Drain()'s, the arguments of one command
    std::atomic<std::size_t> head{0};
    std::atomic<std::size_t> tail{0};
    std::atomic_flag sending = ATOMIC_FLAG_INIT;
    std::atomic<unsigned int> dropped{0};

    // Host readers only: the triple buffer has one reader at a time.
    mutable std::mutex reading;
  };

} // namespace PATCHER
} // namespace YSE
