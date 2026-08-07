#pragma once
#include "../pObject.h"
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Tag incoming data with its inlet number and merge it to one
     *         outlet — ``.funnel`` (issue #480).
     *
     *  Max's ``funnel``, whose one-line summary is "Tag data with its inlet
     *  number": "When a number or list is received in any inlet, funnel outputs
     *  a list consisting of the inlet number followed the input."
     *
     *  The many-to-one collector. A bank of controls, a row of sensors, eight
     *  voices reporting their state — all of them want to reach the same
     *  destination, and all of the destination's logic depends on knowing
     *  *which* of them spoke. Without this object a patch either runs one cord
     *  per source all the way to the destination, or puts a ``.+`` or a message
     *  box in front of every source to stamp a constant on it by hand: N boxes
     *  to maintain, and N places to get the number wrong when a source is
     *  inserted. ``.funnel`` is that stamp, once, with the number taken from the
     *  wiring itself.
     *
     *  ### The exact inverse of ``.spray``
     *
     *  ``.spray`` (#479) takes ``2 60`` and sends 60 out outlet 2; this takes 60
     *  arriving at inlet 2 and sends ``2 60``. Max lists ``spray`` under See Also
     *  for that reason, and the pairing is literal: a ``.funnel n`` into a
     *  ``.spray n`` with the same offset is an *n*-way bus carried over a single
     *  patch cord, entering at inlet *i* and leaving at outlet *i*. That is the
     *  whole point of the two objects existing — the wire between them can cross
     *  a subpatch boundary, a ``.s``/``.r`` pair or a save file, none of which
     *  would carry *n* separate cords as cheaply.
     *
     *  ### What it is not
     *
     *  ``.bondo`` and ``.buddy`` also gather several inlets, but they gather them
     *  into a *set*: N outlets, and a release that says something about all of
     *  them at once. This has one outlet and says something about exactly one
     *  arrival — it merges a stream rather than synchronising one. Nothing here
     *  waits, nothing is stored on behalf of the release, and two values arriving
     *  at two inlets produce two output lists rather than one.
     *
     *  ``.switch`` also merges several inlets onto one outlet, but it merges by
     *  *selecting*: only the chosen inlet passes, and what comes out carries no
     *  record of where it came from. Here every inlet always passes and the
     *  record is the point.
     *
     *  ``.route`` and ``.sel`` are the objects that consume this output —
     *  ``.funnel`` writes the tag, they read it back — which is why the tag is
     *  the first element of the list rather than the last.
     *
     *  ### The tag, and the offset
     *
     *  The number prepended is the inlet index plus the offset, since Max's
     *  second creation argument "specifies an offset for the first inlet number.
     *  If no second argument is present, the inlets are numbered beginning with
     *  0", and the ``offset`` message "will offset the numbering of inlets by the
     *  number given". Both are the same value, so they are one value here rather
     *  than two mechanisms.
     *
     *  Adding where ``.spray``'s offset subtracts is what makes the two objects
     *  mirror images: a ``.funnel`` and a ``.spray`` given the same offset send
     *  inlet *i* to outlet *i*, whatever the offset is. It exists because the
     *  numbers a downstream object expects usually start where *it* starts —
     *  MIDI channels at 1, a table's rows at some base — and the alternative is
     *  a ``.+`` on the far side of the merge, which has to be kept in step with
     *  the funnel by hand.
     *
     *  Run-time state, as ``.spray``'s ``offset``, ``.cycle``'s ``thresh`` and
     *  ``.bucket``'s ``R2L`` are: the ``offset`` message does not write back to
     *  the parameters, so a saved patch carries the creation argument and not
     *  whatever the last message set.
     *
     *  ### ``bang``, and why the store holds a *number*
     *
     *  Max: "bang: In any inlet: The number of the inlet and the stored (most
     *  recently received) number in that inlet are sent out as a two-item list."
     *  So each inlet remembers the last number it was given and a bang re-sends
     *  it — the object is pollable as well as reactive, which is what lets a
     *  patch ask "what is source 3 saying?" without the source having to speak
     *  again.
     *
     *  "The stored ... **number**", and the result is a "**two-item** list", so
     *  the store holds one number and not one message. That decides the case Max
     *  leaves unsaid: a ``list`` or an ``anything`` arriving at an inlet is
     *  tagged and forwarded but does **not** become the stored value, because
     *  ``note 60`` is not a number and a bang that replayed it would send a
     *  three-item list out of a method documented to send two. Only ``int``,
     *  ``float`` and ``set`` write the store. It starts at 0, so a bang into an
     *  inlet nothing has reached yet is ``<tag> 0`` rather than silence — the
     *  reading that makes the object's output shape independent of its history.
     *
     *  ### ``set``
     *
     *  Max: "The word set followed by a list of numbers which correspond with
     *  the number of inlets, will set the input list of numbers without sending
     *  them through the outputs." So ``set`` addresses **every** inlet at once,
     *  element *k* writing inlet *k*'s store — the counterpart of the bang that
     *  reads one back, and the way a patch primes the whole bank before anything
     *  has spoken. It emits nothing, in any inlet.
     *
     *  Elements past the last inlet are dropped and a short list leaves the
     *  inlets it does not reach alone. A non-number element costs its position
     *  and leaves that inlet's store untouched rather than closing the gap,
     *  which is ``.spray``'s discipline for the same situation and for the same
     *  reason: this is a positional message, and silently shifting the rest
     *  would write every later value into the wrong inlet.
     *
     *  ``set`` and ``offset`` are matched in any inlet, as Max scopes them —
     *  unlike ``.buddy``'s ``clear``, which Max scopes to the left inlet and
     *  which is therefore kept there. A message word with nothing after it is
     *  the method with nothing to do: it is silently ignored rather than being
     *  tagged and forwarded as the symbol it also is, since a bare ``set``
     *  leaving as ``0 set`` would be a surprising way to answer a method call.
     *
     *  ### Spelling
     *
     *  Each value leaves as the kind it was *spelled* as, the discipline
     *  ``.trigger``, ``.bondo``, ``.cycle``, ``.bucket`` and ``.spray`` share:
     *  an object that forwards must not rewrite the type of what passes through
     *  it. Max says as much for this one outright — "In a list floats are not
     *  converted to ints" — and a list is forwarded by *text*, so its elements
     *  are not merely of the right kind but character for character what the
     *  patch sent. The tag itself is always an int.
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing. The object is driven by its inlets, and an
     *  emitting ``Calculate()`` would tag a value from a stimulus no patch sent
     *  — the rule ``.sel``, ``.trigger``, ``.bondo``, ``.cycle``, ``.bucket``
     *  and ``.spray`` establish.
     *
     *  Nothing on any path allocates, locks or blocks. The inlets and the store
     *  are built once, on the control thread, by the parameter callbacks before
     *  the object is wired or published — at most ``MAX_PORTS`` (256), the
     *  ceiling the family already uses, with a larger creation argument clamped.
     *  The one outlet's text goes through a buffer reserved to
     *  ``TEXT_CAPACITY`` at construction and refilled immediately before the
     *  send, so tagging a number touches no allocator and tagging a list up to
     *  that length is a copy into memory the object already owns; a longer one
     *  costs one reallocation, the same bound ``.spray``, ``.bondo`` and
     *  ``.regexp`` accept.
     */
    PATCHER_CLASS(gFunnel, YSE::OBJ::G_FUNNEL)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(SetBang)
    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Most inlets the object will build.
     *
     *  256, the ceiling ``.sel``, ``.trigger``, ``.bondo``, ``.buddy``,
     *  ``.cycle``, ``.bucket`` and ``.spray`` already use. Max documents no
     *  limit; a larger creation argument is clamped rather than honoured, which
     *  is what makes the store a fixed allocation, and the clamp is said once on
     *  the control thread.
     */
    static constexpr int MAX_PORTS = 256;

    /** @brief Fewest inlets the object will build. */
    static constexpr int MIN_PORTS = 1;

    /**
     *  @brief Inlets with no creation argument.
     *
     *  **Two** — Max: "If there is no argument there will be two inlets." The
     *  same default ``.spray`` has for its outlets, which is what makes the
     *  no-argument pair of the two objects already a working two-way bus.
     */
    static constexpr int DEFAULT_PORTS = 2;

    /**
     *  @brief Characters of a tagged message the object can send without
     *         allocating.
     *
     *  256, the same bound ``.spray``, ``.cycle``, ``.bondo`` and ``.regexp``
     *  accept. Tagging a bare number never comes near it; only a long incoming
     *  list can.
     */
    static constexpr std::size_t TEXT_CAPACITY = 256;

    /** @brief How many inlets the object has. At least one. */
    int InletCount() const {
      return (int)slots.size();
    }

    /**
     *  @brief The number added to an inlet index to make the tag it sends.
     *
     *  Max's second creation argument and the ``offset`` message, which are the
     *  same value. Run-time state once the object exists, so a saved patch
     *  carries the argument rather than the last message.
     */
    int Offset() const {
      return offset;
    }

    /** @brief The tag inlet @p inlet stamps on what arrives there. */
    int TagFor(int inlet) const;

    /** @brief True when inlet @p index's stored number is spelled as a float. */
    bool StoredIsFloat(int index) const;

    /** @brief Inlet @p index's stored number as an int. 0 out of range. */
    int StoredInt(int index) const;

    /** @brief Inlet @p index's stored number as a float. 0 out of range. */
    float StoredFloat(int index) const;

  private:
    // What one inlet last received, for `bang` to replay. A number, not a
    // message — see the header on why. Both spellings are kept rather than one
    // float, so a large int is replayed exactly rather than through the 24 bits
    // of mantissa a float would leave it.
    struct Slot {
      int intValue = 0;
      float floatValue = 0.f;
      bool isFloat = false;
    };

    // Send `<tag> <text>` out the one outlet, with `tag` the number inlet
    // `inlet` stamps. The single exit of the object, so the list shape is
    // decided in exactly one place.
    void Emit(int inlet, const char* text, std::size_t length, YSE::THREAD thread);

    // Replay inlet `inlet`'s stored number, tagged. Max's `bang`.
    void EmitStored(int inlet, YSE::THREAD thread);

    // Max's `set`: element k of the text at `at` writes inlet k's store, and
    // nothing is sent. Positional — a non-number element spends its place and
    // leaves that inlet alone.
    void Store(const std::string& text, std::size_t at);

    // Rebuild the inlets and the store from the current creation arguments,
    // docs included. Control thread only: called from the constructor and from
    // the parameter callbacks, all of which run before the object is wired or
    // published.
    void ShapePorts();

    // The creation arguments, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::vector<std::string> creationArgs;

    // One entry per inlet. Sized by the parameter callbacks before the object
    // is published and never resized by a message handler, so a `set` walking
    // it cannot race a structural change.
    std::vector<Slot> slots;

    // Max's inlet-numbering offset. Seeded from the second creation argument
    // and then owned by the `offset` message, so it does not survive a save.
    int offset = 0;

    // The one allocation a send would otherwise need.
    std::string listText;
  };
}
}
