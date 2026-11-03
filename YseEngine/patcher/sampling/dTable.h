#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../../dsp/sampling/sampleBuffer.hpp"
#include "../../dsp/sampling/tableLookup.hpp"
#include "bufferBinding.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~table``: a named sample buffer read as a table of values
     *         at signal rate, wrapping ``DSP::tableLookup`` (issue #1148).
     *
     *  Ported from Max 9's ``table~``. Max's object reads the integer
     *  ``table`` of its name; the YSE ``.table`` keeps its values to itself
     *  (gTable.h says why), and the store the patcher shares by name at
     *  signal rate is the sample buffer registry. So ``~table`` reads the
     *  first channel of a ``DSP::sampleBuffer``, filled by a ``~buffer``, a
     *  ``~poke`` or the host. The wrapper only maps pins, messages and
     *  parameters onto the DSP class.
     *
     *  - Arguments: ``name interp extend inlo inhi outlo outhi inputmode
     *    triggermode range signed``, all but the name Max's attributes of the
     *    same names. ``inmap`` and ``outscale`` take two arguments each;
     *    ``0 0`` is Max's "none".
     *  - Inlet 0: the input, as a signal or a number (the number is used
     *    while no signal is connected, see #1036): an index in ``inputmode``
     *    0, triggers in 1 and 2. ``bang`` draws a random entry, ``goto
     *    <index>``, ``next`` and ``prev`` move the position, and ``interp``,
     *    ``extend``, ``inmap``, ``outscale``, ``inputmode``, ``triggermode``,
     *    ``range`` and ``signed`` set the attributes. ``name <name>`` (Max)
     *    and ``set <name>`` (the sampling family) switch buffers.
     *  - Outlet 0: the values read.
     *
     *  ### Threads
     *
     *  The number, ``bang``, ``goto``, ``next`` and ``prev`` each need the
     *  table, which only ``CALC()`` pins. They are queued as commands in a
     *  fixed queue allocated when the object is built, and applied in order
     *  at the start of the next block (``~zigzag``'s scheme), so no message
     *  allocates or locks. Senders take a non-blocking flag among
     *  themselves; a sender that loses it, or finds the queue full, is
     *  refused and counted in ``Dropped()``. Attribute messages store
     *  atomics that ``CALC()`` passes to the class.
     *
     *  ### Rebinding: ``name`` / ``set``
     *
     *  Through a ``bufferBinding`` (bufferBinding.h, #1256): the message
     *  hands the name to the patcher's ``fileScheduler``, and the top of a
     *  later ``CALC()`` takes the new reader. ``set`` alone unbinds. Outside
     *  a patcher neither does anything.
     *
     *  **Saving.** The arguments are saved; messages are not. A reloaded
     *  ``~table`` reads the buffer of its argument with the attributes of
     *  its arguments.
     *
     *  Not ported: ``size`` (the table's length is the buffer's, set by
     *  ``~buffer`` or the host, not by a reader), ``embed`` (the values live
     *  in the shared buffer, not in the object, so there is nothing of its
     *  own to save), ``mouse`` (an editor window; the host draws),
     *  ``annotation_name``, ``parameter_enable`` and ``parameter_mappable``
     *  (Max's inspector and Live parameters).
     */
    PATCHER_CLASS(dTable, YSE::OBJ::D_TABLE)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_PARSE

    _FLOAT_IN(SetInput)
    _INT_IN(SetInputInt)
    _BANG_IN(Bang)

    void SetParent(pObject* newParent) override;

    /** @brief The buffer read, or null without a name. Only while no block
     *         runs (tests). */
    const std::shared_ptr<DSP::sampleBuffer>& Buffer() const {
      return binding.Current().buffer();
    }

    /** @brief A reader let go by a rebind and not yet freed. Tests. */
    bool ReleasePending() const {
      return binding.ReleasePending();
    }

    /** @brief The DSP class. Only while no block runs (tests). */
    const DSP::tableLookup& Lookup() const {
      return fn;
    }

    /** @brief Commands refused: a sender lost the flag, or the queue was
     *         full. Diagnostics and tests. */
    unsigned int Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

  private:
    struct command {
      int op = 0;
      Flt value = 0.f;
    };

    void Queue(int op, Flt value);
    void Apply(const command& c, const DSP::sampleView& view);

    // Taken and pinned by CALC() only.
    bufferBinding<DSP::sampleBufferReader> binding;
    DSP::tableLookup fn;

    std::vector<command> queue;
    std::atomic<UInt> head{0}; // written by the senders, under `writing`
    std::atomic<UInt> tail{0}; // written by the audio thread
    std::atomic_flag writing = ATOMIC_FLAG_INIT;
    std::atomic<unsigned int> dropped{0};

    std::string bufferName;
    aInt interp;
    aInt extend;
    aFlt inLo;
    aFlt inHi;
    aFlt outLo;
    aFlt outHi;
    aInt inputmode;
    aInt triggermode;
    aInt range;
    aInt isSigned;
  };

} // namespace PATCHER
} // namespace YSE
