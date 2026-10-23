/*
  ==============================================================================

    arithmetic.hpp
    Signal-rate arithmetic operators that the buffer operators in buffer.hpp
    do not cover: reversed subtraction and division, and the floating
    remainder (issue #1046).

  ==============================================================================
*/

#ifndef YSE_DSP_OPERATORS_ARITHMETIC_HPP
#define YSE_DSP_OPERATORS_ARITHMETIC_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Reversed subtraction: ``right - in`` per sample.
     *
     *  ``-`` with the operands the other way round. With a right operand of 1
     *  it turns a 0..1 envelope or phase ramp upside down (``1 - x``) without
     *  a second operator.
     *
     *  The right operand is a number or a signal. Each call processes one
     *  block of at most ``STANDARD_BUFFERSIZE`` samples into an output the
     *  class owns; a longer input is processed up to that length. With a
     *  signal right operand the block is as long as the shorter of the two
     *  inputs. There is no sample-rate dependent state.
     *
     *  Call it from one thread at a time. Processing allocates nothing: the
     *  output is sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API reverseSubtract {
    public:
      reverseSubtract();

      /** @brief One sample: ``right - in``. */
      static Flt process(Flt in, Flt right);

      /** @brief One block: ``right - in`` with a constant ``right``. */
      buffer& operator()(buffer& in, Flt right);

      /** @brief One block: ``right - in`` sample by sample. */
      buffer& operator()(buffer& in, buffer& right);

    private:
      buffer output;
    };

    /**
     *  @brief Reversed division: ``right / in`` per sample.
     *
     *  ``/`` with the operands the other way round, so the signal is the
     *  divisor. With a right operand of 1 it gives the reciprocal ``1 / x``.
     *
     *  A sample of ``in`` that is exactly 0 gives 0 rather than an infinity,
     *  the convention the control operator ``.!/`` uses, so a silent input
     *  cannot put an inf or a NaN into the signal chain. A divisor merely
     *  close to 0 still gives a large value.
     *
     *  Block length, threading and allocation as ``reverseSubtract``. There
     *  is no sample-rate dependent state.
     */
    class API reverseDivide {
    public:
      reverseDivide();

      /** @brief One sample: ``right / in``, or 0 when ``in`` is 0. */
      static Flt process(Flt in, Flt right);

      /** @brief One block: ``right / in`` with a constant ``right``. */
      buffer& operator()(buffer& in, Flt right);

      /** @brief One block: ``right / in`` sample by sample. */
      buffer& operator()(buffer& in, buffer& right);

    private:
      buffer output;
    };

    /**
     *  @brief Floating remainder: ``in`` modulo ``divisor`` per sample.
     *
     *  The remainder of ``in / divisor`` after truncating the quotient toward
     *  zero, as C's ``fmod``. The remainder has the sign of ``in`` and is
     *  smaller in size than the divisor: ``5.5 % 2`` is 1.5, ``-5.5 % 2`` is
     *  -1.5, and ``5.5 % -2`` is 1.5. The sign of the divisor plays no part.
     *  This is the convention of the signal remainder operator this ports and
     *  of the control operator ``.%``. To wrap a value that can go negative
     *  into 0..1, use ``wrap`` from ``math.hpp`` instead.
     *
     *  A divisor of 0 gives 0 rather than a NaN.
     *
     *  Block length, threading and allocation as ``reverseSubtract``. There
     *  is no sample-rate dependent state.
     */
    class API modulo {
    public:
      modulo();

      /** @brief One sample: the remainder of ``in / divisor``, or 0 when
       *         ``divisor`` is 0. */
      static Flt process(Flt in, Flt divisor);

      /** @brief One block with a constant ``divisor``. */
      buffer& operator()(buffer& in, Flt divisor);

      /** @brief One block with a per-sample ``divisor``. */
      buffer& operator()(buffer& in, buffer& divisor);

    private:
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_OPERATORS_ARITHMETIC_HPP
