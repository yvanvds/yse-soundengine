#include "phasorSubdiv.hpp"
#include "phaseStep.hpp"
#include "whiteSource.hpp"

#include <cmath>

namespace {
  using YSE::DSP::buffer;

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // Within the capacity set in the constructor: no allocation.
  inline void Fit(buffer& b, UInt length) {
    if (b.getLength() != length) b.resize(length);
  }

  // The double ramp is below 1, but may round to 1.0f. Keep the output below
  // 1 too, at the largest float that is.
  inline Flt ToSample(Dbl p) {
    const Flt v = static_cast<Flt>(p);
    return v < 1.0f ? v : 0.99999994f;
  }

  inline int ClampDiv(int value) {
    if (value < 1) return 1;
    return value > YSE::DSP::phasorSubdiv::MAX_DIV ? YSE::DSP::phasorSubdiv::MAX_DIV : value;
  }

  // a mod b for b > 0, in 0 .. b - 1 also for a negative a.
  inline std::int64_t Mod(std::int64_t a, std::int64_t b) {
    const std::int64_t r = a % b;
    return r < 0 ? r + b : r;
  }

  // A draw in [0, 1) from the shared white source.
  inline Dbl Uniform(std::uint32_t& state) {
    return (YSE::DSP::detail::NextWhite(state) + 1.0) * 0.5;
  }
} // namespace

namespace YSE {
  namespace DSP {

    phasorSubdiv::phasorSubdiv()
      : output(STANDARD_BUFFERSIZE),
        indexOutput(STANDARD_BUFFERSIZE),
        state(detail::WHITE_DEFAULT_SEED) {
      Land();
    }

    phasorSubdiv& phasorSubdiv::div(int value) {
      value = ClampDiv(value);
      if (value != requested.subdivisions) {
        requested.subdivisions = value;
        pending = true;
      }
      return *this;
    }

    phasorSubdiv& phasorSubdiv::pattern(const int* steps, UInt count) {
      if (steps == nullptr) count = 0;
      if (count > MAX_PATTERN) count = MAX_PATTERN;
      for (UInt i = 0; i < count; ++i)
        requested.steps[i] = ClampDiv(steps[i]);
      requested.count = count;
      pending = true;
      return *this;
    }

    phasorSubdiv& phasorSubdiv::prob(const Flt* chances, UInt count) {
      if (chances == nullptr) count = 0;
      if (count > MAX_PATTERN) count = MAX_PATTERN;
      for (UInt i = 0; i < count; ++i) {
        const Flt p = chances[i];
        chance[i] = !std::isfinite(p) || p < 0.f ? 0.f : (p > 1.f ? 1.f : p);
      }
      probCount = count;
      return *this;
    }

    phasorSubdiv& phasorSubdiv::lockProb(bool on) {
      lockProbability = on;
      return *this;
    }

    phasorSubdiv& phasorSubdiv::silentMode(silent value) {
      silence = value;
      return *this;
    }

    phasorSubdiv& phasorSubdiv::syncUpdate(update value) {
      landing = value;
      return *this;
    }

    phasorSubdiv& phasorSubdiv::seed(std::uint32_t value) {
      state = value;
      return *this;
    }

    void phasorSubdiv::Land() {
      active = requested;
      const std::int64_t n = active.subdivisions;
      if (active.count == 0) {
        active.used = 0;
        active.units = n;
        active.cycles = 1;
      } else {
        std::int64_t sum = 0;
        for (UInt i = 0; i < active.count; ++i) {
          active.starts[i] = static_cast<int>(sum);
          sum += active.steps[i];
        }
        active.used = active.count;
        const std::int64_t rest = sum % n;
        if (rest != 0) {
          active.steps[active.used] = static_cast<int>(n - rest);
          active.starts[active.used] = static_cast<int>(sum);
          active.used++;
          sum += n - rest;
        }
        active.units = sum;
        active.cycles = sum / n;
      }
      pending = false;
      hasStep = false;
    }

    Flt phasorSubdiv::Step(Flt in) {
      if (!std::isfinite(in)) in = previous;
      Flt x = static_cast<Flt>(phaseStep::Wrap(static_cast<Dbl>(in)));
      if (x >= 1.f) x = 0.f;

      // Settings made before the first sample apply from it, whatever the
      // sync mode.
      const bool first = !started;
      int wrap = 0;
      if (started) wrap = phaseStep::Of(previous, x).wrap;
      started = true;
      previous = x;
      wraps += wrap;

      if (pending && (landing == update::now || (landing == update::input && wrap != 0) || first))
        Land();

      // Where the input is in the active layout: the step's id (counted
      // since the start), its number in the pattern, its first subdivision
      // unit (counted since the start) and the ramp.
      std::int64_t id = 0;
      std::int64_t unit = 0;
      std::int64_t number = 0;
      Dbl ramp = 0.0;
      for (int pass = 0; pass < 2; ++pass) {
        const std::int64_t n = active.subdivisions;
        const Dbl u = static_cast<Dbl>(x) * static_cast<Dbl>(n);
        if (active.used == 0) {
          Dbl k = std::floor(u);
          if (k > static_cast<Dbl>(n - 1)) k = static_cast<Dbl>(n - 1);
          number = static_cast<std::int64_t>(k);
          ramp = u - k;
          id = wraps * n + number;
          unit = id;
        } else {
          const std::int64_t c = Mod(wraps, active.cycles);
          const std::int64_t round = (wraps - c) / active.cycles;
          const Dbl p = static_cast<Dbl>(c * n) + u;
          UInt j = 0;
          while (j + 1 < active.used && static_cast<Dbl>(active.starts[j + 1]) <= p)
            ++j;
          number = static_cast<std::int64_t>(j);
          ramp = (p - static_cast<Dbl>(active.starts[j])) / static_cast<Dbl>(active.steps[j]);
          id = round * static_cast<std::int64_t>(active.used) + number;
          unit = round * active.units + active.starts[j];
        }
        // Max's "output reset": the old layout's next step starts, so the
        // new one lands and this sample is read again in it.
        if (pass == 0 && pending && landing == update::output && hasStep && id != stepId) {
          Land();
          continue;
        }
        break;
      }

      if (!hasStep || id != stepId) {
        hasStep = true;
        stepId = id;
        playing = true;
        if (probCount > 0) {
          const Flt p =
              chance[Mod(lockProbability ? id : unit, static_cast<std::int64_t>(probCount))];
          playing = p >= 1.f || (p > 0.f && Uniform(state) < static_cast<Dbl>(p));
        }
      }

      if (playing) {
        playedIndex = static_cast<Flt>(number);
        currentIndex = playedIndex;
        return ToSample(ramp);
      }
      currentIndex = silence == silent::hold ? playedIndex : -1.f;
      return 0.f;
    }

    Flt phasorSubdiv::process(Flt phase) {
      return Step(phase);
    }

    buffer& phasorSubdiv::operator()(buffer& phase) {
      const UInt length = Clamp(phase.getLength());
      Fit(output, length);
      Fit(indexOutput, length);
      const Flt* in = phase.getPtr();
      Flt* out = output.getPtr();
      Flt* idx = indexOutput.getPtr();
      for (UInt i = 0; i < length; ++i) {
        out[i] = Step(in[i]);
        idx[i] = currentIndex;
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
