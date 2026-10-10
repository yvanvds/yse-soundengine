#pragma once
#include "../pObject.h"
#include <atomic>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief A pitch on a staff — ``.nslider`` (issue #555).
     *
     *  Max's ``nslider``, "display or output a pitch on a musical staff". The
     *  other half of issue #555's pair: ``.kslider`` expresses a pitch as a key
     *  on a keyboard, this one expresses it as a note on a stave. Both hand a
     *  host a musically meaningful surface instead of a bare number box, and
     *  both feed the patcher's note objects directly.
     *
     *  ### It is not ``.i`` with a different name, and the accidental is why
     *
     *  A pitch alone is what ``.i`` already holds. What a staff needs and a
     *  number box does not is **how to spell the pitch**, because MIDI pitch 61
     *  is C sharp *or* D flat — the same key, two different notes, drawn on two
     *  different lines with two different signs. Nothing in the pitch decides
     *  it; the key the music is in does, and headless that has to come from
     *  somewhere the patch can set.
     *
     *  So the state is **two cells** under issue #551's protocol: cell 0 is the
     *  pitch and cell 1 is the accidental — ``1`` sharp, ``-1`` flat, ``0`` for
     *  a natural. With those two a host has everything it needs: the accidental
     *  gives the sign, and the pitch minus the accidental gives the natural the
     *  sign is attached to, which fixes the line or space to draw on. A single
     *  scalar cell could not, and a host left to guess would draw a different
     *  stave from the one the patch meant.
     *
     *  **The accidental is derived, and overridable.** For a white key it is
     *  always 0 — there is no such thing as a sharpened E drawn as an E. For one
     *  of the five black keys it is the ``spelling`` parameter's choice
     *  (``1`` for sharps, the default; anything negative for flats), unless the
     *  message that set the pitch said otherwise, in which case it is what that
     *  message said. That override lasts until the next bare pitch arrives, so
     *  a patch in F minor sets ``spelling -1`` once and a single note re-spelled
     *  as a sharp does not change the key it is in.
     *
     *  Black and white are computed from the pitch class rather than looked up:
     *  pitch classes 1, 3, 6, 8 and 10 are the black keys, which is one compare
     *  chain and no table at all. Issue #555 asks that this "reuse the existing
     *  ``music/`` scale primitives rather than a private table", and there is
     *  nothing there to reuse: ``YSE::scale`` is a set answering "is this pitch
     *  in the key", which is a different question, and ``MIDI::M_PITCH`` is a
     *  name enum in which ``CSM1`` and ``DFM1`` are *aliases of the same
     *  value* — so by construction it cannot answer how a pitch should be
     *  spelled. The requirement is met by needing no table on either side.
     *
     *  ### The grammar
     *
     *  One inlet, hot, and everything on it emits — the patcher's rule for a hot
     *  inlet, which ``.rslider`` states:
     *
     *   - an **int or a float** is a pitch, and resets the spelling to the
     *     ``spelling`` default. This is "play this note", and a note arriving
     *     from elsewhere in the patch carries no opinion about how to write it.
     *   - a **list** is ``<pitch> [<accidental>]``: the first number is the
     *     pitch and an optional second is the accidental to spell it with. Two
     *     numbers is exactly the string ``GetGuiValue()`` produces, which is
     *     #551's round trip; further numbers are ignored, ``.rslider``'s policy
     *     for a longer list.
     *   - ``set <index> <value>`` writes one cell, #551's cell write:
     *     ``set 0 <pitch>`` moves the note and leaves the spelling alone —
     *     unlike a bare int, because a cell write touches one cell by
     *     definition — and ``set 1 <accidental>`` re-spells the note without
     *     moving it.
     *   - a **bang** re-sends.
     *
     *  **Outlets.** Pitch on outlet 0 and accidental on outlet 1, two ints, and
     *  they fire right to left — the ordering guarantee ``.trigger`` and
     *  ``.bangbang`` document — so the accidental is in hand by the time the
     *  pitch lands on a hot inlet downstream, which is ``.kslider``'s and
     *  ``.flush``'s reason for sending a pair that way round. Both carry one
     *  sample of the state, taken once at the top of ``Calculate()``: a note
     *  re-spelled mid-send would otherwise report two different notes down two
     *  cords of the same event.
     *
     *  ### Bounds
     *
     *  ``minimum`` and ``maximum`` are the pitches the stave covers, the
     *  family's parameter pair (``.dial``'s, ``.incdec``'s, ``.rslider``'s),
     *  taken as an ordered pair so a reversed argument still bounds against the
     *  right two numbers, and clamped on the way out as well as in
     *  (``.incdec``'s rule) so a live re-range shows immediately. They are
     *  themselves confined to 0-127: a stave here draws MIDI pitches, and a
     *  pitch outside that range is not one. The default 0-127 is therefore the
     *  whole range, and ``.nslider 36 96`` is a five-octave stave.
     *
     *  Both parameters are scalars and the object registers no clear/parse
     *  callbacks, so ``Parameters::NeedsRebuild()`` is false and a live
     *  re-range rides the wait-free scalar plan (issue #234) instead of
     *  replacing the object.
     *
     *  ### Deliberately not here
     *
     *  Max's ``clear``, which blanks the display. There is no display to blank
     *  and no pitch that means "nothing" — 0 is C minus one, a real note — so
     *  the message would have to invent an empty state that every reader of the
     *  two cells would then have to handle. A host that wants to draw nothing
     *  draws nothing.
     *
     *  Everything about drawing: ``clef``, the staff and note colours, sizes and
     *  the display-only ``set`` of Max's own (which is spoken for by the
     *  protocol's ``set`` in any case). The YSE patcher is headless and issue
     *  #555 names any editor representation a non-goal.
     *
     *  Chords. Max's nslider displays several notes at once; that is a *list* of
     *  pitches, and the patcher's answer to a list of pitches is the object that
     *  holds one — ``.multislider`` for a bank, or several ``.nslider``s. Two
     *  cells that are a pitch and its spelling cannot also be N pitches without
     *  the cell count meaning two different things.
     *
     *  ### Real time
     *
     *  No path allocates, locks or blocks. The state is two ``std::atomic<int>``
     *  scalars, the outlets carry ints so no send builds a string, and the list
     *  handler compares its keyword in place and parses through
     *  ``ExprParseFloatList`` rather than a stream, because a list may well
     *  arrive on the audio thread down a cord. The *pair* is not atomic — a host
     *  can read cell 0 from before an edit and cell 1 from after it — which is
     *  the non-atomic multi-cell read the protocol names and permits for a
     *  repaint; a host that needs a coherent pair uses the bulk read, which
     *  samples both under one call.
     */
    PATCHER_CLASS(gNSlider, YSE::OBJ::G_NSLIDER)
    _NO_MESSAGES
    _DO_CALCULATE

    _BANG_IN(BangIn)
    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    _HAS_GUI_CELLS

    /** @brief Lowest pitch a stave can be bounded to, and the bottom of MIDI. */
    static constexpr int MIN_PITCH = 0;

    /** @brief Highest pitch a stave can be bounded to, and the top of MIDI. */
    static constexpr int MAX_PITCH = 127;

    /** @brief The accidental a black key gets when nothing overrides it — a
     *         sharp, which is Max's own default spelling. */
    static constexpr int DEFAULT_SPELLING = 1;

    /** @brief The note on the stave, clamped into the minimum-maximum bounds. */
    int Pitch() const;

    /**
     *  @brief How the note is spelled: ``1`` sharp, ``-1`` flat, ``0`` natural.
     *
     *  0 for every white key, whatever was asked for — a natural cannot be
     *  drawn with a sign. Otherwise the override the last message set, or the
     *  ``spelling`` parameter when it set none.
     */
    int Accidental() const;

  private:
    // Clamp `value` into the ordered [minimum, maximum] bounds, themselves
    // confined to MIN_PITCH-MAX_PITCH.
    static int Bound(int value, int low, int high);

    // Whether `value` is one of the five black keys in its octave — the only
    // ones an accidental can attach to. Arithmetic on the pitch class, not a
    // table; see the class comment on why there is nothing in `music/` to reuse.
    static bool IsBlack(int value);

    // Store a pitch. `respell` resets the per-note override to the `spelling`
    // default, which a bare pitch does and a cell write does not.
    void StorePitch(int value, bool respell);

    // Store the per-note spelling override: positive a sharp, negative a flat,
    // 0 "use the parameter". Kept as given rather than resolved here, so a
    // white key that later becomes a black one through `set 0` is spelled the
    // way the override asked.
    void StoreAccidental(int value);

    // The note, and how it is written. Written by the host thread, read by the
    // audio thread — see the class comment on the window between them.
    std::atomic<int> pitch;
    std::atomic<int> accidental;

    // Written by the parameter system (the scalar plan applies them on the
    // audio thread at the top of a block), read by every path. Plain fields,
    // exactly as `.rslider` and `.incdec` hold their bounds.
    int minimum;
    int maximum;
    int spelling;
  };
} // namespace PATCHER
} // namespace YSE
