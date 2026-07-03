/*
  ==============================================================================

    ADSRenvelope.hpp
    Created: 26 Jul 2015 3:25:02pm
    Author:  yvan

  ==============================================================================
*/

#ifndef ADSRENVELOPE_HPP_INCLUDED
#define ADSRENVELOPE_HPP_INCLUDED

#include "../classes.hpp"
#include "../headers/defines.hpp"
#include "buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Playable ADSR-style envelope with optional sustain loop.
     *
     *  Built from a sequence of timed breakpoints; the envelope can be driven
     *  through attack / sustain / release phases at runtime. Use to shape note
     *  amplitudes in synthesisers or to gate effects.
     */
    class API ADSRenvelope {
    public:
      /**
       *  @brief Envelope vertex.
       *
       *  @param time      Time in seconds.
       *  @param value     Value at this time.
       *  @param coef      Curvature toward this point (0 = linear).
       *  @param loopStart Marks the beginning of the sustain loop region.
       *  @param loopEnd   Marks the end of the sustain loop region.
       */
      struct breakPoint {
        breakPoint(Flt time, Flt value, Flt coef, Bool loopStart = false, Bool loopEnd = false)
          : time(time), value(value), coef(coef), loopStart(loopStart), loopEnd(loopEnd) {}
        Flt time, value, coef;
        Bool loopStart, loopEnd;
      };

      /** @brief Playback phases driven by ``operator()``. */
      enum STATE {
        ATTACK, ///< Advance through the envelope from the start.
        RESUME, ///< Loop within the sustain region (between loopStart and loopEnd).
        RELEASE, ///< Advance from the current position to the end.
      };

      /**
       *  @brief Append a breakpoint.
       *
       *  @warning Breakpoints must be added in ascending time order — the
       *           class does not sort them.
       */
      void addPoint(const breakPoint& point);

      /**
       *  @brief Compile the breakpoints into the runtime envelope.
       *
       *  Call once after all ``addPoint`` calls and before invoking
       *  ``operator()``.
       */
      void generate();

      /** @brief Produce the next envelope block for the given phase. */
      YSE::DSP::buffer& operator()(STATE state, UInt length = STANDARD_BUFFERSIZE);

      /** @brief Save the breakpoints to a file. */
      void saveToFile(const char* fileName);

      /** @brief Whether the release phase has finished playing. */
      inline Bool isAtEnd() {
        return endReached;
      }

    private:
      std::vector<breakPoint> breakPoints;
      buffer envelope, result;
      Flt* phase;
      Flt* loopStart;
      Flt* loopEnd;
      Flt* envelopeEnd;
      Bool looping;
      Bool endReached;
    };

  } // namespace DSP
} // namespace YSE

#endif // ADSRENVELOPE_HPP_INCLUDED
