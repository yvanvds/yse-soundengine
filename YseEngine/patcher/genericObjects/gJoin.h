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
     *  @brief Concatenate what several inlets are holding into one list —
     *         ``.join`` (issue #520).
     *
     *  Max's ``join``: "combines separate untyped items into an output list".
     *
     *  ### What it is that ``.pack`` is not
     *
     *  ``.pack`` (#517) and this object look alike from the outside — n inlets,
     *  one outlet, one list — and they are not interchangeable. A ``.pack``
     *  slot holds **exactly one atom** of a **declared type**: its creation
     *  argument fixes both, so the output list is always as long as the
     *  argument list and every element is coerced on the way in. A ``.join``
     *  inlet holds **whatever message arrived at it**, however many items that
     *  was and whatever they spell, and the output is those pieces laid
     *  end to end. So ``.pack`` is the object for a list of a *fixed shape*
     *  built from single values, and this is the object for a list of a
     *  *variable length* built from pieces — which is exactly the issue's
     *  framing: "where ``.pack`` has a fixed shape declared up front, these
     *  handle variable-length data".
     *
     *  The consequence a patch feels: a ``.join 2`` whose inlets are holding
     *  ``1 2`` and ``3 4`` sends ``1 2 3 4``, and the same two messages into a
     *  ``.pack 0 0`` send ``1 3`` — the second and later items of each spread
     *  rightwards into the neighbouring slot and the surplus is dropped.
     *  ``.unjoin`` is the way back, as ``.unpack`` is ``.pack``'s.
     *
     *  There is no type coercion here at all, which is Max's "separate
     *  **untyped** items": an inlet stores the characters that arrived. That
     *  is not a shortcut — a variable-length piece has no per-item type to
     *  declare in the first place, and declaring one per *inlet* would say
     *  nothing about an inlet holding four items.
     *
     *  ### The arguments are the inlet count and which inlets are hot
     *
     *  ``.join <inlets> [triggers...]``.
     *
     *  The first argument is the number of inlets, 1 to 256; with no arguments
     *  the object is Max's default, "two inlets with initial values of 0". Each
     *  inlet starts out holding the single atom ``0``, so a bang before
     *  anything has arrived sends a complete list rather than silence — the
     *  same choice ``.pack`` makes about its starting values.
     *
     *  The arguments after it are Max's ``@triggers`` attribute, "designates
     *  inputs that automatically trigger output": each one names an inlet that
     *  releases the list when written, and ``-1`` makes **every** inlet hot.
     *  With none of them the leftmost inlet is the hot one, which is the
     *  arrangement ``.pack``, ``.+`` and ``.counter`` already use and the one a
     *  patch gets by default in Max. The patcher has no attributes, so an
     *  attribute that changes what the object *does* becomes a positional
     *  creation argument — ``.zl <mode> <arg>``'s route.
     *
     *  This is also why there is no ``.jak`` beside ``.join`` the way ``.pak``
     *  sits beside ``.pack``: hot-ness here is already a creation argument, so
     *  a second registered name would be sugar for ``.join <n> -1`` and nothing
     *  more.
     *
     *  ### Bounded storage, shared with the rest of the list family
     *
     *  The joined list *is* an ``AtomList`` (see ``pAtomList.h``), the bounded
     *  pre-allocated storage ``.zl`` settled in #523: at most
     *  ``AtomList::MAX_ATOMS`` (256) atoms spanning at most
     *  ``AtomList::TEXT_CAPACITY`` (1024) characters between them, in memory
     *  reserved when the object is built.
     *
     *  The atoms are held **already concatenated**, in inlet order, with a
     *  small table saying how many of them belong to each inlet. That is what
     *  keeps the output path free — a release is ``SendAtoms`` over storage
     *  that is already in the right shape, with nothing to assemble — and it is
     *  the same trade ``.pack`` makes: the cost is that a store is a rebuild,
     *  because the backing text is append-only until ``Clear()``. The rebuild
     *  is a bounded number of copies into memory the object already owns and
     *  still allocates nothing.
     *
     *  A store whose result would not fit is **refused whole** and counted on
     *  ``Dropped()`` — ``.pack``'s rule rather than ``.zl``'s, and for
     *  ``.pack``'s reason: what would be lost is not surplus input but the
     *  messages the object's other inlets are holding, and dropping those would
     *  silently rewrite state nobody touched. The counter rather than a log
     *  line is the family rule; formatting one builds a ``std::string`` on a
     *  thread that may be the audio callback. A creation argument that does not
     *  fit *is* logged, parameter parsing being control-thread only.
     *
     *  ### A bang releases, ``set`` stores quietly
     *
     *  ``bang`` sends the list as it stands and stores nothing, and it is
     *  accepted on **every** inlet — Max's "bang: Outputs the currently stored
     *  list from any inlet", which is where this parts company with ``.pack``,
     *  whose bang lives on the releasing inlets only. Cold here means "writing
     *  me does not release", not "I am inert".
     *
     *  ``set <message>`` performs *exactly* the store the same message without
     *  the word would have performed and suppresses only the release — Max's
     *  "set: Stores list array without triggering output", and one storage rule
     *  so the two paths cannot drift apart. It is also the only way to empty an
     *  inlet: ``set`` followed by nothing but separators stores no atoms at
     *  all, and an inlet holding no atoms contributes nothing to the joined
     *  list.
     *
     *  ### What comes out
     *
     *  The family's transport convention, shared as ``SendAtoms`` in
     *  ``pAtomList.h``: a joined list of one atom leaves as the int, float or
     *  symbol it spells rather than as a list of one, and a longer one as list
     *  text. Unlike ``.pack``, this object *can* hold nothing — every inlet
     *  emptied — and then it sends nothing at all rather than an empty message.
     *
     *  ### Live SetParams (#234)
     *
     *  The creation arguments are a ``LIST`` parameter, so
     *  ``Parameters::NeedsRebuild()`` is true and a live re-parse takes the
     *  structural-replacement route. That is right rather than a limitation:
     *  the arguments *are* the inlet count and the trigger set, so re-typing
     *  them is re-typing the object.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlets, and
     *  one that emitted would re-send the list on every DSP tick from a
     *  stimulus no patch sent.
     *
     *  No message path allocates, locks or blocks. Numbers are rendered by
     *  ``ExprFormatValue`` into a stack buffer and stored through the same
     *  token path a list takes, the rebuild runs in an ``AtomList`` reserved by
     *  the constructor, and the release renders into a buffer reserved at the
     *  same time. Two threads writing the same object are serialised by a
     *  single test-and-set guard whose loser is **dropped and counted** rather
     *  than made to spin, this being a path the audio callback takes; the same
     *  guard is what stops an object wired back into one of its own inlets from
     *  recursing on the audio thread.
     */
    PATCHER_CLASS(gJoin, YSE::OBJ::G_JOIN)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(JoinBang)
    _INT_IN(JoinInt)
    _FLOAT_IN(JoinFloat)
    _LIST_IN(JoinList)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Most inlets the object will build.
     *
     *  ``AtomList::MAX_ATOMS``, because every inlet starts out holding one
     *  atom of the shared bounded list: one bound rather than a second one
     *  beside it. A larger creation argument is clamped, which the control
     *  thread logs.
     */
    static constexpr int MAX_PORTS = (int)AtomList::MAX_ATOMS;

    /** @brief Fewest inlets the object will build. */
    static constexpr int MIN_PORTS = 1;

    /** @brief Inlets with no creation argument — Max's default of 2. */
    static constexpr int DEFAULT_PORTS = 2;

    /** @brief The trigger argument that makes every inlet hot — Max's
     *         "@triggers -1". Any negative index reads as this. */
    static constexpr int TRIGGER_ALL = -1;

    /** @brief How many inlets the object has. At least one. */
    int PortCount() const {
      return (int)counts.size();
    }

    /** @brief Whether writing inlet @p index releases the joined list. False
     *         for an index out of range. */
    bool Hot(int index) const;

    /** @brief How many atoms inlet @p index is holding. 0 for an index out of
     *         range, and 0 for an inlet a ``set`` has emptied. */
    int InletSize(int index) const;

    /**
     *  @brief The joined list as it stands, rendered — diagnostics and tests.
     *
     *  Builds a ``std::string``, so it is **not** for a message path; the
     *  object's own release renders into a buffer it reserved at construction.
     */
    std::string Joined() const;

    /**
     *  @brief Items refused so far, plus messages dropped because another
     *         thread held the object.
     *
     *  Counts a store that would not fit the bounded list and a message that
     *  found the object busy. Monotonic, readable from any thread, and the
     *  object's refusal report — a counter rather than a log line, because the
     *  refusing thread may be the audio callback.
     */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

  private:
    // Rebuild inlets, the outlet, the per-inlet table and the starting values
    // from the creation arguments. Control thread only: the constructor and the
    // two parameter callbacks, all of which run before the object is wired or
    // published. A *live* SetParams never reaches here on a published object —
    // registering the callbacks makes ParamsNeedRebuild() true, so #234
    // replaces the object instead.
    void ShapePorts();

    // Put the documentation on the ports as they are now. Called by ShapePorts,
    // so a re-parse cannot leave a port undocumented.
    void ApplyDocs();

    // Take the guard, or count a drop and answer false. The loser of a race and
    // a feedback loop take the same route — see the class notes.
    bool Enter();
    void Leave();

    // One refusal, on the counter Dropped() reports.
    void CountDrop(std::size_t count = 1);

    // Rebuild the joined list with the tokens of [text, length) standing in for
    // whatever inlet `at` was holding, every other inlet keeping its own. False
    // when the result would not fit, in which case nothing changed.
    bool Store(const char* text, std::size_t length, int at);

    // Store one message arriving at `inlet`, and release when `emit`. The one
    // path every value-carrying handler funnels through, guard included.
    void Take(const char* text, std::size_t length, int inlet, bool emit, YSE::THREAD thread);

    // Send the joined list. Called with the guard held.
    void Emit(YSE::THREAD thread);

    // The creation arguments, whole. One LIST parameter rather than scalars
    // because the trigger list has no fixed length. Control thread only.
    std::vector<std::string> creationArgs;

    // How many atoms of `slots` belong to each inlet, in inlet order — the
    // table that turns one concatenated list back into per-inlet pieces. Its
    // size is the inlet count. Sized by ShapePorts before the object is
    // published and never resized by a message handler.
    std::vector<int> counts;

    // Which inlets release when written, parallel to `counts`. A char rather
    // than a bool so the vector is a plain array of bytes a handler can read
    // without the proxy std::vector<bool> hands back.
    std::vector<char> hot;

    // **The object.** Every inlet's atoms, concatenated in inlet order, so a
    // release has nothing to assemble. `work` is the scratch the rebuild is
    // written into, so a store that does not fit leaves `slots` untouched. Both
    // reserve their storage in the constructor; neither is thread-safe, which
    // is what `busy` is for.
    AtomList slots;
    AtomList work;

    // Where a release is rendered. Reserved by the constructor to
    // AtomList::RENDER_CAPACITY, so sending the list allocates nothing.
    std::string render;

    // The guard. A message that finds it taken is dropped and counted rather
    // than made to spin, this being a path the audio callback takes.
    std::atomic<bool> busy{false};

    std::atomic<std::uint64_t> dropped{0};
  };

} // namespace PATCHER
} // namespace YSE
