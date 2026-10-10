#pragma once
#include "../pObject.h"

#include <atomic>
#include <cstdint>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief `.loadbang` — send a bang when the patcher has finished loading
     *         (issue #547). Max's `loadbang`.
     *
     *  ### What it is for
     *
     *  Max: "Send a bang when a patcher is loaded" — "outputs a `bang`
     *  automatically when the file is opened or when the patch is part of
     *  another file that is opened."
     *
     *  Until it existed, a patcher restored from JSON had no way to set its own
     *  starting state. Every parameter that needed initialising — a gain, a
     *  filter cutoff, the contents of a `.coll`, whether a `.metro` starts
     *  running — had to be pushed in by the host after the load, which meant the
     *  patch was not self-contained: it was a graph plus a list of things the
     *  host had to remember to do to it. This is the one cord that makes a saved
     *  patch describe its own beginning, and `.loadmess` is the same statement
     *  with a payload.
     *
     *  ### When it fires, which is the whole of the object
     *
     *  Exactly once, at the end of `patcherImplementation::ParseJSON`, after the
     *  parsed graph has been compiled into a GraphState and published with the
     *  single atomic swap of issue #228 — not during construction, not at the
     *  end of the create loop, not from `Calculate()`.
     *
     *  That is not a detail, it is the feature. A bang fired from a constructor
     *  travels down cords that do not exist yet and reaches nothing. A bang
     *  fired once every object has been created but before the connections have
     *  been restored reaches some of the patch and not the rest, and *which*
     *  part depends on the order the file happens to list its objects in. Only
     *  after the publish is the patch the patch the file describes, so that is
     *  when this speaks. See `patcherImplementation::LoadbangObjects`, which is
     *  `TeardownObjects`'s mirror image and is shaped the same way for the same
     *  reasons: one pass, on the control thread, dispatched with `T_GUI`, and
     *  outside the patcher's mutex so that a patch whose initialisation passes
     *  through a `.forward` or a `.qlist` loads instead of deadlocking.
     *
     *  The order in which two `.loadbang`s in one patch fire is **not defined**.
     *  Max says nothing about it either, and the answer is the same as for every
     *  other ordering requirement in a patcher: if one initialisation has to
     *  precede another, say so with a `.trigger`.
     *
     *  ### Live editing: it does not fire, and the inlet is why that is enough
     *
     *  An object created through `patcher::CreateObject` never receives a
     *  loadbang. Adding a `.loadbang` to a running patch does nothing until that
     *  patch is saved and loaded again.
     *
     *  Two reasons, and they agree. At `CreateObject` time the object has no
     *  cords — a patch is built by creating an object and *then* connecting it —
     *  so firing there would send into nothing and the behaviour would be a
     *  no-op wearing a name. And firing at some later edit instead would mean a
     *  patch could not be touched without re-running its initialisation, which
     *  is the opposite of what initialisation is for: the performer moves a
     *  fader, someone adds an object elsewhere in the patch, and the fader jumps
     *  back. Max's reference is silent on the live case, but every trigger it
     *  *does* document beyond the load is a manual one — a double-click in a
     *  locked patcher, a `loadbang` message to `thispatcher` — which says the
     *  same thing: a load fires it and nothing else does.
     *
     *  Here the manual trigger is the inlet. Max: "sending a `bang` message to a
     *  `loadbang` object causes it to output a `bang` message." That is the
     *  whole of what a host needs to initialise a patch it has just edited
     *  live, and it is testable, which the double-click is not.
     *
     *  ### Shape
     *
     *  One inlet, one bang outlet, no creation arguments — Max's "None." There
     *  is no state to persist, so a save and a load produce the same object,
     *  which then fires because it was loaded.
     *
     *  The `loadbang` *message* Max documents ("same as `bang`") is deliberately
     *  not implemented. A message arrives through `pObject::SetMessage`, whose
     *  signature carries no `THREAD` tag, so an implementation would have to
     *  invent one — and inventing `T_GUI` on a message that in-patcher delivery
     *  dispatched as `T_DSP` would skip `CalculateIfReady` on a DSP object
     *  mid-block. The inlet is the documented trigger and it carries its thread
     *  honestly, so it is the only one offered.
     *
     *  ### Real-time behaviour
     *
     *  `Calculate()` does nothing at all; this is not a DSP object and has no
     *  block-rate work. Both paths that make it send — the load pass and the
     *  inlet — are one relaxed atomic increment and one `outlet::SendBang`, so
     *  nothing here allocates, locks or blocks whichever thread it runs on.
     */
    PATCHER_CLASS(gLoadbang, YSE::OBJ::G_LOADBANG)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)

    /**
     *  @brief The patch this object is in has finished loading — send the bang.
     *
     *  Called once per object by `patcherImplementation::LoadbangObjects`, after
     *  the parsed graph has been published. Never called for an object created
     *  live; see the class notes.
     */
    void Loadbang(YSE::THREAD thread) override;

    /**
     *  @brief How many bangs this object has sent, from every cause — the load
     *         and the inlet together.
     *
     *  Diagnostics and tests. Monotonic and readable from any thread; a patch
     *  sees the same thing by counting what comes out of the outlet.
     */
    std::uint64_t Fired() const {
      return fired.load(std::memory_order_relaxed);
    }

  private:
    // Bangs sent. Relaxed throughout: it is a counter nothing synchronises
    // against, and the inlet can be driven from a render thread while a test
    // reads it from the control thread.
    std::atomic<std::uint64_t> fired{0};
  };

} // namespace PATCHER
} // namespace YSE
