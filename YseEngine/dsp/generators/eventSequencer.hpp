/*
  ==============================================================================

    eventSequencer.hpp
    Events at phase positions, fired when a driving phase crosses them
    (issue #1069).

  ==============================================================================
*/

#ifndef YSE_DSP_GENERATORS_EVENTSEQUENCER_HPP
#define YSE_DSP_GENERATORS_EVENTSEQUENCER_HPP

#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief Stores events, each a short text at a phase position, and
     *         fires them, sample-accurately, when a driving phase signal
     *         crosses them.
     *
     *  Ported from Max's ``seq~``. The class knows nothing about messages:
     *  an event is a text of up to ``TEXT_CAPACITY - 1`` bytes (a patcher
     *  stores an atom list's text there) and firing an event means listing
     *  it, with its sample offset in the block, in ``fired()``.
     *
     *  **Sequences.** Every event belongs to a numbered sequence; any
     *  number of sequences can be stored, up to ``capacity()`` events in
     *  all. One sequence, ``select()``, is the current one: it is the one
     *  played and recorded into. ``add()`` stores an event in any sequence,
     *  ``remove()`` deletes events by phase range and text, ``clear()``
     *  empties a sequence and ``clearAll()`` all of them.
     *
     *  **Playing.** The block call takes the driving phase, normally a
     *  ``~phasor`` running 0 to 1. While ``play()`` is on, each sample
     *  fires the current sequence's events with a phase in ``(previous,
     *  current]``, in phase order (equal phases in the order they were
     *  stored). A fall of more than 0.5 is a wrap: the events above the
     *  previous phase fire, then those from the bottom up to the current
     *  one. A smaller fall is the phase moving back, and fires nothing. The
     *  first sample after play starts fires the events at exactly its
     *  phase too, so a sequence started on a phase of 0 plays its events at
     *  0. Phases outside 0..1 are stored and crossed like any other.
     *
     *  **Recording.** ``recordEvent()`` stores an event in the current
     *  sequence at the phase of the last sample, while ``record()`` or
     *  ``overdub()`` is on. Turning ``record()`` on empties the current
     *  sequence first; ``overdub()`` adds to what is there. An event
     *  recorded at the phase being played fires the next time round, not at
     *  once. The phase is only known per sample, so a recording made
     *  between two blocks takes the phase the last block ended on.
     *
     *  **Firing.** ``fired()`` lists the events the last block call fired,
     *  each with its text, phase and sample offset, up to
     *  ``firedCapacity()`` per block; the rest are counted in ``missed()``.
     *  The texts point into the store and stay valid until the next edit or
     *  block call.
     *
     *  Nothing here depends on the sample rate: the timing is the driving
     *  signal's. Apart from the constructor, call it from one thread at a
     *  time, the audio thread once it renders. Nothing but the constructor
     *  allocates: the store, the free list and the fired list are sized to
     *  their capacities there. A block is at most ``STANDARD_BUFFERSIZE``
     *  long (a longer one is processed up to that length).
     */
    class API eventSequencer {
    public:
      /** @brief The capacity of a default-constructed sequencer, in events
       *         over all sequences. */
      static constexpr UInt DEFAULT_EVENTS = 1024;
      /** @brief The largest capacity; a larger request is clamped. */
      static constexpr UInt MAX_EVENTS = 65536;
      /** @brief The room for one event's text, the terminator included. */
      static constexpr UInt TEXT_CAPACITY = 128;
      /** @brief The default number of events one block call can fire. */
      static constexpr UInt DEFAULT_FIRED = 256;

      /** @brief One event fired by the last block call. */
      struct firing {
        // Terminated; valid until the next edit or block call.
        const char* text;
        UInt length;
        Flt phase;
        // The sample in the block that fired it.
        UInt offset;
      };

      /** @brief A sequencer with room for @p capacity events (1 to
       *         ``MAX_EVENTS``) and @p firedCapacity events fired per block
       *         (at least 1). Allocates. */
      explicit eventSequencer(UInt capacity = DEFAULT_EVENTS, UInt firedCapacity = DEFAULT_FIRED);

      /** @brief How many events the store holds at most. */
      UInt capacity() const {
        return static_cast<UInt>(entries.size());
      }

      /** @brief How many events one block call can fire. */
      UInt firedCapacity() const {
        return static_cast<UInt>(firings.size());
      }

      /** @brief How many events are stored, over all sequences. */
      UInt size() const {
        return stored;
      }

      /** @brief How many events sequence @p sequence holds. */
      UInt count(Int sequence) const;

      /**
       *  @brief Stores an event: @p text (@p length bytes) at @p phase in
       *         sequence @p sequence.
       *
       *  False, changing nothing, when the store is full, @p phase is not a
       *  finite number, @p text is null or empty, @p length is not under
       *  ``TEXT_CAPACITY``, or the text holds a line break or a 0 byte.
       */
      bool add(Int sequence, Flt phase, const char* text, UInt length);

      /**
       *  @brief Deletes the events of @p sequence with a phase from @p from
       *         to @p to (either order, both included) and, when @p text is
       *         not null, the text @p text (@p length bytes). Returns how
       *         many went.
       */
      UInt remove(Int sequence, Flt from, Flt to, const char* text = nullptr, UInt length = 0);

      /** @brief Deletes every event of @p sequence. Returns how many
       *         went. */
      UInt clear(Int sequence);

      /** @brief Deletes every event of every sequence. */
      eventSequencer& clearAll();

      /** @brief Sets the current sequence, the one played and recorded
       *         into. Default 0. */
      eventSequencer& select(Int sequence);
      Int selected() const {
        return current;
      }

      /** @brief Plays the current sequence from where the phase is, or
       *         stops. Default off. */
      eventSequencer& play(bool on);
      bool playing() const {
        return isPlaying;
      }

      /** @brief Records into the current sequence, emptying it first, or
       *         stops. Default off. */
      eventSequencer& record(bool on);
      bool recording() const {
        return isRecording;
      }

      /** @brief Records into the current sequence, keeping what is there,
       *         or stops. Default off. */
      eventSequencer& overdub(bool on);
      bool overdubbing() const {
        return isOverdubbing;
      }

      /**
       *  @brief Stores @p text in the current sequence at ``phase()``, when
       *         ``record()`` or ``overdub()`` is on.
       *
       *  False, changing nothing, when neither is on, or for the reasons
       *  ``add()`` gives.
       */
      bool recordEvent(const char* text, UInt length);

      /** @brief The phase of the last sample; 0 before the first. */
      Flt phase() const {
        return last;
      }

      /** @brief The sequence of the @p index -th stored event, sorted by
       *         sequence, phase and the order they were stored; 0 past the
       *         end. */
      Int eventSequence(UInt index) const {
        return index < stored ? entries[index].sequence : 0;
      }

      /** @brief The phase of the @p index -th stored event; 0 past the
       *         end. */
      Flt eventPhase(UInt index) const {
        return index < stored ? entries[index].phase : 0.f;
      }

      /** @brief The text of the @p index -th stored event, terminated; ""
       *         past the end. */
      const char* eventText(UInt index) const;

      /** @brief The length of the @p index -th stored event's text; 0 past
       *         the end. */
      UInt eventLength(UInt index) const {
        return index < stored ? lengths[entries[index].slot] : 0;
      }

      /**
       *  @brief One block: reads the driving phase from @p input and fires
       *         the events it crosses.
       *
       *  A sample that is not a finite number holds the previous phase.
       */
      eventSequencer& operator()(buffer& input, UInt length = STANDARD_BUFFERSIZE);

      /** @brief How many events the last block call fired. */
      UInt fired() const {
        return firedCount;
      }

      /** @brief Fired event @p index of the last block call, in the order
       *         they fired. @p index must be under ``fired()``. */
      const firing& firedEvent(UInt index) const {
        return firings[index];
      }

      /** @brief Events the last block call crossed but had no room to list
       *         in ``fired()``. */
      UInt missed() const {
        return missedCount;
      }

      /** @brief Back to the state after construction, keeping the
       *         capacities. */
      eventSequencer& reset();

    private:
      struct entry {
        Int sequence;
        Flt phase;
        // The order it was stored in, for equal phases.
        UInt order;
        // Its text's place in `texts`.
        UInt slot;
      };

      // Finds `low` and `high`, the current sequence's events.
      void Range();
      // The first event of the range with a phase above `p`.
      UInt Above(Flt p) const;
      // The first event of the range with a phase of at least `p`.
      UInt AtLeast(Flt p) const;
      void Fire(UInt index, UInt offset);
      char* Text(UInt slot) {
        return &texts[static_cast<std::size_t>(slot) * TEXT_CAPACITY];
      }
      const char* Text(UInt slot) const {
        return &texts[static_cast<std::size_t>(slot) * TEXT_CAPACITY];
      }

      std::vector<entry> entries;
      std::vector<char> texts;
      std::vector<UInt> lengths;
      std::vector<UInt> freeSlots;
      std::vector<firing> firings;
      UInt stored = 0;
      UInt freeCount = 0;
      UInt nextOrder = 0;
      UInt firedCount = 0;
      UInt missedCount = 0;

      Int current = 0;
      bool isPlaying = false;
      bool isRecording = false;
      bool isOverdubbing = false;

      // The current sequence's events are [low, high); `cursor` is the next
      // to fire. `dirty`: they need finding again after an edit. `seek`: the
      // next sample also fires the events at exactly its phase. `primed`: a
      // sample has been read into `last`.
      UInt low = 0;
      UInt high = 0;
      UInt cursor = 0;
      bool dirty = true;
      bool seek = false;
      bool primed = false;
      Flt last = 0.f;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_GENERATORS_EVENTSEQUENCER_HPP
