#pragma once
#include "../pAtomList.h"
#include "../pObject.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Declares one member of the list-building family (issue #517).
//
// Mirrors AFFIX_CLASS from gAffix.h: the whole body lives in gPackBase and the
// constructor in gPack.cpp only picks a trigger policy and fills in the
// documentation that differs between the two. `.pak` (#518) is `.pack` with
// every inlet hot, which is one enumerator's difference and no code's, so it is
// a second PACK_CLASS line rather than a second implementation.
#define PACK_CLASS(className, typeName)                                                            \
  class className : public gPackBase {                                                             \
  public:                                                                                          \
    className();                                                                                   \
    const char* Type() const override {                                                            \
      return typeName;                                                                             \
    }                                                                                              \
    CREATE(className)                                                                              \
  };

namespace YSE {
  namespace PATCHER {

    /** @brief Which inlets release the packed list when written. */
    enum class packTrigger : std::uint8_t {
      LeftInlet, //!< ``.pack`` (#517) — only inlet 0 is hot.
      AnyInlet //!< ``.pak`` (#518) — every inlet is hot.
    };

    /**
     *  @brief Shared body for ``.pack`` (#517) and ``.pak`` (#518) — the
     *         objects that build one list out of values arriving separately.
     *
     *  Max's ``pack``: "Combine items into an output list. The arguments
     *  determine the list format and types of the list elements. The number of
     *  inlets is based on the number of arguments."
     *
     *  ### Why the patcher needed it
     *
     *  ``.l`` holds a list a patch typed; nothing until now could *build* one
     *  out of values that arrive at run time. That blocks every list-consuming
     *  object — ``.zl``, ``.coll``, ``.funbuff`` — and it blocks the list
     *  payloads the named bus already accepts, since a three-element list is
     *  how ``sound.<name>.position`` is addressed. This is the object that
     *  turns three cords carrying three numbers into one cord carrying one
     *  list, and ``.unpack`` (#519) is the way back.
     *
     *  ### One hot inlet, and what the cold ones are for
     *
     *  ``.pack``'s leftmost inlet is the only one that releases: writing any
     *  other inlet stores and stays quiet, so a patch loads the right-hand
     *  values first and lets the leftmost one carry the finished list out. That
     *  is the same hot/cold arrangement ``.+``, ``.sel`` and ``.counter`` use,
     *  and it is the reverse of ``.bondo``, whose every inlet releases.
     *
     *  ``.pak`` is this object with ``packTrigger::AnyInlet``: every inlet
     *  releases, which is what a patch wants when any one of the packed values
     *  changing should push a fresh list downstream. The two differ in that one
     *  enumerator and nothing else — same storage, same coercion, same output
     *  rule — which is why they share this base rather than being written
     *  twice.
     *
     *  ### The arguments are the shape *and* the types
     *
     *  One inlet per creation argument, and each argument's **spelling**
     *  decides what its slot holds: a token spelled as a whole integer is an
     *  int slot, one carrying a decimal point or an exponent is a float slot,
     *  and anything else is a symbol slot. The argument is also that slot's
     *  starting value, so ``.pack 0 0 0`` banged before anything arrives sends
     *  ``0 0 0`` rather than nothing. With no arguments at all the object is
     *  Max's default: "two inlets with initial values of 0 (int)".
     *
     *  The type is enforced on the way in, which is Max's "type conversion
     *  occurs based on initialization":
     *
     *  - an **int** slot truncates a float — ``2.9`` stored in ``.pack 0 0`` is
     *    ``2``;
     *  - a **float** slot promotes an int, and keeps its decimal point so the
     *    list still reads as one with a float in it;
     *  - a **symbol** slot takes whatever arrives, verbatim. Max raises an
     *    error instead; here the patcher's transport is text to begin with, so
     *    storing the characters is both the useful reading and the honest one.
     *  - a number slot handed a **symbol** has nothing to convert. It keeps the
     *    value it had and counts the refusal, rather than storing a 0 that
     *    would read downstream as a value the patch chose.
     *
     *  ### Bounded storage, shared with the rest of the list family
     *
     *  The packed list *is* an ``AtomList`` (see ``pAtomList.h``), the bounded
     *  pre-allocated storage ``.zl`` settled in #523: at most
     *  ``AtomList::MAX_ATOMS`` (256) atoms — so at most 256 inlets — spanning at
     *  most ``AtomList::TEXT_CAPACITY`` (1024) characters between them, in
     *  memory reserved when the object is built.
     *
     *  Holding the slots *as* a list rather than as a table of values beside
     *  one is what keeps the output path free: a release is
     *  ``SendAtoms`` over storage that is already in the right shape, with
     *  nothing to assemble. The cost is that a store is a rebuild — the backing
     *  text is append-only until ``Clear()``, so replacing one atom means
     *  writing the whole list into a scratch ``AtomList`` and swapping it in,
     *  which is a bounded number of ``memcpy``s into memory the object already
     *  owns and still allocates nothing.
     *
     *  A store whose result would not fit the text bound is **refused whole**
     *  and counted on ``Dropped()``. Refused whole, rather than losing the tail
     *  as an over-long incoming list does in ``.zl``: the tail here is not
     *  surplus input, it is the values the patch's other inlets are holding,
     *  and dropping them would silently rewrite state nobody touched. The
     *  counter rather than a log line is the family rule — formatting one
     *  builds a ``std::string`` on a thread that may be the audio callback.
     *
     *  ### A list spreads, a bang releases, ``set`` stores quietly
     *
     *  A multi-item message spreads from the inlet that received it rightwards,
     *  one item per slot — Max's rule for ``pack``, and the rule ``.bondo``
     *  already follows for the same reason. Items past the last slot have
     *  nowhere to go: they are dropped and counted.
     *
     *  ``bang`` releases the list as it stands, storing nothing, and is
     *  accepted on the leftmost inlet only — the inlet Max documents it on, and
     *  registering it nowhere else keeps ``GetAcceptedTypes()`` reporting the
     *  real contract, the ``.zl`` / ``.combine`` discipline.
     *
     *  ``set <message>`` performs *exactly* the store the same message without
     *  the word would have performed and suppresses only the release — one
     *  storage rule, so the two paths cannot drift apart. Max: "Sets the values
     *  without causing list output. Although the set message works with any
     *  inlet, it is only meaningful in the left inlet."
     *
     *  ### What comes out
     *
     *  The family's transport convention, shared as ``SendAtoms`` in
     *  ``pAtomList.h``: a packed list of one item leaves as the int, float or
     *  symbol it spells rather than as a list of one, and a longer one as list
     *  text. A ``.pack`` always holds at least one slot, so it is never the
     *  silent case.
     *
     *  ### Live SetParams (#234)
     *
     *  The creation arguments are a ``LIST`` parameter, so
     *  ``Parameters::NeedsRebuild()`` is true and a live re-parse takes the
     *  structural-replacement route. That is right rather than a limitation:
     *  the arguments *are* the inlet count and the slot types, so re-typing
     *  them is re-typing the object, and Max loses the held values too.
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
     *  guard is what stops an object wired back into its own inlet from
     *  recursing on the audio thread.
     */
    class gPackBase : public pObject {
    public:
      /**
       *  @param name     the object's registered name. Taken as an argument
       *                  rather than read from ``Type()`` because the base
       *                  constructor shapes the ports, and a virtual call from
       *                  a base constructor lands on ``pObject``'s pure
       *                  ``Type()`` — the log line naming a clamped argument
       *                  list has to be able to say which object clamped it.
       *  @param trigger  which inlets release the packed list.
       */
      gPackBase(const char* name, packTrigger trigger);

