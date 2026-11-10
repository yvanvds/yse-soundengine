#include "signalSelector.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
  using YSE::DSP::buffer;

  // The largest whole-number part a control value keeps, so the cast stays
  // in range. Far beyond any channel count.
  constexpr Flt LARGEST_WHOLE = 1073741824.f;

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  // Within the capacity set in the constructor: no allocation.
  inline void Fit(buffer& b, UInt length) {
    if (b.getLength() != length) b.resize(length);
  }

  inline void Silence(buffer& b, UInt length) {
    std::memset(b.getPtr(), 0, sizeof(Flt) * length);
  }

  // The length of a select() block: the shortest of the inputs there are.
  UInt SelectLength(buffer* const* inputs, UInt count) {
    UInt length = YSE::STANDARD_BUFFERSIZE;
    for (UInt c = 0; c < count; ++c) {
      if (inputs[c] != nullptr) length = std::min(length, inputs[c]->getLength());
    }
    return length;
  }
} // namespace

namespace YSE {
  namespace DSP {

    signalSelector::signalSelector(UInt channels, Int open)
      : count(std::clamp<UInt>(channels, 1, MAX_CHANNELS)), outputs(count) {
      for (buffer& b : outputs)
        b = 0.f;
      target = Channel(static_cast<Flt>(open), count);
      if (target > 0) gains[target - 1] = 1.f;
    }

    Int signalSelector::Channel(Flt value, UInt channels) {
      // A NaN fails the comparison and closes too.
      if (!(value >= 1.f)) return 0;
      if (value >= static_cast<Flt>(channels)) return static_cast<Int>(channels);
      return static_cast<Int>(value);
    }

    Int signalSelector::Next(Int open, UInt channels) {
      if (open <= 0 || open >= static_cast<Int>(channels)) return 1;
      return open + 1;
    }

    signalSelector& signalSelector::open(Int channel) {
      Head(channel <= 0 ? 0 : std::min(channel, static_cast<Int>(count)));
      return *this;
    }

    signalSelector& signalSelector::next() {
      Head(Next(target, count));
      return *this;
    }

    signalSelector& signalSelector::rampTime(Flt ms) {
      if (std::isfinite(ms)) ramp = ms < 0.f ? 0.f : ms;
      return *this;
    }

    signalSelector& signalSelector::stepMode(bool on) {
      steps = on;
      return *this;
    }

    Flt signalSelector::gain(Int channel) const {
      if (channel < 1 || channel > static_cast<Int>(count)) return 0.f;
      return gains[channel - 1];
    }

    buffer& signalSelector::output(UInt index) {
      return outputs[index < count ? index : count - 1];
    }

    void signalSelector::Head(Int channel) {
      if (channel == target) return;
      target = channel;
      // The gains move at the next sample processed, at that block's ramp.
      fading = true;
    }

    void signalSelector::Control(Flt value) {
      if (!steps) {
        Head(Channel(value, count));
        return;
      }
      const Int whole = value >= 1.f ? static_cast<Int>(std::min(value, LARGEST_WHOLE)) : 0;
      if (whole == lastWhole) return;
      lastWhole = whole;
      Head(whole == 0 ? 0 : Next(target, count));
    }

    Flt signalSelector::Step() const {
      const Dbl samples = static_cast<Dbl>(ramp) * static_cast<Dbl>(SAMPLERATE) / 1000.0;
      // Under one sample is a hard switch.
      if (!(samples > 1.0)) return 1.f;
      return static_cast<Flt>(1.0 / samples);
    }

    void signalSelector::Advance(Flt step) {
      bool moving = false;
      for (UInt c = 0; c < count; ++c) {
        Flt& g = gains[c];
        if (static_cast<Int>(c) + 1 == target) {
          if (g < 1.f) {
            g = std::min(1.f, g + step);
            moving = moving || g < 1.f;
          }
        } else if (g > 0.f) {
          g = std::max(0.f, g - step);
          moving = moving || g > 0.f;
        }
      }
      fading = moving;
    }

    buffer& signalSelector::select(buffer* const* inputs) {
      const UInt length = Clamp(SelectLength(inputs, count));
      buffer& out = outputs[0];
      Fit(out, length);
      Flt* y = out.getPtr();

      if (!fading) {
        // Steady: the open input as it is, or silence.
        buffer* source = target > 0 ? inputs[target - 1] : nullptr;
        if (source != nullptr)
          std::memcpy(y, source->getPtr(), sizeof(Flt) * length);
        else
          Silence(out, length);
        return out;
      }

      const Flt step = Step();
      for (UInt i = 0; i < length; ++i) {
        if (fading) Advance(step);
        Flt sum = 0.f;
        for (UInt c = 0; c < count; ++c) {
          if (gains[c] > 0.f && inputs[c] != nullptr) sum += inputs[c]->getPtr()[i] * gains[c];
        }
        y[i] = sum;
      }
      return out;
    }

    buffer& signalSelector::select(buffer* const* inputs, buffer& control) {
      const UInt length = Clamp(std::min(SelectLength(inputs, count), control.getLength()));
      buffer& out = outputs[0];
      Fit(out, length);
      Flt* y = out.getPtr();
      const Flt* x = control.getPtr();

      const Flt step = Step();
      for (UInt i = 0; i < length; ++i) {
        Control(x[i]);
        if (fading) {
          Advance(step);
          Flt sum = 0.f;
          for (UInt c = 0; c < count; ++c) {
            if (gains[c] > 0.f && inputs[c] != nullptr) sum += inputs[c]->getPtr()[i] * gains[c];
          }
          y[i] = sum;
        } else {
          buffer* source = target > 0 ? inputs[target - 1] : nullptr;
          y[i] = source != nullptr ? source->getPtr()[i] : 0.f;
        }
      }
      return out;
    }

    buffer& signalSelector::route(buffer& in) {
      const UInt length = Clamp(in.getLength());
      for (buffer& b : outputs)
        Fit(b, length);
      const Flt* x = in.getPtr();

      if (!fading) {
        // Steady: the input to the open output, silence to the others.
        for (UInt c = 0; c < count; ++c) {
          if (static_cast<Int>(c) + 1 == target)
            std::memcpy(outputs[c].getPtr(), x, sizeof(Flt) * length);
          else
            Silence(outputs[c], length);
        }
        return outputs[0];
      }

      const Flt step = Step();
      for (UInt i = 0; i < length; ++i) {
        if (fading) Advance(step);
        for (UInt c = 0; c < count; ++c)
          outputs[c].getPtr()[i] = x[i] * gains[c];
      }
      return outputs[0];
    }

    buffer& signalSelector::route(buffer& in, buffer& control) {
      const UInt length = Clamp(std::min(in.getLength(), control.getLength()));
      for (buffer& b : outputs)
        Fit(b, length);
      const Flt* x = in.getPtr();
      const Flt* k = control.getPtr();

      const Flt step = Step();
      for (UInt i = 0; i < length; ++i) {
        Control(k[i]);
        if (fading) {
          Advance(step);
          for (UInt c = 0; c < count; ++c)
            outputs[c].getPtr()[i] = x[i] * gains[c];
        } else {
          for (UInt c = 0; c < count; ++c)
            outputs[c].getPtr()[i] = static_cast<Int>(c) + 1 == target ? x[i] : 0.f;
        }
      }
      return outputs[0];
    }

  } // namespace DSP
} // namespace YSE
