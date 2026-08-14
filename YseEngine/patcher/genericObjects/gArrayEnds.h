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
     *  @brief Shared body for the four end-mutators of the ``array.*`` family —
     *         ``.array.push``, ``.array.pop``, ``.array.shift`` and
     *         ``.array.unshift`` (issue #784).
     *
     *  The stack and queue operations: the four that turn a shared array into
     *  the buffer a generative patch actually pushes events onto and pops them
     *  off. ``push``/``pop`` at the back and ``unshift``/``shift`` at the
     *  front, so back-to-back is a stack and back-to-front is a queue. Each is
     *  a thin wrapper over ``ArrayAppend`` / ``ArrayInsertAt(0, ...)`` /
     *  ``ArrayEraseAt(count - 1)`` / ``ArrayEraseAt(0)``, which is why the
     *  four are one file: everything but the end they act on is common, and it
     *  lives here — the arrangement ``gThreshBase`` established for
     *  ``.thresh`` / ``.quickthresh``.
     *
     *  An array never travels down a cord — an ``OUT_TYPE`` carries a value,
     *  not an identity (see gArray.h for the whole argument) — so the array is
     *  **bound from the creation argument**, on the control thread:
     *  ``.array.push <name>`` holds one ``shared_ptr<arrayStore>`` resolved in
     *  ``SetParent`` / ``PARM_PARSE`` / ``RefreshBinding``, exactly as
     *  ``gArray``, ``gArrayAt`` and ``gArrayLength`` resolve theirs, and
     *  ``patcherImplementation::SetName`` re-anchors it on a rename. An
     *  ``array <name>`` message is honoured only when it names the array
     *  already bound — ``ArrayReferenceNames``' bounded compare — and refused
     *  otherwise, because resolving an unrecognised name means the registry's
     *  mutex on whatever thread the message arrived on. An unnamed object acts
     *  on a private, empty array of its own, ``gArray``'s rule.
     *
     *  ### Every operation is one guard hold — the concurrent-write answer
     *
     *  #784 inherits #548's warning that ``insert`` and ``delete`` renumber,
     *  and asks what a write arriving mid-walk does. The answer here is
     *  ``.array.at``'s: **there is no walk to be in the middle of**. A push —
     *  one element or a whole list of them — is validated before the store's
     *  guard is taken and applied entirely under one hold of it; a pop reads
     *  the departing element and closes the gap under one hold. A writer on
     *  another thread loses the try-lock while the operation holds it (dropped
     *  and counted, the store's rule), and every send happens after the guard
     *  is released, so an operation triggered by the send changes what the
     *  *next* trigger sees, never the one in flight. No cursor, no partial
     *  state, nothing of an operation lives in the shared store.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing — every object here is driven by its
     *  inlet, the rule ``.value``, ``.coll``, ``.dict`` and ``.array``
     *  establish. No message path allocates, locks or blocks: the name is
     *  resolved on the control thread, an arriving number is rendered by
     *  ``ExprFormatValue`` into a fixed ``AtomList``, a departing element is
     *  copied into a fixed buffer under the guard and sent after it, and a
     *  writer's reference is built once per rebind. Refusals are counted
     *  (``Dropped()``), never logged — formatting a log line builds a string
     *  on whichever thread the message arrived on, which is routinely the
     *  audio callback.
     */
    class gArrayEndsBase : public pObject {
    public:
      void SetMessage(const std::string&, float) override {}
      void Calculate(YSE::THREAD) override {}

      /** @brief Most elements the bound array holds —
       *         ``arrayStore::MAX_ELEMENTS``. */
      static constexpr std::size_t MAX_ELEMENTS = arrayStore::MAX_ELEMENTS;

      /** @brief Longest element, in characters —
       *         ``arrayStore::ELEMENT_CAPACITY``. */
      static constexpr std::size_t ELEMENT_CAPACITY = arrayStore::ELEMENT_CAPACITY;

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
       *  @brief Messages refused so far — an operation the array could not
       *         take whole, a reference naming an array this object is not
       *         bound to, an unrecognised message, or a lost try-lock.
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
      // object now acts on a different array. Called from
      // patcherImplementation::SetName alongside gArray::RefreshBinding.
      void RefreshBinding();

    protected:
      gArrayEndsBase();

      // The parameter hooks all four register: a re-parse must not leave half
      // of the previous configuration standing. gArray's rule. The clear
      // half is virtual so a subclass that registers parameters of its own
      // (gArrayPositionBase's stored position, #785) extends the reset
      // rather than shadowing it — the constructor's REG_PARM_CLEAR binding
      // dispatches to the most-derived override. Control thread only, so the
      // virtual call costs nothing that matters.
      virtual void ClearParams();
      void ParseParams();

      // Called after ClearParams / ParseParams have re-read the name, for
      // state derived from it — the writers rebuild their reference message
      // here. The base derives nothing.
      virtual void ParamsChanged() {}

      // Point the store at the current name and parent address. Control
      // thread only (SetParams / SetParent / SetName). A no-op when the
      // address has not changed.
      void Rebind();

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

    /**
     *  @brief The adding half — ``.array.push`` appends at the end,
     *         ``.array.unshift`` inserts at the front (issue #784).
     *
     *  Two inlets, one outlet:
     *
     *  - **An int or a float on the element inlet is added** as the text that
     *    spells it — ``ExprFormatValue``'s spelling, so ``7.5`` stays visibly
     *    a float and comes back out as one. A non-finite float is refused:
     *    it has no spelling that reads back, and storing the ``0.`` it
     *    formats as would be silent corruption.
     *  - **A list is added whole, in the order sent** — every atom one
     *    element, ``unshift a b c`` leaving ``a b c`` at the front — **or
     *    refused whole**, one counted refusal and nothing changed, when the
     *    array cannot take all of it or any element outruns
     *    ``ELEMENT_CAPACITY``. The mutating counterpart of ``.array.at``'s
     *    whole-reply rule, and a deliberate departure from ``.array``'s own
     *    ``append``, which keeps what fits: a list sent as one thing landing
     *    as a fragment of itself is truncation by another name, and at the
     *    *front* it would leave the array beginning with a prefix the patch
     *    never sent.
     *  - **``array <name>`` on the element inlet is refused**, even naming
     *    the bound array: a reference is an identity, not an element, so a
     *    reference cord mis-wired into the element inlet shows up in
     *    ``Dropped()`` instead of pushing the words ``array`` and ``<name>``
     *    into the data. (Only the bound name can be recognised at all —
     *    the bounded compare is the whole of what a message path may do with
     *    a name — so any *other* list starting with ``array`` is simply
     *    atoms.)
     *  - **``array <name>`` on the reference inlet is acknowledged
     *    silently** when it names the bound array — ``gDictSlice``'s shape,
     *    kept so a patch may wire the array's reference outlet across. It
     *    sets nothing, and anything else there is refused and counted.
     *  - **The outlet emits the bound array's reference, ``array <name>``,
     *    after an add that landed** — Max's ``array.push`` outputs the
     *    modified array, and the reference is how an array leaves an object
     *    on the value model: wiring it onward chains the family, so push
     *    into ``.array.length`` reports the new depth and push into
     *    ``.array.pop``'s trigger is a stack exercised by one cord. A
     *    refused add emits nothing, and an unnamed object stays silent — the
     *    write happens, but there is no name to pass on, ``gArray``'s bang
     *    rule.
     */
    class gArrayEndsWriter : public gArrayEndsBase {
    public:
      /** @brief The message the outlet emits after an add that landed —
       *         ``"array <name>"``, or empty for an unnamed object. */
      const std::string& Reference() const {
        return reference;
      }

      void IntIn(int value, int inlet, YSE::THREAD thread);
      void FloatIn(float value, int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      // `front` is the whole difference between .array.unshift and
      // .array.push.
      explicit gArrayEndsWriter(bool front);

      void ParamsChanged() override;

    private:
      // Rebuild `reference` from the current name. Control thread only.
      void RefreshReference();

      // The add itself: validate `pending` outside the guard, apply it whole
      // under one hold, release, then emit the reference. See the class notes
      // on whole-or-nothing.
      void Apply(YSE::THREAD thread);

      const bool atFront;

      // The elements one message adds, parsed (and, for a number, rendered)
      // before the guard is taken. Fixed storage — the price of never
      // allocating on a message path.
      AtomList pending;

      // "array <name>", built once per rebind so a successful add is a send
      // of a string the object already owns rather than a concatenation on
      // whichever thread the message arrived on.
      std::string reference;
    };

    /**
     *  @brief The removing half — ``.array.pop`` takes from the end,
     *         ``.array.shift`` takes from the front (issue #784).
     *
     *  Two inlets, two outlets:
     *
     *  - **A bang on the trigger inlet removes the element and emits it** out
     *    the element outlet, typed the way the patcher spells it
     *    (``SendAtom`` — an int, a float or a symbol by its spelling). The
     *    read and the erase are one hold of the store's guard, so the element
     *    that leaves is exactly the one that left the array. Emitting the
     *    departing element is the whole difference from ``.array``'s own
     *    ``delete``, which discards silently.
     *  - **An empty array bangs the empty outlet instead** — the state a
     *    patch draining a queue must be able to see, and a bang rather than a
     *    miss-style silence because "nothing left" is the loop's exit
     *    condition, not an error. An unnamed (private) array is always
     *    empty. A lost try-lock is neither: the array's state is unknown, so
     *    it is a counted refusal and no outlet fires.
     *  - **``array <name>`` on the trigger inlet removes** when it names the
     *    array already bound — the message an ``.array``'s reference outlet
     *    emits on a bang, so wiring that outlet here gives the family's
     *    gesture: bang the array, out comes an element. Refused otherwise,
     *    for the binding rule on ``gArrayEndsBase``.
     *  - **``array <name>`` on the reference inlet is acknowledged
     *    silently** when it names the bound array, and anything else there is
     *    refused and counted — ``gDictSlice``'s shape, exactly as on the
     *    writers.
     *
     *  A removal is asked for with a bang, never addressed, so there is no
     *  int or float method anywhere — ``.array.at`` is the object that takes
     *  a position.
     */
    class gArrayEndsRemover : public gArrayEndsBase {
    public:
      void BangIn(int inlet, YSE::THREAD thread);
      void ListIn(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      // `front` is the whole difference between .array.shift and
      // .array.pop.
      explicit gArrayEndsRemover(bool front);

    private:
      // The removal itself: copy the departing element and close the gap
      // under one hold of the store's guard, release, then send — the element
      // out the element outlet, or a bang out the empty outlet when there was
      // nothing to remove.
      void Remove(YSE::THREAD thread);

      const bool atFront;

      // Where the departing element is copied while the guard is held, so the
      // send can happen after it is released. A plain array rather than a
      // string because it is written from inside the critical section —
      // gArray's `fetched`, for gArray's reason.
      char fetched[arrayStore::ELEMENT_CAPACITY + 1] = {};
      std::size_t fetchedLength = 0;

      // Render buffer for the element outlet, reserved to
      // AtomList::RENDER_CAPACITY at construction.
      std::string emitScratch;
    };

    /**
     *  @brief Add an element at the end of an array — Max's ``array.push`` on
     *         the name-addressed value model ``.array`` settled (issue #784).
     *
     *  With ``.array.pop`` it is a stack; with ``.array.shift`` it is the
     *  queue a generative patch pushes events onto. Read ``gArrayEndsWriter``
     *  for what arrives and what leaves, and ``gArrayEndsBase`` for the
     *  binding and the guard.
     */
    class gArrayPush : public gArrayEndsWriter {
    public:
      gArrayPush();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_PUSH;
      }
      CREATE(gArrayPush)
    };

    /**
     *  @brief Remove the last element of an array and output it — Max's
     *         ``array.pop`` on the value model ``.array`` settled (issue
     *         #784).
     *
     *  ``.array.push``'s inverse: push then pop is a stack. Read
     *  ``gArrayEndsRemover`` for what arrives and what leaves, and
     *  ``gArrayEndsBase`` for the binding and the guard.
     */
    class gArrayPop : public gArrayEndsRemover {
    public:
      gArrayPop();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_POP;
      }
      CREATE(gArrayPop)
    };

    /**
     *  @brief Remove the first element of an array and output it — Max's
     *         ``array.shift`` on the value model ``.array`` settled (issue
     *         #784).
     *
     *  The consuming end of a queue fed by ``.array.push``. Read
     *  ``gArrayEndsRemover`` for what arrives and what leaves, and
     *  ``gArrayEndsBase`` for the binding and the guard.
     */
    class gArrayShift : public gArrayEndsRemover {
    public:
      gArrayShift();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_SHIFT;
      }
      CREATE(gArrayShift)
    };

    /**
     *  @brief Add an element at the front of an array — Max's
     *         ``array.unshift`` on the value model ``.array`` settled (issue
     *         #784).
     *
     *  ``.array.shift``'s inverse, and the object that turns a queue around:
     *  unshift then pop consumes in arrival order from the other end. A list
     *  lands in the order sent — ``unshift a b c`` leaves ``a b c`` at the
     *  front. Read ``gArrayEndsWriter`` for what arrives and what leaves, and
     *  ``gArrayEndsBase`` for the binding and the guard.
     */
    class gArrayUnshift : public gArrayEndsWriter {
    public:
      gArrayUnshift();
      const char* Type() const override {
        return YSE::OBJ::G_ARRAY_UNSHIFT;
      }
      CREATE(gArrayUnshift)
    };

  } // namespace PATCHER
} // namespace YSE
