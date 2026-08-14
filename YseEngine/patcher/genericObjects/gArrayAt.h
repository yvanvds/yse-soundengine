#pragma once
#include "../pAtomList.h"
#include "../pObject.h"
#include "gArray.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Output the element at an index — Max's ``array.at`` on the
     *         name-addressed value model ``.array`` settled (issue #782).
     *
     *  ``.array``'s own ``get`` needs the index inside the message text;
     *  this is the object a running patch actually wires an index *into*: an
     *  int on the left inlet fetches that position, so the index can arrive
     *  on a cord.
     *
     *  An array never travels down a cord — an ``OUT_TYPE`` carries a value,
     *  not an identity (see gArray.h for the whole argument) — so the array
     *  is **bound from the creation argument**, on the control thread:
     *  ``.array.at <name> [<index>]`` holds one ``shared_ptr<arrayStore>``
     *  resolved in ``SetParent`` / ``PARM_PARSE`` / ``RefreshBinding``,
     *  exactly as ``gArray`` and the ``dict.*`` family resolve theirs.
     *
     *  ### What arrives, and what leaves
     *
     *  Two inlets — one for the index and a second for the reference, the
     *  shape the family's acknowledging inlets established (``.dict.slice``):
     *
     *  - **An int on the index inlet** stores the index and fetches: the
     *    element at that position leaves the element outlet, typed the way
     *    the patcher spells it (``SendAtom`` — an int, a float or a symbol
     *    by its spelling). A position the array does not have bangs the miss
     *    outlet instead — ``.array``'s own miss rule, kept so a patch can
     *    tell "no such element" from an element it received. A negative
     *    index is refused and counted, never wrapped or clamped —
     *    ``arrayStore``'s indexing rule, decided once for the family.
     *  - **A float truncates to an int** — Max's float method on an int
     *    attribute, and ``.table``'s precedent — then behaves as the int
     *    would. A negative or non-finite float is refused.
     *  - **A bang re-fetches at the stored index** — the index the last int
     *    or float stored, seeded by the second creation argument (0 when
     *    absent). The index moves even when the fetch missed: a miss is a
     *    property of the array at that moment, not of the request, so a
     *    bang after the array has grown finds the element the earlier fetch
     *    could not.
     *  - **A list of indices is honoured** — Max's ``array.at`` takes
     *    several — and answered **whole**: one list of the named elements,
     *    in the order asked, out the element outlet. Any index out of range
     *    is one bang out the miss outlet and no partial list — a reply
     *    shorter than the request would misalign every position after the
     *    miss, which is truncation by another name. A list with anything in
     *    it that is not a non-negative integer is refused and counted, and
     *    a list never moves the stored index: it is a compound fetch
     *    answered at the moment it arrives, not a cursor move.
     *  - **``array <name>`` on the index inlet fetches at the stored
     *    index** when it names the array already bound — the message a
     *    ``.array``'s reference outlet emits on a bang, so wiring that
     *    outlet here gives the family's gesture: bang the array, out comes
     *    the current element. Honoured only via ``ArrayReferenceNames``'
     *    bounded compare and refused otherwise, because resolving an
     *    unrecognised name means the registry's mutex on whatever thread
     *    the message arrived on.
     *  - **``array <name>`` on the reference inlet is acknowledged
     *    silently** when it names the bound array, so a patch may wire the
     *    array's reference outlet across and the wiring stays readable. It
     *    sets nothing — the binding is the creation argument — and anything
     *    else there is refused and counted. Re-pointing at a different name
     *    from a message is not portable, for the reason gArray.h gives for
     *    not porting ``refer``.
     *
     *  ### A fetch is atomic — the concurrent-write decision #782 asks for
     *
     *  ``insert`` and ``delete`` renumber, so an operation that walks an
     *  array while another object writes to it must say what a mid-walk
     *  write does. Here the answer is that there is no mid-walk to be in: a
     *  fetch — one index or a list of them — reads every position under
     *  **one** hold of the store's guard, into a list the object
     *  pre-allocated at construction, and sends only after releasing it. A
     *  write from another thread loses the try-lock while the fetch holds
     *  it (dropped and counted by the writer, the store's rule); a write
     *  from the fetch's own downstream subgraph happens after the guard is
     *  released and changes what the *next* fetch sees, never the reply in
     *  flight. Nothing about a fetch lives in the shared store — the stored
     *  index is this object's own, ``.coll``'s per-object pointer rule — so
     *  two ``.array.at`` on one name fetch independently, and a renumbering
     *  write between two fetches simply moves what the unchanged index
     *  names, which is what a position means.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlet,
     *  the rule ``.value``, ``.coll``, ``.dict`` and ``.array`` establish.
     *  No message path allocates, locks or blocks: the name is resolved on
     *  the control thread, the index list and the emit list are fixed
     *  storage reserved at construction, and every send happens after the
     *  store's guard is released.
     */
    PATCHER_CLASS(gArrayAt, YSE::OBJ::G_ARRAY_AT)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief Most indices one list fetch honours — ``AtomList::MAX_ATOMS``,
     *         the patcher's bound on how many atoms travel down a cord. */
    static constexpr std::size_t MAX_INDICES = AtomList::MAX_ATOMS;

    /** @brief The array's shared name — the first creation argument, or
     *         empty for a private (empty) array. */
    const std::string& ArrayName() const {
      return arrayName;
    }

    /** @brief The address the store is registered under —
     *         ``"<patcherName>.<name>"`` — or empty while it is private. */
    const std::string& Address() const {
      return boundAddress;
    }

    /** @brief The stored index a bang fetches at — the last int or float
     *         received, seeded by the second creation argument. */
    int Index() const {
      return index.load(std::memory_order_relaxed);
    }

    /**
     *  @brief Messages refused so far — a negative or malformed index, a
     *         reference naming an array this object is not bound to, an
     *         unrecognised message, a reply past the emit list's capacity,
     *         or a lost try-lock.
     *
     *  A counter rather than a log line because the refusing thread may be
     *  the audio callback; ``gArray::Dropped``, for ``gArray``'s reason.
     */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    // Bind the store the moment the patcher name is known, so a message
    // never resolves a name. gArray::SetParent's rule.
    void SetParent(pObject* parent) override;

    // Re-bind after a patcher rename: the address prefix moved, so the
    // object now reads a different array. Called from
    // patcherImplementation::SetName alongside gArray::RefreshBinding.
    void RefreshBinding();

  private:
    // Point the store at the current name and parent address. Control
    // thread only (SetParams / SetParent / SetName). A no-op when the
    // address has not changed.
    void Rebind();

    // The fetch itself: read every position under one hold of the store's
    // guard into the emit list, release, then send — the whole reply out
    // the element outlet, or one bang out the miss outlet when any
    // position is not there. See the class notes on why this is atomic.
    void Fetch(const std::size_t* positions, std::size_t count, YSE::THREAD thread);

    // A fetch at the stored index — what a bang and the reference gesture
    // both come down to. Refuses (counted) a negative stored index, which
    // only a creation argument can plant.
    void FetchAtIndex(YSE::THREAD thread);

    void Refuse() {
      dropped.fetch_add(1, std::memory_order_relaxed);
    }

    // The shared name. The first creation argument; empty means a private,
    // empty array.
    std::string arrayName;

    // The stored index — the position a bang fetches. Atomic because an
    // int may arrive on any thread while the control thread re-parses the
    // creation arguments; never a lock.
    std::atomic<int> index{0};

    // The address the store is registered under, or empty while it is
    // private. Also the "has the binding changed?" key Rebind() compares
    // against.
    std::string boundAddress;

    // The array. Never null once the object has been constructed, so no
    // message handler needs a null check.
    std::shared_ptr<arrayStore> store;

    // The positions a list fetch asked for, parsed before the guard is
    // taken. Fixed storage — the price of never allocating on a message
    // path.
    std::size_t requested[MAX_INDICES] = {};

    // The reply, collected under the guard and sent after it is released.
    // An AtomList rather than a string because it carries the patcher's
    // own bound on how much list text may travel down a cord — a reply
    // that outruns it is refused whole rather than the send allocating.
    AtomList emitList;

    // Render buffer for the outlet, reserved to AtomList::RENDER_CAPACITY
    // at construction.
    std::string emitScratch;

    // Refusals, published for tests and diagnostics.
    std::atomic<std::uint64_t> dropped{0};
  };
} // namespace PATCHER
} // namespace YSE
