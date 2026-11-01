#include "eventSequencer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
  inline UInt Clamp(UInt length) {
    return length > YSE::STANDARD_BUFFERSIZE ? YSE::STANDARD_BUFFERSIZE : length;
  }

  inline UInt Capacity(UInt requested) {
    if (requested < 1) return 1;
    if (requested > YSE::DSP::eventSequencer::MAX_EVENTS) {
      return YSE::DSP::eventSequencer::MAX_EVENTS;
    }
    return requested;
  }

  // A text the store takes: not empty, room for its terminator, and nothing
  // that would end it early or split a line of a saved file.
  inline bool ValidText(const char* text, UInt length) {
    if (text == nullptr || length == 0 || length >= YSE::DSP::eventSequencer::TEXT_CAPACITY) {
      return false;
    }
    for (UInt i = 0; i < length; ++i) {
      if (text[i] == '\0' || text[i] == '\n' || text[i] == '\r') return false;
    }
    return true;
  }
} // namespace

namespace YSE {
  namespace DSP {

    eventSequencer::eventSequencer(UInt capacity, UInt firedCapacity) {
      const UInt size = Capacity(capacity);
      entries.assign(size, entry{0, 0.f, 0, 0});
      texts.assign(static_cast<std::size_t>(size) * TEXT_CAPACITY, '\0');
      lengths.assign(size, 0);
      freeSlots.assign(size, 0);
      firings.assign(firedCapacity < 1 ? 1 : firedCapacity, firing{"", 0, 0.f, 0});
      clearAll();
    }

    UInt eventSequencer::count(Int sequence) const {
      UInt n = 0;
      for (UInt i = 0; i < stored; ++i) {
        if (entries[i].sequence == sequence) n++;
      }
      return n;
    }

    bool eventSequencer::add(Int sequence, Flt phase, const char* text, UInt length) {
      if (stored >= capacity() || freeCount == 0 || !std::isfinite(phase)) return false;
      if (!ValidText(text, length)) return false;

      // After every event that sorts before or with it: a new event goes
      // last among equal phases.
      UInt lo = 0;
      UInt hi = stored;
      while (lo < hi) {
        const UInt mid = lo + (hi - lo) / 2;
        const entry& e = entries[mid];
        if (e.sequence < sequence || (e.sequence == sequence && e.phase <= phase)) {
          lo = mid + 1;
        } else {
          hi = mid;
        }
      }
      std::copy_backward(entries.begin() + lo, entries.begin() + stored,
                         entries.begin() + stored + 1);

      const UInt slot = freeSlots[--freeCount];
      char* place = Text(slot);
      std::memcpy(place, text, length);
      place[length] = '\0';
      lengths[slot] = length;
      entries[lo] = entry{sequence, phase, nextOrder++, slot};
      stored++;
      dirty = true;
      return true;
    }

    UInt eventSequencer::remove(Int sequence, Flt from, Flt to, const char* text, UInt length) {
      if (std::isnan(from) || std::isnan(to)) return 0;
      if (from > to) std::swap(from, to);
      UInt kept = 0;
      UInt removed = 0;
      for (UInt i = 0; i < stored; ++i) {
        const entry e = entries[i];
        bool match = e.sequence == sequence && e.phase >= from && e.phase <= to;
        if (match && text != nullptr) {
          match = lengths[e.slot] == length && std::memcmp(Text(e.slot), text, length) == 0;
        }
        if (match) {
          freeSlots[freeCount++] = e.slot;
          removed++;
        } else {
          entries[kept++] = e;
        }
      }
      stored = kept;
      if (removed > 0) dirty = true;
      return removed;
    }

    UInt eventSequencer::clear(Int sequence) {
      return remove(sequence, -HUGE_VALF, HUGE_VALF);
    }

    eventSequencer& eventSequencer::clearAll() {
      const UInt size = capacity();
      stored = 0;
      freeCount = size;
      // Popped from the end, so the first event takes slot 0.
      for (UInt i = 0; i < size; ++i)
        freeSlots[i] = size - 1 - i;
      nextOrder = 0;
      dirty = true;
      return *this;
    }

