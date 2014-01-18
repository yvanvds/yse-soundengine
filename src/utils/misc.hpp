#pragma once
#include <stdlib.h>
#include "../headers/defines.hpp"

#ifdef WINDOWS
#pragma warning( disable : 4244 4018 )
#endif

namespace YSE {
  template<typename T0, typename T1, typename T2>
  inline API void  Clamp(T0  &x, T1  min, T2  max) { if(x<min)x=min;else if(x>max)x=max; }
  inline API Int Random(Int max) { return rand() % max; }

  const Flt Pi_6 = 0.52359878f; // PI/6 ( 30 deg)
  const Flt Pi_4 = 0.78539816f; // PI/4 ( 45 deg)
  const Flt Pi_3 = 1.04719755f; // PI/3 ( 60 deg)
  const Flt Pi_2 = 1.57079633f; // PI/2 ( 90 deg)
  const Flt Pi   = 3.14159265f; // PI   (180 deg)
  const Flt Pi2  = 6.28318531f; // PI*2 (360 deg)
  const Flt ToDegrees  = 57.29577951f;
  const Flt ToRadians  = 0.017453293f;

  const Flt Sqrt2   = 1.4142135623730950; // Sqrt(2)
  const Flt Sqrt3   = 1.7320508075688773; // Sqrt(3)
  const Flt Sqrt2_2 = 0.7071067811865475; // Sqrt(2)/2
  const Flt Sqrt3_3 = 0.5773502691896257; // Sqrt(3)/3
}

