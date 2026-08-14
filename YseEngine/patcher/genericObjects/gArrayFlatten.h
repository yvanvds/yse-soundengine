#pragma once
#include "../pAtomList.h"
#include "../pObject.h"
#include "gArray.h"
#include "gArrayEnds.h"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Flatten several named arrays into one sequence — Max's
     *         ``array.flatten`` on the name-addressed value model ``.array``
     *         settled (issue #795).
     *
     *  ### What "nested" means here — #795's design question, answered
     *
     *  Max's ``array.flatten`` descends into an array of arrays. On the value
     *  model there is no such thing to descend into: an element is **one
     *  atom** (see ``arrayStore``'s notes), so an array cannot hold an array,
     *  and what stands where Max's nesting stood is **a group of named
     *  arrays** — each name addressing a sequence of its own, exactly what
     *  ``.array.concat`` reads two of. A name is resolvable only on the
     *  control thread (the registry takes a mutex), so the names are
     *  **creation arguments** — the one portable answer #795 offers, taken:
     *
     *  ``.array.flatten <first> <second> ... <last>``
     *
     *  binds every name once, on the control thread, in ``SetParent`` /
     *  ``PARM_PARSE`` / ``RefreshBinding``, and none may be re-pointed from a
     *  message. The first name is ``gArrayEndsBase``'s own binding; the
     *  trailing names are a list parameter bound into fixed slots of this
     *  object's — ``gArraySetOpBase``'s right side, generalised. Max's
     *  ``@depth`` has no counterpart: the grouping is one level deep by
     *  construction, because a name names an array of atoms.
     *
     *  ### The semantics, which are ``.array.concat``'s over N arrays
     *
     *  A flatten is the named arrays' elements in argument order, everything
     *  kept — repeats included, order preserved, no thinning — the read-only
     *  keep-everything walk ``.array.concat`` established for two names,
     *  taken over as many as ``MAX_SOURCES``. Nothing is ever written to any
     *  store, and the result leaves as **the list it spells**, never as a
     *  new named array — the value model's form of "a new array", lossless
     *  into another ``.array`` by the one-atom-per-element rule. An empty
     *  result — every source empty, or unnamed and therefore private — bangs
     *  the **empty outlet**: "no data" is a state a patch must route on.
     *
     *  ### One guard at a time, in sequence
     *
     *  The pair objects snapshot the left array so the right store's guard is
     *  never nested inside it. Over N sources the same rule holds with no
     *  snapshot at all: each source is read under **its own guard alone**,
     *  the elements appended to the result the object owns, the guard
     *  released before the next is taken. No two guards are ever held at
     *  once, so no lock-ordering obligation lands on any pair of objects
     *  naming the same arrays, and one array named twice —
     *  ``.array.flatten seq seq`` — answers the sequence doubled instead of
     *  tripping over its own try-lock. Each hold is a moment: a write landing
     *  between two of them shows in the result exactly as it would had the
     *  ask arrived after it, the pair's documented behaviour. Any guard lost
     *  is the ask refused whole and counted — the sources' state is unknown,
     *  and a flatten missing an array in the middle would be truncation by
     *  another name.
     *
     *  ### More names than ``MAX_SOURCES`` refuses every ask
     *
     *  The slot table is fixed at construction — the price of never
     *  allocating on a message path — so the trailing names are bounded at
     *  ``MAX_SOURCES`` arrays in all. A creation line that spells more binds
     *  **nothing beyond the first** and marks the object overflowed: every
     *  ask is then refused whole and counted, at the moment a patch can
     *  observe it, until a re-parse spells a configuration that fits. A
     *  flatten that silently dropped its seventeenth array would be
     *  truncation by another name — the family's refusal-over-truncation
     *  rule, applied to the configuration itself.
     *
     *  ### What arrives, and what leaves
     *
     *  Two inlets, two outlets — the pair's shape:
     *
     *  - **A bang on the trigger asks** for the flattened sequence.
     *    ``array <name>`` does the same when it names the **first** array —
     *    the message an ``.array``'s reference outlet emits on a bang, the
     *    family's gesture. Anything else there is refused and counted:
     *    resolving an unrecognised name means the registry's mutex on
     *    whatever thread the message arrived on.
     *  - **The sources inlet acknowledges** ``array <name>`` naming any of
     *    the trailing arrays, silently — so a patch may wire every source's
     *    reference outlet across, as it would in Max — and refuses anything
     *    else, the first array's name included: each inlet is bound to its
     *    own side, ``.array.concat``'s rule.
     *  - **The result outlet** carries the flattened sequence as the list it
     *    spells — one element as the atom it is, several as list text. A
     *    result that outruns what a cord carries is refused whole and
     *    counted, ``.array.at``'s whole-reply rule.
     *  - **The empty outlet** bangs when the flatten selected nothing.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlets, the
     *  family's rule. No message path allocates, locks or blocks: every name
     *  is resolved on the control thread, the walk is bounded appends into an
     *  ``AtomList`` reserved at construction, the sources-inlet compare is at
     *  most ``MAX_SOURCES`` bounded byte compares, and refusals are counted,
     *  never logged.
     */
    class gArrayFlatten : public gArrayEndsBase {
    public:
      gArrayFlatten();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_FLATTEN;
      }
      CREATE(gArrayFlatten)

      /**
       *  @brief Most arrays one flatten reads, the first included — 16.
       *
       *  The bound that keeps the slot table, and the sources-inlet compare,
       *  fixed at construction. Not the bound a patch meets first: sixteen
       *  full arrays spell sixteen times the atoms a cord carries, so the
       *  cord's own ceiling refuses long before the table would.
       */
      static constexpr std::size_t MAX_SOURCES = 16;

      /** @brief How many trailing arrays the creation arguments bound — 0
       *         when none were given, and 0 while overflowed. */
      std::size_t ExtraSources() const {
        return extraCount;
      }

      /** @brief Whether the creation line spelled more than ``MAX_SOURCES``
       *         arrays — every ask is then refused whole until a re-parse.
       */
      bool Overflowed() const {
        return overflowed;
      }

      /** @brief The name the trailing source at @p index was bound from, or
       *         empty past the bound count. Diagnostics and tests. */
      std::string ExtraName(std::size_t index) const;

      /** @brief The address the trailing source at @p index is registered
       *         under — ``"<patcherName>.<name>"`` — or empty while it is
       *         private or past the bound count. Diagnostics and tests. */
      std::string ExtraAddress(std::size_t index) const;

      void BangIn(int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

      // Re-bind every side after SetParent / a patcher rename: the base
      // re-anchors the first array, the override adds the trailing ones —
      // gArraySetOpBase's arrangement, on the hook the base makes virtual
      // for exactly this extension.
      void SetParent(pObject* parent) override;
      void RefreshBinding() override;

    protected:
      // Extends gArrayEndsBase's hook so a re-parse resets the trailing
      // names along with the first: SetParams("") must not keep reading
      // whatever the previous arguments pointed at. gArraySetOpBase's rule.
      void ClearParams() override;

      // Called by the base after ClearParams / ParseParams have re-read the
      // names — the moment the trailing bindings follow the first one.
      void ParamsChanged() override;

    private:
      // Point the slot table at the current names and parent address —
      // gArraySetOpBase::RebindRight over every trailing name. Control
      // thread only; an unchanged binding keeps its store. More names than
      // the table holds binds nothing and marks the object overflowed.
      void RebindExtras();

      // What a bang and the first array's reference both come down to: read
      // every source under its own guard alone, appending to the result,
      // release, then send — the list out the result outlet, or a bang out
      // the empty outlet when nothing was collected.
      void Ask(YSE::THREAD thread);

      // The trailing names — the creation arguments after the first, which
      // is the base's. A list parameter, so the object takes as many as the
      // author spells; the slot table below is what bounds the binding.
      std::vector<std::string> moreNames;

      // One bound trailing source: its address key and its store — the
      // base's left-side pair, per slot. The stores are resolved on the
      // control thread and only read on message paths, so no refcount
      // operation ever lands on the audio thread.
      struct extraSource {
        std::string address;
        std::shared_ptr<arrayStore> store;
      };
      extraSource extras[MAX_SOURCES - 1];
      std::size_t extraCount = 0;

      // True when the creation line spelled more arrays than MAX_SOURCES —
      // the configuration refused whole, so every ask refuses too. Written
      // on the control thread only.
      bool overflowed = false;

      // The result, built one guard hold at a time and sent after the last
      // is released. An AtomList rather than a string because it carries the
      // patcher's own bound on how much list text may travel down a cord.
      AtomList result;

      // Render buffer for the result outlet, reserved to
      // AtomList::RENDER_CAPACITY at construction.
      std::string emitScratch;
    };

  } // namespace PATCHER
} // namespace YSE
