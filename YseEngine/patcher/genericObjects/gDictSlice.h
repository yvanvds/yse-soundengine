#pragma once
#include "../pObject.h"
#include "gDict.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Split a dictionary at a key path — Max's ``dict.slice`` on the
     *         name-addressed value model ``.dict`` settled (issue #779).
     *
     *  Taking a sub-tree out of a dictionary as a dictionary of its own:
     *  reading one voice out of a voice table, one section out of a preset.
     *  This is the operation the flat store makes explicit — a sub-tree is
     *  not a value (see the nesting note in gDict.h), so extracting one is a
     *  real operation, a bounded scan of every path that begins with the
     *  slice prefix, rather than a lookup. Both halves of Max's object are
     *  kept: the **slice** (everything under the path, with the prefix
     *  stripped) and the **remainder** (everything else, unchanged).
     *
     *  A dictionary never travels down a cord — an ``OUT_TYPE`` carries a
     *  value, not an identity (see gDict.h for the whole argument) — so all
     *  three dictionaries are **bound from the creation arguments**, on the
     *  control thread: ``.dict.slice <source> <slice> <remainder> [<path>]``
     *  holds three ``shared_ptr<dictStore>`` resolved in ``SetParent`` /
     *  ``PARM_PARSE`` / ``RefreshBinding``, exactly as ``gDict`` and
     *  ``gDictGroup`` resolve theirs. The results go into *bound* targets
     *  rather than into new anonymous dictionaries, for ``gDictGroup``'s
     *  reason: there is no way to hand a fresh dictionary's identity down a
     *  cord.
     *
     *  ### What the slice is
     *
     *  The path names a sub-tree, and the split is a clean partition — every
     *  source entry lands in exactly one half:
     *
     *  - **An entry under the path** — its key begins ``<path>::`` with at
     *    least one character after the separator — belongs to the slice. The
     *    prefix and its separator are stripped on the way in, so slicing
     *    ``voice::1`` out of ``voice::1::freq = 440`` stores ``freq = 440``:
     *    the sub-tree becomes a dictionary of its own, rooted at itself.
     *  - **Every other entry** belongs to the remainder, its key unchanged —
     *    including an entry stored *at* exactly the path, which is a leaf
     *    value, not a sub-tree, so it is not part of the slice. And a key
     *    that only *begins like* the path (``voicecard`` against ``voice``)
     *    is no match at all: the prefix ends at a ``::`` boundary or it is
     *    not a prefix.
     *  - **An empty path matches nothing** — there is no sub-tree above the
     *    root — so the slice comes out empty and the remainder is the whole
     *    source. The degenerate case falls out of the rule rather than being
     *    a special one.
     *
     *  Stripping never lengthens a key and the partition never grows the
     *  entry count, so a slice cannot overflow what the source already held —
     *  even the pathological key that *ends* at the separator
     *  (``voice::1::``) is simply not under the path, since there is nothing
     *  after the prefix to root a sub-tree entry at, and stays in the
     *  remainder unchanged.
     *
     *  ### What arrives, and what leaves
     *
     *  - **A bang on inlet 0 slices**: both targets are **replaced** with
     *    their halves, then each target's reference ``dictionary <name>``
     *    leaves its outlet — the remainder's (outlet 1) first, then the
     *    slice's (outlet 0), Max's universal right-to-left order. Silent per
     *    outlet for an unnamed target, which has no name to pass on.
     *  - **``dictionary <source>`` on inlet 0 also slices** — the message a
     *    ``.dict``'s reference outlet emits on a bang, so wiring the source
     *    dictionary's reference outlet here gives Max's own gesture. Honoured
     *    only when it names the dictionary already bound —
     *    ``DictReferenceNames``, the bounded compare — and refused and
     *    counted otherwise, because resolving an unrecognised name means the
     *    registry's mutex on whatever thread the message arrived on.
     *  - **``dictionary <slice>`` on inlet 1 and ``dictionary <remainder>``
     *    on inlet 2 are acknowledged and nothing more.** The bindings are
     *    creation arguments, so each reference is accepted silently when it
     *    names its own inlet's target — a patch may wire a target's
     *    reference outlet across — and refused and counted when it names
     *    anything else. Re-pointing at a different name from a message is
     *    not portable, for the reason gDict.h gives for not porting
     *    ``refer``.
     *
     *  ### How the split avoids holding two guards
     *
     *  All three stores are guarded by ``.value``-style try-locks, and taking
     *  two at once would put a lock-ordering obligation on every pair of
     *  objects that name the same two dictionaries. So the split never holds
     *  two: it copies the source out under its guard into a snapshot the
     *  object pre-allocated at construction, releases, then writes each half
     *  from the snapshot under that target's guard alone. That is also what
     *  makes the degenerate bindings safe — the source as its own slice or
     *  remainder replaces itself from the snapshot, and both targets on one
     *  name simply write it twice, the remainder (written second) winning. A
     *  guard another thread holds refuses the whole split, counted, rather
     *  than waiting — and a refusal anywhere sends nothing, so a reference
     *  never announces a half that was not written.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlets, the
     *  rule ``.value``, ``.coll`` and ``.dict`` establish. No message path
     *  allocates, locks or blocks: the names are resolved on the control
     *  thread, the snapshot rows are reserved at construction, stripping a
     *  prefix is pointer arithmetic into keys the snapshot already owns, and
     *  the references are built once per rebind so each send is of a string
     *  the object already owns.
     */
    PATCHER_CLASS(gDictSlice, YSE::OBJ::G_DICT_SLICE)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief The source dictionary's shared name — the first creation
     *         argument, or empty for a private (empty) source. */
    const std::string& SourceName() const {
      return sourceName;
    }

    /** @brief The slice target's shared name — the second creation
     *         argument. */
    const std::string& SliceName() const {
      return sliceName;
    }

    /** @brief The remainder target's shared name — the third creation
     *         argument. */
    const std::string& RemainderName() const {
      return remainderName;
    }

    /** @brief The key path the source is split at — the fourth creation
     *         argument, or empty to slice nothing out. */
    const std::string& SlicePath() const {
      return slicePath;
    }

    /** @brief The address the source store is registered under —
     *         ``"<patcherName>.<name>"`` — or empty while it is private. */
    const std::string& SourceAddress() const {
      return boundSourceAddress;
    }

    /** @brief The address the slice target is registered under, or empty. */
    const std::string& SliceAddress() const {
      return boundSliceAddress;
    }

    /** @brief The address the remainder target is registered under, or
     *         empty. */
    const std::string& RemainderAddress() const {
      return boundRemainderAddress;
    }

    /**
     *  @brief Messages refused so far — an unrecognised message, a reference
     *         naming a dictionary this object is not bound to, or a lost
     *         try-lock.
     *
     *  A counter rather than a log line because the refusing thread may be
     *  the audio callback; ``gDict::Dropped``, for ``gDict``'s reason.
     */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    // Bind all three stores the moment the patcher name is known, so a
    // message never resolves a name. gDict::SetParent's rule.
    void SetParent(pObject* parent) override;

    // Re-bind after a patcher rename: the address prefix moved, so the
    // object now splits different dictionaries. Called from
    // patcherImplementation::SetName alongside gDict::RefreshBinding.
    void RefreshBinding();

  private:
    // Point all three stores at the current names and parent address.
    // Control thread only (SetParams / SetParent / SetName). A no-op per
    // side when that side's address has not changed.
    void Rebind();

    // Rebuild both references from the current target names. Control thread
    // only.
    void RefreshReferences();

    // The split itself: snapshot the source store under its guard, write
    // each half from the snapshot under that target's guard alone, send the
    // references — remainder first, Max's right-to-left — after every guard
    // is released. A lost guard anywhere refuses the whole split (counted)
    // and sends nothing.
    void Slice(YSE::THREAD thread);

    // Whether one snapshot entry belongs to the slice: its key begins
    // "<path>::" with at least one character after the separator. Bounded
    // compares into strings the snapshot already owns — no allocation, so
    // it is safe on whichever thread the trigger arrived on.
    bool UnderPath(const dictEntry& entry) const;

    void Refuse() {
      dropped.fetch_add(1, std::memory_order_relaxed);
    }

    // The shared names. First three creation arguments; an empty name means
    // that side is a private, empty dictionary.
    std::string sourceName;
    std::string sliceName;
    std::string remainderName;

    // The key path the source is split at. Fourth creation argument; empty
    // slices nothing out.
    std::string slicePath;

    // The addresses the stores are registered under, or empty while private.
    // Also the "has the binding changed?" keys Rebind() compares against.
    std::string boundSourceAddress;
    std::string boundSliceAddress;
    std::string boundRemainderAddress;

    // The three dictionaries. Never null once the object has been
    // constructed, so no message handler needs a null check.
    std::shared_ptr<dictStore> sourceStore;
    std::shared_ptr<dictStore> sliceStore;
    std::shared_ptr<dictStore> remainderStore;

    // Where the source dictionary is copied to while its guard is held, so
    // no target's guard is ever nested inside it. Allocated whole by
    // dictStore's own constructor, on the control thread, once.
    dictStore snapshot;

    // "dictionary <slice>" / "dictionary <remainder>", built once per rebind
    // so the sends after a split are of strings the object already owns
    // rather than concatenations on whichever thread the trigger arrived on.
    std::string sliceReference;
    std::string remainderReference;

    // Refusals, published for tests and diagnostics.
    std::atomic<std::uint64_t> dropped{0};
  };
} // namespace PATCHER
} // namespace YSE
