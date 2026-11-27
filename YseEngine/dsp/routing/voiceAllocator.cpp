#include "voiceAllocator.hpp"

namespace YSE {
  namespace DSP {

    voiceAllocator::voiceAllocator(int capacity)
      : slots(static_cast<std::size_t>(capacity < 1 ? 1 : capacity)),
        count(capacity < 1 ? 1 : capacity) {}

    voiceAllocator& voiceAllocator::voices(int value) {
      count = value < 1 ? 1 : (value > capacity() ? capacity() : value);
      return *this;
    }

    void voiceAllocator::touch(int voice) {
      if (voice < 0 || voice >= capacity()) return;
      slots[static_cast<std::size_t>(voice)].age = nextAge++;
    }

    int voiceAllocator::noteOff(int value) {
      int found = NO_VOICE;
      std::uint64_t age = UINT64_MAX;
      for (int v = 0; v < capacity(); v++) {
        const slot& s = slots[static_cast<std::size_t>(v)];
        if (s.held && s.pitch == value && s.age < age) {
          age = s.age;
          found = v;
        }
      }
      if (found != NO_VOICE) release(found);
      return found;
    }

    void voiceAllocator::release(int voice) {
      if (voice < 0 || voice >= capacity()) return;
      slot& s = slots[static_cast<std::size_t>(voice)];
      if (!s.held) return;
      s.held = false;
      held--;
    }

    bool voiceAllocator::holds(int voice) const {
      return voice >= 0 && voice < capacity() && slots[static_cast<std::size_t>(voice)].held;
    }

    int voiceAllocator::pitch(int voice) const {
      return holds(voice) ? slots[static_cast<std::size_t>(voice)].pitch : -1;
    }

    void voiceAllocator::reset() {
      for (slot& s : slots)
        s = slot();
      held = 0;
      nextAge = 1;
    }

  } // namespace DSP
} // namespace YSE
