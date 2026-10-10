#pragma once
#include "../pObject.h"
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Send 1 out a selected outlet and 0 out every other — ``.decode``
     *         (issue #481).
     *
     *  Max's ``decode``, whose one-line summary is "Send 1 or 0 out a specific
     *  outlet": "Provides hierarchical switching. The right inlet turns all
     *  outlets off, while the middle inlet turns all outlets on. The right inlet
     *  overrides the middle inlet, and the middle inlet overrides numbers sent
     *  to the left inlet that turn individual outlets on or off."
     *
     *  The one-hot decoder. An index goes in and a *set of states* comes out:
     *  the selected outlet carries 1 and every other carries 0. That is what
     *  drives a bank of gates, a row of indicators, or any group of things of
     *  which exactly one is meant to be live — the mutual exclusion is the
     *  object rather than something the patch has to maintain. Without it a
     *  patch puts a ``.sel`` or a ``.==`` in front of every member of the bank
     *  and keeps the constants in step by hand: N boxes, and N chances for two
     *  of them to be on at once after an edit.
     *
     *  ### What it is not
     *
     *  ``.gate`` routes a **value** to one of N outlets and says nothing at all
     *  on the others; this emits a **state** on every outlet, and the values it
     *  emits are its own rather than anything the patch sent. So a ``.gate``
     *  fed 60 puts 60 somewhere, while a ``.decode`` fed 2 puts 1 on outlet 2
     *  and 0 everywhere else whatever the patch is carrying. The two compose:
     *  a ``.decode`` into the select inlets of a bank of gates is exactly the
     *  hierarchical switch Max's description names.
     *
     *  ``.sel`` fires a bang when a value matches one of its constants, so it
     *  reports the *arrival* of a state and never its departure — the outlet
     *  that was selected last time hears nothing when the selection moves.
     *  Turning the old one off is the whole difference, and it is why this
     *  object exists at all.
     *
     *  ``.spray`` (#479) and ``.cycle`` (#477) also address a numbered outlet,
     *  but each sends **one** message out **one** outlet; here one message
     *  reaches all of them. And ``.trigger`` fans one value out every outlet as
     *  copies, where every outlet here carries the state that outlet is in.
     *
     *  ### Three inlets, and which one wins
     *
     *  Max gives the object a strict hierarchy, and the reason is that a patch
     *  wants to be able to mute or force a whole bank without losing the
     *  selection underneath it:
     *
     *  - **Right inlet** (``primary``): "Any positive number other than 0 sends
     *    a 0 out all outlets. When ``decode`` receives a 0 in its right inlet,
     *    it outputs 0 or 1 out its outlets based on the values last received in
     *    the middle and left inlets." The master mute, and the top of the
     *    hierarchy. Non-zero is taken as the "off" case whichever sign it has:
     *    Max names positive numbers because those are the ones a patch sends,
     *    and treating a negative as "on" would make ``-1`` mean the opposite of
     *    ``1`` in an inlet whose whole vocabulary is on and off.
     *  - **Middle inlet** (``secondary``): "If a 1 was last received in the
     *    right inlet, any number received in the middle inlet will send a 0 out
     *    all outlets. Otherwise, a number greater than 0 received in the middle
     *    inlet sends a 1 out all outlets. If 0 is received in the middle inlet,
     *    ``decode`` sends a 1 out the last outlet decoded by a number received
     *    in the left inlet, and 0 out all other outlets." The "all on" override,
     *    and the middle of the hierarchy. Strictly "greater than 0", as Max
     *    words it, so 0 and anything below it fall through to the selection.
     *  - **Left inlet**: "An index (starting with 0 for the left outlet) that
     *    specifies an outlet out to turn on, turning off all other outlets."
     *    The selection itself, and the bottom of the hierarchy.
     *
     *  The three are *state*, not events: each inlet remembers the last number
     *  it was given, and every message re-resolves the whole set from all three.
     *  That is what makes the overrides non-destructive — a ``1`` and then a
     *  ``0`` in the right inlet leave the bank exactly as it was before the
     *  mute, without the patch having to re-send the index.
     *
     *  ### Every outlet fires, right to left
     *
     *  Each accepted message sends the complete state: one int out every outlet,
     *  from the last to outlet 0, Max's universal order. That is the reading
     *  every sentence of the reference takes — "sends a 0 out all outlets",
     *  "sends a 1 out all outlets", "a 1 out the last outlet decoded ... and 0
     *  out all other outlets" — and it is what makes the object idempotent: a
     *  downstream bank always agrees with the decoder, whatever it missed
     *  earlier and whatever order it was wired in.
     *
     *  The three values are snapshotted into locals before the first send. The
     *  send path is synchronous and re-entrant, so a patch looping an outlet
     *  back into an inlet arrives here again *inside* the ``Send``; the
     *  interrupted burst has to go on emitting the state it started with rather
     *  than half of one state and half of another, which is the settle-first
     *  discipline ``.onebang``, ``.next``, ``.buddy``, ``.cycle``, ``.bucket``
     *  and ``.spray`` share.
     *
     *  ### The initial state, and an index with no outlet
     *
     *  Max: "The left outlet is initially enabled." So the object starts at
     *  index 0 with both overrides off — a ``bang`` before anything else has
     *  arrived reports 1 on outlet 0 and 0 on the rest, rather than a bank in
     *  no state at all.
     *
     *  An index the object has no outlet for is **ignored**: nothing is sent and
     *  the selection is left where it was. That is ``.gate``'s, ``.cycle``'s and
     *  ``.spray``'s discipline for the same situation and for the same reason —
     *  an out-of-range index is a miscount in the patch, and either wrapping it
     *  onto a real outlet or silently blanking the bank would hide the miscount
     *  at the object best placed to expose it. A negative index is the same
     *  case.
     *
     *  ### ``bang``
     *
     *  Max: "The message ``bang`` causes ``decode`` to output its current
     *  state." A poll — it changes nothing and emits the same burst any other
     *  accepted message would. Scoped to the left inlet, where the reference
     *  puts it: the middle and right inlets document an ``int`` method and
     *  nothing else.
     *
     *  ### What is not accepted
     *
     *  ``float``, ``list`` and ``anything`` are not registered on any inlet.
     *  Max documents ``bang`` and ``int`` for this object and nothing more, and
     *  a message handler must not log, so rather than a handler that silently
     *  swallows the message the inlets decline the type outright —
     *  ``GetAcceptedTypes`` then reports the object's real contract, which is
     *  more use to a patch than a method that eats what it cannot do. That is
     *  ``.spray``'s reading of the same situation.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing. The object is driven by its inlets, and an
     *  emitting ``Calculate()`` would restate the bank from a stimulus no patch
     *  sent — the rule ``.sel``, ``.trigger``, ``.bondo``, ``.cycle``,
     *  ``.bucket``, ``.spray`` and ``.funnel`` establish.
     *
     *  Nothing on any path allocates, locks or blocks. The outlets are built
     *  once, on the control thread, by the parameter callbacks before the object
     *  is wired or published — at most ``MAX_PORTS`` (256), the ceiling the
     *  family already uses, with a larger creation argument clamped and the
     *  clamp said once. A burst is three ints on the stack and one integer
     *  compare per outlet; no text is formatted and no buffer is needed, because
     *  every outlet carries an int the object computed rather than anything it
     *  has to forward.
     */
    PATCHER_CLASS(gDecode, YSE::OBJ::G_DECODE)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(SetBang)
    _INT_IN(SetInt)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Most outlets the object will build.
     *
     *  256, the ceiling ``.sel``, ``.trigger``, ``.bondo``, ``.buddy``,
     *  ``.cycle``, ``.bucket``, ``.spray`` and ``.funnel`` already use. Max
     *  documents no limit; a larger creation argument is clamped rather than
     *  honoured, and the clamp is said once on the control thread.
     */
    static constexpr int MAX_PORTS = 256;

    /** @brief Fewest outlets the object will build. */
    static constexpr int MIN_PORTS = 1;

    /**
     *  @brief Outlets with no creation argument.
     *
     *  **One** — Max: "Sets the number of outlets. The default is one outlet."
     *  The same default ``.bucket`` has, and the odd one out against ``.spray``
     *  and ``.funnel``, which default to two.
     */
    static constexpr int DEFAULT_PORTS = 1;

    /** @brief How many outlets the object has. At least one. */
    int OutletCount() const {
      return (int)outputs.size();
    }

    /**
     *  @brief The outlet the left inlet last selected.
     *
     *  0 before anything arrives — Max: "The left outlet is initially enabled."
     *  Unchanged by an index the object has no outlet for.
     */
    int Index() const {
      return index;
    }

    /** @brief The number the middle inlet last received. Max's "all on". */
    int Secondary() const {
      return secondary;
    }

    /** @brief The number the right inlet last received. Max's master mute. */
    int Primary() const {
      return primary;
    }

    /**
     *  @brief What outlet @p outlet would carry now — the value a ``bang``
     *         would put there.
     *
     *  False for an outlet the object does not have.
     */
    bool StateAt(int outlet) const;

  private:
    // The hierarchy, in one place: the right inlet beats the middle inlet, and
    // the middle inlet beats the selection. Static and pure so that StateAt()
    // and the send loop cannot drift apart — the loop passes a snapshot of the
    // three members, StateAt() passes the members themselves.
    static bool Resolve(int outlet, int primaryValue, int secondaryValue, int selected) {
      // Max: "Any positive number other than 0 sends a 0 out all outlets."
      if (primaryValue != 0) return false;
      // Max: "a number greater than 0 received in the middle inlet sends a 1 out
      // all outlets", and 0 "sends a 1 out the last outlet decoded".
      if (secondaryValue > 0) return true;
      return outlet == selected;
    }

    // Send the whole state, one int per outlet, right to left. The single exit
    // of the object, so the burst shape is decided in exactly one place.
    void Emit(YSE::THREAD thread);

    // Rebuild the outlets from the current creation argument, docs included.
    // Control thread only: called from the constructor and from the parameter
    // callbacks, all of which run before the object is wired or published.
    void ShapePorts();

    // The creation arguments, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::vector<std::string> creationArgs;

    // The outlet the left inlet selected. Max: "The left outlet is initially
    // enabled", so 0 rather than "none".
    int index = 0;

    // The middle inlet's last number, Max's "all on" override. 0 is the
    // pass-through case, which is why the object starts there.
    int secondary = 0;

    // The right inlet's last number, Max's master mute. Non-zero blanks the
    // whole bank without disturbing the two below it.
    int primary = 0;
  };
}
}
