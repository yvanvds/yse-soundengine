#include "biquadCascade.hpp"

#include <algorithm>

namespace YSE {
  namespace DSP {

    biquadCascade::biquadCascade(UInt capacity) : output(STANDARD_BUFFERSIZE) {
      const UInt sections = std::clamp<UInt>(capacity, 1, MAX_CAPACITY);
      stages.reserve(sections);
      for (UInt i = 0; i < sections; ++i) {
        stages.emplace_back();
        // Select the coefficient path (all 0) so a block call never runs a
        // section on the type path.
        stages.back().coefficients(0.f, 0.f, 0.f, 0.f, 0.f);
      }
    }

    void biquadCascade::Set(UInt index, const Flt* values, Bool fresh) {
      biQuad& stage = stages[index];
      if (fresh) {
        // Unsmoothed, so the section starts at these coefficients rather
        // than moving to them from the ones it had when last in use.
        stage.smooth(false);
        stage.clear();
      }
      stage.coefficients(values[0], values[1], values[2], values[3], values[4]);
      if (fresh) stage.smooth(smoothing);
    }

    biquadCascade& biquadCascade::coefficients(const Flt* values, UInt count) {
      if (values == nullptr) return *this;
      const UInt sets = std::min(count / 5, capacity());
      if (sets == 0) return *this;
      for (UInt i = 0; i < sets; ++i)
        Set(i, values + 5 * i, i >= running);
      active = running = sets;
      return *this;
    }

    biQuad::coefficientSet biquadCascade::coefficients(UInt section) const {
      if (section >= active) return {};
      return stages[section].coefficients();
    }

    biquadCascade& biquadCascade::smooth(Bool on) {
      if (on == smoothing) return *this;
      smoothing = on;
      for (auto& stage : stages)
        stage.smooth(on);
      return *this;
    }

    biquadCascade& biquadCascade::clear() {
      constexpr Flt none[5] = {0.f, 0.f, 0.f, 0.f, 0.f};
      for (UInt i = 0; i < capacity(); ++i)
        Set(i, none, true);
      // Silent sections have nothing to move from: the next coefficients
      // apply at once.
      running = 0;
      return *this;
    }

    Flt biquadCascade::process(Flt in) {
      Flt y = in;
      for (UInt i = 0; i < active; ++i)
        y = stages[i].process(y);
      if (bypassed) return in;
      if (muted) return 0.f;
      return y;
    }

    buffer& biquadCascade::operator()(buffer& in) {
      buffer* signal = &in;
      for (UInt i = 0; i < active; ++i)
        signal = &stages[i](*signal, nullptr, nullptr, nullptr, nullptr, nullptr);
      if (active > 0 && !bypassed && !muted) return *signal;

      const UInt length = std::min(in.getLength(), STANDARD_BUFFERSIZE);
      // Within the capacity set in the constructor: no allocation.
      if (output.getLength() != length) output.resize(length);
      Flt* y = output.getPtr();
      if (muted && !bypassed) {
        std::fill(y, y + length, 0.f);
      } else {
        const Flt* x = in.getPtr();
        std::copy(x, x + length, y);
      }
      return output;
    }

  } // namespace DSP
} // namespace YSE
