#pragma once
#include "../pObject.h"
#include "gArray.h"
#include "gArrayEnds.h"
#include <atomic>
#include <cstddef>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Thin an array by removing near-duplicate neighbours — issue
     *         #807's reading of Max's ``array.thin`` on the name-addressed
     *         value model ``.array`` settled (#548).
     *
     *  The decimator: a control stream sampled faster than anything
     *  downstream needs, reduced to the values that actually differ. Max's
     *  own ``array.thin`` is ``zl thin``'s wholesale dedupe, and on this
     *  family that operation already exists — ``.array.unique`` — so this
     *  object is the *neighbour* thin #807 specifies instead: one scan
     *  comparing each element with the one before it, the repeats and the
     *  near-repeats dropped, the distinct values kept in order however often
     *  they recur later.
     *
     *  Everything about the binding is ``gArrayEndsBase``'s, inherited whole:
     *  the array is bound from the first creation argument on the control
     *  thread, an ``array <name>`` message is honoured only when it names the
     *  array already bound, an unnamed object thins a private, empty array of
     *  its own, and refusals are counted, never logged.
     *
     *  ### Which thin this is — #807's exact-or-tolerance decision, written down
     *
     *  The issue asks whether this is the exact-match thin — ``.array.unique``
     *  restricted to neighbours — or a tolerance thin over numeric elements,
     *  and the answer is **both, by one parameter, exact by default**. The
     *  tolerance seeds from the second creation argument and its absence (0)
     *  means the exact thin: the family's byte compare, so a bare
     *  ``.array.thin <name>`` thins symbols as readily as numbers and keeps
     *  ``7`` and ``7.`` distinct — equality is the spelling, every
     *  comparison's rule (``.array.unique``, ``.array.indexof``,
     *  ``.array.replace``). A *positive* tolerance is the thin the issue's
     *  use case actually needs — a densely sampled stream rarely repeats a
     *  value exactly — and it widens "near" for the pairs that can carry a
     *  distance: a neighbour pair where **both** elements read as numbers
     *  (``ReadNumericToken``'s strict yes/no) is near when their distance is
     *  at most the tolerance. A pair that is not wholly numeric falls back to
     *  the byte compare at any tolerance — an exact duplicate is the nearest
     *  duplicate there is, so it always goes, while distinct symbols always
     *  survive, having no distance to measure.
     *
     *  ### The baseline is the last survivor
     *
     *  Each element is compared with the element it would otherwise sit next
     *  to *after* the thin — the last survivor — not with its original
     *  predecessor. Against the original predecessor a drift disappears: a
     *  stream climbing in steps each inside the tolerance would collapse to
     *  its first element even though its ends are far apart, so the thinned
     *  array could misstate the stream by an unbounded amount. Against the
     *  survivor the error is the tolerance and no more: every removed element
     *  is within it of the element that now stands for it, and any two
     *  surviving neighbours differ by more than it (or spell differently) —
     *  the guarantee that makes a decimated control stream usable.
     *
     *  ### One guard hold — the mid-walk answer, written down
     *
     *  #807 inherits #548's warning that writes renumber and asks what a
     *  write arriving mid-walk does. The answer is ``.array.filter``'s:
     *  **the whole thin is one hold of the store's guard**, a single scan in
     *  which the survivors close ranks in their original order — each moved
     *  at most once, a bounded ``assign`` into storage the store reserved at
     *  construction — and the vacated slots are cleared behind the new
     *  count. (The issue sketches ``ArrayEraseAt`` for the ones that go; an
     *  erase per removal shifts the whole tail every time, where the
     *  compaction is the same result in one bounded scan — the arithmetic
     *  the audio thread should pay.) Yes, a thin renumbers under every other
     *  object on the name, exactly as ``.array.remove`` and
     *  ``.array.filter`` do — renumbering is what a removal is. A writer on
     *  another thread loses the try-lock while the thin holds it (dropped
     *  and counted, the store's rule), and both sends happen after the guard
     *  is released, so a write they trigger changes what the *next* thin
     *  sees, never the one in flight.
     *
     *  ### What arrives, and what leaves
     *
     *  ``gArrayEndsRemover``'s trigger with ``.array.fill``'s cold
     *  configuration. Three inlets, two outlets:
     *
     *  - **A bang on the trigger inlet thins now** — a thin is asked for
     *    with a bang, never addressed, so there is no int or float method on
     *    the hot side, ``.array.pop``'s rule.
     *  - **``array <name>`` on the trigger inlet thins** when it names the
     *    array already bound — the message an ``.array``'s reference outlet
     *    emits on a bang, the family's gesture — and on the reference inlet
     *    it is acknowledged silently; anything else on either is refused and
     *    counted.
     *  - **An int or a float on the tolerance inlet stores the tolerance,
     *    silently** — the cold half of the idiom, ``.array.fill``'s count
     *    inlet. 0 restores the exact thin; a negative or non-finite value is
     *    refused before it is stored — never clamped, the family's rule — a
     *    distance cannot be negative, and a non-finite tolerance would call
     *    everything near. An out-of-range creation argument is refused at
     *    the trigger instead, ``.array.stream``'s rule for a seed only the
     *    creation string can plant.
     *  - **The removed count leaves the count outlet, then the reference
     *    outlet emits ``array <name>``** — right to left, ``.array.replace``'s
     *    announcement — because the count is the only way a patch can tell
     *    thinned-nothing from thinned-a-lot: a thin that removed nothing
     *    still landed and reports 0, where a refused one (a lost try-lock, a
     *    malformed tolerance) reports nothing on either outlet. An unnamed
     *    object still counts — an answer is a value, not an identity — but
     *    has no name to pass on, so the reference stays silent.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlets,
     *  the family's rule. No message path allocates, locks or blocks: the
     *  name is resolved on the control thread, reading an element as a
     *  number is one bounded copy into a stack buffer (``ReadNumericToken``,
     *  parsed once per element per scan), every move is an ``assign`` into
     *  storage the store reserved at construction, and the announcement is
     *  an int and a string the object already owns.
     */
    class gArrayThin : public gArrayEndsBase {
    public:
      gArrayThin();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_THIN;
      }
      CREATE(gArrayThin)

      /** @brief The stored tolerance — the last value received on the
       *         tolerance inlet, seeded by the second creation argument. 0
       *         (the default) means the exact thin. */
      float StoredTolerance() const {
        return tolerance.load(std::memory_order_relaxed);
      }

      /** @brief The message the reference outlet emits after a thin that
       *         landed — ``"array <name>"``, or empty for an unnamed
       *         object. */
      const std::string& Reference() const {
        return reference;
      }

      void BangIn(int inlet, YSE::THREAD thread);
      void IntIn(int value, int inlet, YSE::THREAD thread);
      void FloatIn(float value, int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      // Extends gArrayEndsBase's hook so a re-parse resets the stored
      // tolerance along with the name: SetParams("") must not keep thinning
      // to the distance the previous arguments configured. gArrayStream's
      // rule.
      void ClearParams() override;

      // Rebuilds the reference after a re-parse.
      void ParamsChanged() override;

    private:
      // Rebuild `reference` from the current name. Control thread only.
      void RefreshReference();

      // Store a tolerance arriving on the cold inlet. Negative or non-finite
      // is refused and counted, and the stored tolerance does not move — a
      // trigger after the refusal thins to the distance it would have
      // thinned to before it.
      void StoreTolerance(float value);

      // The thin itself: validate the stored tolerance, compact under one
      // hold of the store's guard, release, then announce. A lost guard or a
      // malformed tolerance (a negative creation argument) refuses the
      // message whole.
      void Thin(YSE::THREAD thread);

      // The scan: every element near the last survivor before it is
      // dropped, the survivors closing ranks in original order. The caller
      // holds the store's guard and has proven `tol` finite and >= 0.
      // Returns how many elements went.
      std::size_t ThinLocked(float tol);

      // The announcement, after the guard is released: the removed count out
      // the count outlet, then the reference — right to left, so the number
      // has arrived wherever it is wired by the time the reference triggers
      // the family downstream.
      void Announce(std::size_t removed, YSE::THREAD thread);

      // The stored tolerance. Atomic because a number may arrive on any
      // thread while another thins; never a lock. The second creation
      // argument seeds it; 0 — the default — is the exact thin.
      std::atomic<float> tolerance{0.f};

      // "array <name>", built once per rebind so a landed thin is a send of
      // a string the object already owns rather than a concatenation on
      // whichever thread the trigger arrived on.
      std::string reference;
    };

  } // namespace PATCHER
} // namespace YSE
