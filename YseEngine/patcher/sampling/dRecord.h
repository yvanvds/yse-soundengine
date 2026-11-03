#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include "../pObject.h"
#include "../../dsp/sampling/bufferRecorder.hpp"
#include "../../dsp/sampling/sampleBuffer.hpp"
#include "bufferBinding.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~record``: records signals into a named sample buffer,
     *         wrapping ``DSP::bufferRecorder`` (issue #1143).
     *
     *  Ported from Max's ``record~``. The wrapper only maps pins, messages
     *  and parameters onto the DSP class.
     *
     *  - Arguments: ``name channels append loop loopstart loopend``. ``name``
     *    and ``channels`` are Max's ``buffer-name`` and ``input-channels``
     *    (1 to ``DSP::bufferRecorder::MAX_CHANNELS``, default 1); the rest
     *    are Max's attributes of the same names, the points in milliseconds.
     *  - Inlets 0 to ``channels - 1``: the signals, input ``i`` into buffer
     *    channel ``i + 1``. Unconnected, a signal inlet records silence: the
     *    number in inlet 0 is the record switch, not a value (#1036 lets an
     *    object decide what its numbers mean). In inlet 0 a non-zero number
     *    starts recording and 0 stops it, and the words ``set``, ``reset``,
     *    ``append``, ``loop``, ``loopstart`` and ``loopend`` arrive.
     *  - Inlet ``channels``: the start point in ms. Inlet ``channels + 1``:
     *    the end point in ms, 0 for the end of the buffer.
     *  - Outlet 0: the sync signal, 0 to 1 over the loop points while
     *    recording, 0 while not.
     *
     *  ### Threads
     *
     *  ``CALC()`` writes through a ``DSP::sampleBufferWriter`` pinned once
     *  per block: in place, without a lock, a copy or an allocation, so
     *  every reader of the buffer hears the recording at once. No message
     *  touches the buffer. A start or stop is posted as a request that the
     *  top of the next block applies (the last one sent wins); the
     *  attributes are atomics the block passes to the class.
     *
     *  ### Rebinding: ``set <name>`` (issue #1256)
     *
     *  Through a ``bufferBinding`` (bufferBinding.h): the message hands the
     *  name to the patcher's ``fileScheduler``, and the top of a later
     *  ``CALC()`` takes the new writer. ``set`` alone stops writing. Outside
     *  a patcher ``set`` does nothing. A recording goes on into the new
     *  buffer from the frame it had reached.
     *
     *  **Saving.** The arguments are saved; messages (``set``, the start
     *  and end points, ``append``, ``loop``, ``reset``) are not.
     *
     *  Not ported: ``mouse`` (Max opens an editor window on a double click;
     *  the host draws, the engine has no window), ``transport`` and the Max
     *  time formats that ``list`` and ``anything`` give the loop points
     *  (bars and beats, note values), which need the patcher's clock bridge
     *  wired to signal objects, as for ``~phasor``. The points take
     *  milliseconds.
     */
    PATCHER_CLASS(dRecord, YSE::OBJ::D_RECORD)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_PARSE

    _FLOAT_IN(SetRecord)
    _INT_IN(SetRecordInt)
    _FLOAT_IN(SetStart)
    _INT_IN(SetStartInt)
    _FLOAT_IN(SetEnd)
    _INT_IN(SetEndInt)

    void SetParent(pObject* newParent) override;

    /** @brief The buffer written, or null without a name. Only while no
     *         block runs (tests). */
    const std::shared_ptr<DSP::sampleBuffer>& Buffer() const {
      return binding.Current().buffer();
    }

    /** @brief A writer let go by a ``set`` and not yet freed. Tests. */
    bool ReleasePending() const {
      return binding.ReleasePending();
    }

    /** @brief The DSP class. Only while no block runs (tests). */
    const DSP::bufferRecorder& Recorder() const {
      return recorder;
    }

    /** @brief The number of signal inlets. */
    int Channels() const {
      return channels;
    }

  private:
    void ShapePorts();
    void PostRecord(float value);

    // Taken and pinned by CALC() only.
    bufferBinding<DSP::sampleBufferWriter> binding;
    DSP::bufferRecorder recorder;
    std::array<DSP::buffer*, DSP::bufferRecorder::MAX_CHANNELS> blocks{};

    // A start or stop for the next block: 0 none, 1 start, 2 stop.
    std::atomic<int> request{0};

    std::string bufferName;
    // The pins follow it. Written by PARM_PARSE only, before any audio
    // thread sees the object (a change of argument rebuilds it).
    int channels = 1;
    aInt append;
    aInt loop;
    aFlt loopStart; // ms
    aFlt loopEnd; // ms; 0 is the end of the buffer
  };

} // namespace PATCHER
} // namespace YSE
