#pragma once
#include "../math/gRandomSource.h"
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
     *  @brief Output a random element of an array — Max's ``array.random`` on
     *         the name-addressed value model ``.array`` settled (issue #805).
     *
     *  The picker: picking from a stored collection is the generative patch's
     *  most common single operation, and the one ``.array`` plus ``.random``
     *  cannot do together without knowing the length first. Its closest kin
     *  are the read-only reducers (``gArrayStatsBase``), not the mutating
     *  ``gArrayPermuteBase``: nothing here ever writes to the store, so a pick
     *  is **one guarded read at a random position**, answered after the guard
     *  is released.
     *
     *  Everything about the binding is ``gArrayEndsBase``'s, inherited whole:
     *  the array is bound from the first creation argument on the control
     *  thread, an ``array <name>`` message is honoured only when it names the
     *  array already bound, an unnamed object reads a private, empty array of
     *  its own, and refusals are counted, never logged.
     *
     *  ### The randomness — the family's source, one draw per element picked
     *
     *  The position is drawn from a per-object, seedable ``RandomSource`` —
     *  the source ``.drunk``, ``.urn`` and ``.array.scramble`` share, kept
     *  per object so a seeded patch is replayable (see gRandomSource.h for
     *  why the engine generator's thread-local stream cannot be). The seed is
     *  the second creation argument: non-zero replays the same picks every
     *  run, 0 takes an arbitrary stream — ``.urn``'s contract and Max's
     *  ``@seed 0`` — and an int on the seed inlet restarts the sequence
     *  silently, *without* rewriting the author's argument: a run-time reseed
     *  is the patch's business, the saved seed the author's.
     *
     *  **Exactly one draw per element that leaves** — never for an empty
     *  array, a lost try-lock or a refused message — so a seeded stream stays
     *  aligned with the elements actually picked, which is what makes it
     *  replayable, and the kind of thing a refactor breaks silently
     *  (``Draws()`` exists so tests can pin it). **Repeats are allowed**:
     *  every pick is an independent uniform draw over the positions the array
     *  holds at that moment (up to the residual bias ``RandomSource::Bounded``
     *  documents). Drawing without replacement is ``.urn``'s behaviour, and
     *  per #805 a second object rather than a mode of this one.
     *
     *  ### What arrives, and what leaves
     *
     *  Three inlets, two outlets — trigger, seed and reference,
     *  ``.array.scramble``'s arrangement:
     *
     *  - **A bang on the trigger picks**: one element leaves the element
     *    outlet, typed the way the patcher spells it (``SendAtom`` — an int,
     *    a float or a symbol by its spelling). A pick is asked for with a
     *    bang, never addressed — ``.array.at`` is the object that takes a
     *    position — so there is no int or float method on the trigger.
     *  - **``array <name>`` on the trigger picks** when it names the array
     *    already bound — the message an ``.array``'s reference outlet emits
     *    on a bang, so wiring that outlet here gives the family's gesture:
     *    bang the array, out comes a random element. Honoured only via
     *    ``ArrayReferenceNames``' bounded compare and refused otherwise,
     *    because resolving an unrecognised name means the registry's mutex on
     *    whatever thread the message arrived on.
     *  - **An int on the seed inlet reseeds, silently.** A float truncates to
     *    an int first — Max's float method — and a non-finite one is refused
     *    rather than quietly becoming seed 0.
     *  - **``array <name>`` on the reference inlet is acknowledged
     *    silently** when it names the bound array — ``gDictSlice``'s shape —
     *    and anything else there is refused and counted.
     *  - **An empty array bangs the empty outlet instead** — an empty or
     *    unnamed (private) array has no element to pick, and "no data" is a
     *    state a patch must be able to route on, not an error; a sentinel
     *    value would be indistinguishable from a real answer —
     *    ``gArrayStatsBase``'s empty rule. A lost try-lock is neither: the
     *    array's state is unknown, so it is a counted refusal, no outlet
     *    fires and no draw is taken.
     *
     *  ### A pick is atomic — the concurrent-write decision #805 asks for
     *
     *  ``insert`` and ``delete`` renumber, and #805 asks what a write
     *  arriving mid-walk does. The answer is the family's: **there is no walk
     *  to be in the middle of**. The length is read, the position drawn and
     *  the element copied out under **one** hold of the store's guard, so the
     *  draw ranges over exactly the length that same hold read and a pick can
     *  never miss. A writer on another thread loses the try-lock while the
     *  pick holds it (dropped and counted by the writer, the store's rule); a
     *  write from the pick's own downstream subgraph happens after the guard
     *  is released and changes what the *next* draw ranges over, never the
     *  element in flight. Nothing of a pick lives in the shared store — the
     *  sequence is this object's own, ``.coll``'s per-object pointer rule —
     *  so two ``.array.random`` on one name pick independently, each from its
     *  own seedable stream.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlet, the
     *  family's rule. No message path allocates, locks or blocks: the name is
     *  resolved on the control thread, a draw is ``RandomSource``'s bounded
     *  arithmetic, the element is copied into a fixed buffer under the guard
     *  and sent after it through a render scratch reserved at construction.
     *  Refusals are counted (``Dropped()``), never logged.
     */
    class gArrayRandom : public gArrayEndsBase {
    public:
      gArrayRandom();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_RANDOM;
      }
      CREATE(gArrayRandom)

      /** @brief How many draws the picks have taken since the last seeding.
       *         One per element that left — never for an empty array, a lost
       *         try-lock or a refusal — which is what keeps a seeded stream
       *         aligned with the elements actually picked, and the kind of
       *         thing a refactor breaks silently. */
      UInt Draws() const {
        return rng.Draws();
      }

      void BangIn(int inlet, YSE::THREAD thread);
      void IntIn(int value, int inlet, YSE::THREAD thread);
      void FloatIn(float value, int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      // Extends the base's hooks so a re-parse reseeds along with the name:
      // the seed is not known until the parameter string is read —
      // gArrayScrambleBase's arrangement, and gArrayPositionBase's rule that
      // SetParams("") must not keep picking from whatever stream the previous
      // arguments planted.
      void ParamsChanged() override;
      void ClearParams() override;

    private:
      // The pick itself: read the length, draw the position and copy the
      // element out under one hold of the store's guard, release, then send —
      // the element out the element outlet, or a bang out the empty outlet
      // when there was nothing to pick. See the class notes on why this is
      // atomic and when a draw is taken.
      void Pick(YSE::THREAD thread);

      // The seed — the second creation argument, control-thread state the
      // seed inlet never rewrites. 0 means an arbitrary stream.
      std::atomic<int> seed{0};

      // The random source. Per object and seedable, which is what makes a
      // pick sequence reproducible — the whole reason RandomSource exists
      // beside the engine generator.
      RandomSource rng;

      // Where the picked element is copied while the guard is held, so the
      // send can happen after it is released. A plain array rather than a
      // string because it is written from inside the critical section —
      // gArray's `fetched`, for gArray's reason.
      char fetched[arrayStore::ELEMENT_CAPACITY + 1] = {};
      std::size_t fetchedLength = 0;

      // Render buffer for the element outlet, reserved to
      // AtomList::RENDER_CAPACITY at construction.
      std::string emitScratch;
    };

  } // namespace PATCHER
} // namespace YSE
