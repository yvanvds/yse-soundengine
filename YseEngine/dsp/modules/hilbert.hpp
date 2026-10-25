/*
  ==============================================================================

    hilbert.h
    Created: 31 Jan 2014 2:56:08pm
    Author:  yvan

  ==============================================================================
*/

#ifndef HILBERT_H_INCLUDED
#define HILBERT_H_INCLUDED

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Hilbert transform: splits a signal into two outputs 90 degrees
     *         apart across the band.
     *
     *  Used to build single-sideband modulators, frequency shifters, and
     *  analytic-signal envelope followers; behind ``~hilbert``, ported from
     *  Max's ``hilbert~`` (issue #1097).
     *
     *  Each output runs the input through its own chain of two second-order
     *  allpass sections (the design of Pure Data's ``hilbert~``), so both
     *  have a gain of exactly 1 at every frequency and only their phases
     *  differ. Each section is
     *
     *  ``(a2 + a1 z^-1 + z^-2) / (1 + a1 z^-1 + a2 z^-2)``
     *
     *  with the ``a1 a2`` of ``REAL_SECTIONS`` and ``IMAGINARY_SECTIONS``.
     *  The ``imaginary()`` output lags ``real()`` by 90 degrees, so the pair
     *  is the analytic signal ``real + j imaginary`` of a band-limited copy
     *  of the input: a cosine in gives a cosine on ``real()`` and the sine of
     *  the same phase on ``imaginary()``, and their magnitude
     *  ``hypot(real, imaginary)`` is the envelope (``DSP::cartToPolar`` gives
     *  it with the instantaneous phase). Multiplying ``real`` by ``cos`` and
     *  subtracting ``imaginary`` times ``sin`` of a carrier shifts the
     *  spectrum up by the carrier's frequency.
     *
     *  The phase difference is within 1 degree of 90 from ``0.0018`` to
     *  ``0.308`` times the sample rate (85 Hz to 14.8 kHz at 48 kHz) and
     *  falls towards 0 at 0 Hz and the Nyquist frequency. The design is
     *  fixed in terms of the sample rate, so that band follows the live
     *  sample rate and nothing is derived from ``SAMPLERATE``.
     *
     *  State is double precision. An input sample that is not a finite
     *  number is skipped (the outputs and state hold), and an output that is
     *  not finite sets the state to 0, so neither can latch the filter;
     *  ``clear()`` sets it to 0 as well.
     *
     *  Each block call fills both outputs, returns ``real()``, and processes
     *  at most ``STANDARD_BUFFERSIZE`` samples. Call it from one thread at a
     *  time. Processing allocates nothing: the outputs are sized to
     *  ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API hilbert {
    public:
      /** @brief The two coefficients of one allpass section, as in the
       *         class description. */
      struct section {
        Dbl a1;
        Dbl a2;
      };

      /** @brief The sections of the ``real()`` output, in order. */
      static constexpr section REAL_SECTIONS[2] = {{0.02569, -0.260502}, {-1.8685, 0.870686}};

      /** @brief The sections of the ``imaginary()`` output, in order. */
      static constexpr section IMAGINARY_SECTIONS[2] = {{-1.94632, 0.94657}, {-0.83774, 0.06338}};

      /** @brief Both outputs for one sample. */
      struct outputs {
        Flt real = 0.f;
        Flt imaginary = 0.f;
      };

      hilbert();

      /** @brief Sets the filter state to 0. */
      hilbert& clear();

      /** @brief One sample. */
      outputs process(Flt in);

      /** @brief One block; fills both outputs and returns ``real()``. */
      buffer& operator()(buffer& in);

      /**
       *  @brief One block, copied into @p out1 (``real()``) and @p out2
       *         (``imaginary()``, 90 degrees lagged from ``out1``).
       *
       *  Kept for older code. The copies resize @p out1 and @p out2 to the
       *  block, which allocates when their length changes; read ``real()``
       *  and ``imaginary()`` instead on an audio thread.
       */
      void operator()(buffer& in, buffer& out1, buffer& out2);

      /** @brief The in-phase output of the last block call. */
      buffer& real() {
        return re;
      }

      /** @brief The quadrature output of the last block call, 90 degrees
       *         behind ``real()``. */
      buffer& imaginary() {
        return im;
      }

    private:
      // Transposed direct form II state of one section.
      struct state {
        Dbl s1 = 0.0;
        Dbl s2 = 0.0;
      };

      void Step(Flt in);
      static Dbl Run(const section& c, state& s, Dbl x);

      buffer re;
      buffer im;
      state realState[2];
      state imaginaryState[2];
      outputs last;
    };

  } // namespace DSP
} // namespace YSE

#endif // HILBERT_H_INCLUDED
