#pragma once
#include "../../utils/mpmcQueue.hpp"
#include "../pObject.h"
#include "../time/timerBridge.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Snapshot and recall of the patch's control values — ``.preset``
     *         (issue #564).
     *
     *  Max's ``preset``, "store and recall the values of the control objects
     *  in a patcher". A bank of numbered slots; storing captures the current
     *  value of every participating control in the patch into one slot, and
     *  recalling pushes each captured value back into its object. Patch
     *  presets — the feature every instrument needs, and the thing a host
     *  would otherwise reimplement by walking the graph itself.
     *
     *  ### Who participates
     *
     *  Issue #564 asks for the participation rule up front, and it is the one
     *  the GUI value protocol (issue #551) already defines: **exactly the
     *  objects that answer true to ``GuiValueIsSettable()``**. That answer is
     *  a promise — "inlet 0 accepts, as a list, the exact string
     *  ``GetGuiValue()`` produced, and restores the state it described" — and
     *  the promise *is* what a preset needs, so making it the membership test
     *  means an object cannot be captured without also being restorable, by
     *  construction. Everything nested in subpatchers participates too:
     *  nesting is addressing, not storage (issue #545), and a preset belongs
     *  to the patch, not to one level of it.
     *
     *  The corollaries are all deliberate. ``.preset`` itself is not settable,
     *  so no preset ever captures a preset — Max's doesn't either. The scalar
     *  controls (``.slider``, ``.i``, ``.f``, ``.dial``, ``.incdec``, ``.t``)
     *  hold the round trip since their issue #846 migration — this object was
     *  the one that needed it — and are captured like any other settable
     *  control. ``.b`` stays out: its value is a consume-on-read press, an
     *  event no recall could meaningfully restore. And because participation
     *  is checked *before* the value is read, a store never polls ``.b`` at
     *  all, so capturing a patch cannot eat a press.
     *
     *  An object whose bulk read is empty at store time is left out of the
     *  slot: an empty string is not a message an inlet can take back, so
     *  recall leaves that object untouched rather than pretending to restore
     *  it.
     *
     *  ### The grammar
     *
     *  One inlet, hot:
     *
     *   - an **int or float** recalls that slot — Max's "recalls the preset
     *     whose number is received". A float is truncated the way every
     *     numeric control truncates.
     *   - a **bang** recalls the active slot again — the resync a patch wants
     *     after editing by hand. Nothing when no slot is active.
     *   - ``store <n>`` captures the patch into slot n — the headless
     *     spelling of Max's shift-click.
     *   - ``recall <n>`` is the int, spelled out.
     *   - ``clear <n>`` empties one slot; bare ``clear`` empties the active
     *     one (the parallel of the bang); ``clearall`` empties every slot.
     *     Clearing the active slot clears the active marker with it — the
     *     number no longer names stored contents.
     *
     *  Two int outlets: outlet 0 announces the slot just recalled, *after*
     *  its values have been pushed, so whatever it triggers sees the patch
     *  already in the preset; outlet 1 announces the slot just stored. Max's
     *  recalled/stored pair, in Max's order; his leftmost outlet speaks to
     *  ``pattrstorage``, which sits on the pattr system excluded from this
     *  pass. Recalling an empty or out-of-range slot does nothing and
     *  announces nothing — the family's rule that an object with no answer
     *  stays quiet.
     *
     *  ### The write path
     *
     *  Recall restores a patch by sending each captured object the exact
     *  string it produced, through ``pHandle::SetListData`` on inlet 0 — the
     *  ordinary control-thread message path, the same call a host makes. It
     *  never reaches into another object's fields, which would race the audio
     *  thread and skip every clamp, side effect and outlet send the object's
     *  own inlet handler performs. Whether a restore also *emits* is each
     *  object's own decision (the protocol leaves it per object), so a
     *  recalled ``.multislider`` announces its bank and a recalled
     *  ``.function`` stays silent, exactly as if a host had written them.
     *
     *  A slot entry is checked before it is pushed: the storage ID must still
     *  name a live object *of the type that was captured* — IDs are reused
     *  after a delete (issue #733), so without the type check a recall could
     *  write a ``.xyslider``'s state into whatever inherited its number.
     *  A stale entry is skipped silently; the rest of the slot still lands.
     *
     *  ### Threads — control-side work, and the hand-off (issue #952)
     *
     *  Store walks the patcher's object set and builds strings; recall runs
     *  whole subgraphs behind other objects' inlets. None of that can happen
     *  on the audio callback — and a great deal of what drives a preset lands
     *  there: the MIDI input objects drain ``MIDI::inHub`` inside the render
     *  pass, so ``.pgmin`` → ``.preset`` (Max's "program change recalls a
     *  preset") arrives on the callback; so does a host ``PassData`` into a
     *  ``.r`` feeding the preset (the #225 value queue drains at the top of
     *  ``Calculate``), and every ``.delay``/``.pipe`` delivery. Until #952
     *  all of those were dropped, silently.
     *
     *  **A message that physically arrives on the callback is handed off**,
     *  not run and not dropped. Physically: the tag is dispatch semantics, not
     *  thread identity (issue #690), so the handlers ask
     *  ``patcherImplementation::CallingThread``, as ``.metro``, ``.s`` and the
     *  whole time family do; a standalone object has no patcher to ask and
     *  trusts the tag. The handler parses the message — in place, allocation
     *  free, as it always did — into an operation and a slot number, pushes
     *  that onto a bounded lock-free queue, and wakes this object's
     *  ``timerBridge`` slot with a wait-free request. The timer thread — the
     *  thread a ``.metro``-driven recall has always run on — drains the queue
     *  and performs each operation exactly as a control-thread message would,
     *  in arrival order, with T_GUI. The callback pays a queue push and, for
     *  the first message of a burst, one atomic exchange plus the bridge's
     *  wait-free request: no allocation, no lock, no wait. The operation lands
     *  a timer tick later (about a millisecond plus a background-pool hop),
     *  not inside the block that carried it.
     *
     *  "Bang" and bare "clear" are resolved against the active slot when they
     *  *run*, not when they arrive, so ``store 3`` followed by a bang in the
     *  same block still recalls slot 3. A full queue drops the newest request
     *  and counts it (``DeferredDropped``) — a count rather than a log line,
     *  because the dropping thread is the callback. So does a preset whose
     *  bridge slot was refused at construction (a process holding
     *  ``timerBridge::CAPACITY`` timer owners at once).
     *
     *  The wake-up is one-shot in effect: the timer drains, requests its own
     *  stop, and only then gives up ``armed`` — with an acquire-release
     *  exchange, so a message pushed by a producer that saw ``armed`` still
     *  set is visible to the second drain that follows. A start is requested
     *  only by the producer that flips ``armed`` from clear to set, and a stop
     *  only by the callback while it is still set, so the bridge never sees a
     *  start and a stop race each other. ``Teardown`` stops the timer with the
     *  blocking handshake, so no recall is still running into the patch when
     *  the patcher starts deleting handles; the destructor gives the slot back.
     *
     *  The slot table itself is guarded by the family's non-blocking
     *  exclusive flag (``.funbuff``'s, ``.function``'s): a save may be
     *  serialising the patch on one control thread while a timer-thread
     *  recall fires on another, and a loser drops rather than waiting. A
     *  second store guard bounds re-entrancy: a push that arrives back at
     *  this object mid-recall (an outlet wired around to inlet 0) is dropped,
     *  the same loser-drops discipline.
     *
     *  ### What persists
     *
     *  The slots — contents, not just parameters — through ``DumpState`` /
     *  ``RestoreState``, unconditionally, plus the active marker: Max's
     *  ``preset`` is a UI object whose contents save with the patch. Loading
     *  does **not** recall anything; a patch that wants to come up in slot 0
     *  wires ``.loadbang`` into this inlet, which is exactly what
     *  ``.loadbang`` is for.
     *
     *  In the file an entry names its object by the dump's own record order —
     *  the *rank* of its storage ID among the live objects — not by the raw
     *  ID. ``ParseJSON`` hands loaded objects fresh IDs in exactly that order
     *  (issue #730's fixed point), so a patch whose IDs went sparse through
     *  deletes still recalls correctly after a save and a load, where a raw
     *  ID would silently point one object over. An entry whose object no
     *  longer exists at save time is dropped from the file — it could never
     *  be pushed again. The spelling assumes what every ordinary load does:
     *  a dump parsed into an empty patcher. Parsed into one that already
     *  holds objects, the fresh numbering starts past them and slot entries
     *  point at the wrong objects — the same caveat ``GetHandleFromID``
     *  carries for cached IDs, and the type check turns most of it into
     *  skips rather than misdirected writes.
     *
     *  ### The GUI value
     *
     *  The active slot number, ``-1`` when none — what a host highlights in a
     *  row of preset dots. Read-only: recalling is inlet 0's job and the
     *  display string is not a message, so ``GuiValueIsSettable()`` stays
     *  false, which is also what keeps ``.preset`` out of its own snapshots.
     *
     *  ### Deliberately not here
     *
     *  Interpolated recall — issue #564 leaves instant-vs-interpolated to be
     *  settled, and the answer is Max's own: ``preset`` recalls instantly,
     *  and interpolation belongs to ``pattrstorage``, which sits on the
     *  excluded pattr system. Morphing *between* presets is already a patch:
     *  ``.xyslider`` into ``.nodes`` into whatever weights the targets — the
     *  #563 use case, one cord per stage. Also not here: Max's
     *  ``pattrstorage`` outlet and client model, ``preset`` remnants that
     *  are pure UI (bubble sizes, margins, ``brgb``/``frgb``), and any
     *  capture of DSP-object state — a ``~vcf``'s cutoff is set by a message
     *  from a control, and the control is what a preset captures.
     */
    PATCHER_CLASS(gPreset, YSE::OBJ::G_PRESET)
    _NO_MESSAGES
    _NO_CALCULATE

    ~gPreset() override;

    _BANG_IN(BangIn)
    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    _HAS_GUI

    /** @brief Most slots the bank can be built to hold — the family's 1024. */
    static constexpr int MAX_SLOTS = 1024;

    /** @brief Fewest. One slot is still a preset. */
    static constexpr int MIN_SLOTS = 1;

    /** @brief The capacity with no creation argument. Max's ``preset`` holds
     *         as many presets as its box has room to draw; headless, the
     *         capacity is a creation parameter, and 32 is more than a live
     *         set tends to reach for at the cost of a pointer each. */
    static constexpr int DEFAULT_SLOTS = 32;

    /** @brief How many slots the bank was built to hold. */
    int Capacity() const {
      return capacity;
    }

    /** @brief The active slot — the one last stored or recalled — or -1.
     *         Diagnostics and tests; the GUI value is the same number. */
    int ActiveSlot() const {
      return active.load(std::memory_order_relaxed);
    }

    /** @brief How many captured objects slot @p slot holds. 0 for an empty
     *         or out-of-range slot, and 0 when the guard was held elsewhere —
     *         diagnostics and tests, control thread. */
    std::size_t SlotEntryCount(int slot) const;

    /** @brief Most operations that can wait for the timer thread at once —
     *         a burst of program changes inside one block, with room to
     *         spare. Fixed, so the queue is one allocation with the object. */
    static constexpr std::size_t DEFERRED_CAPACITY = 64;

    /** @brief Audio-callback messages refused so far: a full hand-off queue,
     *         or no timer slot to wake. A count rather than a log line, because
     *         the refusing thread is the callback (issue #952). */
    std::uint64_t DeferredDropped() const {
      return deferredDropped.load(std::memory_order_relaxed);
    }

    // Stop the hand-off timer with the blocking handshake, so no deferred
    // store or recall is still running into the patch once the patcher starts
    // unwiring and deleting handles (issue #952). Control thread, T_GUI.
    void Teardown(YSE::THREAD thread) override;

    // The slots, unconditionally — Max's preset is a UI object whose contents
    // save with the patch. Control thread; the guard is still taken because a
    // timer-thread recall may land mid-save. Entries are written as ranks —
    // see the class comment.
    bool DumpState(nlohmann::json::value_type& json) override;

    // The other half: ParseJSON, on a freshly built object the audio thread
    // cannot see yet. A rank read back *is* the fresh storage ID.
    void RestoreState(const nlohmann::json::value_type& json) override;

  private:
    // One captured object: which object (storage ID at capture time), what it
    // was (the type check that survives ID reuse — issue #733), and the exact
    // string its GetGuiValue() produced.
    struct Entry {
      unsigned int id = 0;
      std::string type;
      std::string value;
    };

    /**
     *  @brief Non-blocking exclusive access — ``.funbuff``'s guard, for its
     *         reason: the slots are read and written as a group, more than
     *         one control-side thread can arrive, and a loser drops rather
     *         than waiting.
     */
    class storeGuard {
    public:
      explicit storeGuard(std::atomic<bool>& flag)
        : flag_(flag), held_(!flag.exchange(true, std::memory_order_acquire)) {}
      ~storeGuard() {
        if (held_) flag_.store(false, std::memory_order_release);
      }
      storeGuard(const storeGuard&) = delete;
      storeGuard& operator=(const storeGuard&) = delete;
      storeGuard(storeGuard&&) = delete;
      storeGuard& operator=(storeGuard&&) = delete;

      bool Held() const {
        return held_;
      }

    private:
      std::atomic<bool>& flag_;
      bool held_;
    };

    // Whether this delivery is physically on the audio callback, whatever its
    // tag says (issue #690) — the question every handler asks first, because
    // everything this object does is control-thread work. A standalone object
    // has no patcher to ask and trusts the tag: the conservative reading for
    // an object whose work must never run on the callback.
    bool OnAudioThread(YSE::THREAD thread) const;

    // What one inlet message asks for, parsed. RECALL_ACTIVE (the bang) and
    // CLEAR_ACTIVE (bare `clear`) carry no slot: the active slot is read when
    // the operation runs, so a hand-off keeps the order it arrived in.
    enum class Op : std::uint8_t { RECALL, RECALL_ACTIVE, STORE, CLEAR, CLEAR_ACTIVE, CLEAR_ALL };

    // One handed-off operation. Trivially copyable, so it rides the lock-free
    // queue by value.
    struct Request {
      Op op = Op::RECALL;
      int slot = 0;
    };

    // Every handler's last step: run the operation now, or — physically on
    // the audio callback — hand it to the timer thread (issue #952).
    void Submit(Op op, int slot, YSE::THREAD thread);

    // Run one operation. Control-side threads only.
    void Execute(Op op, int slot, YSE::THREAD thread);

    // The callback half of the hand-off: queue, and wake the timer unless it
    // is already woken. Lock-free and allocation-free.
    void Defer(Op op, int slot);

    // The timer-thread half: drain, stop, give up `armed`, drain again. See
    // the class comment for why that order is the one that loses nothing.
    static void DeferTrampoline(void* ctx);
    void DrainDeferred();
    void RunDeferred();

    // Capture every participating object into `slot`, make it active, and
    // announce on outlet 1. Control thread; refused whole for a slot out of
    // range or without a patcher to walk.
    void Store(int slot, YSE::THREAD thread);

    // Push every entry of `slot` back into its object through the ordinary
    // message path, make the slot active, and announce on outlet 0. Control
    // thread; an empty or out-of-range slot does nothing and announces
    // nothing.
    void Recall(int slot, YSE::THREAD thread);

    // Empty one slot / every slot. Clearing the active slot clears the
    // active marker with it.
    void ClearSlot(int slot);
    void ClearAll();

    // The command half of the list inlet. False when `word` is none of them,
    // leaving the caller to read the message as a number. Parses only —
    // allocation-free, so it is safe on the callback — and submits.
    bool HandleCommand(const char* word, std::size_t length, const std::string& message,
                       std::size_t argOffset, YSE::THREAD thread);

    // Rebuild the bank from the creation arguments. Control thread only: the
    // constructor and the parameter callbacks, all of which run before the
    // object is wired or published; a live SetParams replaces the object
    // instead (ParamsNeedRebuild() is true), and the replacement starts with
    // empty slots — `.function`'s arrangement, and its consequence.
    void ShapeStore();

    // The creation arguments, as tokens. Control thread only.
    std::vector<std::string> creationArgs;

    // The bank: `capacity` slots, each a captured patch. Sized once by
    // ShapeStore(); entries are allocated at store time, which is always a
    // control-side thread — the hand-off above is what makes that a fact
    // rather than a hope.
    std::vector<std::vector<Entry>> slots;

    // Capture/recall staging, reused across operations so a busy patch does
    // not regrow it every time. Only ever touched with `sending` held.
    std::vector<Entry> scratch;

    int capacity = 0;

    // The active slot, or -1. Atomic because the GUI value is polled from the
    // host thread while a store lands on another.
    std::atomic<int> active{-1};

    // Slot-table guard: claimed with a single exchange by readers and writers
    // alike; the loser drops. Mutable so the const accessors can take it.
    mutable std::atomic<bool> busy{false};

    // Re-entrancy guard for whole operations (`.bag`'s `sending`): a store or
    // recall reached from inside a recall's own fan-out is dropped rather
    // than run over the scratch buffer the outer one is still using.
    std::atomic<bool> sending{false};

    // ---- The audio-callback hand-off (issue #952) ----

    // Operations that arrived on the callback, waiting for the timer thread.
    // Producers: whichever thread renders the patch. Consumer: the timer
    // callback. MPMC because nothing promises only one thread renders.
    YSE::mpmcQueue<Request> deferred{DEFERRED_CAPACITY};

    // This object's timerBridge slot, claimed in the constructor and given
    // back in the destructor. 0 when the bridge was full; every hand-off is
    // then counted as dropped.
    timerBridge::Handle timerSlot = 0;

    // Set by the producer that requests the wake-up, cleared by the callback
    // after it has requested the stop. Serialises the bridge's start and stop
    // requests — see the class comment.
    std::atomic<bool> armed{false};

    // Set by Teardown: a callback that still fires afterwards stops itself
    // without touching the patch.
    std::atomic<bool> closed{false};

    std::atomic<std::uint64_t> deferredDropped{0};
  };

} // namespace PATCHER
} // namespace YSE
