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
  template<typename T0, typename T1, typename T2>
  inline API void  Clamp(T0  &x, T1  min, T2  max) { if (x<min)x = min; else if (x>max)x = max; }
  inline API void Randomize() { srand(static_cast<UInt>(::time(nullptr))); }
  inline API Int Random(Int max) { return rand() % max; }
  inline API Int Random(Int min, Int max) { return min + (rand() % (max - min)); }
  inline API Int BigRandom(Int max) {
    max = static_cast<Int>(sqrt(max));
    return ((rand() % max) * (rand() % max));
  }

  inline API Flt RandomF() { return (float)rand() / (float)RAND_MAX; }
  inline API Flt RandomF(Flt max) { return (float)rand() / (float)RAND_MAX * max; }
  inline API Flt RandomF(Flt min, Flt max) { return min + ((float)rand() / (float)RAND_MAX * (max - min)); }

  inline API Flt * Random(Flt * min, Flt * max) { return min + (rand() % (max - min)); }

  const Flt Pi_6 = 0.52359878f; // PI/6 ( 30 deg)
  const Flt Pi_4 = 0.78539816f; // PI/4 ( 45 deg)
  const Flt Pi_3 = 1.04719755f; // PI/3 ( 60 deg)
  const Flt Pi_2 = 1.57079633f; // PI/2 ( 90 deg)
  const Flt Pi = 3.14159265f; // PI   (180 deg)
  const Flt Pi2 = 6.28318531f; // PI*2 (360 deg)
  const Flt ToDegrees = 57.29577951f;
  const Flt ToRadians = 0.017453293f;

  const Flt Sqrt2 = 1.4142135623730950f; // Sqrt(2)
  const Flt Sqrt3 = 1.7320508075688773f; // Sqrt(3)
  const Flt Sqrt2_2 = 0.7071067811865475f; // Sqrt(2)/2
  const Flt Sqrt3_3 = 0.5773502691896257f; // Sqrt(3)/3
}




#endif  // MISC_H_INCLUDED
