#include "sampleCapture.hpp"

#include <algorithm>
#include <cstring>

namespace {
  inline UInt ClampCapacity(UInt capacity) {
    if (capacity < 1) return 1;
    if (capacity > YSE::DSP::sampleCapture::MAXIMUM_CAPACITY) {
      return YSE::DSP::sampleCapture::MAXIMUM_CAPACITY;
    }
    return capacity;
  }
} // namespace

namespace YSE {
  namespace DSP {

    sampleCapture::sampleCapture(UInt capacity)
      : store(ClampCapacity(capacity), 0.f),
        held(0),
        next(0),
        keep(LAST),
        positions{},
        positionCount(0) {}

    sampleCapture& sampleCapture::mode(MODE value) {
      keep = value;
      return *this;
    }

    sampleCapture::MODE sampleCapture::mode() const {
      return keep;
    }

    sampleCapture& sampleCapture::indices(const UInt* list, UInt count) {
      positionCount = 0;
      if (list == nullptr) return *this;
      if (count > MAX_INDICES) count = MAX_INDICES;
      for (UInt i = 0; i < count; ++i)
        positions[i] = list[i];
      std::sort(positions, positions + count);
      // Duplicates dropped: a position is stored once per block.
      for (UInt i = 0; i < count; ++i) {
        if (positionCount == 0 || positions[positionCount - 1] != positions[i]) {
          positions[positionCount++] = positions[i];
        }
      }
      return *this;
    }

    UInt sampleCapture::indexCount() const {
      return positionCount;
    }

    UInt sampleCapture::index(UInt i) const {
      return i < positionCount ? positions[i] : 0;
    }

    void sampleCapture::process(Flt in) {
      const auto room = static_cast<UInt>(store.size());
      if (held == room) {
        if (keep == FIRST) return;
      } else {
        held++;
      }
      store[next] = in;
      next = next + 1 == room ? 0 : next + 1;
    }

    void sampleCapture::operator()(buffer& in) {
      const UInt length = in.getLength();
      const Flt* x = in.getPtr();
      if (positionCount == 0) {
        for (UInt i = 0; i < length; ++i)
          process(x[i]);
        return;
      }
      for (UInt i = 0; i < positionCount && positions[i] < length; ++i)
        process(x[positions[i]]);
    }

    UInt sampleCapture::size() const {
      return held;
    }

    UInt sampleCapture::capacity() const {
      return static_cast<UInt>(store.size());
    }

    bool sampleCapture::full() const {
      return held == store.size();
    }

    Flt sampleCapture::sample(UInt i) const {
      if (i >= held) return 0.f;
      // Until the store is full the oldest sample is at 0; after that it is
      // the one the next sample would replace.
      const auto room = static_cast<UInt>(store.size());
      const UInt oldest = held < room ? 0 : next;
      const UInt at = oldest + i;
      return store[at >= room ? at - room : at];
    }

    UInt sampleCapture::copy(Flt* out, UInt count) const {
      if (out == nullptr) return 0;
      if (count > held) count = held;
      const auto room = static_cast<UInt>(store.size());
      const UInt oldest = held < room ? 0 : next;
      // In at most two runs: from the oldest to the end, then from 0.
      const UInt first = std::min(count, room - oldest);
      std::memcpy(out, store.data() + oldest, first * sizeof(Flt));
      if (count > first) std::memcpy(out + first, store.data(), (count - first) * sizeof(Flt));
      return count;
    }

    sampleCapture& sampleCapture::clear() {
      held = 0;
      next = 0;
      return *this;
    }

  } // namespace DSP
} // namespace YSE
