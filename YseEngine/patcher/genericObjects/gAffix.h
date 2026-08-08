#pragma once
#include "../pObject.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Declares one member of the message-construction family (issue #487).
// Mirrors POLAR_CLASS from gPolar.h: the whole two-inlet/one-outlet body lives
// in gAffixBase, and the constructor in gAffix.cpp only has to pick a side and
// fill in the side-specific documentation.
#define AFFIX_CLASS(className, typeName)                                                           \
  class className : public gAffixBase {                                                            \
  public:                                                                                          \
    className();                                                                                   \
    const char* Type() const override {                                                            \
      return typeName;                                                                             \
    }                                                                                              \
    CREATE(className)                                                                              \
  };

namespace YSE {
  namespace PATCHER {

    /** @brief Which end of the incoming message the stored one is joined to. */
    enum class affixSide : std::uint8_t {
      Front, //!< ``.prepend`` — stored message first.
      Back //!< ``.append`` — stored message last.
    };

    /**
     *  @brief Shared body for ``.prepend`` and ``.append`` (issue #487) — the
     *         two objects that build a message out of a stored part and an
     *         incoming one.
     *
     *  Max's ``prepend`` ("adds a message in front of input") and ``append``
     *  ("add arguments to the end of any message you input"). One
     *  implementation, two insertion points: everything about the two is the
     *  same except which side of the separator the stored message lands on,
     *  which is why they share a base the way ``.cartopol`` / ``.poltocar`` and
     *  ``.peak`` / ``.trough`` do.
     *
     *  ### What they add that nothing else in the patcher can express
     *
     *  The patcher carries a list as text, and its whole routing family reads
     *  the **first token** of that text: ``.route`` and ``.routepass`` match on
     *  it, ``.sel`` compares against it, and every object with a message-based
     *  interface (``.matrix``'s ``<in> <out> <gain>``, ``.funnel``'s
     *  ``offset``, ``.router``'s ``connect``) is addressed by putting a word in
     *  front of the data. Until now a patch could *consume* such a message but
     *  never build one: no object could put a word in front of a value that
     *  arrived at run time. ``.prepend`` is that word, and ``.append`` is the
     *  same operation for the trailing arguments a message ends with.
     *
     *  ### The stored message has its own inlet
     *
     *  Max spells the replacement as ``set <message>`` into the object's one
     *  inlet, and can, because a Max box has one inlet and its parser can tell
     *  the *selector* ``set`` from the data behind it. Here a list arrives as
     *  text, and reserving a first word out of that text would cost these two
     *  objects the one property they exist for: they put words in **front of**
     *  messages, so ``set`` is exactly the kind of word a patch legitimately
     *  sends through them — ``.prepend set`` feeding a ``.f`` is the canonical
     *  Max idiom, and a ``.prepend`` that swallowed the list ``set 3`` would
     *  quietly re-aim itself instead of forwarding it.
     *
     *  So the stored message goes in its own inlet, the arrangement ``.router``,
     *  ``.matrix`` and ``.forward`` (#485) reached from the same collision.
     *  Inlet 0 stays a pure data inlet with no reserved words in it; inlet 1 is
     *  the stored message and nothing but the stored message, so there is no
     *  message it cannot express either. Inlet 1 takes a **list** (the whole
     *  text, not the first token — a stored message is allowed to be several
     *  words, which is the difference from ``.forward``'s destination), an
     *  **int** and a **float**, both spelled through ``ExprFormatValue`` so a
     *  number computed by a ``.counter`` reads back as the number a patch
     *  author would have typed. A **bang** is declined: the ``.decode`` /
     *  ``.router`` discipline of not registering a handler with nothing to do,
     *  which also leaves ``inlet::GetAcceptedTypes()`` reporting the real
     *  contract. An empty list on inlet 1 is not "nothing to do" but a real
     *  request — it clears the stored message, and see below for what that
     *  leaves.
     *
     *  ### With nothing stored, both objects are the identity
     *
     *  Not "join with an empty message", which would emit a stray separator,
     *  and not "drop", which would make an unconfigured object a hole in the
     *  patch. A ``.prepend`` with no argument passes what it is given straight
     *  through — and passes it through **as the type it arrived as**, so an int
     *  stays an int and a bang stays a bang rather than becoming the one-token
     *  list that spells it. That is the only reading under which inserting one
     *  of these objects into a working patch and configuring it afterwards is
     *  safe, and it is what makes the outlet an ``ANY`` outlet rather than a
     *  list one.
     *
     *  With something stored the result is always a list, because a message
     *  with a word in front of it *is* a list — that is the point.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlets, and an
     *  emitting ``Calculate()`` would hand a patch a message it never sent —
     *  the rule ``.s`` and the whole routing family establish.
     *
     *  No message path allocates, locks or blocks. The stored message is
     *  refilled into capacity reserved for ``MESSAGE_CAPACITY`` at construction
     *  and refused outright past it (silently on the inlet, which may be the
     *  audio thread; loudly on the creation argument, which is control-thread
     *  only). The outgoing text is built in a buffer reserved at construction
     *  for the longest stored message plus the longest list the patcher's own
     *  queues carry — ``patcherImplementation::kValueListCap`` — so the join is
     *  two ``memcpy``s into memory the object already owns. A list longer than
     *  that arriving straight from another object costs the buffer one growth,
     *  once, at each new high-water mark, since a ``std::string`` keeps its
     *  capacity; the same trade ``.funnel`` makes on the same path.
     */
    class gAffixBase : public pObject {
    public:
      explicit gAffixBase(affixSide side);

