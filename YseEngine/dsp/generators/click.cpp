#include "click.hpp"

#include <cmath>

namespace {

  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  inline Flt Finite(Flt value) {
    return std::isfinite(value) ? value : 0.f;
  }

  // No trigger signal: nothing starts inside the block.
  struct noTriggers {
    bool operator()(UInt, Flt&) const {
      return false;
    }
  };

  // A rise from 0 to anything else at sample i.
  struct signalTriggers {
    const Flt* values;
    bool operator()(UInt i, Flt& last) const {
      const Flt now = values[i];
      const bool rise = last == 0.f && now != 0.f;
      last = now;
      return rise;
    }
  };

} // namespace

namespace YSE {
  namespace DSP {

    click::click()
      : position(MAX_LENGTH),
        scale(1.f),
        pendingScale(1.f),
        pending(false),
        last(0.f),
        output(STANDARD_BUFFERSIZE) {
      table[0] = 1.f;
    }

    click& click::set(const Flt* values, UInt count) {
      if (values == nullptr || count == 0) {
        table.fill(0.f);
        table[0] = 1.f;
        size = 1;
        return *this;
      }
      if (count > MAX_LENGTH) count = MAX_LENGTH;
      for (UInt i = 0; i < count; ++i)
        table[i] = Finite(values[i]);
      for (UInt i = count; i < MAX_LENGTH; ++i)
        table[i] = 0.f;
      size = count;
      return *this;
    }

    click& click::trigger(Flt value) {
      pendingScale = Finite(value);
      pending = true;
      return *this;
    }

    click& click::reset() {
      position = MAX_LENGTH;
      pending = false;
      last = 0.f;
      return *this;
    }

    template <class Triggers> buffer& click::process(Triggers triggers, UInt length) {
      length = Clamp(length);
      // Within the capacity set in the constructor: no allocation.
      if (output.getLength() != length) output.resize(length);
      Flt* out = output.getPtr();

      if (pending) {
        pending = false;
        scale = pendingScale;
        position = 0;
      }
      for (UInt i = 0; i < length; ++i) {
        if (triggers(i, last)) {
          scale = 1.f;
          position = 0;
        }
        if (position < size) {
          out[i] = table[position] * scale;
          // Finished: idle, so a longer waveform set later does not resume.
          if (++position >= size) position = MAX_LENGTH;
        } else {
          out[i] = 0.f;
        }
      }
      return output;
    }

    buffer& click::operator()(UInt length) {
      return process(noTriggers{}, length);
    }

    buffer& click::operator()(buffer& triggers) {
      return process(signalTriggers{triggers.getPtr()}, triggers.getLength());
    }

  } // namespace DSP
} // namespace YSE
