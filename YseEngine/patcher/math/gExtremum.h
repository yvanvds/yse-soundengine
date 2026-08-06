#pragma once
#include "../pObject.h"
#include <cmath>
#include <string>

// Declares one member of the running-comparison family (issue #462).
// Mirrors COMPARE_CLASS from gCompare.h: the whole two-inlet body lives in
// gExtremumBase and the constructor in gExtremum.cpp only has to pick an
// ordering and fill in the direction-specific documentation.
#define EXTREMUM_CLASS(className, typeName)                                                        \
  class className : public gExtremumBase {                                                         \
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
     *  @brief Which of two numbers wins — the one thing ``.maximum`` and
     *         ``.minimum`` disagree about.
     *
     *  Returns true when @p candidate should displace @p incumbent. Strict, so
     *  a tie keeps the incumbent: the two are numerically indistinguishable on
     *  the outlet, but "the stored value is sent out" is what Max documents for
     *  the equal case and an object that swapped on a tie would churn its state
     *  for nothing.
     *
     *  A plain function pointer rather than a template parameter, following
     *  ``gCompareBase``: one indirect call on the message path, one shared
     *  translation unit for the body, and no template instantiation per
     *  direction.
     */
    typedef bool (*ExtremumOrder)(float candidate, float incumbent);

    /// The ordering ``.maximum`` (and, in #463, ``.peak``) uses.
    inline bool ExtremumGreater(float candidate, float incumbent) {
      return candidate > incumbent;
    }

    /// The ordering ``.minimum`` (and, in #463, ``.trough``) uses.
    inline bool ExtremumLess(float candidate, float incumbent) {
      return candidate < incumbent;
    }

    /**
     *  @brief Replaces a non-finite number with 0.
     *
     *  The convention ``./``, ``.sqrt``, ``.zmap``, ``.clip``, ``.slide``,
     *  ``.mean`` and ``.accum`` already use, and here it is load-bearing rather
     *  than merely tidy: a NaN compares false against everything, so an
     *  unfiltered NaN incumbent would lose every comparison and still be the
     *  value sent out, and an unfiltered NaN candidate would lose every
     *  comparison and be silently discarded. Either way the object would stop
     *  reporting extremes without saying so. An infinity is worse still — it
     *  wins (or loses) every comparison for the rest of the object's life, so
     *  one stray value from a neighbour would pin the outlet permanently.
     *
     *  Branch-only: no allocation, no lock, no I/O.
     */
    inline float ExtremumSanitize(float value) {
      return std::isfinite(value) ? value : 0.f;
    }

    /**
     *  @brief The better of two numbers under @p beats.
     *
     *  ``beats(candidate, incumbent) ? candidate : incumbent`` — Max's rule
     *  for the scalar case, verbatim: "If the number is greater than the value
     *  currently stored ... it is sent out the outlet. Otherwise, the stored
     *  value is sent out."
     */
    inline float ExtremumBest(float candidate, float incumbent, ExtremumOrder beats) {
      return beats(candidate, incumbent) ? candidate : incumbent;
    }

    /**
     *  @brief One pass over @p items giving both the winner and the runner-up.
     *
     *  @p best receives the extreme under @p beats and @p runnerUp the next one
     *  along — the second element in sorted order, counting duplicates, so a
     *  list of ``5 5 1`` has 5 as its runner-up. ``.maximum`` and ``.minimum``
     *  need both because Max's list rule is not simply "emit the extreme": "The
     *  numbers in the list are all compared to each other, and the greatest
     *  value is sent out the outlet. The value stored in maximum is replaced by
     *  the next greatest value in the list."
     *
     *  Both outputs are written even for a degenerate call: an empty list gives
     *  0/0 and a one-element list gives that element twice, so a caller can
     *  never read an uninitialised value out of it.
     *
     *  Seeded from the *first two* elements rather than from the first one
     *  twice. Seeding both from ``items[0]`` looks equivalent and is not: the
     *  runner-up would then start out equal to the winner, so for ``5 3`` the 3
     *  would fail both the "beats the best" and the "beats the runner-up" test
     *  and the runner-up would stay 5.
     *
     *  Linear in @p count with a bounded array supplied by the caller: no
     *  allocation, no lock, no I/O, and no sort.
     */
    inline void ExtremumScan(const float* items, int count, ExtremumOrder beats, float& best,
                             float& runnerUp) {
      if (items == nullptr || count <= 0) {
        best = 0.f;
        runnerUp = 0.f;
        return;
      }

      if (count == 1) {
        best = items[0];
        runnerUp = items[0];
        return;
      }

      if (beats(items[1], items[0])) {
        best = items[1];
        runnerUp = items[0];
      } else {
        best = items[0];
        runnerUp = items[1];
      }

      for (int i = 2; i < count; i++) {
        const float value = items[i];
        if (beats(value, best)) {
          runnerUp = best;
          best = value;
        } else if (beats(value, runnerUp)) {
          runnerUp = value;
        }
      }
    }

    /**
     *  @brief Shared body for ``.maximum`` and ``.minimum`` (issue #462).
     *
     *  Compares the numbers arriving on inlet 0 against a stored comparand and
     *  sends the winner out. The everyday use is clamping against a *dynamic*
     *  bound — a ``.clip`` whose limit is itself a signal — and combining two
     *  control sources into one, which is why the bound lives on its own inlet
     *  rather than in a creation argument only.
     *
     *  ### Shape — two inlets, one float outlet, as in Max
     *
     *  - inlet 0 (hot) — an int or float is compared with the stored comparand
     *    and the winner is sent out. A bang re-sends the most recent output. A
     *    list is reduced (see below).
     *  - inlet 1 (cold) — an int or float **replaces the comparand and emits
     *    nothing**. Max: "The number is stored for comparison with subsequent
     *    numbers received in the left inlet."
     *
     *  ### The comparand is *not* a running extreme
     *
     *  This is the distinction that decides what the object is for, and it is
     *  easy to get backwards. A number arriving on inlet 0 is compared and
     *  forgotten: it never becomes the comparand. So ``.maximum 5`` fed 10
     *  emits 10 and still holds 5, and fed 3 next emits 5 again. The object is
     *  a *stateless* two-operand max whose right operand happens to be
     *  sticky — the ``.>`` of the pair, not its ``.accum``.
     *
     *  The running-extreme reading of the same idea is Max's ``peak`` /
     *  ``trough``, which is issue #463 and a separate object precisely because
     *  the two behaviours cannot both live in one: ``peak`` stores what it
     *  emits and only speaks when a new extreme arrives, where this one answers
     *  every message and never moves. The ordering functions, the non-finite
     *  substitution and the list scan above are the pieces both need, which is
     *  why they are free functions in this header rather than members here.
     *
     *  ### A list
     *
     *  Max's rule, verbatim: "The numbers in the list are all compared to each
     *  other, and the greatest value is sent out the outlet. The value stored
     *  in maximum is replaced by the next greatest value in the list. The
     *  maximum object accepts lists of up to 256 elements."
     *
     *  Three consequences worth stating, because none of them is obvious:
     *
     *  - the stored comparand does **not** take part in the comparison — a list
     *    is reduced against itself alone;
     *  - the comparand *is* overwritten afterwards, which is the one path by
     *    which inlet 0 changes it. The runner-up is a deliberate choice on
     *    Max's part rather than an accident: leaving the winner behind would
     *    make every subsequent scalar comparison a tie with the value just
     *    emitted, so the object would answer the same number forever;
     *  - a one-element list is not a list. Max routes it to the ``int`` /
     *    ``float`` method, so it compares against the comparand and leaves it
     *    alone, and so does this port.
     *
     *  A message with no numbers in it at all (``wobble``, ``clear``) is
     *  ignored rather than guessed at, as in ``.slide``, ``.mean`` and
     *  ``.accum``. Note the corollary those objects also carry: a word
     *  *followed* by numbers (``set 3``) is a list of one number by the time it
     *  gets here, because the shared reader steps over tokens it cannot parse.
     *  Neither Max object defines any message word, so nothing is shadowed by
     *  this; it just means ``set 3`` behaves as the number 3.
     *
     *  ### A bang before any input
     *
     *  Max says only "Sends the most recent output out the outlet again", which
     *  leaves open what happens when there has not been one yet. The answer
     *  here is the ``initial`` creation argument — 0 for a bare ``.maximum`` —
     *  and not silence, following ``.accum``: in a headless patcher an object
     *  that answers nothing is invisible rather than merely empty, and a patch
     *  that bangs its objects once on load is the normal way to prime one.
     *
     *  That stand-in is fixed at parse time and the cold inlet does **not**
     *  move it. A bang is documented as a *replay*, so it must not report a
     *  number the object has never sent; a comparand set silently on inlet 1
     *  has not been sent. Once anything has come out of the outlet the stand-in
     *  is irrelevant — the bang replays that instead.
     *
     *  ### One numeric type
     *
     *  Everything is a float and the outlet is a float outlet. Max's objects
     *  are int by default and turn float only when the creation argument
     *  carries a decimal point; that duality is deliberately not reproduced,
     *  for the reason ``.accum`` gives at length — ``.maximum 5`` and
     *  ``.maximum 5.0`` are the same parameter string in this patcher, so the
     *  mode would have to be guessed from the text of the argument. Guessing
     *  int here would silently round a comparison that a patch is using to
     *  clamp a gain. A patch that wants integers puts a ``.round`` on the
     *  outlet, where the rounding is visible.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlets, not
     *  by the DSP tick — and the emitting step lives in Emit(), called from the
     *  three inlet-0 handlers. This is the rule ``.slide``, ``.mean`` and
     *  ``.accum`` establish: a hot inlet fires CalculateIfReady() after *every*
     *  message type it accepts, so a Calculate() that emitted would send a
     *  second, stale value after every list — and would have to guess what to
     *  send after a bang.
     *
     *  Nothing on any path allocates, locks or blocks. A scalar comparison is
     *  one finiteness branch and one indirect call; a bang is one Send; a list
     *  is a bounded walk over at most ``MAX_LIST_ITEMS`` numbers read through
     *  the shared, allocation-free and locale-free ``ExprParseFloatList`` into
     *  a stack array. The outlet carries a float, so there is no
     *  ``std::string`` on the message path at all.
     */
    class gExtremumBase : public pObject {
    public:
      explicit gExtremumBase(ExtremumOrder order);

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
       *  Max's own documented ceiling for these two objects ("accepts lists of
       *  up to 256 elements"), and the same number ``.mean`` and ``.vexpr``
       *  use. A list arrives as text, so reading it needs a fixed destination
       *  if it is not to allocate; a longer list is truncated to its first
       *  ``MAX_LIST_ITEMS`` numbers.
       */
      static constexpr int MAX_LIST_ITEMS = 256;

      /** @brief The stored value inlet-0 numbers are compared against. */
      float Comparand() const {
        return comparand;
      }

      /** @brief What the outlet last carried, and what a bang re-sends. */
      float LastOutput() const {
        return lastOutput;
      }

      /** @brief The creation argument: the first comparand, and the value a
       *         bang reports before anything has been emitted. */
      float Initial() const {
        return initial;
      }

    protected:
      // Fills in the pieces of documentation that differ per direction; the
      // base constructor already set the category and built the ports. RT-cold
      // — constructor use only.
      void Document(const char* summary, const char* inletDoc, const char* comparandDoc,
                    const char* outletDoc, const char* paramDoc);

    private:
      // Send @p value and remember it as what a bang replays. The only place
      // the outlet is written, so "what came out" and "what a bang re-sends"
      // cannot drift apart.
      void Emit(float value, YSE::THREAD thread);

      // Which direction wins. Never null — the derived constructors are the
      // only callers and both pass a function.
      ExtremumOrder beats;

      // The creation argument. A plain float parameter, applied to the two
      // members below by ParseParams() so a saved `.maximum 5` compares
      // against 5 and answers 5 to its first bang.
      float initial;

      // The sticky right operand. Moved by inlet 1 and by a list of two or
      // more numbers; never by a scalar on inlet 0.
      float comparand;

      // The most recent output, which is what a bang re-sends. Seeded from
      // `initial` rather than left at 0 so the first bang has an answer.
      float lastOutput;
    };

    EXTREMUM_CLASS(gMaximum, YSE::OBJ::G_MAXIMUM)
    EXTREMUM_CLASS(gMinimum, YSE::OBJ::G_MINIMUM)

  } // namespace PATCHER
} // namespace YSE
