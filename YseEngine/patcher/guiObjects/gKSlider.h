#pragma once
#include "../pObject.h"
#include <atomic>
#include <cstddef>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief A piano keyboard as one control — ``.kslider`` (issue #555).
     *
     *  Max's ``kslider``, "output pitch and velocity from a keyboard display".
     *  The patcher already has every way of expressing a *number* as a control
     *  — ``.slider`` a position, ``.dial`` a value, ``.rslider`` a span,
     *  ``.multislider`` a bank — and none of expressing a **note**. A pitch is
     *  not a number that happens to run 0-127: it is one of 128 named things,
     *  five of every twelve of which are black, and the surface a musician
     *  reaches for to pick one is a keyboard. This is that surface, and it feeds
     *  the patcher's note API — ``.noteon``, ``.makenote``, ``.flush``,
     *  ``.midiformat`` — in the pair-of-ints those objects already speak.
     *
     *  ### The state is the keyboard, all 128 keys of it
     *
     *  Issue #555 asks for "bounded held-note state (a fixed 128-entry bitmap,
     *  no allocation)", and the GUI protocol (issue #551) asks for state as a
     *  list of cells. Both are answered by the same array: **one cell per MIDI
     *  pitch, holding that key's velocity, 0 meaning the key is up.** So
     *  ``GetGuiValueCount()`` is always 128, cell *i* is pitch *i*, and a host
     *  draws key *i* held exactly when cell *i* is non-zero — no decoding, no
     *  index arithmetic, no separate "how many are down" question whose answer
     *  could disagree with the cells.
     *
     *  A velocity per key rather than a bare bit, because the object's whole
     *  output is a *pair*: a bitmap could say which keys are down but not what
     *  to re-send for one, so a bang could not replay what it is holding and a
     *  host could not draw a velocity-shaded keyboard. It costs 512 bytes, all
     *  of it allocated with the object.
     *
     *  Unlike ``.multislider``'s bank the count is **fixed**: a keyboard is 128
     *  keys because MIDI is, so there is no ``size``, no ``listresize`` and no
     *  live re-count. That makes every read a plain indexed load — the cells are
     *  independent ``std::atomic<int>`` scalars, exactly as ``.multislider``'s
     *  and ``.matrixctrl``'s are, so a host poll and an audio-thread message
     *  need nothing between them.
     *
     *  It does **not** share a held-note store with ``.flush`` (#540),
     *  ``.sustain`` (#541) or ``.midiflush`` (#537), and mFlush.h already
     *  explains why those three do not share one either: the sets are keyed
     *  differently (a bare pitch here and there, a channel-plus-pitch in
     *  ``.midiflush``, scheduler handles in ``.makenote``) and what they have in
     *  common is one line of bit-twiddling. This one is keyed differently again
     *  — it holds a *velocity* per pitch rather than a bit, because it has to be
     *  able to hand the key back.
     *
     *  ### The grammar, which is Max's plus the family's pair convention
     *
     *  Two inlets, ``.flush``'s and ``.stripnote``'s shape rather than Max's
     *  single one, because in this patcher a pitch/velocity pair is written that
     *  way everywhere:
     *
     *   - **inlet 0 (hot), the pitch.** An int or a float plays that pitch at
     *     the stored velocity — pressing a key. A list is Max's list method and
     *     ``.flush``'s inlet distribution on one cord: ``60 100`` presses key 60
     *     at velocity 100 and ``60 0`` releases it, 0 being how the whole MIDI
     *     world spells a release. A list of exactly 128 numbers is the whole
     *     state, which is issue #551's round trip. ``set <index> <value>`` is
     *     #551's cell write, and on this object it is the same message as the
     *     pair — the index *is* the pitch — which is the happy case
     *     ``.multislider`` also found and ``.rslider`` could not.  ``clear``
     *     releases every key it is holding, which is Max's message and
     *     ``.flush``'s bang. A **bang** re-sends every key still held.
     *   - **inlet 1 (cold), the velocity** for pitches arriving *after* it,
     *     stored silently. Max has no such inlet — its kslider is played with a
     *     mouse, which supplies a velocity from where the key was clicked — and
     *     headless there is nothing else a bare pitch could be paired with.
     *
     *  ### What emits, and why not everything
     *
     *  Every message that changes a key emits the pair for the key it changed,
     *  which is ``.matrixctrl``'s conclusion reached for its reason: headless,
     *  the outlet is the only way a change reaches anything, so an object whose
     *  purpose is to play notes cannot have a form that plays none. But the two
     *  *bulk* messages emit **what actually changed**, not every cell:
     *
     *   - a whole-state write sends a pair for each key whose velocity differs
     *     from what it was — attacks for keys that went down, releases for keys
     *     that came up, nothing for the ones that did not move;
     *   - ``clear`` sends a release for each key that was held;
     *   - a bang sends a pair for each key that *is* held.
     *
     *  Replaying all 128 keys instead — ``.matrixctrl``'s full dump — would send
     *  120-odd ``<pitch> 0`` releases for keys nobody touched and re-attack the
     *  ones already sounding, which downstream of a synth is not a repaint but a
     *  retrigger. A note controller's outlet drives *events*, and only a
     *  transition is one.
     *
     *  The single-key forms emit unconditionally even when the velocity is
     *  unchanged, because there the patch said "play this" and a repeated
     *  note-on at the same velocity is a thing a musician does on purpose.
     *
     *  A dump is guarded against re-entering itself, ``.matrixctrl``'s and
     *  ``.matrix``'s guard: outlet 0 carries pitches and inlet 0 takes pitches,
     *  so wiring the control back into itself is one cord away, and each line of
     *  a running dump would otherwise start a dump of its own.
     *
     *  **Outlets.** Pitch on outlet 0 and velocity on outlet 1, two ints —
     *  ``.flush``'s, ``.stripnote``'s, ``.makenote``'s and ``.noteon``'s shape,
     *  so the pair is wired across rather than packed and unpacked again. They
     *  fire **right to left**, the velocity first, and that is load-bearing
     *  rather than cosmetic: everything downstream that takes a pair takes the
     *  velocity on a cold inlet and the pitch on a hot one, so a pitch sent
     *  first would carry the previous note's velocity.
     *
     *  ### Bounds are MIDI's, and are not parameters
     *
     *  Pitch runs 0-127 because there are 128 keys, and velocity 0-127 because a
     *  cell is a MIDI velocity. Neither is a creation argument: the family's
     *  ``minimum`` / ``maximum`` describe a range a patch *chooses*, and nothing
     *  chooses how many keys a keyboard has. A pitch outside the range is
     *  **dropped** rather than clamped or passed on — ``.flush`` passes an
     *  untrackable pitch through because it is a watcher in the middle of a
     *  cord, while this object is a *source*, so a pitch it cannot display is a
     *  key it cannot press, and filing it under a pitch that is not its own
     *  would light the wrong key and release a note nobody played.
     *
     *  ### Deliberately not here
     *
     *  Max's ``mode`` attribute (monophonic / polyphonic). Monophony exists
     *  there because a kslider is *clicked* and a mouse can only be in one
     *  place; headless there is no click, a control whose entire state is a
     *  128-key held set is not usefully restricted to one key, and issue #551's
     *  round trip — restoring a 128-number state — could not even be honoured in
     *  a mode that permits one. A patch that wants monophony puts ``.poly`` or
     *  ``.flush`` in the cord, which is where voice allocation lives.
     *
     *  Everything about drawing: ``offset``, the key range attributes, colours
     *  and sizes. The YSE patcher is headless and issue #555 names any editor
     *  representation a non-goal — how a host draws the 128 cells it polls is
     *  the host's business.
     *
     *  ### What persists
     *
     *  The ``velocity`` parameter, and nothing else — no ``DumpState``
     *  override. pObject.h's rule: "a parameter is what the object was *created*
     *  with, not what it has since been told", so a reload brings back a
     *  keyboard with no key down. The form a host stores held keys in is
     *  ``GetGuiValue()``, which is what ``.preset`` is for and what inlet 0
     *  takes back.
     *
     *  ### Real time
     *
     *  No path allocates, locks or blocks. The keyboard is a fixed member array
     *  built with the object; the outlets carry ints, so no send path builds a
     *  string at all and there is no reserved buffer to grow. The list handler
     *  compares its keywords and walks its numbers in place rather than into a
     *  128-wide stack buffer, because a whole-state list may well arrive on the
     *  audio thread down a cord. Cells are read and written ``relaxed``: they
     *  are independent scalars, and the protocol explicitly permits a host to
     *  see a torn *frame*.
     */
    PATCHER_CLASS(gKSlider, YSE::OBJ::G_KSLIDER)
    _NO_MESSAGES
    // Every send happens in a handler: a bang and a `clear` each emit a whole
    // run of pairs, which a Calculate() holding one pending pair could not
    // express. `.matrixctrl`'s arrangement, for `.matrixctrl`'s reason.
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    _HAS_GUI_CELLS

    /** @brief Keys on the keyboard, which is the whole MIDI note range and the
     *         fixed GUI cell count. */
    static constexpr int PITCHES = 128;

    /** @brief Highest velocity a key can hold — MIDI's, and the reason the
     *         bounds are not creation arguments. */
    static constexpr int MAX_VELOCITY = 127;

    /**
     *  @brief Velocity a bare pitch is played at with no creation argument and
     *         nothing sent to inlet 1 — **100**.
     *
     *  Max needs no such default: its kslider takes its velocity from where the
     *  key was clicked. Headless there is no click, and 0 — ``.flush``'s
     *  starting velocity — would make every bare pitch a release, so the object
     *  has to start somewhere a key can actually go down. 100 is the value
     *  ``.makenote`` and the MIDI world use for "played normally".
     */
    static constexpr int DEFAULT_VELOCITY = 100;

    /** @brief Velocity @p pitch is held at, or 0 for a key that is up or a
     *         pitch the keyboard does not have. Diagnostics and tests; a host
     *         reads the same thing through ``GetGuiValueAt``. */
    int VelocityOf(int pitch) const;

    /** @brief Keys currently down: what a bang would re-send. Diagnostics and
     *         tests — a patch sees the same thing by banging. */
    int Held() const;

    /** @brief The velocity the next bare pitch will be played at — inlet 1's
     *         store, seeded by the ``velocity`` parameter. */
    int Velocity() const {
      return velocity.load(std::memory_order_relaxed);
    }

  private:
    // Clamp a velocity into 0-MAX_VELOCITY. A NaN — which only reaches here
    // through ExprToInt, so never — and anything outside the range lands on the
    // nearer end, because every value this object reports is one a MIDI velocity
    // can be.
    static int BoundVelocity(int value);

    // Whether `pitch` is a key at all. A pitch the keyboard does not have is
    // dropped rather than clamped; see the class comment.
    static bool IsKey(int pitch);

    // Press or release one key and emit the pair unconditionally — the
    // single-key forms, where the patch said "play this". The bulk forms write
    // cells directly and emit only what moved.
    void Play(int pitch, int noteVelocity, YSE::THREAD thread);

    // One pitch/velocity pair out of the two outlets, right to left.
    void Emit(int pitch, int noteVelocity, YSE::THREAD thread);

    // Every key that is down, as pairs, in ascending pitch order — a bang. With
    // `release` the pairs carry velocity 0 and the keyboard is emptied as they
    // go, which is `clear`. Guarded against re-entering itself.
    void SendHeld(bool release, YSE::THREAD thread);

    // Issue #551's whole-state write: 128 velocities, of which only the keys
    // that actually moved are emitted. Returns nothing; the sends happen inside.
    void RestoreAll(const char* text, std::size_t length, YSE::THREAD thread);

    // The keyboard: cell `i` is the velocity pitch `i` is held at, 0 for a key
    // that is up. Fixed size and allocated with the object — this is written
    // from the audio thread, where an array that could grow has no place.
    std::atomic<int> keys[PITCHES];

    // The velocity pitches arriving on inlet 0 are paired with. A registered
    // parameter *and* live state, `.multislider`'s `size` arrangement: the
    // creation argument sets it through the wait-free scalar plan (issue #234)
    // and inlet 1 changes it afterwards, both being this one store.
    std::atomic<int> velocity;

    // True while SendHeld() is emitting. Outlet 0's pitches are exactly what
    // inlet 0 accepts, so wiring the control back into itself is one cord away
    // and every pair of a running dump would otherwise start a dump of its own.
    bool dumping = false;
  };
} // namespace PATCHER
} // namespace YSE
