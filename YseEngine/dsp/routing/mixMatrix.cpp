#include "mixMatrix.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
  using YSE::DSP::buffer;

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // Within the capacity set in the constructor: no allocation.
  inline void Fit(buffer& b, UInt length) {
    if (b.getLength() != length) b.resize(length);
  }

  // The length of a block: the shortest of the inputs there are.
  UInt BlockLength(buffer* const* inputs, UInt count) {
    UInt length = YSE::STANDARD_BUFFERSIZE;
    for (UInt c = 0; c < count; ++c) {
      if (inputs[c] != nullptr) length = std::min(length, inputs[c]->getLength());
    }
    return length;
  }
} // namespace

namespace YSE {
  namespace DSP {

    mixMatrix::mixMatrix(UInt inputs, UInt outputs, Flt defaultGain)
      : ins(std::clamp<UInt>(inputs, 1, MAX_CHANNELS)),
        outs(std::clamp<UInt>(outputs, 1, MAX_CHANNELS)),
        defGain(std::isfinite(defaultGain) ? defaultGain : 1.f),
        cells(ins * outs),
        outputBuffers(outs) {
      for (buffer& b : outputBuffers)
        b = 0.f;
    }

    mixMatrix& mixMatrix::set(UInt in, UInt out, Flt gain) {
      return set(in, out, gain, ramp);
    }

    mixMatrix& mixMatrix::set(UInt in, UInt out, Flt gain, Flt rampMs) {
      if (in >= ins || out >= outs || !std::isfinite(gain)) return *this;
      if (!std::isfinite(rampMs)) rampMs = ramp;
      if (rampMs < 0.f) rampMs = 0.f;
      if (gain != 0.f) {
        Exclusions(mode, in, out, ins, outs, [this, rampMs](UInt i, UInt o) {
          if (At(i, o).goal != 0.f) Head(i, o, 0.f, rampMs);
        });
      }
      Head(in, out, gain, rampMs);
      return *this;
    }

    mixMatrix& mixMatrix::connect(UInt in, UInt out) {
      return set(in, out, defGain);
    }

    mixMatrix& mixMatrix::disconnect(UInt in, UInt out) {
      return set(in, out, 0.f);
    }

    mixMatrix& mixMatrix::clear() {
      for (UInt out = 0; out < outs; ++out) {
        for (UInt in = 0; in < ins; ++in)
          Head(in, out, 0.f, ramp);
      }
      return *this;
    }

    mixMatrix& mixMatrix::rampTime(Flt ms) {
      if (std::isfinite(ms)) ramp = ms < 0.f ? 0.f : ms;
      return *this;
    }

    mixMatrix& mixMatrix::defaultGain(Flt gain) {
      if (std::isfinite(gain)) defGain = gain;
      return *this;
    }

    mixMatrix& mixMatrix::exclusive(Int value) {
      mode = std::clamp<Int>(value, EXCLUSIVE_OFF, EXCLUSIVE_BOTH);
      return *this;
    }

    Flt mixMatrix::gain(UInt in, UInt out) const {
      if (in >= ins || out >= outs) return 0.f;
      return At(in, out).gain;
    }

    Flt mixMatrix::target(UInt in, UInt out) const {
      if (in >= ins || out >= outs) return 0.f;
      return At(in, out).goal;
    }

    buffer& mixMatrix::output(UInt index) {
      return outputBuffers[index < outs ? index : outs - 1];
    }

    void mixMatrix::Head(UInt in, UInt out, Flt goal, Flt rampMs) {
      crosspoint& c = At(in, out);
      if (goal == c.goal) return;
      c.goal = goal;
      if (c.gain == goal) {
        // Already there, part way back from a ramp elsewhere: nothing moves.
        if (c.moving) {
          c.moving = false;
          moving--;
        }
        c.phase = 1.0;
        return;
      }
      c.start = c.gain;
      c.ramp = rampMs;
      c.phase = 0.0;
      if (!c.moving) {
        c.moving = true;
        moving++;
      }
    }

    buffer& mixMatrix::operator()(buffer* const* inputs) {
      const UInt length = Clamp(BlockLength(inputs, ins));
      // Samples per millisecond at the live rate, for this block's ramps.
      const Dbl perMs = static_cast<Dbl>(SAMPLERATE) / 1000.0;

      for (UInt out = 0; out < outs; ++out) {
        buffer& b = outputBuffers[out];
        Fit(b, length);
        Flt* y = b.getPtr();
        std::memset(y, 0, sizeof(Flt) * length);

        for (UInt in = 0; in < ins; ++in) {
          crosspoint& c = At(in, out);
          buffer* source = inputs[in];
          const Flt* x = source != nullptr ? source->getPtr() : nullptr;

          if (!c.moving) {
            // Steady: one multiply-add per sample, or nothing at all.
            if (c.gain == 0.f || x == nullptr) continue;
            const Flt g = c.gain;
            for (UInt i = 0; i < length; ++i)
              y[i] += x[i] * g;
            continue;
          }

          // Under one sample is a jump at the first sample.
          const Dbl samples = static_cast<Dbl>(c.ramp) * perMs;
          const Dbl step = samples > 1.0 ? 1.0 / samples : 1.0;
          const Flt span = c.goal - c.start;
          Dbl phase = c.phase;
          Flt g = c.gain;
          UInt i = 0;
          for (; i < length; ++i) {
            phase += step;
            if (phase >= 1.0) break;
            g = c.start + (span * static_cast<Flt>(phase));
            if (x != nullptr) y[i] += x[i] * g;
          }
          if (i < length) {
            // The ramp ends on this sample; the rest of the block is steady.
            g = c.goal;
            phase = 1.0;
            c.moving = false;
            moving--;
            if (x != nullptr && g != 0.f) {
              for (; i < length; ++i)
                y[i] += x[i] * g;
            }
          }
          c.phase = phase;
          c.gain = g;
        }
      }
      return outputBuffers[0];
    }

  } // namespace DSP
} // namespace YSE
