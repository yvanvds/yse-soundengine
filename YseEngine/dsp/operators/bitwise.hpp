/*
  ==============================================================================

    bitwise.hpp
    Bit manipulation of signals: and, or, exclusive-or, inversion, shifting
    and NaN / infinity removal (issue #1051).

  ==============================================================================
*/

#ifndef YSE_DSP_OPERATORS_BITWISE_HPP
#define YSE_DSP_OPERATORS_BITWISE_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief How a bitwise class reads its float samples as 32 bits.
     *
     *  A sample is a 32-bit IEEE 754 float: 1 sign bit, 8 exponent bits and
     *  23 mantissa bits. *Raw* takes that bit pattern as it is, so 1.0 is
     *  ``0x3F800000``. *Integer* first converts the sample to a 32-bit
     *  integer (toward zero, saturating at the integer range, NaN as 0), so
     *  1.7 is 1. A result read as integer is converted back to a float,
     *  which keeps 24 bits of integer resolution.
     *
     *  The first word is the left operand (the signal processed), the second
     *  the right operand (the mask or second signal). Only ``bitwise`` has a
     *  right operand: ``bitnot`` and ``bitshift`` read the left word only, so
     *  ``RAW_INTEGER`` acts as ``RAW`` and ``INTEGER_RAW`` as ``INTEGER``.
     *  The values match Max's ``mode`` attribute, 0 to 3.
     */
    enum class BIT_MODE {
      RAW = 0, ///< Both operands raw.
      INTEGER = 1, ///< Both operands converted to integers.
      RAW_INTEGER = 2, ///< The left operand raw, the right one an integer.
      INTEGER_RAW = 3, ///< The left operand an integer, the right one raw.
    };

    /** @brief A mode from an integer: 0 to 3, anything else ``RAW``. */
    API BIT_MODE BitModeFromInt(Int value);

    /** @brief ``true`` when ``mode`` converts the left operand to an integer. */
    API bool BitLeftIsInteger(BIT_MODE mode);

    /** @brief ``true`` when ``mode`` converts the right operand to an integer. */
    API bool BitRightIsInteger(BIT_MODE mode);

    /**
     *  @brief The 32 bits of ``value``: its raw float bit pattern, or with
     *         ``integer`` the 32-bit integer it converts to (toward zero,
     *         saturating, NaN as 0).
     */
    API Int FloatToBits(Flt value, bool integer);

    /**
     *  @brief The float for 32 result bits: the float with that bit pattern,
     *         or with ``integer`` the integer converted to a float.
     *
     *  A raw bit pattern that is NaN or infinity gives 0, so no NaN or
     *  infinity leaves the bitwise classes.
     */
    API Flt BitsToFloat(Int bits, bool integer);

    /**
     *  @brief Bitwise and, or, or exclusive-or of a signal with a bit mask
     *         or with a second signal.
     *
     *  Bit-level manipulation of samples, for glitch and bitcrush sounds.
     *  ``AND`` with a mask clears bits (``0xFFFF0000`` in raw mode drops the
     *  low 16 mantissa bits, a coarse bitcrush), ``OR`` sets them and
     *  ``XOR`` flips them. The operands are read as set by ``setMode`` (see
     *  ``BIT_MODE``), raw by default.
     *
     *  The mask (``setMask``) is a 32-bit pattern used as it is in every
     *  mode. ``FloatToBits`` makes one from a float the way a mode would
     *  read it. A second signal is read sample by sample with the right
     *  half of the mode.
     *
     *  **No NaN or infinity leaves this class:** a raw result whose bits
     *  form NaN or infinity gives 0, as ``bitsafe`` would make it.
     *
     *  Each call processes one block of at most ``STANDARD_BUFFERSIZE``
     *  samples into an output the class owns. There is no sample-rate
     *  dependent state. Call it from one thread at a time; the setters are
     *  atomic and may be called from any thread. Processing allocates
     *  nothing: the output is sized to ``STANDARD_BUFFERSIZE`` in the
     *  constructor.
     */
    class API bitwise {
    public:
      /** @brief The operation. */
      enum class OP {
        AND, ///< 1 where both operands have a 1.
        OR, ///< 1 where either operand has a 1.
        XOR, ///< 1 where exactly one operand has a 1.
      };

      explicit bitwise(OP op = OP::AND, Int mask = 0, BIT_MODE mode = BIT_MODE::RAW);

      bitwise& setOp(OP value);
      OP getOp() const {
        return op;
      }

      /** @brief The 32-bit mask used by ``operator()(buffer&)``. */
      bitwise& setMask(Int value);
      Int getMask() const {
        return mask;
      }

      bitwise& setMode(BIT_MODE value);
      BIT_MODE getMode() const {
        return mode;
      }

      /** @brief One sample against a 32-bit right operand. */
      static Flt process(OP op, BIT_MODE mode, Flt left, Int rightBits);

      /** @brief One block against the mask. */
      buffer& operator()(buffer& in);

      /** @brief One block against ``right`` sample by sample, read with the
       *         right half of the mode. The block is as long as the shorter
       *         input. */
      buffer& operator()(buffer& in, buffer& right);

    private:
      std::atomic<OP> op;
      aInt mask;
      std::atomic<BIT_MODE> mode;
      buffer output;
    };

    /**
     *  @brief Inverts every bit of every sample.
     *
     *  In raw mode this flips the sign, exponent and mantissa bits of the
     *  float (1.0 gives just under -4, 0 gives NaN, so 0); in integer mode it
     *  gives ``-n - 1`` for an integer ``n``. A raw result that is NaN or
     *  infinity gives 0. Block length, threading and allocation as
     *  ``bitwise``. There is no sample-rate dependent state.
     */
    class API bitnot {
    public:
      explicit bitnot(BIT_MODE mode = BIT_MODE::RAW);

      bitnot& setMode(BIT_MODE value);
      BIT_MODE getMode() const {
        return mode;
      }

      /** @brief One sample. */
      static Flt process(BIT_MODE mode, Flt in);

      /** @brief One block. */
      buffer& operator()(buffer& in);

    private:
      std::atomic<BIT_MODE> mode;
      buffer output;
    };

    /**
     *  @brief Shifts the bits of every sample left or right.
     *
     *  A positive shift moves bits left (toward the sign bit), a negative
     *  one right. In raw mode the shift is logical on the float bit pattern:
     *  zeros come in at either end. In integer mode a left shift doubles the
     *  integer per bit (wrapping at 32 bits) and a right shift halves it,
     *  rounding toward minus infinity (the sign is kept). A shift of 32 bits
     *  or more empties the word: 0, or -1 for a negative integer shifted
     *  right. A raw result that is NaN or infinity gives 0. Block length,
     *  threading and allocation as ``bitwise``. There is no sample-rate
     *  dependent state.
     */
    class API bitshift {
    public:
      explicit bitshift(Int shift = 0, BIT_MODE mode = BIT_MODE::RAW);

      /** @brief Bits to shift: positive left, negative right. */
      bitshift& setShift(Int value);
      Int getShift() const {
        return shift;
      }

      bitshift& setMode(BIT_MODE value);
      BIT_MODE getMode() const {
        return mode;
      }

      /** @brief One sample. */
      static Flt process(BIT_MODE mode, Int shift, Flt in);

      /** @brief One block. */
      buffer& operator()(buffer& in);

    private:
      aInt shift;
      std::atomic<BIT_MODE> mode;
      buffer output;
    };

    /**
     *  @brief Replaces NaN and infinite samples with 0 and passes every
     *         other sample through unchanged.
     *
     *  The guard every feedback path wants at the end of its chain: one NaN
     *  in a recursive filter or delay line otherwise stays there for good.
     *  The test reads the exponent bits, so it holds whatever the compiler's
     *  floating-point flags. ``apply`` cleans a buffer in place, for C++
     *  modules that own their buffer. Block length, threading and allocation
     *  as ``bitwise``. There is no sample-rate dependent state.
     */
    class API bitsafe {
    public:
      bitsafe();

      /** @brief One sample: 0 for NaN or infinity, otherwise ``in``. */
      static Flt process(Flt in);

      /** @brief Cleans ``io`` in place. Allocates nothing. */
      static void apply(buffer& io);

      /** @brief One block into the output this class owns. */
      buffer& operator()(buffer& in);

    private:
      buffer output;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_OPERATORS_BITWISE_HPP
