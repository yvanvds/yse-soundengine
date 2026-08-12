#pragma once
#include "../pObject.h"
#include "../../headers/types.hpp"
#include <atomic>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Bounded stepper ``.incdec`` (issue #558).
     *
     *  Max's ``incdec`` — "increment or decrement a value", drawn as a pair of
     *  arrows over a number. The control for anything *nudged* rather than
     *  swept: transpose, octave, preset slot, bar count. Where ``.dial`` and
     *  ``.slider`` hand a host a continuous position to drag, this one hands it
     *  two buttons and a whole number that moves by a fixed amount, which is
     *  what those quantities actually want.
     *
     *  **Why this is not ``.counter``.** The two overlap — both hold a number
     *  and step it — and issue #558 asks the question outright, so here is the
     *  answer and the reason. They stay separate because *bang means opposite
     *  things on them*. On ``.counter``, Max's counter, a bang **steps**: that
     *  is the whole object, the thing you put behind a ``.metro`` to number
     *  events. On ``incdec``, Max's incdec, a bang **outputs the stored value
     *  without moving it**, because the arrows are what move it. One object
     *  cannot hold both meanings, and picking a winner would silently break
     *  whichever port lost. The rest follows from that: ``.counter`` is
     *  unbounded, one-directional and lives in MATH as patch plumbing;
     *  ``.incdec`` is bounded, two-directional and lives in GUI as a control a
     *  host renders. Bolting minimum, maximum and wrap onto ``.counter`` would
     *  have given it this object's parameters without giving it this object's
     *  grammar, which is the half that matters.
     *
     *  **The grammar**, and it is Max's:
     *
     *   - ``bang`` on the left inlet emits the stored value and does not move
     *     it.
     *   - an ``int`` or ``float`` on the left inlet *sets* the value and emits
     *     nothing — Max is explicit that the number arriving at the inlet "is
     *     not output directly". So a patch can place the stepper (from
     *     ``.loadmess``, from a preset) without firing everything downstream,
     *     and read it back with a bang when it wants to.
     *   - the words ``inc`` and ``dec`` step up and down by ``step`` and emit
     *     the result. ``set <n>`` is the int message spelled out.
     *   - the middle inlet is the same two arrows reachable from a cord: an int
     *     ``n`` moves ``n`` steps, signed, and emits. That is what lets a MIDI
     *     encoder — which sends +1 and -1 — drive the object with one
     *     connection, and 0 there is a read, exactly like a bang.
     *   - the right inlet sets the step size live, as ``.counter``'s right
     *     inlet does, for the coarse/fine pair a performance surface wants.
     *
     *  **The bounds.** ``minimum`` and ``maximum`` are taken as an ordered pair
     *  the way ``.pong`` takes its limits, so giving them the wrong way round
     *  still bounds against the right two numbers. ``wrap`` chooses what happens
     *  at the end of the range: 0 (the default) clamps, 1 carries around to the
     *  other side. The wrapped range is **inclusive on both ends** and the span
     *  is therefore ``maximum - minimum + 1`` — one past the top is the bottom
     *  — because this is a stepper over whole numbers and 11 stepping to 0 is
     *  what a twelve-note pitch class does. That is deliberately not
     *  ``.pong``'s float wrap, whose range is half-open at the top because a
     *  continuous phase has no "one past".
     *
     *  The default range is the whole int range, so an object with no arguments
     *  is unbounded and clamping is a no-op: it steps like ``.counter`` in two
     *  directions, and it saturates rather than overflowing at the ends.
     *
     *  **Bounding happens on the way out as well as on the way in.** Every
     *  write stores an already-bounded value, and every read bounds again. The
     *  second pass is what makes a live re-range visible immediately, and what
     *  keeps the object's promise on a stepper that was never touched: a
     *  ``.incdec 1 60 72`` starts at the stored 0, which is outside its range,
     *  and reads back as 60. With ``wrap`` on, that untouched 0 lands wherever
     *  the wrap puts it (65 for that range) rather than at an end — a stepper
     *  that wraps has no "bottom" to start at, so place it with ``set`` or a
     *  ``.loadmess`` if the starting point matters.
     *
     *  All the arithmetic runs in 64 bits and is bounded back into ``int``
     *  before it leaves, so no step, no set and no range can overflow — not
     *  even a step of ``INT_MIN``, whose negation is not representable.
     *
     *  The GUI value is the stored value as a single string, the scalar
     *  protocol ``.slider``, ``.dial`` and ``.counter`` already use. A stepper
     *  is one number and needs nothing structured, so issue #551's redesign is
     *  not pre-empted here.
     *
     *  All four parameters are scalars and the object registers no clear/parse
     *  callbacks, so ``Parameters::NeedsRebuild()`` is false and a live
     *  re-range or re-step rides the wait-free scalar plan (issue #234) instead
     *  of replacing the object.
     *
     *  Real time: every message path is a handful of compares, at most one
     *  64-bit ``%`` and one Send — no allocation, no lock, no I/O. The list
     *  handler reads its command words in place rather than building substrings,
     *  as ``.sustain`` and ``.flush`` do, because a list may well arrive on the
     *  audio thread. ``Calculate()`` does nothing: this object sends from its
     *  handlers, as ``.counter`` does, precisely so that a set can stay silent
     *  while a bang emits.
     *
     *  The host-thread/audio-thread window on the stored value is ``.slider``'s
     *  and ``.counter``'s: the value is a plain ``std::atomic<int>``, so a GUI
     *  write and an audio-thread read never tear, but nothing orders a write
     *  against the block already rendering. That is the contract every scalar
     *  GUI control in the patcher has (see the notes in
     *  Tests/patcher/test_patcher_object_races.cpp) and this object does not
     *  invent a different one.
     */
    PATCHER_CLASS(gIncDec, YSE::OBJ::G_INCDEC)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    _HAS_GUI

  private:
    // The stored value, bounded once more on the way out — see the class
    // comment on why both ends.
    int Value() const;

    // Clamp or wrap `value` into the ordered [minimum, maximum] range. Takes an
    // I64 because every caller has already done arithmetic that can leave the
    // int range.
    int Bound(I64 value) const;

    // Store `value` bounded. Emits nothing: the callers that emit do it
    // themselves, and the ones that must not are the reason this object sends
    // from its handlers at all.
    void Store(I64 value);

    // Move `steps` steps (signed) and emit the result.
    void Move(I64 steps, YSE::THREAD thread);

    // Shared by the int, float and list paths on all three inlets.
    void Number(int value, int inlet, YSE::THREAD thread);

    // Written by the host thread, read by the audio thread — see the class
    // comment.
    std::atomic<int> current;

    // Written by the parameter system (the scalar plan applies them on the
    // audio thread at the top of a block) and by the middle/right inlets, read
    // by every path. Plain fields, exactly as .pong holds its limits.
    int step;
    int minimum;
    int maximum;
    int wrap;
  };
}
}
