#pragma once

#include <atomic>
#include <memory>
#include <string>
#include "../pObject.h"
#include "../../dsp/sampling/bufferPlayer.hpp"
#include "../../dsp/sampling/sampleBuffer.hpp"
#include "bufferBinding.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~play``: plays a named sample buffer from a start to an end
     *         point, or at the positions a signal gives, wrapping
     *         ``DSP::bufferPlayer`` (issue #1140).
     *
     *  Ported from Max's ``play~``. The wrapper only maps pins, messages
     *  and parameters onto the DSP class.
     *
     *  - Arguments: ``name channels loop loopinterp interptime``. ``name``
     *    and ``channels`` are Max's ``buffer-name`` and
     *    ``number-of-output-channels`` (1 to
     *    ``DSP::bufferPlayer::MAX_CHANNELS``, default 1); the rest are Max's
     *    attributes of the same names, ``interptime`` in milliseconds
     *    (default 50).
     *  - Inlet 0: a connected signal is the position to play, in ms, and
     *    playing follows it. Unconnected, the object plays on its own: a
     *    number other than 0 plays the whole buffer, 0 stops (#1036 lets an
     *    object decide what its numbers mean). The words ``start [start
     *    [end [duration]]]``, ``stop``, ``pause``, ``resume``, ``set``,
     *    ``loop``, ``loopinterp`` and ``interptime`` arrive here too.
     *  - Outlets 0 to ``channels - 1``: the buffer's channels. Outlet
     *    ``channels``: a bang after the block in which playback reached its
     *    end point (not in a loop, not on ``stop``), armed in ``CALC()`` and
     *    sent by the patcher after the block (#1037).
     *
     *  ### Threads
     *
     *  ``CALC()`` reads through a ``DSP::sampleBufferReader`` pinned once per
     *  block. No message touches the player: ``start`` (with its numbers),
     *  ``stop``, ``pause`` and ``resume`` are posted as a request that the
     *  top of the next block applies (the last one sent wins), and the
     *  attributes are atomics the block passes to the class.
     *
     *  ### Rebinding: ``set <name>`` (issue #1256)
     *
     *  Through a ``bufferBinding`` (bufferBinding.h): the message hands the
     *  name to the patcher's ``fileScheduler``, and the top of a later
     *  ``CALC()`` takes the new reader. ``set`` alone plays silence. Outside
     *  a patcher ``set`` does nothing. Playback goes on in the new buffer
     *  from the position it had reached.
     *
     *  **Saving.** The arguments are saved; messages (``set``, ``loop``,
     *  ``loopinterp``, ``interptime``, playback state) are not.
     *
     *  Not ported: ``mouse`` (in Max a double click opens the buffer's
     *  editor window; the host draws, the engine has no window).
     */
    PATCHER_CLASS(dPlay, YSE::OBJ::D_PLAY)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_PARSE

    _FLOAT_IN(SetPlay)
    _INT_IN(SetPlayInt)

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
    const DSP::bufferPlayer& Player() const {
      return player;
    }

    /** @brief The number of signal outlets. */
    int Channels() const {
      return channels;
    }

  private:
    void ShapePorts();
    void Post(int what, float startMs = 0.f, float endMs = -1.f, float durationMs = 0.f);

    // Taken and pinned by CALC() only.
    bufferBinding<DSP::sampleBufferReader> binding;
    DSP::bufferPlayer player;

    // A transport request for the next block (see dPlay.cpp), with the
    // numbers of a start. The numbers are stored before the request.
    std::atomic<int> request{0};
    aFlt requestStart;
    aFlt requestEnd;
    aFlt requestDuration;

    std::string bufferName;
    // The pins follow it. Written by PARM_PARSE only, before any audio
    // thread sees the object (a change of argument rebuilds it).
    int channels = 1;
    aInt loop;
    aInt loopInterp;
    aFlt interpTime; // ms
  };

} // namespace PATCHER
} // namespace YSE
