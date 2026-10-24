/*
  ==============================================================================

    click.hpp
    An impulse, or a short stored waveform, on a trigger (issue #1058).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_CLICK_HPP
#define YSE_DSP_GENERATORS_CLICK_HPP

#include <array>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Impulse generator: outputs a single-sample impulse, or a short
     *         stored waveform, each time it is triggered, and silence
     *         otherwise.
     *
     *  An excitation for filters and resonators: a Karplus-Strong string, a
     *  modal or comb resonator, a filter's impulse response. Ported from
     *  Max's ``click~``.
     *
     *  The waveform is a list of up to ``MAX_LENGTH`` (256) samples, set with
     *  ``set``; by default it is the single sample 1, a unit impulse. A
     *  trigger plays it once from its first sample, scaled, and silence
     *  follows. A trigger that arrives while the waveform is still playing
     *  starts it again from the top.
     *
     *  There are two ways to trigger it:
     *
     *  - ``trigger(scale)`` from code: the waveform, times ``scale``, starts
     *    at the first sample of the next block. Block-accurate. Several
     *    triggers before a block are one trigger, with the last scale.
     *  - A trigger signal, ``operator()(buffer&)``: the waveform starts, at
     *    scale 1, at every sample where the signal moves from 0 to anything
     *    that is not 0. Sample-accurate. The signal's last sample is kept
     *    from one block to the next, so a signal that stays non-zero
     *    triggers once; a new generator starts as if the signal had been 0.
     *
     *  Both can be used in the same block: the pending trigger lands on
     *  sample 0, the signal's transitions where they are.
     *
     *  The waveform is in samples, not in seconds, so nothing depends on the
     *  sample rate: a click is the same samples at any rate.
     *
     *  Any finite value is accepted in the waveform (Max documents 0..1 but
     *  does not enforce it); a value that is not a finite number is stored
     *  as 0, and so is a trigger scale that is not one.
     *
     *  Apart from the constructor, call it from one thread at a time, the
     *  audio thread once it renders. Nothing allocates: the waveform is a
     *  fixed array, and the output is sized to ``STANDARD_BUFFERSIZE`` in the
     *  constructor; a block is at most that long (a longer one is processed
     *  up to ``STANDARD_BUFFERSIZE`` samples).
     */
    class API click {
    public:
      /** @brief The longest waveform ``set`` keeps, in samples (Max's 256). */
      static constexpr UInt MAX_LENGTH = 256;

      click();

      /**
       *  @brief Use the first @p count values at @p values as the waveform.
       *
       *  More than ``MAX_LENGTH`` keeps the first ``MAX_LENGTH``. A count of
       *  0 (or a null pointer) goes back to the default unit impulse. A
       *  waveform that is playing goes on with the new values from where it
       *  is, and stops if the new one is shorter.
       */
      click& set(const Flt* values, UInt count);

      /** @brief How many samples the waveform has: 1 to ``MAX_LENGTH``. */
      UInt length() const {
        return size;
      }

      /** @brief Sample @p index of the waveform, or 0 past its end. */
      Flt value(UInt index) const {
        return index < size ? table[index] : 0.f;
      }

      /** @brief Play the waveform, times @p scale, from the start of the
       *         next block. */
      click& trigger(Flt scale = 1.f);

      /** @brief Stop playing and forget a pending trigger and the trigger
       *         signal's last sample. The waveform stays. */
      click& reset();

      /** @brief One block, triggered by ``trigger`` only. */
      buffer& operator()(UInt length = STANDARD_BUFFERSIZE);

      /** @brief One block, also triggered at every 0 to non-zero transition
       *         of @p triggers. */
      buffer& operator()(buffer& triggers);

    private:
      template <class Triggers> buffer& process(Triggers triggers, UInt length);

      std::array<Flt, MAX_LENGTH> table{};
      UInt size = 1;

      // The next waveform sample to play; size or more is idle. Idle is
      // MAX_LENGTH, so a longer waveform set later does not start playing.
      UInt position;
      Flt scale; // the scale of the waveform playing
      Flt pendingScale;
      bool pending;
      Flt last; // the trigger signal's last sample
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_CLICK_HPP