    eventSequencer& eventSequencer::select(Int sequence) {
      if (sequence != current) {
        current = sequence;
        dirty = true;
      }
      return *this;
    }

    eventSequencer& eventSequencer::play(bool on) {
      if (on && !isPlaying) seek = true;
      isPlaying = on;
      return *this;
    }

    eventSequencer& eventSequencer::record(bool on) {
      if (on && !isRecording) clear(current);
      isRecording = on;
      return *this;
    }

    eventSequencer& eventSequencer::overdub(bool on) {
      isOverdubbing = on;
      return *this;
    }

    bool eventSequencer::recordEvent(const char* text, UInt length) {
      if (!isRecording && !isOverdubbing) return false;
      return add(current, last, text, length);
    }

    const char* eventSequencer::eventText(UInt index) const {
      if (index >= stored) return "";
      return Text(entries[index].slot);
    }

    void eventSequencer::Range() {
      UInt lo = 0;
      UInt hi = stored;
      while (lo < hi) {
        const UInt mid = lo + (hi - lo) / 2;
        if (entries[mid].sequence < current) {
          lo = mid + 1;
        } else {
          hi = mid;
        }
      }
      low = lo;
      hi = stored;
      while (lo < hi) {
        const UInt mid = lo + (hi - lo) / 2;
        if (entries[mid].sequence <= current) {
          lo = mid + 1;
        } else {
          hi = mid;
        }
      }
      high = lo;
    }

    UInt eventSequencer::Above(Flt p) const {
      UInt lo = low;
      UInt hi = high;
      while (lo < hi) {
        const UInt mid = lo + (hi - lo) / 2;
        if (entries[mid].phase <= p) {
          lo = mid + 1;
        } else {
          hi = mid;
        }
      }
      return lo;
    }

    UInt eventSequencer::AtLeast(Flt p) const {
      UInt lo = low;
      UInt hi = high;
      while (lo < hi) {
        const UInt mid = lo + (hi - lo) / 2;
        if (entries[mid].phase < p) {
          lo = mid + 1;
        } else {
          hi = mid;
        }
      }
      return lo;
    }

    void eventSequencer::Fire(UInt index, UInt offset) {
      if (firedCount >= firings.size()) {
        missedCount++;
        return;
      }
      const entry& e = entries[index];
      firing& f = firings[firedCount++];
      f.text = Text(e.slot);
      f.length = lengths[e.slot];
      f.phase = e.phase;
      f.offset = offset;
    }

    eventSequencer& eventSequencer::operator()(buffer& input, UInt length) {
      UInt n = Clamp(length);
      if (n > input.getLength()) n = input.getLength();
      firedCount = 0;
      missedCount = 0;
      if (dirty) {
        Range();
        cursor = primed ? Above(last) : low;
        dirty = false;
      }

      const Flt* in = input.getPtr();
      for (UInt i = 0; i < n; ++i) {
        Flt p = in[i];
        if (!std::isfinite(p)) p = primed ? last : 0.f;
        if (isPlaying) {
          if (seek || !primed) {
            // Play just started: from here, the events at this very phase
            // included.
            cursor = AtLeast(p);
            while (cursor < high && entries[cursor].phase <= p)
              Fire(cursor++, i);
            seek = false;
          } else if (p >= last) {
            while (cursor < high && entries[cursor].phase <= p)
              Fire(cursor++, i);
          } else if (last - p > 0.5f) {
            // A wrap: the rest of the cycle, then the new one up to here.
            while (cursor < high)
              Fire(cursor++, i);
            cursor = low;
            while (cursor < high && entries[cursor].phase <= p)
              Fire(cursor++, i);
          } else {
            // The phase moved back: nothing fires, and the events it went
            // back over fire again when it passes them.
            cursor = Above(p);
          }
        }
        last = p;
        primed = true;
      }
      return *this;
    }

    eventSequencer& eventSequencer::reset() {
      clearAll();
      current = 0;
      isPlaying = false;
      isRecording = false;
      isOverdubbing = false;
      low = 0;
      high = 0;
      cursor = 0;
      seek = false;
      primed = false;
      last = 0.f;
      firedCount = 0;
      missedCount = 0;
      return *this;
    }

  } // namespace DSP
} // namespace YSE
