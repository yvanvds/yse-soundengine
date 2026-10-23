/*
  ==============================================================================

    units.hpp
    Signal-rate unit conversions: amplitude <-> dBFS, milliseconds <->
    samples (issue #1052).

  ==============================================================================
*/

#ifndef YSE_DSP_OPERATORS_UNITS_HPP
#define YSE_DSP_OPERATORS_UNITS_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /** @brief The decibel value ``AmpToDb`` gives for silence (an amplitude
     *         of 0), and the level at or below which ``DbToAmp`` gives 0. */
    constexpr Flt DB_FLOOR = -999.f;

    /**
     *  @brief Linear amplitude to decibels full scale: ``20 * log10(|amp|)``.
     *
     *  1 is 0 dB, 0.5 about -6 dB, 2 about +6 dB. The sign of the amplitude
     *  is ignored, so a bipolar signal converts sample by sample. 0 (and
     *  NaN) gives ``DB_FLOOR``; nothing below ``DB_FLOOR`` and no NaN or
     *  infinity is returned. This is the full-scale decibel; the Pd-style
     *  ``rmsToDb`` (where 100 dB is an amplitude of 1) is a separate class.
     */
    Flt API AmpToDb(Flt amp);

    /**
     *  @brief Decibels full scale to linear amplitude: ``10 ** (db / 20)``.
     *
     *  0 dB is 1, -6 dB about 0.5. ``DB_FLOOR`` or below (and NaN) gives 0;
     *  levels above 770 dB are clamped to it so the result stays finite.
     */
    Flt API DbToAmp(Flt db);

    /**
     *  @brief Milliseconds to samples at the engine's current sample rate.
     *
     *  Reads ``YSE::SAMPLERATE`` on every call. A non-finite result gives 0.
     */
    Flt API MsToSamples(Flt ms);

    /**
     *  @brief Samples to milliseconds at the engine's current sample rate.
     *
     *  Reads ``YSE::SAMPLERATE`` on every call. A non-finite result, or a
     *  sample rate of 0, gives 0.
     */
    Flt API SamplesToMs(Flt samples);

    /**
     *  @brief Per-sample linear amplitude to decibels full scale.
     *
     *  Buffer-rate counterpart of ``AmpToDb``, with the same floor. Each
     *  call processes one block of at most ``STANDARD_BUFFERSIZE`` samples
     *  into an output the class owns. There is no sample-rate dependent
     *  state, and processing allocates nothing: the output is sized to
     *  ``STANDARD_BUFFERSIZE`` in the constructor. Call it from one thread
     *  at a time.
     */
    class API ampToDb {
    public:
      ampToDb();

      /** @brief One sample, as ``AmpToDb``. */
      static Flt process(Flt in);

      /** @brief One block. */
      buffer& operator()(buffer& in);

    private:
      buffer output;
    };

    /**
     *  @brief Per-sample decibels full scale to linear amplitude.
     *
     *  Buffer-rate counterpart of ``DbToAmp``. Block length, threading and
     *  allocation as ``ampToDb``.
     */
    class API dbToAmp {
    public:
      dbToAmp();

      /** @brief One sample, as ``DbToAmp``. */
      static Flt process(Flt in);

      /** @brief One block. */
      buffer& operator()(buffer& in);

    private:
      buffer output;
    };

    /**
     *  @brief Per-sample milliseconds to samples.
     *
     *  Follows the live sample rate: ``YSE::SAMPLERATE`` is read on every
     *  block, nothing is derived from it in the constructor. A non-finite
     *  result gives 0. Block length, threading and allocation as
     *  ``ampToDb``.
     */
    class API msToSamples {
    public:
      msToSamples();

      /** @brief One sample at ``sampleRate``. */
      static Flt process(Flt ms, Flt sampleRate);

      /** @brief One block at the engine's current sample rate. */
      buffer& operator()(buffer& in);

    private:
      buffer output;
    };

    /**
     *  @brief Per-sample samples to milliseconds.
     *
     *  Follows the live sample rate as ``msToSamples`` does. A non-finite
     *  result, or a sample rate of 0, gives 0. Block length, threading and
     *  allocation as ``ampToDb``.
     */
    class API samplesToMs {
    public:
      samplesToMs();

      /** @brief One sample at ``sampleRate``. */
      static Flt process(Flt samples, Flt sampleRate);

      /** @brief One block at the engine's current sample rate. */
      buffer& operator()(buffer& in);

    private:
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_OPERATORS_UNITS_HPP
