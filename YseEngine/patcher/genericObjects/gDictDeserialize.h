#pragma once
#include "../pObject.h"
#include "dictParser.h"
#include "gDict.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Build a dictionary from serialised text — Max's
     *         ``dict.deserialize`` on the name-addressed value model ``.dict``
     *         settled (issue #771).
     *
     *  The read half of the interchange pair whose write half is
     *  ``.dict.serialize`` (#778): a JSON string arriving from a host, a
     *  ``.textedit``, a ``.s``/``.r`` pair or the live-coding DSL becomes a
     *  dictionary the patch can address — what makes the dictionary a
     *  transport format rather than only a store.
     *
     *  ### What arrives, and what leaves
     *
     *  A dictionary never travels down a cord — an ``OUT_TYPE`` carries a
     *  value, not an identity (see gDict.h for the whole argument) — so the
     *  target dictionary is **bound from the creation argument**, on the
     *  control thread: ``.dict.deserialize <name>`` holds one
     *  ``shared_ptr<dictStore>`` resolved in ``SetParent`` / ``PARM_PARSE``
     *  / ``RefreshBinding``, exactly as the rest of the family resolves
     *  theirs.
     *
     *  - **A list message is the document.** The inlet takes exactly what
     *    ``.dict.serialize`` emits — one single-line compact JSON object —
     *    and anything else JSON calls an object; ``DictFromJson``, the one
     *    flattener the family shares, turns its nesting back into ``::``
     *    paths, so a serialise → deserialise round trip reproduces the
     *    dictionary. A parsed document **replaces the bound dictionary
     *    whole** (``DictFromJson``'s contract), then the reference
     *    ``dictionary <name>`` leaves the outlet so the rest of the family
     *    can pick the result up — ``.dict.pack``'s gesture. Silent for an
     *    unnamed dictionary, which has no name to pass on; the contents
     *    still land in the private store.
     *  - **There is no bang and no bare-number handler**: a bang carries no
     *    document, and a number names nothing — the family's rule, applied
     *    to an object whose only trigger *is* its payload.
     *
     *  ### The parse is on the background pool — the thread-placement
     *      decision #771 asks for
     *
     *  ``nlohmann::json::parse`` allocates without bound and throws, and a
     *  message handler runs on whichever thread dispatched it — routinely
     *  the audio callback. So the handler does not parse: it hands the
     *  document to ``dictParser`` (this issue's sibling of ``fileScheduler``
     *  #683 and ``clockBridge`` #688) with one CAS, one bounded ``memcpy``
     *  and one lock-free push, and the parse itself — nlohmann, then
     *  ``DictFromJson`` into a staging store the parser owns — runs on the
     *  background pool.
     *
     *  The result has no cord to arrive on, so the object collects it the
     *  way ``.midiinfo`` collects a rescan (#757): ``WantsBlockPoll()`` puts
     *  ``Calculate()`` at the top of every patcher block, beside the value,
     *  deferred-message and file-completion drains, and a poll that finds a
     *  finished parse installs it — bounded ``assign``s into the bound
     *  store's pre-reserved rows, under its guard, released before the send
     *  — and emits the reference. A poll that finds nothing is one atomic
     *  load, which is what it costs to have this object in a patch at all.
     *  Like every other deferred path here, a result lands only while the
     *  patcher renders; a standalone object in a test pumps ``Calculate()``
     *  by hand.
     *
     *  An install that loses the store's try-lock simply leaves the result
     *  in the slot and retries next block — nothing is lost, which is what
     *  distinguishes this consumer from the refuse-and-count message paths:
     *  here there is a later block to try again in.
     *
     *  ### Refusal, failure, and what each costs
     *
     *  - **Refused and counted** (``Dropped``): a document longer than
     *    ``DOCUMENT_CAPACITY`` characters — the longest list payload the
     *    patcher's value queue carries, the same bound ``.dict.serialize``
     *    enforces from the write side, refused whole rather than truncated
     *    because a prefix of a JSON document is a different document or none
     *    — a document arriving while the previous one is still being parsed
     *    or waiting to be installed, and any document at all when the
     *    process-wide parse table had no slot left for this object.
     *  - **Failed and counted** (``Failed``): a document the pool could not
     *    parse to a JSON object — malformed text, a bare number, an array, a
     *    ``dictionary <name>`` reference wired here by mistake. The bound
     *    dictionary is untouched: a bad document never costs a dictionary
     *    its contents, and nothing leaves the outlet.
     *  - **Counted on success** (``Parsed``): installed and announced.
     *
     *  Counters rather than log lines, because the refusing thread may be
     *  the audio callback — the family's rule.
     *
     *  ### Real-time behaviour
     *
     *  No message path and no block poll allocates, locks or blocks. The
     *  submit is a CAS, a bounded copy and a lock-free push; the install is
     *  bounded ``assign``s into rows reserved at construction, under the
     *  store's try-lock guard, released before the send; the reference is
     *  built once per rebind, so the send is of a string the object already
     *  owns. The one place nlohmann runs is the background pool, where
     *  allocation is the job description.
     *
     *  ### What persists
     *
     *  The creation argument, because it is a creation argument. The
     *  dictionary's contents persist with the ``.dict`` that owns them.
     */
    PATCHER_CLASS(gDictDeserialize, YSE::OBJ::G_DICT_DESERIALIZE)
    _NO_MESSAGES
    _DO_CALCULATE

    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    ~gDictDeserialize() override;

    /** @brief Most entries a parsed document installs —
     *         ``dictStore::MAX_ENTRIES``. Entries past it, and entries whose
     *         path or value outgrows the store's capacities, are dropped by
     *         ``DictFromJson`` while the rest of the document still loads —
     *         ``.coll``'s rule for an over-long record. */
    static constexpr std::size_t MAX_ENTRIES = dictStore::MAX_ENTRIES;

    /**
     *  @brief Longest document the inlet accepts, in characters — 255,
     *         ``patcherImplementation::kValueListCap - 1``, the longest list
     *         payload the patcher's value queue carries inline.
     *
     *  The same bound ``.dict.serialize`` enforces from the write side, and
     *  for the mirrored reason: a longer document could arrive over a direct
     *  cord but never over a ``.s``/``.r`` or from the host, and a payload
     *  the transport would have cut is refused whole rather than parsed as
     *  the different document its prefix spells. A larger document wants the
     *  file route (a ``read <file>`` path, tracked as follow-up work), not a
     *  cord.
     */
    static constexpr std::size_t DOCUMENT_CAPACITY = 255;

    /** @brief The target dictionary's shared name — the creation argument,
     *         or empty for a private dictionary. */
    const std::string& DictName() const {
      return dictName;
    }

    /** @brief The address the store is registered under —
     *         ``"<patcherName>.<name>"`` — or empty while it is private. */
    const std::string& Address() const {
      return boundAddress;
    }

    /** @brief The message a parsed document emits — ``"dictionary <name>"``,
     *         or empty for an unnamed dictionary, which has no name to pass
     *         on. */
    const std::string& Reference() const {
      return reference;
    }

    /**
     *  @brief Documents refused so far — over the payload bound, arriving
     *         while one is already in flight, or with no parse slot at all.
     *
     *  A counter rather than a log line because the refusing thread may be
     *  the audio callback; ``gDict::Dropped``, for ``gDict``'s reason.
     */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief Documents the pool could not parse to a JSON object. The
     *         bound dictionary is untouched by these. */
    std::uint64_t Failed() const {
      return failed.load(std::memory_order_relaxed);
    }

    /** @brief Documents parsed and installed. Diagnostics and tests. */
    std::uint64_t Parsed() const {
      return parsed.load(std::memory_order_relaxed);
    }

    /** @brief Yes — but for a background result rather than for a wire
     *         (issue #757's arrangement). A parse finishes on the pool and
     *         has no cord to arrive on, so the block poll is what installs
     *         it and emits the reference. See the class comment. */
    bool WantsBlockPoll() const override {
      return true;
    }

    // Bind the store the moment the patcher name is known, so a message
    // never resolves a name. gDict::SetParent's rule.
    void SetParent(pObject* parent) override;

    // Re-bind after a patcher rename: the address prefix moved, so the
    // object now fills a different dictionary. Called from
    // patcherImplementation::SetName alongside gDict::RefreshBinding.
    void RefreshBinding();

  private:
    // Point the store at the current name and parent address. Control
    // thread only (SetParams / SetParent / SetName). A no-op when the
    // address has not changed, so the contents survive a re-parse that
    // leaves the name alone.
    void Rebind();

    // Rebuild `reference` from the current name. Control thread only.
    void RefreshReference();

    void Refuse() {
      dropped.fetch_add(1, std::memory_order_relaxed);
    }

    // The shared name. The creation argument; empty means a private
    // dictionary.
    std::string dictName;

    // The address the store is registered under, or empty while it is
    // private. Also the "has the binding changed?" key Rebind() compares
    // against.
    std::string boundAddress;

    // The dictionary a parsed document replaces. Never null once the object
    // has been constructed, so no message handler needs a null check.
    std::shared_ptr<dictStore> store;

    // "dictionary <name>", built once per rebind so the announcement is a
    // send of a string the object already owns rather than a concatenation
    // on the polling thread.
    std::string reference;

    // This object's slot in the process-wide parse table, claimed at
    // construction and released at destruction. 0 when the table was full;
    // the object then refuses every document, counted, and the claim
    // failure is logged once from the constructor (control thread).
    dictParser::Handle slot = 0;

    std::atomic<std::uint64_t> dropped{0};
    std::atomic<std::uint64_t> failed{0};
    std::atomic<std::uint64_t> parsed{0};
  };
} // namespace PATCHER
} // namespace YSE
