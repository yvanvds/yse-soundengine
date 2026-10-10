#pragma once
#include "../pListArgs.h"
#include "../pSelector.h"
#include <cstddef>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief The tempo-relative half of Max's time-value syntax, in beats
     *         (issue #705).
     *
     *  Max's time values come in two families. The **fixed** ones —
     *  milliseconds, ``hh:mm:ss``, ``samples``, ``hz`` — need no clock beyond
     *  the one the patcher already has, and ``.delay`` and ``.metro`` already
     *  speak the milliseconds. The **tempo-relative** ones need a beat, and
     *  since #688 the patcher has one: ``clockBridge`` binds a named
     *  ``CLOCK::domainClock`` and ``messageScheduler::ScheduleBangOnClock``
     *  waits on its beat position.
     *
     *  This header is the arithmetic in between, shared rather than copied for
     *  the reason ``pSelector`` is shared (#680): two objects that read the
     *  same syntax must read it the same way, or a patch that swaps one for the
     *  other changes meaning silently. ``.delay`` and ``.metro`` are the two
     *  Max lists a ``clock`` method on, and they are the two here.
     *
     *  ### What is read, and what is deliberately not
     *
     *  - **Note values** — Max: "symbols that abbreviate musical note time
     *    values, for example ``4n`` for quarter-note", with ``d`` for dotted
     *    and ``t`` for triplet. Pure arithmetic over a beat: a quarter note
     *    *is* the beat, so ``4n`` is 1, ``8n`` is 0.5, ``1n`` is 4, dotted
     *    multiplies by 3/2 and triplet by 2/3.
     *  - **Ticks** — Max: "ticks represent 1/480th of a quarter note",
     *    written as a number followed by ``ticks``. This is the *general* beat
     *    unit, and the reason it is here rather than left out: the note values
     *    cannot spell "three beats" or "two thirds of a beat", and ``ticks``
     *    can, in Max's own vocabulary and without a meter.
     *  - **``bars.beats.units`` is not read**, and cannot be. A bar needs a
     *    meter, and ``CLOCK::domainClock`` is a bare beat accumulator — the
     *    running integral of tempo, with no bar, no meter and no downbeat
     *    anywhere in it. The same objection retires Max's ``quantize`` and
     *    ``transport`` attributes on both objects, which is the scope issue
     *    #705 was filed with.
     *  - **``ms``, ``hz``, ``samples`` and ``hh:mm:ss`` are not read here.**
     *    They are fixed times, not beats. A bare number already means
     *    milliseconds on both objects, which is Max's own default and the whole
     *    of the fixed syntax either object had before this.
     *
     *  ### The one departure from Max's table
     *
     *  Max's note-value table lists 21 of the 24 spellings the formula covers:
     *  it stops at ``64nd`` / ``64n`` / ``128n``, giving no ``64nt``, ``128nd``
     *  or ``128nt``. Those three are read here anyway, because they are the
     *  same multiplication as the other 21 and refusing them would be a table
     *  lookup pretending to be a rule. Every spelling Max *does* list is read
     *  as exactly the value Max lists — ``4nd`` is 720 ticks is 1.5 beats,
     *  ``8nt`` is 160 ticks is 1/3 — which is what the tests pin.
     *
     *  Nothing here allocates or takes a lock: the tokens are walked in place,
     *  because a message handler runs on whichever thread dispatched it and
     *  that may be the audio callback.
     */

    /** @brief Ticks per quarter note — Max: "ticks represent 1/480th of a
     *         quarter note". A quarter note is one beat on a ``domainClock``,
     *         so this is also ticks per beat. */
    constexpr double TICKS_PER_BEAT = 480.0;

    /** @brief The longest note-value denominator Max spells (``128n``). */
    constexpr int MAX_NOTE_DENOMINATOR = 128;

    /**
     *  @brief The beats one Max note-value token means — ``4n`` → 1, ``4nd`` →
     *         1.5, ``8nt`` → 1/3 — or false when the token is not one.
     *
     *  Strict: the whole token must be the note value. ``4nx`` and ``4`` are
     *  refused rather than half read, the same rule ``ReadNumericToken``
     *  applies to a number.
     */
    inline bool ReadNoteValue(const char* text, std::size_t length, double& beats) {
      if (text == nullptr || length < 2) return false;

      // The denominator. Bounded by MAX_NOTE_DENOMINATOR, so three digits is
      // already more than any spelling has and the accumulator cannot run away.
      std::size_t at = 0;
      int denominator = 0;
      while (at < length && text[at] >= '0' && text[at] <= '9') {
        if (denominator > MAX_NOTE_DENOMINATOR) return false;
        denominator = (denominator * 10) + (text[at] - '0');
        at++;
      }
      if (at == 0) return false;

      // Max's table is the powers of two from 1 to 128, and only those: there
      // is no `3n` or `6n` in the syntax, and reading one would invent a note
      // value Max does not have.
      if (denominator < 1 || denominator > MAX_NOTE_DENOMINATOR) return false;
      if ((denominator & (denominator - 1)) != 0) return false;

      if (at >= length || text[at] != 'n') return false;
      at++;

      // A quarter note is one beat on a domain clock, so `4n` is 1 beat and the
      // rest follows from the denominator.
      double value = 4.0 / (double)denominator;
      if (at < length) {
        if (text[at] == 'd') {
          value *= 1.5; // dotted: Max's 4nd is 720 ticks against 4n's 480
        } else if (text[at] == 't') {
          value *= 2.0 / 3.0; // triplet: Max's 4nt is 320 ticks
        } else {
          return false;
        }
        at++;
      }
      // Anything left over means this was never a note value.
      if (at != length) return false;

      beats = value;
      return true;
    }

    /**
     *  @brief The beats the tempo-relative time value in ``[text, text +
     *         length)`` means, or false when those tokens are not one.
     *
     *  Reads a whole time value rather than a single token, because one of the
     *  two spellings is two tokens: a note value (``4nd``) or a tick count
     *  (``1440 ticks``). Trailing separators are fine; a trailing *token* is
     *  not — ``4n 5`` is not a time value, and reading its first token would
     *  silently drop the rest.
     *
     *  Any thread; allocation-free.
     */
    inline bool ReadBeatTime(const char* text, std::size_t length, double& beats) {
      if (text == nullptr) return false;

      // First token.
      std::size_t begin = 0;
      while (begin < length && IsSelectorSeparator(text[begin]))
        begin++;
      std::size_t end = begin;
      while (end < length && !IsSelectorSeparator(text[end]))
        end++;
      if (end <= begin) return false;

      // Second token, if there is one.
      std::size_t unitBegin = end;
      while (unitBegin < length && IsSelectorSeparator(text[unitBegin]))
        unitBegin++;
      std::size_t unitEnd = unitBegin;
      while (unitEnd < length && !IsSelectorSeparator(text[unitEnd]))
        unitEnd++;

      // Whatever follows the unit is surplus: a time value is the whole of what
      // it is read from.
      std::size_t after = unitEnd;
      while (after < length && IsSelectorSeparator(text[after]))
        after++;
      if (after != length) return false;

      if (unitEnd <= unitBegin) {
        // One token: a note value, or nothing this header reads.
        return ReadNoteValue(text + begin, end - begin, beats);
      }

      // Two tokens: Max's "a number followed by ticks".
      const std::size_t unitLength = unitEnd - unitBegin;
      if (unitLength != 5) return false;
      const char* unit = text + unitBegin;
      if (unit[0] != 't' || unit[1] != 'i' || unit[2] != 'c' || unit[3] != 'k' || unit[4] != 's') {
        return false;
      }

      float ticks = 0.f;
      if (!ReadNumericToken(text + begin, end - begin, ticks)) return false;
      beats = (double)ticks / TICKS_PER_BEAT;
      return true;
    }

  } // namespace PATCHER
} // namespace YSE
