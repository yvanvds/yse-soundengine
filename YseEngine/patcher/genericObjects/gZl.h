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
     *         behaviour is chosen by a mode word (issues #523, #524, #525,
     *         #526, #527).
     *
     *  Max: "zl — multi-purpose list processing object". Two inlets, two
     *  outlets, and a mode that decides what happens between them. #523 landed
     *  the shell, the dispatch, the bounded storage model and the three modes
     *  that prove the design: ``len``, ``rev`` and ``nth``. #524 adds the
     *  **reordering** group — ``rot``, ``scramble``, ``sort``, ``swap`` and
     *  ``indexmap``. #525 adds the **extraction** group — ``mth``, ``slice``,
     *  ``sub`` and ``lookup`` — which is the read side of list processing: get
     *  at one item, at a piece, at a position, at a table entry. #526 adds the
     *  **set** group — ``sect``, ``union``, ``unique``, ``thin``, ``filter``,
     *  ``compare`` and ``change`` — which is the object read as a *set* rather
     *  than as a sequence. #527 adds the **structural** group — ``group``,
     *  ``iter``, ``join``, ``lace``, ``delace``, ``ecils``, ``stream``,
     *  ``queue``, ``stack`` and ``reg`` — which is where a list and a *stream*
     *  meet. The remaining mode groups follow in their own issues.
     *
     *  ### The structural group, and the second store it needed
     *
     *  Every mode before #527 was a function of two lists: what arrived, what
     *  the right inlet holds, and an answer sent straight back out. Six of the
     *  ten added here still are — ``iter`` cuts one list into chunks, ``join``
     *  and ``lace`` combine two, ``delace`` and ``ecils`` cut one in two, and
     *  ``reg`` is the identity. The other four are not, and that is the whole
     *  of what this group adds to the object: ``group``, ``stream``, ``queue``
     *  and ``stack`` **accumulate across messages**, so what they answer
     *  depends on what came before rather than only on what just arrived.
     *
     *  They share **one** accumulator rather than owning four, and not only to
     *  save three kilobytes apiece. All four hold the same thing — atoms in
     *  arrival order, waiting to be consumed from one end — and differ only in
     *  when they consume and from which end: ``group`` takes N off the front as
     *  soon as N are there, ``stream`` drops off the front to keep the last N,
     *  ``queue`` takes one off the front on a bang and ``stack`` one off the
     *  back. Sharing the store is what makes ``mode queue`` → ``mode stack``
     *  mid-stream mean the obvious thing rather than silently switching to a
     *  second buffer holding older material. ``zlclear`` empties it, which is
     *  Max's "reinitializes the zl object".
     *
     *  Consuming from an end is also the first thing here that **shortens** a
     *  list rather than rebuilding it, and ``AtomList``'s text is append-only:
     *  a ``queue`` popped a thousand times would walk off ``TEXT_CAPACITY``
     *  and start refusing atoms it has room for. ``AtomList::Keep`` is the
     *  answer — it moves the retained characters down in place, which is
     *  possible in one pass because a retained atom's new offset is never past
     *  its old one, and allocates nothing.
     *
     *  ### A bang is not an arrival, and for these four it never was
     *
     *  ``.zl``'s bang has always meant "run the current mode over what the
     *  object holds again". For the nineteen stateless modes that is the stored
     *  input list and a bang is indistinguishable from re-sending it. For the
     *  accumulating four it is *not*: re-sending a list into a ``queue`` pushes
     *  it a second time, and popping is exactly what a bang has to do instead.
     *  So ``Run`` is told which of the two stimuli it is answering, and the
     *  four read it — a bang pops the ``queue`` and the ``stack``, flushes
     *  ``group``'s partial group, and re-sends ``stream``'s window without
     *  sliding it. Nothing else in the object looks at it, so the rule the
     *  other nineteen document is unchanged.
     *
     *  ### ``reg`` is the one mode that reads the right inlet as its *contents*
     *
     *  Max: "a list received in the left inlet is sent out the left outlet
     *  immediately. A list received in the right inlet is stored. A bang sends
     *  the stored list out the left outlet." So ``reg``'s right inlet does not
     *  carry an argument at all — it carries the register's contents — and it
     *  is therefore the mirror of ``change`` (#526), which is the one mode that
     *  *writes* to the right inlet's list. Both are implemented the same way:
     *  the shared right-inlet path fills the argument list as it does for every
     *  mode, and the mode then copies across, so the two stores can never
     *  disagree about what last arrived.
     *
     *  ``reg`` being ported does **not** make it the no-argument default, which
     *  it is in Max. The reason an unconfigured ``.zl`` stays inert never was
     *  that ``reg`` was missing: an object that echoed everything sent to it
     *  because its mode word was misspelled is harder to debug than one that
     *  says nothing, and ``mode reg`` is one word away.
     *
     *  ### The set group, and the one ordering primitive underneath it
     *
     *  Five of the seven ask the same question — "is this atom one of those?" —
     *  and the sixth and seventh ask "are these two lists the same?". The naive
     *  answer to the first is a nested scan, and on two full-length lists that
     *  is sixty-five thousand atom comparisons on a path the audio callback
     *  takes: the number that already bought ``sub`` its Knuth-Morris-Pratt
     *  walk and ``sort`` its merge sort. So membership goes the same way the
     *  rest of the object does — through an **ordering**. Each list's indices
     *  are merge-sorted into a fixed member array and membership is a binary
     *  search, which is O(n log n + m log m) whatever the data and about four
     *  thousand comparisons at full length.
     *
     *  The sort is the one ``sort`` mode already uses, lifted to work on any
     *  ``AtomList`` rather than only the stored one, and its **stability** is
     *  load-bearing a second time: equal atoms come out contiguous *and* in
     *  their original order, so the first entry of each run is the earliest
     *  occurrence in the input. That is the whole of ``thin``, and it is what
     *  makes ``sect`` and ``union`` produce their result in the order a patch
     *  sent it rather than in sorted order.
     *
     *  **Sets are sets, and filters are filters.** ``sect``, ``union`` and
     *  ``thin`` are named for set operations, so their results are sets: each
     *  atom appears once, at the position of its first occurrence. ``unique``
     *  and ``filter`` are *removals* from the list — Max: "a list with elements
     *  matching the filtering list removed" — so they keep the list as it
     *  arrived, duplicates and all, minus the atoms that matched. That split is
     *  not a coin toss: ``filter``'s right outlet reports the **positions** of
     *  the atoms that survived, and a position only means something if the
     *  survivors are still where they were.
     *
     *  **``unique`` and ``filter`` select the same atoms**, which is Max's
     *  arrangement rather than this port's — the two reference pages describe
     *  one operation ("items to remove" / "a reference list whose elements will
     *  be excluded"). They differ in what the right outlet says about it:
     *  ``filter`` reports where the survivors were, ``unique`` says nothing.
     *  Both are ported because a patch reaching for either name should find it,
     *  and because ``filter``'s positions are the composable half — what it
     *  reports is what ``nth`` takes, exactly as ``sub``'s positions are.
     *
     *  Note that Max's *summary* line for ``zl.filter`` ("pass only the items
     *  that match") and its *reference* text ("elements matching the filtering
     *  list removed") disagree with each other. The reference text is what is
     *  ported, because the same page's inlet and outlet labels agree with it —
     *  "items to remove", "filtered list" — and because the other reading would
     *  make ``filter`` the exact complement of ``unique`` rather than its twin,
     *  which no Max patch behaves as though it were.
     *
     *  ### ``change`` is the one mode that writes to the right inlet's list
     *
     *  Max's ``zl.change`` compares what arrives against what arrived last, and
     *  its right inlet "receives lists that set the comparison reference". So
     *  the reference *is* the mode's argument, and it lives where every other
     *  mode's argument lives — the right inlet's list — with the difference
     *  that ``change`` updates it itself as lists go by. A bang therefore
     *  answers 0: the stored list has already become the reference, and asking
     *  again whether it changed is asking about the same list twice.
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
     *  ``lookup`` (#525) joins them, because it *is* one of them wearing the
     *  other hat: ``indexmap`` holds the data on the left and takes the map on
     *  the right, ``lookup`` holds the table on the right and takes the indices
     *  on the left. Same computation, the two lists swapped — so ``lookup``
     *  computes an order too and goes out through the same path, with the
     *  right-inlet list as the source rather than the stored one.
     *
     *  ### Indices are 1-based, everywhere, on purpose — and ``mth`` is why
     *      that costs nothing
     *
     *  ``nth`` already takes Max's 1-based index, and ``swap``, ``indexmap``,
     *  ``lookup``, the positions ``sub`` reports and the map ``sort`` publishes
     *  all follow it. One numbering across the object matters more than
     *  matching Max mode by mode, because the modes are meant to **compose**:
     *  ``sort``'s right outlet is an index map, and the whole point of
     *  publishing it is that it can be sent to an ``indexmap`` to put a
     *  *parallel* list — the durations beside the pitches — into the same new
     *  order. ``sub`` finds *where*, and what it reports is exactly what
     *  ``nth`` takes. A map that came out 1-based and went back in 0-based
     *  would make the object's headline idiom silently wrong.
     *
     *  Max numbers ``indexmap``, ``swap`` and ``lookup`` from 0 and ``nth``
     *  from 1, so *something* has to give. What makes 1 the safe choice here is
     *  **``mth``**: Max defines it as "exactly like nth, except the list index
     *  numbering begins with 0", so 0-based picking is not a convention this
     *  port is taking away — it is a mode with a name. A patch that thinks in
     *  0-based indices asks for ``mth`` and gets it, and every *other* mode
     *  agrees with every other mode. Porting ``mth`` as anything but 0-based
     *  would be porting a second spelling of ``nth``, which is why it is the
     *  single documented exception rather than a slip.
     *
     *  An index naming no item is refused rather than clamped, and the modes
     *  that take several refuse differently because they ask differently.
     *  ``indexmap`` and ``lookup`` are elementwise — a list of independent
     *  picks — so a bad index drops its own element and the rest still arrive.
     *  ``swap`` is one exchange between two named places, so a bad index means
     *  the exchange asked for cannot be made and **nothing** is sent, which is
     *  the answer ``nth`` and ``mth`` already give to an index naming no item.
     *  ``slice`` asks for neither: its argument is a *count* rather than an
     *  index, so it has nothing to be off by one about and is clamped to the
     *  list — a count of 0 puts everything out the right outlet and a count
     *  past the end puts everything out the left one.
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
     *  It is the *input* register and nothing more. The accumulating modes
     *  (#527 — ``group``, ``stream``, ``queue``, ``stack``) collect into a
     *  store of their own and read this one only as "the atoms that just
     *  arrived"; ``reg`` is the mode for which the two coincide.
     *
     *  ### Bounded storage
     *
     *  ``AtomList`` (see ``pAtomList.h``), which is the shared bounded list
     *  this issue settles for the whole family: at most ``AtomList::MAX_ATOMS``
     *  (256, Max's own default maximum length) atoms spanning at most
     *  ``AtomList::TEXT_CAPACITY`` (1024) characters, in storage reserved when
     *  the object is built. Four of them — the stored list, a scratch copy the
     *  reordering modes work on so that processing never destroys what arrived,
     *  the right inlet's list, and the accumulator the four collecting modes
     *  share (#527).
     *
     *  That third one is #525's addition, and it is what the right inlet always
     *  meant. Until now the cold inlet carried *numbers* — an index, a count, a
     *  direction — so an ``int`` array was the whole of it. ``sub`` takes a
     *  **pattern** to search for and ``lookup`` takes a **table** to read from,
     *  and neither is a list of numbers: a pattern of note names or a table of
     *  sample names is exactly what a patch will send. So the right inlet's
     *  list is kept as atoms as well, and the numeric array stays beside it as
     *  the reading the index modes want. One arrival fills both, under the same
     *  guard, so the two can never disagree about what was last sent.
     *
     *  They are refused differently, and on purpose. A right-inlet list with no
     *  numbers in it **leaves the numeric argument standing** — a cord that
     *  delivers the occasional symbol should not silently un-point a ``swap`` —
     *  while the atom list is simply replaced, because for ``sub`` and
     *  ``lookup`` a list of symbols is not a malformed argument but the
     *  ordinary one.
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
     *  default is ``reg``, which *is* ported (#527) and is still not the
     *  default: behaving as a mode the patch did not ask for would be worse
     *  than staying quiet, so an unconfigured ``.zl`` is inert.
     *
     *  The rest of Max's vocabulary — ``median`` and ``sum`` — arrives with its
     *  own issue. A word this object does not know leaves the mode where it
     *  was, which is ``.translate``'s answer to the same question.
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
      // The extraction group (#525) — the read side of list processing: one
      // item (`mth`), a piece (`slice`), a position (`sub`), a table entry
      // (`lookup`).
      MTH,
      SLICE,
      SUB,
      LOOKUP,
      // The set group (#526) — the list read as a set rather than as a
      // sequence: what two lists share (`sect`), what they add up to (`union`),
      // what one has that the other has not (`unique`, `filter`), what a list
      // has more than once (`thin`), and whether two lists are the same at all
      // (`compare`, `change`).
      SECT,
      UNION,
      UNIQUE,
      THIN,
      FILTER,
      COMPARE,
      CHANGE,
      // The structural group (#527) — where a list and a stream meet. Six of
      // them restructure whatever arrives: `iter` cuts one list into chunks,
      // `join` and `lace` combine two, `delace` and `ecils` cut one in two, and
      // `reg` holds one. The last four accumulate across messages instead, and
      // are the only modes in the object whose answer depends on what came
      // before: `group`, `stream`, `queue`, `stack`.
      ITER,
      JOIN,
      LACE,
      DELACE,
      ECILS,
      REG,
      GROUP,
      STREAM,
      QUEUE,
      STACK,
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
     *  ``rot``, ``sort``, ``nth``, ``mth`` and ``slice`` read one number and
     *  this is 1; ``swap`` reads two; ``indexmap`` reads as many as it is
     *  given, up to ``AtomList::MAX_ATOMS``. ``len``, ``rev`` and ``scramble``
     *  read none, and ``sub`` and ``lookup`` read the right inlet as atoms
     *  rather than as numbers — see ``ArgumentAtoms``.
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
     *  @brief How many atoms the right inlet's list holds — ``sub``'s pattern
     *         and ``lookup``'s table (issue #525).
     *
     *  The same list ``ArgumentCount`` counts the *numbers* of, so the two
     *  differ exactly when the right inlet carried something that is not a
     *  number. Diagnostics and tests; the modes read the list directly under
     *  the guard.
     */
    std::size_t ArgumentAtoms() const {
      return argumentAtoms.Size();
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
     *  @brief How many atoms the accumulating modes are holding (issue #527).
     *
     *  The one store in the object that survives a message — ``group``'s
     *  partial group, ``stream``'s window, the ``queue`` and the ``stack``.
     *  0 for every other mode, which never puts anything in it. Diagnostics
     *  and tests; the modes read the list directly under the guard.
     */
    std::size_t Pending() const {
      return pending.Size();
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
    /**
     *  @brief What set the current mode running (issue #527).
     *
     *  A named pair rather than a bool because it is a *distinction* rather
     *  than a flag, and one only four of the twenty-nine modes make: for the
     *  accumulating group a bang consumes what is held where an arrival adds
     *  to it, so `Run(BANG)` and `Run(ARRIVAL)` are two different operations
     *  rather than the same one run twice. See the class notes.
     */
    enum class Trigger { ARRIVAL, BANG };

    // Run the current mode over the stored list and send the result. Called
    // with the guard held, so the lists cannot move under it.
    void Run(YSE::THREAD thread, Trigger trigger);

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

    // Replace the mode's argument with the `length` characters at `text`, both
    // as atoms and as the numbers among them. Guarded, because the argument is
    // an array and a list rather than one atomic word — see the note on
    // `argumentList`.
    void TakeArguments(const char* text, std::size_t length);

    // The mode's argument as a single number: what an int or a float on the
    // right inlet means, and the shape every mode but `swap` and `indexmap`
    // reads.
    void TakeArgument(int value);

    // Re-read `argumentAtoms` as the numeric argument the index modes want
    // (#525), and answer how many numbers were in it. Committed only when that
    // is not 0 — the "a list carrying no numbers leaves the argument standing"
    // rule, which the creation-argument path deliberately overrides.
    std::size_t ReadArgumentNumbers();

    // `ReadArgumentNumbers` with the "leaves the argument standing" rule turned
    // off: the numeric reading becomes whatever the atoms say, empty included.
    // What a full reconfiguration wants — the creation arguments, and `change`
    // replacing its reference (#526) — so that the two readings of the right
    // inlet's list can never drift apart.
    void SetArgumentNumbers();

    // ─── the reordering modes (#524) ──────────────────────────────────────────
    // Each fills `order` with an index order over the stored list and answers
    // how many entries it wrote; 0 means "nothing to send". All are called from
    // Run(), so the guard is held and the lists cannot move underneath them.

    std::size_t OrderRotate(std::size_t size);
    std::size_t OrderScramble(std::size_t size);
    std::size_t OrderSort(std::size_t size);
    std::size_t OrderSwap(std::size_t size);
    std::size_t OrderIndexMap(std::size_t size);

    // ─── the extraction modes (#525) ─────────────────────────────────────────
    // Also called from Run() with the guard held.

    // `lookup`: the stored list read as 1-based indices into the right inlet's
    // list. `indexmap` with the two lists swapped, so it fills `order` like the
    // rest — the source SendOrdered applies it to is the *argument* list.
    std::size_t OrderLookup(std::size_t size);

    // `nth` and `mth`: the item at 0-based position `at`, out the left outlet,
    // and everything else out the right one — right first. An `at` naming no
    // item sends nothing at all from either. Taken as a long long because the
    // 1-based modes reach here as `argument - 1`, and INT_MIN - 1 is not an
    // int.
    void Pick(long long at, YSE::THREAD thread);

    // `slice`: the first `Argument()` items out the left outlet and the rest
    // out the right — right first, which Max says explicitly for this mode.
    void SendSlice(YSE::THREAD thread);

    // `sub`: fill `work` with the 1-based position of every occurrence of the
    // right inlet's list within the stored one, and answer how many there were.
    // Occurrences may overlap.
    std::size_t FindPattern();

    // ─── the set modes (#526) ────────────────────────────────────────────────
    // Also called from Run() with the guard held. The first three fill `order`
    // like the reordering group and leave through `SendOrdered`; the last three
    // build their own answer, because it is not a selection of the stored list.

    // Sort the stored list's indices into `sortedStored`, the right inlet
    // list's into `sortedArgument`, or both. The ordering membership and
    // first-occurrence both go through — see the class notes on why this is a
    // binary search rather than a nested scan.
    void RankStored(std::size_t size);
    void RankArgument();

    // `thin`: the stored list with every repeat after the first dropped.
    std::size_t OrderThin(std::size_t size);

    // `sect`: the atoms the stored list and the right inlet's list share, once
    // each, in the order the stored list has them.
    std::size_t OrderSect(std::size_t size);

    // `unique` and `filter`: the stored list minus the atoms the right inlet's
    // list names. Duplicates and positions survive — see the class notes on why
    // these two are filters rather than set operations.
    std::size_t OrderReject(std::size_t size);

    // `union`: the two lists added together as sets — the stored list thinned,
    // then whatever the right inlet's list has that it does not. Builds `work`
    // atom by atom rather than through `order`, because the result is drawn
    // from *two* lists and `AssignOrder` reorders one.
    void SendUnion(YSE::THREAD thread);

    // `compare`: 1 or 0 out the left outlet, and the 1-based positions at which
    // the two lists differ out the right one when they do.
    void SendCompare(YSE::THREAD thread);

    // `change`: the stored list out the left outlet only when it differs from
    // the reference, 1 or 0 out the right one either way — and the reference
    // then becomes the stored list. The one mode that writes to the right
    // inlet's list; see the class notes.
    void SendChange(YSE::THREAD thread);

    // True when the stored list and the right inlet's list are the same list,
    // atom for atom. Shared by `compare` and `change`, which ask it two ways.
    bool ArgumentMatchesStored() const;

    // `reg` (#527) only: make the right inlet's list the stored one, silently.
    // Max's "a list received in the right inlet is stored", and the mirror of
    // `change` writing the other way. Called from the right-inlet paths with
    // the guard held; a no-op in every other mode, so the shared path does not
    // have to know which mode is in force.
    void StoreRegister();

    // ─── the structural modes (#527) ─────────────────────────────────────────
    // Also called from Run() with the guard held.

    // `iter`: the stored list out the left outlet as a run of chunks of
    // `Argument()` atoms each, the last one short when the list does not
    // divide. Several sends from one stimulus, which the guard already covers —
    // an object wired back into its own inlet is refused mid-walk rather than
    // restarting an iteration that would not terminate.
    void SendIter(YSE::THREAD thread);

    // `join`: the stored list followed by the right inlet's list.
    void SendJoin(YSE::THREAD thread);

    // `lace`: the two lists interleaved, and whatever is left of the longer one
    // appended rather than dropped.
    void SendLace(YSE::THREAD thread);

    // `delace`: the atoms at odd positions out the right outlet and the ones at
    // even positions out the left — right first, `lace` undone.
    void SendDelace(YSE::THREAD thread);

    // `ecils`: `slice` counting from the end. The last `Argument()` atoms out
    // the right outlet and the rest out the left, right first.
    void SendEcils(YSE::THREAD thread);

    // ─── the accumulating modes (#527) ───────────────────────────────────────
    // The four that read `pending`, and the only ones for which a bang and an
    // arrival are different operations.

    // Append the stored list's atoms to `pending`, counting whatever will not
    // fit. What an arrival does for all four.
    void Collect();

    // `group`: emit every complete group of `Argument()` atoms the accumulator
    // now holds and keep the remainder; a bang flushes the partial group.
    void RunGroup(YSE::THREAD thread, Trigger trigger);

    // `stream`: keep the last `Argument()` atoms and send them once there are
    // that many, with the shortfall out the right outlet either way.
    void RunStream(YSE::THREAD thread, Trigger trigger);

    // `queue` and `stack`: an arrival pushes, a bang pops — from the front for
    // the queue and from the back for the stack, which is the whole difference
    // between the two and why they are one function.
    void RunPop(YSE::THREAD thread, Trigger trigger, bool fromBack);

    // The mode's argument read as a length in atoms — `group`'s group size,
    // `stream`'s window, `iter`'s chunk. 0 when it names no length at all,
    // which the modes read as "not configured"; otherwise clamped to Limit(),
    // since a window wider than the accumulator can hold would never fill.
    std::size_t ArgumentLength() const;

    // Send `count` entries of `order` applied to @p source out the left outlet,
    // through the family's transport convention. The source is a parameter
    // rather than always the stored list because `lookup` (#525) orders the
    // *argument* list — it is `indexmap` with the two lists swapped.
    void SendOrdered(const AtomList& source, std::size_t count, YSE::THREAD thread);

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

    // The right inlet's list as atoms (#525) — `sub`'s search pattern and
    // `lookup`'s table, neither of which is a list of numbers. Written by the
    // same arrival that fills `argumentList`, under the same guard, so the
    // numeric reading and the atoms can never disagree about what was last
    // sent. See the class notes on why the two are nevertheless *refused*
    // differently.
    AtomList argumentAtoms;

    // The accumulating modes' store (#527) — `group`'s partial group,
    // `stream`'s window, the `queue` and the `stack`. One list shared by all
    // four rather than one apiece: they hold the same thing and differ only in
    // when and from which end they consume it, so sharing is what makes a live
    // `mode queue` → `mode stack` mean the obvious thing. Emptied by
    // `zlclear`, and by a re-parse of the creation arguments. Not thread-safe,
    // like the rest; `busy` is what covers it.
    AtomList pending;

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

    // `sub`'s Knuth-Morris-Pratt failure function over the search pattern
    // (#525). Its own array rather than a borrowed `merge`, which belongs to
    // the sort and says so. KMP rather than the obvious nested-loop scan for
    // the reason the sort is a merge sort: the scan is O(n*m), which on two
    // full-length lists of repeated atoms is sixty-five thousand comparisons on
    // a path the audio callback takes, and KMP is O(n+m) whatever the data for
    // twenty lines and 512 bytes that are already paid for.
    std::uint16_t failure[AtomList::MAX_ATOMS] = {};

    // The set group's orderings (#526): the stored list's atoms and the right
    // inlet list's atoms, each as an ascending index order over its own list.
    // Two arrays rather than one because `sect` and `union` need both at once —
    // one to answer "is this atom in the other list", the other to answer "is
    // this the first time this atom appears". Their own arrays rather than
    // `order` and `merge`, which are the reordering group's and are in use while
    // a set mode is filling `order` from them. Rebuilt per message rather than
    // cached: a cache would have to be invalidated from three write paths, and
    // rebuilding is the cheap half of the work.
    std::uint16_t sortedStored[AtomList::MAX_ATOMS] = {};
    std::uint16_t sortedArgument[AtomList::MAX_ATOMS] = {};

    // For each atom of whichever list was last ranked, whether it is the
    // *first* occurrence of its value — `thin`'s whole answer, and the
    // deduplication `sect` and `union` apply to theirs. Written from the sorted
    // order, where the stability of the merge sort puts the earliest occurrence
    // of each run first.
    bool firstOccurrence[AtomList::MAX_ATOMS] = {};

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
