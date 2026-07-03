/*
  ==============================================================================

    denormalGuard.h
    Flush-to-zero / denormals-are-zero for the audio thread.

    Without this, any IIR feedback path (one-pole filters, reverb tails) can
    decay into subnormal floats which slow x86/ARM FPU arithmetic by 10-100x,
    pegging the audio callback at high CPU even when the graph is effectively
    silent. See issue #53.

    Call enableFlushToZero() once at the top of every audio callback. It is a
    single MXCSR / FPCR write (per-thread state). Re-setting on every callback
    is safe and keeps us robust to backends that recycle callback threads.

  ==============================================================================
*/

#ifndef DENORMAL_GUARD_H_INCLUDED
#define DENORMAL_GUARD_H_INCLUDED

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <pmmintrin.h>
#include <xmmintrin.h>
#elif defined(__aarch64__) || defined(__arm__)
#include <cstdint>
#endif

namespace YSE {
  namespace INTERNAL {

    inline void enableFlushToZero() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
      _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
      _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#elif defined(__aarch64__)
      std::uint64_t fpcr;
      asm volatile("mrs %0, fpcr" : "=r"(fpcr));
      fpcr |= (std::uint64_t(1) << 24);
      asm volatile("msr fpcr, %0" : : "r"(fpcr));
#elif defined(__arm__)
      std::uint32_t fpscr;
      asm volatile("vmrs %0, fpscr" : "=r"(fpscr));
      fpscr |= (std::uint32_t(1) << 24);
      asm volatile("vmsr fpscr, %0" : : "r"(fpscr));
#endif
    }

  } // namespace INTERNAL
} // namespace YSE

#endif
