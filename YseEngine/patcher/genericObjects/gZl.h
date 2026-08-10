#pragma once
#include "../math/gRandomSource.h"
#include "../pAtomList.h"
#include "../pObject.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Max's ``zl`` — the list-processing workhorse: one object whose
     *         behaviour is chosen by a mode word (issues #523, #524).
     *
     *  Max: "zl — multi-purpose list processing object". Two inlets, two
     *  outlets, and a mode that decides what happens between them. #523 landed
     *  the shell, the dispatch, the bounded storage model and the three modes
     *  that prove the design: ``len``, ``rev`` and ``nth``. #524 adds the
     *  **reordering** group — ``rot``, ``scramble``, ``sort``, ``swap`` and
     *  ``indexmap``. The remaining mode groups follow in their own issues.
     *
     *  ### One shape for every reordering mode
     *
     *  The five reordering modes differ only in the *order* they want, never in
     *  what they then do with it. Each computes an index order over the stored
     *  list into one scratch array and hands it to ``AtomList::AssignOrder``,
     *  so the rearranging is written once and a mode is only its own
     *  arithmetic. That is also what keeps them allocation-free: the order
     *  array, and the scratch the sort merges through, are fixed members sized
     *  at ``AtomList::MAX_ATOMS`` when the object is built.
     *
     *  ### Indices are 1-based, everywhere, on purpose
     *
     *  ``nth`` already takes Max's 1-based index, and ``swap``, ``indexmap``
     *  and the map ``sort`` publishes all follow it. One numbering across the
     *  object matters more than matching Max mode by mode, because the modes
     *  are meant to **compose**: ``sort``'s right outlet is an index map, and
     *  the whole point of publishing it is that it can be sent to an
     *  ``indexmap`` to put a *parallel* list — the durations beside the
     *  pitches — into the same new order. A map that came out 1-based and went
     *  back in 0-based would make the object's headline idiom silently wrong.
     *
     *  An index naming no item is refused rather than clamped, and the two
     *  modes that take several refuse differently because they ask
     *  differently. ``indexmap`` is elementwise — a list of independent picks —
     *  so a bad index drops its own element and the rest still arrive.
     *  ``swap`` is one exchange between two named places, so a bad index means
     *  the exchange asked for cannot be made and **nothing** is sent, which is
     *  the answer ``nth`` already gives to an index naming no item.
     *
     *  ### One object with a mode, not thirty objects
     *
     *  Max ships both spellings: ``zl rev`` and ``zl.rev``, and its own
     *  reference describes the named variants as the same object with the mode
     *  fixed. **Only ``.zl <mode>`` is ported**, deliberately:
     *
     *  - Thirty registered names for one class is thirty entries in the
     *    registry, thirty full ``ADD_DESCRIPTION`` / ``INLET_DOC`` /
     *    ``OUTLET_DOC`` / ``PARAM_DOC`` sets and thirty rows in the published
     *    metadata, for no behaviour that ``.zl <mode>`` does not already have.
     *  - The sugar and the object disagree about the ``mode`` message. A
     *    ``.zl.rev`` that can be told ``mode nth`` is not a ``.zl.rev``, and
     *    one that refuses the message is a second, subtly different object
     *    rather than a spelling.
     *
     *  So the mode is a creation argument and a run-time message, and nothing
     *  else. The neighbouring list objects — ``.pack`` (#517), ``.pak``
     *  (#518), ``.unpack`` (#519) — stay separate objects, because their port
     *  *shape* differs rather than only their behaviour.
     *
     *  ### The stored list, and what a bang re-runs
     *
     *  ``.zl`` keeps the last list that arrived at its left inlet, and a
     *  **bang** runs the current mode over it again. That is what makes
     *  ``mode <name>`` useful at run time — change the mode, bang, and the
     *  same list comes back processed the other way — and it is what
     *  ``zlclear`` clears.
     *
     *  It is the *input* register and nothing more. The stateful modes Max has
     *  (``reg``, ``queue``, ``stack``, ``group``, ``stream``) accumulate on
     *  their own terms and belong to their own issue; this list is simply what
     *  arrived most recently.
     *
     *  ### Bounded storage
     *
     *  ``AtomList`` (see ``pAtomList.h``), which is the shared bounded list
     *  this issue settles for the whole family: at most ``AtomList::MAX_ATOMS``
     *  (256, Max's own default maximum length) atoms spanning at most
     *  ``AtomList::TEXT_CAPACITY`` (1024) characters, in storage reserved when
     *  the object is built. Two of them — the stored list and a scratch copy
     *  the reordering modes work on, so processing never destroys what
     *  arrived.
     *
     *  Max's maximum length is settable, and so is this one, *downwards*: a
     *  leading integer creation argument or the ``zlmaxsize <n>`` message
     *  narrows the working limit to anywhere in 1..256. It cannot be widened
     *  past 256, because the storage behind it was allocated once and an
     *  object that could grow it would allocate on whichever thread sent the
     *  message.
     *
     *  An atom that does not fit is **refused and counted** (``Dropped()``),
     *  never silently truncated and never logged from a message handler —
     *  formatting a log line builds a ``std::string`` on a thread that may be
     *  the audio callback, which is the objection ``.thresh``, ``.bondo`` and
     *  ``.combine`` all record. An over-long list therefore loses its **tail**
     *  and keeps its head. A creation argument that overflows *is* logged,
     *  parameter parsing being control-thread only by construction —
     *  ``.combine``'s split between the two routes.
     *
     *  ### What comes out
     *
     *  The family's transport convention, shared as ``SendAtom`` /
     *  ``SendAtoms`` in ``pAtomList.h``: a result of **one** atom leaves as the
     *  int, float or symbol it spells, and a longer one as list text. A list
     *  of one atom is not a list in Max, and this patcher does no coercion at
     *  an inlet, so an object that always retyped a single-atom result to a
     *  list would stop it reaching the ``.i`` a patch wired it to. A result of
     *  **no** atoms sends nothing at all rather than an empty message, which is
     *  the ``.sprintf`` / ``.prepend`` rule that keeps an unconfigured object
     *  safe to drop into a working patch.
     *
     *  Where a mode fills both outlets the **right one is sent first**, Max's
     *  right-to-left rule and ``.trigger``'s.
     *
     *  ### Live SetParams (#234)
     *
     *  The creation arguments are a ``LIST`` parameter, so
     *  ``Parameters::NeedsRebuild()`` is true and a live re-parse takes the
     *  structural-replacement route rather than patching fields. That is the
     *  right answer rather than a limitation: re-typing an object's arguments
     *  in Max recreates it and loses its contents, and this reproduces that
     *  exactly — the rebuilt object comes back with the new mode and an empty
     *  stored list. The ``mode <name>`` message is the other half of the
     *  contract and does *keep* the list, which is what a patch changing modes
     *  live actually wants.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven entirely by its
     *  inlets, and one that emitted would re-send on every DSP tick from a
     *  stimulus no patch sent.
     *
     *  No message path allocates, locks or blocks. The command words are
     *  matched against the message in place (a ``substr`` would allocate), the
     *  atoms are parsed by ``pListArgs.h``'s allocation-free readers into
     *  storage reserved in the constructor, numbers are rendered by
     *  ``ExprFormatValue`` into a stack buffer, and the result is built in a
     *  render buffer reserved at the same time. The dispatch itself is one
     *  relaxed atomic load and a ``switch``.
     *
     *  Two threads sending to the same ``.zl`` genuinely race for the stored
     *  list, so a single test-and-set guard serialises them: a message that
     *  finds the object busy is **dropped and counted** rather than made to
     *  spin, this being a path the audio callback takes. The same guard is
     *  what stops an object wired back into its own inlet from recursing on
     *  the audio thread — the trap ``.pipe``'s header sets out.
     */
    PATCHER_CLASS(gZl, YSE::OBJ::G_ZL)
    _NO_MESSAGES
    _NO_CALCULATE
    _PARM_CLEAR
    _PARM_PARSE

    _BANG_IN(Again)
    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    /**
     *  @brief The modes this object answers to.
     *
     *  ``NONE`` is what a ``.zl`` with no recognised mode word has: it stores
     *  what it is sent and emits nothing. Max's undocumented no-argument
     *  default is ``reg``, which belongs to the register group and is not
     *  ported yet; behaving as a mode the patch did not ask for would be worse
     *  than staying quiet, so an unconfigured ``.zl`` is inert.
     *
     *  The rest of Max's vocabulary — ``change compare delace ecils group iter
     *  join lace lookup median mth queue reg sect slice stack stream sub sum
     *  thin union unique`` — arrives with its own issues. A word this object
     *  does not know leaves the mode where it was, which is ``.translate``'s
     *  answer to the same question.
     */
    enum class Mode {
      NONE,
      LEN,
      REV,
      NTH,
      // The reordering group (#524). Every one of these produces a permutation
      // — or, for `indexmap`, a re-selection — of the stored list, and every
      // one of them goes out the left outlet through the same path.
      ROT,
      SCRAMBLE,
      SORT,
      SWAP,
      INDEXMAP,
    };

    /** @brief The mode in force right now. Readable from any thread. */
    Mode CurrentMode() const {
      return (Mode)mode.load(std::memory_order_relaxed);
    }

    /**
     *  @brief The working maximum list length, in atoms — Max's
     *         ``zlmaxsize``, always within 1..``AtomList::MAX_ATOMS``.
     *
     *  Clamped on read rather than on write, which is ``.thresh``'s and
     *  ``.metro``'s arrangement and for their reason: a live ``SetParams``
     *  re-parse writes straight into the field and notifies nobody, so a clamp
     *  applied at the inlet would not cover that route.
     */
    std::size_t Limit() const;

    /**
     *  @brief The mode's argument, from the right inlet or the creation
     *         arguments — for ``nth``, the **1-based** index Max uses.
     *
     *  The *first* number of the argument, which is the whole of it for every
     *  mode that takes one number. ``swap`` and ``indexmap`` take several; see
     *  ``ArgumentCount``.
     */
    int Argument() const {
      return argument.load(std::memory_order_relaxed);
    }

    /**
     *  @brief How many numbers the mode's argument holds (issue #524).
     *
     *  ``rot``, ``sort`` and ``nth`` read one number and this is 1; ``swap``
     *  reads two; ``indexmap`` reads as many as it is given, up to
     *  ``AtomList::MAX_ATOMS``. ``len``, ``rev`` and ``scramble`` read none.
     *
     *  The argument is one list rather than one number because two of the
     *  modes need it to be: an index map is a list by definition, and a swap
     *  names two places. Keeping ``Argument()`` as its first element is what
     *  makes that a widening rather than a change — an ``.zl nth`` wired to a
     *  number sees exactly what it saw before.
     */
    std::size_t ArgumentCount() const {
      return arguments;
    }

    /** @brief Number @p index of the mode's argument, or 0 past the end.
     *         Diagnostics and tests; the modes read the array directly under
     *         the guard. */
    int ArgumentAt(std::size_t index) const {
      if (index >= arguments) return 0;
      return argumentList[index];
    }

    /**
     *  @brief How many draws ``scramble`` has taken since the last seeding.
     *
     *  Exists so a test can pin *how often* the object draws — one draw per
     *  item moved is what makes a seeded shuffle replayable, and it is the kind
     *  of property a refactor breaks silently. ``.urn``'s ``Draws()``, for
     *  ``.urn``'s reason.
     */
    UInt Draws() const {
      return rng.Draws();
    }

    /** @brief How many atoms the stored list holds. Diagnostics / tests. */
    std::size_t Stored() const {
      return stored.Size();
    }

    /**
     *  @brief Atoms refused so far, plus messages dropped because another
     *         thread held the object.
     *
     *  The list was longer than ``Limit()``, its text was longer than the
     *  buffer, or a second message arrived while one was being processed.
     *  Monotonic, readable from any thread, and the object's overflow report —
     *  a counter rather than a log line because the refusing thread may be the
     *  audio callback.
     */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /**
     *  @brief The mode the @p length characters at @p text name, or false.
     *
     *  Strict and allocation-free: the whole token must be the word. Public so
     *  the tests can pin the vocabulary without going through a message.
     */
    static bool ReadMode(const char* text, std::size_t length, Mode& out);

    /** @brief The word @p mode is spelled as, or ``""`` for ``NONE``. */
    static const char* ModeName(Mode mode);

  private:
    // Run the current mode over the stored list and send the result. Called
    // with the guard held, so the lists cannot move under it.
    void Run(YSE::THREAD thread);

    // Take the guard, or count a drop and answer false. The loser of a race
    // and a feedback loop take the same route — see the class notes.
    bool Enter();
    void Leave();

    // Replace the stored list with the atoms of `text` and run the mode.
    void Take(const char* text, std::size_t length, YSE::THREAD thread);

    // A bare number is a list of one, which is what it is in Max.
    void TakeInt(int value, YSE::THREAD thread);
    void TakeFloat(float value, YSE::THREAD thread);

    // Max's command words on the left inlet: `mode <name>`, `zlclear`,
    // `zlmaxsize <n>` and `zlseed <n>`. True when the message was one of them
    // and so was not data. Matched against the leading token in place.
    bool Command(const std::string& value, std::size_t begin, std::size_t end);

    // Replace the mode's argument with every number in the `length` characters
    // at `text`. Guarded, because the argument is an array rather than one
    // atomic word — see the note on `argumentList`.
    void TakeArguments(const char* text, std::size_t length);

    // The mode's argument as a single number: what an int or a float on the
    // right inlet means, and the shape every mode but `swap` and `indexmap`
    // reads.
    void TakeArgument(int value);

    // ─── the reordering modes (#524) ──────────────────────────────────────────
    // Each fills `order` with an index order over the stored list and answers
    // how many entries it wrote; 0 means "nothing to send". All are called from
    // Run(), so the guard is held and the lists cannot move underneath them.

    std::size_t OrderRotate(std::size_t size);
    std::size_t OrderScramble(std::size_t size);
    std::size_t OrderSort(std::size_t size);
    std::size_t OrderSwap(std::size_t size);
    std::size_t OrderIndexMap(std::size_t size);

    // Strictly "stored atom `a` sorts before stored atom `b`". Numbers come
    // before symbols in both directions — the number/symbol split is a type
    // ordering rather than a value one — numbers compare by value and symbols
    // by their characters.
    bool SortsBefore(std::size_t a, std::size_t b, bool descending) const;

    // Send `count` entries of `order` applied to the stored list out the left
    // outlet, through the family's transport convention.
    void SendOrdered(std::size_t count, YSE::THREAD thread);

    // One refusal, on the counter Dropped() reports.
    void CountDrop(std::size_t count = 1) {
      dropped.fetch_add((std::uint64_t)count, std::memory_order_relaxed);
    }

    // The creation arguments, whole. One LIST parameter rather than three
    // scalars because the tokens are not positional in the way
    // Parameters::Set is: `.zl nth 2` and `.zl 64 nth 2` are both legal, the
    // leading integer being Max's optional maximum length.
    std::vector<std::string> args;

    // The mode, held as an int because `mode <name>` may arrive on any
    // thread. Mode::NONE until a word says otherwise.
    std::atomic<int> mode{(int)Mode::NONE};

    // Max's zlmaxsize. Unclamped, since Limit() applies the range.
    std::atomic<int> limit{(int)AtomList::MAX_ATOMS};

    // The mode's argument — nth's 1-based index, rot's places, sort's
    // direction. The first number of `argumentList`, kept as its own atomic
    // word because it is the whole argument for every mode that takes one
    // number and a lock-free read is what a Run() on the audio thread wants.
    std::atomic<int> argument{0};

    // The mode's argument in full: `swap`'s two indices, `indexmap`'s map
    // (#524). A plain array rather than an AtomList because indices are all
    // these modes want from it, and 1 KB is a tenth of what a second list would
    // cost. Not atomic and not thread-safe — writes take the same `busy` guard
    // the stored list does, so a right-inlet list arriving while a message is
    // being processed is dropped and counted rather than half-applied.
    int argumentList[AtomList::MAX_ATOMS] = {};
    std::size_t arguments = 0;

    // The last list received at the left inlet, and the scratch copy the
    // reordering modes work on so that processing never destroys it. Both
    // reserve their storage in the constructor; neither is thread-safe, which
    // is what `busy` is for.
    AtomList stored;
    AtomList work;

    // Where a result is rendered. Reserved by the constructor to
    // AtomList::RENDER_CAPACITY, so building a result allocates nothing.
    std::string render;

    // The index order a reordering mode computes, and the scratch the sort
    // merges through (#524). Members rather than locals: 1 KB of stack per
    // message on a path the audio callback takes is not a trade worth making,
    // and a fixed member is the only shape that is allocation-free by
    // construction. uint16_t because MAX_ATOMS is 256 — and because 0xFFFF is
    // then free to mean "this entry names no atom", which is how a rejected
    // index reaches AssignOrder without the caller compacting the array first.
    std::uint16_t order[AtomList::MAX_ATOMS] = {};
    std::uint16_t merge[AtomList::MAX_ATOMS] = {};

    // `scramble`'s randomness. Per object and seedable, which is what makes a
    // shuffle reproducible — the whole reason RandomSource exists beside the
    // engine-wide generator; see its header.
    RandomSource rng;

    // The guard. A message that finds it taken is dropped and counted rather
    // than made to spin, this being a path the audio callback takes.
    std::atomic<bool> busy{false};

    std::atomic<std::uint64_t> dropped{0};
  };

} // namespace PATCHER
} // namespace YSE
