#include "cycle.hpp"

#include <cmath>
#include "../sampling/sampleBuffer.hpp"

namespace {

  // One cycle of a cosine. Built once, the first time an oscillator is
  // constructed (a control thread); the audio thread only reads it through
  // the pointer each oscillator keeps, so it never touches the static guard.
  const Flt* CosineTable() {
    static const struct table {
      Flt points[YSE::DSP::cycle::TABLE_SIZE];
      table() : points() {
        const Dbl step =
            2.0 * 3.14159265358979323846 / static_cast<Dbl>(YSE::DSP::cycle::TABLE_SIZE);
        for (UInt i = 0; i < YSE::DSP::cycle::TABLE_SIZE; ++i)
          points[i] = static_cast<Flt>(std::cos(step * static_cast<Dbl>(i)));
      }
    } cosine;
    return cosine.points;
  }

  // Wrap into [0, 1). floor keeps negative phases on the same cycle grid as
  // positive ones; the guard catches rounding onto 1 and anything that is not
  // a finite number, which restarts the phase rather than poisoning it.
  inline Dbl Wrap(Dbl p) {
    p -= std::floor(p);
    return (p >= 0.0 && p < 1.0) ? p : 0.0;
  }

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  struct constantValue {
    Dbl value;
    Dbl operator[](UInt) const {
      return value;
    }
  };

  struct signalValue {
    const Flt* values;
    Dbl scale;
    Dbl operator[](UInt i) const {
      return static_cast<Dbl>(values[i]) * scale;
    }
  };

} // namespace

namespace YSE {
  namespace DSP {

    cycle::cycle()
      : data(CosineTable()), size(TABLE_SIZE), current(0.0), output(STANDARD_BUFFERSIZE) {}

    cycle& cycle::table(const Flt* samples, UInt count) {
      data = count > 0 ? samples : nullptr;
      size = data != nullptr ? count : 0;
      return *this;
    }

    cycle& cycle::table(const sampleView& view, UInt offset, Int count) {
      const Flt* channel = view.channel(0);
      if (view.empty() || channel == nullptr || offset >= view.frames()) return table(nullptr, 0);
      const UInt available = view.frames() - offset;
      UInt wanted = available;
      if (count == 0)
        wanted = DEFAULT_VIEW_SIZE;
      else if (count > 0)
        wanted = static_cast<UInt>(count);
      return table(channel + offset, wanted < available ? wanted : available);
    }

    cycle& cycle::cosine() {
      data = CosineTable();
      size = TABLE_SIZE;
      return *this;
    }

    cycle& cycle::reset(Flt value) {
      current = Wrap(static_cast<Dbl>(value));
      return *this;
    }

    Flt cycle::phase() const {
      const Flt v = static_cast<Flt>(current);
      return v < 1.0f ? v : 0.99999994f;
    }

    Flt cycle::lookup(Dbl p) const {
      const Dbl position = p * static_cast<Dbl>(size);
      UInt index = static_cast<UInt>(position);
      if (index >= size) index = size - 1;
      const Flt fraction = static_cast<Flt>(position - static_cast<Dbl>(index));
      const UInt next = index + 1 == size ? 0 : index + 1;
      const Flt a = data[index];
      return a + (data[next] - a) * fraction;
    }

    template <class Frequency, class Phase>
    buffer& cycle::process(Frequency frequency, Phase phase, UInt length) {
      length = Clamp(length);
      // Within the capacity set in the constructor: no allocation.
      if (output.getLength() != length) output.resize(length);
      Flt* out = output.getPtr();
      Dbl p = current;

      if (size == 0) {
        // An empty table: silence, but the phase keeps running so a buffer
        // that fills later starts where it would have been.
        for (UInt i = 0; i < length; ++i) {
          out[i] = 0.f;
          p = Wrap(p + frequency[i]);
        }
      } else {
        for (UInt i = 0; i < length; ++i) {
          out[i] = lookup(Wrap(p + phase[i]));
          p = Wrap(p + frequency[i]);
        }
      }
      current = p;
      return output;
    }

    buffer& cycle::operator()(Flt frequency, Flt phase, UInt length) {
      const Dbl increment = static_cast<Dbl>(frequency) / static_cast<Dbl>(SAMPLERATE);
      return process(constantValue{increment}, constantValue{static_cast<Dbl>(phase)}, length);
    }

    buffer& cycle::operator()(buffer& frequency, Flt phase) {
      return process(signalValue{frequency.getPtr(), 1.0 / static_cast<Dbl>(SAMPLERATE)},
                     constantValue{static_cast<Dbl>(phase)}, frequency.getLength());
    }

    buffer& cycle::operator()(Flt frequency, buffer& phase) {
      const Dbl increment = static_cast<Dbl>(frequency) / static_cast<Dbl>(SAMPLERATE);
      return process(constantValue{increment}, signalValue{phase.getPtr(), 1.0}, phase.getLength());
    }

    buffer& cycle::operator()(buffer& frequency, buffer& phase) {
      const UInt length =
          frequency.getLength() < phase.getLength() ? frequency.getLength() : phase.getLength();
      return process(signalValue{frequency.getPtr(), 1.0 / static_cast<Dbl>(SAMPLERATE)},
                     signalValue{phase.getPtr(), 1.0}, length);
    }

  } // namespace DSP
} // namespace YSE
