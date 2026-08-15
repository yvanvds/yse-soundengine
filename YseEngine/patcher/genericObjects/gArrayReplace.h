#pragma once
#include "../pObject.h"
#include "gArray.h"
#include "gArrayEnds.h"
#include <cstddef>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Replace occurrences of a value — Max's ``array.replace`` on the
     *         name-addressed value model ``.array`` settled (issue #802).
     *
     *  Search and replace over a stored array: retuning every occurrence of
     *  one pitch, swapping a placeholder for a real value. ``.array.indexof``
     *  answers *where* a value is; this object *rewrites* it — the mutating
     *  half of the search pair, and the write ``.array``'s own ``set`` cannot
     *  spell, because ``set`` addresses a position and a replace addresses a
     *  value.
     *
     *  Everything about the binding is ``gArrayEndsBase``'s, inherited whole:
     *  the array is bound from the first creation argument on the control
     *  thread, an ``array <name>`` message is honoured only when it names the
     *  array already bound, an unnamed object rewrites a private, empty array
     *  of its own, and refusals are counted, never logged.
     *
     *  ### Every occurrence — #802's first-or-all decision, written down
     *
     *  The issue asks whether every occurrence or only the first is replaced.
     *  **Every occurrence.** The use case #802 names is "retuning every
     *  occurrence of one pitch", and first-only is already composable —
     *  ``.array.indexof`` into ``.array``'s own ``set`` is exactly one
     *  first-occurrence replace — where all-occurrences is the operation the
     *  family cannot spell in one message. One scan, every element equal to
     *  the find value rewritten, all under one hold of the store's guard.
     *
     *  ### The count leaves — #802's second-outlet decision, written down
     *
     *  The issue observes that a replacement count is the only way a patch
     *  can tell "replaced nothing" from "replaced everything", and this
     *  object emits it: the number of elements rewritten, out the count
     *  outlet, **before** the reference — Max's outlets firing right to
     *  left. A replace that matched nothing still landed — the ask was
     *  well-formed, the array simply holds no such value — so it announces
     *  with count 0, where a *refused* replace (a lost guard, a
     *  half-configured object) emits nothing on either outlet. Replacing a
     *  value with itself lands too, and counts its occurrences.
     *
     *  ### Equality is the spelling
     *
     *  ``ArrayFind``'s byte compare, the family's rule (``.array.indexof``,
     *  ``.array.mode``, ``.array.group``): ``7`` and ``7.`` are different
     *  elements, because they are a different atom downstream. Case-sensitive
     *  for symbols, as every comparison in this patcher is.
     *
     *  ### Find hot, replacement cold — the family's inlets
     *
     *  ``gArrayFill``'s arrangement (the ask on the left, configuration on
     *  the right), with ``gArrayFindBase``'s stored-value semantics on the
     *  hot side. Three inlets, two outlets:
     *
     *  - **An int, a float or a symbol on the find inlet stores the find
     *    value and replaces now** — ``gArrayFindBase``'s hot inlet, with a
     *    rewrite where the report was: a bang afterwards re-replaces with the
     *    same value. A non-finite float is refused: it has no spelling that
     *    reads back. A multi-atom list is refused whole — an element is one
     *    atom, so only one atom can be matched.
     *  - **A bang replaces with the stored find and replacement values** —
     *    seeded by the second and third creation arguments. A trigger before
     *    both exist is refused whole, ``gArrayFindBase``'s absent-value rule:
     *    malformed, not a miss — and nothing is stored by a refused trigger,
     *    so a later bang behaves as if it never arrived.
     *  - **An int, a float or a symbol on the replacement inlet stores the
     *    replacement, silently** — the cold half of the idiom, ``.array.fill``'s
     *    count inlet with an atom where the number was. The same one-atom and
     *    finite-float rules; a reference is two atoms and is refused here —
     *    an identity is not an element, ``gArrayEndsWriter``'s rule.
     *  - **``array <name>`` on the find inlet replaces with the stored
     *    values** when it names the array already bound — the message an
     *    ``.array``'s reference outlet emits on a bang, the family's gesture
     *    — and on the reference inlet it is acknowledged silently; anything
     *    else on either is refused and counted.
     *  - **The count outlet emits how many elements were rewritten**, then
     *    **the reference outlet emits ``array <name>``** — the way an array
     *    leaves an object on the value model, so the family chains: into
     *    ``.array.length`` the reference proves a replace never resizes. An
     *    unnamed object still counts — an answer is a value, not an identity
     *    — but has no name to pass on, so the reference stays silent,
     *    ``.array.fill``'s announce rule.
     *
     *  ### One guard hold — the mid-walk answer, written down
     *
     *  #802 inherits #548's warning that writes renumber and asks what a
     *  write arriving mid-walk does. The answer is ``.array.fill``'s: **the
     *  whole replace is one hold of the store's guard** — the scan, every
     *  rewrite, and the reads of the stored find and replacement values,
     *  which live under that same guard exactly as ``gArrayFindBase``'s
     *  stored value does, so there is no second flag to order against it. A
     *  replace never inserts and never erases, so nothing renumbers at all:
     *  every element keeps its position, only its spelling changes. A writer
     *  on another thread loses the try-lock while the replace holds it
     *  (dropped and counted, the store's rule), and both sends happen after
     *  the guard is released, so a write they trigger changes what the
     *  *next* replace sees, never the one in flight.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlets, the
     *  family's rule. No message path allocates, locks or blocks: the name is
     *  resolved on the control thread, an arriving number is rendered by
     *  ``ExprFormatValue`` into a stack buffer, the stored values are fixed
     *  buffers written under the guard, every rewrite is a bounded ``assign``
     *  into storage the store reserved at construction, and the announcement
     *  is an int and a string the object already owns.
     */
    class gArrayReplace : public gArrayEndsBase {
    public:
      gArrayReplace();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_REPLACE;
      }
      CREATE(gArrayReplace)

      /** @brief The stored find value — the last atom received on the find
       *         inlet, seeded by the second creation argument; empty while
       *         there is none. Diagnostics and tests: takes the store's
       *         guard and allocates, so control thread only. */
      std::string FindValue() const;

      /** @brief The stored replacement — the last atom received on the
       *         replacement inlet, seeded by the third creation argument;
       *         empty while there is none. Control thread only, as above. */
      std::string ReplaceValue() const;

      /** @brief The message the reference outlet emits after a replace that
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
      // Extends gArrayEndsBase's hook so a re-parse resets both stored
      // values along with the name: SetParams("") must not keep replacing
      // whatever the previous arguments planted. gArrayFindBase's rule,
      // twice over.
      void ClearParams() override;

      // Syncs both stored values from the seed arguments after a re-parse —
      // under the store's guard, as every access to them is — and rebuilds
      // the reference.
      void ParamsChanged() override;

    private:
      // Rebuild `reference` from the current name. Control thread only.
      void RefreshReference();

      // Store `text` as the find value and rewrite every occurrence of it,
      // all under one hold of the store's guard; release, then announce. A
      // lost guard — or no stored replacement to rewrite with — refuses the
      // message whole: neither the value nor a rewrite. The caller has
      // already bounded `length` to 1..ELEMENT_CAPACITY.
      void ReplaceWith(const char* text, std::size_t length, YSE::THREAD thread);

      // A replace with the stored find and replacement — what a bang and the
      // reference gesture both come down to. Refuses (counted) while either
      // is absent: malformed, not a miss.
      void ReplaceStored(YSE::THREAD thread);

      // Store `text` as the replacement, silently — the cold inlet. Under
      // the guard, refused (counted) when the try-lock is lost.
      void StoreReplacement(const char* text, std::size_t length);

      // The scan itself: every element spelling the stored find value is
      // assigned the stored replacement. The caller holds the store's guard
      // and has proven both values present. Returns how many were rewritten.
      std::size_t ReplaceLocked();

      // The announcement, after the guard is released: the count out the
      // count outlet, then the reference — right to left, so the count has
      // arrived wherever it is wired by the time the reference triggers the
      // family downstream. An unnamed object counts but stays silent on the
      // reference: there is no name to pass on.
      void Announce(std::size_t replaced, YSE::THREAD thread);

      // The seeds — the second and third creation arguments, control-thread
      // state the inlets never rewrite: the live copies below are what
      // messages read and write, so a run-time value never rewrites the
      // author's arguments.
      std::string findSeed;
      std::string replaceSeed;

      // The stored find value and replacement, guarded by the *store's*
      // guard: every read and write happens inside a hold the replace needs
      // anyway, so there is no second flag to order against it —
      // gArrayFindBase's arrangement, twice. Plain arrays rather than
      // strings because they are written from inside the critical section —
      // gArray's `fetched`, for gArray's reason. Length 0 means no value.
      char storedFind[arrayStore::ELEMENT_CAPACITY + 1] = {};
      std::size_t storedFindLength = 0;
      char storedReplace[arrayStore::ELEMENT_CAPACITY + 1] = {};
      std::size_t storedReplaceLength = 0;

      // "array <name>", built once per rebind so a landed replace is a send
      // of a string the object already owns rather than a concatenation on
      // whichever thread the message arrived on.
      std::string reference;
    };

  } // namespace PATCHER
} // namespace YSE
