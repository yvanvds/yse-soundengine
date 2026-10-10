#pragma once
#include "../pAtomList.h"
#include "../pObject.h"
#include "gArray.h"
#include "gArrayEnds.h"
#include <atomic>
#include <climits>
#include <cstddef>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Shared body for the range readers of the ``array.*`` family —
     *         ``.array.slice``, ``.array.subarray`` and ``.array.sub``
     *         (issue #791).
     *
     *  The cutting objects: each outputs a *piece* of the bound array — the
     *  windowing an analytical patch does over a collected sequence. They are
     *  read-only, ``gArrayStatsBase``'s kin rather than the mutating bases:
     *  nothing here ever writes to the store, so an ask is **one read under
     *  one hold of the store's guard**, collected into a list the object owns
     *  and sent after the guard is released.
     *
     *  Everything about the binding is ``gArrayEndsBase``'s, inherited whole:
     *  the array is bound from the first creation argument on the control
     *  thread, an ``array <name>`` message is honoured only when it names the
     *  array already bound, an unnamed object reads a private, empty array of
     *  its own, and refusals are counted, never logged.
     *
     *  ### A piece leaves as list text
     *
     *  The decision #791 asks for, made once for all four objects in the
     *  file: a piece leaves as the list it spells (``SendAtoms`` — one
     *  element as the atom it is, several as list text), **never as a new
     *  named array**. Creating an array from a message would mean resolving a
     *  name on a message path — the registry's mutex on whatever thread the
     *  message arrived on — which is exactly what the family's binding rule
     *  exists to keep off cords. An array and the list text it spells are the
     *  same thing seen twice (the store's one-atom-per-element rule), so the
     *  piece can be pushed into another ``.array`` losslessly when a patch
     *  wants it stored.
     *
     *  ### The bounds: a position inside the array, a bound past it
     *
     *  ``start`` and ``end`` are zero-based, and negative is refused —
     *  ``arrayStore``'s indexing rule, decided once for the family. Max's
     *  negative from-the-end indexing is deliberately not ported: it is the
     *  wrap the base type refuses, and ``.array.length`` plus arithmetic
     *  spells it explicitly where a patch wants it.
     *
     *  Past the end the two numbers are *bounds of a range*, not positions
     *  addressing elements, so the piece is the **intersection** of the asked
     *  range with the live elements — ``.zl slice``'s "the final list may be
     *  shorter than specified", the clamp ``SendAtomRange`` already applies
     *  to every slice the patcher spells. An ask whose intersection is empty
     *  — an empty array, a start past the last element, a crossed exclusive
     *  range — bangs the **empty outlet**: "no data" is a state a patch must
     *  be able to route on, not an error, and an out-of-range *fetch* is a
     *  miss on an outlet, the family's fetch rule. A lost try-lock is
     *  neither: the array's state is unknown, so it is a counted refusal and
     *  no outlet fires.
     *
     *  ### Where the three differ, which is Max's difference verbatim
     *
     *  - ``.array.slice`` is JavaScript's ``Array.slice()``: the piece is
     *    ``[start, end)``, end **exclusive**; an end of 0 extends to the end
     *    of the array (Max's documented extra over the JS form); a crossed
     *    range selects nothing — "reverse slices are not permitted".
     *  - ``.array.subarray`` is the inclusive form: ``[start, end]``, and a
     *    crossed range is legal — "reverse slices are permitted" — so an end
     *    before the start emits the piece **reversed**, walking from start
     *    toward end.
     *  - ``.array.sub`` is ``.array.subarray`` under a second name: Max's
     *    reference serves ``array.subarray``'s page for ``array.sub``
     *    verbatim, so the port keeps both spellings over one implementation —
     *    ``.array.scramble`` / ``.array.shuffle``'s arrangement (#788).
     *
     *  ### One guard hold — the mid-walk answer, once more
     *
     *  #791 inherits #548's warning that ``insert`` and ``delete`` renumber
     *  and asks what a write arriving mid-walk does. The answer is the
     *  family's: **there is no walk to be in the middle of**. The whole piece
     *  is collected under one hold of the store's guard, so what leaves is
     *  the array as it stood at the trigger; a writer on another thread loses
     *  the try-lock while the collection holds it (dropped and counted, the
     *  store's rule), and the send happens after the guard is released, so a
     *  write the piece triggers changes what the *next* ask sees, never the
     *  one in flight.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — every object here is driven by its
     *  inlet, the family's rule. No message path allocates, locks or blocks:
     *  the name is resolved on the control thread, the piece is collected
     *  into an ``AtomList`` member reserved at construction and rendered into
     *  a scratch reserved alongside it, and the stored bounds are atomics. A
     *  piece whose elements together outrun what a cord carries is refused
     *  whole and counted — ``.array.at``'s whole-reply rule; a partial piece
     *  would be truncation by another name.
     */
    class gArraySliceBase : public gArrayEndsBase {
    public:
      /**
       *  @brief The stored end bound that means "to the end of the array" —
       *         the default when the third creation argument is absent.
       *
       *  Not a sentinel: any bound at or past the last element already means
       *  the end through the intersection rule, so the open default is simply
       *  the largest one. At run time any int past the array's length re-opens
       *  the range the same way (and, on ``.array.slice`` only, so does 0 —
       *  Max's documented spelling).
       */
      static constexpr int OPEN_END = INT_MAX;

      /** @brief The stored start of the range — the last int received on the
       *         start inlet, seeded by the second creation argument (0 when
       *         absent). */
      int RangeStart() const {
        return rangeStart.load(std::memory_order_relaxed);
      }

      /** @brief The stored end of the range — the last int received on the
       *         end inlet, seeded by the third creation argument
       *         (``OPEN_END`` when absent). */
      int RangeEnd() const {
        return rangeEnd.load(std::memory_order_relaxed);
      }

      void BangIn(int inlet, YSE::THREAD thread);
      void IntIn(int value, int inlet, YSE::THREAD thread);
      void FloatIn(float value, int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      // `inclusive` is the whole difference between .array.subarray (and its
      // second spelling .array.sub) and .array.slice: an inclusive end that
      // permits the reversed piece, against JS's exclusive end that does not.
      explicit gArraySliceBase(bool inclusive);

      // Extends gArrayEndsBase's hook so a re-parse resets the stored bounds
      // along with the name: SetParams("") must not keep cutting wherever the
      // previous arguments pointed. gArrayPositionBase's rule.
      void ClearParams() override;

    private:
      // What a bang and the reference gesture both come down to: load the
      // bounds, collect the piece under one hold of the store's guard,
      // release, then send — the piece out the piece outlet, or a bang out
      // the empty outlet when the intersection selected nothing.
      void Ask(YSE::THREAD thread);

      // The collection itself. The caller holds the store's guard. False when
      // the piece outran what a cord carries — the caller then refuses whole.
      bool CollectLocked(std::size_t start, std::size_t end);

      const bool inclusiveEnd;

      // The stored bounds. Atomic because a number may arrive on any thread
      // while the control thread re-parses the creation arguments; never a
      // lock.
      std::atomic<int> rangeStart{0};
      std::atomic<int> rangeEnd{OPEN_END};

      // The piece, collected under the guard and sent after it is released.
      // An AtomList rather than a string because it carries the patcher's own
      // bound on how much list text may travel down a cord — a piece that
      // outruns it is refused whole rather than the send allocating.
      AtomList piece;

      // Render buffer for the piece outlet, reserved to
      // AtomList::RENDER_CAPACITY at construction.
      std::string emitScratch;
    };

    /**
     *  @brief Output a run of elements of an array — Max's ``array.slice`` on
     *         the name-addressed value model ``.array`` settled (issue #791).
     *
     *  JavaScript's ``Array.slice()``: the piece is ``[start, end)``, end
     *  exclusive, and a crossed range selects nothing. An end of 0 — or any
     *  bound past the array — extends to the end. Read ``gArraySliceBase``
     *  for the bounds, the empty outlet and the guard.
     */
    class gArraySlice : public gArraySliceBase {
    public:
      gArraySlice();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_SLICE;
      }
      CREATE(gArraySlice)
    };

    /**
     *  @brief Output a sub-range of an array, both ends inclusive — Max's
     *         ``array.subarray`` on the value model ``.array`` settled
     *         (issue #791).
     *
     *  The non-JS form: ``[start, end]`` inclusive, and the reversed piece is
     *  permitted — an end before the start walks from start toward end. Read
     *  ``gArraySliceBase`` for the bounds, the empty outlet and the guard.
     */
    class gArraySubarray : public gArraySliceBase {
    public:
      gArraySubarray();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_SUBARRAY;
      }
      CREATE(gArraySubarray)
    };

    /**
     *  @brief ``.array.subarray`` under its second Max name — ``array.sub``'s
     *         reference page is ``array.subarray``'s verbatim, so the port
     *         keeps both spellings over one implementation (issue #791),
     *         ``.array.scramble`` / ``.array.shuffle``'s arrangement.
     */
    class gArraySub : public gArraySliceBase {
    public:
      gArraySub();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_SUB;
      }
      CREATE(gArraySub)
    };

    /**
     *  @brief Split an array in two at a position — Max's ``array.split`` on
     *         the name-addressed value model ``.array`` settled (issue #791).
     *
     *  The head/tail split every recursive patch needs: elements before the
     *  split position leave the head outlet, elements at and after it leave
     *  the tail outlet — the element *at* the position goes to the tail,
     *  ``.zl slice``'s rule, so the position reads as "how many elements the
     *  head takes". Both pieces leave as list text (see ``gArraySliceBase``
     *  on why never as a new named array), collected under **one** hold of
     *  the store's guard so the two halves are two views of one moment, and
     *  sent tail before head — right-to-left, the patcher's outlet order.
     *
     *  The position is a **boundary**, not an element position: 0 puts
     *  everything in the tail, the array's length puts everything in the
     *  head, and past the length is still the same boundary — the
     *  intersection rule ``gArraySliceBase`` states. An empty half says
     *  nothing at all (``SendAtoms``' rule: an object with nothing to say
     *  says nothing rather than sending an empty message), which is what
     *  lets a recursive head/tail patch terminate by absence. A negative
     *  position is refused and counted — the family's indexing rule.
     *
     *  Three inlets, two outlets:
     *
     *  - **A bang on the trigger splits at the stored position** — the
     *    position the last int stored, seeded by the second creation
     *    argument.
     *  - **``array <name>`` on the trigger splits** when it names the array
     *    already bound — the family's gesture — and on the reference inlet
     *    it is acknowledged silently; anything else on either is refused.
     *  - **An int on the position inlet stores the position, silently** —
     *    the cold half of the Max idiom; a float truncates to an int first,
     *    and a negative or non-finite value is refused without moving the
     *    stored position.
     *
     *  Read-only, ``gArraySliceBase``'s real-time story whole: one guard
     *  hold, fixed storage, refusals counted, and a split whose half outruns
     *  what a cord carries is refused whole — no partial piece.
     */
    class gArraySplit : public gArrayEndsBase {
    public:
      gArraySplit();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_SPLIT;
      }
      CREATE(gArraySplit)

      /** @brief The stored position the next split cuts at — the last int
       *         received on the position inlet, seeded by the second
       *         creation argument (0 when absent). */
      int Position() const {
        return position.load(std::memory_order_relaxed);
      }

      void BangIn(int inlet, YSE::THREAD thread);
      void IntIn(int value, int inlet, YSE::THREAD thread);
      void FloatIn(float value, int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      // Extends gArrayEndsBase's hook so a re-parse resets the stored
      // position along with the name. gArrayPositionBase's rule.
      void ClearParams() override;

    private:
      // The split itself: collect both halves under one hold of the store's
      // guard, release, then send tail before head. An empty half sends
      // nothing; a half that outruns a cord refuses the whole split.
      void Split(YSE::THREAD thread);

      // The stored boundary. Atomic because a number may arrive on any
      // thread while the control thread re-parses the creation arguments;
      // never a lock.
      std::atomic<int> position{0};

      // The two halves, collected under the guard and sent after it is
      // released — AtomLists for the bound they carry, gArraySliceBase's
      // reason.
      AtomList head;
      AtomList tail;

      // Render buffer for both outlets, reserved to
      // AtomList::RENDER_CAPACITY at construction. One buffer serves both
      // sends because they are sequential.
      std::string emitScratch;
    };

  } // namespace PATCHER
} // namespace YSE
