#pragma once
#include "gExtremum.h"
#include <string>

// Declares one member of the running-extreme family (issue #463).
// Mirrors EXTREMUM_CLASS from gExtremum.h: the whole body lives in
// gRunningExtremumBase and the constructor in gRunningExtremum.cpp only has to
// pick an ordering, a starting value and the direction-specific documentation.
#define RUNNING_EXTREMUM_CLASS(className, typeName)                                                \
  class className : public gRunningExtremumBase {                                                  \
  public:                                                                                          \
    className();                                                                                   \
    const char* Type() const override {                                                            \
      return typeName;                                                                             \
    }                                                                                              \
    CREATE(className)                                                                              \
  };

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Shared body for ``.peak`` and ``.trough`` (issue #463).
     *
     *  A **running** extreme: the highest (``.peak``) or lowest (``.trough``)
     *  number the object has seen, kept and reported only when a new one
     *  arrives. Peak-hold metering, envelope maxima, worst-case latency
     *  displays and "only react when this gets worse" logic — anything where
     *  the interesting event is the record being broken rather than each
     *  individual value.
     *
     *  ### Not ``.maximum`` / ``.minimum``, and the difference is the object
     *
     *  This is the distinction #462 spends a section on from the other side,
     *  and it is worth stating in both places because the two pairs look
     *  identical in a patch and behave nothing alike:
     *
     *  - ``.maximum`` **compares and forgets.** Its stored comparand is a
     *    sticky right *operand* that a number on inlet 0 never moves, so
     *    ``.maximum 5`` fed 10 emits 10 and still holds 5, and fed 3 next emits
     *    5 again. It answers every message and it is a ``.clip`` whose bound is
     *    itself a signal.
     *  - ``.peak`` **stores what it emits.** A number that beats the stored
     *    extreme *becomes* it, so the same 10 leaves 10 behind and the
     *    following 3 produces nothing at all on the value outlet. It answers
     *    only when the record moves, and it is a latch.
     *
     *  The pieces the two pairs genuinely share — the ordering predicates and
     *  the non-finite substitution — are the free functions in ``gExtremum.h``,
     *  which is why they are free functions rather than members of either base.
     *  ``ExtremumBest`` and ``ExtremumScan`` are deliberately *not* used here:
     *  this object needs the predicate's yes/no answer for the flag outlets
     *  below, not merely the winner, and its list is a two-element Max idiom
     *  rather than a reduction over the whole list.
     *
     *  ### Shape — two inlets and three outlets, as in Max
     *
     *  - inlet 0 (hot) — an int or float is offered to the stored extreme; a
     *    bang reports it; a list and the message words below are described
     *    further down.
     *  - inlet 1 (cold) — an int or float **reseeds** the extreme *and sends
     *    it out*. Max: "The number is stored in peak as the new peak value, and
     *    is sent out", with the left outlet's "(A number received in the right
     *    inlet is always the new peak value.)" settling that the flag outlets
     *    fire for it too. Note that this is the opposite of ``.maximum``'s cold
     *    inlet, which is silent — here the reseed is itself a peak event.
     *  - outlet 0 (float) — new extremes. **Silent when the input does not beat
     *    the stored value**, which is the whole point of the object.
     *  - outlet 1 (int) — 1 when the number just received was a new extreme, 0
     *    when it was not.
     *  - outlet 2 (int) — the same answer inverted: 0 for a new extreme, 1 for
     *    a rejection.
     *
     *  Outlet 2 carries no information outlet 1 does not, and it is ported all
     *  the same because Max ships it: it is what makes "do something when the
     *  value was *not* a record" one cord rather than a cord plus a ``.==``,
     *  and a patch translated from Max would otherwise silently lose an outlet.
     *
     *  The three are sent **right to left**, as ``.mean`` and ``.cartopol``
     *  already do, so anything triggered by the value on outlet 0 already sees
     *  the matching flags rather than the previous message's. The stored
     *  extreme is settled before any of them, so nothing reached from an outlet
     *  can observe the object half-updated.
     *
     *  ### A bang, and what it reports before any input
     *
     *  Max: "Sends the currently stored peak value out the left outlet." The
     *  flag outlets stay quiet — a bang receives no number, so there is nothing
     *  for them to be an answer about.
     *
     *  Before any input the stored extreme is the ``initial`` creation
     *  argument, so a bang reports that rather than nothing. Silence was
     *  rejected for the reason ``.accum`` and ``.maximum`` give: in a headless
     *  patcher an object that answers nothing is invisible rather than merely
     *  empty, and priming a patch by banging it once on load is the normal way
     *  to get a starting value onto the cords.
     *
     *  ### Where the two directions start, and why ``.trough`` starts at 128
     *
     *  Max's defaults, kept: 0 for ``peak`` and 128 for ``trough``. The
     *  asymmetry looks like an oversight and is not. A running extreme is only
     *  useful if its starting value can lose, and the identity that can always
     *  lose is ±∞ — which this family refuses to store, since an infinity that
     *  *wins* pins the outlet for good. So the starting value has to be a
     *  finite number chosen to be beatable, and 0 is beatable from above but
     *  not from below: a ``.trough`` starting at 0 would reject every positive
     *  number for the rest of the patch's life and never emit anything. Max's
     *  128 is the top of the MIDI range, which is where the object's original
     *  inputs came from, and it stays the right order of magnitude for the
     *  0-127 and 0-1 signals a patcher actually carries. A patch that knows its
     *  range says so in the creation argument, which is what the argument is
     *  for.
     *
     *  ### ``reset`` and ``set`` — a running extreme needs a way back
     *
     *  Max gives neither object a reset: the only way back is a number on the
     *  right inlet, which reseeds *and announces*. That is not enough. A
     *  peak-hold display cleared between takes wants the hold dropped without
     *  a spurious peak event going downstream, and a patch that has to repeat
     *  its own creation argument in a message box to get back to where it
     *  started has lost that argument's meaning. So two silent messages on
     *  inlet 0 complete the object:
     *
     *  - ``reset`` — back to the ``initial`` creation argument, emitting
     *    nothing. The family convention, with exactly the meaning ``.counter``
     *    and ``.accum`` already give the word: back to where the object
     *    started, not to zero.
     *  - ``set <n>`` — stores @em n as the extreme, emitting nothing. The
     *    silent twin of the cold inlet, spelled as ``.accum`` spells the same
     *    idea, and the message the issue's notes asked for.
     *
     *  Silence is the point of both. The cold inlet already covers "reseed and
     *  announce"; a second spelling of that would be redundant, whereas "reseed
     *  and stay quiet, so that the *next* input is the next peak event" is
     *  otherwise unreachable.
     *
     *  ``clear`` is deliberately **not** accepted, for the reason ``.accum``
     *  gives: on an object created as ``.peak 5`` the word could mean either 0
     *  or 5, and a message whose meaning depends on the creation argument is
     *  worse than a message that does not exist. ``reset`` says which one it
     *  means.
     *
     *  ### A list
     *
     *  Max's, verbatim: "The second number is stored as the new peak value and
     *  is sent out, then the first number is received in the left inlet." That
     *  is the standard Max idiom of a two-element list filling the inlets right
     *  to left, written out explicitly because this patcher does not
     *  distribute lists across inlets on its own.
     *
     *  So ``5 100`` into a ``.peak`` sets the extreme to 100 and emits it, then
     *  offers 5 — which loses. One message, two events, in that order. Numbers
     *  past the second are ignored: Max documents exactly two arguments, and
     *  guessing at a third (a reduction? a queue?) would invent behaviour.
     *
     *  A one-element list is not a list. It takes the int / float path and is
     *  offered to the extreme like any other number, as it is in Max.
     *
     *  A message with no numbers in it at all and no recognised word is
     *  ignored rather than guessed at, as in ``.slide``, ``.mean``, ``.accum``
     *  and ``.maximum``.
     *
     *  ### One numeric type
     *
     *  Everything is a float and outlet 0 is a float outlet; Max's
     *  int-unless-the-creation-argument-has-a-decimal-point duality is not
     *  reproduced, for the reason ``.accum`` and ``.maximum`` set out at
     *  length — ``.peak 5`` and ``.peak 5.0`` are the same parameter string in
     *  this patcher, so the mode would have to be guessed from the text of the
     *  argument, and guessing integral would silently round a peak a patch is
     *  metering with. A patch that wants integers puts a ``.round`` on the
     *  outlet. The two flag outlets *are* int outlets, because 0 and 1 are the
     *  only values they can carry.
     *
     *  ### Non-finite input
     *
     *  Read as 0 wherever it arrives — the convention ``./``, ``.sqrt``,
     *  ``.zmap``, ``.clip``, ``.slide``, ``.mean``, ``.accum``, ``.maximum``
     *  and ``.minimum`` all follow, applied here through the shared
     *  ``ExtremumSanitize``.
     *
     *  It is load-bearing rather than tidy, and more so than for ``.maximum``,
     *  because this object *keeps* what it accepts. A stored NaN would lose
     *  every comparison, so no later number could ever displace it and the
     *  object would report a NaN to every bang forever. A stored +∞ would beat
     *  every number a ``.trough`` could ever be sent — and a stored -∞ the same
     *  for a ``.peak`` — so one stray value from a neighbouring object would
     *  silence the outlet permanently. Either way the object would stop working
     *  without saying so, and unlike ``.maximum`` it would not recover on the
     *  next message.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlets, not
     *  by the DSP tick — and every emitting step goes through Offer(), Reseed()
     *  or Report(). This is the rule ``.slide``, ``.mean``, ``.accum`` and
     *  ``.maximum`` establish: a hot inlet fires CalculateIfReady() after
     *  *every* message type it accepts, so a Calculate() that emitted would
     *  make ``set`` and ``reset`` emit too, would send a second stale value
     *  after every list, and would have to guess what to send after a bang.
     *
     *  Nothing on any path allocates, locks or blocks. A number is one
     *  finiteness branch, one indirect call and at most three Sends; a bang is
     *  one Send; a message is a fixed-length prefix comparison against two
     *  words plus at most two numbers read through the shared,
     *  allocation-free and locale-free ``ExprParseFloatList`` into a stack
     *  array. The outlets carry a float and two ints, so there is no
     *  ``std::string`` on the message path at all.
     */
    class gRunningExtremumBase : public pObject {
    public:
      gRunningExtremumBase(ExtremumOrder order, float defaultInitial);

      void SetMessage(const std::string&, float) override {}
      void Calculate(YSE::THREAD) override {}

      void SetLeftFloat(float value, int inlet, YSE::THREAD thread);
      void SetLeftInt(int value, int inlet, YSE::THREAD thread);
      void SetLeftBang(int inlet, YSE::THREAD thread);
      void SetLeftList(const std::string& value, int inlet, YSE::THREAD thread);
      void SetRightFloat(float value, int inlet, YSE::THREAD thread);
      void SetRightInt(int value, int inlet, YSE::THREAD thread);

      void ParseParams();

      std::string GetGuiValue() override;

      /**
       *  @brief Most numbers read out of one list message.
       *
       *  Two, because that is the whole of Max's list rule for these objects
       *  ("input" and "peak"); anything past the second is ignored. Reading
       *  into a two-element stack array rather than the 256 ``.maximum`` needs
       *  is not a saving so much as an honesty: this object has nothing to do
       *  with a third number.
       */
      static constexpr int MAX_LIST_ITEMS = 2;

      /** @brief The running extreme — what the object has kept, and what a
       *         bang reports. */
      float Extreme() const {
        return extreme;
      }

      /** @brief The creation argument: where the extreme starts and the value
       *         ``reset`` returns to. */
      float Initial() const {
        return initial;
      }

    protected:
      // Fills in the pieces of documentation that differ per direction; the
      // base constructor already set the category and built the ports. RT-cold
      // — constructor use only.
      void Document(const char* summary, const char* inletDoc, const char* seedDoc,
                    const char* outletDoc, const char* defaultValue, const char* paramDoc);

    private:
      // Offer a number to the extreme: it is kept and sent out only if it beats
      // what is stored, and either way the two flag outlets report which
      // happened. Max's left-inlet rule.
      void Offer(float value, YSE::THREAD thread);

      // Replace the extreme and announce it — Max's right-inlet rule, where the
      // new value is *always* a new extreme, flags and all.
      void Reseed(float value, YSE::THREAD thread);

      // Replace the extreme without saying anything. `reset` and `set <n>`.
      void Store(float value);

      // The two flag outlets, sent right to left ahead of the value outlet.
      void Report(bool isNew, YSE::THREAD thread);

      // Which direction wins. Never null — the derived constructors are the
      // only callers and both pass a function.
      ExtremumOrder beats;

      // The creation argument. A plain float parameter, applied to `extreme` by
      // ParseParams() so a saved `.peak 5` starts at 5, and the value `reset`
      // returns to.
      float initial;

      // The running extreme. Always finite: every path into it goes through
      // ExtremumSanitize.
      float extreme;
    };

    RUNNING_EXTREMUM_CLASS(gPeak, YSE::OBJ::G_PEAK)
    RUNNING_EXTREMUM_CLASS(gTrough, YSE::OBJ::G_TROUGH)

  } // namespace PATCHER
} // namespace YSE
