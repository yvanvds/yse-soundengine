/*
  ==============================================================================

    envelope.hpp
    Created: 18 Jul 2015 6:54:05pm
    Author:  yvan

  ==============================================================================
*/

#ifndef ENVELOPE_H_INCLUDED
#define ENVELOPE_H_INCLUDED

#include "../classes.hpp"
#include "../headers/defines.hpp"
#include <vector>

namespace YSE {
  namespace DSP {

    /**
     *  @brief Time-value envelope (breakpoint shape).
     *
     *  Extracts an amplitude envelope from a source buffer or loads one from a
     *  saved breakpoint file. The resulting envelope can be applied to other
     *  buffers via ``drawableBuffer::applyEnvelope`` or saved back to disk.
     */
    class API envelope {
    public:
      /** @brief One vertex of the envelope: a value at a given time in seconds. */
      struct breakPoint {
        breakPoint(Flt time, Flt value) : time(time), value(value) {}
        Flt time;
        Flt value;
      };

      /**
       *  @brief Extract an envelope from an audio buffer.
       *
       *  @param source     Audio data to analyse.
       *  @param windowSize Analysis window in milliseconds. Larger windows
       *                    produce a smoother envelope at the cost of
       *                    responsiveness.
       *  @return ``true`` on success.
       */
      bool create(YSE::DSP::buffer& source, Int windowSize = 15);

      /** @brief Load a previously saved breakpoint file. */
      bool create(const char* fileName);

      /** @brief Scale every value so the peak equals 1.0. */
      void normalize();

      /** @brief Save the breakpoint set to a file.
       *  @return ``true`` on success, ``false`` on write error.
       */
      bool saveToFile(const char* fileName) const;

      /** @brief Number of breakpoints. */
      UInt elms() const;

      /** @brief Total envelope length in seconds. */
      Flt getLengthSec() const;

      /** @brief Access a breakpoint by index. */
      const breakPoint& operator[](UInt pos) const;

    private:
      std::vector<breakPoint> breakPoints;
    };

  } // namespace DSP
} // namespace YSE

#endif // ENVELOPE_H_INCLUDED
