#pragma once
#include "../math/gRandomSource.h"
#include "../pAtomList.h"
#include "../pObject.h"
#include "gArray.h"
#include "gArrayEnds.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Shared body for the four permutation objects of the ``array.*``
     *         family — ``.array.reverse``, ``.array.rotate``,
     *         ``.array.scramble`` and ``.array.shuffle`` (issue #788).
     *
     *  The rearrangers: each is **an index order plus one shared "apply this
     *  order to the store" helper**, exactly as ``.zl``'s reordering modes
     *  each reduce to ``AtomList::AssignOrder`` — reverse's order counts
     *  down, rotate's wraps, scramble's and shuffle's is drawn. The helper is
     *  ``ApplyOrderLocked`` below, ``gArrayIndexMap::ReorderLocked``'s
     *  scratch-table reorder with the order computed in-object instead of
     *  arriving on a cord, and it lives here so the family does not write the
     *  copy-out/copy-back a fourth time.
     *
     *  Everything about the binding is ``gArrayEndsBase``'s, inherited whole:
     *  the array is bound from the first creation argument on the control
     *  thread, an ``array <name>`` message is honoured only when it names the
     *  array already bound, an unnamed object permutes a private, empty array
     *  of its own, and refusals are counted, never logged.
     *
     *  ### One guard hold, through the scratch table — #787's decisions kept
     *
     *  #788 inherits #548's warning that writes renumber and asks what a
     *  write arriving mid-walk does. The answer is the one #782/#784/#785
     *  gave and #787 wrote down for the reorder in particular: **there is no
     *  walk to be in the middle of**. The order is computed from the array as
     *  it stood at the trigger, *under the same hold of the store's guard
     *  that applies it* — which is also why a permutation, unlike an arriving
     *  map, can never miss: every entry indexes the length the same hold just
     *  read, so the result is exactly as long as the array. A writer on
     *  another thread loses the try-lock while the permutation holds it
     *  (dropped and counted, the store's rule), and the reference is sent
     *  after the guard is released, so a write it triggers changes what the
     *  *next* trigger sees, never the one in flight.
     *
     *  The apply goes through a scratch ``arrayStore`` the object owns rather
     *  than a second pass over the same table — #787's second decision,
     *  inherited with its reason: picking straight into the store would read
     *  elements a previous pick already overwrote, and the in-place
     *  cycle-walk that avoids it buys nothing once the scratch exists. The
     *  scratch and the order table are touched only under the bound store's
     *  guard, which is what serialises them.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — every object here is driven by its
     *  inlets, the family's rule. No message path allocates, locks or blocks:
     *  the name is resolved on the control thread, the order is bounded
     *  arithmetic into a fixed table, the apply is bounded assigns between
     *  tables reserved at construction, and the reference is built once per
     *  rebind, so a landed permutation is a send of a string the object
     *  already owns.
     */
    class gArrayPermuteBase : public gArrayEndsBase {
    public:
      /** @brief The message the outlet emits after a permutation that landed
       *         — ``"array <name>"``, or empty for an unnamed object. */
      const std::string& Reference() const {
        return reference;
      }

    protected:
      // Registers nothing of its own: the binding and its parameter hooks are
      // the base's, and each object declares its own inlets and outlets.
      gArrayPermuteBase() = default;

      // Rebuilds the reference after a re-parse. A subclass that derives more
      // from its parameters (the pair's seed) extends this rather than
      // shadowing it.
      void ParamsChanged() override;

      // Rebuild `reference` from the current name. Control thread only.
      void RefreshReference();

      // The shared helper: apply `order[0..count)` to the store, through the
      // scratch table. **The caller holds the store's guard.** Copies every
      // pick into the scratch in order, then copies the scratch back, and
      // clears the slots a shorter result vacates — gArrayIndexMap's
      // ReorderLocked, kept general (an entry past the end contributes
      // nothing) even though a permutation computed under the same hold
      // cannot produce one.
      void ApplyOrderLocked(std::size_t count);

      // The reference out the outlet, after the guard is released. An
      // unnamed object has no name to pass on — the announcement is simply
      // empty.
      void Announce(YSE::THREAD thread);

      // The index order one trigger applies, filled by the subclass under
      // the store's guard. gZl's `order`, sized to the store's own bound.
      std::uint16_t order[arrayStore::MAX_ELEMENTS] = {};

      // The scratch table the apply copies its picks into under the guard —
      // the object-owned scratch #787 chose over a second pass. Its
      // constructor reserves the whole table on the control thread; `busy`
      // and `count` go unused, the bound store's guard being what serialises
      // access to it.
      arrayStore scratch;

      // "array <name>", built once per rebind so a landed permutation is a
      // send of a string the object already owns rather than a concatenation
      // on whichever thread the message arrived on.
      std::string reference;
    };

    /**
     *  @brief Reverse an array's order — Max's ``array.reverse`` on the
     *         name-addressed value model ``.array`` settled (issue #788).
     *
     *  The simplest permutation: element ``i`` becomes element ``count-1-i``.
     *  Two inlets, one outlet — the remover twins' shape, and like them
     *  there is no int or float method anywhere: a reversal is asked for
     *  with a bang, never parameterised.
     *
     *  - **A bang on the trigger reverses the bound array in place.** An
     *    empty array reverses to itself and still announces — the ask was
     *    well-formed and the answer is what it says.
     *  - **``array <name>`` on the trigger reverses** when it names the
     *    array already bound — the message an ``.array``'s reference outlet
     *    emits on a bang, so wiring that outlet here gives the family's
     *    gesture: bang the array, out comes the reversed array's reference.
     *    Anything else there is refused and counted.
     *  - **``array <name>`` on the reference inlet is acknowledged
     *    silently** when it names the bound array — ``gDictSlice``'s shape —
     *    and anything else there is refused and counted.
     *  - **The outlet emits the bound array's reference after a reversal** —
     *    the way an array leaves an object on the value model, so the family
     *    chains: into ``.array.at`` it fetches from the new order. A lost
     *    guard emits nothing (counted), and an unnamed object stays silent —
     *    the reversal happens, but there is no name to pass on.
     */
    class gArrayReverse : public gArrayPermuteBase {
    public:
      gArrayReverse();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_REVERSE;
      }
      CREATE(gArrayReverse)

      void BangIn(int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    private:
      // The reversal: one hold of the store's guard around the counted-down
      // order and the shared apply, release, then announce.
      void Flip(YSE::THREAD thread);
    };

    /**
     *  @brief Rotate an array by a signed amount, wrapping — Max's
     *         ``array.rotate`` on the value model ``.array`` settled (issue
     *         #788).
     *
     *  **This is where the wrapping the base type refuses actually lives.**
     *  ``arrayStore`` decided once that an index is a position — never
     *  wrapped, never clamped, negatives refused — and named this object as
     *  the one that exists to provide the alternative. So the amount is a
     *  *signed distance*, not a position: any magnitude is legal (taken
     *  modulo the length, so a whole turn is the identity), positive rotates
     *  toward the end — the element pushed past the last position wraps to
     *  the front, Max's direction — and negative rotates toward the start.
     *  An amount of 0 rotates by nothing and still announces, Max's "a value
     *  of 0 will perform no rotation".
     *
     *  Three inlets, one outlet — ``.array.insert``'s arrangement, the Max
     *  idiom kept: configuration on the right, the ask on the left.
     *
     *  - **A bang on the trigger rotates by the stored amount** — the amount
     *    the last int on the amount inlet stored, seeded by the second
     *    creation argument (0 when absent, Max's optional argument).
     *  - **An int on the trigger rotates by that amount at the moment it
     *    arrives, and stores nothing** — ``gArrayIndexMap``'s trigger rule:
     *    a compound ask is answered when it arrives, and a bang that
     *    replayed the last inline amount would be hidden state. A float
     *    truncates to an int first, Max's float method; a list spelling
     *    exactly one signed int is the amount it spells, kept equivalent to
     *    the int so an amount-producing outlet still lands.
     *  - **An int on the amount inlet stores the amount, silently** — the
     *    cold half of the idiom, Max's right inlet. Negative is *legal*
     *    here, the whole point of the object; a float truncates first, and a
     *    non-finite one is refused rather than quietly becoming 0.
     *  - **``array <name>`` on the trigger rotates by the stored amount**
     *    when it names the array already bound — the family's gesture — and
     *    on the reference inlet it is acknowledged silently; anything else
     *    on either is refused and counted.
     *  - **The outlet emits the bound array's reference after a rotation** —
     *    a lost guard emits nothing (counted), and an unnamed object stays
     *    silent.
     */
    class gArrayRotate : public gArrayPermuteBase {
    public:
      gArrayRotate();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_ROTATE;
      }
      CREATE(gArrayRotate)

      /** @brief The stored amount the next bang rotates by — the last int
       *         received on the amount inlet, seeded by the second creation
       *         argument (0 when absent). */
      int Amount() const {
        return amount.load(std::memory_order_relaxed);
      }

      void BangIn(int inlet, YSE::THREAD thread);
      void IntIn(int value, int inlet, YSE::THREAD thread);
      void FloatIn(float value, int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      // Extends gArrayEndsBase's hook so a re-parse resets the stored amount
      // along with the name: SetParams("") must not keep rotating by
      // whatever the previous arguments planted. gArrayPositionBase's rule.
      void ClearParams() override;

    private:
      // The rotation: one hold of the store's guard around the wrapped order
      // and the shared apply, release, then announce. `by` is taken modulo
      // the length under the guard, so any magnitude is legal.
      void Rotate(int by, YSE::THREAD thread);

      // The stored amount. Atomic because a number may arrive on any thread
      // while the control thread re-parses the creation arguments; never a
      // lock. Signed on purpose — see the class notes.
      std::atomic<int> amount{0};
    };

    /**
     *  @brief Shared body for the randomising pair — ``.array.scramble`` and
     *         ``.array.shuffle`` (issue #788).
     *
     *  Max ships ``array.scramble`` and ``array.shuffle`` as one object under
     *  two names — their reference pages are word-for-word identical:
     *  "randomize the order of elements in an array object", the scrambled
     *  array out the left outlet and the scrambled index list out the right.
     *  The port keeps both spellings over one body, ``gArrayFindBase``'s
     *  arrangement for a pair that differs only in name — so a patch pasted
     *  from either habit works, and the two never drift apart.
     *
     *  The order is a Fisher-Yates shuffle from a per-object, seedable
     *  ``RandomSource`` — every permutation equally likely up to the residual
     *  bias ``RandomSource::Bounded`` documents, exactly one draw per element
     *  moved (a bounded count, never a rejection loop), which is what makes a
     *  seeded shuffle replayable. The seed is the second creation argument:
     *  non-zero replays the same shuffles every run, 0 takes an arbitrary
     *  stream — ``.urn``'s contract and Max's ``@seed 0`` — and an int on the
     *  seed inlet restarts the sequence silently, *without* rewriting the
     *  author's argument: a run-time reseed is the patch's business, the
     *  saved seed is the author's.
     *
     *  Three inlets, two outlets:
     *
     *  - **A bang on the trigger shuffles the bound array in place**; an
     *    empty array shuffles to itself and still announces the reference,
     *    though there is no order to publish.
     *  - **``array <name>`` on the trigger shuffles** when it names the
     *    array already bound — the family's gesture — and on the reference
     *    inlet it is acknowledged silently; anything else on either is
     *    refused and counted.
     *  - **An int on the seed inlet reseeds, silently.** A float truncates
     *    to an int first; a non-finite one is refused rather than quietly
     *    becoming seed 0.
     *  - **The reference outlet emits the bound array's reference after a
     *    shuffle that landed**, and **the order outlet publishes the applied
     *    order** — the picks as zero-based indices into the array as it
     *    stood, Max's "scrambled index" — *before* the reference leaves,
     *    Max's right-to-left rule and ``.zl sort``'s headline idiom: feed
     *    the order to an ``.array.indexmap``'s map inlet and the reference
     *    to its trigger, and a parallel array lands in the same new order.
     *    Zero-based because the family's positions are ``arrayStore``'s, and
     *    a one-element order leaves as the int it spells — which
     *    ``.array.indexmap``'s map inlet accepts as the one-entry map, the
     *    equivalence it keeps for exactly this producer.
     */
    class gArrayScrambleBase : public gArrayPermuteBase {
    public:
      /** @brief How many draws the shuffles have taken since the last
       *         seeding. One per element moved — what makes a seeded shuffle
       *         replayable, and the kind of thing a refactor breaks
       *         silently. */
      UInt Draws() const {
        return rng.Draws();
      }

      void BangIn(int inlet, YSE::THREAD thread);
      void IntIn(int value, int inlet, YSE::THREAD thread);
      void FloatIn(float value, int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      gArrayScrambleBase();

      // Extends the base's hook so a re-parse reseeds along with the name:
      // the seed is not known until the parameter string is read, gUrn's
      // arrangement.
      void ParamsChanged() override;
      void ClearParams() override;

    private:
      // The shuffle: one hold of the store's guard around the Fisher-Yates
      // order, the shared apply and the order list's build; release, then
      // the order out the right outlet and the reference out the left.
      void Shuffle(YSE::THREAD thread);

      // The seed — the second creation argument, control-thread state the
      // seed inlet never rewrites. 0 means an arbitrary stream.
      std::atomic<int> seed{0};

      // The random source. Per object and seedable, which is what makes a
      // shuffle reproducible — the whole reason RandomSource exists beside
      // the engine generator.
      RandomSource rng;

      // The applied order as a list, built under the guard and sent after it
      // is released — gArrayEndsRemover's `fetched` arrangement, for a whole
      // list. Both fixed at construction.
      AtomList orderOut;
      std::string orderRender;
    };

    /**
     *  @brief Reorder an array at random — Max's ``array.scramble`` on the
     *         value model ``.array`` settled (issue #788).
     *
     *  Read ``gArrayScrambleBase`` for the whole story — including why
     *  ``.array.shuffle`` is this same object under Max's other name for it.
     */
    class gArrayScramble : public gArrayScrambleBase {
    public:
      gArrayScramble();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_SCRAMBLE;
      }
      CREATE(gArrayScramble)
    };

    /**
     *  @brief Shuffle an array's elements — Max's ``array.shuffle`` on the
     *         value model ``.array`` settled (issue #788).
     *
     *  Read ``gArrayScrambleBase`` for the whole story — including why
     *  ``.array.scramble`` is this same object under Max's other name for it.
     */
    class gArrayShuffle : public gArrayScrambleBase {
    public:
      gArrayShuffle();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_SHUFFLE;
      }
      CREATE(gArrayShuffle)
    };

  } // namespace PATCHER
} // namespace YSE
