#pragma once

#include <atomic>
#include <memory>
#include <string>
#include "../pObject.h"
#include "../../dsp/sampling/groovePlayer.hpp"
#include "../../dsp/sampling/sampleBuffer.hpp"
#include "bufferBinding.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~groove``: plays a named sample buffer at the speed of a
     *         signal, between loop points, with a sync output, wrapping
     *         ``DSP::groovePlayer`` (issue #1141).
     *
     *  Ported from Max's ``groove~``. The wrapper only maps pins, messages
     *  and parameters onto the DSP class.
     *
     *  - Arguments: ``name channels loop loopinterp interptime loopstart
     *    loopend``. ``name`` and ``channels`` are Max's ``buffer-name`` and
     *    ``number-of-outputs`` (1 to ``DSP::groovePlayer::MAX_CHANNELS``,
     *    default 1); the rest are Max's attributes of the same names:
     *    ``loop`` 0 off, 1 on, 2 pendulum, the times in milliseconds
     *    (``interptime`` 50 by default, as ``~play``; Max has no setting
     *    for its length).
     *  - Inlet 0: the speed signal (1 normal, -1 backward, 0 still);
     *    unconnected, the speed is 0. A number is a position in ms to play
     *    from (#1036 lets an object decide what its numbers mean). The
     *    words ``set``, ``setloop <start> <end>``, ``startloop``,
     *    ``endloop``, ``stop``, ``reset``, ``loop``, ``loopinterp``,
     *    ``interptime``, ``loopstart`` and ``loopend`` arrive here too.
     *  - Inlet 1: the loop start, inlet 2: the loop end, in ms (0 is the
     *    buffer's end). A number sets it; a connected signal sets it once
     *    per block, from the block's first sample.
     *  - Outlets 0 to ``channels - 1``: the buffer's channels. Outlet
     *    ``channels``: the sync signal, 0 to 1 over the loop.
     *
     *  ### Threads
     *
     *  ``CALC()`` reads through a ``DSP::sampleBufferReader`` pinned once per
     *  block. No message touches the player: a position, ``startloop``,
     *  ``endloop`` and ``stop`` are posted as a request that the top of the
     *  next block applies (the last one sent wins), and the attributes and
     *  loop points are atomics the block passes to the class.
     *
     *  ### Rebinding: ``set <name>`` (issue #1256)
     *
     *  Through a ``bufferBinding`` (bufferBinding.h): the message hands the
     *  name to the patcher's ``fileScheduler``, and the top of a later
     *  ``CALC()`` takes the new reader. ``set`` alone plays silence. Outside
     *  a patcher ``set`` does nothing. Playback goes on in the new buffer
     *  from the position it had reached.
     *
     *  **Saving.** The arguments are saved; messages (``set``, the loop
     *  points sent as numbers or words, ``loop``, ``loopinterp``,
     *  ``interptime``, playback state) are not.
     *
     *  Not ported: ``mouse`` (Max opens the buffer's editor window on a
     *  double click; the host draws, the engine has no window); ``list``,
     *  ``anything``, ``transport``, ``lock`` and ``phase`` (Max time
     *  formats for the loop points and transport sync, which need the
     *  patcher's clock bridge wired to signal objects, as for ``~phasor``
     *  and ``~record``); and the
     *  Ztx time-stretch and pitch-shift family (``timestretch``,
     *  ``pitchshift``, ``pitchshiftcent``, ``formant``,
     *  ``formantcorrection``, ``pitchcorrection``, ``slurtime``, ``mode``,
     *  ``quality``, ``basictuning``, ``followglobaltempo``,
     *  ``originallength``, ``originaltempo``, ``dictionary``,
     *  ``clearspeedcues``, ``printspeedcues``), out of scope until #1152 /
     *  #1153.
     */
    PATCHER_CLASS(dGroove, YSE::OBJ::D_GROOVE)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_PARSE

    _FLOAT_IN(SetPosition)
    _INT_IN(SetPositionInt)
    _FLOAT_IN(SetStart)
    _INT_IN(SetStartInt)
    _FLOAT_IN(SetEnd)
    _INT_IN(SetEndInt)

    void SetParent(pObject* newParent) override;

    /** @brief The buffer played, or null without a name. Only while no
     *         block runs (tests). */
    const std::shared_ptr<DSP::sampleBuffer>& Buffer() const {
      return binding.Current().buffer();
    }

    /** @brief A reader let go by a ``set`` and not yet freed. Tests. */
    bool ReleasePending() const {
      return binding.ReleasePending();
    }

    /** @brief The DSP class. Only while no block runs (tests). */
    const DSP::groovePlayer& Player() const {
      return player;
    }

    /** @brief The number of sample outlets (the sync outlet follows). */
    int Channels() const {
      return channels;
    }

  private:
    void ShapePorts();
    void Post(int what, float positionMs = 0.f);

    // Taken and pinned by CALC() only.
    bufferBinding<DSP::sampleBufferReader> binding;
    DSP::groovePlayer player;

    // A request for the next block (see dGroove.cpp), with the position of
    // a jump. The position is stored before the request.
    std::atomic<int> request{0};
    aFlt requestPosition;

    std::string bufferName;
    // The pins follow it. Written by PARM_PARSE only, before any audio
    // thread sees the object (a change of argument rebuilds it).
    int channels = 1;
    aInt loop; // 0 off, 1 on, 2 pendulum
    aInt loopInterp;
    aFlt interpTime; // ms
    aFlt loopStart; // ms
    aFlt loopEnd; // ms; 0 is the end of the buffer
  };

} // namespace PATCHER
} // namespace YSE
