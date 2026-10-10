#pragma once
#include <vector>

namespace YSE {
  namespace PATCHER {

    class pObject;
    class patcherImplementation;
    struct inlet;
    struct outlet;

    // Immutable, compiled snapshot of a patcher's topology (issue #226).
    //
    // Built off the audio thread from the objects' control-thread wiring and
    // published to the audio thread through a single
    // ``std::atomic<const GraphState*>`` pointer swap. Once published a
    // GraphState is never mutated; ``Calculate`` pins one snapshot for the
    // duration of a block and resolves every send/readiness query against it,
    // so the audio thread never touches the live (mutable) object wiring and
    // needs no lock. Control-thread sends read the published snapshot the same
    // way, under a control-side pin (issue #963; see graphReadScope below).
    //
    // Objects are stable (allocated once, never rebuilt), so the ``inlet*``
    // and ``pObject*`` pointers stored here stay valid across edits; only the
    // adjacency is swapped. Indices into ``outletTargets`` / ``inletHasDsp``
    // are the dense, construction-time ``graphId`` stamped on each
    // outlet / inlet — stable for the object's lifetime, so a swap never
    // rewrites them.
    struct GraphState {
      // Every object in the snapshot — walked to invalidate DSP buffers at the
      // top of a block.
      std::vector<pObject*> objects;

      // DSP objects with no active DSP input: the roots the push traversal
      // starts from. Precomputed so ``Calculate`` never scans for them.
      std::vector<pObject*> startPoints;

      // Objects that must run once per block whatever the patch does with them
      // (issue #529) — ``pObject::WantsBlockPoll()``. The MIDI-input family
      // lives here: its events arrive on RtMidi's thread and would otherwise
      // never be drained, there being no inlet to push the object through and
      // no DSP edge to reach it by. Precomputed for the same reason
      // ``startPoints`` is — ``Calculate`` never scans for them.
      std::vector<pObject*> pollers;

      // DAC sinks whose channel buffers are summed into the patcher output.
      std::vector<pObject*> dacs;

      // ADC sources fed by an external host buffer when the patcher runs as an
      // insert (issue #167). The host adapter points each ADC's channels at the
      // incoming audio before the block renders.
      std::vector<pObject*> adcs;

      // outletTargets[outlet.graphId] = the inlets that outlet feeds. Empty for
      // ids that belong to deleted objects or outlets with no connections.
      std::vector<std::vector<inlet*>> outletTargets;

      // inletHasDsp[inlet.graphId] != 0 when the inlet has an active buffer
      // input in this snapshot. Replaces the audio-thread reads of
      // ``inlet::dspConnection`` used by start-point selection / WaitingForDSP.
      std::vector<char> inletHasDsp;

      // outletOwner[id] / inletOwner[id] = the outlet / inlet that held graph id
      // `id` when this snapshot was built, or null for an unused id (issue
      // #963). A lookup only trusts ``outletTargets`` / ``inletHasDsp`` for a
      // pin that finds itself here. The audio thread only ever reaches pins of
      // objects in its snapshot, so for it the check always passes; a control
      // thread can still hold an object deleted since (a ``.preset`` recall,
      // under an objectPin), whose id a Clear may have recompacted onto a new
      // object (issue #355) — without the check that object's sends would take
      // the new one's cords. What a pin not found here reads instead is
      // graphReadScope's business, below.
      std::vector<const outlet*> outletOwner;
      std::vector<const inlet*> inletOwner;
    };

    // The GraphState an inlet / outlet resolves its topology against on this
    // thread, held for the scope's lifetime (issues #226, #962, #963). Every
    // send (``outlet::Send*``) and readiness query (``inlet::WaitingForDSP``)
    // opens one around its read of the wiring; ``Graph()`` is what it reads.
    //
    //  - On the thread rendering the owning patcher's block: the snapshot that
    //    block pinned. Nothing is taken — the reclaimer's +2-block grace
    //    already covers it. One TLS load, as before #963.
    //  - On any other thread (a host ``Set*``, a ``.preset`` recall, a
    //    ``.metro`` tick): the patcher's published snapshot, ``active_``,
    //    loaded once by the outermost scope on this thread and pinned with a
    //    control-side pin (patcherImplementation::objectPin's counter) until
    //    that scope closes. While any pin is held the reclaimer frees nothing
    //    retired — no graph, no object — so the snapshot and every object it
    //    names stay allocated for the whole fan-out. Nested scopes for the
    //    same patcher reuse the outer one: one pin and one snapshot for a
    //    send's whole fan-out, however deep it goes.
    //    Control threads therefore never read the live, mutable wiring that a
    //    structural edit on another thread rewrites under mtx.
    //  - For a standalone object (no patcher): null, and the caller reads the
    //    live wiring, which only its own thread touches.
    //
    // A pin the snapshot does not own (see outletOwner) is resolved by the
    // caller in two more steps. On a control thread it first tries Latest():
    // a frame opened before an object was created can still reach that object
    // through a lookup under mtx (a `.preset` recall nested in a send), and
    // the newest publish has it. Failing that the pin belongs to no published
    // snapshot — a test rig given a parent but never added (graph id -1), or
    // an object deleted since — and the caller reads its live wiring. That is
    // not the race this scope exists for: a deleted object was unwired under
    // mtx before the publish that dropped it, which this thread's snapshot
    // load synchronised with, and nothing writes its wiring again.
    //
    // Lock-free and allocation-free: the control-thread case is one atomic
    // add and one atomic sub. Defined in patcherImplementation.cpp, next to
    // the thread_local frame markers it consults.
    class graphReadScope {
    public:
      explicit graphReadScope(const pObject* object);
      ~graphReadScope() {
        if (pinned_ != nullptr) Release();
      }
      graphReadScope(const graphReadScope&) = delete;
      graphReadScope& operator=(const graphReadScope&) = delete;
      graphReadScope(graphReadScope&&) = delete;
      graphReadScope& operator=(graphReadScope&&) = delete;

      const GraphState* Graph() const {
        return graph_;
      }

      // The newest published snapshot, safe to read for this scope's
      // lifetime because this thread's control-side pin is held. Null on the
      // renderer and for a standalone object: there is nothing newer the
      // renderer may read, and the audio thread only reaches objects its own
      // snapshot holds.
      const GraphState* Latest() const;

    private:
      void Release();

      const GraphState* graph_ = nullptr;
      // The patcher whose control-side pin this thread holds for the scope,
      // taken here or by an enclosing scope; null on the renderer.
      const patcherImplementation* control_ = nullptr;
      // Set only when this scope took the pin (the outermost one on this
      // thread for this patcher); the frame it replaced is restored on exit.
      patcherImplementation* pinned_ = nullptr;
      const patcherImplementation* savedPatcher_ = nullptr;
      const GraphState* savedGraph_ = nullptr;
    };

  } // namespace PATCHER
} // namespace YSE
