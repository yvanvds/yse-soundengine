#pragma once
#include "../io/fileScheduler.h"
#include "../pObject.h"
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief A sequence of messages collected as lines of text — ``.textfile``
     *         (issue #499).
     *
     *  Max's ``text``, "format messages as a text file": "collects and formats
     *  incoming messages as text to be output as lines of text."
     *
     *  ### What it is, next to the four stores that came before it
     *
     *  The line is the unit, and that is the whole difference. ``.coll`` (#494)
     *  is keyed by an address, ``.bag`` (#495) has no keys, ``.funbuff`` (#497)
     *  is a sparse function of x,y pairs and ``.table`` (#498) a dense array of
     *  numbers. ``.capture`` (#496) is the closest — it too swallows whatever
     *  arrives — but it stores **one atom per item**, so a three-element list
     *  becomes three entries and comes back as three sends. Here a message is
     *  appended to the *current line*, several messages accumulate on that line
     *  separated by spaces, and ``cr`` ends it. That is the shape a note list, a
     *  cue sheet or a configuration block actually has, and it is the shape a
     *  text file has.
     *
     *  ### The name
     *
     *  ``.text`` is already taken by the patcher's text-label object, and issue
     *  #499 offers renaming that one to ``.label`` as the alternative. It is not
     *  renamed: the name is written into every saved patch's JSON, so changing it
     *  would break every file that holds one, and no behaviour of this object
     *  depends on getting the shorter name. ``.textfile`` also says what the
     *  object is *for* — the file half is where it ends up (see below) — where
     *  ``.label`` would say only what the other one draws.
     *
     *  ### Appending, and the trailing space
     *
     *  Max's ``anything``: "the message is stored in the text object, placed
     *  after any previously stored messages, and is followed by a space." Max
     *  keeps that space in the buffer, which is why both ``cr`` and ``tab``
     *  carry the rule "if the last character in text is a space, the [carriage
     *  return / tab stop] replaces that space".
     *
     *  Here the separator is kept **between** messages instead of after each
     *  one: appending to a line that already carries something writes one space
     *  first. The visible text is identical — Max's replace-the-space rule exists
     *  precisely to remove the one it just wrote — and there is no trailing space
     *  for ``line`` and ``dump`` to hand downstream. ``tab`` is what makes this
     *  more than bookkeeping: it writes a tab and then *clears* the pending
     *  separator, so ``5``, ``tab``, ``6`` gives ``5\t6`` and not ``5\t 6``,
     *  which is Max's behaviour spelled as state rather than as a fix-up.
     *
     *  A number is spelled the patcher's one way — ``WriteInt`` for an int and
     *  ``ExprFormatValue`` for a float — so the text this object holds agrees
     *  with the text every other object in the patcher writes for the same value.
     *  A list, or any message that is not a command, is appended **as it
     *  arrived**: it is already text, and re-spelling its atoms would rewrite a
     *  message the patch composed on purpose.
     *
     *  ### Lines, and what ``cr`` does to an empty one
     *
     *  ``lineCount`` lines exist and the last of them is *open* while it is still
     *  accepting appends. ``cr`` closes it, and a ``cr`` arriving when no line is
     *  open materialises an empty one instead — so two in a row leave a blank
     *  line between two full ones, and a ``cr`` on a fresh object leaves one
     *  blank line. That is the flat buffer Max describes, read back as lines: a
     *  ``cr`` always puts a newline at the end of the contents, and the number of
     *  lines is the number of newlines plus whatever follows the last one.
     *
     *  ### The outlets, and Max's middle one
     *
     *  Max has three: the text, a bang when a file has finished loading, and the
     *  line count from ``query``. The middle one is **outlet 2** here rather than
     *  outlet 1, because #499 shipped this object with two outlets and inserting
     *  the file outlet where Max puts it would shift the cords of every patch
     *  saved since. Appending is ``.coll``'s rule and the whole file-reading
     *  family follows it (issue #687).
     *
     *  ``line`` prepends the word ``set``, which is Max's: "the text of the
     *  specified line number is sent out preceded by the word ``set`` ... can be
     *  sent to any other object for which that particular ``set`` message is
     *  appropriate". That word is not foreign here — ``.table``, ``.funbuff``,
     *  ``.bucket``, ``.cycle``, ``.accum`` and ``.match`` all take a ``set``, so
     *  a ``line`` straight into one of them does in this patcher what it does in
     *  Max. ``dump`` sends the contents bare, which is also Max's.
     *
     *  What ``dump`` sends leaves in the kind the line **is**, ``.route``'s rule:
     *  a line holding ``60`` leaves as the int 60, ``60.5`` as that float, and
     *  anything else as a list. ``line`` is always a list, because ``set`` plus
     *  the contents is always more than one atom.
     *
     *  ### Reserved words
     *
     *  ``clear``, ``cr``, ``tab``, ``dump``, ``line``, ``query``, ``symbol``,
     *  ``read`` and ``write`` do their jobs; ``open``, ``wclose``, ``settitle``,
     *  ``filetype``, ``precision`` and ``stringout`` are consumed and do nothing.
     *  Reserving words on a data inlet is what the ``.prepend`` / ``.atoi``
     *  discipline warns against, and ``.coll``'s exemption (its inlet is a
     *  *command* inlet) does not apply — but Max settles it the way it settled it
     *  for ``.capture``: Max dispatches on the selector, so a ``text`` in Max
     *  cannot store the word ``clear`` either, and an object that stored it would
     *  produce different *contents* from Max's for the same patch. All fifteen
     *  are therefore consumed, including the two attribute names, which in Max
     *  set an attribute rather than being stored.
     *
     *  Max gives the escape hatch itself, and it is ported: ``symbol clear``
     *  stores the word ``clear`` — "this is useful if you want to store a word
     *  that would otherwise be understood as a specific message by ``text``".
     *
     *  ### Text files (issue #687)
     *
     *  The half the name promises. ``read <file>`` replaces the contents with the
     *  lines of a text file and ``write <file>`` writes them back out; the
     *  ``filename`` creation argument is read when the object is built, which is
     *  Max's "names a text file to be read in when the object is loaded".
     *
     *  None of it happens on the message path. A handler runs on whichever thread
     *  the message arrived on, in-patcher delivery dispatches on **T_DSP**, and
     *  ``THREAD`` is a *dispatch-semantics* tag rather than a thread identity —
     *  there is no predicate an object can ask to find out that it is not on the
     *  audio thread, so opening a file in a handler would block the callback.
     *  ``fileScheduler`` (issue #683) is the shared answer: the request is a
     *  wait-free claim on a patcher-owned slot, the disk work runs on the
     *  background pool honouring the host's ``IO()`` layer, and the bytes are
     *  parsed in the completion the patcher delivers at the top of a later block
     *  — which is also when outlet 2 bangs. ``.coll`` is the worked example and
     *  this object is the same five steps over a different format.
     *
     *  ### The format, and what round-trips
     *
     *  A plain text file, one stored line per line of the file, ``\n``
     *  separated. The **last line carries a newline only when it is closed**,
     *  which is what makes the round trip exact rather than merely equal: Max's
     *  buffer is flat text where a ``cr`` is a character, so contents whose last
     *  line is still open have no trailing newline, and reading a file that ends
     *  without one leaves its last line open for the next append. A file ending
     *  in ``\n`` therefore reads back with every line closed, and ``a\n\n`` is
     *  two lines, the second blank — the same thing two ``cr``s produce.
     *
     *  A trailing ``\r`` is dropped from each line, so a CRLF file written by
     *  another editor reads as the same lines on every platform; the write side
     *  emits ``\n`` alone. The one thing that cannot round-trip is a stored line
     *  containing a newline of its own, which is Max's limitation too and the
     *  same kind of thing as ``.coll``'s comma and semicolon.
     *
     *  A ``read`` **replaces** what is held, as Max's does. A line longer than
     *  ``LINE_CAPACITY`` is skipped and the rest of the file still loads, lines
     *  past ``MAX_LINES`` are dropped and the first ``MAX_LINES`` kept — the
     *  rules an over-long append and a full table already follow, since the store
     *  cannot grow without allocating on whichever thread the message arrived on.
     *  A file larger than ``fileScheduler::BYTES_CAPACITY``, or one that cannot
     *  be opened, is refused whole and outlet 2 stays silent.
     *
     *  ``read`` and ``write`` with no argument reuse the last name given, which
     *  starts out as the ``filename`` creation argument: Max's bare forms open a
     *  file dialog and a headless patcher has none, so a bare ``write`` on a
     *  ``.textfile notes.txt`` saves back over the file the object is named
     *  after. ``filetype`` is consumed and inert for the same reason — it narrows
     *  the types those dialogs offer. Max has no ``readagain`` / ``writeagain``
     *  for ``text`` (``coll`` does), so neither is invented here.
     *
     *  ### Storage model
     *
     *  ``.coll``'s, for ``.coll``'s reason: a fixed table of lines allocated
     *  whole at construction, every line reserved to its capacity, plus
     *  ``.value``'s non-blocking ``busy`` guard, claimed with a single
     *  ``exchange`` by a loser that **drops** rather than spinning. Not a
     *  copy-on-write ``GraphState`` publish, which assumes the writer is the
     *  control thread — this object is written by whichever thread its message
     *  arrived on.
     *
     *  The guard is never held across a send: a line is copied into a buffer
     *  reserved at construction, the guard released, and only then does the
     *  message leave, so a patch that wires an outlet back into this object's
     *  inlet finds the store free. ``dump`` takes the guard once per line and
     *  re-reads the bounds each step, ``.coll``'s walk.
     *
     *  Anything that does not fit is refused whole and silently: an append that
     *  would push a line past ``LINE_CAPACITY``, and a new line past
     *  ``MAX_LINES``. Refused rather than truncated, because half a line is a
     *  different line.
     *
     *  ``Calculate()`` does nothing, for the reason it does nothing in
     *  ``.route``, ``.value``, ``.coll``, ``.capture`` and ``.table``: the object
     *  is driven by its inlet, and one that emitted would re-dump on every DSP
     *  tick.
     *
     *  ### What persists
     *
     *  The ``filename`` argument, because it is a creation parameter. The
     *  **contents deliberately do not**, and that is Max: the family's rule is
     *  save-iff-Max-has-a-save-flag — ``coll`` has "save data with patcher",
     *  ``table`` and ``funbuff`` have ``embed`` — and ``text`` has none of them.
     *  Max keeps a ``text``'s contents in a *file*, reached by ``read`` /
     *  ``write`` and by the filename argument at load, which is the same answer
     *  ``.capture`` gives for the same reason. Since #687 that file is where they
     *  live here too: a ``.textfile notes.txt`` reloads its lines when the patch
     *  it was saved in is opened.
     *
     *  ### Deliberately not here
     *
     *  The editing window and everything addressing it — ``open``, ``wclose``,
     *  ``settitle``, the double-click — and ``filetype``, which selects among
     *  Mac-era four-letter type codes for the file *dialogs* a headless patcher
     *  does not have.
     *
     *  The ``precision`` attribute, which sets "the number of decimal places for
     *  converted floating point values". The patcher has no attribute mechanism,
     *  and more than that it already has exactly one way of spelling a float:
     *  ``ExprFormatValue``, which writes the fewest digits that read back as the
     *  same value. That is strictly better than a fixed decimal count — no digits
     *  lost, no padding zeros — and a second spelling on this one object would
     *  make its text disagree with every other object's for the same number.
     *
     *  The ``stringout`` attribute, "output lines as string object": the patcher
     *  has no string atom to output.
     */
    PATCHER_CLASS(gTextfile, YSE::OBJ::G_TEXTFILE)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Most lines the contents can hold — 256.
     *
     *  The patcher's own bound: ``.coll``'s entry count, ``.atoi``'s character
     *  ceiling and ``patcherImplementation::kValueListCap``. The table is
     *  allocated at this size once, on the control thread, and never resized; a
     *  ``cr`` past it is refused rather than growing it, since growing it would
     *  allocate on whichever thread the message arrived on.
     */
    static constexpr std::size_t MAX_LINES = 256;

    /**
     *  @brief Longest line, in characters — 256.
     *
     *  Max's own limit for the message that reads one back: ``line`` "causes
     *  text to send out the contents of that line number (**up to 256
     *  characters**)". An append that would pass it is refused whole.
     */
    static constexpr std::size_t LINE_CAPACITY = 256;

    /**
     *  @brief Longest text a ``write`` produces, in characters (issue #687).
     *
     *  Every line at its maximum plus the newline each one costs, so the whole
     *  contents always fit and a ``write`` can only fail on the disk rather than
     *  on its own bound. Reserved once at construction, because the message that
     *  asks for a ``write`` may be on the audio thread.
     */
    static constexpr std::size_t FILE_TEXT_CAPACITY = MAX_LINES * (LINE_CAPACITY + 1);
    static_assert(FILE_TEXT_CAPACITY <= fileScheduler::BYTES_CAPACITY,
                  "full .textfile contents must fit one file slot");

    /** @brief How many lines the contents hold — what ``query`` reports. */
    std::size_t LineCount() const;

    /** @brief Line @p index, 0 the first, or ``""``. Note that Max's ``line``
     *         message numbers from **1**; this is the storage index.
     *         Diagnostics and tests: it returns a copy, so control thread
     *         only. */
    std::string LineAt(std::size_t index) const;

    /** @brief Whether the last line is still accepting appends — false on a
     *         fresh object and after a ``cr``. */
    bool LineIsOpen() const;

    /** @brief Max's ``filename`` creation argument, or ``""``. Held, saved, and
     *         read when the object is built — see the class documentation.
     *         Control thread only. */
    std::string Filename() const;

    /** @brief The name the last ``read`` was given, which a bare ``read``
     *         reuses. Seeded from the ``filename`` argument. Control thread. */
    const std::string& ReadFile() const {
      return readPath;
    }

    /** @brief The same for ``write``. Control thread. */
    const std::string& WriteFile() const {
      return writePath;
    }

    // Build the file plumbing while still on the control thread — so a `read`
    // arriving later on the audio thread finds the table already there — and
    // then ask for the `filename` argument's file, which is Max's "names a text
    // file to be read in when the object is loaded". SetParent is only called
    // under patcherImplementation::mtx, and always after SetParams, so the name
    // is already parsed by the time this runs (issue #687).
    void SetParent(pObject* newParent) override;

    // A read or write this object asked for has finished. Called on the
    // patcher's dispatch thread inside a fresh messageEventScope; parses the
    // bytes into the contents and bangs outlet 2. Allocation-free, like every
    // other path into this object.
    void DeliverFileResult(const fileResult& result, YSE::THREAD thread) override;

  private:
    /**
     *  @brief Non-blocking exclusive access to the contents.
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

    // Make sure there is a line to append to, starting one when the last was
    // closed by a `cr`. False when the contents already hold MAX_LINES lines, in
    // which case the caller stores nothing. Guard held.
    bool OpenLine();

    // Append the `length` characters at `text` to the open line, writing one
    // separating space first when the line already carries something. False when
    // the line has no room — refused whole rather than truncated. Guard held.
    bool AppendText(const char* text, std::size_t length);

    // Take the guard, open a line if none is, and append `text` through
    // AppendText; does nothing at all when it loses the guard.
    void Append(const char* text, std::size_t length);

    // Spell a number the patcher's one way and append it — WriteInt for an int,
    // ExprFormatValue for a float, so the text agrees with what every other
    // object in the patcher writes for the same value. Take the guard themselves.
    void AppendInt(int value);
    void AppendFloat(float value);

    // Max's `cr`: end the open line, or — when none is open — leave a blank line
    // behind, because the carriage return then goes at the end of contents that
    // already end in one. Takes the guard itself.
    void Cr();

    // Max's `tab`: a tab character at the end of the open line, and no separating
    // space in front of whatever comes next — Max's "if the last character in
    // text is a space, the tab stop replaces that space", spelled as state.
    // Takes the guard itself.
    void Tab();

    // Take the guard, copy line `index` into the send buffer, release it, and
    // send it out outlet 0. With `withSet` the word `set` leads the message,
    // which is Max's `line`; without it the line goes out bare, which is Max's
    // `dump`. False when there is no such line — which is what stops the dump
    // walk — and nothing is sent then. The guard is deliberately not held across
    // the send, see the class documentation.
    bool Output(std::size_t index, bool withSet, YSE::THREAD thread);

    // Send `text` out outlet `pin` in the kind it is: a list, or the int or
    // float it spells when it is a single number. `.route`'s rule, minus its
    // bang case — an empty line is still a line, and a bang would read
    // downstream as "no data" rather than "empty data".
    void SendTyped(std::size_t pin, const std::string& text, YSE::THREAD thread);

    // The command half of the inlet. Returns false when `word` is none of them,
    // leaving the caller to append the message as text.
    bool HandleCommand(const char* word, std::size_t length, const std::string& message,
                       std::size_t argOffset, YSE::THREAD thread);

    // What a completion carries back, so a read and a write can be told apart in
    // DeliverFileResult. Private to this object — the tag means nothing to the
    // scheduler.
    static constexpr int FILE_TAG_READ = 0;
    static constexpr int FILE_TAG_WRITE = 1;

    // The read / write half of the inlet. `name` is the argument the message
    // carried, or null for a bare `read` / `write`, which reuses the last name
    // given — Max's bare forms open a dialog and a headless patcher has none.
    // False when there is nothing to do — no patcher, no name yet, a name that
    // does not fit, or a file table that is full — in every case silently, since
    // this may be the audio thread.
    bool RequestFile(FILE_OP op, const char* name, std::size_t length);

    // Format the contents into `fileScratch` as plain text, one line per line
    // and `\n` separated, with a trailing newline only when the last line is
    // closed. Takes the guard; allocates nothing, because the scratch was
    // reserved to FILE_TEXT_CAPACITY at construction. False when the guard was
    // held elsewhere.
    bool Serialize();

    // The other direction: replace the contents with the lines in the `length`
    // bytes at `text`. Takes the guard; allocates nothing. False when the guard
    // was held elsewhere, in which case nothing was changed.
    bool LoadFrom(const char* text, std::size_t length);

    // Claimed with a single exchange by readers and writers alike; the loser
    // drops. Mutable so the const diagnostic accessors can take it.
    mutable std::atomic<bool> busy{false};

    // The contents. Sized to MAX_LINES at construction and never resized, every
    // line reserved to LINE_CAPACITY; only the first `lineCount` are live.
    std::vector<std::string> lines;
    std::size_t lineCount = 0;

    // Whether line `lineCount - 1` still takes appends. False on a fresh object
    // and after a `cr`, so the next append starts a new line.
    bool lineOpen = false;

    // Whether the next append writes a separating space first. Set by an append,
    // cleared at the start of a line and by `tab` — which is what keeps Max's
    // "the tab stop replaces that space" from needing a fix-up pass.
    bool needsSeparator = false;

    // What a send is made from, reserved at construction. A copy rather than the
    // line itself: the send path is synchronous, so handing an outlet the stored
    // string would let a patch that appends to this object from downstream mutate
    // the very message still being fanned out.
    std::string sendText;

    // Max's filename argument. Held so a `.textfile mydata.txt` brought across
    // from Max builds and so the argument survives a save; it is also the file
    // SetParent reads and the name a bare `read` / `write` falls back on
    // (issue #687). Control thread only.
    std::string fileName;

    // The last name each half of the file surface was given, seeded from
    // `fileName` — what a bare `read` / `write` reuses, there being no dialog to
    // ask with. Reserved to the scheduler's path bound at construction, so
    // remembering a name on a message path is an assign() into storage that
    // exists rather than an allocation.
    std::string readPath;
    std::string writePath;

    // Where a `write` is formatted before it is handed to the scheduler.
    // Reserved to FILE_TEXT_CAPACITY at construction for the same reason.
    std::string fileScratch;

    // The creation arguments, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::vector<std::string> creationArgs;
  };

} // namespace PATCHER
} // namespace YSE
