#pragma once
#include "../pAtomList.h"
#include "../pObject.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Send a list's items out one at a time — ``.iter`` (issue #521).
     *
     *  Max's ``iter``, whose summary line is "Split a list into a series of
     *  numbers": it "unpacks and outputs list contents one element at a time".
     *
     *  ### The serialiser of the list family
     *
     *  Everything else in this family answers a list with a *list* — ``.zl``
     *  rearranges one, ``.unpack`` and ``.unjoin`` fan one out across a fixed
     *  set of outlets. This is the one object that turns a list of **unknown
     *  length** into a stream of individual messages down **one** cord, which
     *  is what makes ordinary scalar objects (``.i``, ``.+``, ``.mtof``,
     *  ``.coll``) usable per element. ``.unpack 0 0 0`` needs to know there are
     *  three items and gives each its own outlet; this needs to know nothing and
     *  gives them all the same outlet, in order.
     *
     *  With ``.uzi`` (#473) it is the patcher's second iteration primitive, and
     *  the two iterate over different things: ``.uzi`` counts, so it drives a
     *  loop whose body reads an index, while this walks the data it was handed.
     *  A patch that wants both wires the index of one into the other.
     *
     *  ### One inlet, one outlet, in order
     *
     *  Items leave **first to last** — Max: "the numbers in the list are sent
     *  out the outlet in sequential order". There is one outlet, so the family's
     *  right-to-left rule has nothing to order here; what matters instead is
     *  that each send completes in full — the whole subgraph behind the outlet,
     *  depth first — before the next item is sent, because ``outlet::Send*``
     *  calls the target inlet directly with no queue in between. That is what
     *  makes the object a *serialiser* rather than a scatter, and it is the
     *  property a patch relies on when it accumulates the items downstream.
     *
     *  ``bang`` re-sends the last message — Max: "sends the number or list most
     *  recently received, in sequential order" — which is why the object holds a
     *  list at all rather than passing straight through. Before anything has
     *  arrived there is nothing held, and a bang then sends nothing rather than
     *  an empty message, the ``.sprintf`` / ``.prepend`` rule the family shares.
     *
     *  An ``int`` or a ``float`` is a one-item list, as everywhere in this
     *  family, so it is stored and sent straight back out — Max's "outputs the
     *  number". ``anything`` is a list, which is why there is no message word to
     *  strip.
     *
     *  ### The message budget, which is the part that is not Max's
     *
     *  One arriving message becomes **as many messages as the list is long**,
     *  all inside the call frame of the one ``inlet::Set*`` that started it. So
     *  the object's cost is bounded by the same number that bounds its storage:
     *  ``AtomList::MAX_ATOMS`` (256) items, hence at most 256 sends — and 256
     *  subgraph traversals — per stimulus. That is a bound on the *work*, not a
     *  promise the work fits an audio block: a long list into a deep subgraph is
     *  expensive by construction, here as in Max.
     *
     *  The number worth knowing next to it: the patcher's value-command queue
     *  (#225) is 256 deep, so a full-length ``.iter`` feeding a ``.s`` from the
     *  control thread can saturate it in a single burst and hit its documented
     *  backpressure — drop and log. That is the queue's contract rather than
     *  something this object can fix, and it is the same caution ``.uzi``
     *  records; it is a reason to keep bursts that cross the send/receive
     *  boundary short.
     *
     *  ### Bounded storage, shared with the rest of the list family
     *
     *  The arriving list is read into an ``AtomList`` (see ``pAtomList.h``), the
     *  bounded pre-allocated storage ``.zl`` settled in #523: at most
     *  ``AtomList::MAX_ATOMS`` (256) atoms spanning at most
     *  ``AtomList::TEXT_CAPACITY`` (1024) characters between them, in memory
     *  reserved when the object is built.
     *
     *  An over-long list loses its **tail**, which is counted on ``Dropped()``,
     *  and the head is iterated. That is ``.zl``'s and ``.unjoin``'s side of the
     *  family's two-sided overflow rule rather than ``.join``'s refuse-whole,
     *  and the difference is what is at stake: here the surplus is input the
     *  patch has just sent, not state the object's other inlets are holding.
     *  A counter rather than a log line is the family rule — formatting one
     *  builds a ``std::string`` on a thread that may be the audio callback.
     *
     *  ### What comes out
     *
     *  Per item, the family's shared ``SendAtomRange`` over a slice of one: an
     *  item leaves as the int, float or symbol it spells rather than as a list
     *  of one, so it reaches the inlets an uncollected value would have reached
     *  — which is the whole point of the object. Nothing is coerced; the outlet
     *  is declared ``ANY`` because an item carries whatever the list carried.
     *
     *  ### No creation arguments
     *
     *  Max documents none for ``iter``, and there is nothing here for one to
     *  say: the object has a fixed shape and its only bound is the shared one.
     *  So there is no parameter to re-parse and no structural rebuild —
     *  ``SetParams`` on a ``.iter`` changes nothing, and a ``DumpJSON`` /
     *  ``ParseJSON`` round trip carries an empty parameter string.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlet, and one
     *  that emitted would re-iterate the held list on every DSP tick from a
     *  stimulus no patch sent — the family's rule, and here one of the more
     *  expensive ways to break it.
     *
     *  No message path allocates, locks or blocks. Numbers are rendered by
     *  ``ExprFormatValue`` into a stack buffer and read through the same token
     *  path a list takes, the list is collected into an ``AtomList`` reserved by
     *  the constructor, and the sends render into a buffer reserved at the same
     *  time.
     *
     *  Two threads sending to the same object are serialised by a single
     *  test-and-set guard whose loser is **dropped and counted** rather than
     *  made to spin, this being a path the audio callback takes. Here the guard
     *  carries a second, heavier job, and it is held across the *whole* walk
     *  deliberately: this object emits in a loop, so a cord from its outlet back
     *  to its inlet — directly or round a chain — re-enters the handler from
     *  *inside* the walk. Letting that through would both restart the iteration
     *  under itself, which never terminates, and rewrite the very list being
     *  walked. The returning message is therefore refused and counted, exactly
     *  as ``.uzi`` refuses a re-entrant start, and the list stays fixed for the
     *  duration of its own walk without any snapshot copy.
     */
    PATCHER_CLASS(gIter, YSE::OBJ::G_ITER)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(IterBang)
    _INT_IN(IterInt)
    _FLOAT_IN(IterFloat)
    _LIST_IN(IterList)

    /**
     *  @brief Most items one message can be broken into, and so most sends one
     *         stimulus can cost.
     *
     *  ``AtomList::MAX_ATOMS`` (256), because the items *are* an ``AtomList``:
     *  one bound rather than a second one beside it. This is the documented
     *  message budget — see the class notes on what it means next to the
     *  256-deep value-command queue.
     */
    static constexpr std::size_t MAX_ITEMS = AtomList::MAX_ATOMS;

    /** @brief How many items the held list has — how many messages the next
     *         ``bang`` will send. 0 before anything has arrived. */
    std::size_t Held() const {
      return list.Size();
    }

    /**
     *  @brief The held list as it stands, rendered — diagnostics and tests.
     *
     *  Builds a ``std::string``, so it is **not** for a message path; the
     *  object's own sends render into a buffer it reserved at construction.
     */
    std::string Stored() const;

    /**
     *  @brief Items refused so far, plus messages dropped because the object
     *         was already busy.
     *
     *  Counts the tail of a list too long for the bounded storage, and a
     *  message that arrived while another thread — or a cord looping back from
     *  this object's own outlet — held the object mid-walk. Monotonic, readable
     *  from any thread, and the object's refusal report: a counter rather than
     *  a log line, because the refusing thread may be the audio callback.
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

    // Read [text, length) into `list` and walk it. The one path every
    // value-carrying handler funnels through, guard included.
    void Take(const char* text, std::size_t length, YSE::THREAD thread);

    // Send the held list out the outlet, one item at a time, first to last.
    // Called with the guard held, which is what keeps `list` still.
    void Emit(YSE::THREAD thread);

    // **The last message received**, and the object's whole state: what a bang
    // re-sends. Reserves its storage in the constructor; not thread-safe, which
    // is what `busy` is for.
    AtomList list;

    // Where an item is rendered. Reserved by the constructor to
    // AtomList::RENDER_CAPACITY, so the walk allocates nothing. Only the symbol
    // path touches it at all.
    std::string render;

    // The guard, held across the whole walk. A message that finds it taken is
    // dropped and counted rather than made to spin, this being a path the audio
    // callback takes.
    std::atomic<bool> busy{false};

    std::atomic<std::uint64_t> dropped{0};
  };

} // namespace PATCHER
} // namespace YSE
