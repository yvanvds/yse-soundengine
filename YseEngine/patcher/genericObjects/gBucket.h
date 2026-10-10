#pragma once
#include "../pObject.h"
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Pass numbers from outlet to outlet, shifting on each input —
     *         ``.bucket`` (issue #478).
     *
     *  Max's ``bucket``: "Outputs incoming values to outlets in bucket-brigade
     *  fashion. bucket acts as an n-stage shift register which can shift its
     *  contents from outlet to outlet in either direction."
     *
     *  A delay line for *events* rather than for samples. Every number that
     *  arrives pushes the previous ones one outlet along, so outlet 1 is
     *  "whatever came in one step ago", outlet 2 "two steps ago", and so on for
     *  as many outlets as the creation argument asked for. That is the canonical
     *  shape of a canon, an arpeggio echo, a comparison of a value against its
     *  own recent history, and any pattern where a value has to reappear N steps
     *  later — none of which has a natural spelling without it.
     *
     *  ### What it is not
     *
     *  ``.past``, ``.peak`` and ``.trough`` each remember **one** thing about the
     *  stream. This remembers the last N values *positionally*, and hands all of
     *  them out at once, which is the difference between "has it been higher than
     *  this?" and "what were the last four notes?".
     *
     *  ``.cycle`` (#477) also has N outlets and also advances on every message,
     *  and the two are easy to confuse from the outside. They are opposites:
     *  ``.cycle`` sends **one** message out **one** outlet and moves a cursor,
     *  where this sends the **whole register** out **every** outlet on each
     *  input. A patch that wanted "spread these notes over four voices" wants
     *  ``.cycle``; a patch that wanted "play these notes again four steps later"
     *  wants this.
     *
     *  ``.trigger`` and ``.bangbang`` fan **one** value out every outlet
     *  simultaneously. Here every outlet also fires, but each carries a
     *  *different*, older value — the fan-out is across time, not across copies.
     *
     *  Built by hand it is N ``.past``-like holders chained through N
     *  ``.trigger``s with the store-before-forward order right at every stage,
     *  and one stage wired backwards silently loses a step.
     *
     *  ### The shift, and where the new value lands
     *
     *  Max, for ``int``: "The numbers currently stored in bucket are sent out,
     *  then each number is moved one outlet to the right and the new number is
     *  stored to be sent out the left outlet the next time a number is
     *  received." So by default the register is one step **behind** the input:
     *  the value that just arrived is not in this burst, it is in the next one.
     *
     *  The second creation argument changes that. Max: "A second non-zero
     *  argument sets the bucket object to 'echo to output' mode, whereby the
     *  number received in the inlet is stored and sent out the left outlet when
     *  it is received." Same register, same shift — only the order of the shift
     *  and the send is swapped, so outlet 0 carries the value that arrived rather
     *  than the one before it. Both are one line of code apart and worlds apart
     *  in a patch, which is why the flag is a creation argument and survives a
     *  save.
     *
     *  ``L2R`` and ``R2L`` set the direction at run time. Max: ``R2L`` "sets
     *  bucket to shift its stored values from right to left ..., placing the
     *  incoming number in the rightmost outlet". The near end — where the newest
     *  value lands — is outlet 0 going left-to-right and the last outlet going
     *  right-to-left; everything else in the object is written in terms of "near"
     *  and "far" so the direction is one branch rather than two implementations.
     *
     *  ### Every outlet fires, right to left
     *
     *  Unlike ``.cycle``, a single input produces a send on **every** outlet, and
     *  they go out in Max's universal order: the last outlet first, outlet 0
     *  last. That ordering is a guarantee a patch can build on — a downstream
     *  object collecting the whole register sees the oldest value before the
     *  newest — and it holds whichever direction the register is shifting in,
     *  because the firing order is about outlets and the direction is about
     *  storage.
     *
     *  The values sent are **captured before the state changes**, and the state
     *  is settled before the first send. Both halves matter for the same reason
     *  ``.onebang``, ``.togedge``, ``.next``, ``.buddy`` and ``.cycle`` settle
     *  first: the send path is synchronous and re-entrant, so a patch looping an
     *  outlet back into the inlet arrives here again *inside* the ``Send``. It
     *  must find a register that has already shifted (or the loop shifts the same
     *  step forever), and the burst it interrupted must go on emitting the values
     *  it started with rather than the ones the loop has since written.
     *
     *  ### ``set``, which is not a silent set here
     *
     *  Max: "The word set, followed by a number, sends that number out each
     *  outlet, and stores the number as the next value to be sent out each of its
     *  outlets." Note that it *emits* — the opposite of ``.cycle``'s ``set``,
     *  ``.peak``'s and ``.past``'s, all of which reseed silently. It is not an
     *  inconsistency to iron out: those set a *cursor* or a *comparison point*,
     *  where this one sets the whole visible contents of the register, and a
     *  register whose contents changed without saying so would leave every
     *  downstream object holding a value the object no longer has.
     *
     *  ``clear`` is the silent one — Max: "The clear message resets the internal
     *  values of bucket without causing any output" — and it resets to int 0,
     *  the value a freshly created object holds.
     *
     *  ### ``freeze`` / ``thaw``, and what exactly is frozen
     *
     *  Max: ``freeze`` "suspends the bucket output, but new incoming numbers
     *  continue to shift the stored values internally"; ``thaw`` "resumes bucket
     *  output". So freezing gags the object without stopping it: the register
     *  goes on recording, and a ``thaw`` followed by a bang shows what it
     *  collected while it was quiet. It gates *every* send — bang, ``set`` and
     *  ``roll`` included — because "the bucket output" is the object's output,
     *  not one message's.
     *
     *  ### ``roll``
     *
     *  Max: "The word roll, followed by any number, causes bucket to use the
     *  value stored in its rightmost outlet as input; thus, it sends its output,
     *  shifts all stored values to the right, then stores the value which had
     *  been in the rightmost outlet in the leftmost outlet (as if it had been
     *  received in the inlet)." A rotation, in other words: nothing enters, and
     *  nothing is lost. That turns the register into a loop the patch can step —
     *  fill it with a phrase, then roll it to play the phrase round and round.
     *
     *  "Followed by **any** number" is Max saying the argument is not read, so a
     *  bare ``roll`` is accepted too rather than being treated as a different
     *  message. Max documents the rightmost outlet because it documents the
     *  default direction; the value taken here is the one at the **far** end —
     *  the one the next input would push off — so a rolling register keeps
     *  rotating rather than duplicating a value once ``R2L`` is set.
     *
     *  ### Numbers, and what a list means
     *
     *  Each stored value remembers whether it was spelled as an int or as a
     *  float and leaves as the kind it arrived as, the discipline ``.trigger``,
     *  ``.bondo``, ``.match`` and ``.cycle`` share: an object that forwards must
     *  not rewrite the type of what passes through it, or ``1 2 3`` comes back
     *  out of the register as ``1. 2. 3.`` and every downstream object that tells
     *  the two apart changes behaviour.
     *
     *  A bang is Max's "all stored values are sent out, but their position is not
     *  shifted" — a read of the register, not a value, which is the one place
     *  ``.bucket`` differs from ``.buddy``'s and ``.bondo``'s treatment of bang
     *  as a zero.
     *
     *  Max documents no ``list`` method, so a multi-number message would reach
     *  its ``int`` method with the extra atoms dropped. That is the one place
     *  this object deliberately does more: a message whose first token is a
     *  number is fed in **token by token**, each numeric token making one full
     *  shift-and-send exactly as if it had been sent on its own. A shift register
     *  is a thing you push a *sequence* through, so silently discarding all but
     *  the first element of a sequence would throw data away at the object least
     *  able to afford it, and a one-element list still behaves exactly as Max's
     *  ``int``. Non-numeric tokens inside such a message are skipped — the
     *  register stores numbers — and a message that is neither a recognised word
     *  nor number-leading is ignored, as it is in Max.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing. The object is driven by its inlet, and an
     *  emitting ``Calculate()`` would push the register one step per DSP block
     *  from a stimulus no patch sent — the rule ``.sel``, ``.trigger``,
     *  ``.past``, ``.bondo``, ``.buddy`` and ``.cycle`` establish.
     *
     *  Nothing on any path allocates, locks or blocks. The outlets and the
     *  storage are built once, on the control thread, by the parameter callbacks
     *  before the object is wired or published — at most ``MAX_PORTS`` (256), the
     *  ceiling ``.sel``, ``.trigger``, ``.bondo``, ``.buddy`` and ``.cycle``
     *  already use — and never resized afterwards. A burst is captured into a
     *  fixed ``MAX_PORTS``-entry array on the *stack* rather than into a member
     *  buffer: a member would be overwritten by a re-entrant burst mid-send,
     *  which is precisely the case the capture exists to protect, and 2 KB of
     *  stack is the cheapest honest way to make each burst independent.
     */
    PATCHER_CLASS(gBucket, YSE::OBJ::G_BUCKET)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(SetBang)
    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Most outlets — and therefore stages — the object will build.
     *
     *  256, the ceiling ``.sel``, ``.trigger``, ``.bondo``, ``.buddy`` and
     *  ``.cycle`` already use. Max documents no limit; a larger creation argument
     *  is clamped rather than honoured. Also the size of the stack array a burst
     *  is captured into, which is why it is a compile-time constant.
     */
    static constexpr int MAX_PORTS = 256;

    /** @brief Fewest outlets the object will build. */
    static constexpr int MIN_PORTS = 1;

    /**
     *  @brief Outlets with no creation argument.
     *
     *  Max: "Sets the number of outlets. If there is no argument, there will be
     *  one outlet." A one-stage bucket is a one-step delay, which is degenerate
     *  but useful and is what Max gives you.
     */
    static constexpr int DEFAULT_PORTS = 1;

    /** @brief How many outlets — and stored values — the object has. */
    int OutletCount() const {
      return (int)outputs.size();
    }

    /** @brief The value currently held for outlet @p index, or 0 out of range. */
    float StoredValue(int index) const {
      if (index < 0 || index >= (int)slots.size()) return 0.f;
      return slots[(std::size_t)index].value;
    }

    /**
     *  @brief Whether the value held for outlet @p index was spelled as a float.
     *
     *  A stored value leaves as the kind it arrived as, so this is what decides
     *  between ``SendInt`` and ``SendFloat`` on the next burst.
     */
    bool StoredIsFloat(int index) const {
      if (index < 0 || index >= (int)slots.size()) return false;
      return slots[(std::size_t)index].isFloat;
    }

    /**
     *  @brief Max's "echo to output" mode, from the second creation argument:
     *         the value that arrived is in *this* burst rather than the next.
     */
    bool EchoToOutput() const {
      return echo;
    }

    /** @brief Whether ``R2L`` is in force, so the newest value lands last. */
    bool ShiftsRightToLeft() const {
      return rightToLeft;
    }

    /** @brief Whether ``freeze`` has gagged the object's outlets. */
    bool Frozen() const {
      return frozen;
    }

  private:
    // One stage of the register. The spelling travels with the value because the
    // object forwards rather than computes: an int that came back out as a float
    // would rewrite the type of everything passing through.
    struct Slot {
      float value = 0.f;
      bool isFloat = false;
    };

    // Copy the register into `out`, which must hold at least OutletCount()
    // entries. The caller owns the storage — a stack array — so that a
    // re-entrant burst cannot overwrite a burst still being sent.
    void Capture(Slot* out) const;

    // Send `count` captured values out the outlets, last outlet first. Silent
    // while frozen: Max's freeze "suspends the bucket output", which is the
    // object's output and not one message's.
    void SendBurst(const Slot* burst, std::size_t count, YSE::THREAD thread);

    // Push one value in: capture, shift, send — in the order the mode asks for.
    // The whole int/float/roll path.
    void Accept(float value, bool isFloat, YSE::THREAD thread);

    // Move every stage one place along and put the new value at the near end.
    // Near is outlet 0 by default and the last outlet after `R2L`.
    void Shift(float value, bool isFloat);

    // Feed a number-leading message in token by token, each numeric token one
    // full input. Non-numeric tokens are skipped.
    void FeedTokens(const std::string& text, YSE::THREAD thread);

    // Rebuild the outlets and the storage from the current creation arguments,
    // docs included. Control thread only: called from the constructor and from
    // the parameter callbacks, all of which run before the object is wired or
    // published.
    void ShapePorts();

    // The creation arguments, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::vector<std::string> creationArgs;

    // One stage per outlet, index-aligned with `outputs`. Sized once by
    // ShapePorts() and never resized, so no message path allocates.
    std::vector<Slot> slots;

    // Max's "echo to output" mode. A creation argument, so it survives a save.
    bool echo = false;

    // `R2L` / `L2R`. Run-time state rather than a parameter, exactly as
    // `.cycle`'s `thresh` and `.uzi`'s `pause` are, so it does not survive a
    // DumpJSON / ParseJSON round trip.
    bool rightToLeft = false;

    // `freeze` / `thaw`. Run-time state for the same reason.
    bool frozen = false;
  };
}
}
