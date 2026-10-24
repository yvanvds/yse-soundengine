/*
  ==============================================================================

    sampleCounter.hpp
    A signal counting the samples elapsed (issue #1066).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_SAMPLECOUNTER_HPP
#define YSE_DSP_GENERATORS_SAMPLECOUNTER_HPP

#include <cstdint>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A signal that counts samples: it goes up by 1 at every sample,
     *         from a start value, and can loop below a limit. For indexing
     *         into buffers and sample-accurate timers.
     *
     *  Ported from Max's ``count~``.
     *
     *  **Counting.** ``start()`` sets the count to ``minimum()`` and counts
     *  from there: the first sample of the next block is the minimum, the
     *  next one the minimum + 1, and so on. ``stop()`` stops counting and
     *  goes back to the minimum. While stopped, every sample is the minimum
     *  (so changing it while stopped changes the output). A new counter is
     *  stopped, with minimum 0 and no limit.
     *
     *  **Limit.** With a limit other than 0 the counter loops: when the
     *  count would reach the limit it goes back to the minimum instead, so
     *  the limit itself is never output. ``minimum 0, limit 4`` counts
     *  0 1 2 3 0 1 2 3 ... A limit of 0 counts on without end. A minimum at
     *  or above the limit outputs the minimum at every sample, each one a
     *  wrap. Changing the minimum or the limit does not restart the count:
     *  the next wrap goes to the new minimum, and a count already at or past
     *  a new limit wraps at the next sample.
     *
     *  **Carry.** ``carry()`` is a second signal from the same call: how many
     *  times the counter has wrapped since it started, as of each sample.
     *  It goes up at the sample that goes back to the minimum, and is 0
     *  while stopped and after every start.
     *
     *  **Enable signal.** ``operator()(buffer&)`` counts while the enable
     *  signal is not 0: at a sample where it turns non-zero the counter
     *  starts (from the minimum), at one where it turns 0 it stops. The
     *  enable signal is read at every sample, so it decides over ``start``
     *  and ``stop``, which still apply at the top of the block. A sample
     *  that is not a number counts as 0.
     *
     *  The count is kept as a 64-bit integer, so it does not drift; it is
     *  output as a float signal, which holds every integer up to 2^24
     *  (16777216, about 6 minutes at 44.1 kHz) exactly. Counts are samples,
     *  so nothing depends on the sample rate.
     *
     *  Apart from the constructor, call it from one thread at a time, the
     *  audio thread once it renders. Nothing allocates: both outputs are
     *  sized to ``STANDARD_BUFFERSIZE`` in the constructor; a block is at
     *  most that long (a longer one is processed up to
     *  ``STANDARD_BUFFERSIZE`` samples).
     */
    class API sampleCounter {
    public:
      sampleCounter();

      /** @brief The value counting starts from and wraps back to. Does not
       *         restart the count. */
      sampleCounter& minimum(Int value);
      Int minimum() const {
        return low;
      }

      /** @brief The count limit, never output: the counter wraps to the
       *         minimum instead. 0 means no limit. Does not restart the
       *         count. */
      sampleCounter& limit(Int value);
      Int limit() const {
        return high;
      }

      /** @brief Go to the minimum and count from there. */
      sampleCounter& start();

      /** @brief Set the minimum to @p value, go to it and count from there. */
      sampleCounter& start(Int value);

      /** @brief Stop counting and go back to the minimum. */
      sampleCounter& stop();

      /** @brief Whether the counter is counting. */
      bool counting() const {
        return running;
      }

      /** @brief The value the next sample will have. */
      std::int64_t count() const {
        return running ? current : low;
      }

      /** @brief How many times the counter has wrapped since it started. */
      std::int64_t carries() const {
        return wraps;
      }

      /** @brief One block, started and stopped by ``start`` and ``stop``
       *         only. */
      buffer& operator()(UInt length = STANDARD_BUFFERSIZE);

      /** @brief One block, counting while @p enable is not 0. */
      buffer& operator()(buffer& enable);

      /** @brief The carry signal of the last block: the wrap count as of
       *         each sample. */
      buffer& carry() {
        return carryOutput;
      }

    private:
      template <class Enable> buffer& process(Enable enable, UInt length);

      Int low = 0;
      Int high = 0;
      std::int64_t current = 0;
      std::int64_t wraps = 0;
      bool running = false;
      buffer output;
      buffer carryOutput;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_SAMPLECOUNTER_HPP
