#pragma once
#include "../io/fileScheduler.h"
#include "../pObject.h"
#include "../time/clockBridge.h"
#include "../time/messageScheduler.h"
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief A cue list — a stored sequence of messages, played back in order
     *         and in time — ``.qlist`` (issue #500).
     *
     *  Max's ``qlist``, "store a collection of messages": "stores a collection
     *  of timed or untimed 'cues' in the form of messages which can be sent
     *  either out its outlet or remotely to various receive objects in your
     *  patch."
     *
     *  ### What it is, next to the six stores that came before it
     *
     *  Every one of them is passive. ``.coll`` (#494) answers a lookup,
     *  ``.bag`` (#495) answers a bang, ``.capture`` (#496), ``.funbuff``
     *  (#497), ``.table`` (#498) and ``.textfile`` (#499) answer a dump — and
     *  none of them has any notion of *when*. This one has a clock. A ``bang``
     *  starts it and it walks its own contents, sending as it goes, pausing for
     *  the durations written into the list itself. It is the first patcher
     *  object that plays something back rather than being read.
     *
     *  That is also what makes it a *cue* list rather than a collection: the
     *  timing is not a parameter of the object, it is *data in the list*, so a
     *  scene, a scripted parameter sweep or a note sequence is one object
     *  holding one block of text.
     *
     *  ### The three kinds of line, which is the whole grammar
     *
     *  Max: "There are three different message formats for lines in a cue-list:
     *  lines which contain only numerical values (i.e. an int, float or list),
     *  lines which begin with a symbol (i.e. message name and arguments), and
     *  lines beginning with a numerical value (or list of numerical values) and
     *  subsequently having a symbol (message name) with arguments."
     *
     *  - **All numbers** — sent out **outlet 0**, and, during automatic
     *    playback, the leading number is the delay before the walk continues.
     *    One line doing both jobs is not an overload: the number *is* the time,
     *    and sending it out lets a patch drive the same list by hand.
     *  - **Symbol first** — sent **remotely** to the named destination: to
     *    every ``.r`` of that name in the patcher, through the same
     *    ``PassBang`` / ``PassData`` door ``.s`` uses. Nothing leaves an
     *    outlet, and the walk does not stop.
     *  - **Numbers then a symbol** — Max: "treated as two separate lines - the
     *    first part (all numerical) is sent out the left outlet, and the second
     *    (message) part is remotely sent to a receive object."
     *
     *  That last rule is taken literally: a line is **split when it is stored**,
     *  never at playback. So the cursor is one index into one flat table, every
     *  stored entry is either wholly numeric or symbol-leading, and ``next``,
     *  ``fwd`` and automatic playback are the same walk over the same thing.
     *  The alternative — storing lines verbatim and splitting on the way past —
     *  needs a cursor with a half-step in it, and every one of the four
     *  traversals then has to agree about what half of a line it is standing on.
     *
     *  ### Playing it: by hand, or by the clock
     *
     *  ``next`` is the manual mode: it "will remotely send all lines beginning
     *  with a symbol, and stop after it encounters and outputs a line beginning
     *  with a numerical value". The number it stops on goes out outlet 0, which
     *  is the classic Max idiom — outlet 0 into a delay, the delay's bang back
     *  into ``next`` — and it is the reason a number line outputs at all.
     *  ``next`` with a non-zero argument "will ignore lines beginning with
     *  symbols and only output the next line beginning with a numerical value",
     *  and ``fwd <n>`` is that done @p n times without sending anything
     *  remotely: Max's "fast forward through a given number of lines".
     *
     *  ``bang`` is the automatic mode. It "begins sending messages from the
     *  first line" — so it rewinds first — "until a line begins with a number,
     *  at which point qlist will use that number as a delay time in
     *  milliseconds before continuing to send the remaining messages". ``stop``
     *  ends it; ``tempo`` scales it, 0.5 for half speed and 2. for twice as
     *  fast, which is a division: a tempo of 2 halves every wait.
     *
     *  Either way, reaching the end bangs **outlet 1** — Max's "a bang is sent
     *  when a cue list has reached the end, and there are no more lines to send
     *  or output".
     *
     *  ### Where the time comes from
     *
     *  The patcher's own deferred-message scheduler (#628), the mechanism
     *  ``.bondo`` already defers its release through. Arming is wait-free and
     *  allocation-free, so a ``bang`` arriving from a rendering graph may arm
     *  one; delivery happens at the top of ``patcherImplementation::Calculate``
     *  inside a real ``messageEventScope``, so each resumed step is one logical
     *  event downstream rather than a pile of unrelated stimuli.
     *
     *  Two consequences worth naming. The scheduler's deadline floor is **one
     *  audio block**, so a cue list of zero delays advances one entry per block
     *  instead of spinning — a patch cannot write a list that locks the audio
     *  thread up. And the clock is the patcher's block counter, so a paused
     *  engine holds the sequence where it stands, which is the only meaning
     *  "300 ms from now" can have on a clock that is not running.
     *
     *  ### A resumed step passes its THREAD tag straight through
     *
     *  The scheduler delivers with **T_GUI**, deliberately: the delivery sets
     *  state and the block's own traversal renders what it caused, which is
     *  what the #225 value drain does and what an outlet send from a resumed
     *  step wants. A remote send is tagged the same way and needs no special
     *  case.
     *
     *  It used to. Until **#690**, ``patcherImplementation::PassData`` read
     *  T_GUI as *"the caller is the control thread"* and answered it by taking
     *  ``mtx``, scanning the object map and — for an unknown destination —
     *  building a log string, all on the audio callback that a resumed step
     *  runs on. This object routed around that by forcing **T_DSP** for the
     *  remote half only, and the forcing cost it the T_GUI semantics
     *  downstream. ``PassData`` now asks ``CallingThread`` which thread it is
     *  physically on and picks the lock-free mechanism from that, leaving the
     *  tag to mean only what it says, so both halves carry the delivered tag
     *  and a ``.r`` downstream of a cue behaves the same as one downstream of
     *  the #225 value drain.
     *
     *  ### ``clock <name>``: the same list on a domain clock (issue #688)
     *
     *  Issue #500 asked for playback bound to ``CLOCK::domainClock`` so the
     *  sequence would inherit the polytemporal tempo model ``YSE::clip`` uses,
     *  and #500 declined twice over: Max's cue list is milliseconds — "a delay
     *  time in milliseconds", with a ``tempo`` that is a bare multiplier and no
     *  beat, bar or meter anywhere in it — and there was no patcher-to-domain-
     *  clock bridge to build on. #688 built the bridge (``clockBridge``), which
     *  leaves only the first objection, and the answer to that one is an opt-in:
     *
     *  - **``clock <name>``** binds the domain clock called @p name and switches
     *    the object to beats. The leading number of a numeric cue is then a
     *    **beat count** on that clock instead of a delay in milliseconds.
     *    Everything else is untouched — the same list, the same split, the same
     *    outlets, the same ``next`` / ``fwd`` / ``rewind`` / ``stop``, and
     *    ``tempo`` still divides, so ``tempo 2`` is still twice as fast.
     *  - **``clock``** with no argument goes back to milliseconds. A
     *    ``.qlist`` that is never sent one is Max's object exactly, which is
     *    the whole reason this is a message rather than a change of unit.
     *
     *  ``clock`` is Max's own vocabulary for this: ``setclock`` documents its
     *  name as something "passed as the argument to a ``clock`` message to
     *  numerous objects that use timing in Max". Max's ``qlist`` reference does
     *  not list ``clock`` among *its* methods, so this is an addition rather
     *  than a port — named the way Max names the idea, and inert until used.
     *
     *  What it buys is what milliseconds cannot: a cue list that follows tempo
     *  changes and ramps, that stays in step with every ``YSE::clip`` on the
     *  same domain, and that pauses when the domain does (a clock at tempo 0
     *  holds the walk where it stands). A clock named before the host creates it
     *  simply does not advance until it appears, and then plays — the binding
     *  resolves on the background pool and the wait is baselined at the moment
     *  the clock starts existing.
     *
     *  The binding is **not saved** with the patch, for the reason ``tempo``
     *  and the cursor are not: it is run-time state, and a reloaded patch that
     *  re-bound itself to a clock the host may not have created yet would have
     *  two answers to "what is this object waiting on".
     *
     *  ### Storage model
     *
     *  ``.coll``'s, for ``.coll``'s reason: a fixed table of entries allocated
     *  whole at construction, every entry reserved to its capacity, plus
     *  ``.value``'s non-blocking ``busy`` guard, claimed with a single
     *  ``exchange`` by a loser that **drops** rather than spinning. Not a
     *  copy-on-write ``GraphState`` publish, which assumes the writer is the
     *  control thread — this object is written by whichever thread its message
     *  arrived on, and in-patcher delivery dispatches on ``T_DSP``.
     *
     *  The guard is never held across a send: one entry is copied into a buffer
     *  reserved at construction, the cursor advanced, the guard released, and
     *  only then does the message leave — so a patch that wires an outlet, or a
     *  ``.r``, back into this object finds the store free. A walk therefore
     *  takes the guard once per entry and re-reads the bounds each step, which
     *  is ``.coll``'s ``dump``.
     *
     *  Anything that does not fit is refused whole and silently: an entry past
     *  ``ENTRY_CAPACITY`` characters, and an entry past the ``MAX_ENTRIES``th.
     *  Refused rather than truncated, because half a cue is a different cue.
     *
     *  ``Calculate()`` does nothing, for the reason it does nothing in
     *  ``.coll``, ``.capture``, ``.table`` and ``.textfile``: the object is
     *  driven by its inlet and by its own clock, and one that emitted would
     *  restart itself on every DSP tick.
     *
     *  ### What persists — and this one is not ``.textfile``'s answer
     *
     *  The **contents**, through ``DumpJSON`` / ``ParseJSON`` and
     *  ``pObject::DumpState`` / ``RestoreState``. The family rule is *save iff
     *  Max gives the object a save flag*, and Max gives this one the plainest
     *  statement of it in the whole reference: "The qlist object saves its
     *  cue-list with the patcher." No attribute to switch, no file needed —
     *  unlike ``text``, whose contents live in a file and which therefore saves
     *  nothing. So ``.qlist`` saves like ``.coll``, not like ``.textfile``,
     *  even though the two are both blocks of message text and both have a
     *  ``read``.
     *
     *  The cursor, the tempo and whether it is playing are **not** saved. They
     *  are run-time position in exactly the sense ``.coll``'s pointer is, Max
     *  documents no stored tempo, and a reloaded patch that resumed a sequence
     *  mid-flight by itself would be a surprise rather than a feature.
     *
     *  ### Cue-list files (issue #689)
     *
     *  ``read <file>`` replaces the cue list with the lines of a text file and
     *  ``write <file>`` writes it back out. Neither opens anything on the
     *  message path: a handler runs on whichever thread the message arrived on,
     *  in-patcher delivery dispatches on ``T_DSP``, and ``THREAD`` is a
     *  *dispatch-semantics* tag rather than a thread identity — there is no
     *  predicate an object can ask to find out that it is not on the audio
     *  callback, so opening a file there would block it. ``fileScheduler``
     *  (issue #683) is the shared answer: the request is a wait-free claim on a
     *  patcher-owned slot, the disk work runs on the background pool honouring
     *  the host's ``IO()`` layer, and the bytes are parsed in the completion the
     *  patcher delivers at the top of a later block — which is also when
     *  **outlet 2** bangs. ``.coll`` and ``.textfile`` are the worked examples
     *  and this object is the same five steps over a different format.
     *
     *  Max's third outlet "bangs when a file has been read successfully from
     *  disk". It is **appended** here rather than inserted, which is ``.coll``'s
     *  rule and the whole family's — and in this case it is also Max's own
     *  position for it, so no saved patch's cords shift either way. It fires
     *  only on a successful ``read``: Max has no outlet for a finished write and
     *  neither does this.
     *
     *  ### The file format, and what round-trips
     *
     *  Max's: one cue line per line, semicolon-terminated, which is what a
     *  ``write`` emits — ``<entry>;\n`` per stored entry. A ``read`` breaks
     *  lines on **either** a ``;`` or a newline, so a file written by hand with
     *  no semicolons loads as the lines it looks like; a trailing ``\r`` costs
     *  nothing either, ``IsSelectorSeparator`` already counting it as
     *  whitespace, so a CRLF file loads the same lines everywhere. Splitting on
     *  the newline as well is a departure from ``.coll``, whose records are
     *  ``;``-terminated only, and it is deliberate: this format has no
     *  punctuation *inside* a line the way ``.coll``'s ``address, message`` has,
     *  so a newline can only ever be a line break.
     *
     *  Every entry stored here is already split at a ``;`` — that is what
     *  ``AddLines`` does — so the round trip is exact: what was written comes
     *  back as the same entries in the same order, including the split of a
     *  numbers-then-message line into two, which the file's reader applies the
     *  same way the inlet's does. The one thing that cannot round-trip is an
     *  entry holding a newline of its own, which only a message carrying one can
     *  produce, and which is the same kind of limitation as ``.coll``'s comma.
     *
     *  A ``read`` **replaces** what is held and rewinds the cursor, the cursor
     *  having pointed into a list that no longer exists. It does not stop a walk
     *  in progress: the walk carries on into the freshly loaded list from its
     *  first line, which is the only reading under which the cue list a patch
     *  just loaded is the one that plays. A line longer than ``ENTRY_CAPACITY``
     *  is refused and the rest of the file still loads, lines past
     *  ``MAX_ENTRIES`` are dropped, and a file larger than
     *  ``fileScheduler::BYTES_CAPACITY`` or one that cannot be opened is refused
     *  whole with outlet 2 silent.
     *
     *  ``read`` and ``write`` with no argument reuse the last name given, each
     *  half remembering its own — Max's bare forms open a file dialog and a
     *  headless patcher has none. Max documents no ``readagain`` / ``writeagain``
     *  for ``qlist`` (``coll`` has them), so neither is invented here.
     *
     *  There is **no filename creation argument**, and that is the one place
     *  this object parts company with ``.textfile``. Max documents none for
     *  ``qlist`` — where ``text`` has one that "names a text file to be read in
     *  when the object is loaded" — and the reason it would be wrong here is the
     *  section above: a ``.qlist`` saves its cue list *with the patcher*, so an
     *  object that also read a file when it joined one would have two answers to
     *  "what is in this list" arriving in an order neither the patch nor the
     *  object controls.
     *
     *  ### Reserved words, and the semicolon
     *
     *  ``bang``, ``next``, ``fwd``, ``rewind``, ``stop``, ``clear``, ``set``,
     *  ``append``, ``insert``, ``tempo``, ``clock``, ``read`` and ``write`` are
     *  commands;
     *  ``open`` and ``wclose`` are consumed and inert. This inlet is a
     *  **command** inlet, not a data inlet, so the ``.prepend`` / ``.atoi``
     *  discipline — never reserve a word on an inlet that has to carry
     *  arbitrary text — does not apply, exactly as it does not apply to
     *  ``.coll``: cue text is written with ``set``, ``append`` and ``insert``,
     *  never by being sent bare. A message that is none of the above does
     *  nothing, which is Max, and a bare int or float does nothing for the same
     *  reason — ``qlist`` documents no ``int`` or ``float`` method.
     *
     *  ``;`` separates cue lines inside ``set``, ``append`` and ``insert``.
     *  Max's own separator, with one departure: Max says "to append a
     *  semicolon, it must be preceded by a backslash character", which is a
     *  rule about the *message box* that would otherwise eat it, not about
     *  ``qlist``. The patcher has no message box, so a semicolon is written
     *  bare and a backslash before one is not special.
     *
     *  ``insert`` really does append. Max: "the word insert followed by any
     *  arguments will append those arguments to the qlist object's cue-list as
     *  a new entry in the list" — its name is a Max idiosyncrasy, and it is
     *  reproduced rather than corrected, since a patch brought across from Max
     *  must build the same list.
     *
     *  ### Deliberately not here
     *
     *  The editing window and everything addressing it (``open``, ``wclose``,
     *  the double-click), the patcher having no GUI at all.
     */
    PATCHER_CLASS(gQlist, YSE::OBJ::G_QLIST)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _LIST_IN(ListIn)

    /**
     *  @brief Most cue lines the list holds — 256.
     *
     *  The patcher's own bound: ``.coll``'s entry count, ``.textfile``'s line
     *  count and ``patcherImplementation::kValueListCap``. The table is
     *  allocated at this size once, on the control thread, and never resized; a
     *  cue past it is refused rather than growing it, since growing it would
     *  allocate on whichever thread the message arrived on.
     */
    static constexpr std::size_t MAX_ENTRIES = 256;

    /** @brief Longest cue line, in characters — ``kValueListCap`` again, so
     *         anything that can reach this object through the patcher's value
     *         queue also fits in it. A line past it is refused whole. */
    static constexpr std::size_t ENTRY_CAPACITY = 256;

    /**
     *  @brief Longest text ``append`` can join before re-splitting it — two
     *         full entries and the separator between them.
     *
     *  An ``append`` has to see the last entry and the new text as one line
     *  before it can decide where that line breaks, so this bounds the one
     *  buffer that holds both. Wider than an entry on purpose: appending a
     *  message to a full numeric cue splits into two entries that each fit,
     *  and refusing at ``ENTRY_CAPACITY`` would refuse that. Anything past it
     *  is refused whole, since a longer join would have to allocate.
     */
    static constexpr std::size_t JOIN_CAPACITY = (2 * ENTRY_CAPACITY) + 1;

    /**
     *  @brief Longest text a ``write`` produces, in characters (issue #689).
     *
     *  Every entry at its maximum plus the ``;\n`` each one costs, so the whole
     *  cue list always fits and a ``write`` can only fail on the disk rather
     *  than on its own bound. Reserved once at construction, because the
     *  message that asks for a ``write`` may be on the audio thread.
     */
    static constexpr std::size_t FILE_TEXT_CAPACITY = MAX_ENTRIES * (ENTRY_CAPACITY + 2);
    static_assert(FILE_TEXT_CAPACITY <= fileScheduler::BYTES_CAPACITY,
                  "a full .qlist cue list must fit one file slot");

    /** @brief How many cue lines the list holds. Note that this counts entries
     *         **after** splitting, so one written line carrying both numbers
     *         and a message counts as two. */
    std::size_t Count() const;

    /** @brief Cue line @p index, 0 the first, or ``""``. Diagnostics and tests:
     *         it returns a copy, so control thread only. */
    std::string EntryAt(std::size_t index) const;

    /** @brief Where the next ``next`` / ``fwd`` / automatic step will read, as
     *         an index. Equal to ``Count()`` once the list has run out. */
    std::size_t Position() const;

    /** @brief The playback multiplier — Max's ``tempo``, 1 by default. Every
     *         wait is divided by it, so 2 plays twice as fast. */
    float Tempo() const {
      return tempo.load(std::memory_order_relaxed);
    }

    /** @brief Whether a ``bang``-started walk is still running. False before
     *         one starts, after ``stop``, and once the end has been reached. */
    bool IsPlaying() const {
      return playing.load(std::memory_order_relaxed);
    }

    /** @brief Whether ``clock <name>`` has put the object on a domain clock, so
     *         a numeric cue's leading number is beats rather than milliseconds
     *         (issue #688). False for a fresh object and after a bare
     *         ``clock``. */
    bool OnClock() const {
      return binding.load(std::memory_order_relaxed) != 0;
    }

    /** @brief The clock name the object is bound to, or ``""``. The storage
     *         belongs to the patcher's bridge and never changes, so this is
     *         safe from any thread. */
    const char* ClockName() const;

    /** @brief The name the last ``read`` was given, which a bare ``read``
     *         reuses. Empty until one names a file, there being no creation
     *         argument to seed it. Control thread. */
    const std::string& ReadFile() const {
      return readPath;
    }

    /** @brief The same for ``write``. Control thread. */
    const std::string& WriteFile() const {
      return writePath;
    }

    // Build the file plumbing while still on the control thread — so a `read`
    // arriving later on the audio thread finds the table already there. Nothing
    // is requested here, unlike `.textfile`: Max gives `qlist` no filename
    // argument, and a load-time read would race the cue list this object
    // restores from the patch itself (issue #689).
    void SetParent(pObject* newParent) override;

    // A read or write this object asked for has finished. Called on the
    // patcher's dispatch thread inside a fresh messageEventScope; parses the
    // bytes into the cue list and bangs outlet 2. Allocation-free, like every
    // other path into this object.
    void DeliverFileResult(const fileResult& result, YSE::THREAD thread) override;

    // The contents, into the object's "state" key of a DumpJSON — Max's "the
    // qlist object saves its cue-list with the patcher". Control thread —
    // patcherImplementation::DumpJSON holds mtx — but the guard is still taken,
    // because a message may be arriving from a rendering graph while the patch
    // is being saved.
    void DumpState(nlohmann::json::value_type& json) override;

    // The other half: called from ParseJSON on the control thread, on a freshly
    // built object the audio thread cannot see yet.
    void RestoreState(const nlohmann::json::value_type& json) override;

    // The scheduler coming back with the next step of an automatic walk.
    void DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) override;

  private:
    /** @brief What one step of a walk did. */
    enum class Step {
      OUTPUT, ///< A numeric line went out outlet 0; its leading number is the wait.
      SENT, ///< A symbol line was sent remotely, or skipped. Keep walking.
      END, ///< Nothing left to read.
      DROP, ///< The guard was lost; this walk does nothing further.
    };

    /**
     *  @brief Non-blocking exclusive access to the cue list.
     *
     *  ``Held()`` is false when another thread had it — the caller then does
     *  nothing at all. Never waits, never allocates. ``.coll``'s ``storeGuard``
     *  and ``.value``'s ``valueSlotGuard``, for the reason both give: this
     *  object is reachable from the control thread and from a rendering graph
     *  alike, a mutex is out on the second of those, and there is no single
     *  writer to build a seqlock around.
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

    // ── writing the list ──────────────────────────────────────────────────

    // Copy one already-split entry in at the end. False when the list is full
    // or the text is longer than an entry can hold — refused rather than
    // truncated. Guard held.
    bool PushEntry(const char* text, std::size_t length);

    // Store one cue line, splitting off its leading run of numeric tokens as an
    // entry of its own — Max's "treated as two separate lines". Guard held.
    void StoreLine(const char* text, std::size_t length);

    // Split `text` on ';' and StoreLine each segment. Guard held.
    void AddLines(const char* text, std::size_t length);

    // Take the guard and add `text` as new cue lines. Does nothing at all when
    // it loses the guard.
    void Insert(const char* text, std::size_t length);

    // Max's `append`: glue `text` onto the last entry and re-split the result,
    // since numbers followed by a message are two lines however they were
    // written. An empty list makes this an `insert`. The combined line is
    // refused whole when it would pass ENTRY_CAPACITY. Takes the guard itself.
    void AppendToLast(const char* text, std::size_t length);

    // Empty the list and put the cursor back at the start. Takes the guard
    // itself; also cancels any pending step, since the walk has nothing left to
    // walk.
    void Clear();

    // ── playing it ────────────────────────────────────────────────────────

    // Read the entry under the cursor into `sendText`, advance the cursor, then
    // — with the guard released — send it: out outlet 0 when it is numeric,
    // remotely when it is a symbol line, and not at all when it is a symbol
    // line and `ignoreSymbols`. `wait` receives the leading number of a numeric
    // line, which is what an automatic walk waits for.
    Step Advance(bool ignoreSymbols, float& wait, YSE::THREAD thread);

    // Max's `next`: walk until a numeric line has been output, banging outlet 1
    // if the end arrives first. Returns false once the end has been reached, so
    // `fwd` stops rather than banging once per remaining repeat.
    bool Next(bool ignoreSymbols, YSE::THREAD thread);

    // The automatic walk: step until a numeric line gives a wait to arm, or the
    // end arrives. Re-entered from DeliverDeferred each time a wait elapses.
    void Resume(YSE::THREAD thread);

    // Arm the next step `wait` out, scaled by the tempo — milliseconds
    // normally, beats on the bound domain clock once `clock <name>` has been
    // given (issue #688). False when there is no scheduler (a standalone
    // object) or it refused, in which case the caller keeps walking without
    // waiting.
    bool ArmContinue(float wait);

    // Max's `clock <name>` / bare `clock`, through the patcher's bridge. Binds
    // wait-free on whichever thread the message arrived on; a name that does
    // not fit, a bridge that is full, or a standalone object all leave the
    // object where it was, silently, since this may be the audio thread.
    void SetClock(const char* name, std::size_t length);

    // Drop a pending step, if there is one.
    void CancelPending();

    // ── sending ───────────────────────────────────────────────────────────

    // Send `text` out outlet `pin` in the kind it is: a list, or the int or
    // float it spells when it is a single number. `.route`'s rule, shared with
    // `.coll` and `.textfile`.
    void SendTyped(std::size_t pin, const std::string& text, YSE::THREAD thread);

    // A symbol line: the first token names the destination, the rest is the
    // message. Sent through the patcher's own PassBang / PassData, in the kind
    // the remainder is, so a `.r` downstream sees an int where the cue wrote
    // one.
    void SendRemote(const std::string& text, YSE::THREAD thread);

    // The command half of the inlet. False when `word` is none of them, which
    // on a command inlet means the message is simply not understood.
    bool HandleCommand(const char* word, std::size_t length, const std::string& message,
                       std::size_t argOffset, YSE::THREAD thread);

    // ── files ─────────────────────────────────────────────────────────────

    // What a completion carries back, so a read and a write can be told apart
    // in DeliverFileResult. Private to this object — the tag means nothing to
    // the scheduler.
    static constexpr int FILE_TAG_READ = 0;
    static constexpr int FILE_TAG_WRITE = 1;

    // The read / write half of the inlet. `name` is the argument the message
    // carried, or null for a bare `read` / `write`, which reuses the last name
    // given — Max's bare forms open a dialog and a headless patcher has none.
    // False when there is nothing to do — no patcher, no name yet, a name that
    // does not fit, or a file table that is full — in every case silently,
    // since this may be the audio thread.
    bool RequestFile(FILE_OP op, const char* name, std::size_t length);

    // Format the cue list into `fileScratch` as Max's plain text, one entry per
    // line and each ended with a semicolon. Takes the guard; allocates nothing,
    // because the scratch was reserved to FILE_TEXT_CAPACITY at construction.
    // False when the guard was held elsewhere.
    bool Serialize();

    // The other direction: replace the cue list with the lines in the `length`
    // bytes at `text`, breaking on `;` and on newlines alike and splitting each
    // one the way the inlet's `set` does. Takes the guard; allocates nothing.
    // False when the guard was held elsewhere, in which case nothing changed.
    bool LoadFrom(const char* text, std::size_t length);

    // Claimed with a single exchange by readers and writers alike; the loser
    // drops. Mutable so the const diagnostic accessors can take it.
    mutable std::atomic<bool> busy{false};

    // The cue list. Sized to MAX_ENTRIES at construction and never resized,
    // every entry reserved to ENTRY_CAPACITY; only the first `count` are live.
    // Each one is either wholly numeric or symbol-leading, because a line is
    // split when it is stored.
    std::vector<std::string> entries;
    std::size_t count = 0;

    // Where the next step reads. Run-time position rather than a parameter, so
    // it does not survive a save — `.coll`'s pointer is the same kind of thing.
    std::size_t position = 0;

    // Max's tempo multiplier. Every wait is divided by it. Always finite and
    // strictly positive: a tempo of zero or less cannot scale a duration into
    // anything a clock can wait for, so one is refused rather than stored.
    // Atomic because a `tempo` message and a resumed step are not on the same
    // thread; relaxed, since neither publishes anything through it.
    std::atomic<float> tempo{1.f};

    // Whether a bang-started walk is still running. Written from the inlet and
    // from the scheduler's delivery — the control thread and the audio thread —
    // so it is atomic for the same reason `tempo` is.
    std::atomic<bool> playing{false};

    // The step this object is waiting on, or 0. One clock per object, Max's
    // shape — `.bondo`'s rule, arrived at for the same reason.
    std::atomic<messageScheduler::Handle> pending{0};

    // The domain clock `clock <name>` bound, or 0 for Max's milliseconds
    // (issue #688). A patcher-owned binding handle rather than a name: the
    // bridge never releases one, so it stays valid for the life of the patcher
    // and costs nothing to carry. Atomic because a `clock` message and a
    // resumed step are not on the same thread; relaxed, since neither
    // publishes anything through it.
    std::atomic<clockBridge::Handle> binding{0};

    // What a send is made from, reserved at construction. Copies rather than
    // the entry itself: the send path is synchronous, so handing an outlet the
    // stored string would let a patch that writes to this object from
    // downstream mutate the very message still being fanned out.
    std::string sendText;
    std::string sendName;
    std::string sendPayload;
    // Scratch for `append`, which has to see the old entry and the new text as
    // one line before it can split them.
    std::string joinText;

    // The last name each half of the file surface was given — what a bare
    // `read` / `write` reuses, there being no dialog to ask with and no
    // creation argument to seed them. Reserved to the scheduler's path bound at
    // construction, so remembering a name on a message path is an assign() into
    // storage that exists rather than an allocation (issue #689).
    std::string readPath;
    std::string writePath;

    // Where a `write` is formatted before it is handed to the scheduler.
    // Reserved to FILE_TEXT_CAPACITY at construction for the same reason.
    std::string fileScratch;
  };

} // namespace PATCHER
} // namespace YSE
