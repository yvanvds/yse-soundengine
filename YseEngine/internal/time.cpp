/*
  ==============================================================================

    time.cpp
    Created: 2 Feb 2014 12:03:16pm
    Author:  yvan

  ==============================================================================
*/

#include "time.h"

YSE::INTERNAL::time& YSE::INTERNAL::Time() {
  static time t;
  return t;
}

YSE::INTERNAL::time& YSE::INTERNAL::DeviceTime() {
  static time t;
  return t;
}

void YSE::INTERNAL::time::update() {
  // Monotonic wall time (issue #667). RT-safe: no allocation, no lock — see the
  // class comment in time.h for why this is not std::clock() any more.
  const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  if (!started) {
    // Seeding tick: steady_clock's epoch is unspecified, so there is nothing to
    // measure against yet. Report no elapsed time rather than an epoch-sized one.
    started = true;
    last = now;
    d = 0.0f;
    return;
  }
  // Difference first, then convert: the subtraction is exact in the clock's own
  // integer duration, and only the (small) tick length is rounded to Flt.
  d = static_cast<Flt>(std::chrono::duration<Dbl>(now - last).count());
  last = now;
}

Flt YSE::INTERNAL::time::delta() {
  return d;
}