      _NO_MESSAGES
      _NO_CALCULATE

      _BANG_IN(PackBang)
      _INT_IN(PackInt)
      _FLOAT_IN(PackFloat)
      _LIST_IN(PackList)

      _PARM_CLEAR
      _PARM_PARSE

      /**
       *  @brief Most slots — and so most inlets — the object will build.
       *
       *  ``AtomList::MAX_ATOMS``, because the slots *are* an ``AtomList``: one
       *  bound rather than a second one beside it. A creation argument list
       *  longer than this is clamped, which the control thread logs.
       */
      static constexpr int MAX_PORTS = (int)AtomList::MAX_ATOMS;

      /** @brief Fewest slots the object will build. */
      static constexpr int MIN_PORTS = 1;

      /** @brief Slots with no creation argument — Max's default of 2. */
      static constexpr int DEFAULT_PORTS = 2;

      /** @brief What one slot holds, decided by its creation argument's
       *         spelling and enforced on every store. */
      enum class packType : std::uint8_t {
        INT, ///< spelled as a whole integer; a float stored here is truncated
        FLOAT, ///< spelled with a point or an exponent; an int is promoted
        SYMBOL, ///< anything else; stores whatever arrives, verbatim
      };

      /** @brief How many slots — and inlets — the object has. At least one. */
      int PortCount() const {
        return (int)types.size();
      }

      /** @brief The type of slot @p index. ``SYMBOL`` for an index out of
       *         range, which stores nothing anyway. */
      packType SlotType(int index) const;

      /**
       *  @brief The packed list as it stands, rendered — diagnostics and tests.
       *
       *  Builds a ``std::string``, so it is **not** for a message path; the
       *  object's own release renders into a buffer it reserved at
       *  construction.
       */
      std::string Packed() const;

