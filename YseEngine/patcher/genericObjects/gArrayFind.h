#pragma once
#include "../pObject.h"
#include "gArray.h"
#include "gArrayEnds.h"
#include <cstddef>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Shared body for the two search objects of the ``array.*``
     *         family — ``.array.indexof`` and ``.array.index`` (issue #786).
     *
     *  Both are one ``ArrayFind`` — a bounded linear scan, which is what a
     *  sequence offers: an array has no key to index by — and they differ
     *  only in how the answer leaves, which is why one base carries the
     *  whole search and each object carries only its reporting. The lookup a
     *  patch needs before it can ``.array.at`` around a found element, and
     *  the membership test every set-like patch is built from.
     *
     *  Everything about the binding is ``gArrayEndsBase``'s, inherited
     *  whole: the array is bound from the first creation argument on the
     *  control thread, an ``array <name>`` message is honoured only when it
     *  names the array already bound, an unnamed object searches a private,
     *  empty array of its own, and refusals are counted, never logged. What
     *  this base adds is the **stored search value**: the second creation
     *  argument seeds it, a value arriving on the hot inlet replaces it, and
     *  a bang — or the bound array's reference — re-searches with it,
     *  ``gArrayAt``'s stored index with an atom where the index was.
     *
     *  ### A value is one atom, and equality is its spelling
     *
     *  An element is one atom (the argument is on ``arrayStore``), so the
     *  only thing that can be *in* an array is one atom — a search value is
     *  therefore one atom too, and a multi-atom list on the value inlet is
     *  refused whole and counted. Max's ``array.indexof`` matches a
     *  multi-element input against complete sub-arrays; that is a match over
     *  a *sequence of positions*, which renumbering writes would tear, and
     *  it is not offered — refused rather than guessed, exactly as
     *  ``.array.remove`` refuses a multi-position list.
     *
     *  A match is ``ArrayFind``'s byte compare: an element equals the value
     *  that spells it. An int ``7`` searches as ``7`` and finds the element
     *  a ``7`` stored; a float ``7.`` searches as ``7.`` — the one spelling
     *  ``ExprFormatValue`` gives every float in this patcher, so what a
     *  float push stored is what a float search finds — and ``7`` and ``7.``
     *  are different elements, which is the same fact ``getvalue`` spells
     *  them differently. Case-sensitive for symbols, as every comparison in
     *  this patcher is; ``.tolower`` exists to fold both sides first.
     *
     *  ### First position only — the every-or-first decision #786 asks for
     *
     *  ``ArrayFind`` reports the **first** matching position, and so do both
     *  objects. Every position as a list is not offered, for two reasons
     *  written down here so the family does not relitigate them: the reply
     *  stays one int, so it can feed ``.array.at``, arithmetic and a
     *  comparison directly, where a list of positions would also blur the
     *  miss (``-1`` and an empty list cannot both travel); and ``insert``
     *  and ``delete`` renumber, so a *set* of positions is stale as a set
     *  the moment a write lands, where a single position is simply a
     *  position — what the unchanged answer names after a renumbering write
     *  is what a position means. Max's ``@all`` and ``@offset`` attributes
     *  are deliberately not ported with the objects.
     *
     *  ### One guard hold — the concurrent-write answer, once more
     *
     *  #786 inherits #548's warning about renumbering writes and asks what a
     *  write arriving mid-walk does to the scan. The answer is the one #782,
     *  #784 and #785 gave: **there is no walk to be in the middle of**. A
     *  search — storing the value and scanning for it — happens under one
     *  hold of the store's guard, so the position reported is the position
     *  as the array stood at the trigger; a writer on another thread loses
     *  the try-lock while the scan holds it (dropped and counted, the
     *  store's rule), and the answer is sent after the guard is released, so
     *  a write the answer triggers changes what the *next* search sees,
     *  never the one in flight. The stored value is guarded by that same
     *  hold — it is read and written on whatever threads dispatch, and the
     *  search that uses it is already inside — so a message that loses the
     *  guard is refused whole: neither the value nor an answer.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — both objects are driven by their
     *  inlets, the family's rule. No message path allocates, locks or
     *  blocks: the name is resolved on the control thread, an arriving
     *  number is rendered by ``ExprFormatValue`` into a stack buffer, the
     *  stored value is a fixed buffer written under the guard, and the
     *  answer is an int or a bang.
     */
    class gArrayFindBase : public gArrayEndsBase {
    public:
      /** @brief The stored search value — the last atom received on the
       *         value inlet, seeded by the second creation argument; empty
       *         while there is none. Diagnostics and tests: takes the
       *         store's guard and allocates, so control thread only. */
      std::string Value() const;

      void BangIn(int inlet, YSE::THREAD thread);
      void IntIn(int value, int inlet, YSE::THREAD thread);
      void FloatIn(float value, int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      gArrayFindBase();

      // Extends gArrayEndsBase's hook so a re-parse resets the stored value
      // along with the name: SetParams("") must not keep searching for
      // whatever the previous arguments planted. gArrayPositionBase's rule.
      void ClearParams() override;

      // Syncs the stored value from the seed argument after a re-parse —
      // under the store's guard, as every access to the stored value is. An
      // absent argument means no value; an over-long one cannot be an
      // element at all, so it becomes no value too, counted.
      void ParamsChanged() override;

      // How the answer leaves — the whole difference between the two
      // objects. Called after the store's guard is released, so a send may
      // run the downstream graph freely. `position` is meaningful only when
      // `found`.
      virtual void Report(bool found, std::size_t position, YSE::THREAD thread) = 0;

    private:
      // Store `text` as the search value and scan for it, both under one
      // hold of the store's guard; release, then Report. A lost guard
      // refuses the message whole — neither the value nor an answer. The
      // caller has already bounded `length` to 1..ELEMENT_CAPACITY.
      void SearchFor(const char* text, std::size_t length, YSE::THREAD thread);

      // A search with the stored value — what a bang and the reference
      // gesture both come down to. Refuses (counted) when no value has ever
      // arrived: an absent value is malformed, where a miss is a well-formed
      // value the array happens not to hold.
      void SearchStored(YSE::THREAD thread);

      // The seed — the second creation argument, control-thread state the
      // hot inlet never touches. The live copy below is what messages read
      // and write, so a run-time value never rewrites the author's
      // arguments.
      std::string seedValue;

      // The stored search value, guarded by the *store's* guard: every read
      // and write happens inside a hold the search needs anyway, so there is
      // no second flag to order against it. A plain array rather than a
      // string because it is written from inside the critical section —
      // gArray's `fetched`, for gArray's reason. Length 0 means no value.
      char storedValue[arrayStore::ELEMENT_CAPACITY + 1] = {};
      std::size_t storedLength = 0;
    };

    /**
     *  @brief Output the position of a value — Max's ``array.indexof`` on
     *         the name-addressed value model ``.array`` settled (issue
     *         #786).
     *
     *  The in-band reporter: one int outlet, and every answered search emits
     *  exactly one int — the first matching position, or **-1 on a miss**,
     *  Max's own answer and the one a patch can test with ``.sel -1`` or
     *  feed straight into a comparison. ``.array.index`` is the same search
     *  with the miss split onto an outlet instead; wire whichever form the
     *  patch downstream wants to consume.
     *
     *  Read ``gArrayFindBase`` for what arrives — a value on the hot inlet
     *  searches and stores, a bang re-searches with the stored value, seeded
     *  by the second creation argument, and the bound array's reference on
     *  the hot inlet is the family gesture: bang the array, out comes the
     *  stored value's position. An empty or unnamed (private) array misses
     *  every search.
     */
    class gArrayIndexOf : public gArrayFindBase {
    public:
      gArrayIndexOf();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_INDEXOF;
      }
      CREATE(gArrayIndexOf)

    protected:
      void Report(bool found, std::size_t position, YSE::THREAD thread) override;
    };

    /**
     *  @brief Output whether, and where, a value is held — Max's
     *         ``array.index`` reinterpreted as the membership test #786
     *         defines, on the value model ``.array`` settled.
     *
     *  The out-of-band reporter — the family's split, ``.array.at``'s
     *  shape: the first matching position leaves the position outlet on a
     *  hit, and a miss bangs the miss outlet, so *whether* is which outlet
     *  fired and *where* is the int. The membership test becomes a cord
     *  choice rather than a comparison: wire the position outlet to the
     *  "held" branch and the miss outlet to the "not held" branch, no
     *  ``.sel -1`` in between. ``.array.indexof`` is the same search
     *  answered in-band.
     *
     *  (Max's own ``array.index`` extracts the element at an index — which
     *  is ``.array.at`` here; #786 assigns this name the membership half of
     *  the search pair instead, and the issue is the spec.)
     *
     *  Read ``gArrayFindBase`` for what arrives; an empty or unnamed
     *  (private) array misses every search, so "is it held" over one that
     *  holds nothing answers no, on the miss outlet.
     */
    class gArrayIndex : public gArrayFindBase {
    public:
      gArrayIndex();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_INDEX;
      }
      CREATE(gArrayIndex)

    protected:
      void Report(bool found, std::size_t position, YSE::THREAD thread) override;
    };

  } // namespace PATCHER
} // namespace YSE
