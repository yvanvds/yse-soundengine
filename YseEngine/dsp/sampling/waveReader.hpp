/*
  ==============================================================================

    waveReader.hpp
    A region of a sample buffer read as one cycle of a waveform, at a phase
    from 0 to 1, in one row or crossfaded across rows (issue #1146).

  ==============================================================================
*/

#ifndef YSE_DSP_SAMPLING_WAVEREADER_HPP
#define YSE_DSP_SAMPLING_WAVEREADER_HPP

#include <vector>
#include "../../headers/constants.hpp"
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    class sampleView;

    /**
     *  @brief Reads a region of a ``sampleBuffer`` as one cycle of a
     *         waveform: a phase from 0 to 1 scans it from start to end.
     *         Wavetable synthesis from any sample. What the patcher's
     *         ``~wave`` (Max's ``wave~``) and, with rows, ``~2d.wave``
     *         (Max's ``2d.wave~``) do.
     *
     *  Drive it with a ``phasor``: the region repeats at the phasor's
     *  frequency, and a falling ramp plays it backward.
     *
     *  - **The region** (``start``, ``end``, in milliseconds into the
     *    buffer). Turned into frames each block at the rate the buffer's
     *    samples are at, through ``sampleRegion::fromMs``: an end of 0 is
     *    the end of the buffer, both points are kept inside it, and an end
     *    at or before the start leaves no region (silence).
     *  - **The phase.** 0 reads the region's first frame. The region is
     *    one cycle, so the phase wraps: 1 is 0 again, 1.25 is 0.25, -0.25
     *    is 0.75. A phase that is not a finite number outputs 0.
     *  - **Rows** (``rows``, 0 or 1 by default: one row). With ``n`` rows
     *    the region is cut into ``n`` rows of ``length / n`` frames each
     *    (the frames left over at its end are not read), and each row is
     *    one cycle. The row phase, 0 to 1, picks the row: 0 the first, 1
     *    the last, and between two rows their outputs are crossfaded in a
     *    straight line, so a slow ramp morphs from the first cycle to the
     *    last. A row phase below 0 is 0, above 1 is 1, not a number is 0.
     *    With one row the row phase is not used.
     *  - **Interpolation** (``interp``, Max's ``interp`` 0 to 6, linear by
     *    default). Between two frames: ``NONE`` reads the frame at or
     *    before the position; ``LINEAR`` a straight line; ``LINEAR_LOW``
     *    is Max's faster low-quality linear mode and reads as ``LINEAR``
     *    here; ``COSINE`` a half cosine; ``CUBIC`` the 4-point cubic
     *    polynomial, ``SPLINE`` the Catmull-Rom spline and ``HERMITE`` the
     *    Hermite curve with ``tension`` and ``bias`` (both 0 by default,
     *    where it equals ``SPLINE``), each through the frame before and the
     *    two after. The neighbours wrap around the row, so the last frame
     *    leads back into the first and a cycle loops without a seam.
     *  - **Channels** (``channels``, 1 to ``MAX_CHANNELS``). Output ``k``
     *    reads buffer channel ``k``. Buffer channels past the last output
     *    are not read; outputs the buffer has no channel for are silent.
     *
     *  .. code-block:: cpp
     *
     *     // control thread
     *     reader.bind(DSP::sampleBufferRegistry::acquire("cycles"));
     *     DSP::waveReader wave(1);
     *     wave.rows(8).interp(DSP::waveReader::interpolation::CUBIC);
     *
     *     // audio thread, once per block
     *     DSP::sampleView view = reader.pin();
     *     DSP::buffer& out = wave(view, phase, morph);
     *
     *  Every read is a relaxed atomic load through the ``sampleView`` a
     *  ``sampleBufferReader`` pins for the block, so a writer on another
     *  render thread may change the samples meanwhile. The region follows
     *  the buffer's rate, length and shape from block to block; the engine's
     *  sample rate enters nowhere, so a change of it changes nothing here.
     *
     *  The outputs are sized to ``STANDARD_BUFFERSIZE`` when the channel
     *  count is set; a shorter block resizes them down without allocating,
     *  and a longer one is cut to ``STANDARD_BUFFERSIZE``. Processing
     *  allocates nothing and takes no lock. Not thread-safe: one thread at a
     *  time, and the setters on that thread too; ``channels`` allocates, so
     *  set it before the audio thread sees the object.
     */
    class API waveReader {
    public:
      /** @brief How a position between two frames is read: Max's
       *         ``interp`` 0 to 6. */
      enum class interpolation {
        NONE = 0,
        LINEAR = 1,
        LINEAR_LOW = 2,
        COSINE = 3,
        CUBIC = 4,
        SPLINE = 5,
        HERMITE = 6,
      };

      /** @brief The most outputs. */
      static constexpr UInt MAX_CHANNELS = 64;

      /** @brief A reader with @p count outputs (1 to ``MAX_CHANNELS``).
       *         Allocates. */
      explicit waveReader(UInt count = 1);

      /** @brief The number of outputs, 1 to ``MAX_CHANNELS``. Allocates:
       *         not on the audio thread. */
      waveReader& channels(UInt count);
      UInt channels() const {
        return static_cast<UInt>(outputs.size());
      }

      /** @brief The start of the region, in ms into the buffer. Default
       *         0. */
      waveReader& start(Flt ms) {
        startMs = ms;
        return *this;
      }
      Flt start() const {
        return startMs;
      }

      /** @brief The end of the region, in ms into the buffer; 0 (the
       *         default) is the end of the buffer. */
      waveReader& end(Flt ms) {
        endMs = ms;
        return *this;
      }
      Flt end() const {
        return endMs;
      }

      /** @brief The number of rows the region is cut into; 0 and 1 are one
       *         row (the default). */
      waveReader& rows(UInt count) {
        rowCount = count;
        return *this;
      }
      UInt rows() const {
        return rowCount;
      }

      waveReader& interp(interpolation value) {
        interpolate = value;
        return *this;
      }
      interpolation interp() const {
        return interpolate;
      }

      /** @brief The Hermite curve's bias: 0 even (the default), above 0
       *         leaning to the frame before, below 0 to the frame after. */
      waveReader& bias(Flt value) {
        hermiteBias = value;
        return *this;
      }
      Flt bias() const {
        return hermiteBias;
      }

      /** @brief The Hermite curve's tension: 0 (the default), 1 high
       *         (straight lines), -1 low (rounder). */
      waveReader& tension(Flt value) {
        hermiteTension = value;
        return *this;
      }
      Flt tension() const {
        return hermiteTension;
      }

      /** @brief One sample of buffer channel @p channel at @p phase, in the
       *         row @p rowPhase picks. 0 without a region or that channel. */
      Flt read(const sampleView& view, UInt channel, Flt phase, Flt rowPhase = 0.f) const;

      /** @brief One block at the phases of @p phase, in the first row (or
       *         the only one). As long as @p phase, up to
       *         ``STANDARD_BUFFERSIZE``. Returns output 0. */
      buffer& operator()(const sampleView& view, buffer& phase);

      /** @brief One block at the phases of @p phase, in the rows
       *         @p rowPhase picks, sample by sample. As long as the shorter
       *         of the two, up to ``STANDARD_BUFFERSIZE``. Returns output
       *         0. */
      buffer& operator()(const sampleView& view, buffer& phase, buffer& rowPhase);

      /** @brief Output @p channel (from 0); the last output past the
       *         count. */
      buffer& out(UInt channel = 0) {
        return outputs[channel < outputs.size() ? channel : outputs.size() - 1];
      }

    private:
      buffer& Render(const sampleView& view, const Flt* phase, const Flt* rowPhase, UInt length);

      std::vector<buffer> outputs;
      Flt startMs = 0.f;
      Flt endMs = 0.f;
      UInt rowCount = 1;
      interpolation interpolate = interpolation::LINEAR;
      Flt hermiteBias = 0.f;
      Flt hermiteTension = 0.f;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_WAVEREADER_HPP
