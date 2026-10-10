#pragma once
#include "../pObject.h"
#include "gArray.h"
#include "gArrayEnds.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Shared body for the six statistics of the ``array.*`` family —
     *         ``.array.min``, ``.array.max``, ``.array.mean``,
     *         ``.array.median``, ``.array.mode`` and ``.array.stddev``
     *         (issue #790).
     *
     *  The read-only reducers: each turns the array into one number — what an
     *  analytical patch does with a collected array, and the reason a patch
     *  collects one in the first place. Their closest kin are ``gArrayLength``
     *  and ``gArrayFindBase``, not the mutating ``gArrayPermuteBase``: nothing
     *  here ever writes to the store, so a statistic is **one read under one
     *  hold of the store's guard**, answered as a scalar after the guard is
     *  released.
     *
     *  Everything about the binding is ``gArrayEndsBase``'s, inherited whole:
     *  the array is bound from the creation argument on the control thread, an
     *  ``array <name>`` message is honoured only when it names the array
     *  already bound, an unnamed object reads a private, empty array of its
     *  own, and refusals are counted, never logged. What this base adds is the
     *  ask itself — ``gArrayLength``'s shape: a statistic is asked for with a
     *  bang, never addressed, so there is no int or float method anywhere.
     *
     *  ### The population, and the non-numeric decision #790 asks for
     *
     *  Five of the six are statistics **over the numeric elements** — the
     *  issue's own words — so a non-numeric element is **skipped**: it is not
     *  part of the population, exactly as a symbol is not part of what
     *  ``.zl sort`` means by a number. An element is numeric when
     *  ``ReadNumericToken`` — the classifier ``AtomList`` applies to every
     *  atom on the way in, and the one ``.array.sort`` classifies with — reads
     *  the whole of it as one finite number, so ``7`` and ``7.`` are the same
     *  number here exactly as they are to ``.zl``. Refusing the whole message
     *  over one symbol would make ``mean`` unusable on the mixed arrays the
     *  rest of the family happily holds. ``.array.mode`` is the deliberate
     *  exception: it reports the most frequent **element**, so every element
     *  counts and equality is the spelling — ``ArrayFind``'s byte compare, the
     *  reason ``7`` and ``7.`` are different elements to it.
     *
     *  ### An empty population bangs the empty outlet
     *
     *  The minimum of nothing does not exist, so it cannot travel in-band the
     *  way ``.array.length``'s 0 can. Every object here has a second outlet
     *  that bangs when the population is empty — an empty or unnamed (private)
     *  array, or one holding no numeric element at all for the five numeric
     *  statistics. ``gArrayEndsRemover``'s empty outlet, for the same reason:
     *  "no data" is a state a patch must be able to route on, not an error,
     *  and a sentinel value would be indistinguishable from a real answer. A
     *  lost try-lock is neither: the array's state is unknown, so it is a
     *  counted refusal and no outlet fires.
     *
     *  ### One guard hold — the concurrent-write answer, once more
     *
     *  #790 inherits #548's warning about renumbering writes and asks what a
     *  write arriving mid-walk does. The answer is the family's: **there is no
     *  walk to be in the middle of**. The whole reduction — collecting the
     *  numeric elements, sorting a scratch, counting runs — happens under one
     *  hold of the store's guard, so the statistic reported is the array as it
     *  stood at the trigger; a writer on another thread loses the try-lock
     *  while the reduction holds it (dropped and counted, the store's rule),
     *  and the answer is sent after the guard is released, so a write the
     *  answer triggers changes what the *next* ask sees, never the one in
     *  flight. ``median`` and ``mode`` sort **a scratch the object owns**,
     *  never the shared store — a statistic must not reorder the array under
     *  everything else reading it; ``.array.sort`` is the object that exists
     *  to do that.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — every object here is driven by its
     *  inlet, the family's rule. No message path allocates, locks or blocks:
     *  the name is resolved on the control thread, the collection tables and
     *  scratches are fixed members, and the answer is one int, float or copied
     *  element.
     */
    class gArrayStatsBase : public gArrayEndsBase {
    public:
      void BangIn(int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      gArrayStatsBase();

      // The reduction itself, run under the store's guard: compute the answer
      // into the subclass's own state. False when the population is empty —
      // the base then bangs the empty outlet instead of reporting.
      virtual bool ReduceLocked() = 0;

      // Send the computed answer. Called after the store's guard is released,
      // so a send may run the downstream graph freely.
      virtual void Report(YSE::THREAD thread) = 0;

    private:
      // What a bang and the reference gesture both come down to: one hold of
      // the store's guard around ReduceLocked, release, then Report — or the
      // empty outlet when there was no population to reduce.
      void Ask(YSE::THREAD thread);
    };

    /**
     *  @brief The five numeric statistics' shared collector — the "read the
     *         numeric elements out" helper #790 says the objects share.
     *
     *  One bounded scan of the store, classifying each element with
     *  ``ReadNumericToken`` and keeping, in arrival order, every value that
     *  reads as one finite number — plus the position it came from, which is
     *  how ``min`` and ``max`` answer with the *element* rather than with a
     *  re-spelling of its value. Fixed tables, sized to the store's own
     *  bound: the price of never allocating on a message path.
     */
    class gArrayNumericStatsBase : public gArrayStatsBase {
    protected:
      gArrayNumericStatsBase() = default;

      // Fill values/sourceIndex with the numeric elements, in order. The
      // caller holds the store's guard.
      void CollectNumbers();

      // The numeric population: the value each element reads as, and the
      // position in the store it came from. Only the first numericCount
      // entries are live per ask.
      float values[arrayStore::MAX_ELEMENTS] = {};
      std::uint16_t sourceIndex[arrayStore::MAX_ELEMENTS] = {};
      std::size_t numericCount = 0;
    };

    /**
     *  @brief The comparing pair's shared body — ``.array.min`` and
     *         ``.array.max`` are one scan with the comparison flipped
     *         (issue #790).
     *
     *  Both answer with the winning **element**, typed the way the patcher
     *  spells it (``SendAtom`` — ``7.5`` leaves as the float it is, ``10`` as
     *  an int), because the issue asks for the smallest and largest *element*
     *  and its spelling is part of what it is. Values compare numerically, so
     *  ``7`` and ``7.`` tie; a tie keeps the **first occurrence** — the strict
     *  comparison never replaces an equal earlier winner — which is what makes
     *  the answer deterministic under ``getvalue``'s eyes.
     */
    class gArrayExtremumBase : public gArrayNumericStatsBase {
    protected:
      // `wantMax` is the whole difference between .array.max and .array.min.
      explicit gArrayExtremumBase(bool wantMax);

      bool ReduceLocked() override;
      void Report(YSE::THREAD thread) override;

    private:
      const bool wantMax;

      // Where the winning element is copied while the guard is held, so the
      // send can happen after it is released. A plain array rather than a
      // string because it is written from inside the critical section —
      // gArray's `fetched`, for gArray's reason.
      char fetched[arrayStore::ELEMENT_CAPACITY + 1] = {};
      std::size_t fetchedLength = 0;

      // Render buffer for the element outlet, reserved to
      // AtomList::RENDER_CAPACITY at construction.
      std::string emitScratch;
    };

    /**
     *  @brief Output the smallest numeric element of an array — Max's
     *         ``array.min`` on the name-addressed value model ``.array``
     *         settled (issue #790).
     *
     *  Read ``gArrayExtremumBase`` for the comparison and ``gArrayStatsBase``
     *  for the population, the empty outlet and the guard.
     */
    class gArrayMin : public gArrayExtremumBase {
    public:
      gArrayMin();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_MIN;
      }
      CREATE(gArrayMin)
    };

    /**
     *  @brief Output the largest numeric element of an array — Max's
     *         ``array.max`` on the name-addressed value model ``.array``
     *         settled (issue #790).
     *
     *  Read ``gArrayExtremumBase`` for the comparison and ``gArrayStatsBase``
     *  for the population, the empty outlet and the guard.
     */
    class gArrayMax : public gArrayExtremumBase {
    public:
      gArrayMax();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_MAX;
      }
      CREATE(gArrayMax)
    };

    /**
     *  @brief Output the arithmetic mean of an array's numeric elements —
     *         Max's ``array.mean`` on the value model ``.array`` settled
     *         (issue #790).
     *
     *  Always a float: a mean is a statistic *of* the values, not one of
     *  them, and the mean of ints is routinely fractional. Accumulated in
     *  double so 256 floats do not lose their tail to the sum's magnitude.
     *  Read ``gArrayStatsBase`` for the population, the empty outlet and the
     *  guard.
     */
    class gArrayMean : public gArrayNumericStatsBase {
    public:
      gArrayMean();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_MEAN;
      }
      CREATE(gArrayMean)

    protected:
      bool ReduceLocked() override;
      void Report(YSE::THREAD thread) override;

    private:
      float result = 0.f;
    };

    /**
     *  @brief Output the median of an array's numeric elements — Max's
     *         ``array.median`` on the value model ``.array`` settled
     *         (issue #790).
     *
     *  The middle value of the numeric population in sorted order; an even
     *  population answers the mean of the two middle values, which is why the
     *  answer is always a float, ``mean``'s reporting. The sort is a bounded
     *  bottom-up merge over **a scratch the object owns** — ``gZl``'s
     *  SortIndices shape, O(n log n) whatever the data, on a path the audio
     *  callback takes — never a reorder of the shared store: a statistic must
     *  not move the array under everything else reading it. Read
     *  ``gArrayStatsBase`` for the population, the empty outlet and the
     *  guard.
     */
    class gArrayMedian : public gArrayNumericStatsBase {
    public:
      gArrayMedian();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_MEDIAN;
      }
      CREATE(gArrayMedian)

    protected:
      bool ReduceLocked() override;
      void Report(YSE::THREAD thread) override;

    private:
      // Sort values[0..count) ascending, in place, through the fixed merge
      // scratch. The caller holds the store's guard.
      void SortValues(std::size_t count);

      // The merge sort's scratch half — gZl's `merge`, over values instead
      // of indices because a median needs no source position.
      float merge[arrayStore::MAX_ELEMENTS] = {};

      float result = 0.f;
    };

    /**
     *  @brief Output the most frequent element of an array — Max's
     *         ``array.mode`` on the value model ``.array`` settled
     *         (issue #790).
     *
     *  The one statistic over **every** element, numeric or not, because a
     *  mode is about identity rather than magnitude — and identity here is
     *  the spelling, ``ArrayFind``'s byte compare, so ``7`` and ``7.`` are
     *  different elements to it exactly as ``getvalue`` spells them
     *  differently. The count is bought the way the family buys order —
     *  a stable bounded merge over an index scratch the object owns, so equal
     *  spellings sit adjacent and a run's length is its frequency —
     *  never a reorder of the shared store. A tie keeps the element whose
     *  **first occurrence is earliest**, which the stable order makes
     *  deterministic. The answer is the element itself, typed the way the
     *  patcher spells it. Read ``gArrayStatsBase`` for the empty outlet and
     *  the guard.
     */
    class gArrayMode : public gArrayStatsBase {
    public:
      gArrayMode();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_MODE;
      }
      CREATE(gArrayMode)

    protected:
      bool ReduceLocked() override;
      void Report(YSE::THREAD thread) override;

    private:
      // Strictly "element `i` spells before element `j`" — byte order, the
      // shorter first when one is a prefix of the other. Read under the
      // guard, like everything it reads.
      bool SpellingBefore(std::size_t i, std::size_t j) const;

      // Fill `order[0..count)` with the stable spelling order — gZl's
      // SortIndices through `merge`, gArraySort's arrangement. The caller
      // holds the store's guard.
      void SortOrder(std::size_t count);

      // The index scratch the run count walks — the object's own, never the
      // store's order.
      std::uint16_t order[arrayStore::MAX_ELEMENTS] = {};
      std::uint16_t merge[arrayStore::MAX_ELEMENTS] = {};

      // Where the winning element is copied while the guard is held —
      // gArray's `fetched`, for gArray's reason.
      char fetched[arrayStore::ELEMENT_CAPACITY + 1] = {};
      std::size_t fetchedLength = 0;

      // Render buffer for the element outlet, reserved at construction.
      std::string emitScratch;
    };

    /**
     *  @brief Output the standard deviation of an array's numeric elements —
     *         Max's ``array.stddev`` on the value model ``.array`` settled
     *         (issue #790).
     *
     *  The **population** standard deviation — divided by N, not N-1 —
     *  because the array is the whole population, not a sample of one: what
     *  the patch collected is what the statistic describes. A one-element
     *  population answers 0. Always a float, accumulated in double through
     *  the two-pass form (mean first, then squared deviations), which keeps
     *  the catastrophic cancellation of the one-pass sum-of-squares out of a
     *  256-element window. Read ``gArrayStatsBase`` for the population, the
     *  empty outlet and the guard.
     */
    class gArrayStdDev : public gArrayNumericStatsBase {
    public:
      gArrayStdDev();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_STDDEV;
      }
      CREATE(gArrayStdDev)

    protected:
      bool ReduceLocked() override;
      void Report(YSE::THREAD thread) override;

    private:
      float result = 0.f;
    };

  } // namespace PATCHER
} // namespace YSE
