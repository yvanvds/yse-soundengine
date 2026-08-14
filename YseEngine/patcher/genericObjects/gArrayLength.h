#pragma once
#include "../pObject.h"
#include "gArray.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Output an array's length — Max's ``array.length`` on the
     *         name-addressed value model ``.array`` settled (issue #783).
     *
     *  The number every ``.uzi``-driven walk over an array needs before it
     *  can start, and the one ``.zl len`` gives for a list: without it a
     *  patch cannot iterate an array whose length it did not set itself.
     *
     *  An array never travels down a cord — an ``OUT_TYPE`` carries a value,
     *  not an identity (see gArray.h for the whole argument) — so the array
     *  is **bound from the creation argument**, on the control thread:
     *  ``.array.length <name>`` holds one ``shared_ptr<arrayStore>``
     *  resolved in ``SetParent`` / ``PARM_PARSE`` / ``RefreshBinding``,
     *  exactly as ``gArray`` and ``gArrayAt`` resolve theirs.
     *
     *  ### What arrives, and what leaves
     *
     *  Two inlets — a trigger and a reference, ``gArrayAt``'s shape with the
     *  index dropped, because a length is asked for, never addressed:
     *
     *  - **A bang on the trigger inlet emits the length**: one int out the
     *    length outlet, ``store->count`` as it stood at the trigger. An
     *    empty array answers 0, and so does an unnamed (private) one — zero
     *    is a length, not a miss, which is why this object has no miss
     *    outlet where ``.array.at`` needs one: every array has a length,
     *    where not every index has an element.
     *  - **``array <name>`` on the trigger inlet emits the length** when it
     *    names the array already bound — the message an ``.array``'s
     *    reference outlet emits on a bang, so wiring that outlet here gives
     *    the family's gesture: bang the array, out comes its length.
     *    Honoured only via ``ArrayReferenceNames``' bounded compare and
     *    refused otherwise, because resolving an unrecognised name means
     *    the registry's mutex on whatever thread the message arrived on.
     *  - **``array <name>`` on the reference inlet is acknowledged
     *    silently** when it names the bound array, so a patch may wire the
     *    array's reference outlet across and the wiring stays readable. It
     *    sets nothing — the binding is the creation argument — and anything
     *    else there is refused and counted. Re-pointing at a different name
     *    from a message is not portable, for the reason gArray.h gives for
     *    not porting ``refer``.
     *
     *  ### Asked, never announced — the emission decision #783 asks for
     *
     *  The length leaves only when the trigger inlet asks for it, never on
     *  a write to the store — ``.value``'s rule that an object driven by
     *  its inlet does not emit on its own. A change notification would also
     *  mean the store knowing its readers, which nothing on the value model
     *  does: a store is storage, and every object on a name talks to the
     *  store, never to the other objects.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlet,
     *  the rule ``.value``, ``.coll``, ``.dict`` and ``.array`` establish.
     *  The whole object is one guarded read of ``store->count``: the
     *  trigger takes the store's guard, copies the count, releases, and
     *  sends only after the release — never across the hold, since a send
     *  runs the whole downstream graph, which may well write into this same
     *  array, and inside the guard that write would be the one thing the
     *  try-lock drops. A guard another thread holds refuses the read,
     *  counted, rather than waiting. No message path allocates, locks or
     *  blocks: the name is resolved on the control thread, and a length is
     *  one int.
     */
    PATCHER_CLASS(gArrayLength, YSE::OBJ::G_ARRAY_LENGTH)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

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

    /**
     *  @brief Messages refused so far — a reference naming an array this
     *         object is not bound to, an unrecognised message, or a lost
     *         try-lock.
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

    // The read itself: copy `store->count` under one hold of the store's
    // guard, release, then send the int — what a bang and the reference
    // gesture both come down to. A lost guard is a counted refusal.
    void EmitLength(YSE::THREAD thread);

    void Refuse() {
      dropped.fetch_add(1, std::memory_order_relaxed);
    }

    // The shared name. The first creation argument; empty means a private,
    // empty array.
    std::string arrayName;

    // The address the store is registered under, or empty while it is
    // private. Also the "has the binding changed?" key Rebind() compares
    // against.
    std::string boundAddress;

    // The array. Never null once the object has been constructed, so no
    // message handler needs a null check.
    std::shared_ptr<arrayStore> store;

    // Refusals, published for tests and diagnostics.
    std::atomic<std::uint64_t> dropped{0};
  };
} // namespace PATCHER
} // namespace YSE