      /**
       *  @brief Items refused so far, plus messages dropped because another
       *         thread held the object.
       *
       *  Counts a store that would not fit the text bound, a symbol handed to a
       *  number slot, an item that arrived past the last slot, and a message
       *  that found the object busy. Monotonic, readable from any thread, and
       *  the object's refusal report — a counter rather than a log line,
       *  because the refusing thread may be the audio callback.
       */
      std::uint64_t Dropped() const {
        return dropped.load(std::memory_order_relaxed);
      }

      /** @brief True for ``.pak``: every inlet releases the list. False for
       *         ``.pack``, where only the leftmost one does. */
      bool EveryInletHot() const {
        return trigger == packTrigger::AnyInlet;
      }

    protected:
      /**
       *  @brief Fills in the documentation that differs per family member; the
       *         base constructor already set the category and built the ports.
       *
       *  RT-cold — constructor use only. The inlet and outlet texts are kept,
       *  because a re-parse rebuilds the ports and has to document the new ones
       *  without knowing which object it belongs to.
       */
      void Document(const char* summary, const char* leftInlet, const char* otherInlets,
                    const char* outlet, const char* paramDoc);

    private:
      // Rebuild inlets, outlets, slot types and starting values from the
      // creation arguments. Control thread only: the constructor and the two
      // parameter callbacks, all of which run before the object is wired or
      // published. A *live* SetParams never reaches here on a published object
      // — registering the callbacks makes ParamsNeedRebuild() true, so #234
      // replaces the object instead.
      void ShapePorts();

      // Put the stored documentation on the ports as they are now. Called by
      // ShapePorts and by Document, so a re-parse cannot leave a port
      // undocumented.
      void ApplyDocs();

      // Take the guard, or count a drop and answer false. The loser of a race
      // and a feedback loop take the same route — see the class notes.
      bool Enter();
      void Leave();

      // One refusal, on the counter Dropped() reports.
      void CountDrop(std::size_t count = 1);

      // Rebuild the packed list with the tokens of [text, length) placed from
      // slot `first` rightwards, every other slot keeping what it holds. False
      // when the result would not fit, in which case nothing changed.
      bool Store(const char* text, std::size_t length, int first);

      // Append `token` to `into` as slot `index`'s type wants it, or — when the
      // token is a symbol and the slot is not — the slot's current value, with
      // `refused` set. False only when it did not fit.
      bool AddCoerced(AtomList& into, int index, const char* token, std::size_t length,
                      bool& refused);

      // Store one message arriving at `inlet`, and release when `emit`. The one
      // path every handler funnels through, guard included.
      void Take(const char* text, std::size_t length, int inlet, bool emit, YSE::THREAD thread);

      // Send the packed list. Called with the guard held.
      void Emit(YSE::THREAD thread);

      // Whether a write at `inlet` releases: inlet 0 for `.pack`, any for
      // `.pak`.
      bool Hot(int inlet) const {
        return trigger == packTrigger::AnyInlet || inlet == 0;
      }

      // The creation arguments, whole. One LIST parameter rather than scalars
      // because there is one argument per inlet and the count is what is being
      // declared. Control thread only.
      std::vector<std::string> creationArgs;

      // One entry per slot, parallel to the atoms of `slots`. Sized by
      // ShapePorts before the object is published and never resized by a
      // message handler.
      std::vector<packType> types;

      // **The object.** One atom per slot, always exactly PortCount() of them,
      // and already in the shape a release sends. `work` is the scratch the
      // rebuild is written into, so a store that does not fit leaves `slots`
      // untouched. Both reserve their storage in the constructor; neither is
      // thread-safe, which is what `busy` is for.
      AtomList slots;
      AtomList work;

      // Where a release is rendered. Reserved by the constructor to
      // AtomList::RENDER_CAPACITY, so sending the list allocates nothing.
      std::string render;

      // The registered name, for the construction-time log only — see the
      // constructor on why Type() cannot serve there.
      const char* typeName;

      // Which inlets are hot. Set once, by the derived constructor's choice.
      packTrigger trigger;

      // The per-member documentation, kept so a re-parse can document the ports
      // it rebuilds. Pointers to string literals owned by the derived
      // constructor's translation unit.
      const char* leftInletDoc = nullptr;
      const char* otherInletDoc = nullptr;
      const char* outletDoc = nullptr;

      // The guard. A message that finds it taken is dropped and counted rather
      // than made to spin, this being a path the audio callback takes.
      std::atomic<bool> busy{false};

      std::atomic<std::uint64_t> dropped{0};
    };

    PACK_CLASS(gPack, YSE::OBJ::G_PACK)

  } // namespace PATCHER
} // namespace YSE
