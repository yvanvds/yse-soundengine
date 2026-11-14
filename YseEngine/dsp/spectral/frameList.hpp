/*
  ==============================================================================

    frameList.hpp
    Frames from stored values and stored values from frames: what ~frame and
    ~framesnap turn lists into frames and frames into lists with (issue
    #1163).

  ==============================================================================
*/

#ifndef YSE_DSP_SPECTRAL_FRAMELIST_HPP
#define YSE_DSP_SPECTRAL_FRAMELIST_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "stft.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Puts out a stored frame, bin by bin, frame after frame: a
     *         spectrum drawn from data, or a frozen one.
     *
     *  Ported from MSP's ``frame~``. The values are set with ``set()``;
     *  every frame then puts out bin ``k`` as value ``k``. Bins no value was
     *  set for (past the last one set, or skipped by an offset) put out the
     *  ``fill()`` value, 0 by default.
     *
     *  ``set(values, count, offset)`` sets bins ``offset`` .. ``offset +
     *  count - 1``. The bins before ``offset`` keep their values and the
     *  bins from ``offset + count`` on are no longer set, so values handed
     *  over in consecutive pieces, each starting where the one before ended,
     *  build up one frame, and a ``set`` from 0 replaces the whole frame.
     *
     *  In a full-spectrum frame (``stftFrame::fullSpectrum``) the bins above
     *  Nyquist, ``k > size / 2``, put out 0; with ``mirror()`` on they put
     *  out bin ``size - k`` instead, the mirror image the spectrum of a real
     *  signal has. A half-spectrum frame has no bin above Nyquist.
     *
     *  The output is handed over as runs of bins (``stftFrame``:
     *  ``firstBin``, ``count``): a whole ``DSP::stft`` callback frame, or a
     *  frame cut into runs (the passes of a ``~pfft``). The block overload
     *  treats a block as a whole frame of its length, sample ``i`` being
     *  value ``i``, as MSP does outside a ``pfft~``.
     *
     *  - Values at or past ``capacity()`` are not kept: those bins put out
     *    the fill value.
     *  - A value that is not a finite number is kept as 0.
     *
     *  The values are allocated in the constructor, ``capacity()`` of them,
     *  and never again: setting, copying and processing allocate nothing,
     *  take no lock and nothing depends on the sample rate. A block call
     *  puts out at most ``STANDARD_BUFFERSIZE`` samples. One thread at a
     *  time.
     */
    class API frameSource {
    public:
      /** @brief The capacity of a default-constructed object: frames of up
       *         to 4096 bins (a half spectrum of an 8192-point FFT). */
      static constexpr UInt DEFAULT_CAPACITY = 4096;
      /** @brief The largest capacity: a full spectrum of ``~pfft``'s
       *         largest FFT. */
      static constexpr UInt MAXIMUM_CAPACITY = 1u << 16;

      /** @brief Room for @p capacity values (clamped to 1 to
       *         ``MAXIMUM_CAPACITY``), none set. Allocates. */
      explicit frameSource(UInt capacity = DEFAULT_CAPACITY);

      /** @brief How many values the object keeps. */
      UInt capacity() const {
        return static_cast<UInt>(values.size());
      }

      /** @brief Sets bins @p offset .. ``offset + count - 1`` to @p in
       *         (those past the capacity are dropped). The bins before
       *         @p offset keep their values; the bins from ``offset +
       *         count`` on are no longer set. */
      frameSource& set(const Flt* in, UInt count, UInt offset = 0);

      /** @brief No bin is set: the whole frame is the fill value. */
      frameSource& clear();

      /** @brief One past the last bin set: the bins from here on put out
       *         the fill value. */
      UInt length() const {
        return specified;
      }

      /** @brief What the bins no value was set for put out (a value that
       *         is not finite counts as 0). MSP's ``fill``. */
      frameSource& fill(Flt value);

      /** @brief The fill value. */
      Flt fill() const {
        return filler;
      }

      /** @brief In a full-spectrum frame, puts out bin ``size - k`` for the
       *         bins ``k`` above Nyquist instead of 0. MSP's ``mirror``. */
      frameSource& mirror(bool on) {
        mirroring = on;
        return *this;
      }

      /** @brief Whether the bins above Nyquist mirror the ones below. */
      bool mirror() const {
        return mirroring;
      }

      /** @brief What bin @p bin puts out in a half-spectrum frame or a
       *         block: its value, or the fill value. */
      Flt value(UInt bin) const;

      /** @brief Takes over @p other's values (as many as this capacity
       *         holds), fill and mirror. Allocates nothing. */
      frameSource& copy(const frameSource& other);

      /** @brief One run of bins: ``frame.firstBin`` .. ``+ frame.count -
       *         1`` into @p out (``frame.count`` values). ``frame.real``
       *         and ``frame.imaginary`` are not used. */
      void operator()(const stftFrame& frame, Flt* out);

      /** @brief One run of bins into the class's own output, ``frame.count``
       *         samples long (at most ``STANDARD_BUFFERSIZE``). */
      buffer& operator()(const stftFrame& frame);

      /** @brief One block as a whole frame of @p length samples (at most
       *         ``STANDARD_BUFFERSIZE``): sample ``i`` is ``value(i)``. */
      buffer& operator()(UInt length = STANDARD_BUFFERSIZE);

    private:
      buffer output;
      std::vector<Flt> values; // NaN: a bin skipped by an offset
      UInt specified = 0;
      Flt filler = 0.f;
      bool mirroring = false;
    };

    /**
     *  @brief Keeps the last whole frame of a stream of frames, to be read
     *         as values: a spectrum captured as data.
     *
     *  Ported from MSP's ``framesnap~``. The input is a stream of frames,
     *  handed over as runs of bins (``stftFrame``: ``firstBin``, ``count``,
     *  the frame's ``bins`` and its ``index``), with the values in a
     *  separate array (a frame's real parts, or its magnitudes). A run
     *  begins a new frame when its ``index`` differs from the run before or
     *  its ``firstBin`` is not past the one before. A frame is complete when
     *  its runs have covered bins 0 to ``bins - 1`` in order; it then
     *  becomes the one ``values()`` reads, and ``frames()`` counts it. A
     *  frame whose runs leave a gap is dropped, and the frame before stays.
     *
     *  The same object serves a whole frame (a ``DSP::stft`` callback,
     *  ``firstBin`` 0, ``count`` = ``bins``) or a frame cut into runs in bin
     *  order (the passes of a ``~pfft``). The block overload takes a block
     *  as a whole frame of its length, as MSP does outside a ``pfft~``.
     *
     *  - Bins at or past ``capacity()`` are not kept: ``size()`` is the
     *    frame's ``bins`` or the capacity, whichever is smaller.
     *  - An input that is not a finite number is kept as 0.
     *
     *  Two frames of ``capacity()`` values (the one being taken and the
     *  last whole one) are allocated in the constructor and never again:
     *  processing, reading and ``clear()`` allocate nothing, take no lock
     *  and nothing depends on the sample rate. One thread at a time.
     */
    class API frameSnapshot {
    public:
      static constexpr UInt DEFAULT_CAPACITY = frameSource::DEFAULT_CAPACITY;
      static constexpr UInt MAXIMUM_CAPACITY = frameSource::MAXIMUM_CAPACITY;

      /** @brief Room for frames of @p capacity bins (clamped to 1 to
       *         ``MAXIMUM_CAPACITY``). Allocates. */
      explicit frameSnapshot(UInt capacity = DEFAULT_CAPACITY);

      /** @brief How many bins of a frame the object keeps. */
      UInt capacity() const {
        return static_cast<UInt>(latest.size());
      }

      /** @brief One run of bins: ``frame.firstBin`` .. ``+ frame.count - 1``
       *         from @p in (``frame.count`` values). ``frame.real`` and
       *         ``frame.imaginary`` are not used. */
      void operator()(const stftFrame& frame, const Flt* in);

      /** @brief One block as a whole frame of its length (at most
       *         ``STANDARD_BUFFERSIZE`` samples). */
      void operator()(buffer& in);

      /** @brief Whole frames taken since construction or ``clear()``. */
      U64 frames() const {
        return completed;
      }

      /** @brief The values of the last whole frame: ``size()`` of them, bin
       *         ``k`` at ``k``. */
      const Flt* values() const {
        return latest.data();
      }

      /** @brief How many values the last whole frame left: its bins, at
       *         most the capacity; 0 before the first. */
      UInt size() const {
        return latestSize;
      }

      /** @brief The bins of the last whole frame, kept or not; 0 before the
       *         first. */
      UInt bins() const {
        return latestBins;
      }

      /** @brief Bin @p bin of the last whole frame; 0 past ``size()``. */
      Flt value(UInt bin) const {
        return bin < latestSize ? latest[bin] : 0.f;
      }

      /** @brief Copies the last whole frame into @p to, at most @p max
       *         values. Returns how many. */
      UInt copy(Flt* to, UInt max) const;

      /** @brief Forgets every frame: ``size()`` and ``frames()`` are 0 and
       *         a frame being taken is dropped. */
      frameSnapshot& clear();

    private:
      std::vector<Flt> taking;
      std::vector<Flt> latest;
      UInt latestSize = 0;
      UInt latestBins = 0;
      U64 completed = 0;

      // The frame being taken.
      bool started = false;
      bool intact = false;
      U64 lastIndex = 0;
      UInt lastFirst = 0;
      UInt nextBin = 0;
      UInt takingBins = 0;
      U64 blocks = 0; // the block overload's frame index
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SPECTRAL_FRAMELIST_HPP
