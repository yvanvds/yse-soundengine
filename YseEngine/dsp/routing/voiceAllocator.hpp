/*
  ==============================================================================

    voiceAllocator.hpp
    Note-to-voice allocation over a fixed pool: a free voice first, in
    ascending order, else the oldest one when stealing (issue #1040).

  ==============================================================================
*/

#ifndef YSE_DSP_ROUTING_VOICEALLOCATOR_HPP
#define YSE_DSP_ROUTING_VOICEALLOCATOR_HPP

#include <cstdint>
#include <vector>
#include "../../headers/defines.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Allocates notes to a numbered pool of voices: the policy of
     *         ``.poly`` and ``~poly``, usable from C++ without a patcher
     *         (issue #1040).
     *
     *  Voices are numbered from 0 here; the patcher objects add 1. The pool
     *  has a fixed *capacity*, allocated in the constructor, and a *size*
     *  (``voices()``) from 1 to the capacity that bounds where a note may be
     *  put. The policy is the one ``SYNTH::implementationObject::
     *  allocateInGroup`` follows:
     *
     *  - A **free** voice is preferred, the lowest one first. A voice is
     *    free when it holds no note and the caller's *busy* test says it is
     *    not busy. ``.poly`` has no busy test; ``~poly`` passes the busy
     *    flags its voices set with ``~thispoly``.
     *  - With no free voice and **stealing** on, the voice that was started
     *    longest ago takes the note. With stealing off there is no voice for
     *    it (``NO_VOICE``).
     *
     *  ``noteOn()`` makes a voice *hold* a pitch until ``noteOff()`` for that
     *  pitch releases it; a pitch held twice is released oldest first.
     *  ``choose()`` and ``touch()`` start a voice without a pitch to track
     *  (``~poly``'s ``note`` message). ``releaseAll()`` frees every held
     *  voice, in ascending order. Releases look at the whole capacity, not
     *  only the current size, so shrinking the pool never strands a note.
     *
     *  Any int is a pitch: the pool tracks voices, not a bitmap of pitches.
     *  Nothing allocates after the constructor, so every call is safe on the
     *  audio thread. Not thread-safe: call it from one thread at a time.
     */
    class API voiceAllocator {
    public:
      /** @brief What a call returns when there is no voice. */
      static constexpr int NO_VOICE = -1;

      /** @brief A pool of @p capacity voices (at least 1), all of them in
       *         use (``voices() == capacity``). Allocates. */
      explicit voiceAllocator(int capacity = 16);

      /** @brief The fixed number of voices the pool was built with. */
      int capacity() const {
        return static_cast<int>(slots.size());
      }

      /** @brief How many voices notes may go to, clamped to 1..capacity(). */
      voiceAllocator& voices(int count);
      int voices() const {
        return count;
      }

      /** @brief Whether a note with no free voice takes the oldest one. Off
       *         on a new pool. */
      voiceAllocator& steal(bool on) {
        stealing = on;
        return *this;
      }
      bool steal() const {
        return stealing;
      }

      /** @brief What noteOn() did. */
      struct allocation {
        /** @brief The voice the note went to, or NO_VOICE. */
        int voice = NO_VOICE;
        /** @brief True when that voice held a note, which it no longer
         *         does: ``stolenPitch``. */
        bool stolen = false;
        int stolenPitch = 0;
      };

      /**
       *  @brief The voice the policy picks, without starting it: the lowest
       *         one that holds no note and that @p busy (called with a voice
       *         number, returning true for a busy voice) does not call busy;
       *         else, when stealing, the one started longest ago; else
       *         NO_VOICE.
       */
      template <class Busy> int choose(Busy&& busy) const {
        for (int v = 0; v < count; v++) {
          if (!slots[static_cast<std::size_t>(v)].held && !busy(v)) return v;
        }
        if (!stealing) return NO_VOICE;
        int oldest = NO_VOICE;
        std::uint64_t age = UINT64_MAX;
        for (int v = 0; v < count; v++) {
          if (slots[static_cast<std::size_t>(v)].age < age) {
            age = slots[static_cast<std::size_t>(v)].age;
            oldest = v;
          }
        }
        return oldest;
      }
      int choose() const {
        return choose([](int) { return false; });
      }

      /** @brief Marks @p voice as started now, for stealing: it becomes the
       *         newest. Does not make it hold a note. */
      void touch(int voice);

      /** @brief Starts a note: choose(@p busy), and the voice chosen holds
       *         @p pitch until noteOff(). */
      template <class Busy> allocation noteOn(int pitch, Busy&& busy) {
        allocation result;
        result.voice = choose(busy);
        if (result.voice == NO_VOICE) return result;
        slot& s = slots[static_cast<std::size_t>(result.voice)];
        if (s.held) {
          result.stolen = true;
          result.stolenPitch = s.pitch;
          held--;
        }
        s.pitch = pitch;
        s.held = true;
        s.age = nextAge++;
        held++;
        return result;
      }
      allocation noteOn(int pitch) {
        return noteOn(pitch, [](int) { return false; });
      }

      /** @brief Releases the voice holding @p pitch (the oldest of them when
       *         several do) and returns it, or NO_VOICE when none does. */
      int noteOff(int pitch);

      /** @brief Frees @p voice, whatever it holds. */
      void release(int voice);

      /** @brief Frees every voice that holds a note, in ascending order,
       *         calling @p f(voice, pitch) as each is freed. Returns how many
       *         were. */
      template <class F> int releaseAll(F&& f) {
        int released = 0;
        for (int v = 0; v < capacity(); v++) {
          slot& s = slots[static_cast<std::size_t>(v)];
          if (!s.held) continue;
          s.held = false;
          held--;
          released++;
          f(v, s.pitch);
        }
        return released;
      }

      /** @brief Whether @p voice holds a note. False for a voice out of range. */
      bool holds(int voice) const;
      /** @brief The pitch @p voice holds, or -1 when it holds none. */
      int pitch(int voice) const;
      /** @brief How many voices hold a note. */
      int heldCount() const {
        return held;
      }

      /** @brief Every voice free and unstarted, as on a new pool; the size
       *         and the stealing switch are kept. */
      void reset();

    private:
      struct slot {
        int pitch = 0;
        // When the voice was last started; never reused, so an older start
        // always compares lower. 0: never started.
        std::uint64_t age = 0;
        bool held = false;
      };

      std::vector<slot> slots;
      int count;
      bool stealing = false;
      int held = 0;
      std::uint64_t nextAge = 1;
    };

  } // namespace DSP
} // namespace YSE

#endif
