/*
  ==============================================================================

    mixMatrix.hpp
    Signal routing and mixing matrix: every input reaches every output at a
    gain of its own, and each gain change ramps (issue #1134).

  ==============================================================================
*/

#ifndef YSE_DSP_ROUTING_MIXMATRIX_HPP
#define YSE_DSP_ROUTING_MIXMATRIX_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A signal matrix: a number of inputs, a number of outputs, and a
     *         gain at every crosspoint. Each output is the sum of every input
     *         times the gain of their crosspoint.
     *
     *  Ported from Max's ``matrix~``. Inputs and outputs are numbered from 0.
     *  A crosspoint with a gain of 0 is *disconnected*; any other gain
     *  (negative too) connects it. Every crosspoint starts disconnected.
     *
     *  - ``set(in, out, gain)`` sets a crosspoint's gain, as Max's three-number
     *    list does: 0 disconnects. ``set(in, out, gain, ms)`` ramps that one
     *    change over its own time.
     *  - ``connect(in, out)`` connects at the default gain (1 unless the
     *    constructor or ``defaultGain()`` says otherwise); ``disconnect`` and
     *    ``clear`` set gains to 0.
     *  - In exclusive mode a connection disconnects the crosspoints it would
     *    otherwise share an input with (``EXCLUSIVE_INPUTS``), an output with
     *    (``EXCLUSIVE_OUTPUTS``), or both. The new connection wins and the old
     *    ones fade out over the same ramp. Turning exclusive mode on leaves
     *    the connections already made alone.
     *
     *  Every change ramps: the crosspoint's gain moves in a straight line from
     *  where it is to the new gain over the ramp time (``rampTime()``, 10 ms
     *  by default), so switching and mixing do not click or zipper. A change
     *  during a ramp starts a new ramp from the gain reached; a change to the
     *  gain a crosspoint is already heading for is ignored. A ramp time of 0
     *  (or under one sample) jumps at the next sample. The ramp is converted
     *  to samples at the live ``SAMPLERATE`` at the start of each block, so a
     *  ramp in progress follows a rate change.
     *
     *  Each call processes at most ``STANDARD_BUFFERSIZE`` samples. Call it
     *  from one thread at a time. Processing allocates nothing: the
     *  crosspoints and the outputs are sized in the constructor.
     */
    class API mixMatrix {
    public:
      /** @brief The most inputs, and the most outputs, a matrix has. */
      static constexpr UInt MAX_CHANNELS = 128;

      /** @brief The input and output count when none is given. */
      static constexpr UInt DEFAULT_CHANNELS = 1;

      /** @brief The ramp time in milliseconds when none is set. */
      static constexpr Flt DEFAULT_RAMP = 10.f;

      /** @brief Exclusive mode off: no connection limits another. */
      static constexpr Int EXCLUSIVE_OFF = 0;
      /** @brief Exclusive mode: an input reaches one output at a time. */
      static constexpr Int EXCLUSIVE_INPUTS = 1;
      /** @brief Exclusive mode: an output hears one input at a time. */
      static constexpr Int EXCLUSIVE_OUTPUTS = 2;
      /** @brief Exclusive mode: both of the others. */
      static constexpr Int EXCLUSIVE_BOTH = 3;

      /** @brief @p inputs inputs and @p outputs outputs (each 1 to
       *         ``MAX_CHANNELS``, clamped), nothing connected, and
       *         @p defaultGain the gain ``connect()`` uses. */
      explicit mixMatrix(UInt inputs = DEFAULT_CHANNELS, UInt outputs = DEFAULT_CHANNELS,
                         Flt defaultGain = 1.f);

      /** @brief The number of inputs. */
      UInt inputs() const {
        return ins;
      }

      /** @brief The number of outputs. */
      UInt outputs() const {
        return outs;
      }

      /** @brief Sets the gain from input @p in to output @p out, ramped over
       *         ``rampTime()``; 0 disconnects. A port the matrix does not
       *         have, or a gain that is not finite, is ignored. */
      mixMatrix& set(UInt in, UInt out, Flt gain);

      /** @brief As ``set(in, out, gain)``, with this change ramped over
       *         @p rampMs milliseconds instead (below 0 counts as 0; not
       *         finite uses ``rampTime()``). */
      mixMatrix& set(UInt in, UInt out, Flt gain, Flt rampMs);

      /** @brief Connects @p in to @p out at ``defaultGain()``. */
      mixMatrix& connect(UInt in, UInt out);

      /** @brief Disconnects @p in from @p out. */
      mixMatrix& disconnect(UInt in, UInt out);

      /** @brief Disconnects every crosspoint, each over ``rampTime()``. */
      mixMatrix& clear();

      /** @brief The ramp time in milliseconds for later changes. Below 0
       *         counts as 0; not finite is ignored. */
      mixMatrix& rampTime(Flt ms);

      /** @brief The ramp time as set. */
      Flt rampTime() const {
        return ramp;
      }

      /** @brief The gain ``connect()`` uses. Not finite is ignored. */
      mixMatrix& defaultGain(Flt gain);

      /** @brief The gain ``connect()`` uses. */
      Flt defaultGain() const {
        return defGain;
      }

      /** @brief The exclusive mode, ``EXCLUSIVE_OFF`` to
       *         ``EXCLUSIVE_BOTH``; a value outside that range is clamped. */
      mixMatrix& exclusive(Int mode);

      /** @brief The exclusive mode. */
      Int exclusive() const {
        return mode;
      }

      /** @brief The gain from @p in to @p out now, part way through a ramp
       *         while it moves. 0 for a port the matrix does not have. */
      Flt gain(UInt in, UInt out) const;

      /** @brief The gain from @p in to @p out is heading for: the last one
       *         set. 0 for a port the matrix does not have. */
      Flt target(UInt in, UInt out) const;

      /** @brief True while any crosspoint is ramping. */
      bool ramping() const {
        return moving > 0;
      }

      /** @brief Calls @p visit(in, out) for every other crosspoint a
       *         connection from @p in to @p out disconnects in exclusive
       *         @p mode, on a matrix of @p inputs by @p outputs: the rest of
       *         the input's row (``EXCLUSIVE_INPUTS``) and the rest of the
       *         output's column (``EXCLUSIVE_OUTPUTS``). The rule ``set``
       *         applies, for a caller that keeps the gains elsewhere. */
      template <typename F>
      static void Exclusions(Int mode, UInt in, UInt out, UInt inputs, UInt outputs, F&& visit) {
        if ((mode & EXCLUSIVE_INPUTS) != 0) {
          for (UInt o = 0; o < outputs; ++o)
            if (o != out) visit(in, o);
        }
        if ((mode & EXCLUSIVE_OUTPUTS) != 0) {
          for (UInt i = 0; i < inputs; ++i)
            if (i != in) visit(i, out);
        }
      }

      /** @brief One block: every output is the sum of the inputs connected
       *         to it, each times its gain. Returns ``output(0)``.
       *
       *  @p inputs points at ``inputs()`` buffers, input 0 first; a null
       *  pointer is a silent input (its ramps still run). As long as the
       *  shortest input, ``STANDARD_BUFFERSIZE`` when every input is null. */
      buffer& operator()(buffer* const* inputs);

      /** @brief Output @p index, as the last call left it. An index past the
       *         last output reads the last output. */
      buffer& output(UInt index);

    private:
      struct crosspoint {
        Flt gain = 0.f; // now
        Flt start = 0.f; // where the ramp began
        Flt goal = 0.f; // where it is heading
        Flt ramp = 0.f; // this ramp's time in ms
        Dbl phase = 1.0; // 0 to 1 through the ramp
        bool moving = false;
      };

      crosspoint& At(UInt in, UInt out) {
        return cells[(out * ins) + in];
      }
      const crosspoint& At(UInt in, UInt out) const {
        return cells[(out * ins) + in];
      }
      // Starts the crosspoint heading for `goal` over `rampMs`.
      void Head(UInt in, UInt out, Flt goal, Flt rampMs);

      UInt ins;
      UInt outs;
      Flt ramp = DEFAULT_RAMP;
      Flt defGain;
      Int mode = EXCLUSIVE_OFF;
      // The number of crosspoints ramping.
      UInt moving = 0;
      // Output-major: an output's crosspoints are next to each other.
      std::vector<crosspoint> cells;
      std::vector<buffer> outputBuffers;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_ROUTING_MIXMATRIX_HPP
