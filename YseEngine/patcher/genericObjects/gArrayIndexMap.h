#pragma once
#include "../pObject.h"
#include "gArray.h"
#include "gArrayEnds.h"
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Reorder an array by a list of indices — Max's ``array.indexmap``
     *         on the name-addressed value model ``.array`` settled (issue
     *         #787).
     *
     *  The shared reordering primitive applied to a stored array: the object
     *  every custom permutation is built from, and the one ``.zl indexmap``
     *  already gives for a list. ``AtomList::AssignOrder`` is the shape —
     *  compute an order, apply it once — with the array's own zero-based
     *  indices where ``.zl`` keeps Max's 1-based ones, because the family's
     *  positions are ``arrayStore``'s.
     *
     *  Everything about the binding is ``gArrayEndsBase``'s, inherited whole:
     *  the array is bound from the first creation argument on the control
     *  thread, an ``array <name>`` message is honoured only when it names the
     *  array already bound, an unnamed object acts on a private, empty array
     *  of its own, and refusals are counted, never logged. What this object
     *  adds is the **stored map**: the trailing creation arguments seed it, a
     *  list on the map inlet replaces it, and a bang — or the bound array's
     *  reference — applies it, ``gArrayFindBase``'s stored value with a whole
     *  map where the atom was.
     *
     *  ### What arrives, and what leaves
     *
     *  Three inlets, one outlet — ``.array.insert``'s arrangement, the Max
     *  idiom kept: configuration on the right, the ask on the left.
     *
     *  - **A bang on the trigger applies the stored map** — the map the last
     *    list on the map inlet stored, seeded by the trailing creation
     *    arguments. A bang before any map exists is refused and counted:
     *    an absent map is malformed, not a reorder to nothing, and
     *    ``.array``'s own ``clear`` is the object that empties on purpose.
     *  - **A list of indices on the trigger is applied at the moment it
     *    arrives, and stores nothing** — ``gArrayAt``'s list rule: a compound
     *    ask is answered when it arrives, and a bang that replayed the last
     *    *list* would be hidden state no patch can see. The stored map stays
     *    what the map inlet or the arguments made it. A single int is the
     *    one-entry map it spells — kept equivalent to the one-atom list, so a
     *    map-producing outlet that sends its single entry as an int still
     *    lands — and a float truncates to an int first, Max's float method.
     *  - **A list on the map inlet stores the map, silently** — the cold half
     *    of the idiom, ``.zl indexmap``'s right inlet. A single int stores a
     *    one-entry map; a float truncates first. A map with anything in it
     *    that is not a non-negative integer, or longer than ``MAX_INDICES``,
     *    is refused whole and the stored map does not move — negative is
     *    malformed, the family's indexing rule, decided once on
     *    ``arrayStore``.
     *  - **``array <name>`` on the trigger applies the stored map** when it
     *    names the array already bound — the message an ``.array``'s
     *    reference outlet emits on a bang, so wiring that outlet here gives
     *    the family's gesture: bang the array, out comes the reordered
     *    array's reference. On the **map inlet** the same words are not a
     *    map: they fail the index parse and are refused — an identity is not
     *    data, the end-writers' rule.
     *  - **``array <name>`` on the reference inlet is acknowledged
     *    silently** when it names the bound array — ``gDictSlice``'s shape —
     *    and anything else there is refused and counted.
     *  - **The outlet emits the bound array's reference after a reorder that
     *    landed** — the way an array leaves an object on the value model, so
     *    the family chains: into ``.array.length`` it reports the new
     *    length, into ``.array.at`` it fetches from the new order. A refused
     *    reorder emits nothing, and an unnamed object stays silent — the
     *    reorder happens, but there is no name to pass on.
     *
     *  ### An index naming no element contributes nothing
     *
     *  ``AssignOrder``'s established rule, and #787's spec: the map is a list
     *  of independent picks, so a well-formed index past the end drops its
     *  own pick and the rest still land — never a placeholder, never a
     *  refusal of the whole map. Entries may repeat — a map naming the same
     *  element twice produces it twice — and the result is as long as what
     *  landed, which may be shorter or longer than the array was. A map
     *  whose every index misses lands as an empty array, which is what those
     *  picks say. (A *negative* index is different: it is malformed, refused
     *  whole before anything is looked at — the family's split between a
     *  miss and a refusal.)
     *
     *  ### One guard hold, through a scratch table — the two decisions #787
     *      asks for
     *
     *  #787 inherits #548's warning that ``insert`` and ``delete`` renumber
     *  and asks what a write arriving mid-walk does; it also asks whether
     *  applying an order to a *store* takes a second pass or a scratch table,
     *  the source and the destination being the same table. The answers,
     *  written down here so the family does not relitigate them:
     *
     *  - **There is no walk to be in the middle of** — #782/#784/#785's
     *    answer. The whole reorder is one hold of the store's guard, so the
     *    order applied is the array as it stood at the trigger; a writer on
     *    another thread loses the try-lock while the reorder holds it
     *    (dropped and counted, the store's rule), and the reference is sent
     *    after the guard is released, so a write it triggers changes what
     *    the *next* reorder sees, never the one in flight.
     *  - **A scratch ``arrayStore`` the object owns**, not a second pass over
     *    the same table: picking straight into the store would read elements
     *    a previous pick already overwrote, and an in-place permutation
     *    cycle-walk cannot express a map that repeats or drops. The picks
     *    are copied out in map order and copied back, both passes inside the
     *    one hold — bounded assigns into storage both tables reserved at
     *    construction. The scratch is touched only under the bound store's
     *    guard, which is what serialises it.
     *
     *  The stored map is guarded by that same hold — it is read and written
     *  on whatever threads dispatch, and the apply that reads it is already
     *  inside — so a message that loses the guard is refused whole: neither
     *  the map nor a reorder. ``gArrayFindBase``'s arrangement for its
     *  stored value.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlets,
     *  the family's rule. No message path allocates, locks or blocks: the
     *  name is resolved on the control thread, an arriving map is parsed
     *  into fixed storage before the guard is taken, the reorder is bounded
     *  assigns between pre-reserved tables, and the reference is built once
     *  per rebind, so a landed reorder is a send of a string the object
     *  already owns.
     */
    class gArrayIndexMap : public gArrayEndsBase {
    public:
      gArrayIndexMap();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_INDEXMAP;
      }
      CREATE(gArrayIndexMap)

      /** @brief Most entries one map holds — ``arrayStore::MAX_ELEMENTS``,
       *         since a longer map cannot land more picks than the array can
       *         hold (and ``AtomList::MAX_ATOMS`` bounds a cord to the same
       *         number). */
      static constexpr std::size_t MAX_INDICES = arrayStore::MAX_ELEMENTS;

      /** @brief The message the outlet emits after a reorder that landed —
       *         ``"array <name>"``, or empty for an unnamed object. */
      const std::string& Reference() const {
        return reference;
      }

      /** @brief How many entries the stored map holds — 0 while none has
       *         arrived and no argument seeded one. Diagnostics and tests:
       *         takes the store's guard, so control thread only. */
      std::size_t MapSize() const;

      /** @brief Entry @p index of the stored map, or -1 past the end.
       *         Diagnostics and tests; takes the store's guard. */
      int MapAt(std::size_t index) const;

      void BangIn(int inlet, YSE::THREAD thread);
      void IntIn(int value, int inlet, YSE::THREAD thread);
      void FloatIn(float value, int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      // Extends gArrayEndsBase's hook so a re-parse resets the stored map
      // along with the name: SetParams("") must not keep applying whatever
      // the previous arguments planted. gArrayFindBase's rule.
      void ClearParams() override;

      // Rebuilds the reference and syncs the stored map from the seed
      // arguments after a re-parse — under the store's guard, as every
      // access to the stored map is. A token that is not a non-negative
      // integer, or a map past MAX_INDICES, plants no map at all, counted.
      void ParamsChanged() override;

    private:
      // Rebuild `reference` from the current name. Control thread only.
      void RefreshReference();

      // Apply the stored map — what a bang and the reference gesture both
      // come down to. Refuses (counted) when no map has ever arrived: an
      // absent map is malformed, not a reorder to nothing.
      void ApplyStored(YSE::THREAD thread);

      // Apply an arriving map: one hold of the store's guard around
      // ReorderLocked, release, then announce.
      void ApplyMap(const std::size_t* positions, std::size_t count, YSE::THREAD thread);

      // The reorder itself. **The caller holds the store's guard.** Copies
      // every pick into the scratch table in map order, then copies the
      // scratch back — see the class notes on why a scratch table and why
      // one hold. An index naming no element contributes nothing.
      void ReorderLocked(const std::size_t* positions, std::size_t count);

      // Replace the stored map, under the store's guard. A lost guard
      // refuses the message whole and the map does not move.
      void StoreMap(const std::size_t* positions, std::size_t count);

      // The reference out the outlet, after the guard is released. An
      // unnamed object has no name to pass on — the announcement is simply
      // empty.
      void Announce(YSE::THREAD thread);

      // The seed — the trailing creation arguments, control-thread state the
      // inlets never touch. The live map below is what messages read and
      // write, so a run-time map never rewrites the author's arguments.
      std::vector<std::string> seedMap;

      // The stored map, guarded by the *store's* guard: every read and write
      // happens inside a hold the apply needs anyway, so there is no second
      // flag to order against it. gArrayFindBase's storedValue, with a whole
      // map where the atom was. Length 0 means no map.
      std::size_t storedMap[MAX_INDICES] = {};
      std::size_t storedCount = 0;

      // The entries one arriving map names, parsed before the guard is
      // taken. Fixed storage — the price of never allocating on a message
      // path. gArrayAt's `requested`.
      std::size_t requested[MAX_INDICES] = {};

      // The scratch table the reorder copies its picks into under the guard
      // — the object-owned scratch #787 offers as the alternative to a
      // second pass, chosen in the class notes. Its constructor reserves the
      // whole table on the control thread; `busy` and `count` go unused,
      // the bound store's guard being what serialises access to it.
      arrayStore scratch;

      // "array <name>", built once per rebind so a landed reorder is a send
      // of a string the object already owns rather than a concatenation on
      // whichever thread the message arrived on.
      std::string reference;
    };

  } // namespace PATCHER
} // namespace YSE
