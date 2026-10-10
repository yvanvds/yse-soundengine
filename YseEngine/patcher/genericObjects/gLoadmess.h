#pragma once
#include "../pObject.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief `.loadmess` — send a stored message when the patcher has finished
     *         loading (issue #547). Max's `loadmess`.
     *
     *  ### What it is for
     *
     *  Max: "Send a message when a patch is loaded" — "outputs a message
     *  automatically when the file is opened, or when the patch is part of
     *  another file that is opened", and "any arguments you type into a
     *  `loadmess` object are treated as a message to be sent when output is
     *  triggered."
     *
     *  It is `.loadbang` with a payload, and it is the half that does the actual
     *  initialising: `.loadbang -> .m 0.5 -> ~sine` is three objects and a cord
     *  where `.loadmess 0.5 -> ~sine` is one. A patch that sets its own gains,
     *  cutoffs, tempos and modes at load needs one of these per value and
     *  nothing from the host at all.
     *
     *  ### When it fires
     *
     *  Exactly where `.loadbang` fires, through the same
     *  `pObject::Loadbang` hook and the same
     *  `patcherImplementation::LoadbangObjects` pass: once, at the end of
     *  `ParseJSON`, after the parsed graph has been compiled and published.
     *  Read gLoadbang.h for why that point and no earlier one is "loading
     *  finished", why the pass runs on the control thread outside the patcher's
     *  mutex, why the order of two of these is undefined, and why an object
     *  created live through `CreateObject` never receives one.
     *
     *  The consequence is sharper here than for `.loadbang`, which is worth
     *  saying once: a `.loadmess` that fired on every edit would re-send its
     *  value every time anything in the patch was touched, overwriting whatever
     *  the patch had done since. An initialisation that will not stay
     *  initialised is not one.
     *
     *  ### What it sends
     *
     *  The creation arguments, as one message, in the kind they are — the rule
     *  `.route`, `.coll`, `.textfile` and `.qlist` already share, so a stored
     *  `60` comes back as an int and not as `60.`:
     *
     *  - nothing typed: **nothing is sent**. There is no message, and `.loadbang`
     *    is the object for wanting a bang;
     *  - the single word `bang`: a **bang**. Max's `loadmess` holds Max's
     *    messages and `bang` is one of them; `.if` reads a lone `bang` in its
     *    branches the same way, being "the one message that is a word rather
     *    than a number";
     *  - one token that is a whole finite number: an **int** when it is spelled
     *    as one and a **float** when it carries a `.` or an exponent, which is
     *    how Max's own parser tells the two atoms apart;
     *  - anything else, including every multi-token message: a **list** carrying
     *    the text verbatim. Text travels as a list message in this patcher —
     *    there is no symbol message — which is the same deviation `.trigger`
     *    documents.
     *
     *  ### `set`
     *
     *  Max: "the word `set` followed by any message will set the message held by
     *  `loadmess` without any output. (Can be used for output in conjunction
     *  with `bang`.)" Implemented on both routes text can arrive by, because a
     *  message box reaches `SetMessage` while a `.trigger` or a `.sprintf`
     *  reaches the list inlet, and `set` has to work either way. A bare `set`
     *  with nothing after it empties the stored message, which is the same
     *  object a `.loadmess` with no arguments is.
     *
     *  A list that does not begin with `set` is ignored rather than stored.
     *  Max's `loadmess` documents exactly three inputs — `bang`, `set` and a
     *  double-click — and treating a bare list as an implicit `set` would make
     *  every value that happened to pass through this object overwrite the
     *  initialisation it exists to hold.
     *
     *  ### Real-time behaviour
     *
     *  A message handler runs on whichever thread dispatched the message, and
     *  in-patcher delivery dispatches on `T_DSP`, so both the `set` path and the
     *  bang path can be the audio callback. `Calculate()` does nothing.
     *
     *  The stored message is a `std::string` whose capacity is reserved to
     *  `MESSAGE_CAPACITY` in the constructor, so `set` **assigns into the buffer
     *  it already owns** and allocates nothing — the same trick
     *  `patcherImplementation::listScratch_` uses on the same path, for the same
     *  reason. A message longer than that capacity is refused **whole** and
     *  counted, not truncated: half a message is a different message, which is
     *  `.capture`'s rule rather than `.print`'s, and for `.capture`'s reason.
     *  Sending allocates nothing either — the outlet takes the stored string by
     *  reference, and the number forms are read with `ReadNumericToken`, which
     *  parses into a stack buffer.
     *
     *  Reads and writes of the string are serialised by `.value`'s non-blocking
     *  `storeGuard`, since this object is reachable from the control thread and
     *  from a rendering graph alike and a mutex is out on the second. A loser of
     *  the guard drops and is counted rather than waiting, which a message
     *  handler may never do.
     *
     *  Unusually for this patcher the guard **is** held across the send, and
     *  deliberately: the thing being sent is the buffer itself, so releasing
     *  first would mean either copying it — an allocation on the audio thread —
     *  or handing the outlet a string another thread may be overwriting. What it
     *  costs is that an outlet wired back into this object's own inlet finds the
     *  guard taken and drops, and that cost is really a second benefit: a
     *  `.loadmess` cannot be made to recurse into itself. Nothing else is
     *  affected, because the guard is this object's own and no other object
     *  waits on it.
     *
     *  ### What persists
     *
     *  The creation arguments, verbatim, because they are creation arguments —
     *  `Parameters` stores the argument string as given, so a save writes back
     *  what was typed even when a `set` has since replaced what the object
     *  holds. A `set` is a live override and does not survive a save, exactly as
     *  in Max, where the typed message is what the box contains.
     */
    PATCHER_CLASS(gLoadmess, YSE::OBJ::G_LOADMESS)
    _DO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Longest message the object will hold — 256 characters.
     *
     *  The patcher's own bound: `.coll`'s entry count, `.bag`'s capacity,
     *  `.offer`'s table and the patcher's own value queue (`kValueListCap`) are
     *  all this number, so anything that can reach this object through a patch
     *  also fits in it. It is the string's reserved capacity, which is what
     *  makes `set` allocation-free; a message past it is refused whole.
     */
    static constexpr std::size_t MESSAGE_CAPACITY = 256;

    /**
     *  @brief The patch this object is in has finished loading — send the stored
     *         message.
     *
     *  Called once per object by `patcherImplementation::LoadbangObjects`, after
     *  the parsed graph has been published. Never called for an object created
     *  live; see gLoadbang.h.
     */
    void Loadbang(YSE::THREAD thread) override;

    /**
     *  @brief The message currently held, as text.
     *
     *  Diagnostics and tests. Not synchronised against a concurrent `set` — a
     *  caller that needs a consistent read is the control thread of a patch that
     *  is not being driven, which is every caller there is.
     */
    const std::string& Message() const {
      return message;
    }

    /** @brief Messages sent, from every cause — the load and the inlet
     *         together. Monotonic, readable from any thread. A patch sees the
     *         same thing by counting what leaves the outlet. */
    std::uint64_t Sent() const {
      return sent.load(std::memory_order_relaxed);
    }

    /** @brief Messages refused: a `set` longer than `MESSAGE_CAPACITY`, and any
     *         path that found the store held by another thread. Monotonic, and
     *         the object's overflow report — a counter rather than a log line
     *         because the refusing thread may be the audio callback. */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

  private:
    /**
     *  @brief Non-blocking exclusive access to the stored message.
     *
     *  `Held()` is false when another thread had it — the caller then does
     *  nothing at all and counts the loss. Never waits, never allocates.
     *  `.value`'s `valueSlotGuard` and `.bag`'s / `.offer`'s `storeGuard`, for
     *  the reason all three give: this object is reachable from the control
     *  thread and from a rendering graph alike, a mutex is out on the second of
     *  those, and there is no single writer to build a seqlock around.
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

    // Send whatever is held, in the kind it is. Takes the guard and keeps it for
    // the whole send — see the class notes for why this one does. The single
    // path both the load hook and the inlet end at, so the two ways of starting
    // a patch cannot start it differently.
    void Emit(YSE::THREAD thread);

    // Max's `set`: replace the held message without sending. Refuses whole and
    // counts anything past MESSAGE_CAPACITY, and anything that loses the guard.
    // RT-safe: one bounded assign into already-reserved storage.
    void Store(const char* text, std::size_t length);

    // Read `set <message>` (or a bare `set`) out of `text`. Returns false when
    // the text is not a set message at all, in which case nothing is stored.
    // Shared by the list inlet and the `anything` handler, which are the two
    // routes text can arrive by.
    bool TryStoreSet(const std::string& text);

    // The message, as text. Capacity reserved to MESSAGE_CAPACITY in the
    // constructor so `set` never reallocates on whatever thread it runs on.
    // Guarded by `busy` on every path that reads or writes it after
    // construction; the parse callbacks run before the object is published and
    // are the one exception.
    std::string message;

    // The store's non-blocking guard. See storeGuard.
    std::atomic<bool> busy{false};

    std::atomic<std::uint64_t> sent{0};
    std::atomic<std::uint64_t> dropped{0};

    // The creation arguments, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler. A
    // LIST parameter rather than a STRING one because it has to absorb every
    // token — Max's "any arguments you type" is the whole remainder of the
    // argument string, and a STRING parameter takes one token.
    std::vector<std::string> creationArgs;
  };

} // namespace PATCHER
} // namespace YSE
