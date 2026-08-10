#pragma once
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
     *         behaviour is chosen by a mode word (issue #523).
     *
     *  Max: "zl — multi-purpose list processing object". Two inlets, two
     *  outlets, and a mode that decides what happens between them. This issue
     *  lands the shell, the dispatch, the bounded storage model and three
     *  modes — ``len``, ``rev`` and ``nth`` — enough to prove the design; the
     *  remaining mode groups follow in their own issues.
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
     *  join lace lookup median mth queue reg rot scramble sect slice sort
     *  stack stream sub sum thin union unique`` — arrives with its own issues.
     *  A word this object does not know leaves the mode where it was, which is
     *  ``.translate``'s answer to the same question.
     */
    enum class Mode {
      NONE,
      LEN,
      REV,
      NTH,
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
     */
    int Argument() const {
      return argument.load(std::memory_order_relaxed);
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

    // Max's command words on the left inlet: `mode <name>`, `zlclear` and
    // `zlmaxsize <n>`. True when the message was one of them and so was not
    // data. Matched against the leading token in place.
    bool Command(const std::string& value, std::size_t begin, std::size_t end);

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

    // The mode's argument — nth's 1-based index. Written by the right inlet
    // and by the creation arguments, read by Run().
    std::atomic<int> argument{0};

    // The last list received at the left inlet, and the scratch copy the
    // reordering modes work on so that processing never destroys it. Both
    // reserve their storage in the constructor; neither is thread-safe, which
    // is what `busy` is for.
    AtomList stored;
    AtomList work;

    // Where a result is rendered. Reserved by the constructor to
    // AtomList::RENDER_CAPACITY, so building a result allocates nothing.
    std::string render;

    // The guard. A message that finds it taken is dropped and counted rather
    // than made to spin, this being a path the audio callback takes.
    std::atomic<bool> busy{false};

    std::atomic<std::uint64_t> dropped{0};
  };

} // namespace PATCHER
} // namespace YSE
