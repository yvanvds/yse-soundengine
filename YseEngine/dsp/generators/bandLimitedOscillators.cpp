#include "bandLimitedOscillators.hpp"

#include <cmath>

namespace {

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  inline UInt Shortest(UInt a, UInt b) {
    return a < b ? a : b;
  }

  // Wrap into [0, 1). The guard catches the one input where rounding lands
  // exactly on 1: a value a hair below 0.
  inline Dbl Wrap(Dbl p) {
    p -= std::floor(p);
    return p >= 1.0 ? 0.0 : p;
  }

  // The double phase is below 1, but the nearest float may be 1.0f.
  inline Flt ToPhase(Dbl p) {
    const Flt v = static_cast<Flt>(p);
    return v < 1.0f ? v : 0.99999994f;
  }

  // The phase step per sample for a frequency in cycles per sample: held to
  // half a cycle (Nyquist) either way; not a number holds the phase.
  inline Dbl Step(Dbl cyclesPerSample) {
    if (cyclesPerSample != cyclesPerSample) return 0.0; // NaN
    if (cyclesPerSample > 0.5) return 0.5; // also +infinity
    if (cyclesPerSample < -0.5) return -0.5;
    return cyclesPerSample;
  }

  // Signed phase distance from an edge at `edge`, wrapped into [-0.5, 0.5).
  inline Dbl Distance(Dbl p, Dbl edge) {
    Dbl d = p - edge;
    if (d >= 0.5)
      d -= 1.0;
    else if (d < -0.5)
      d += 1.0;
    return d;
  }

  // polyBLEP: the band-limited step minus the naive one, for a unit jump of
  // the waveform (as a function of phase) at distance `d`, with the phase
  // moving `adt` (> 0) per sample. Two samples wide; the same expression
  // holds whichever way the phase runs.
  inline Dbl Blep(Dbl d, Dbl adt) {
    const Dbl x = d / adt;
    if (x >= 0.0 && x < 1.0) return -0.5 * (1.0 - x) * (1.0 - x);
    if (x < 0.0 && x > -1.0) return 0.5 * (1.0 + x) * (1.0 + x);
    return 0.0;
  }

  // polyBLAMP: the integral of the residual above, for a corner where the
  // slope (per sample) rises by 1. Symmetric, so the direction drops out.
  inline Dbl Blamp(Dbl d, Dbl adt) {
    const Dbl x = std::fabs(d) / adt;
    if (x >= 1.0) return 0.0;
    const Dbl r = 1.0 - x;
    return r * r * r / 6.0;
  }

  // ─── the waveforms, as a function of phase ────────────────────────────────

  struct sawWave {
    static Dbl Shape(Flt) {
      return 0.0;
    }
    static Dbl Value(Dbl p, Dbl adt, Dbl) {
      Dbl v = 2.0 * p - 1.0;
      // At phase 0 the ramp jumps from 1 to -1: a jump of -2.
      if (adt > 0.0) v -= 2.0 * Blep(Distance(p, 0.0), adt);
      return v;
    }
  };

  struct rectWave {
    static Dbl Shape(Flt width) {
      if (!std::isfinite(width)) return 0.5;
      return Wrap(static_cast<Dbl>(width));
    }
    static Dbl Value(Dbl p, Dbl adt, Dbl width) {
      Dbl v = p < width ? 1.0 : -1.0;
      // Up by 2 at phase 0, down by 2 at the width; at width 0 they cancel.
      if (adt > 0.0) v += 2.0 * (Blep(Distance(p, 0.0), adt) - Blep(Distance(p, width), adt));
      return v;
    }
  };

  struct triWave {
    static Dbl Shape(Flt duty) {
      if (!std::isfinite(duty)) return 0.5;
      const Dbl d = Wrap(static_cast<Dbl>(duty));
      return d < 0.05 ? 0.05 : (d > 0.95 ? 0.95 : d);
    }
    static Dbl Value(Dbl p, Dbl adt, Dbl duty) {
      Dbl v = p < duty ? -1.0 + 2.0 * p / duty : 1.0 - 2.0 * (p - duty) / (1.0 - duty);
      if (adt > 0.0) {
        // The slope per cycle turns from -2/(1-duty) to 2/duty at phase 0
        // and back at the duty: a change of 2/(duty*(1-duty)), which is
        // adt times that per sample.
        const Dbl turn = 2.0 / (duty * (1.0 - duty)) * adt;
        v += turn * (Blamp(Distance(p, 0.0), adt) - Blamp(Distance(p, duty), adt));
      }
      return v;
    }
  };

