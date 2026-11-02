/*
  ==============================================================================

    sampleCapture.hpp
    A fixed-size store of signal samples, for inspection (issue #1128).

  ==============================================================================
*/

#ifndef YSE_DSP_ANALYSIS_SAMPLECAPTURE_HPP
#define YSE_DSP_ANALYSIS_SAMPLECAPTURE_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Stores up to a fixed number of signal samples so they can be
     *         read back and inspected: the most recent ones, or the first
     *         ones since the last ``clear()``.
     *
     *  In ``LAST`` mode (the default) the store keeps the most recent
     *  ``capacity()`` samples: once it is full each new sample replaces the
     *  oldest. In ``FIRST`` mode it keeps the first ``capacity()`` samples
     *  and ignores the rest until ``clear()``. Either way ``sample(0)`` is
     *  the oldest sample held.
     *
     *  A block call stores every sample of the block, or with
     *  ``indices()`` set only the samples at those positions in each
     *  block (up to ``MAX_INDICES`` of them, counted from 0; a position
     *  past the end of a block stores nothing). Samples are stored as they
     *  are, NaN and infinities included.
     *
     *  The store is allocated in the constructor at its capacity, clamped
     *  to 1 to ``MAXIMUM_CAPACITY``; nothing allocates afterwards, so the
     *  block call, ``clear()`` and the readers are real-time safe. No state
     *  depends on the sample rate.
     *
     *  Call it from one thread at a time.
     */
    class API sampleCapture {
    public:
      /** @brief Which samples the store keeps once it is full. */
      enum MODE {
        LAST, ///< the most recent ones: a new sample replaces the oldest
        FIRST, ///< the first ones: later samples are ignored until clear()
      };

      /** @brief The capacity of a default-constructed store (Max's
       *         ``capture~`` default). */
      static constexpr UInt DEFAULT_CAPACITY = 4096;

      /** @brief The largest capacity a store takes. */
      static constexpr UInt MAXIMUM_CAPACITY = 1u << 20;

      /** @brief The most block positions ``indices()`` takes. */
      static constexpr UInt MAX_INDICES = 10;

      /** @brief A store of @p capacity samples, clamped to 1 to
       *         ``MAXIMUM_CAPACITY``. Allocates. */
      explicit sampleCapture(UInt capacity = DEFAULT_CAPACITY);

      /** @brief Sets which samples are kept once the store is full. Keeps
       *         what is stored. */
      sampleCapture& mode(MODE value);

      /** @brief Which samples are kept once the store is full. */
      MODE mode() const;

      /**
       *  @brief Stores only the samples at these positions in each block,
       *         counted from 0, instead of every sample.
       *
       *  Takes the first ``MAX_INDICES`` of the @p count positions at
       *  @p list; they are sorted and duplicates dropped, so the samples
       *  of a block are stored in time order. A @p count of 0 (or a null
       *  @p list) stores every sample again. Does not apply to
       *  ``process()``, which stores the one sample it is given.
       */
      sampleCapture& indices(const UInt* list, UInt count);

      /** @brief How many block positions are set; 0 when every sample is
       *         stored. */
      UInt indexCount() const;

      /** @brief The block position @p i of ``indexCount()``, ascending; 0
       *         past the end. */
      UInt index(UInt i) const;

      /** @brief Stores one sample. */
      void process(Flt in);

      /** @brief Stores the samples of a block: all of them, or those at
       *         the ``indices()``. */
      void operator()(buffer& in);

      /** @brief How many samples are held, at most ``capacity()``. */
      UInt size() const;

      /** @brief How many samples the store holds when full. */
      UInt capacity() const;

      /** @brief True when ``size()`` has reached ``capacity()``. In
       *         ``FIRST`` mode the store then ignores new samples. */
      bool full() const;

      /** @brief Held sample @p i, 0 being the oldest; 0 past ``size()``. */
      Flt sample(UInt i) const;

      /** @brief Copies up to @p count held samples, oldest first, to
       *         @p out. Returns how many were copied: the smaller of
       *         @p count and ``size()``. */
      UInt copy(Flt* out, UInt count) const;

      /** @brief Empties the store. Keeps the capacity, the mode and the
       *         indices. */
      sampleCapture& clear();

    private:
      std::vector<Flt> store;
      UInt held;
      UInt next; // where the next sample goes
      MODE keep;
      UInt positions[MAX_INDICES];
      UInt positionCount;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_ANALYSIS_SAMPLECAPTURE_HPP
