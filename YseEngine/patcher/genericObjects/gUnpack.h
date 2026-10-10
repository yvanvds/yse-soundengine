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
     *  @brief Break a list into its elements, one per outlet — ``.unpack``
     *         (issue #519).
     *
     *  Max's ``unpack``: "breaks a list into its elements, and sends each item
     *  out a separate outlet".
     *
     *  ### The mirror image of ``.pack``
     *
     *  ``.pack`` (#517) turns several cords carrying several values into one
     *  cord carrying one list; this is the way back, and it is the only way a
     *  patch gets at the individual elements of a list that arrives from
     *  somewhere else — including the list payloads the named bus already
     *  delivers, a three-element list being how ``sound.<name>.position`` is
     *  addressed. Without it a list is opaque: it can be stored (``.l``),
     *  measured and rearranged (``.zl``), and passed along, but never taken
     *  apart into the numbers a patch actually computes with.
     *
     *  The symmetry with ``.pack`` is deliberate and goes further than the
     *  picture. The creation arguments are read the same way, the type coercion
     *  is the same rule read in the opposite direction, the storage is the same
     *  bounded ``AtomList``, and the overflow behaviour is the same refusal. A
     *  ``.pack 0 0 0`` wired into a ``.unpack 0 0 0`` is the identity.
     *
     *  ### The arguments are the outlet count *and* the outlet types
     *
     *  One outlet per creation argument, and each argument's **spelling**
     *  decides what its outlet emits: a token spelled as a whole integer is an
     *  int outlet, one carrying a decimal point or an exponent is a float
     *  outlet, and anything else is a symbol outlet. Max: "the arguments can be
     *  any combination of ints, floats, and symbols" and they set "the output
     *  type for each corresponding outlet". The argument is also that element's
     *  starting value, so a ``bang`` before anything has arrived sends a
     *  complete set rather than nothing. With no arguments at all the object is
     *  Max's default: "if no argument is typed in, unpack will have two int
     *  outlets".
     *
     *  The type is enforced on the way in — Max's "the inlet type is forced to
     *  the outlet type that is defined" — and it is ``.pack``'s enforcement,
     *  atom for atom:
     *
     *  - an **int** outlet truncates a float, so ``2.9`` reaching one leaves as
     *    ``2``;
     *  - a **float** outlet promotes an int and keeps its decimal point, so it
     *    leaves as a float message rather than as an int one;
     *  - a **symbol** outlet takes whatever arrives, verbatim;
     *  - a number outlet handed a **symbol** has nothing to convert. It keeps
     *    the value it holds and counts the refusal, exactly as ``.pack``'s
     *    number element does, rather than emitting a 0 that would read
     *    downstream as a value the list carried.
     *
     *  Because the coercion is enforced on storage, an int outlet only ever
     *  carries int messages and a float outlet only ever float ones, which is
     *  why they are declared ``OUT_TYPE::INT`` and ``OUT_TYPE::FLOAT`` rather
     *  than ``ANY``. A symbol outlet is genuinely ``ANY``: it stores what
     *  arrived, and what arrived may spell a number.
     *
     *  ### What fires, and in which order
     *
     *  Outlets fire **right to left** — Max's universal order, the one
     *  ``.trigger``, ``.bondo`` and ``.decode`` already keep — and each send
     *  completes in full, the whole subgraph behind that outlet, before the
     *  next one starts, because the patcher's send path calls the target inlet
     *  directly with no queue in between. That is what makes ``.unpack``
     *  composable with cold inlets: the elements to the right land before the
     *  leftmost one arrives to set the result off.
     *
     *  Only the outlets the message actually **reached** fire. Max: "each item
     *  in the list (up to the number of outlets) is sent out the outlet
     *  corresponding to its position in the list" — so a two-item list into a
     *  ``.unpack 0 0 0`` fires outlets 1 and 0 and leaves outlet 2 silent, and
     *  a bare ``int`` fires the leftmost outlet only, which is Max's "the
     *  number is sent out the left outlet". Items past the last outlet have
     *  nowhere to go: they are dropped and counted, Max's "up to the number of
     *  outlets".
     *
     *  ``bang`` sends the set as it stands — Max: "causes each stored item of a
     *  list to be sent out the corresponding outlet" — which is why the object
     *  holds the elements at all rather than distributing them straight
     *  through. Every outlet fires on a bang, including ones no list has ever
     *  reached; they carry their creation argument.
     *
     *  There is no ``set``. Max does not document one for ``unpack`` and there
     *  is nothing for it to mean: this object's whole output is the
     *  distribution, so a store that suppressed it would store nothing anybody
     *  could observe except through a later bang.
     *
     *  ### Bounded storage, shared with the rest of the list family
     *
     *  The held elements *are* an ``AtomList`` (see ``pAtomList.h``), the
     *  bounded pre-allocated storage ``.zl`` settled in #523: at most
     *  ``AtomList::MAX_ATOMS`` (256) atoms — so at most 256 outlets — spanning
     *  at most ``AtomList::TEXT_CAPACITY`` (1024) characters between them, in
     *  memory reserved when the object is built. A creation argument list
     *  longer than that is clamped, which the control thread logs.
     *
     *  A store is a rebuild, for the reason ``.pack``'s is: the backing text is
     *  append-only until ``Clear()``, so replacing the elements means writing
     *  them into a scratch ``AtomList`` and swapping it in — a bounded number
     *  of copies into memory the object already owns, and no allocation.
     *
     *  A store whose result would not fit the text bound is **refused whole**,
     *  counted on ``Dropped()``, and **sends nothing**. That last part is where
     *  this parts company with ``.pack``, which still releases its unchanged
     *  list because writing its hot inlet is a release: here the output *is*
     *  the incoming elements, so firing the outlets with the values they
     *  already held would present stale state as the list that just arrived. A
     *  patch that wants the held set anyway asks for it with a bang.
     *
     *  The counter rather than a log line is the family rule — formatting one
     *  builds a ``std::string`` on a thread that may be the audio callback.
     *
     *  ### What comes out
     *
     *  One message per fired outlet, through the family's shared ``SendAtom``:
     *  an element leaves as the int, float or symbol it spells. Nothing is ever
     *  sent as a list, this object's whole purpose being that lists arrive here
     *  and single values leave.
     *
     *  ### Live SetParams (#234)
     *
     *  The creation arguments are a ``LIST`` parameter, so
     *  ``Parameters::NeedsRebuild()`` is true and a live re-parse takes the
     *  structural-replacement route. That is right rather than a limitation:
     *  the arguments *are* the outlet count and the outlet types, so re-typing
     *  them is re-typing the object.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlet, and one
     *  that emitted would re-distribute the held elements on every DSP tick
     *  from a stimulus no patch sent.
     *
     *  No message path allocates, locks or blocks. Numbers are rendered by
     *  ``ExprFormatValue`` into a stack buffer and stored through the same
     *  token path a list takes, the rebuild runs in an ``AtomList`` reserved by
     *  the constructor, and the sends render into a buffer reserved at the same
     *  time. Two threads writing the same object are serialised by a single
     *  test-and-set guard whose loser is **dropped and counted** rather than
     *  made to spin, this being a path the audio callback takes; the same guard
     *  is what stops an object wired back into its own inlet from recursing on
     *  the audio thread.
     */
    PATCHER_CLASS(gUnpack, YSE::OBJ::G_UNPACK)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(UnpackBang)
    _INT_IN(UnpackInt)
    _FLOAT_IN(UnpackFloat)
    _LIST_IN(UnpackList)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Most elements — and so most outlets — the object will build.
     *
     *  ``AtomList::MAX_ATOMS``, because the elements *are* an ``AtomList``: one
     *  bound rather than a second one beside it. A creation argument list
     *  longer than this is clamped, which the control thread logs.
     */
    static constexpr int MAX_PORTS = (int)AtomList::MAX_ATOMS;

    /** @brief Fewest elements the object will build. */
    static constexpr int MIN_PORTS = 1;

    /** @brief Elements with no creation argument — Max's default of 2. */
    static constexpr int DEFAULT_PORTS = 2;

    /** @brief What one element holds, decided by its creation argument's
     *         spelling and enforced on every store. The same three readings
     *         ``.pack`` makes of the same arguments. */
    enum class unpackType : std::uint8_t {
      INT, ///< spelled as a whole integer; a float reaching it is truncated
      FLOAT, ///< spelled with a point or an exponent; an int is promoted
      SYMBOL, ///< anything else; carries whatever arrives, verbatim
    };

    /** @brief How many elements — and outlets — the object has. At least one. */
    int PortCount() const {
      return (int)types.size();
    }

    /** @brief The type of element @p index. ``SYMBOL`` for an index out of
     *         range, which holds nothing anyway. */
    unpackType SlotType(int index) const;

    /**
     *  @brief The held elements as they stand, rendered — diagnostics and
     *         tests.
     *
     *  Builds a ``std::string``, so it is **not** for a message path; the
     *  object's own sends render into a buffer it reserved at construction.
     */
    std::string Stored() const;

    /**
     *  @brief Items refused so far, plus messages dropped because another
     *         thread held the object.
     *
     *  Counts an item that arrived past the last outlet, a symbol handed to a
     *  number outlet, a store that would not fit the text bound, and a message
     *  that found the object busy. Monotonic, readable from any thread, and
     *  the object's refusal report — a counter rather than a log line, because
     *  the refusing thread may be the audio callback.
     */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

  private:
    // Rebuild outlets, element types and starting values from the creation
    // arguments. Control thread only: the constructor and the two parameter
    // callbacks, all of which run before the object is wired or published. A
    // *live* SetParams never reaches here on a published object — registering
    // the callbacks makes ParamsNeedRebuild() true, so #234 replaces the object
    // instead.
    void ShapePorts();

    // Take the guard, or count a drop and answer false. The loser of a race and
    // a feedback loop take the same route — see the class notes.
    bool Enter();
    void Leave();

    // One refusal, on the counter Dropped() reports.
    void CountDrop(std::size_t count = 1);

    // Rebuild the held elements from the tokens of [text, length), placed from
    // element 0 rightwards; every element the message did not reach keeps what
    // it holds. `written` comes back holding how many elements the message did
    // reach, which is how many outlets fire. False when the result would not
    // fit, in which case nothing changed and `written` means nothing.
    bool Store(const char* text, std::size_t length, int& written);

    // Append `token` to `into` as element `index`'s type wants it, or — when
    // the token is a symbol and the element is not — the element's current
    // value, with `refused` set. False only when it did not fit.
    bool AddCoerced(AtomList& into, int index, const char* token, std::size_t length,
                    bool& refused);

    // Store one message and distribute what it reached. The one path every
    // value-carrying handler funnels through, guard included.
    void Take(const char* text, std::size_t length, YSE::THREAD thread);

    // Send elements [0, count) out their outlets, right to left. Called with
    // the guard held.
    void Emit(int count, YSE::THREAD thread);

    // Put the documentation on the outlets as they are now. Called by
    // ShapePorts, so a re-parse cannot leave an outlet undocumented.
    void ApplyDocs();

    // The creation arguments, whole. One LIST parameter rather than scalars
    // because there is one argument per outlet and the count is what is being
    // declared. Control thread only.
    std::vector<std::string> creationArgs;

    // One entry per element, parallel to the atoms of `slots`. Sized by
    // ShapePorts before the object is published and never resized by a message
    // handler.
    std::vector<unpackType> types;

    // **The object.** One atom per element, always exactly PortCount() of them.
    // `work` is the scratch the rebuild is written into, so a store that does
    // not fit leaves `slots` untouched. Both reserve their storage in the
    // constructor; neither is thread-safe, which is what `busy` is for.
    AtomList slots;
    AtomList work;

    // Where a send is rendered. Reserved by the constructor to
    // AtomList::RENDER_CAPACITY, so distributing the elements allocates
    // nothing.
    std::string render;

    // The guard. A message that finds it taken is dropped and counted rather
    // than made to spin, this being a path the audio callback takes.
    std::atomic<bool> busy{false};

    std::atomic<std::uint64_t> dropped{0};
  };

} // namespace PATCHER
} // namespace YSE
