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
     *  @brief Cut a list into equal groups, one per outlet, and send the rest
     *         out the right — ``.unjoin`` (issue #520).
     *
     *  Max's ``unjoin``: "separates a list's elements by group, and sends each
     *  group of items out a separate outlet".
     *
     *  ### What it is that ``.unpack`` is not
     *
     *  ``.unpack`` (#519) sends **one item** out each outlet and drops whatever
     *  it has no outlet for. This sends a **group of items** out each outlet
     *  and drops nothing at all, because its rightmost outlet is a remainder:
     *  everything the groups did not claim leaves there, whole. So ``.unpack``
     *  is the object for taking a list of known shape apart into its named
     *  components, and this is the object for cutting a list of *unknown
     *  length* into equal pieces — the issue's "where ``.pack`` has a fixed
     *  shape declared up front, these handle variable-length data", read from
     *  the splitting end.
     *
     *  It is ``.join``'s inverse, and the round trip is exact: a ``.join 2``
     *  holding ``1 2`` and ``3 4`` sends ``1 2 3 4``, and a ``.unjoin 2 2``
     *  gives back ``1 2`` on its first outlet and ``3 4`` on its second.
     *
     *  ### The arguments are the outlet count and the group size
     *
     *  ``.unjoin <groups> [size]``.
     *
     *  The first argument is Max's, "the number of outlets **beyond the
     *  rightmost outlet**": an object with ``groups`` group outlets has
     *  ``groups + 1`` outlets in total, the last of which is the remainder. The
     *  default is 2, so a bare ``.unjoin`` has three outlets.
     *
     *  The second is Max's ``@outsize`` attribute, "defines the number of items
     *  to be sent out the outlets", and it defaults to 1. The patcher has no
     *  attributes, so an attribute that changes what the object does becomes a
     *  positional creation argument — ``.zl <mode> <arg>``'s route, and the one
     *  ``.join`` takes for its trigger list.
     *
     *  With the default size of 1 the object is ``.unpack`` with a remainder
     *  outlet instead of a drop, which is a fair way to read Max's own
     *  defaults.
     *
     *  ### Complete groups only, and the remainder catches everything else
     *
     *  Reading Max's "the rightmost outlet receives remaining items that don't
     *  fill a complete group" literally: a group outlet fires only when there
     *  are enough items left to fill it **completely**, and everything from the
     *  first incomplete group onwards — a short tail, a surplus past the last
     *  group outlet, or both — goes out the rightmost outlet as one message.
     *
     *  So ``.unjoin 2 2`` given ``1 2 3`` sends ``1 2`` out outlet 0, nothing
     *  out outlet 1, and ``3`` out the remainder; given ``1 2 3 4 5 6`` it
     *  sends ``1 2``, ``3 4`` and ``5 6``. A group outlet is either whole or
     *  silent, which is what makes the object safe to wire into anything that
     *  expects a fixed-length list, and it is why the remainder exists at all.
     *
     *  Outlets fire **right to left** — Max's universal order, the one
     *  ``.trigger``, ``.unpack`` and ``.bondo`` already keep — so the remainder
     *  lands first and the leftmost group last, and each send completes in full
     *  (the whole subgraph behind that outlet) before the next one starts.
     *
     *  ### No state, and so no bang
     *
     *  Max documents ``int``, ``float``, ``list`` and ``anything`` on
     *  ``unjoin`` and no ``bang``, and that is the honest contract rather than
     *  an omission: this object holds nothing between messages. It is a
     *  distributor, not a register — where ``.unpack`` keeps its elements so a
     *  bang can re-send them, every group here is a slice of the list that just
     *  arrived and there is nothing to re-send. A patch that wants that
     *  behaviour puts a ``.l`` in front. Registering no ``bang`` is what keeps
     *  ``GetAcceptedTypes()`` reporting what the object really takes, the
     *  ``.zl`` / ``.combine`` discipline.
     *
     *  An ``int`` or a ``float`` is a one-item list, as it is everywhere in
     *  this family, and so lands wherever a one-item list lands: on the
     *  leftmost outlet at the default group size — Max's "int: Number sent to
     *  left outlet" — and on the remainder at any larger one, there being no
     *  complete group to put it in.
     *
     *  ### Bounded storage, shared with the rest of the list family
     *
     *  The arriving list is read into an ``AtomList`` (see ``pAtomList.h``),
     *  the bounded pre-allocated storage ``.zl`` settled in #523: at most
     *  ``AtomList::MAX_ATOMS`` (256) atoms spanning at most
     *  ``AtomList::TEXT_CAPACITY`` (1024) characters between them, in memory
     *  reserved when the object is built.
     *
     *  An over-long list loses its **tail**, which is counted on ``Dropped()``,
     *  and the head is distributed. That is ``.zl``'s rule rather than
     *  ``.join``'s refuse-whole, and the difference is what is at stake: here
     *  the surplus is input the patch has just sent, not state the object's
     *  other inlets are holding. The counter rather than a log line is the
     *  family rule — formatting one builds a ``std::string`` on a thread that
     *  may be the audio callback. A creation argument out of range *is* logged,
     *  parameter parsing being control-thread only.
     *
     *  ### What comes out
     *
     *  Per outlet, the family's shared ``SendAtomRange``: a group of one atom
     *  leaves as the int, float or symbol it spells rather than as a list of
     *  one, a longer one as list text, and an empty remainder sends nothing at
     *  all rather than an empty message. Every outlet is declared ``ANY``,
     *  because a group carries whatever the list carried — this object coerces
     *  nothing, its input having no declared per-item type to coerce to.
     *
     *  ### Live SetParams (#234)
     *
     *  The creation arguments are a ``LIST`` parameter, so
     *  ``Parameters::NeedsRebuild()`` is true and a live re-parse takes the
     *  structural-replacement route. That is right rather than a limitation:
     *  the first argument *is* the outlet count, so re-typing it is re-typing
     *  the object.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlet, and one
     *  that emitted would re-distribute the last list on every DSP tick from a
     *  stimulus no patch sent.
     *
     *  No message path allocates, locks or blocks. Numbers are rendered by
     *  ``ExprFormatValue`` into a stack buffer and read through the same token
     *  path a list takes, the list is collected into an ``AtomList`` reserved
     *  by the constructor, and the sends render into a buffer reserved at the
     *  same time. Two threads sending to the same object are serialised by a
     *  single test-and-set guard whose loser is **dropped and counted** rather
     *  than made to spin, this being a path the audio callback takes; the same
     *  guard is what stops an object wired back into its own inlet from
     *  recursing on the audio thread.
     */
    PATCHER_CLASS(gUnjoin, YSE::OBJ::G_UNJOIN)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(UnjoinInt)
    _FLOAT_IN(UnjoinFloat)
    _LIST_IN(UnjoinList)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Most group outlets the object will build — one short of its
     *         outlet count, the last outlet always being the remainder.
     *
     *  ``AtomList::MAX_ATOMS``, because a group needs at least one atom and the
     *  bounded list holds at most that many: one bound rather than a second one
     *  beside it. A larger creation argument is clamped, which the control
     *  thread logs.
     */
    static constexpr int MAX_GROUPS = (int)AtomList::MAX_ATOMS;

    /** @brief Fewest group outlets the object will build, so the smallest
     *         object has two outlets: one group and the remainder. */
    static constexpr int MIN_GROUPS = 1;

    /** @brief Group outlets with no creation argument — Max's default of 2,
     *         which is three outlets in all. */
    static constexpr int DEFAULT_GROUPS = 2;

    /** @brief Largest group size, the whole of the bounded list. */
    static constexpr int MAX_SIZE = (int)AtomList::MAX_ATOMS;

    /** @brief Items per group with no second creation argument — Max's
     *         ``@outsize`` default of 1. */
    static constexpr int DEFAULT_SIZE = 1;

    /** @brief How many group outlets the object has. Its outlet count is one
     *         more than this. */
    int GroupCount() const {
      return groups;
    }

    /** @brief How many items fill one group — Max's ``@outsize``. */
    int GroupSize() const {
      return size;
    }

    /**
     *  @brief Items refused so far, plus messages dropped because another
     *         thread held the object.
     *
     *  Counts the tail of a list too long for the bounded storage and a message
     *  that found the object busy — and nothing else, there being no outlet an
     *  item can fail to reach. Monotonic, readable from any thread, and the
     *  object's refusal report: a counter rather than a log line, because the
     *  refusing thread may be the audio callback.
     */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

  private:
    // Rebuild the outlets and re-read the group size from the creation
    // arguments. Control thread only: the constructor and the two parameter
    // callbacks, all of which run before the object is wired or published. A
    // *live* SetParams never reaches here on a published object — registering
    // the callbacks makes ParamsNeedRebuild() true, so #234 replaces the object
    // instead.
    void ShapePorts();

    // Put the documentation on the outlets as they are now. Called by
    // ShapePorts, so a re-parse cannot leave an outlet undocumented.
    void ApplyDocs();

    // Take the guard, or count a drop and answer false. The loser of a race and
    // a feedback loop take the same route — see the class notes.
    bool Enter();
    void Leave();

    // One refusal, on the counter Dropped() reports.
    void CountDrop(std::size_t count = 1);

    // Read [text, length) into `list` and distribute it. The one path every
    // handler funnels through, guard included.
    void Take(const char* text, std::size_t length, YSE::THREAD thread);

    // Send the collected list out the outlets, right to left. Called with the
    // guard held.
    void Emit(YSE::THREAD thread);

    // The creation arguments, whole. One LIST parameter rather than two scalars
    // so that a live re-parse takes the structural route the outlet count needs
    // — the family's rule, and the same shape `.join` registers. Control thread
    // only.
    std::vector<std::string> creationArgs;

    // Group outlets, and items per group. Written by ShapePorts before the
    // object is published and read by every message afterwards.
    int groups = DEFAULT_GROUPS;
    int size = DEFAULT_SIZE;

    // **The message being distributed**, and nothing more: this object holds no
    // state between messages, so the list is cleared and refilled each time.
    // Reserves its storage in the constructor; not thread-safe, which is what
    // `busy` is for.
    AtomList list;

    // Where a group is rendered. Reserved by the constructor to
    // AtomList::RENDER_CAPACITY, so distributing the list allocates nothing.
    std::string render;

    // The guard. A message that finds it taken is dropped and counted rather
    // than made to spin, this being a path the audio callback takes.
    std::atomic<bool> busy{false};

    std::atomic<std::uint64_t> dropped{0};
  };

} // namespace PATCHER
} // namespace YSE
