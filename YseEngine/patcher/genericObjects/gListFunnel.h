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
     *  @brief Index a list's elements and send them out as ``index element``
     *         pairs — ``.listfunnel`` (issue #522).
     *
     *  Max's ``listfunnel``: "Outputs the elements of an incoming list in the
     *  format: [index] [element] for each element of the list."
     *
     *  ### The list side of ``.funnel``
     *
     *  ``.funnel`` (#480) takes the tag from the *wiring* — a value arriving at
     *  inlet 2 leaves as ``2 <value>`` — which means a patch that wants eight
     *  numbered sources has to run eight cords into it. This takes the tag from
     *  the *position in the message* instead, so one cord carrying ``a b c``
     *  becomes ``0 a``, ``1 b``, ``2 c``. Same output shape, same downstream
     *  consumers (``.route``, ``.sel``, ``.spray``), different source of the
     *  number — and the whole point of it is that the list's length need not be
     *  known when the patch is drawn.
     *
     *  Max's own note is that it "conveniently replaces" an ``unpack`` into a
     *  ``funnel``, and the replacement is worth having for exactly that reason:
     *  a ``.unpack``/``.funnel`` pair has to be told how many elements there are
     *  in two places at once, and a list one element longer than the patch
     *  expects silently loses its tail. Here nothing is fixed at creation.
     *
     *  ### Next to ``.iter``
     *
     *  ``.iter`` (#521) is the same walk with the index left off: both turn one
     *  arriving message into one message per element, down one cord, first to
     *  last, each send completing in full before the next begins. The difference
     *  is what a downstream object can do with the result. ``.iter`` sends the
     *  element *as* the int, float or symbol it spells, so it reaches the
     *  ordinary scalar inlets; this always sends a **two-element list**, so the
     *  element is addressed rather than merely delivered. A patch that wants "do
     *  something to every element" reaches for ``.iter``; a patch that wants "put
     *  element k somewhere that depends on k" reaches for this.
     *
     *  That is also why there is no ``bang`` here where ``.iter`` has one. Max
     *  documents no ``bang`` method for ``listfunnel``, and it follows from the
     *  object rather than being an omission: ``iter``'s bang re-sends "the number
     *  or list most recently received", which means ``iter`` holds a message as
     *  *state*, while this object holds nothing between messages but its offset.
     *  The inlet therefore declines ``bang`` outright rather than accepting it
     *  and doing nothing, so ``GetAcceptedTypes()`` reports the real contract —
     *  ``.spray``'s reasoning for declining the types Max has no method for.
     *
     *  ### The offset
     *
     *  Max: the creation argument specifies "a starting index value", and the
     *  ``offset`` message "is used to specify an offset for the first index
     *  value". One value from two directions, as on ``.funnel`` and ``.spray``,
     *  so it is one field here rather than two mechanisms. Element *k* leaves
     *  tagged ``offset + k``.
     *
     *  It exists because the numbers a downstream object expects usually start
     *  where *it* starts — a ``.spray`` bank with its own offset, MIDI channels
     *  at 1, a table's rows at some base — and the alternative is a ``.+`` on
     *  every element after the split, which has to be kept in step by hand. A
     *  ``.listfunnel n`` into a ``.spray`` with the same offset sends element *k*
     *  to outlet *k*, which is the pairing the issue names.
     *
     *  Run-time state, as ``.funnel``'s and ``.spray``'s offsets are: the
     *  ``offset`` message does not write back to the parameters, so a saved patch
     *  carries the creation argument rather than whatever the last message set.
     *  The addition is saturated at the ``int`` limits rather than allowed to
     *  wrap, because a wrapped index reads as a perfectly plausible one to
     *  whatever consumes it.
     *
     *  ### Bounded storage, shared with the rest of the list family
     *
     *  The arriving message is read into an ``AtomList`` (see ``pAtomList.h``),
     *  the bounded pre-allocated storage ``.zl`` settled in #523: at most
     *  ``AtomList::MAX_ATOMS`` (256) atoms spanning at most
     *  ``AtomList::TEXT_CAPACITY`` (1024) characters between them, in memory
     *  reserved when the object is built.
     *
     *  An over-long list loses its **tail**, which is counted on ``Dropped()``,
     *  and the head is indexed. That is ``.zl``'s, ``.unjoin``'s and ``.iter``'s
     *  side of the family's two-sided overflow rule rather than ``.join``'s
     *  refuse-whole, and the difference is what is at stake: the surplus here is
     *  input the patch has just sent, not state the object's other inlets are
     *  holding. A counter rather than a log line, because formatting one builds a
     *  ``std::string`` on a thread that may be the audio callback.
     *
     *  ### The message budget
     *
     *  One arriving message becomes **as many messages as the list is long**, all
     *  inside the call frame of the one ``inlet::Set*`` that started it — the
     *  same budget ``.iter`` documents, and bounded by the same number that
     *  bounds the storage: at most 256 pairs per stimulus. That is a bound on the
     *  *work*, not a promise the work fits an audio block. The number worth
     *  knowing beside it is that the patcher's value-command queue (#225) is 256
     *  deep, so a full-length burst feeding a ``.s`` from the control thread can
     *  saturate it and hit its documented drop-and-log backpressure.
     *
     *  ### What comes out
     *
     *  Always a two-element list, so the outlet is typed ``LIST`` rather than
     *  ``ANY``: there is no arity at which this object sends a bare atom, which
     *  is precisely how it differs from ``.iter``. The index is always an int;
     *  the element keeps the spelling it arrived with, character for character,
     *  because it is copied out of the ``AtomList``'s backing text rather than
     *  re-formatted. A message with no elements sends nothing at all rather than
     *  an empty message — the ``.sprintf`` / ``.prepend`` rule the family shares.
     *
     *  An ``int`` or a ``float`` is a one-element list, as everywhere in this
     *  family, so it leaves as ``<offset> <value>`` — Max's "the low index value
     *  and the received number are sent out as a two-element list". ``anything``
     *  is a list, so there is no message word to strip beyond the ``offset``
     *  method itself.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlet, and one
     *  that emitted would re-index the last message on every DSP tick from a
     *  stimulus no patch sent.
     *
     *  No message path allocates, locks or blocks. Numbers are rendered by
     *  ``ExprFormatValue`` into a stack buffer, the message is collected into an
     *  ``AtomList`` reserved by the constructor, the index is written by
     *  ``WriteInt`` into a stack buffer, and the pair is assembled in a
     *  ``std::string`` reserved to ``AtomList::RENDER_CAPACITY`` at construction.
     *
     *  Two threads sending to the same object are serialised by a single
     *  test-and-set guard whose loser is **dropped and counted** rather than made
     *  to spin, this being a path the audio callback takes. As on ``.iter``, the
     *  guard is held across the *whole* walk deliberately: this object emits in a
     *  loop, so a cord from its outlet back to its inlet re-enters the handler
     *  from inside the walk, and letting that through would both restart an
     *  iteration that never terminates and rewrite the list being walked. The
     *  ``offset`` message takes the same guard, which is what makes the index
     *  base fixed for the duration of a walk rather than able to change halfway
     *  down a list.
     */
    PATCHER_CLASS(gListFunnel, YSE::OBJ::G_LISTFUNNEL)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(FunnelInt)
    _FLOAT_IN(FunnelFloat)
    _LIST_IN(FunnelList)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Most elements one message can be broken into, and so most sends
     *         one stimulus can cost.
     *
     *  ``AtomList::MAX_ATOMS`` (256), because the elements *are* an
     *  ``AtomList``: one bound rather than a second one beside it.
     */
    static constexpr std::size_t MAX_ITEMS = AtomList::MAX_ATOMS;

    /**
     *  @brief The number added to an element's position to make the index it is
     *         sent with.
     *
     *  Max's creation argument and the ``offset`` message, which are the same
     *  value. Run-time state once the object exists, so a saved patch carries
     *  the argument rather than the last message.
     */
    int Offset() const {
      return offset;
    }

    /** @brief The index element @p item is sent with — @p item plus the offset,
     *         saturated at the ``int`` limits. */
    int IndexFor(std::size_t item) const;

    /** @brief How many elements the last message was broken into. 0 before
     *         anything has arrived, and 0 after a message with no elements. */
    std::size_t Held() const {
      return list.Size();
    }

    /**
     *  @brief The last message's elements as they stand, rendered —
     *         diagnostics and tests.
     *
     *  Builds a ``std::string``, so it is **not** for a message path; the
     *  object's own sends use the buffer reserved at construction.
     */
    std::string Stored() const;

    /**
     *  @brief Elements refused so far, plus messages dropped because the object
     *         was already busy.
     *
     *  Counts the tail of a list too long for the bounded storage, and a message
     *  that arrived while another thread — or a cord looping back from this
     *  object's own outlet — held the object mid-walk. Monotonic, readable from
     *  any thread, and the object's refusal report: a counter rather than a log
     *  line, because the refusing thread may be the audio callback.
     */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

  private:
    // Take the guard, or count a drop and answer false. The loser of a race and
    // a feedback loop take the same route — see the class notes on why the
    // guard spans the whole walk.
    bool Enter();
    void Leave();

    // One refusal, on the counter Dropped() reports.
    void CountDrop(std::size_t count = 1);

    // Re-read `offset` from the creation arguments. Control thread only: called
    // from the constructor and from the two parameter callbacks, all of which
    // run before the object is wired or published.
    void ReadArgs();

    // Read [text, length) into `list` and walk it. The one path every
    // value-carrying handler funnels through, guard included.
    void Take(const char* text, std::size_t length, YSE::THREAD thread);

    // Send one `<index> <element>` pair per element, first to last. Called with
    // the guard held, which is what keeps `list` and `offset` still.
    void Emit(YSE::THREAD thread);

    // The creation arguments, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::vector<std::string> creationArgs;

    // The elements of the message being indexed. A working buffer rather than
    // state — nothing re-reads it between messages, there being no bang — but a
    // member so that its storage is reserved once, in the constructor. Not
    // thread-safe, which is what `busy` is for.
    AtomList list;

    // Where a pair is assembled. Reserved by the constructor to
    // AtomList::RENDER_CAPACITY, so the walk allocates nothing.
    std::string render;

    // Max's index offset. Seeded from the creation argument and then owned by
    // the `offset` message, so it does not survive a save. Written only with the
    // guard held.
    int offset = 0;

    // The guard, held across the whole walk. A message that finds it taken is
    // dropped and counted rather than made to spin, this being a path the audio
    // callback takes.
    std::atomic<bool> busy{false};

    std::atomic<std::uint64_t> dropped{0};
  };

} // namespace PATCHER
} // namespace YSE
