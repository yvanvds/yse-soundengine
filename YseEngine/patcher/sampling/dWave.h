#pragma once

#include <memory>
#include <string>
#include "../pObject.h"
#include "../../dsp/sampling/sampleBuffer.hpp"
#include "../../dsp/sampling/waveReader.hpp"
#include "bufferBinding.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~wave``: reads a region of a named sample buffer as one
     *         cycle of a waveform at a phase from 0 to 1, wrapping
     *         ``DSP::waveReader`` (issue #1146).
     *
     *  Ported from Max's ``wave~``: a wavetable made of any sample. The
     *  wrapper only maps pins, messages and parameters onto the DSP class.
     *
     *  - Arguments: ``name start end channels interp interp_bias
     *    interp_tension``. ``name``, ``start``, ``end`` and ``channels`` are
     *    Max's ``buffer-name``, ``start-point``, ``end-point`` (in ms, an end
     *    of 0 being the end of the buffer) and ``number-of-output-channels``
     *    (1 to ``DSP::waveReader::MAX_CHANNELS``, default 1); the rest are
     *    Max's attributes of the same names (``interp`` 0 to 6, linear by
     *    default).
     *  - Inlet 0: the phase signal, or a number used while no signal is
     *    connected (#1036). A list ``<start> [end]`` sets the region in ms.
     *    Messages: ``set [name] [start] [end]``, ``interp <0-6>``,
     *    ``interp_bias <f>``, ``interp_tension <f>``.
     *  - Inlet 1: the start, inlet 2: the end, in ms. A number sets it; a
     *    connected signal sets it once per block, from the block's first
     *    sample, and wins over the number.
     *  - Outlets 0 to ``channels - 1``: buffer channels 1 to ``channels``.
     *
     *  ### Threads
     *
     *  ``CALC()`` reads through a ``DSP::sampleBufferReader`` pinned once
     *  per block, which allocates nothing and takes no lock. Numbers,
     *  lists and messages store atomics that ``CALC()`` hands to the class,
     *  so no message reads the buffer.
     *
     *  ### Rebinding: ``set``
     *
     *  As on ``~index``: the name goes to the patcher's ``fileScheduler``
     *  through a ``bufferBinding`` (#1256), and the top of a later
     *  ``CALC()`` takes the new reader. ``set`` with no name unbinds. As in
     *  Max, the region becomes the start and end after the name, or the
     *  whole buffer without them, at once.
     *
     *  **Saving.** Arguments are saved; messages, lists and numbers are not
     *  (as on ``~table``).
     *
     *  Not ported: ``mouse`` (Max opens an editor window on a double click;
     *  the host draws, the engine has no window).
     */
    PATCHER_CLASS(dWave, YSE::OBJ::D_WAVE)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_PARSE

    _FLOAT_IN(SetPhase)
    _INT_IN(SetPhaseInt)
    _LIST_IN(SetRegion)
    _FLOAT_IN(SetStart)
    _INT_IN(SetStartInt)
    _FLOAT_IN(SetEnd)
    _INT_IN(SetEndInt)

    void SetParent(pObject* newParent) override;

    /** @brief The buffer read, or null without a name. Only while no block
     *         runs (tests). */
    const std::shared_ptr<DSP::sampleBuffer>& Buffer() const {
      return binding.Current().buffer();
    }

    /** @brief A reader let go by a ``set`` and not yet freed. Tests. */
    bool ReleasePending() const {
      return binding.ReleasePending();
    }

    /** @brief The number of outlets. */
    int Channels() const {
      return channels;
    }

    /** @brief The region in ms and the interpolation, as the last
     *         argument, number, list or message set them (a connected
     *         signal is not stored). */
    float CurrentStart() const {
      return startMs.load();
    }
    float CurrentEnd() const {
      return endMs.load();
    }
    int CurrentInterp() const {
      return interpMode.load();
    }
    float CurrentBias() const {
      return bias.load();
    }
    float CurrentTension() const {
      return tension.load();
    }

  private:
    void ShapePorts();

    // Taken and pinned by CALC() only.
    bufferBinding<DSP::sampleBufferReader> binding;
    DSP::waveReader reader;

    std::string bufferName;
    // The outlets follow it. Written by PARM_PARSE only, before any audio
    // thread sees the object (a change of argument rebuilds it).
    int channels = 1;
    aFlt startMs;
    aFlt endMs; // 0 is the end of the buffer
    aInt interpMode;
    aFlt bias;
    aFlt tension;

    // The phase used while no signal is connected.
    aFlt constPhase;
  };

  /**
   *  @brief ``~2d.wave``: reads a region of a named sample buffer cut
   *         into rows, each one cycle of a waveform: a phase from 0 to 1
   *         scans the row, and a second phase picks the row, crossfading
   *         between two. Wraps ``DSP::waveReader`` with rows (issue
   *         #1146).
   *
   *  Ported from Max's ``2d.wave~``. The wrapper only maps pins, messages
   *  and parameters onto the DSP class.
   *
   *  - Arguments: ``name start end channels rows``: Max's
   *    ``buffer-name``, ``start and end-points`` (in ms, an end of 0 being
   *    the end of the buffer), ``number-of-output-channels`` (1 to
   *    ``DSP::waveReader::MAX_CHANNELS``, default 1) and ``rows`` (0 and
   *    1 are one row, the default).
   *  - Inlet 0: the phase signal, or a number used while no signal is
   *    connected (#1036). A list ``<start> [end]`` sets the region in ms.
   *    Messages: ``set [name] [start] [end]``, ``rows <n>``.
   *  - Inlet 1: the row phase, 0 the first row and 1 the last, a signal
   *    or a number used while no signal is connected.
   *  - Inlet 2: the start, inlet 3: the end, in ms, as on ``~wave``.
   *  - Outlets 0 to ``channels - 1``: buffer channels 1 to ``channels``.
   *
   *  Threads, rebinding and saving as on ``~wave``. Not ported: ``mouse``.
   */
  PATCHER_CLASS(dWave2d, YSE::OBJ::D_WAVE2D)
  _DO_MESSAGES
  _DO_CALCULATE

  _PARM_PARSE

  _FLOAT_IN(SetPhase)
  _INT_IN(SetPhaseInt)
  _LIST_IN(SetRegion)
  _FLOAT_IN(SetRow)
  _INT_IN(SetRowInt)
  _FLOAT_IN(SetStart)
  _INT_IN(SetStartInt)
  _FLOAT_IN(SetEnd)
  _INT_IN(SetEndInt)

  void SetParent(pObject* newParent) override;

  /** @brief The buffer read, or null without a name. Only while no block
   *         runs (tests). */
  const std::shared_ptr<DSP::sampleBuffer>& Buffer() const {
    return binding.Current().buffer();
  }

  /** @brief A reader let go by a ``set`` and not yet freed. Tests. */
  bool ReleasePending() const {
    return binding.ReleasePending();
  }

  /** @brief The number of outlets. */
  int Channels() const {
    return channels;
  }

  /** @brief The region in ms and the rows, as the last argument, number,
   *         list or message set them. */
  float CurrentStart() const {
    return startMs.load();
  }
  float CurrentEnd() const {
    return endMs.load();
  }
  int CurrentRows() const {
    return rows.load();
  }

private:
  void ShapePorts();

  // Taken and pinned by CALC() only.
  bufferBinding<DSP::sampleBufferReader> binding;
  DSP::waveReader reader;

  std::string bufferName;
  // The outlets follow it. Written by PARM_PARSE only.
  int channels = 1;
  aFlt startMs;
  aFlt endMs; // 0 is the end of the buffer
  aInt rows;

  // The phases used while no signal is connected.
  aFlt constPhase;
  aFlt constRow;
};

} // namespace PATCHER
} // namespace YSE
