/*
  ==============================================================================

    misc.h
    Created: 29 Jan 2014 11:11:59pm
    Author:  yvan

  ==============================================================================
*/

#ifndef MISC_H_INCLUDED
#define MISC_H_INCLUDED

#include <cstdlib>
#include <time.h> 
#include <cmath>
#include "../headers/types.hpp"

namespace YSE {

  /**
   *  @brief Clamp ``x`` to the inclusive range [``min``, ``max``].
   *
   *  Templated so it works on any types that compare with ``<``. The result
   *  is written back into ``x``.
   */
  template<typename T0, typename T1, typename T2>
  inline void  Clamp(T0  &x, T1  min, T2  max) { if (x<min)x = min; else if (x>max)x = max; }

  /** @brief Seed the random generator from the current time. Call once at startup. */
  inline void Randomize() { srand(static_cast<UInt>(::time(nullptr))); }

  /** @brief Random integer in [0, ``max``). */
  inline Int Random(Int max) { return rand() % max; } // NOSONAR S3518: caller-side precondition (max > 0) — same contract as std::uniform_int_distribution

  /** @brief Random integer in [``min``, ``max``). */
  inline Int Random(Int min, Int max) { return min + (rand() % (max - min)); } // NOSONAR S3518: caller-side precondition (max > min)

  /** @brief Heavy-tailed random integer biased toward 0; produces fewer high values than ``Random``. */
  inline Int BigRandom(Int max) {
    max = static_cast<Int>(sqrt(max));
    return ((rand() % max) * (rand() % max)); // NOSONAR S3518: caller-side precondition (max > 0 so sqrt(max) >= 1)
  }

  /** @brief Random float in [0, 1]. */
  inline Flt RandomF() { return (float)rand() / (float)RAND_MAX; }

  /** @brief Random float in [0, ``max``]. */
  inline Flt RandomF(Flt max) { return (float)rand() / (float)RAND_MAX * max; }

  /** @brief Random float in [``min``, ``max``]. */
  inline Flt RandomF(Flt min, Flt max) { return min + ((float)rand() / (float)RAND_MAX * (max - min)); }

  /** @brief Random pointer in [``min``, ``max``), stride one ``Flt``. */
  inline Flt * Random(Flt * min, Flt * max) { return min + (rand() % (max - min)); }

  const Flt Pi_6 = 0.52359878f;        ///< PI/6 (30°).
  const Flt Pi_4 = 0.78539816f;        ///< PI/4 (45°).
  const Flt Pi_3 = 1.04719755f;        ///< PI/3 (60°).
  const Flt Pi_2 = 1.57079633f;        ///< PI/2 (90°).
  const Flt Pi = 3.14159265f;          ///< PI (180°).
  const Flt Pi2 = 6.28318531f;         ///< PI × 2 (360°).
  const Flt ToDegrees = 57.29577951f;  ///< Multiply radians by this to get degrees.
  const Flt ToRadians = 0.017453293f;  ///< Multiply degrees by this to get radians.

  const Flt Sqrt2 = 1.4142135623730950f;    ///< √2.
  const Flt Sqrt3 = 1.7320508075688773f;    ///< √3.
  const Flt Sqrt2_2 = 0.7071067811865475f;  ///< √2 / 2.
  const Flt Sqrt3_3 = 0.5773502691896257f;  ///< √3 / 3.
}




#endif  // MISC_H_INCLUDED
