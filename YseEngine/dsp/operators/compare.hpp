/*
  ==============================================================================

    compare.hpp
    Signal-rate comparison and per-sample minimum / maximum (issue #1048).

  ==============================================================================
*/

#ifndef YSE_DSP_OPERATORS_COMPARE_HPP
#define YSE_DSP_OPERATORS_COMPARE_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Sample-wise comparison of a signal against a number or a second
     *         signal: 1 where the comparison holds, 0 where it does not.
     *
     *  The result is exactly 0.0 or 1.0, ready to gate, count or switch with.
     *  The operator is chosen with ``setOperator`` (or the constructor) and
     *  reads ``in OP right``: with ``GREATER``, a sample is 1 where ``in`` is
     *  greater than ``right``. A NaN compares false everywhere, so it gives 0
     *  for every operator except ``NOT_EQUAL``.
     *
     *  **Tolerance.** ``setFuzzy`` widens equality: two samples closer than
     *  or exactly ``fuzzy`` apart count as equal. That affects ``EQUAL``,
     *  ``NOT_EQUAL``, ``GREATER_EQUAL`` (``in >= right - fuzzy``) and
     *  ``LESS_EQUAL`` (``in <= right + fuzzy``). ``GREATER`` and ``LESS`` are
     *  strict and ignore it. The default is 0: exact comparison.
     *
     *  Each call processes one block of at most ``STANDARD_BUFFERSIZE``
     *  samples into an output the class owns; with a signal right operand the
     *  block is as long as the shorter of the two inputs. There is no
     *  sample-rate dependent state.
     *
     *  Call it from one thread at a time. Processing allocates nothing: the
     *  output is sized to ``STANDARD_BUFFERSIZE`` in the constructor.
     */
    class API compare {
    public:
      /** @brief The comparison applied as ``in OP right``. */
      enum class OP {
        EQUAL, ///< ``in == right``
        NOT_EQUAL, ///< ``in != right``
        GREATER, ///< ``in > right``
        GREATER_EQUAL, ///< ``in >= right``
        LESS, ///< ``in < right``
        LESS_EQUAL, ///< ``in <= right``
      };

      /** @brief A comparison with operator ``op`` and no tolerance. */
      explicit compare(OP op = OP::EQUAL);

      /** @brief Choose the comparison. Takes effect on the next block. */
      compare& setOperator(OP op);
      OP getOperator() const {
        return op;
      }

      /** @brief Equality tolerance. A negative value is treated as 0. */
      compare& setFuzzy(Flt tolerance);
      Flt getFuzzy() const {
        return fuzzy;
      }

      /** @brief One sample: 1 when ``in OP right`` holds within ``fuzzy``,
       *         otherwise 0. */
      static Flt process(OP op, Flt in, Flt right, Flt fuzzy = 0.f);

      /** @brief One block against a constant ``right``. */
      buffer& operator()(buffer& in, Flt right);

      /** @brief One block against ``right`` sample by sample. */
      buffer& operator()(buffer& in, buffer& right);

    private:
      OP op;
      Flt fuzzy;
      buffer output;
    };

    /**
     *  @brief Per-sample minimum of a signal and a number or a second signal.
     *
     *  Each output sample is the smaller of the two inputs; with a number it
     *  clamps the signal from above. Where one input is NaN the other input is
     *  the result, so a NaN on one side does not spread.
     *
     *  Block length, threading and allocation as ``compare``. There is no
     *  sample-rate dependent state.
     */
    class API minimum {
    public:
      minimum();

      /** @brief One sample: the smaller of ``in`` and ``right``. */
      static Flt process(Flt in, Flt right);

      /** @brief One block against a constant ``right``. */
      buffer& operator()(buffer& in, Flt right);

      /** @brief One block against ``right`` sample by sample. */
      buffer& operator()(buffer& in, buffer& right);

    private:
      buffer output;
    };

    /**
     *  @brief Per-sample maximum of a signal and a number or a second signal.
     *
     *  Each output sample is the larger of the two inputs; with a number it
     *  clamps the signal from below (``maximum`` against 0 is a half-wave
     *  rectifier). NaN handling, block length, threading and allocation as
     *  ``minimum``.
     */
    class API maximum {
    public:
      maximum();

      /** @brief One sample: the larger of ``in`` and ``right``. */
      static Flt process(Flt in, Flt right);

      /** @brief One block against a constant ``right``. */
      buffer& operator()(buffer& in, Flt right);

      /** @brief One block against ``right`` sample by sample. */
      buffer& operator()(buffer& in, buffer& right);

    private:
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_OPERATORS_COMPARE_HPP
