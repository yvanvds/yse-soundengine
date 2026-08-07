/*
  ==============================================================================

    time.h
    Created: 30 Jan 2014 5:31:26pm
    Author:  yvan

  ==============================================================================
*/

#ifndef TIME_H_INCLUDED
#define TIME_H_INCLUDED

#include <chrono>
#include "../headers/types.hpp"

namespace YSE {
  namespace INTERNAL {

    /** Elapsed-time source for the engine's update ticks.

        Backed by std::chrono::steady_clock: monotonic *wall* time. The clock
        used to be std::clock(), which measures *processor* time — on glibc that
        is CLOCK_PROCESS_CPUTIME_ID, the sum of CPU time across every engine
        thread, so it ran fast while the engine was busy and stood nearly still
        while it idled — and was quantised to CLOCKS_PER_SEC (1 ms on the MSVC /
        MSYS2 runtimes), far coarser than the audio callback period it measures.
        Every consumer treats delta() as elapsed seconds, so both of those were
        wrong quantities (issue #667). steady_clock is sub-microsecond on all
        supported platforms and cannot jump when the system clock is adjusted.

        update() runs on the audio callback, so it must stay RT-safe:
        steady_clock::now() is a vDSO clock_gettime on Linux/Android and a
        QueryPerformanceCounter on Windows — no allocation, no lock, no syscall
        in the common case.
    */
    class time {
    public:
      /** Close the current tick and start a new one. The first call after
          construction only seeds the reference point and reports a delta of 0:
          steady_clock's epoch is unspecified (system boot, in practice), so a
          delta measured against a default-constructed time_point would be days
          long and would blow through every timer that accumulates delta(). */
      void update();

      /** Length of the last tick in seconds. Never negative; 0 for a tick that
          measured no time (the seeding tick, or two updates inside one clock
          period). Callers must not divide by it unguarded — see
          DSP::panner::computeVelocity and issue #660. */
      Flt delta();

      time() : last(), started(false), d(0.0f) {}

    private:
      std::chrono::steady_clock::time_point last;
      bool started; // false until the first update() seeds `last`
      Flt d; // delta
    };

    time& Time(); // updates every time update is called
    time& DeviceTime(); // updates every time the devices asks for a buffer
  } // namespace INTERNAL
} // namespace YSE

#endif // TIME_H_INCLUDED