  // ─── the per-sample inputs ────────────────────────────────────────────────

  struct constantStep {
    Dbl step;
    Dbl operator[](UInt) const {
      return step;
    }
  };

  struct signalStep {
    const Flt* values;
    Dbl toCycles;
    Dbl operator[](UInt i) const {
      return Step(static_cast<Dbl>(values[i]) * toCycles);
    }
  };

  struct constantShape {
    Flt value;
    Flt operator[](UInt) const {
      return value;
    }
  };

  struct signalShape {
    const Flt* values;
    Flt operator[](UInt i) const {
      return values[i];
    }
  };

  struct syncInput {
    const Flt* values; // null: no sync
    Flt threshold;
    Flt& last;
    bool& primed;

    // A rise across the threshold from the previous sample. The very first
    // sample has no previous one and never resets.
    bool Rises(UInt i) {
      const Flt now = values[i];
      const bool rises = primed && last < threshold && now >= threshold;
      last = now;
      primed = true;
      return rises;
    }
  };

  template <class Wave, class StepSource, class ShapeSource>
  void Run(Dbl& phase, StepSource step, ShapeSource shape, syncInput sync, Flt* out, UInt length) {
    Dbl p = phase;
    for (UInt i = 0; i < length; ++i) {
      if (sync.values != nullptr && sync.Rises(i)) p = 0.0;
      const Dbl dt = step[i];
      out[i] = static_cast<Flt>(Wave::Value(p, std::fabs(dt), Wave::Shape(shape[i])));
      p += dt;
      if (p >= 1.0)
        p -= 1.0;
      else if (p < 0.0)
        p += 1.0;
      if (p >= 1.0) p = 0.0; // -tiny + 1 rounds to 1
    }
    phase = p;
  }

  inline Dbl ToCycles() {
    return 1.0 / static_cast<Dbl>(YSE::SAMPLERATE);
  }

  inline void Size(YSE::DSP::buffer& output, UInt length) {
    // Within the capacity set in the constructor: no allocation.
    if (output.getLength() != length) output.resize(length);
  }

} // namespace

namespace YSE {
  namespace DSP {

    // ─── blSaw ──────────────────────────────────────────────────────────────

    blSaw::blSaw()
      : current(0.0),
        threshold(0.5f),
        lastSync(0.f),
        syncPrimed(false),
        output(STANDARD_BUFFERSIZE) {}

    blSaw& blSaw::phase(Flt value) {
      current = std::isfinite(value) ? Wrap(static_cast<Dbl>(value)) : 0.0;
      return *this;
    }

    Flt blSaw::phase() const {
      return ToPhase(current);
    }

    blSaw& blSaw::syncThreshold(Flt value) {
      threshold = value;
      return *this;
    }

    Flt blSaw::syncThreshold() const {
      return threshold;
    }

    buffer& blSaw::operator()(Flt frequency, UInt length) {
      length = Clamp(length);
      Size(output, length);
      Run<sawWave>(current, constantStep{Step(static_cast<Dbl>(frequency) * ToCycles())},
                   constantShape{0.f}, syncInput{nullptr, threshold, lastSync, syncPrimed},
                   output.getPtr(), length);
      return output;
    }

    buffer& blSaw::operator()(buffer& frequency) {
      const UInt length = Clamp(frequency.getLength());
      Size(output, length);
      Run<sawWave>(current, signalStep{frequency.getPtr(), ToCycles()}, constantShape{0.f},
                   syncInput{nullptr, threshold, lastSync, syncPrimed}, output.getPtr(), length);
      return output;
    }

    buffer& blSaw::operator()(buffer& frequency, buffer& sync) {
      const UInt length = Clamp(Shortest(frequency.getLength(), sync.getLength()));
      Size(output, length);
      Run<sawWave>(current, signalStep{frequency.getPtr(), ToCycles()}, constantShape{0.f},
                   syncInput{sync.getPtr(), threshold, lastSync, syncPrimed}, output.getPtr(),
                   length);
      return output;
    }

    // ─── blRect ─────────────────────────────────────────────────────────────

    blRect::blRect()
      : current(0.0),
        pulseWidth(0.5f),
        threshold(0.5f),
        lastSync(0.f),
        syncPrimed(false),
        output(STANDARD_BUFFERSIZE) {}

    blRect& blRect::width(Flt value) {
      pulseWidth = value;
      return *this;
    }

    Flt blRect::width() const {
      return pulseWidth;
    }

    blRect& blRect::phase(Flt value) {
      current = std::isfinite(value) ? Wrap(static_cast<Dbl>(value)) : 0.0;
      return *this;
    }

