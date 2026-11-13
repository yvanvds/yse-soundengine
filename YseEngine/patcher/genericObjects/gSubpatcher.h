#pragma once
#include "../pObject.h"

#include <atomic>
#include <cstdint>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief `patcher` — a subpatcher: a group of objects addressed by the
     *         parent patch as one object (issue #545). Max's `patcher` / `p`.
     *
     *  ### What it is for
     *
     *  Encapsulation. Without it every graph is flat, which puts a hard ceiling
     *  on how large a patch can get before it stops being readable, and rules
     *  out reusable building blocks entirely. A subpatcher is the unit of
     *  "this bit is one thing now": the parent wires to it by number, and what
     *  is behind those numbers is the subpatcher's business.
     *
     *  ### The model: flat storage, nested addressing
     *
     *  This is the whole of the design and every other property follows from
     *  it. A subpatcher's contents are **not** a second graph. Every object in
     *  a patch — top level or nested twenty deep — lives in the one
     *  `patcherImplementation::objects` map, gets a graph id from the one id
     *  space, is compiled into the one `GraphState` and is walked by the one
     *  flat traversal. Nesting is a single control-thread annotation on each
     *  object: `pObject::Container()`, the subpatcher it belongs to, or null
     *  for the top level.
     *
     *  The issue asked whether the nested graph should be *flattened at publish
     *  time* or kept genuinely nested. This is the stronger answer to the same
     *  question — it is never unflat, so there is no flattening step to get
     *  wrong and no second representation to keep in step with the first.
     *  Concretely:
     *
     *  - **The audio thread's traversal cost does not grow with nesting
     *    depth.** It cannot: the render walks the one compiled signal order
     *    (`GraphState::renderOrder`, issue #1185) whatever the nesting, and a
     *    subpatcher object appears in no edge, no start-point list and no
     *    poller list. Depth is invisible to it.
     *  - **A live edit inside a subpatcher is an ordinary edit.** Creating,
     *    deleting, connecting or re-parametrising an object that happens to sit
     *    inside a subpatcher goes down the same `RebuildAndPublish()` path and
     *    the same single atomic `GraphState` swap as one at the top level
     *    (issues #226 / #227 / #228). There is no second publish, no per-level
     *    lock, and nothing about nesting the audio thread has to be told.
     *  - **The audio thread reads nothing about a subpatcher but its mute
     *    word** (see Muting, below), through pointers the snapshot holds.
     *    Membership and boundary resolution are used by `Connect`, `Disconnect`,
     *    `DeleteObject`, `DumpJSON` and the graph build — all control thread,
     *    all under the patcher's mutex. That is why this object has no
     *    `Calculate()` worth the name and no pins.
     *
     *  ### The boundary, and why this object has no inlets or outlets
     *
     *  `patcher.Connect(source, 0, subpatcher, 2)` does not create an edge to
     *  *this* object. `patcherImplementation::ConnectUnlocked` resolves the
     *  façade first: inlet 2 of a subpatcher means "the boundary object inside
     *  it claiming index 2" — a `.inlet 2` for a message pin, a `~inlet 2` for
     *  a signal pin (issue #764), the two sharing one index space because a
     *  subpatcher has one set of pins per side — and the edge is recorded from
     *  `source`'s outlet straight to that object's inlet. Outlets mirror it
     *  through `.outlet` / `~outlet`. So the cord
     *  that actually exists is an ordinary cord between two ordinary objects,
     *  and every downstream mechanism — the graph compiler, the reclaimer, the
     *  serialiser, `Disconnect`, `UnwireFromPeers` — needs to know nothing
     *  about subpatchers to handle it correctly.
     *
     *  This object therefore carries **no pins of its own**, and that is a
     *  correctness requirement rather than an economy. A subpatcher's boundary
     *  changes when a `.inlet` is added to it, which is an ordinary live edit;
     *  if the boundary were a real pin vector on this object, that edit would
     *  have to resize `inputs` / `outputs` on an object the audio thread is
     *  concurrently walking in `ResetDSP()` and `CalculateIfReady()`. Issue
     *  #234 exists because pin-count changes cannot be made in place; the
     *  façade sidesteps the problem by never owning the pins in the first
     *  place. `NumInputs()` and `NumOutputs()` are consequently 0 here, and
     *  `patcher::SubpatcherInlets` / `SubpatcherOutlets` are what report the
     *  boundary shape.
     *
     *  ### Lifetime
     *
     *  Deleting a subpatcher deletes everything inside it, transitively —
     *  `patcherImplementation::DeleteObject` collects the containment subtree
     *  before it removes anything. Every object in that subtree gets its
     *  `Teardown(THREAD)` (issue #758) in one pass **before** any of them is
     *  unwired, exactly as `Clear()` does for a whole patch and for the same
     *  reason: an object that has left something sounding outside the patch can
     *  only release it while its cords are still there.
     *
     *  ### Loading
     *
     *  A subpatcher's contents fire their `Loadbang(THREAD)` (issue #547) in
     *  the same single post-publish pass as the top level, in no defined order
     *  relative to it. There is deliberately **no** "inner patchers initialise
     *  first" rule. The reason is the model again: the whole tree is compiled
     *  and published in one atomic swap, so there is exactly one instant at
     *  which "the patch has finished loading" becomes true, and it is true for
     *  every level of nesting simultaneously. Inventing an order would be
     *  claiming a distinction the publish does not make. As at the top level, a
     *  patch that needs one initialisation to precede another says so with a
     *  `.trigger` — which works across a subpatcher boundary like any other
     *  cord.
     *
     *  ### Shape
     *
     *  No inlets and no outlets. One creation argument, `name` (issue #1260):
     *  free text the object keeps for a host to show as the subpatch's title,
     *  the way Max reads `p gain`. The engine never reads it — the stored
     *  parameter string is what a host gets back, verbatim, so a name of
     *  several words survives `GetParams` and a save. A subpatcher is
     *  addressed by its handle (or its storage ID), never by this name, and
     *  its contents are placed inside it with `patcher::SetContainer`.
     *
     *  Because `name` is a string parameter, a live `SetParams` on a
     *  subpatcher takes the #234 structural-replacement path: the object is
     *  rebuilt and the handle swapped to the replacement.
     *  `patcherImplementation::ReplaceObjectUnlocked` re-points every object
     *  that named the old one as its container, so the contents stay inside
     *  and the boundary keeps resolving. The replacement is unmuted (see
     *  Muting): the mute is runtime state and is not carried over.
     *
     *  ### Muting (issue #1137)
     *
     *  The one piece of subpatcher state the audio thread does read: a mute
     *  word, set by a `~mute` whose outlet is connected to one of this
     *  subpatcher's inlets. Two bits: `MUTE_CONTENTS` switches off the signal
     *  objects directly inside, `MUTE_NESTED` the ones inside its nested
     *  subpatchers, at any depth. The render does not walk the containment
     *  tree: `BuildGraph` gives each render step the mute words of the
     *  subpatchers around it (GraphState::muteChain), and the step is muted
     *  when its innermost one has `MUTE_CONTENTS` set or any outer one has
     *  `MUTE_NESTED`. A muted object does not compute; its signal outlets
     *  deliver silence (pObject::SendSilence).
     *
     *  The word is an atomic set with `fetch_or` / `fetch_and` from whichever
     *  thread runs the `~mute` (the control thread, or the audio thread's
     *  drains) and loaded once per render step. It is runtime state, like the
     *  patcher's DSP switch: not saved, 0 on a new object, so a `SetParams`
     *  rename (which builds a new object) starts unmuted.
     *
     *  ### Real-time behaviour
     *
     *  `Calculate()` does nothing and is never called: with no pins the object
     *  is not a DSP start point, is not a poller and is the target of no edge,
     *  so it is not reachable from the render traversal at all. The mute word
     *  is lock-free and allocation-free on every thread.
     */
    PATCHER_CLASS(gSubpatcher, YSE::OBJ::PATCHER)
    _NO_MESSAGES
    _NO_CALCULATE

    /** @brief Mute bit: the signal objects directly inside. */
    static constexpr std::uint8_t MUTE_CONTENTS = 1;
    /** @brief Mute bit: the signal objects in nested subpatchers, any depth. */
    static constexpr std::uint8_t MUTE_NESTED = 2;

    /** @brief Set (@p on) or clear the bits in @p mask. Any thread; RT-safe. */
    void SetMute(std::uint8_t mask, bool on) {
      if (on) {
        mute_.fetch_or(mask, std::memory_order_acq_rel);
      } else {
        mute_.fetch_and(static_cast<std::uint8_t>(~mask), std::memory_order_acq_rel);
      }
    }
    /** @brief The current mute bits. Any thread. */
    std::uint8_t Mute() const {
      return mute_.load(std::memory_order_acquire);
    }
    /** @brief The mute word itself, for GraphState to point at. */
    std::atomic<std::uint8_t>* MuteWord() {
      return &mute_;
    }

  private:
    std::atomic<std::uint8_t> mute_{0};
    // The `name` creation argument. Held only so the parameter is registered
    // and documented; nothing reads it (see "Shape" above).
    std::string name;
  };

} // namespace PATCHER
} // namespace YSE