      _NO_MESSAGES
      _NO_CALCULATE

      _BANG_IN(PassBang)
      _INT_IN(PassInt)
      _FLOAT_IN(PassFloat)
      _LIST_IN(PassList)

      _INT_IN(SetStoredInt)
      _FLOAT_IN(SetStoredFloat)
      _LIST_IN(SetStoredList)

      _PARM_CLEAR
      _PARM_PARSE

      /**
       *  @brief Longest stored message the object will hold.
       *
       *  256 — ``patcherImplementation::kValueListCap``, the bound the
       *  patcher's own value queue carries a list payload in, so anything that
       *  can reach this object through the patcher can also be stored in it.
       *  A longer message is refused rather than truncated: half a message is a
       *  different message, and the whole job here is to produce a message a
       *  ``.route`` downstream will recognise.
       */
      static constexpr std::size_t MESSAGE_CAPACITY = 256;

      /**
       *  @brief The message currently being joined on, or empty when the object
       *         is the identity. The creation argument, and thereafter whatever
       *         inlet 1 was last given.
       */
      const std::string& Stored() const {
        return stored;
      }

    protected:
      // Fills in the pieces of documentation that differ per side; the base
      // constructor already set the category. RT-cold — constructor use only.
      void Document(const char* summary, const char* dataDoc, const char* outDoc,
                    const char* paramDoc);

    private:
      // Join `length` characters at `text` with the stored message and send the
      // result. Only called with a non-empty stored message — the identity case
      // never reaches here, because it must preserve the incoming type.
      void Emit(const char* text, std::size_t length, YSE::THREAD thread);

      // Replace the stored message with the `length` characters at `text`,
      // trimmed of surrounding whitespace. Empty clears it (and makes the
      // object the identity); longer than MESSAGE_CAPACITY is refused and the
      // previous message kept. Runs on whichever thread sent the message, so it
      // neither allocates nor logs: the buffer was reserved on the control
      // thread.
      void StoreText(const char* text, std::size_t length);

      // Which side of the separator the stored message lands on. Fixed by the
      // derived constructor, so the join is one branch on a register.
      affixSide side;

      // The creation argument — a LIST parameter, so it absorbs every remaining
      // token rather than only the next one, and ".prepend note 60" stores two
      // words rather than one.
      std::vector<std::string> message;

      // The joined creation argument, and thereafter whatever inlet 1 was last
      // given. Reserved to MESSAGE_CAPACITY at construction so the inlet path
      // refills it without allocating.
      std::string stored;

      // Where the outgoing list is built. Reserved at construction for the
      // longest stored message, a separator, and the longest list the patcher's
      // queues carry.
      std::string outText;
    };

    AFFIX_CLASS(gPrepend, YSE::OBJ::G_PREPEND)
    AFFIX_CLASS(gAppend, YSE::OBJ::G_APPEND)

  } // namespace PATCHER
} // namespace YSE
