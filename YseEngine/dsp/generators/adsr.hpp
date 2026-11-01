/*
  ==============================================================================

    adsr.hpp
    A parametric attack / decay / sustain / release envelope (issue #1064).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_ADSR_HPP
#define YSE_DSP_GENERATORS_ADSR_HPP

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A gate- or event-triggered ADSR envelope with its times in
     *         milliseconds: the standard synth envelope.
     *
     *  Unlike ``ADSRenvelope``, which plays a breakpoint shape, this one is
     *  set by four numbers. A trigger with a non-zero amplitude starts it:
     *  the level rises in a straight line to the amplitude over the attack
     *  time, falls to ``sustain`` times the amplitude over the decay time and
     *  holds there. A release (a trigger of 0) takes it from wherever it is
     *  down to 0 over the release time. A negative amplitude gives a
     *  negative envelope; a sustain above 1 holds above the peak.
     *
     *  Each segment is a straight line, counted in samples at the live
     *  ``SAMPLERATE``, read on every sample. A segment of N samples reaches
     *  its target on its Nth sample; a time of 0 (or less, or not a finite
     *  number) reaches it on the first. A time that changes while its
     *  segment runs keeps the fraction already covered. The sustain level is
     *  read on every sample too, so a change moves the decay target and the
     *  held level at once.
     *
     *  **Triggers.** ``trigger(amplitude)`` is an event: a non-zero amplitude
     *  starts (or restarts) the envelope at the first sample of the next
     *  block call, 0 releases it there. The latest event before a block call
     *  wins. A block call with a gate signal also reads triggers from it: a
     *  sample going from 0 to non-zero starts the envelope at that sample's
     *  value, one going back to 0 releases it. Changes between two non-zero
     *  values do nothing, so the gate must return to 0 before it can
     *  retrigger. A gate sample that is not a finite number counts as 0.
     *
     *  **Retriggering.** A start while the level is not 0 first ramps it
     *  down to 0 over the retrigger time (default 5 ms) to avoid a click,
     *  then runs the attack from 0. With a retrigger time of 0 the attack
     *  starts at once from 0. With ``legato(true)`` there is no ramp down:
     *  the attack runs from the current level to the new amplitude.
     *
     *  **maxSustain.** A value of 0 or more limits the sustain to that many
     *  milliseconds, after which the release starts on its own; 0 makes an
     *  attack / decay / release envelope. Negative (the default) holds until
     *  released.
     *
     *  **Play to end.** With ``playToEnd(true)`` (``~adsr``'s
     *  ``triggermode 1``) a start runs the whole envelope: attack, decay, a
     *  sustain of ``maxSustain`` milliseconds (none when that is negative)
     *  and release, ignoring every trigger and release until it is back at
     *  rest.
     *
     *  Besides the envelope, each block call fills a gate output that is 1
     *  during the attack, decay and sustain and 0 during the release, a
     *  retrigger ramp and at rest, and records whether the envelope started
     *  from rest (``started()``) or came to rest (``finished()``) in it.
     *
     *  Call it from one thread at a time. Processing allocates nothing: both
     *  outputs are sized to ``STANDARD_BUFFERSIZE`` in the constructor and a
     *  block is at most that long (a longer one is processed up to that
     *  length).
     */
    class API adsr {
    public:
      /** @brief The segment the envelope is in. */
      enum STAGE {
        IDLE, ///< at rest, output 0
        ATTACK,
        DECAY,
        SUSTAIN,
        RELEASE,
        RETRIGGER, ///< the ramp down to 0 before a restart
      };

      adsr();

      /** @brief One block from events and the set times and sustain. */
      buffer& operator()(UInt length = STANDARD_BUFFERSIZE);

      /** @brief One block triggered by a gate signal (and events), with the
       *         set times and sustain. */
      buffer& operator()(buffer& gate);

      /** @brief One block from events, with a per-sample attack, decay and
       *         release in milliseconds and sustain level. The block is as
       *         long as the shortest of the four. */
      buffer& operator()(buffer& attacks, buffer& decays, buffer& sustains, buffer& releases);

      /** @brief One block triggered by a gate signal (and events), with a
       *         per-sample attack, decay, sustain and release. The block is
       *         as long as the shortest of the five. */
      buffer& operator()(buffer& gate, buffer& attacks, buffer& decays, buffer& sustains,
                         buffer& releases);

      /** @brief The gate output of the last block call: 1 during attack,
       *         decay and sustain, else 0. */
      buffer& gate();

      /** @brief True when the envelope started from rest in the last block
       *         call. */
      bool started() const;

      /** @brief True when the envelope came to rest in the last block
       *         call. */
      bool finished() const;

      /** @brief True unless the envelope is at rest. */
      bool active() const;

      /** @brief The segment after the last sample processed. */
      STAGE stage() const;

      /** @brief The level of the last sample processed. */
      Flt level() const;

      /** @brief A non-zero amplitude starts the envelope at the next block
       *         call, 0 releases it. Not a finite number: ignored. */
      adsr& trigger(Flt amplitude);

      /** @brief Sets the attack time in milliseconds. Default 0. */
      adsr& attack(Flt ms);

      /** @brief The attack time in milliseconds. */
      Flt attack() const;

      /** @brief Sets the decay time in milliseconds. Default 0. */
      adsr& decay(Flt ms);

      /** @brief The decay time in milliseconds. */
      Flt decay() const;

      /** @brief Sets the sustain level as a factor of the amplitude.
       *         Default 1. */
      adsr& sustain(Flt factor);

      /** @brief The sustain factor. */
      Flt sustain() const;

      /** @brief Sets the release time in milliseconds. Default 0. */
      adsr& release(Flt ms);

      /** @brief The release time in milliseconds. */
      Flt release() const;

      /** @brief Sets the ramp-down time before a non-legato restart, in
       *         milliseconds. Default 5. */
      adsr& retrigger(Flt ms);

      /** @brief The retrigger ramp time in milliseconds. */
      Flt retrigger() const;

      /** @brief Sets the longest sustain in milliseconds; negative (the
       *         default) is no limit. */
      adsr& maxSustain(Flt ms);

      /** @brief The sustain limit in milliseconds, -1 for none. */
      Flt maxSustain() const;

      /** @brief True restarts from the current level without a ramp down.
       *         Default false. */
      adsr& legato(bool on);

      /** @brief True in legato mode. */
      bool legato() const;

      /** @brief True runs every start to the end, ignoring triggers until
       *         at rest. Default false. */
      adsr& playToEnd(bool on);

      /** @brief True when every start runs to the end. */
      bool playToEnd() const;

      /** @brief Back to rest at once, with no pending event; keeps the
       *         settings. */
      adsr& reset();

    private:
      buffer& run(UInt length, const Flt* gates, const Flt* attacks, const Flt* decays,
                  const Flt* sustains, const Flt* releases);
      void begin(Flt amplitude);
      void end();
      void enter(STAGE next);
      bool ramp(Flt ms, Flt target);

      Flt attackMs;
      Flt decayMs;
      Flt sustainLevel;
      Flt releaseMs;
      Flt retriggerMs;
      Flt maxSustainMs;
      bool legatoMode;
      bool playEnd;

      STAGE current;
      Flt amplitude; // the peak of the running envelope
      Flt from; // the level the running segment started at
      Dbl progress; // the fraction of the running segment covered
      Dbl held; // samples spent in the sustain
      Flt value; // the level of the last sample
      bool gateHigh; // the last gate sample was non-zero
      bool pending;
      Flt pendingAmplitude;
      bool startedFlag;
      bool finishedFlag;
      buffer output;
      buffer gateOutput;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_ADSR_HPP
