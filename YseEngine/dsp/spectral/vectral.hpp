/*
  ==============================================================================

    vectral.hpp
    Per-index smoothing of successive frames (spectra): every index of a
    frame is smoothed against the same index of the frames before it, with
    rampsmooth, slide or deltaclip (issue #1132).

  ==============================================================================
*/

#ifndef YSE_DSP_SPECTRAL_VECTRAL_HPP
#define YSE_DSP_SPECTRAL_VECTRAL_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"
#include "../filters/rampSmooth.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Smooths a stream of frames index by index: each value of a
     *         frame against the value at the same index in the frames before
     *         it. For stable spectral displays and spectral envelope
     *         following.
     *
     *  Ported from MSP's ``vectral~``. The input is a stream of frames, one
     *  value per sample, with two index signals that say where each sample
     *  belongs: typically an FFT's bins with their bin index, but any frame
     *  stream works. The class keeps one smoothed value per index, a frame
     *  of ``size()`` values. Each sample:
     *
     *  1. the value at the *input index* is smoothed towards the input
     *     sample, with the mode below, and stored;
     *  2. the output is the stored value at the *output index*.
     *
     *  With both indexes equal, every value comes out smoothed in the frame
     *  it arrives in; an input index ahead of the output index by ``d``
     *  shifts the frame by ``d`` places. An index is used as its whole part;
     *  one below 0, at or past ``size()``, or not a number, stores nothing
     *  (input) or reads 0 (output).
     *
     *  The smoothing runs across *frames*, never across the samples of one
     *  frame, with the equations of the time-domain classes, one state per
     *  index:
     *
     *  - ``rampsmooth(up, down)``: as ``rampSmooth``, with lengths in frames.
     *    A new value starts a straight ramp from the stored value, over
     *    ``up`` frames when it is higher and ``down`` frames when it is
     *    lower, landing on it exactly; a value that keeps changing moves
     *    1/length of the way each frame. Lengths are whole frames, below 1
     *    counts as 1. This is the mode a new object starts in, at 1 and 1,
     *    which passes the frames through.
     *  - ``slide(up, down)``: as ``slide``,
     *    ``y(m) = y(m-1) + (x(m) - y(m-1)) / slide``, the slide-up amount
     *    while the value rises and the slide-down amount while it falls.
     *  - ``deltaclip(lowest, highest)``: as ``slewLimiter``, the change from
     *    one frame to the next clipped to ``[lowest, highest]``.
     *
     *  Each mode keeps its own settings; choosing a mode keeps the stored
     *  values, so the smoothing carries on from where it is. Settings that
     *  are not finite are ignored, as in the time-domain classes (a
     *  deltaclip limit may be infinite); an input sample that is not a
     *  finite number stores nothing.
     *
     *  The frame is allocated in the constructor, for ``capacity()``
     *  indexes, and never again: ``size()`` changes how many are used, from
     *  1 up to the capacity, and a new size clears the frame. ``clear()``
     *  sets every stored value to 0. Nothing depends on the sample rate:
     *  everything counts frames.
     *
     *  Each block call processes at most ``STANDARD_BUFFERSIZE`` samples
     *  and is as long as its shortest input. Call it from one thread at a
     *  time. Processing, ``size()``, ``clear()`` and the mode setters
     *  allocate nothing.
     */
    class API vectral {
    public:
      /** @brief How a value is smoothed against the frames before. */
      enum MODE {
        RAMPSMOOTH, ///< a straight ramp over a number of frames
        SLIDE, ///< a part of the distance each frame
        DELTACLIP, ///< a limited change per frame
      };

      /** @brief The frame size of a default-constructed object. */
      static constexpr UInt DEFAULT_SIZE = 512;
      /** @brief The largest capacity. */
      static constexpr UInt MAXIMUM_SIZE = 1u << 16;

      /** @brief A frame of @p capacity values (clamped to 1 to
       *         ``MAXIMUM_SIZE``), all used. Allocates. */
      explicit vectral(UInt capacity = DEFAULT_SIZE);

      /** @brief How many values the frame holds at most. */
      UInt capacity() const {
        return static_cast<UInt>(frame.size());
      }

      /** @brief The frame size: indexes 0 .. @p values - 1 are used. Clamped
       *         to 1 .. ``capacity()``. A new size clears the frame. */
      vectral& size(UInt values);

      /** @brief The frame size in use. */
      UInt size() const {
        return used;
      }

      /** @brief Linear smoothing over @p up frames when a value rises and
       *         @p down frames when it falls. Non-finite: that setting is
       *         ignored. */
      vectral& rampsmooth(Flt up, Flt down);

      /** @brief Logarithmic smoothing by @p up while a value rises and
       *         @p down while it falls. Non-finite: that setting is
       *         ignored. */
      vectral& slide(Flt up, Flt down);

      /** @brief The change per frame limited to @p lowest .. @p highest.
       *         NaN: that setting is ignored. */
      vectral& deltaclip(Flt lowest, Flt highest);

      /** @brief Sets every stored value to 0. */
      vectral& clear();

      /** @brief The mode in use. */
      MODE mode() const {
        return current;
      }

      /** @brief The rampsmooth lengths, as last set. */
      Flt rampUp() const {
        return rampUpFrames;
      }
      Flt rampDown() const {
        return rampDownFrames;
      }

      /** @brief The slide amounts, as last set. */
      Flt slideUp() const {
        return slideUpAmount;
      }
      Flt slideDown() const {
        return slideDownAmount;
      }

      /** @brief The deltaclip limits, as last set. */
      Flt clipLowest() const {
        return clipLow;
      }
      Flt clipHighest() const {
        return clipHigh;
      }

      /** @brief The stored value at @p index; 0 outside the frame. */
      Flt value(UInt index) const;

      /** @brief One sample: stores @p in at @p inputIndex, then returns the
       *         value at @p outputIndex. */
      Flt process(Flt outputIndex, Flt inputIndex, Flt in);

      /** @brief One block, sample by sample as ``process``; as long as the
       *         shortest input. */
      buffer& operator()(buffer& outputIndex, buffer& inputIndex, buffer& in);

    private:
      // The index a signal value names, or `used` when it names none.
      UInt Index(Flt position) const;

      buffer output;
      std::vector<rampSmooth::state> frame;
      UInt used;

      MODE current = RAMPSMOOTH;
      Flt rampUpFrames = rampSmooth::DEFAULT_RAMP;
      Flt rampDownFrames = rampSmooth::DEFAULT_RAMP;
      Flt slideUpAmount = 1.f;
      Flt slideDownAmount = 1.f;
      Flt clipLow = 0.f;
      Flt clipHigh = 0.f;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SPECTRAL_VECTRAL_HPP
