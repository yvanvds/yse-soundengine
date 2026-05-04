#pragma once
#include <cmath>
#include "dsp/buffer.hpp"

// Shared test helpers for buffer construction and comparison.
// Phases 3+ include this header as "support/audio_helpers.hpp"
// (Tests/ must be on the include path).

namespace TestHelpers {

inline YSE::DSP::buffer makeBuffer(unsigned size, float fillValue = 0.0f) {
    YSE::DSP::buffer b(size);
    b = fillValue;
    return b;
}

// Linear ramp from start to end, inclusive of start, approaching end.
inline YSE::DSP::buffer makeRampBuffer(unsigned size, float start, float end) {
    YSE::DSP::buffer b(size);
    float* ptr = b.getPtr();
    float denom = (size > 1) ? static_cast<float>(size - 1) : 1.0f;
    for (unsigned i = 0; i < size; ++i)
        ptr[i] = start + (end - start) * (static_cast<float>(i) / denom);
    return b;
}

inline bool buffersNearlyEqual(YSE::DSP::buffer& a, YSE::DSP::buffer& b, float eps = 1e-5f) {
    if (a.getLength() != b.getLength()) return false;
    float* pa = a.getPtr();
    float* pb = b.getPtr();
    for (unsigned i = 0; i < a.getLength(); ++i) {
        float diff = pa[i] - pb[i];
        if (diff < -eps || diff > eps) return false;
    }
    return true;
}

// Verifies that buf[i] ≈ buf[i + periodSamples] for every valid i.
// Returns false if the buffer is too short to contain two full periods.
inline bool checkPeriodicity(YSE::DSP::buffer& buf, unsigned periodSamples, float eps = 1e-3f) {
    if (buf.getLength() < 2 * periodSamples) return false;
    float* ptr = buf.getPtr();
    for (unsigned i = 0; i + periodSamples < buf.getLength(); ++i) {
        float diff = ptr[i] - ptr[i + periodSamples];
        if (diff < -eps || diff > eps) return false;
    }
    return true;
}

// Returns a buffer of the given size that is 1.0 at index 0 and 0.0 elsewhere.
inline YSE::DSP::buffer makeImpulse(unsigned size) {
    YSE::DSP::buffer b(size);
    b = 0.0f;
    b.getPtr()[0] = 1.0f;
    return b;
}

// Root-mean-square amplitude of the entire buffer.
inline float measureRms(YSE::DSP::buffer& b) {
    float* ptr = b.getPtr();
    float sum = 0.0f;
    unsigned n = b.getLength();
    for (unsigned i = 0; i < n; ++i) sum += ptr[i] * ptr[i];
    return (n > 0) ? std::sqrt(sum / static_cast<float>(n)) : 0.0f;
}

// Returns the bin index in [1, N/2] with maximum magnitude in an FFT output.
// real and im must point to arrays of at least N floats (output of fft or mayer_fft).
// Bin 0 (DC) is excluded; returns 1 if N < 4.
inline unsigned peakBinIndex(float* re, float* im, unsigned N) {
    if (N < 4) return 1;
    unsigned peak = 1;
    float maxMag = re[1] * re[1] + im[1] * im[1];
    for (unsigned k = 2; k <= N / 2; ++k) {
        float mag = re[k] * re[k] + im[k] * im[k];
        if (mag > maxMag) { maxMag = mag; peak = k; }
    }
    return peak;
}

} // namespace TestHelpers
