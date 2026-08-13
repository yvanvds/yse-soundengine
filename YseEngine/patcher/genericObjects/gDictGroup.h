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
     *  @brief Group a dictionary's entries by a value — Max's ``dict.group``
     *         on the name-addressed value model ``.dict`` settled (issue
     *         #772).
     *
     *  Turning a flat table into an index: grouping voices by their
     *  instrument, notes by their channel, presets by their category. The
     *  output is a dictionary of dictionaries, which the flat store expresses
     *  as a path prefix — every grouped entry is written as
     *  ``<groupValue>::<originalPath>``.
     *
     *  A dictionary never travels down a cord — an ``OUT_TYPE`` carries a
     *  value, not an identity (see gDict.h for the whole argument) — so both
     *  dictionaries are **bound from the creation arguments**, on the control
     *  thread: ``.dict.group <source> <target> [<key>]`` holds two
     *  ``shared_ptr<dictStore>`` resolved in ``SetParent`` / ``PARM_PARSE`` /
     *  ``RefreshBinding``, exactly as ``gDict`` and ``gDictCompare`` resolve
     *  theirs. The result goes into the *target* dictionary rather than into
     *  a new anonymous one, because there is no way to hand a fresh
     *  dictionary's identity down a cord.
     *
     *  ### What the grouping value is
     *
     *  - **With a ``<key>`` argument**, the source is read as a table of
     *    records — the first path segment names the record, everything below
     *    it is the record's fields — and each entry's group is the value its
     *    record stores under that key: grouping by ``instrument`` reads
     *    ``<record>::instrument`` and files every ``<record>::…`` entry under
     *    what it holds. An entry whose record holds no ``<key>`` entry has no
     *    group; it is skipped and counted rather than invented.
     *  - **Without one**, each entry's group is its **own value** — the flat
     *    ``voice = instrument`` table becomes the ``instrument::voice``
     *    index directly. The value text is used verbatim, so a multi-token
     *    value groups under its whole spelling.
     *
     *  ### What arrives, and what leaves
     *
     *  - **A bang on inlet 0 groups**: the source is read, the target is
     *    **replaced** with the grouped entries, and the target's reference
     *    ``dictionary <target>`` leaves the outlet so the rest of the
     *    ``dict.*`` family can pick the result up. Silent for an unnamed
     *    target, which has no name to pass on.
     *  - **``dictionary <source>`` on inlet 0 also groups** — the message a
     *    ``.dict``'s reference outlet emits on a bang, so wiring the source
     *    dictionary's reference outlet here gives Max's own gesture. Honoured
     *    only when it names the dictionary already bound —
     *    ``DictReferenceNames``, the bounded compare — and refused and
     *    counted otherwise, because resolving an unrecognised name means the
     *    registry's mutex on whatever thread the message arrived on.
     *  - **``dictionary <target>`` on inlet 1 is acknowledged and nothing
     *    more.** The binding is the second creation argument, so the
     *    reference is accepted silently when it names it — a patch may wire
     *    the target's reference outlet across — and refused and counted when
     *    it names anything else. Re-pointing at a different name from a
     *    message is not portable, for the reason gDict.h gives for not
     *    porting ``refer``.
     *
     *  ### Refusal, never truncation
     *
     *  Prepending the group value can overflow ``KEY_CAPACITY`` where the
     *  input did not. A composed path past the capacity, an entry without a
     *  group, an empty group value (an empty path segment is not a path this
     *  store can express), and a store into a full table are each refused
     *  whole and counted — never truncated. The rest of the grouping still
     *  lands, ``.coll``'s rule for an over-long record.
     *
     *  ### How the grouping avoids holding two guards
     *
     *  Both stores are guarded by ``.value``-style try-locks, and taking both
     *  at once would put a lock-ordering obligation on every pair of objects
     *  that name the same two dictionaries. So the grouping never holds two:
     *  it copies the source dictionary out under its guard into a snapshot
     *  the object pre-allocated at construction, releases, then resolves
     *  every group against the snapshot and writes the result under the
     *  target's guard alone — bounded ``assign``s into pre-reserved rows, no
     *  allocation. The degenerate case of both names binding one store groups
     *  it in place without ever taking its guard twice. A guard another
     *  thread holds refuses the whole grouping, counted, rather than
     *  waiting.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — the object is driven by its inlets, the
     *  rule ``.value``, ``.coll`` and ``.dict`` establish. No message path
     *  allocates, locks or blocks: the names are resolved on the control
     *  thread, the snapshot rows and the key scratch buffers are reserved at
     *  construction, and the reference is built once per rebind so the send
     *  is of a string the object already owns.
     */
    PATCHER_CLASS(gDictGroup, YSE::OBJ::G_DICT_GROUP)
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

    /** @brief The target dictionary's shared name — the second creation
     *         argument. */
    const std::string& TargetName() const {
      return targetName;
    }

    /** @brief The key each record's group is read from — the third creation
     *         argument, or empty to group each entry by its own value. */
    const std::string& GroupKey() const {
      return groupKey;
    }

    /** @brief The address the source store is registered under —
     *         ``"<patcherName>.<name>"`` — or empty while it is private. */
    const std::string& SourceAddress() const {
      return boundSourceAddress;
    }

    /** @brief The address the target store is registered under, or empty. */
    const std::string& TargetAddress() const {
      return boundTargetAddress;
    }

    /**
     *  @brief Messages and entries refused so far — an unrecognised message,
     *         a reference naming a dictionary this object is not bound to, a
     *         lost try-lock, an entry without a group, or a composed path
     *         past ``KEY_CAPACITY``.
     *
     *  A counter rather than a log line because the refusing thread may be
     *  the audio callback; ``gDict::Dropped``, for ``gDict``'s reason.
     */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    // Bind both stores the moment the patcher name is known, so a message
    // never resolves a name. gDict::SetParent's rule.
    void SetParent(pObject* parent) override;

    // Re-bind after a patcher rename: the address prefix moved, so the object
    // now groups different dictionaries. Called from
    // patcherImplementation::SetName alongside gDict::RefreshBinding.
    void RefreshBinding();

  private:
    // Point both stores at the current names and parent address. Control
    // thread only (SetParams / SetParent / SetName). A no-op per side when
    // that side's address has not changed.
    void Rebind();

    // Rebuild `reference` from the current target name. Control thread only.
    void RefreshReference();

    // The grouping itself: snapshot the source store under its guard, resolve
    // every group against the snapshot, replace the target under that guard
    // alone, send the target's reference after both are released. Refuses
    // (counted) on a lost guard; skips (counted) entries without a group or
    // with a composed path past KEY_CAPACITY.
    void Group(YSE::THREAD thread);

    void Refuse() {
      dropped.fetch_add(1, std::memory_order_relaxed);
    }

    // The shared names. First and second creation arguments; an empty name
    // means that side is a private, empty dictionary.
    std::string sourceName;
    std::string targetName;

    // The key each record's group is read from — "<firstSegment>::<key>" in
    // the source. Empty groups each entry by its own value. Third creation
    // argument.
    std::string groupKey;

    // The addresses the stores are registered under, or empty while private.
    // Also the "has the binding changed?" keys Rebind() compares against.
    std::string boundSourceAddress;
    std::string boundTargetAddress;

    // The two dictionaries. Never null once the object has been constructed,
    // so no message handler needs a null check.
    std::shared_ptr<dictStore> sourceStore;
    std::shared_ptr<dictStore> targetStore;

    // Where the source dictionary is copied to while its guard is held, so
    // the target store's guard is never nested inside it. Allocated whole by
    // dictStore's own constructor, on the control thread, once.
    dictStore snapshot;

    // "dictionary <target>", built once per rebind so the send after a
    // grouping is of a string the object already owns rather than a
    // concatenation on whichever thread the trigger arrived on.
    std::string reference;

    // Scratch for "<firstSegment>::<key>" while resolving a record's group,
    // and for "<groupValue>::<originalPath>" while writing one. Plain arrays
    // rather than strings because they are written on the message path, where
    // an assign that had to grow would be the one thing that must not happen.
    char lookup[dictStore::KEY_CAPACITY + 1] = {};
    char composed[dictStore::KEY_CAPACITY + 1] = {};

    // Refusals, published for tests and diagnostics.
    std::atomic<std::uint64_t> dropped{0};
  };
} // namespace PATCHER
} // namespace YSE
