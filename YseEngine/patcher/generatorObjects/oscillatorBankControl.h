#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "../../headers/types.hpp"
#include "../../dsp/generators/oscillatorBank.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief The control half shared by ``~oscbank`` and ``~ioscbank``
     *         (issue #1061): numbers and messages, carried in order to the
     *         audio thread and applied there to a ``DSP::oscillatorBank``.
     *
     *  Every request (a number on an inlet, ``set``, ``clear``, ``size``
     *  and the rest) becomes an event in a ring allocated when the object is
     *  built. Senders write events and publish them with one store; the
     *  audio thread applies everything published at the top of its block, in
     *  the order sent. So ``[index 3] [freq 440] [index 4] [freq 660]`` sent
     *  in one go sets both oscillators, and nothing on any path allocates,
     *  locks or blocks. Senders take a non-blocking flag among themselves (a
     *  message can come from the control thread and from a rendering graph
     *  alike); one that loses it, or finds the ring too full for the whole
     *  request, does nothing and is counted in ``Dropped()``, as ``~click``
     *  does.
     *
     *  No sample math: events map one to one onto the bank's setters.
     */
    class oscillatorBankControl {
    public:
      /** @brief The most oscillators a wrapper allocates. */
      static constexpr UInt MAX_OSCILLATORS = 4096;

      /** @brief @p tabpoints: whether ``tabpoints <n>`` is understood
       *         (``~oscbank`` only). */
      explicit oscillatorBankControl(bool tabpoints);

      /** @brief Size the ring for a bank of @p capacity oscillators and
       *         forget anything queued. Control thread, before the object
       *         is published: allocates. */
      void Build(UInt capacity);

      // Numbers on the inlets. Any thread, no allocation. The numbers are
      // also what an unconnected inlet reads while another one carries a
      // signal (issue #1036).
      void Frequency(Flt value);
      void Magnitude(Flt value);
      void Phase(Flt value);
      void Index(Flt value);

      Flt FrequencyNumber() const {
        return frequency.load(std::memory_order_relaxed);
      }
      Flt MagnitudeNumber() const {
        return magnitude.load(std::memory_order_relaxed);
      }
      Flt IndexNumber() const {
        return index.load(std::memory_order_relaxed);
      }

      /** @brief ``set``, ``clear``, ``silence``, ``size``, ``freqsmooth``,
       *         ``magsmooth``, ``framesync`` and (when enabled)
       *         ``tabpoints``. Any thread, no allocation; anything else is
       *         ignored. */
      void Message(const std::string& message);

      /**
       *  @brief Audio thread, top of the block: apply what was sent.
       *
       *  With @p signals (an inlet carries a signal this block) the numbers
       *  sent to the frequency, magnitude and phase inlets are not applied
       *  as events: the block sets oscillators sample by sample, with the
       *  numbers as the unconnected inlets' values.
       */
      void Apply(DSP::oscillatorBank& bank, bool signals);

      /** @brief Requests refused: another sender held the ring, or it was
       *         too full. Diagnostics and tests. */
      unsigned int Dropped() const {
        return dropped.load(std::memory_order_relaxed);
      }

    private:
      enum class kind : std::uint8_t {
        FREQUENCY,
        MAGNITUDE,
        PHASE,
        PAIR,
        FADE_FROM,
        CLEAR,
        SILENCE,
        SIZE,
        FREQUENCY_SMOOTHING,
        MAGNITUDE_SMOOTHING,
        FRAME_SYNC,
        TABLE_SIZE,
      };

      struct event {
        kind what = kind::CLEAR;
        UInt target = 0;
        Flt a = 0.f;
        Flt b = 0.f;
      };

      // The referenced oscillator: the index number, truncated; -1 for one
      // that names no oscillator.
      int Referenced() const;
      bool Push(kind what, UInt target, Flt a = 0.f, Flt b = 0.f);
      // Under `writing`: room for `count` events from `tail`.
      bool Room(UInt count) const;
      void PushSet(const char* text, std::size_t from, std::size_t length);

      const bool tabpoints;

      std::vector<event> ring;
      UInt mask = 0;
      UInt capacity = 1;
      std::atomic<UInt> head{0}; // the next event the audio thread applies
      std::atomic<UInt> tail{0}; // one past the last published event
      std::atomic_flag writing = ATOMIC_FLAG_INIT;
      std::atomic<unsigned int> dropped{0};

      aFlt frequency;
      aFlt magnitude;
      aFlt index;
    };

  } // namespace PATCHER
} // namespace YSE
