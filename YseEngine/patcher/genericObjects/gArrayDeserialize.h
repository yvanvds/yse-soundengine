#pragma once
#include "../pObject.h"
#include "arrayParser.h"
#include "gArray.h"
#include "gArrayEnds.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Build an array from serialised text — Max's ``array.deserialize``
     *         on the name-addressed value model ``.array`` settled (issue
     *         #797).
     *
     *  The reader half of the JSON form ``.array`` already saves: a document
     *  arriving from a host, a ``.textedit``, a ``.s``/``.r`` pair or the
     *  live-coding DSL becomes an array the patch can address. The document is
     *  **a JSON array of typed elements** — ``[60,62.5,"kick"]`` — which is
     *  exactly what ``ArrayToJson`` spells into a saved patch's ``contents``,
     *  and it is read back by ``ArrayFromJson``, the type's one reader, so
     *  this object and ``gArray::RestoreState`` are the same document read the
     *  same way: booleans become ``1``/``0``, null and an empty string are
     *  skipped (neither is an atom), a nested array or object is skipped (an
     *  element is one atom, and there is no atom that spells a sub-document),
     *  and elements past ``ELEMENT_CAPACITY`` or past ``MAX_ELEMENTS`` are
     *  dropped while the rest of the document still loads — ``.coll``'s rule
     *  for an over-long record.
     *
     *  ### What arrives, and what leaves
     *
     *  An array never travels down a cord — an ``OUT_TYPE`` carries a value,
     *  not an identity (gArray.h has the whole argument) — so the target array
     *  is **bound from the creation argument**: ``.array.deserialize <name>``
     *  resolves the name once, on the control thread, in ``SetParent`` /
     *  ``PARM_PARSE`` / ``RefreshBinding``, exactly as the rest of the family
     *  (``gArrayEndsBase``, inherited whole).
     *
     *  - **A list message is the document.** A parsed document **replaces the
     *    bound array whole** (``ArrayFromJson``'s contract — ``[]`` is an
     *    emptied array, not a failure), then the reference ``array <name>``
     *    leaves the outlet so the rest of the family can pick the result up —
     *    the way an array leaves an object on the value model. Silent for an
     *    unnamed (private) array, which has no name to pass on; the contents
     *    still land in the private store.
     *  - **There is no bang and no bare-number handler**: a bang carries no
     *    document, and a bare number is a JSON document but not a JSON array —
     *    the family's rule, applied to an object whose only trigger *is* its
     *    payload. For the same reason there is no reference inlet: an
     *    ``array <name>`` message arriving here is a mis-wired cord, and it
     *    fails the ``is_array`` check like any other non-document (counted in
     *    ``Failed``), never a resolve of a name on a message path.
     *
     *  ### The parse is on the background pool — the thread decision #797
     *      asks for
     *
     *  ``nlohmann::json::parse`` allocates without bound, and a message
     *  handler runs on whichever thread dispatched it — in-patcher delivery
     *  dispatches on ``T_DSP``, and ``THREAD`` is a *dispatch-semantics* tag
     *  rather than a thread identity, so there is no predicate an object can
     *  ask to find out that it is **not** on the audio callback. That rules
     *  the issue's smaller answer out, not just down: "refuse on a non-control
     *  thread" has no honest implementation, and refusing everywhere would
     *  leave an object whose only trigger is its payload refusing its whole
     *  purpose. The pool is also the settled answer — ``.dict.deserialize``
     *  (#771) met the identical question and built ``dictParser`` for it — so
     *  the handler hands the document to ``arrayParser`` (one CAS, one bounded
     *  ``memcpy``, one lock-free push) and the parse itself — nlohmann, then
     *  ``ArrayFromJson`` into a staging store the parser owns — runs on the
     *  background pool.
     *
     *  The result has no cord to arrive on, so the object collects it the way
     *  ``.dict.deserialize`` does: ``WantsBlockPoll()`` puts ``Calculate()``
     *  at the top of every patcher block, and a poll that finds a finished
     *  parse installs it — bounded ``assign``s into the bound store's
     *  pre-reserved elements, under its guard, released before the send — and
     *  emits the reference. A poll that finds nothing is one atomic load. An
     *  install that loses the store's try-lock leaves the result in the slot
     *  and retries next block — nothing is lost; there is a later block to try
     *  again in.
     *
     *  ### Refusal, failure, and what each costs
     *
     *  - **Refused and counted** (``Dropped``): a document longer than
     *    ``DOCUMENT_CAPACITY`` characters — the longest list payload the
     *    patcher's value queue carries, refused whole rather than truncated
     *    because the prefix of a JSON document is a different document or none
     *    — a document arriving while the previous one is still in flight, and
     *    any document at all when the process-wide parse table had no slot
     *    left for this object. A larger document has no route here: the dict
     *    pair's file route (#840) has no array counterpart, because the write
     *    half it would pair with does not exist either — a bigger array's JSON
     *    travels with a saved patch, through the same ``ArrayFromJson``.
     *  - **Failed and counted** (``Failed``): a document the pool could not
     *    parse to a JSON array — malformed text, a bare number, an object, an
     *    ``array <name>`` reference wired here by mistake. The bound array is
     *    untouched: a bad document never costs an array its contents, and
     *    nothing leaves the outlet.
     *  - **Counted on success** (``Parsed``): installed and announced.
     *
     *  ### Real-time behaviour
     *
     *  No message path and no block poll allocates, locks or blocks. The
     *  submit is a CAS, a bounded copy and a lock-free push; the install is
     *  bounded ``assign``s into elements reserved at construction, under the
     *  store's try-lock guard, released before the send; the reference is
     *  built once per rebind, so the send is of a string the object already
     *  owns. The one place nlohmann runs is the background pool, where
     *  allocation is the job description.
     *
     *  ### What persists
     *
     *  The creation argument, because it is a creation argument. The array's
     *  contents persist with the ``.array`` that owns them.
     */
    class gArrayDeserialize : public gArrayEndsBase {
    public:
      gArrayDeserialize();
      ~gArrayDeserialize() override;
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_DESERIALIZE;
      }
      CREATE(gArrayDeserialize)

      /**
       *  @brief Longest document the inlet accepts, in characters — 255,
       *         ``patcherImplementation::kValueListCap - 1``, the longest list
       *         payload the patcher's value queue carries inline.
       *
       *  ``.dict.deserialize``'s bound, for its reason: a longer document
       *  could arrive over a direct cord but never over a ``.s``/``.r`` or
       *  from the host, and a payload the transport would have cut is refused
       *  whole rather than parsed as the different document its prefix
       *  spells.
       */
      static constexpr std::size_t DOCUMENT_CAPACITY = 255;

      /** @brief The message a parsed document emits — ``"array <name>"``, or
       *         empty for an unnamed array, which has no name to pass on. */
      const std::string& Reference() const {
        return reference;
      }

      /** @brief Documents the pool could not parse to a JSON array. The bound
       *         array is untouched by these, and nothing is announced. */
      std::uint64_t Failed() const {
        return failed.load(std::memory_order_relaxed);
      }

      /** @brief Documents parsed and installed. Diagnostics and tests. */
      std::uint64_t Parsed() const {
        return parsed.load(std::memory_order_relaxed);
      }

      /** @brief Yes — but for a background result rather than for a wire
       *         (#771's arrangement, inherited): a parse finishes on the pool
       *         and has no cord to arrive on, so the block poll is what
       *         installs it and emits the reference. */
      bool WantsBlockPoll() const override {
        return true;
      }

      // The block poll: collect a finished parse, install it under the
      // store's guard, announce the reference after releasing it. One atomic
      // load when nothing has landed.
      void Calculate(YSE::THREAD thread) override;

      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      // The one piece of state derived from the name: the reference message,
      // rebuilt whenever the creation arguments change so the announcement is
      // a send of a string the object already owns.
      void ParamsChanged() override;

    private:
      // Rebuild `reference` from the current name. Control thread only.
      void RefreshReference();

      // "array <name>", built once per rebind rather than concatenated on the
      // polling thread.
      std::string reference;

      // This object's slot in the process-wide parse table, claimed at
      // construction and released at destruction. 0 when the table was full;
      // the object then refuses every document, counted, and the claim
      // failure is logged once from the constructor (control thread).
      arrayParser::Handle slot = 0;

      std::atomic<std::uint64_t> failed{0};
      std::atomic<std::uint64_t> parsed{0};
    };

  } // namespace PATCHER
} // namespace YSE
