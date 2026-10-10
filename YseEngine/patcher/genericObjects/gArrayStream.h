#pragma once
#include "../pAtomList.h"
#include "../pObject.h"
#include "gArrayEnds.h"
#include <atomic>
#include <cstddef>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Collect incoming values into a sliding array — Max's
     *         ``array.stream`` on the name-addressed value model ``.array``
     *         settled (issue #806).
     *
     *  The window builder: a stream of values becomes the last N of them,
     *  which is what an analysis patch runs its statistics over —
     *  ``.array.mean`` of the last sixteen intervals, ``.array.stddev`` of
     *  the last eight velocities. ``.zl stream`` already does this for a
     *  *list on a cord*; this is the same operation landing in a *shared,
     *  named* array, so the window is addressable by the whole family rather
     *  than travelling as text.
     *
     *  Everything about the binding is ``gArrayEndsBase``'s, inherited whole:
     *  the array is bound from the first creation argument on the control
     *  thread, an ``array <name>`` message is honoured only when it names the
     *  array already bound, an unnamed object streams into a private, empty
     *  array of its own, and refusals are counted, never logged.
     *
     *  ### The slide is O(n), and that is accepted — #806's decision
     *
     *  The issue observes that a sliding window over a shared store means
     *  every arriving value shifts every element — ``ArrayEraseAt(0)`` plus
     *  ``ArrayAppend`` — and asks whether that is acceptable at
     *  ``MAX_ELEMENTS`` or whether the object should keep a ring index of
     *  its own. **The O(n) slide is accepted, and the ring is rejected.** A
     *  slide is at most ``MAX_ELEMENTS`` (256) bounded ``assign``\ s of at
     *  most ``ELEMENT_CAPACITY`` (64) characters into storage the store
     *  reserved at construction — the same cost ``.array.unshift`` already
     *  pays per add and ``.array.fill`` per fill, and it allocates nothing.
     *  A ring index cannot live in the store without every other object on
     *  the name having to understand it (the issue's own observation), and
     *  it cannot live in this object either: position 0 must *be* the oldest
     *  value, because the whole point of streaming into a shared array is
     *  that ``.array.at``, ``.array.slice`` and the statistics read it as
     *  the ordered window it claims to be, with no decoder in between.
     *
     *  ### One guard hold — the mid-walk answer, written down
     *
     *  #806 inherits #548's warning that writes renumber and asks what a
     *  write arriving mid-walk does. The answer is ``.array.fill``'s:
     *  **there is no walk to be in the middle of.** However many atoms one
     *  message carries, every slide and every append they cause is one hold
     *  of the store's guard, so the array is never observable mid-slide. A
     *  writer on another thread loses the try-lock while the collect holds
     *  it (dropped and counted, the store's rule), and both sends happen
     *  after the guard is released, so a write they trigger changes what the
     *  *next* collect sees, never the one in flight. A foreign writer that
     *  grew the array past the window between messages is simply trimmed by
     *  the next trigger — oldest first, under that trigger's own hold.
     *
     *  ### The window leaves when it is full — ``.zl stream``'s rule
     *
     *  ``.zl stream`` sends its window only once it holds the asked-for
     *  count, with the shortfall out the right outlet either way, and this
     *  object keeps both halves of that contract on the family's transport:
     *
     *  - **The shortfall outlet emits how many values the window still
     *    needs** — 0 once it is full — after every collect that lands,
     *    **before** the reference (Max's outlets firing right to left). It
     *    is what lets a patch see the window filling, and ``.sel 0`` on it
     *    is the "window ready" edge.
     *  - **The reference outlet emits ``array <name>`` only when the array
     *    holds the full window** — emitting a partial window would hand the
     *    statistics downstream a population the patch never asked them to
     *    run over. An unnamed object still counts its shortfall (an answer
     *    is a value, not an identity) but has no name to pass on, so the
     *    reference stays silent — ``.array.fill``'s announce rule.
     *
     *  ### What arrives, and what it does
     *
     *  ``gArrayFill``'s inlet arrangement — the ask on the left, the
     *  configuration on the right. Three inlets, two outlets:
     *
     *  - **An int, a float or a symbol on the value inlet is collected**:
     *    appended to the bound array as the text that spells it
     *    (``ExprFormatValue``'s spelling, so ``7.5`` stays visibly a float),
     *    the oldest element sliding off the front first when the array
     *    already holds the window. A non-finite float is refused: it has no
     *    spelling that reads back.
     *  - **A list is collected whole, in the order sent** — every atom one
     *    step of the slide, so the array ends holding the last ``size``
     *    atoms of the combined sequence — **or refused whole**, one counted
     *    refusal and nothing changed, when any atom outruns
     *    ``ELEMENT_CAPACITY``: ``gArrayEndsWriter``'s whole-or-nothing rule.
     *    One announcement per message, not per atom.
     *  - **A bang re-announces without collecting** — ``.zl stream``'s bang,
     *    which re-sends the window without sliding it. It trims first, so a
     *    window narrowed live — or an array a foreign writer overgrew — is
     *    honest by the time it is announced; ``.zl``'s trim-on-bang, for
     *    ``.zl``'s reason.
     *  - **An int on the size inlet stores the window size, silently** — the
     *    cold half of the idiom, ``.array.fill``'s count inlet. 1 to
     *    ``MAX_ELEMENTS`` (256); 0 is not a window, negative is malformed
     *    and past the store could never fill, so all three are refused
     *    before they are stored — **refused, never clamped**, the family's
     *    rule where ``.zl`` clamps. The cold inlet touches no shared data:
     *    the trim a narrower window implies happens at the next trigger,
     *    under that trigger's hold.
     *  - **``array <name>`` on the value inlet re-announces** when it names
     *    the array already bound — the message an ``.array``'s reference
     *    outlet emits on a bang, the family's gesture — and on the reference
     *    inlet it is acknowledged silently, anything else there refused and
     *    counted. A reference is an identity, not an element, so the bound
     *    one is never collected into the data; only the bound name can be
     *    recognised at all (the bounded compare is the whole of what a
     *    message path may do with a name), so any *other* list, ``array``-
     *    leading included, is simply atoms — ``gArrayEndsWriter``'s rule.
     *
     *  ### No window, no collect
     *
     *  The size seeds from the second creation argument and its absence (0)
     *  means **unconfigured**: a value or a bang arriving while no window is
     *  set is refused whole and counted — malformed, not a miss,
     *  ``gArrayFindBase``'s absent-value rule. ``.zl stream`` silently
     *  discards while unconfigured; on a *shared* store a silent discard
     *  would be indistinguishable from a landed collect, so it is a refusal
     *  the patch can see in ``Dropped()``.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlets,
     *  the family's rule. No message path allocates, locks or blocks: the
     *  name is resolved on the control thread, an arriving number is
     *  rendered by ``ExprFormatValue`` into a fixed ``AtomList``, the slide
     *  is bounded ``assign``\ s into storage the store reserved at
     *  construction, the shortfall is an int, and the reference is built
     *  once per rebind, so a landed collect is a send of a string the object
     *  already owns.
     */
    class gArrayStream : public gArrayEndsBase {
    public:
      gArrayStream();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_STREAM;
      }
      CREATE(gArrayStream)

      /** @brief The stored window size — the last int received on the size
       *         inlet, seeded by the second creation argument. 0 means
       *         unconfigured, and every trigger is refused until a window
       *         is set. */
      int StoredSize() const {
        return size.load(std::memory_order_relaxed);
      }

      /** @brief The message the reference outlet emits when the window is
       *         full — ``"array <name>"``, or empty for an unnamed
       *         object. */
      const std::string& Reference() const {
        return reference;
      }

      void BangIn(int inlet, YSE::THREAD thread);
      void IntIn(int value, int inlet, YSE::THREAD thread);
      void FloatIn(float value, int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      // Extends gArrayEndsBase's hook so a re-parse resets the stored size
      // along with the name: SetParams("") must not keep sliding the window
      // the previous arguments configured. gArrayPositionBase's rule.
      void ClearParams() override;

      // Rebuilds the reference after a re-parse.
      void ParamsChanged() override;

    private:
      // Rebuild `reference` from the current name. Control thread only.
      void RefreshReference();

      // Store a size arriving on the cold inlet. 0, negative or past
      // MAX_ELEMENTS is refused and counted, and the stored size does not
      // move — a trigger after the refusal slides the window it would have
      // slid before it.
      void StoreSize(int newSize);

      // The collect itself: `pending` (>= 1 atoms, every one validated to
      // ELEMENT_CAPACITY by the caller) lands whole under one hold of the
      // store's guard, each atom sliding the oldest element off the front
      // when the array already holds the window; release, then announce.
      void Collect(YSE::THREAD thread);

      // A bang and the reference gesture: trim the array to the window,
      // announce the shortfall and — when it is full — the reference,
      // without collecting anything. .zl stream's bang.
      void AnnounceStored(YSE::THREAD thread);

      // The announcement, after the guard is released: the shortfall out the
      // shortfall outlet, then — only when it is 0, the window full — the
      // reference. Right to left, so the count has arrived wherever it is
      // wired by the time the reference triggers the family downstream.
      void Announce(std::size_t shortfall, YSE::THREAD thread);

      // The stored window size. Atomic because a number may arrive on any
      // thread while the control thread re-parses the creation arguments;
      // never a lock. The second creation argument seeds it; 0 means
      // unconfigured.
      std::atomic<int> size{0};

      // The atoms one message collects, parsed (and, for a number, rendered)
      // before the guard is taken. Fixed storage — the price of never
      // allocating on a message path. gArrayEndsWriter's `pending`.
      AtomList pending;

      // "array <name>", built once per rebind so a full window is a send of
      // a string the object already owns rather than a concatenation on
      // whichever thread the value arrived on.
      std::string reference;
    };

  } // namespace PATCHER
} // namespace YSE
