#pragma once
#include "../math/gExprEval.h"
#include "../pObject.h"
#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief A breakpoint function editor as one control — ``.function``
     *         (issue #561).
     *
     *  Max's ``function``, "draw or store a set of x, y points as
     *  floating-point numbers". A bounded, sorted store of breakpoints that
     *  answers any x with the interpolation between its neighbours and bangs
     *  out the whole envelope as a ramp list ``.line`` and ``.bline`` consume.
     *  Every hand-drawn envelope, velocity curve, filter sweep and automation
     *  shape is authored in one of these; the patcher's ADSR envelopes are the
     *  fixed-shape special case this object generalises past.
     *
     *  ### Not ``.funbuff``, and why the store is not shared
     *
     *  Issue #561 names the overlap and asks for the storage decision up
     *  front, so here it is: the two objects keep **separate stores**, on
     *  purpose. ``.funbuff`` (#497) is an *integer* function — Max converts a
     *  float to an int on either of its inlets, its plain lookup is a floor
     *  lookup (a step function), and its exact-int ``LowerBound`` is
     *  load-bearing: an int past 2^24 does not survive a trip through a
     *  float, and every store and delete goes through it. This object is a
     *  *float* function with a per-point curvature, and its lookup is the
     *  interpolation ``.funbuff`` deliberately keeps behind a separate
     *  message. One store would either quantise this object's curves —
     *  defeating the reason it exists — or widen ``.funbuff``'s ints and
     *  break its exactness. What they share is the shape of the invariant (a
     *  bounded array sorted by its own key, one y per x) and the guard model,
     *  not code.
     *
     *  ### The grammar
     *
     *  One inlet, hot, Max's left inlet:
     *
     *   - an **int or float** is taken as an x and answers with the
     *     interpolated y out outlet 0 — Max's "outputs a corresponding Y
     *     value ... produced by linear floating-point interpolation", shaped
     *     by the segment's curve here. An x outside the stored span answers
     *     with the nearest end's y, which is what every envelope does at its
     *     ends; an empty function answers nothing.
     *   - a **bang** sends the whole function out outlet 1 as one ramp list —
     *     see below.
     *   - a **list of two numbers** ``<x> <y>`` adds a breakpoint (Max's list
     *     method), replacing the one already at that exact x — a function
     *     holds one y per x. The new point's curve is 0, linear.
     *   - a **list of 3N numbers** is the whole state, ``<x> <y> <curve>``
     *     per point — the exact string ``GetGuiValue()`` produces, issue
     *     #551's round trip. The store is cleared and refilled through the
     *     same sorted, clamped path every other write uses.
     *   - ``set <index> <x> <y> [<curve>]`` rewrites the point at that
     *     position (ascending-x order), the protocol's cell write. The store
     *     re-sorts, so the index a moved point answers to afterwards may
     *     change. With no curve given the point keeps the curve it had, which
     *     is Max's own split between his three- and four-element point
     *     messages.
     *   - ``setcurve <index> <curve>`` re-curves one point — Max's message,
     *     without the mode-1 gate (see below).
     *   - ``nth <index>`` answers that point's y out outlet 0 — Max's
     *     "outputs the Y value of the breakpoint".
     *   - ``clear`` empties the store; ``clear <index>`` removes one point —
     *     Max's clear, with and without indices.
     *   - ``dump`` sends every point as ``<index> <x> <y> <curve>`` out
     *     outlet 2, ascending — Max's dump outlet, made headless.
     *
     *  **Writes are silent.** The sibling controls (``.multislider``,
     *  ``.matrixctrl``) emit on every write because their outlet mirrors
     *  their state and is the only way a change reaches anything. This
     *  object's outlet 1 is not a mirror, it is a *trigger*: the ramp list
     *  restarts whatever ``.line`` it feeds, so a control that replayed its
     *  envelope on every edit would fire spurious ramps exactly while a patch
     *  is being drawn into. Max's ``function`` is silent on edit for the same
     *  reason, and the GUI value protocol explicitly leaves "does a restore
     *  emit" per object.
     *
     *  ### The ramp list
     *
     *  ``y0 0 y1 (x1-x0) y2 (x2-x1) ...`` — one ``<target> <time>`` pair per
     *  point, which is exactly the breakpoint grammar ``.line`` and
     *  ``.bline`` read ("four or more numbers are breakpoint pairs"). The
     *  first pair's time is 0, so the consumer jumps to the first y and then
     *  ramps through the rest; the absolute x of the first point is dropped,
     *  which is Max's default (his ``outputmode`` attribute, not ported, is
     *  what would add it). Max emits an *odd* list — a bare leading y0 —
     *  because ``line~`` treats a leading singleton as a jump; ``.line``
     *  reads pairs, so the jump is spelled as the zero-time pair instead and
     *  the observable ramp is the same. Curves are flattened to their linear
     *  segments here: the patcher has no ``curve~`` to speak triples to, so
     *  the curved shape lives in the query path, where this object does its
     *  own interpolating.
     *
     *  ### Curvature
     *
     *  Each point carries a curve in [-1, 1]; a point's curve shapes the
     *  segment *arriving* at it, so the first point's curve is unused. 0 is
     *  linear. The shape is ``t^e`` with ``e = (1+c)/(1-c)``: a positive
     *  curve leaves the starting value slowly and moves late (the exponents
     *  grow), a negative one moves early and lands slowly, and ``+c`` and
     *  ``-c`` are reflections of one another across the segment's diagonal
     *  (reciprocal exponents). The limits are honest steps: at +1 the
     *  segment holds its start until the end, at -1 it jumps at the start.
     *  Max gates curves behind his ``mode`` attribute and speaks them to
     *  ``curve~``; there is no attribute surface and no ``curve~`` here, so
     *  the curve is simply always available and a curve of 0 *is* mode 0.
     *
     *  ### The GUI value (issue #551)
     *
     *  A cell is one breakpoint, spelled ``<x> <y> <curve>``; the bulk read
     *  is every cell in ascending-x order, which is exactly the whole-state
     *  list inlet 0 takes back. The one wrinkle a variable-count store adds:
     *  an **empty** function's bulk read is the word ``clear`` — the message
     *  that makes a function empty is the honest spelling of the state, it
     *  round-trips through inlet 0 like every other GetGuiValue() string,
     *  and it keeps a zero-token list from being a destructive write (an
     *  accidental empty list wiping an envelope would be worse than the
     *  quirk). ``GetGuiValueCount()`` is then 0, so a host iterating cells
     *  draws nothing.
     *
     *  ### What persists
     *
     *  The breakpoints — contents, not just parameters — through
     *  ``DumpState`` / ``RestoreState``, unconditionally. Max's ``function``
     *  is a UI object whose points are saved with the patch; that is
     *  ``.coll``'s always-rule rather than ``.funbuff``'s opt-in ``embed``,
     *  and it is what issue #561's "contents in the JSON round trip" asks
     *  for. Restore goes through the same sorted, clamped store the inlet
     *  uses, so a hand-edited file still comes back ordered and bounded.
     *
     *  ### Real time
     *
     *  The store is allocated **exactly once, at SetParams time**, on the
     *  control thread: the ``points`` capacity argument sizes it
     *  (``.matrixctrl``'s arrangement, and like there the parameter
     *  callbacks make ``ParamsNeedRebuild()`` true, so a live re-parse
     *  replaces the object through the graph swap instead of reallocating
     *  under the audio thread). No message path allocates, locks or blocks:
     *  the list handler walks its tokens in place, both send buffers are
     *  reserved at construction, the dump captures into a pre-allocated
     *  scratch array under one guard acquisition and emits after releasing
     *  it (``.funbuff``'s capture, for its reason — a re-entrant write can
     *  shift a sorted table under a walk), and every send happens with the
     *  guard released. The guard is the family's non-blocking exclusive flag
     *  (``.funbuff``'s, ``.coll``'s): a loser drops rather than waiting,
     *  because this object is reachable from the control thread and a
     *  rendering graph alike and the points are read and written as a group.
     *  A store past the capacity is refused whole and silently.
     *
     *  ### Deliberately not here
     *
     *  ``sustain``, ``getsustain`` and ``autosustain``, with Max's ``next``:
     *  together they are the note-driven envelope *player* (play to the
     *  sustain point, hold, release on note-off), which is half an ADSR
     *  voice, not part of the breakpoint store — the engine's envelope
     *  generators own that job. ``fix`` / ``getfix``, which protect points
     *  from the *mouse*, of which there is none. ``setdomain`` and Max's
     *  ``domain`` / ``range`` attributes with their rescaling: bounds are
     *  creation parameters in this family, not messages (``.rslider``'s and
     *  ``.multislider``'s rule), and a domain is just the extent of the
     *  stored x's when nothing is drawn. ``outputmode`` (above), ``mode``
     *  (above), and Max's bare three- and four-element point-edit lists —
     *  ``<index> <x> <y> [<curve>]`` without a keyword — whose three-number
     *  form would collide with a one-point whole state; the protocol's
     *  ``set`` spells the same edit unambiguously. Everything about drawing
     *  and the mouse.
     */
    PATCHER_CLASS(gFunction, YSE::OBJ::G_FUNCTION)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    _HAS_GUI_CELLS

    /**
     *  @brief Most breakpoints the store can be built to hold — 1024,
     *         ``.multislider``'s ceiling and for its reason: the ramp list is
     *         the whole function as one message, so its send buffer is
     *         reserved for the capacity at construction.
     */
    static constexpr int MAX_POINTS = 1024;

    /** @brief Fewest. A function needs two points before it is a function. */
    static constexpr int MIN_POINTS = 2;

    /** @brief The capacity with no creation argument — 128, Max's default
     *         domain (1000) drawn at a comfortable point-per-8ms. */
    static constexpr int DEFAULT_POINTS = 128;

    /** @brief Lowest y with no creation argument — Max's ``range`` low. */
    static constexpr float DEFAULT_MINIMUM = 0.f;

    /** @brief Highest y with no creation argument — Max's ``range`` high. */
    static constexpr float DEFAULT_MAXIMUM = 1.f;

    /** @brief How many breakpoints the store was built to hold. */
    int Capacity() const {
      return capacity;
    }

    /** @brief How many breakpoints are stored. 0 when the guard was held
     *         elsewhere — diagnostics and tests, control thread. */
    std::size_t PointCount() const;

    /** @brief The x of the point at @p position in ascending-x order, or 0.
     *         Same contract as ``PointCount``. */
    float XAt(std::size_t position) const;

    /** @brief The y of the point at @p position, or 0. */
    float YAt(std::size_t position) const;

    /** @brief The curve of the point at @p position, or 0. */
    float CurveAt(std::size_t position) const;

    // The contents, unconditionally — Max's function is a UI object whose
    // points are saved with the patch. Control thread; the guard is still
    // taken because a message may arrive from a rendering graph mid-save.
    void DumpState(nlohmann::json::value_type& json) override;

    // The other half: ParseJSON, on a freshly built object the audio thread
    // cannot see yet. Refills through the same sorted, clamped store the
    // inlet uses.
    void RestoreState(const nlohmann::json::value_type& json) override;

  private:
    // One breakpoint. `curve` shapes the segment arriving at this point from
    // the previous one; the first point's curve is unused.
    struct Point {
      float x = 0.f;
      float y = 0.f;
      float curve = 0.f;
    };

    /**
     *  @brief Non-blocking exclusive access to the store — ``.funbuff``'s
     *         guard, for its reason: the points are read and written as a
     *         group, the object is reachable from the control thread and a
     *         rendering graph alike, and a loser drops rather than waiting.
     */
    class storeGuard {
    public:
      explicit storeGuard(std::atomic<bool>& flag)
        : flag_(flag), held_(!flag.exchange(true, std::memory_order_acquire)) {}
      ~storeGuard() {
        if (held_) flag_.store(false, std::memory_order_release);
      }
      storeGuard(const storeGuard&) = delete;
      storeGuard& operator=(const storeGuard&) = delete;
      storeGuard(storeGuard&&) = delete;
      storeGuard& operator=(storeGuard&&) = delete;

      bool Held() const {
        return held_;
      }

    private:
      std::atomic<bool>& flag_;
      bool held_;
    };

    // Clamp `value` into the ordered [minimum, maximum] bounds; a NaN answers
    // the lower bound. `.matrixctrl`'s Bound.
    float Bound(float value) const;

    // Clamp a curve into [-1, 1]; a NaN is read as 0, linear, rather than
    // folded onto an end it never asked for.
    static float BoundCurve(float value);

    // Fold a negative x onto 0 — Max's domain starts at 0 — and refuse a NaN,
    // which no clamp can place.
    static bool SanitizeX(float& x);

    // The segment shape: position `t` in [0, 1] through curve `c`. t^((1+c)/
    // (1-c)), with the +/-1 limits as honest steps. See the class comment.
    static float Shape(float t, float c);

    // Position of the first point whose x is >= `x`, or `count` when there is
    // none. Guard held; `x` is never NaN here.
    std::size_t LowerBound(float x) const;

    // Store a sanitised, bounded point, replacing the one already at exactly
    // that x or inserting in sorted position. False when the store is full —
    // refused whole and silently, since growing would allocate on whichever
    // thread the message arrived on. Guard held.
    bool StorePoint(float x, float y, float curve);

    // Drop the point at `position`, closing the gap. Guard held.
    void EraseAt(std::size_t position);

    // An int or float on the inlet: the curved interpolated y at `x` out
    // outlet 0. Takes the guard itself; sends with it released.
    void Query(float x, YSE::THREAD thread);

    // Max's bang: the whole function as one `.line`-compatible ramp list out
    // outlet 1. Builds into `rampText` (reserved at construction) under the
    // guard and sends after releasing it.
    void EmitRamp(YSE::THREAD thread);

    // Max's dump: every point as `<index> <x> <y> <curve>` out outlet 2.
    // Captured whole into `scratch` under one guard acquisition and emitted
    // after release — a re-entrant write can shift a sorted table under a
    // walk (`.funbuff`'s lesson).
    void EmitDump(YSE::THREAD thread);

    // The command half of the list inlet. False when `word` is none of them,
    // leaving the caller to read the message as numbers.
    bool HandleCommand(const char* word, std::size_t length, const std::string& message,
                       std::size_t argOffset, YSE::THREAD thread);

    // Rebuild the store and the send buffers from the creation arguments.
    // Control thread only: the constructor and the parameter callbacks, all
    // of which run before the object is wired or published; a live SetParams
    // replaces the object instead (ParamsNeedRebuild() is true).
    void ShapeStore();

    // The creation arguments, as tokens. Control thread only.
    std::vector<std::string> creationArgs;

    // The function, and the dump's capture buffer. Allocated exactly
    // `capacity` wide by ShapeStore() and never resized; only the first
    // `count` points are live, always sorted ascending by x.
    std::unique_ptr<Point[]> points;
    std::unique_ptr<Point[]> scratch;
    int capacity = 0;
    std::size_t count = 0;

    // The bounds every y is clamped into on the way in. Written only by
    // ShapeStore(), before the object is published.
    float minimum = DEFAULT_MINIMUM;
    float maximum = DEFAULT_MAXIMUM;

    // The two allocations a send would otherwise need, reserved by
    // ShapeStore(): the ramp list for the worst-case capacity, the dump line
    // for its fixed four numbers.
    std::string rampText;
    std::string lineText;

    // True while a bang or dump is emitting: an outlet wired back into inlet
    // 0 must not rebuild a send buffer mid-fan-out. `.matrixctrl`'s guard.
    bool emitting = false;

    // Claimed with a single exchange by readers and writers alike; the loser
    // drops. Mutable so the const accessors and GUI reads can take it.
    mutable std::atomic<bool> busy{false};
  };

} // namespace PATCHER
} // namespace YSE
