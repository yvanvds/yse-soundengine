#pragma once
#include "../pAtomList.h"
#include "../pObject.h"
#include "gArray.h"
#include "gArrayEnds.h"
#include <atomic>
#include <cstddef>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Fill an array with a repeated value — Max's ``array.fill`` on
     *         the name-addressed value model ``.array`` settled (issue #794).
     *
     *  The initialiser: the one write that **sizes**. The store refuses an
     *  index past the end rather than growing the array, so before an
     *  index-addressed write pattern can land at all a patch has to make the
     *  positions exist — and this object is how it does that in one message:
     *  ``.array.fill notes 8`` then a bang leaves ``notes`` holding eight
     *  ``0``\ s, and every position 0..7 is now real.
     *
     *  Everything about the binding is ``gArrayEndsBase``'s, inherited whole:
     *  the array is bound from the first creation argument on the control
     *  thread, an ``array <name>`` message is honoured only when it names the
     *  array already bound, an unnamed object fills a private, empty array of
     *  its own, and refusals are counted, never logged.
     *
     *  ### A fill replaces — #794's decisions, written down
     *
     *  The issue asks whether a fill replaces the contents or appends to
     *  them, and whether a fill shorter than the current length shrinks the
     *  array. Decided here, together, because they are one question: **the
     *  array becomes exactly ``count`` copies of the value.** Replacing is
     *  what the object exists for — sizing and initialising in one message is
     *  only deterministic when the result does not depend on what was already
     *  there, and Max agrees: its ``array.fill`` makes "a new array object
     *  ... at the length specified". So a fill shorter than the current
     *  length shrinks the array, and a fill of 0 clears it — and still
     *  announces, the ask being well-formed. Appending already has an object:
     *  ``.array.push``.
     *
     *  The count is bounded at ``arrayStore::MAX_ELEMENTS`` and **counted
     *  rather than truncated** — the store's rule: an inlet refuses an
     *  out-of-range count before storing it, and the apply path refuses one
     *  only a creation argument can plant. The value is **one atom**, at most
     *  ``ELEMENT_CAPACITY`` characters — an element is one atom, the store's
     *  first rule — defaulting to ``0``, Max's "without any initial data, the
     *  array will be filled with 0s".
     *
     *  ### Value hot, count cold — Max's own inlets
     *
     *  Max's ``array.fill`` takes the datum on the left inlet and the length
     *  on the right, which is also ``.array.insert``'s arrangement, so the
     *  port keeps it. Three inlets, one outlet:
     *
     *  - **An int, a float or a symbol on the value inlet fills now** — the
     *    bound array becomes the stored count of copies of that value, spelled
     *    the way it arrived (``ExprFormatValue``'s spelling, so ``7.5`` stays
     *    visibly a float), and **stores nothing** — ``gArrayIndexMap``'s
     *    trigger rule: a bang after it fills with the author's value, not the
     *    inline one. A non-finite float is refused: it has no spelling that
     *    reads back. A symbol arrives as a one-atom list, kept equivalent to
     *    the number so a value-producing outlet still lands.
     *  - **A bang fills with the stored count and the stored value** — the
     *    count the last int on the count inlet stored, seeded by the second
     *    creation argument (0 when absent, so a bare ``.array.fill <name>``
     *    bang clears); the value the third creation argument, ``0`` when
     *    absent.
     *  - **A list of more than one atom is refused whole.** The fill value is
     *    one atom, and Max's fill-with-these-contents list gesture
     *    deliberately does not land here: this port fills with *a repeated
     *    value* (the issue's scope), and a two-element content list quietly
     *    re-read as ``<count> <value>`` would be silent corruption of exactly
     *    the message a ported patch sends. Contents belong to ``.array``'s
     *    own ``set``.
     *  - **An int on the count inlet stores the count, silently** — the cold
     *    half of the Max idiom. Negative is malformed (a count is a size, not
     *    a distance) and past ``MAX_ELEMENTS`` could never land: both are
     *    refused before they are stored, so a bang after the refusal fills
     *    with what it would have filled before it. A float truncates to an
     *    int first; a non-finite one is refused rather than quietly becoming
     *    count 0.
     *  - **``array <name>`` on the value inlet fills with the stored count
     *    and value** when it names the array already bound — the message an
     *    ``.array``'s reference outlet emits on a bang, the family's gesture
     *    — and on the reference inlet it is acknowledged silently; anything
     *    else on either is refused and counted.
     *  - **The outlet emits the bound array's reference after a fill that
     *    landed** — the way an array leaves an object on the value model, so
     *    the family chains: into ``.array.length`` it reports the new size. A
     *    refused fill emits nothing, and an unnamed object stays silent — the
     *    fill happens, but there is no name to pass on.
     *
     *  ### One guard hold — the mid-walk answer, written down
     *
     *  #794 inherits #548's warning that writes renumber and asks what a
     *  write arriving mid-walk does. The answer is the one #782–#793 gave:
     *  **there is no walk to be in the middle of.** The value is validated
     *  before the store's guard is taken and the whole fill — every assign,
     *  the clears behind a shorter result, the new count — is one hold of it,
     *  so the array is never observable half-filled. A writer on another
     *  thread loses the try-lock while the fill holds it (dropped and
     *  counted, the store's rule), and the reference is sent after the guard
     *  is released, so a write it triggers changes what the *next* trigger
     *  sees, never the one in flight.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlets, the
     *  family's rule. No message path allocates, locks or blocks: the name is
     *  resolved on the control thread, an arriving number is rendered by
     *  ``ExprFormatValue`` into a fixed ``AtomList``, the stored value is a
     *  fixed buffer derived once per re-parse on the control thread, the fill
     *  is bounded assigns into storage the store reserved at construction,
     *  and the reference is built once per rebind, so a landed fill is a send
     *  of a string the object already owns.
     */
    class gArrayFill : public gArrayEndsBase {
    public:
      gArrayFill();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_FILL;
      }
      CREATE(gArrayFill)

      /** @brief The stored count the next bang fills to — the last int
       *         received on the count inlet, seeded by the second creation
       *         argument (0 when absent). */
      int StoredCount() const {
        return count.load(std::memory_order_relaxed);
      }

      /** @brief The message the outlet emits after a fill that landed —
       *         ``"array <name>"``, or empty for an unnamed object. */
      const std::string& Reference() const {
        return reference;
      }

      void BangIn(int inlet, YSE::THREAD thread);
      void IntIn(int value, int inlet, YSE::THREAD thread);
      void FloatIn(float value, int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      // Extends gArrayEndsBase's hook so a re-parse resets the stored count
      // and value along with the name: SetParams("") must not keep filling
      // with whatever the previous arguments planted. gArrayPositionBase's
      // rule.
      void ClearParams() override;

      // Rebuilds the reference and the stored-value buffer after a re-parse.
      void ParamsChanged() override;

    private:
      // Rebuild `reference` from the current name. Control thread only.
      void RefreshReference();

      // Derive the fixed stored-value buffer from the `value` parameter.
      // Control thread only (SetParams / SetParent / SetName), like every
      // derivation from the creation arguments.
      void RefreshStoredValue();

      // Store a count arriving on the cold inlet. Negative or past
      // MAX_ELEMENTS is refused and counted, and the stored count does not
      // move — a bang after the refusal fills with what it would have filled
      // before it.
      void StoreCount(int newCount);

      // A fill with the stored configuration — what a bang and the reference
      // gesture both come down to.
      void FillStored(YSE::THREAD thread);

      // The fill itself: validate the value and the count outside the guard,
      // make the array exactly `count` copies of the value under one hold of
      // it, release, then emit the reference. See the class notes on
      // replace-not-append.
      void Fill(const char* text, std::size_t length, YSE::THREAD thread);

      // The reference out the outlet, after the guard is released. An
      // unnamed object has no name to pass on — the announcement is simply
      // empty.
      void Announce(YSE::THREAD thread);

      // The stored count. Atomic because a number may arrive on any thread
      // while the control thread re-parses the creation arguments; never a
      // lock. The second creation argument seeds it.
      std::atomic<int> count{0};

      // The author's fill value — the third creation argument, control-thread
      // state the value inlet never rewrites: an inline value is applied at
      // the moment it arrives and stored nowhere.
      std::string value;

      // `value` rendered into fixed storage at parse time, so a bang on any
      // thread fills from a buffer that never reallocates — `reference`'s
      // arrangement. Length 0 marks an author's value no element can hold
      // (past ELEMENT_CAPACITY), which Fill refuses, counted: refusal, never
      // truncation. An empty parameter derives "0", Max's default.
      char storedValue[arrayStore::ELEMENT_CAPACITY + 1] = {};
      std::size_t storedValueLength = 0;

      // Working atom for an inline value, parsed (and, for a number,
      // rendered) before the guard is taken. Fixed storage — the price of
      // never allocating on a message path. gArrayEndsWriter's `pending`.
      AtomList pending;

      // "array <name>", built once per rebind so a landed fill is a send of
      // a string the object already owns rather than a concatenation on
      // whichever thread the message arrived on.
      std::string reference;
    };

  } // namespace PATCHER
} // namespace YSE
