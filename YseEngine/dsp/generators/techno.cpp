#include "techno.hpp"

#include <algorithm>
#include <cmath>

namespace {

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // f^e for f in 0..1 and e >= 0, with 0^0 = 1, and the common exponents
  // without a pow.
  inline Flt Shape(Flt f, Flt e) {
    if (e == 1.f) return f;
    if (e == 0.f) return 1.f;
    return std::pow(f, e);
  }

  inline Flt Exponent(Flt value) {
    return value < 0.f ? 0.f : value;
  }

} // namespace

namespace YSE {
  namespace DSP {

    techno::techno(UInt capacity)
      : output(STANDARD_BUFFERSIZE),
        envelopeOutput(STANDARD_BUFFERSIZE),
        positionOutput(STANDARD_BUFFERSIZE) {
      if (capacity < 1) capacity = 1;
      if (capacity > MAX_STEPS) capacity = MAX_STEPS;
      pitches.assign(capacity, 440.f);
      amplitudes.assign(capacity, 1.f);
      curves.assign(capacity, 0.f);
      attacks.assign(capacity, 1.f);
      decays.assign(capacity, 1.f);
      starts.assign(capacity, 0.f);
      Space();
    }

    void techno::Space() {
      for (UInt i = 0; i < used; ++i)
        starts[i] = static_cast<Flt>(i) / static_cast<Flt>(used);
    }

    techno& techno::length(UInt steps) {
      if (steps < 1) steps = 1;
      if (steps > capacity()) steps = capacity();
      used = steps;
      Space();
      return *this;
    }

    techno& techno::pitch(UInt step, Flt hz) {
      if (step < capacity() && std::isfinite(hz)) pitches[step] = hz;
      return *this;
    }

    Flt techno::pitch(UInt step) const {
      return step < capacity() ? pitches[step] : 0.f;
    }

    techno& techno::amplitude(UInt step, Flt value) {
      if (step < capacity() && std::isfinite(value)) amplitudes[step] = value;
      return *this;
    }

    Flt techno::amplitude(UInt step) const {
      return step < capacity() ? amplitudes[step] : 0.f;
    }

    techno& techno::curve(UInt step, Flt exponent) {
      if (step < capacity() && std::isfinite(exponent)) curves[step] = Exponent(exponent);
      return *this;
    }

    Flt techno::curve(UInt step) const {
      return step < capacity() ? curves[step] : 0.f;
    }

    techno& techno::attack(UInt step, Flt exponent) {
      if (step < capacity() && std::isfinite(exponent)) attacks[step] = Exponent(exponent);
      return *this;
    }

    Flt techno::attack(UInt step) const {
      return step < capacity() ? attacks[step] : 0.f;
    }

    techno& techno::decay(UInt step, Flt exponent) {
      if (step < capacity() && std::isfinite(exponent)) decays[step] = Exponent(exponent);
      return *this;
    }

    Flt techno::decay(UInt step) const {
      return step < capacity() ? decays[step] : 0.f;
    }

    techno& techno::position(UInt step, Flt start) {
      if (step >= used || std::isnan(start)) return *this;
      const Flt low = step == 0 ? 0.f : starts[step - 1];
      const Flt high = step + 1 < used ? starts[step + 1] : 1.f;
      starts[step] = std::min(std::max(start, low), high);
      return *this;
    }

    Flt techno::position(UInt step) const {
      return step < used ? starts[step] : 0.f;
    }

    bool techno::repeatPositions(const Flt* pattern, UInt count) {
      const UInt segment = count + 1;
      if (used % segment != 0) return false;
      Flt previous = 0.f;
      for (UInt i = 0; i < count; ++i) {
        const Flt v = pattern[i];
        if (!(v > previous) || !(v < 1.f)) return false;
        previous = v;
      }
      const UInt segments = used / segment;
      for (UInt s = 0; s < segments; ++s) {
        for (UInt j = 0; j < segment; ++j) {
          const Flt within = j == 0 ? 0.f : pattern[j - 1];
          starts[s * segment + j] = (static_cast<Flt>(s) + within) / static_cast<Flt>(segments);
        }
      }
      return true;
    }

    buffer& techno::operator()(buffer& phase) {
      const UInt length = Clamp(phase.getLength());
      // Within the capacity set in the constructor: no allocation.
      if (output.getLength() != length) output.resize(length);
      if (envelopeOutput.getLength() != length) envelopeOutput.resize(length);
      if (positionOutput.getLength() != length) positionOutput.resize(length);
      const Flt* in = phase.getPtr();
      Flt* pitchOut = output.getPtr();
      Flt* envOut = envelopeOutput.getPtr();
      Flt* posOut = positionOutput.getPtr();

      const UInt n = used;
      const Flt* first = starts.data();
      const Flt* last = first + n;

      for (UInt i = 0; i < length; ++i) {
        Flt p = in[i];
        if (std::isnan(p) || p < 0.f) p = 0.f;
        if (p > 1.f) p = 1.f;

        // The last step starting at or before the phase; before the first
        // step's start, the last step, still running from the wrap.
        UInt k = 0;
        Flt begin = 0.f;
        Flt end = 0.f;
        if (p < first[0]) {
          k = n - 1;
          begin = first[k] - 1.f;
          end = first[0];
        } else {
          k = static_cast<UInt>(std::upper_bound(first, last, p) - first) - 1;
          begin = first[k];
          end = k + 1 < n ? first[k + 1] : first[0] + 1.f;
        }
        const Flt span = end - begin;
        Flt f = span > 0.f ? (p - begin) / span : 0.f;
        if (f < 0.f) f = 0.f;
        if (f > 1.f) f = 1.f;

        const UInt prev = k == 0 ? n - 1 : k - 1;
        const UInt next = k + 1 < n ? k + 1 : 0;
        pitchOut[i] = pitches[prev] + (pitches[k] - pitches[prev]) * Shape(f, curves[k]);
        envOut[i] =
            amplitudes[k] * Shape(1.f - f, decays[k]) + amplitudes[next] * Shape(f, attacks[next]);
        posOut[i] = static_cast<Flt>(k) + f;
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
