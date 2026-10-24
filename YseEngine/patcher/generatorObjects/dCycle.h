#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include "../pObject.h"
#include "../../dsp/generators/cycle.hpp"
#include "../../dsp/sampling/sampleBuffer.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~cycle``: a wavetable oscillator with a phase inlet, wrapping
     *         ``DSP::cycle`` (issue #1054).
     *
     *  Ported from Max's ``cycle~``. The wrapper only maps pins and
     *  parameters onto the DSP class; the oscillator is ``DSP::cycle``.
     *
     *  - Left inlet: frequency in Hz, as a signal or a number (the number is
     *    used while no signal is connected, see #1036). Also takes
     *    ``reset [phase]`` (restart the running phase there, 0 without a
     *    number), ``frequency <hz>``, ``phase <0-1>``,
     *    ``buffer_offset <samples>``, ``buffer_sizeinsamps <samples>``, and
     *    the rebinding messages ``set``, ``setall`` and ``buffer`` (below).
     *  - Right inlet: phase in cycles, added to the running phase, as a signal
     *    or a number (the ``phase`` attribute).
     *  - Parameters: ``frequency`` (argument 1), ``buffer`` (argument 2, the
     *    name of a sample buffer whose first channel is the waveform; none
     *    means the built-in cosine), ``offset`` (argument 3, the first sample
     *    of the table) and ``size`` (argument 4, how many samples: 0 is 512,
     *    -1 the whole buffer from the offset).
     *
     *  The buffer is read as the sampling objects read it (issue #1039): a
     *  ``DSP::sampleBufferReader`` pinned once per block. A buffer that is
     *  reloaded or resized is followed from the next block on, which is Max's
     *  ``buffer_autoupdate`` always on.
     *
     *  ### Rebinding the table (issue #1192)
     *
     *  - ``set [name] [offset]``: read the table from buffer ``name``, from
     *    ``offset`` (0 when not given); the size stays what it is. ``set``
     *    with no name goes back to the built-in cosine.
     *  - ``setall <name>``: the whole of buffer ``name`` is the table
     *    (offset 0, size -1). ``buffer_sizeinsamps`` gives a fixed size back.
     *  - ``buffer <name>``: read buffer ``name``; offset and size stay.
     *
     *  Binding a reader looks the name up in the registry and attaches a
     *  hazard slot, which allocates and locks, and a message may arrive on
     *  the audio thread. So a message only packs the name into a fixed
     *  record and hands it to the patcher's ``fileScheduler`` as a ``TASK``,
     *  as ``~buffer``'s messages do. On the background pool the task binds a
     *  new reader and leaves it in a one-slot mailbox. At the top of a block
     *  ``CALC()`` takes it with one atomic exchange and reads the new buffer
     *  from that block on: the oscillator never stops, and the phase runs on
     *  across the switch. The reader it let go is handed back through the
     *  mailbox and freed by a further task, off the audio thread. A ``set``
     *  offset and a ``setall`` size are applied in the same block as the new
     *  buffer, so the switch never reads the new buffer at the old offset.
     *
     *  The mailbox is shared by the object and its tasks and found by id in
     *  a process-wide table, never through the object, so a task in flight
     *  survives the object's deletion (it then frees what it made).
     *
     *  Limits, the scheduler's: the request is refused when the patcher has
     *  ``fileScheduler::CAPACITY`` requests in flight (counted in
     *  ``FileIO()->Dropped()``), it takes effect only while the patcher
     *  renders, and outside a patcher the three messages do nothing. Of
     *  several rebinds sent before a block, the last wins.
     *
     *  **Saving.** A rebind is not saved: ``DumpJSON`` writes the
     *  ``buffer`` / ``offset`` / ``size`` arguments the object was created
     *  with, and a reloaded patch (or a live ``SetParams`` rebuild) starts
     *  from those again. Change the argument to change what is saved.
     *
     *  Not ported: ``buffer_autoupdate`` (always on) and ``mouse`` (host-side
     *  display).
     *
     *  Control-thread values reach the audio thread through atomics; a reset
     *  is posted as a request and applied at the top of the next block.
     */
    PATCHER_CLASS(dCycle, YSE::OBJ::D_CYCLE)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_PARSE

    _FLOAT_IN(SetFrequency)
    _INT_IN(SetFrequencyInt)
    _FLOAT_IN(SetPhase)
    _INT_IN(SetPhaseInt)

    void SetParent(pObject* newParent) override;

    /** @brief Whether the table is read from a buffer: a buffer argument,
     *         or a rebind that has taken effect. Control thread, tests. */
    bool UsesBuffer() const {
      return usesBuffer.load(std::memory_order_acquire);
    }

    /** @brief The buffer the table is read from, or null. Only while no
     *         block runs (tests). */
    const std::shared_ptr<DSP::sampleBuffer>& Buffer() const;

    /** @brief A reader let go by a rebind and not yet freed. Diagnostics and
     *         tests. */
    bool ReleasePending() const;

    // A reader plus where the table starts, and the handoff between the
    // background task and CALC(). Defined in dCycle.cpp.
    struct table;
    struct mailbox;

  private:
    struct rebindTask;
    // Pack `task` and hand it to the scheduler. Any thread, no allocation.
    bool Submit(rebindTask& task);
    // CALC(): take a waiting table, and ask for the old one to be freed.
    void TakeRebind();

    // The fileTaskFunction: background pool, never touches the object.
    static bool RunTask(char* args, std::size_t argBytes);

    DSP::cycle oscillator;
    std::shared_ptr<mailbox> box; // made at construction, never replaced

    aFlt frequency;
    std::string bufferName;
    std::atomic<int> offset{0};
    std::atomic<int> size{0};

    aFlt phase;

    // A reset waiting for the audio thread, exchanged in CALC() so it is
    // applied once.
    aFlt pendingReset;
    std::atomic<bool> resetPending{false};

    // Mirrors the current table's `active`: set in PARM_PARSE, then by
    // CALC() when a rebind takes effect.
    std::atomic<bool> usesBuffer{false};
  };

} // namespace PATCHER
} // namespace YSE
