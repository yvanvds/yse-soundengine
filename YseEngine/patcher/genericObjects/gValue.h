#pragma once
#include "../pObject.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /** @brief What a ``valueSlot`` is currently holding. */
    enum class valueKind : std::uint8_t {
      None, //!< Nothing has been stored yet — a bang emits nothing.
      Int,
      Float,
      List
    };

    /**
     *  @brief The shared cell behind ``.value`` — one per name, held by every
     *         ``.value`` that addresses it.
     *
     *  Deliberately not a ``BusValue``. The bus's payload variant holds a
     *  ``std::string``, and reading one out of shared storage on the audio
     *  thread would mean copying into a heap buffer somebody else may be
     *  reallocating. Here the list payload is a fixed ``kTextCapacity`` array
     *  that is written in place and never resized, so a store and a read are
     *  both a bounded ``memcpy`` and nothing else.
     *
     *  ### Why the guard is a try-lock rather than a mutex
     *
     *  A ``.value`` is written and read from whichever thread the message
     *  arrived on, and the in-patcher delivery path (``PassData``) dispatches on
     *  **T_DSP** — so "the audio thread stores into a ``.value``" is the normal
     *  case, not an exotic one. A ``std::mutex`` is therefore out (the engine's
     *  standing rule: nothing on an audio path allocates, locks or blocks), and
     *  so is a seqlock: with several ``.value`` objects on the same name there
     *  is no single writer, and two concurrent writers would tear the payload.
     *
     *  What is left is mutual exclusion that never waits. ``busy`` is claimed
     *  with one ``exchange``; whoever loses **drops** its store or its read
     *  rather than spinning. That is the patcher's existing answer to
     *  contention everywhere else — a full ``valueQueue_`` drops, an over-long
     *  list drops — and the window it guards is a few dozen bytes wide, so a
     *  loss needs two threads inside the same handful of nanoseconds on the
     *  same name.
     */
    struct valueSlot {
      /**
       *  @brief Longest list payload the cell holds.
       *
       *  256 — ``patcherImplementation::kValueListCap``, the same bound the
       *  in-patcher value queue carries a list payload in, so anything that can
       *  reach a ``.value`` through the patcher fits in one.
       */
      static constexpr std::size_t kTextCapacity = 256;

      // Claimed with a single exchange by both readers and writers; the loser
      // drops. Excluding readers from each other is unnecessary but costs one
      // atomic and keeps the invariant a single sentence.
      std::atomic<bool> busy{false};

      // Everything below is touched only while `busy` is held.
      valueKind kind = valueKind::None;
      int intValue = 0;
      float floatValue = 0.0f;
      std::size_t textLength = 0;
      char text[kTextCapacity + 1] = {};
    };

    /**
     *  @brief Non-blocking exclusive access to a ``valueSlot``.
     *
     *  ``Held()`` is false when another thread had the cell — the caller then
     *  does nothing at all. Never waits, never allocates.
     */
    class valueSlotGuard {
    public:
      explicit valueSlotGuard(valueSlot& slot)
        : slot_(slot), held_(!slot.busy.exchange(true, std::memory_order_acquire)) {}
      ~valueSlotGuard() {
        if (held_) slot_.busy.store(false, std::memory_order_release);
      }
      valueSlotGuard(const valueSlotGuard&) = delete;
      valueSlotGuard& operator=(const valueSlotGuard&) = delete;
      valueSlotGuard(valueSlotGuard&&) = delete;
      valueSlotGuard& operator=(valueSlotGuard&&) = delete;

      bool Held() const {
        return held_;
      }

    private:
      valueSlot& slot_;
      bool held_;
    };

    /**
     *  @brief The cell addressed by @p address, created if no ``.value`` holds
     *         it yet; @p created reports which of the two happened.
     *
     *  Control thread only — it takes a mutex and may allocate. ``.value``
     *  calls it from ``SetParams`` / ``SetParent`` and never from a message
     *  handler, which is the whole reason the message path is just a pointer
     *  hop.
     *
     *  The registry holds cells weakly: a name lives exactly as long as some
     *  ``.value`` addresses it. Strong ownership would make every name a patch
     *  has ever spelled immortal for the life of the process, which for a
     *  headless engine that opens and closes patchers is an unbounded leak, and
     *  would leave one test's value visible to the next.
     */
    std::shared_ptr<valueSlot> AcquireValueSlot(const std::string& address, bool& created);

    /**
     *  @brief A named cell shared by every object that addresses it —
     *         ``.value`` (issue #486).
     *
     *  Max's ``value`` (aliased ``v``), whose one-line summary is "Share stored
     *  data with other objects": "When you send a message to one value object,
     *  it updates all value objects sharing that name, even across different
     *  patches. Send a bang to retrieve the stored contents."
     *
     *  ### What it adds that ``.s`` / ``.r`` cannot express
     *
     *  ``.s`` and ``.r`` are a *push*: the value exists while it is in flight
     *  and then it is gone, so a receiver that was not listening at the moment
     *  of the send has no way to ask what the value was. Every patch that wants
     *  the answer later has to keep its own copy — a ``.f`` per reader, fed by
     *  a ``.r``, one per place that cares.
     *
     *  ``.value`` is the *pull* half: the value is stored, and a bang asks for
     *  it whenever the asking patch is ready. That is the difference between
     *  "tell me when the tempo changes" and "what is the tempo?", and only the
     *  first of those is expressible with a send.
     *
     *  ### The name is the same name ``.s`` and ``.r`` use
     *
     *  The cell is addressed as ``"<patcherName>.<name>"`` — the
     *  ``INTERNAL::NamedBus`` address form, so ``.value tempo``, ``.s tempo``
     *  and ``.r tempo`` in the same patcher all speak about one word, and two
     *  patchers given the same ``patcher::name()`` share their cells exactly as
     *  they already share their sends. A patcher's auto-generated
     *  ``"patcher_<N>"`` name keeps unrelated patchers apart by default.
     *
     *  What it deliberately does **not** do is publish. A store that went out
     *  on the bus would fire every ``.r`` of that name on every write, which is
     *  precisely ``.s``'s job — and would make ``.value`` a send with a memory
     *  rather than a register. Max's ``value`` is silent on store for the same
     *  reason, and a patch that wants both writes a ``.value`` and a ``.s``
     *  from the same outlet, which says so.
     *
     *  ### An unnamed ``.value`` is private
     *
     *  Not "shares the empty name". ``"<patcherName>."`` is a real, reachable
     *  address, so an unnamed ``.value`` sharing it would silently pool with
     *  every other unconfigured ``.value`` in the patcher — two objects a patch
     *  author has not connected in any visible way, quietly overwriting each
     *  other. An unnamed one therefore gets a cell of its own and behaves as a
     *  plain store-and-recall register, which is the only reading under which
     *  "not named yet" stays distinguishable from "named".
     *
     *  The same holds before the object has a parent: a ``.value`` constructed
     *  standalone (as the unit tests do) has no patcher name to prefix with, so
     *  it stays private until ``SetParent`` gives it one.
     *
     *  ### The initial argument belongs to whoever creates the cell
     *
     *  ``.value tempo 120`` starts the cell at 120 — but only if it is the
     *  first ``.value tempo`` to exist. A second one joining an established name
     *  **adopts what is already there** rather than resetting it, because the
     *  alternative is that adding a reader to a patch silently rewinds the value
     *  every other object is reading.
     *
     *  That rule is also what makes ``.value`` survive live editing. A
     *  ``SetParams`` on a published object is a rebuild (#234): the replacement
     *  is constructed while the original is still alive, so the cell is still
     *  held, so the replacement joins it and the running value carries across
     *  the edit instead of snapping back to the creation argument.
     *
     *  The initial is the *rest* of the argument string, not one token, so
     *  ``.value pos 0 0`` starts the cell at the list ``0 0``. A single numeric
     *  token is classified the way ``.sel`` and ``.trigger`` classify theirs —
     *  ``ReadNumericToken`` decides whether it is a number at all and
     *  ``TokenLooksLikeFloat`` decides int or float — so ``120`` starts as an
     *  int and ``120.`` as a float.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlet, and an
     *  emitting ``Calculate()`` would hand a patch a value it never asked for —
     *  the rule ``.s`` and the whole routing family establish.
     *
     *  No message path allocates, locks or blocks. Resolving the name happens
     *  once, on the control thread, and leaves the object holding a
     *  ``shared_ptr`` to the cell, so a store or a read is one pointer hop and
     *  one ``exchange``. A stored list is copied into the cell's fixed array
     *  and read back into a ``std::string`` reserved to ``kTextCapacity`` at
     *  construction — the ``patcherImplementation::listScratch_`` discipline,
     *  which lets the outlet be handed the ``const std::string&`` it wants
     *  without an allocation. A list longer than the cell is refused rather
     *  than truncated, and silently, since this may be the audio thread.
     */
    PATCHER_CLASS(gValue, YSE::OBJ::G_VALUE)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(Emit)
    _INT_IN(StoreIntValue)
    _FLOAT_IN(StoreFloatValue)
    _LIST_IN(StoreListValue)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief The shared name, or empty when this ``.value`` has a cell of its
     *         own. The first creation argument.
     */
    const std::string& ValueName() const {
      return valueName;
    }

    /**
     *  @brief The address the cell is registered under —
     *         ``"<patcherName>.<name>"`` — or empty while the cell is private.
     */
    const std::string& Address() const {
      return boundAddress;
    }

    /** @brief Whether this object's cell is shared by name rather than private. */
    bool IsShared() const {
      return !boundAddress.empty();
    }

    /** @brief What the cell holds right now. Diagnostics and tests. */
    valueKind Kind() const;

    // Bind to the cell the moment the patcher name is known, so a message never
    // resolves a name. Mirrors gSend::SetParent, which caches its bus address
    // for the same reason (issue #187).
    void SetParent(pObject* parent) override;

    // Re-bind after a patcher rename: the address prefix moved, so the object
    // now addresses a different cell. Called from
    // patcherImplementation::SetName alongside gReceive::Resubscribe and
    // gSend::RefreshBusAddress, so a renamed patcher's values re-anchor with
    // its sends and receives.
    void RefreshBinding();

  private:
    // Point at the cell the current name and parent address, creating it if
    // this is the first .value to name it — in which case the creation
    // argument's initial value is written into it. Control thread only
    // (SetParams / SetParent / SetName). A no-op when the address has not
    // changed, so the running value survives a re-parse that leaves the name
    // alone.
    void Rebind();

    // Write the `initial` creation argument into the cell — called for a cell
    // this object just created, and for a private cell on every re-parse,
    // which owns no readers to disturb. Control thread only.
    void ApplyInitial();

    // Store a scalar. Returns false only when another thread held the cell.
    bool StoreScalar(valueKind kind, int intPart, float floatPart);

    // Store `length` characters of `text` as the list payload. Returns false
    // when the cell was held, or when the payload is longer than the cell —
    // refused rather than truncated, and silently, since this may be the audio
    // thread.
    bool StoreText(const char* text, std::size_t length);

    // The shared name. First creation argument; empty means a private cell.
    std::string valueName;

    // The rest of the creation argument string — the value the cell starts at
    // when this object is the one that creates it. A LIST parameter, so it
    // absorbs every remaining token rather than only the next one.
    std::vector<std::string> initial;

    // The address `slot` is registered under, or empty while it is private.
    // Also the "has the binding changed?" key Rebind() compares against.
    std::string boundAddress;

    // The cell. Never null once the object has been constructed, so no message
    // handler needs a null check.
    std::shared_ptr<valueSlot> slot;

    // Read-out buffer for a stored list, reserved to kTextCapacity at
    // construction so copying the cell's payload out never allocates.
    std::string emitScratch;
  };
} // namespace PATCHER
} // namespace YSE