    Flt blRect::phase() const {
      return ToPhase(current);
    }

    blRect& blRect::syncThreshold(Flt value) {
      threshold = value;
      return *this;
    }

    Flt blRect::syncThreshold() const {
      return threshold;
    }

    buffer& blRect::operator()(Flt frequency, UInt length) {
      length = Clamp(length);
      Size(output, length);
      Run<rectWave>(current, constantStep{Step(static_cast<Dbl>(frequency) * ToCycles())},
                    constantShape{pulseWidth}, syncInput{nullptr, threshold, lastSync, syncPrimed},
                    output.getPtr(), length);
      return output;
    }

    buffer& blRect::operator()(buffer& frequency) {
      const UInt length = Clamp(frequency.getLength());
      Size(output, length);
      Run<rectWave>(current, signalStep{frequency.getPtr(), ToCycles()}, constantShape{pulseWidth},
                    syncInput{nullptr, threshold, lastSync, syncPrimed}, output.getPtr(), length);
      return output;
    }

    buffer& blRect::operator()(buffer& frequency, buffer& width) {
      const UInt length = Clamp(Shortest(frequency.getLength(), width.getLength()));
      Size(output, length);
      Run<rectWave>(current, signalStep{frequency.getPtr(), ToCycles()},
                    signalShape{width.getPtr()},
                    syncInput{nullptr, threshold, lastSync, syncPrimed}, output.getPtr(), length);
      return output;
    }

    buffer& blRect::operator()(buffer& frequency, buffer& width, buffer& sync) {
      const UInt length =
          Clamp(Shortest(Shortest(frequency.getLength(), width.getLength()), sync.getLength()));
      Size(output, length);
      Run<rectWave>(
          current, signalStep{frequency.getPtr(), ToCycles()}, signalShape{width.getPtr()},
          syncInput{sync.getPtr(), threshold, lastSync, syncPrimed}, output.getPtr(), length);
      return output;
    }

    // ─── blTri ──────────────────────────────────────────────────────────────

    blTri::blTri()
      : current(0.0),
        dutyCycle(0.5f),
        threshold(0.5f),
        lastSync(0.f),
        syncPrimed(false),
        output(STANDARD_BUFFERSIZE) {}

    blTri& blTri::duty(Flt value) {
      dutyCycle = value;
      return *this;
    }

    Flt blTri::duty() const {
      return dutyCycle;
    }

    blTri& blTri::phase(Flt value) {
      current = std::isfinite(value) ? Wrap(static_cast<Dbl>(value)) : 0.0;
      return *this;
    }

    Flt blTri::phase() const {
      return ToPhase(current);
    }

    blTri& blTri::syncThreshold(Flt value) {
      threshold = value;
      return *this;
    }

    Flt blTri::syncThreshold() const {
      return threshold;
    }

    buffer& blTri::operator()(Flt frequency, UInt length) {
      length = Clamp(length);
      Size(output, length);
      Run<triWave>(current, constantStep{Step(static_cast<Dbl>(frequency) * ToCycles())},
                   constantShape{dutyCycle}, syncInput{nullptr, threshold, lastSync, syncPrimed},
                   output.getPtr(), length);
      return output;
    }

    buffer& blTri::operator()(buffer& frequency) {
      const UInt length = Clamp(frequency.getLength());
      Size(output, length);
      Run<triWave>(current, signalStep{frequency.getPtr(), ToCycles()}, constantShape{dutyCycle},
                   syncInput{nullptr, threshold, lastSync, syncPrimed}, output.getPtr(), length);
      return output;
    }

    buffer& blTri::operator()(buffer& frequency, buffer& duty) {
      const UInt length = Clamp(Shortest(frequency.getLength(), duty.getLength()));
      Size(output, length);
      Run<triWave>(current, signalStep{frequency.getPtr(), ToCycles()}, signalShape{duty.getPtr()},
                   syncInput{nullptr, threshold, lastSync, syncPrimed}, output.getPtr(), length);
      return output;
    }

    buffer& blTri::operator()(buffer& frequency, buffer& duty, buffer& sync) {
      const UInt length =
          Clamp(Shortest(Shortest(frequency.getLength(), duty.getLength()), sync.getLength()));
      Size(output, length);
      Run<triWave>(current, signalStep{frequency.getPtr(), ToCycles()}, signalShape{duty.getPtr()},
                   syncInput{sync.getPtr(), threshold, lastSync, syncPrimed}, output.getPtr(),
                   length);
      return output;
    }

  } // namespace DSP
} // namespace YSE
