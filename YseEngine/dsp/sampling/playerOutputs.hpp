/*
  ==============================================================================

    playerOutputs.hpp
    The output channels of a sample buffer player (issues #1140, #1141).

  ==============================================================================
*/

#ifndef YSE_DSP_SAMPLING_PLAYEROUTPUTS_HPP
#define YSE_DSP_SAMPLING_PLAYEROUTPUTS_HPP

#include <vector>
#include "../../headers/constants.hpp"
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    class sampleView;
    struct loopCrossfade;

    /**
     *  @brief The output buffers of a sample player, and the read that
     *         fills one sample of each from a ``sampleView``.
     *
     *  What every buffer player shares (``bufferPlayer`` for ``~play``,
     *  ``groovePlayer`` for ``~groove``): output ``k`` plays buffer channel
     *  ``k``; outputs the buffer has no channel for are silent, and buffer
     *  channels past the last output are mixed in, channel ``k`` into output
     *  ``k % channels()``. A read goes through ``sampleView::cubic``, and,
     *  given a ``loopCrossfade``, is crossfaded across its loop jump with the
     *  weight worked out once for every channel.
     *
     *  .. code-block:: cpp
     *
     *     DSP::playerOutputs outs(2);            // control thread: allocates
     *
     *     // audio thread, once per block
     *     const UInt length = outs.clear(blockLength);
     *     for (UInt i = 0; i < length; ++i)
     *       outs.read(view, i, frameOf(i), nullptr, true);
     *     DSP::buffer& left = outs.out(0);
     *
     *  The buffers are ``STANDARD_BUFFERSIZE`` long once the count is set;
     *  ``clear`` resizes them down for a shorter block without allocating
     *  and cuts a longer one to ``STANDARD_BUFFERSIZE``. ``clear``, ``read``
     *  and ``out`` allocate nothing and take no lock; ``channels`` allocates.
     *  Not thread-safe.
     */
    class API playerOutputs {
    public:
      /** @brief The most outputs. */
      static constexpr UInt MAX_CHANNELS = 64;

      /** @brief @p count outputs (1 to ``MAX_CHANNELS``). Allocates. */
      explicit playerOutputs(UInt count = 1);

      /** @brief The number of outputs, 1 to ``MAX_CHANNELS``. Allocates:
       *         not on the audio thread. */
      void channels(UInt count);
      UInt channels() const {
        return static_cast<UInt>(outputs.size());
      }

      /** @brief Every output @p length samples long (at most
       *         ``STANDARD_BUFFERSIZE``) and silent. Returns that length. */
      UInt clear(UInt length);

      /**
       *  @brief Every buffer channel at @p frame, added into sample @p i of
       *         the outputs.
       *
       *  With @p xfade not null, each channel is crossfaded with its value
       *  at the partner frame near the loop jump, playing @p forward or
       *  backward (``loopCrossfade::weight``). @p i must be below the length
       *  ``clear`` gave.
       */
      void read(const sampleView& view, UInt i, Dbl frame, const loopCrossfade* xfade,
                bool forward);

      /** @brief Output @p channel (from 0); the last output past the
       *         count. */
      buffer& out(UInt channel = 0) {
        return outputs[channel < outputs.size() ? channel : outputs.size() - 1];
      }

    private:
      std::vector<buffer> outputs;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_PLAYEROUTPUTS_HPP
