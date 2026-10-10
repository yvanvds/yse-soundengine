#pragma once
#include "../pObject.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Deal successive messages to successive outlets, wrapping round —
     *         ``.cycle`` (issue #477).
     *
     *  Max's ``cycle``: "Each incoming number is sent to the next outlet,
     *  wrapping around to the first outlet after the last has been reached."
     *
     *  The dealer. One cord goes in, N cords come out, and consecutive messages
     *  land on consecutive outlets — the natural way to spread notes across a
     *  bank of voices, alternate between two effect chains, or split one stream
     *  into parallel processing paths. Without it a patch counts by hand: a
     *  ``.counter`` wrapped at N into a ``.gate``'s select inlet, with the value
     *  routed through the gate's other inlet and the two inlets' firing order to
     *  get right. That is three boxes and an ordering hazard to say one thing.
     *
     *  ### What it is not
     *
     *  ``.poly`` allocates by *voice availability* — it asks which voice is
     *  free. ``.cycle`` asks nothing: it rotates blindly, so the same outlet
     *  comes round every N messages whatever the receivers are doing. That is a
     *  feature where the receivers are stateless or identical (parallel filters,
     *  a round of counters) and a bug where they are not (a bank of voices that
     *  can still be sounding), which is the one line to keep in mind when
     *  choosing between the two.
     *
     *  ``.gate`` and ``.switch`` route by an index the *patch* supplies; here
     *  the index is the object's own, advanced by the traffic itself. And
     *  ``.trigger`` fans **one** message out **every** outlet, where this sends
     *  each message out exactly **one** — so there is no right-to-left ordering
     *  guarantee to state, because only one outlet ever fires per message.
     *
     *  ### The rotation, and where it is settled
     *
     *  The outlet is taken, the position is advanced, and only then is the
     *  message sent. Settling first rather than last is the same correctness
     *  argument ``.onebang``, ``.togedge``, ``.next``, ``.match`` and ``.buddy``
     *  make: the send path is synchronous and re-entrant, so a patch that loops
     *  an outlet back into this inlet arrives here again *inside* the ``Send``,
     *  and must find the object already pointing at the next outlet. With the
     *  advance after the send, a self-wired ``.cycle`` would hand every message
     *  of the loop the same outlet and then advance once for each on the way
     *  back out — the rotation read backwards.
     *
     *  ### A list is dealt; an ``anything`` is not
     *
     *  Max gives ``list`` the description "The stream of ints, floats, or
     *  symbols to be directed to successive outlets" — plural outlets for one
     *  message, so the elements of a list are dealt one per outlet rather than
     *  the list going out whole. That is the "deal a stream into parallel paths"
     *  use, said in one message instead of N.
     *
     *  Which messages count as a list is Max's own parser rule, and the same one
     *  ``.bondo`` (#474) already applies for the same reason: this patcher has
     *  one text message type behind Max's two methods, so the **leading token**
     *  decides. A message whose first token is a number is Max's ``list`` and is
     *  dealt token by token; anything else is Max's ``anything`` and leaves one
     *  outlet **verbatim**. Splitting a symbol-led message would be worse than
     *  useless here — ``note 60 100`` dealt across three outlets arrives
     *  downstream as three unrelated fragments, and every object that matches on
     *  a leading word (``.route``, ``.sel``, ``.match``) stops seeing the word.
     *
     *  Each dealt token is forwarded **as the kind it was spelled as**: an int
     *  token leaves as an int, a float token as a float, a non-numeric one as
     *  text. The int/float test is ``TokenLooksLikeFloat``, the same answer
     *  ``.trigger`` classifies its constants with and ``.bondo`` stores its
     *  spread elements by, so ``1 2 3`` does not come back out as ``1. 2. 3.``.
     *
     *  ### ``set``, and why it is not merely a jump
     *
     *  Max: "The word set, followed by a number, specifies an outlet to which
     *  the next input should be directed." It emits nothing — it moves the
     *  position, so the *next* message lands where it was told and the rotation
     *  continues from there. That is what makes a phrase like "restart the deal
     *  at the top" one message rather than a rebuild of the object.
     *
     *  An index outside ``0..n-1`` is **ignored** and leaves the position where
     *  it was, which is ``.gate``'s discipline for its select inlet. Wrapping it
     *  would be defensible — the object wraps everything else — but a ``set 4``
     *  on a three-outlet object is a patch that has miscounted, and silently
     *  redirecting its stream to outlet 1 hides the mistake in exactly the place
     *  it is hardest to see.
     *
     *  ### Event mode: ``thresh`` and the second creation argument
     *
     *  Max's second argument "sets the output mode. If it is non-zero, cycle
     *  detects separate 'events' and restarts at the leftmost outlet when a new
     *  event occurs", and the ``thresh`` message sets the same mode at run time.
     *
     *  The name invites a clock, and a clock would be the wrong object — the
     *  same trap ``.next`` (#471) documents. Max's "event" is a *logical* event:
     *  one stimulus and everything it goes on to cause. Two clicks are two
     *  events however fast you click; the three bangs of one ``.trigger b b b``
     *  are one event however slow the patch is. This patcher has exactly that
     *  notion already — ``CurrentMessageEvent()`` (see ``inlet.h``) hands out one
     *  id per outermost dispatch — so event mode is a comparison of ids and not
     *  an elapsed time: no clock, no threshold, and nothing that needs a sleep
     *  to test.
     *
     *  So with mode on, the first message of every new stimulus goes out outlet
     *  0 and the rest of that stimulus's burst carries on rotating. A three-note
     *  chord arriving as one burst is dealt 0, 1, 2 and the next chord starts at
     *  0 again, however long the first one was — which is the difference between
     *  a bank of voices that stays aligned with the music and one that drifts by
     *  one voice per odd-sized chord. With mode off (the default) the rotation
     *  ignores event boundaries entirely and just keeps counting.
     *
     *  ``set`` still works in event mode and is not a special case: the restart
     *  is applied when the message arrives, so a ``set`` is honoured for the
     *  rest of *its own* event and a later event restarts at 0 over the top of
     *  it. That falls out of the ordering rather than being written down twice.
     *
     *  ``thresh`` changes the mode for as long as the object lives but is not a
     *  parameter, so it does not survive a DumpJSON / ParseJSON round trip — the
     *  creation argument is what a saved patch carries, exactly as ``.bondo``'s
     *  ``set`` and ``.uzi``'s ``pause`` are run-time state rather than saved
     *  state.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing. The object is driven by its inlet, and an
     *  emitting ``Calculate()`` would deal one message per DSP block from a
     *  stimulus no patch sent — the rule ``.sel``, ``.trigger``, ``.past``,
     *  ``.bondo`` and ``.buddy`` establish.
     *
     *  Nothing on any path allocates, locks or blocks. The outlets are built
     *  once, on the control thread, by the parameter callbacks before the object
     *  is wired or published — at most ``MAX_PORTS`` (256), the ceiling ``.sel``,
     *  ``.trigger``, ``.bondo`` and ``.buddy`` already use — and never resized
     *  afterwards. A bang, int or float costs one compare, two stores and one
     *  ``Send``. Dealing a list walks the text once and sends per token; a
     *  non-numeric token is copied into a buffer reserved to ``TEXT_CAPACITY``
     *  at construction, so it costs no allocation up to that length and one
     *  beyond it — the same bound ``.bondo`` and ``.regexp`` accept.
     */
    PATCHER_CLASS(gCycle, YSE::OBJ::G_CYCLE)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(SetBang)
    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Most outlets the object will build.
     *
     *  256, the ceiling ``.sel``, ``.trigger``, ``.bondo`` and ``.buddy``
     *  already use. Max documents no limit; a larger creation argument is
     *  clamped rather than honoured.
     */
    static constexpr int MAX_PORTS = 256;

    /** @brief Fewest outlets the object will build. */
    static constexpr int MIN_PORTS = 1;

    /**
     *  @brief Outlets with no creation argument.
     *
     *  Max: "If there is no argument, there will be one outlet." Degenerate on
     *  purpose — a one-outlet ``.cycle`` is a pass-through — but it is the
     *  documented default and a patch that meant more says so.
     */
    static constexpr int DEFAULT_PORTS = 1;

    /**
     *  @brief Characters of a dealt text token the object can forward without
     *         reallocating.
     *
     *  The buffer is reserved to this at construction. Longer text is still
     *  forwarded — correctness never depends on the bound — at the cost of one
     *  reallocation on the sending thread.
     */
    static constexpr std::size_t TEXT_CAPACITY = 256;

    /** @brief How many outlets the object has. At least one. */
    int OutletCount() const {
      return (int)outputs.size();
    }

    /**
     *  @brief Which outlet the next message will leave by.
     *
     *  Always in ``0..OutletCount()-1``. 0 on a fresh object, after a wrap, and
     *  — in event mode — at the start of every new logical event.
     */
    int NextOutlet() const {
      return next;
    }

    /**
     *  @brief Whether the object restarts at outlet 0 for each new logical
     *         event — Max's output mode, set by the second creation argument or
     *         by ``thresh``.
     */
    bool EventMode() const {
      return eventMode;
    }

  private:
    // Note that a message has arrived and, in event mode, restart at outlet 0
    // when it belongs to a new logical event. Called by *every* handler,
    // including `set` and `thresh`, so that a `set` and the message it was meant
    // for share an event and the set is not undone between them.
    void NoteEvent();

    // Take the current outlet and advance the position past it. Advancing
    // before the caller sends is what keeps a self-wired outlet from re-entering
    // onto the outlet it is already using.
    int TakeOutlet();

    // Deal a Max `list` — a message whose first token is a number — one token
    // per outlet, forwarding each as the kind it was spelled as.
    void DealTokens(const std::string& text, YSE::THREAD thread);

    // Rebuild the outlets from the current creation arguments, docs included.
    // Control thread only: called from the constructor and from the parameter
    // callbacks, all of which run before the object is wired or published.
    void ShapePorts();

    // The creation arguments, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::vector<std::string> creationArgs;

    // Where the next message goes. Kept inside 0..outputs.size()-1 at all times,
    // so no send needs a bounds test the rotation has not already done.
    int next = 0;

    // Max's output mode. Set by the second creation argument and by `thresh`;
    // run-time state rather than a parameter, so `thresh` does not survive a
    // save.
    bool eventMode = false;

    // The event the previous message belonged to, and whether there was one.
    // Compared, never interpreted — the same treatment `.next` gives them, and
    // for the same reason: ids are opaque, and 0 means "no dispatch in
    // progress" rather than an event other messages can belong to.
    std::uint64_t lastEvent = 0;
    bool seen = false;

    // Scratch for one non-numeric token of a dealt list. A member rather than a
    // local so the reservation survives between messages and dealing text costs
    // no allocation.
    std::string tokenText;
  };
}
}
