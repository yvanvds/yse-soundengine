/*
  ==============================================================================

    beatSpan.hpp
    The beats one audio block covers on a domain clock (issue #1278).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_BEATSPAN_HPP
#define YSE_DSP_GENERATORS_BEATSPAN_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief The beat positions of one block's samples on a beat clock, from
     *         the one beat position the clock gives per block.
     *
     *  A domain clock (``system::createClock``, a patch's ``.transport``)
     *  advances once per audio block and publishes one beat position and one
     *  tempo. A ramp that follows it needs a beat for every sample. This
     *  class turns the per-block readings into a straight line across the
     *  block, so ``phasor`` (with a period in beats) and ``clockFollower``
     *  (in its clock mode) can follow a clock's beat without each deciding
     *  for itself where a block starts.
     *
     *  **Which end of the block a reading is.** The engine advances its
     *  domain clocks *before* it renders a block, so the beat a renderer
     *  reads is where the clock stands at the **end** of that block. The
     *  block therefore starts where the reading before it ended: sample ``i``
     *  of a block of ``n`` is at ``start() + (end() - start()) * i / n``, and
     *  the first sample of the next block continues exactly where this one
     *  stopped. Within a block the line is straight; a tempo glide bends the
     *  clock's beat by less than the clock moves in one block, and the line
     *  meets the clock again at every block boundary.
     *
     *  **When the reading before is not the block before.** The first reading,
     *  a reading after ``lose()``, and a reading whose step differs from the
     *  clock's tempo by more than three quarters of a step (blocks went by
     *  unrendered, so the clock moved several blocks' worth) start the block
     *  ``tempo × length / (60 × SAMPLERATE)`` beats before the reading
     *  instead. The ramp then jumps once, to where the clock is, rather than
     *  racing through the beats it missed.
     *
     *  A stopped clock (tempo 0) gives a block that starts and ends on the
     *  same beat; a clock running backwards (negative tempo), one that ends
     *  before it starts.
     *
     *  The sample rate is read from ``SAMPLERATE`` on every reading. Call it
     *  from one thread; it allocates nothing.
     */
    class API beatSpan {
    public:
      /**
       *  @brief This block's reading: @p beat is where the clock stands at the
       *         end of a block of @p length samples, @p bpm its tempo. A
       *         reading that is not a finite number is taken as ``lose()``.
       */
      beatSpan& read(Dbl beat, Flt bpm, UInt length = STANDARD_BUFFERSIZE);

      /** @brief No reading this block (the clock does not exist yet, or is
       *         not bound). ``valid()`` is false until the next ``read``. */
      beatSpan& lose();

      /** @brief Whether the block has a reading. */
      bool valid() const {
        return hasReading;
      }

      /** @brief The beat at the block's first sample. */
      Dbl start() const {
        return startBeat;
      }

      /** @brief The beat at the end of the block (the first sample of the
       *         next). */
      Dbl end() const {
        return endBeat;
      }

      /** @brief The clock's tempo in BPM at the end of the block. */
      Flt tempo() const {
        return bpm;
      }

      /** @brief Samples in the block. */
      UInt length() const {
        return samples;
      }

      /** @brief Beats from one sample to the next in this block. */
      Dbl step() const {
        return samples > 0 ? (endBeat - startBeat) / static_cast<Dbl>(samples) : 0.0;
      }

      /** @brief The beat at sample @p index of the block. */
      Dbl at(UInt index) const {
        return startBeat + step() * static_cast<Dbl>(index);
      }

      /** @brief Whether the clock moved in this block: false while it is
       *         stopped, and while there is no reading. */
      bool moving() const {
        return hasReading && endBeat != startBeat;
      }

    private:
      Dbl startBeat = 0.0;
      Dbl endBeat = 0.0;
      // The step of the reading before, to tell a glide from a gap.
      Dbl lastStep = 0.0;
      Flt bpm = 0.f;
      UInt samples = 0;
      bool hasReading = false;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_BEATSPAN_HPP
