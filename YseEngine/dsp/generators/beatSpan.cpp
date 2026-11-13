#include "beatSpan.hpp"

#include <cmath>

namespace YSE {
  namespace DSP {

    beatSpan& beatSpan::read(Dbl beat, Flt tempo, UInt length) {
      if (!std::isfinite(beat) || !std::isfinite(tempo)) return lose();
      if (length > STANDARD_BUFFERSIZE) length = STANDARD_BUFFERSIZE;
      if (length < 1) length = 1;

      // What the clock moves in this block at its tempo.
      const Dbl expected = static_cast<Dbl>(tempo) * static_cast<Dbl>(length) /
                           (60.0 * static_cast<Dbl>(SAMPLERATE));
      bool continues = false;
      if (hasReading) {
        // A glide moves the clock by something between its old and its new
        // tempo; skipped blocks move it by several blocks' worth.
        const Dbl moved = beat - endBeat;
        const Dbl scale =
            std::fabs(expected) > std::fabs(lastStep) ? std::fabs(expected) : std::fabs(lastStep);
        continues = std::fabs(moved - expected) <= 0.75 * scale + 1e-12;
      }
      startBeat = continues ? endBeat : beat - expected;
      endBeat = beat;
      lastStep = endBeat - startBeat;
      bpm = tempo;
      samples = length;
      hasReading = true;
      return *this;
    }

    beatSpan& beatSpan::lose() {
      hasReading = false;
      lastStep = 0.0;
      bpm = 0.f;
      return *this;
    }

  } // namespace DSP
} // namespace YSE
