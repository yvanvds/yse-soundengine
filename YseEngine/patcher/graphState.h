#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace YSE {
  namespace PATCHER {

    class pObject;
    class gSubpatcher;
    class patcherImplementation;
    struct inlet;
    struct outlet;
    struct spectralCarry;

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
    // adjacency is swapped. Indices into ``outletTargets`` / ``inletDspSources``
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

      // One step of the compiled signal render (issue #1185): the object, and
      // whether it is a start point (computed unconditionally) or a node fed
      // by signal cords (computed only once every buffer it waits for has
      // arrived, by pObject::CalculateIfReady).
      //
      // ``muteBegin`` / ``muteEnd`` (issue #1137) bound the step's slice of
      // ``muteChain``: the mute words of the subpatchers around the object,
      // innermost first. Equal for an object at the top level.
      //
      // ``emit`` (issue #1280) marks the second step of a frame-wide object
      // in a ~pfft subpatch (pObject::SpectralFrameWide): the one that sends
      // the frame it consumed in the stage before. Always a start point:
      // nothing it waits for arrives in its stage.
      struct renderStep {
        pObject* object;
        bool startPoint;
        std::uint32_t muteBegin = 0;
        std::uint32_t muteEnd = 0;
        bool emit = false;
      };

      // The signal traversal, compiled at publish time (issue #1185). It is
      // the exact order the recursive push used to visit objects in — start
      // points in ``startPoints`` order, each followed depth-first by every
      // object it completes, outlet by outlet and cord by cord — but walked as
      // a flat loop. The push recursed one SendBuffer -> SetBuffer ->
      // CalculateIfReady frame per object, through the send-depth cap that
      // breaks *message* feedback loops (#236), so a serial chain deeper than
      // that cap went silent past it; a flat walk costs no stack whatever the
      // chain's length. While it runs, a buffer arriving on an inlet is stored
      // but computes nothing (see renderOrderScope in inlet.h): the walk
      // reaches that object itself, after everything that feeds it.
      std::vector<renderStep> renderOrder;

      // Objects that must run once per block whatever the patch does with them
      // (issue #529) — ``pObject::WantsBlockPoll()``. The MIDI-input family
      // lives here: its events arrive on RtMidi's thread and would otherwise
      // never be drained, there being no inlet to push the object through and
      // no DSP edge to reach it by. Precomputed for the same reason
      // ``startPoints`` is — ``Calculate`` never scans for them.
      std::vector<pObject*> pollers;

      // Whether the patch holds a ~dspstress (issue #1172): only then does
      // Calculate time the block for patcherImplementation::RenderLoad.
      bool measureLoad = false;

      // Signal objects with at least one control outlet, sorted by storage
      // ID (issue #1037). After the DSP traversal the patcher sends what each
      // armed in its CALC(); see pObject::FlushArmedOutlets. Sorted because
      // ``objects`` follows handle addresses, which differ between runs.
      std::vector<pObject*> emitters;

      // DAC sinks whose channel buffers are summed into the patcher output.
      std::vector<pObject*> dacs;
      // dacMutes[d] = the muteChain slice of dacs[d] (issue #1137): a ~dac in
      // a muted subpatcher adds nothing to the output.
      struct muteSlice {
        std::uint32_t begin = 0;
        std::uint32_t end = 0;
      };
      std::vector<muteSlice> dacMutes;

      // The mute words (gSubpatcher::MuteWord) of the subpatchers around each
      // render step and ~dac, one slice per object, innermost first (issue
      // #1137). Pointers into subpatcher objects this snapshot holds, so they
      // live as long as it does. The audio thread only loads them.
      std::vector<const std::atomic<std::uint8_t>*> muteChain;

      // The subpatch of one ~pfft (issue #1041): the objects whose nearest
      // enclosing ~pfft it is. They are rendered by the ~pfft, once per pass
      // of every spectral frame, not by Calculate's walk: none of them is in
      // ``renderOrder``. A signal cord between two domains (the top level, or
      // two ~pfft subpatches) is not compiled at all — not in
      // ``outletTargets``, not counted in ``inletDspSources`` — so it is
      // neither pushed nor waited for; Connect refuses one, and this is what
      // keeps one made by a later SetContainer inert.
      struct spectralDomain {
        // The ~pfft (a dPfft).
        pObject* host = nullptr;
        // The domain's own compiled signal render, built as ``renderOrder``
        // is (start points within the domain, cords within the domain), with
        // mute slices like it, cut into *stages* (issue #1280).
        //
        // A frame-wide object (pObject::SpectralFrameWide) needs every bin of
        // a frame before it can put out any, so what follows it cannot run
        // in the same passes. Each object has a stage: a start point 0, any
        // other the largest of its sources' stages, one more for a frame-wide
        // source. A frame-wide object has two steps: its own (it *consumes*
        // each pass's input) in its stage, and an ``emit`` step at the start
        // of the next stage (it computes the frame at the first and sends
        // each pass's bins). The ~pfft renders every pass of stage 0, then
        // every pass of stage 1, and so on; a domain without frame-wide
        // objects is one stage, the passes as before.
        std::vector<renderStep> renderOrder;
        // stageEnds[s] = the end of stage s's steps in ``renderOrder`` (stage
        // s runs from stageEnds[s - 1], or 0). At least one stage.
        std::vector<std::uint32_t> stageEnds;
        // A signal cord into a later stage than its source puts out in (from
        // a stage-0 object past a frame-wide one into a stage-1 object) is
        // carried: the compiled graph sends what the source puts out into a
        // frame-long store (the carry's tap, in ``outletTargets`` in place of
        // the target inlet), and the ~pfft hands each pass's bins of it to
        // the target at the start of the target's stage. Sorted by that
        // stage. Shared only so this header needs no complete type; built on
        // the control thread and freed with the snapshot.
        std::vector<std::shared_ptr<spectralCarry>> carries;
        // Every object of the domain: the ~pfft resets their inlets before
        // each pass.
        std::vector<pObject*> objects;
        // The ~fftin (dFftin) and ~fftout (dFftout) objects of the domain,
        // sorted by storage ID.
        std::vector<pObject*> inputs;
        std::vector<pObject*> outputs;
        // outputStages[j] = the stage of outputs[j]: a ~fftout is collected
        // after each pass of its own stage (issue #1280).
        std::vector<std::uint32_t> outputStages;
      };
      std::vector<spectralDomain> spectral;

      // The domain @p host renders, or null. Audio thread; linear over the
      // patch's ~pfft objects, no allocation.
      const spectralDomain* Spectral(const pObject* host) const {
        for (const spectralDomain& d : spectral)
          if (d.host == host) return &d;
        return nullptr;
      }

      // The voices of one ~poly (issue #1040). Each voice is a copy of the
      // objects inside the ~poly, made by the patcher (never by the user, never
      // saved) and contained in a polyVoice root. A voice is a domain of its
      // own, like a ~pfft subpatch: its objects are in ``objects`` (reset at
      // the top of each block like any other) but in no ``renderOrder`` of the
      // patcher; the ~poly renders each voice's own order inside its step.
      // Signal cords never cross a voice boundary: Connect refuses every cord
      // across a ~poly's, and copies are wired only to each other. The objects
      // inside the ~poly themselves (the template) are in none of these lists:
      // they do not run.
      struct polyVoiceDomain {
        // The voice's polyVoice root: its number and mute word.
        pObject* root = nullptr;
        // The voice's compiled signal render, built as ``renderOrder`` is,
        // with mute slices (the root's mute word outermost).
        std::vector<renderStep> renderOrder;
        // The voice's ~in, ~out and .in objects (dVoiceIn, dVoiceOut,
        // gVoiceIn), and the objects that take its events
        // (pObject::VoiceEventSink), each in template storage-ID order.
        std::vector<pObject*> signalInputs;
        std::vector<pObject*> signalOutputs;
        std::vector<pObject*> messageInputs;
        std::vector<pObject*> sinks;
      };
      struct polyDomain {
        // The ~poly (a dPoly).
        pObject* host = nullptr;
        // Voice 1 first.
        std::vector<polyVoiceDomain> voices;
      };
      std::vector<polyDomain> poly;

      // The voices @p host renders, or null. Any thread holding the snapshot;
      // linear over the patch's ~poly objects, no allocation.
      const polyDomain* Poly(const pObject* host) const {
        for (const polyDomain& d : poly)
          if (d.host == host) return &d;
        return nullptr;
      }

      // inletSubpatcher[inlet.graphId] = for an inlet of a subpatcher's
      // boundary object (.inlet / ~inlet), that subpatcher, else null (issue
      // #1137). What a ~mute's cord reaches: the cord ends on the boundary
      // object, and this says which subpatcher it stands for.
      std::vector<gSubpatcher*> inletSubpatcher;

      // ADC sources fed by an external host buffer when the patcher runs as an
      // insert (issue #167). The host adapter points each ADC's channels at the
      // incoming audio before the block renders.
      std::vector<pObject*> adcs;

      // outletTargets[outlet.graphId] = the inlets that outlet feeds. Empty for
      // ids that belong to deleted objects or outlets with no connections.
      std::vector<std::vector<inlet*>> outletTargets;

      // inletDspSources[inlet.graphId] = how many buffer sources feed the
      // inlet in this snapshot (issue #1036: several are summed). Replaces the
      // audio-thread reads of ``inlet::dspConnections`` used by readiness
      // (WaitingForDSP) and the fan-in sum.
      std::vector<unsigned int> inletDspSources;

      // inletLinkSources[inlet.graphId] = the object at the other end of the
      // inlet's link cord, or null (issue #1038). A link is not a dependency:
      // it appears in neither ``outletTargets`` nor ``inletDspSources``, so it
      // never holds an object back and never closes a signal loop. This is
      // how the reader (~tapout) finds what it shares (~tapin's delay line)
      // without reading the live wiring.
      std::vector<pObject*> inletLinkSources;

      // outletOwner[id] / inletOwner[id] = the outlet / inlet that held graph id
      // `id` when this snapshot was built, or null for an unused id (issue
      // #963). A lookup only trusts ``outletTargets`` / ``inletDspSources`` for a
      // pin that finds itself here. The audio thread only ever reaches pins of
      // objects in its snapshot, so for it the check always passes; a control
      // thread can still hold an object deleted since (a ``.preset`` recall,
      // under an objectPin), whose id a Clear may have recompacted onto a new
      // object (issue #355) — without the check that object's sends would take
      // the new one's cords. What a pin not found here reads instead is
      // graphReadScope's business, below.
      std::vector<const outlet*> outletOwner;
      std::vector<const inlet*> inletOwner;

      // Whether the object owning the muteChain slice [begin, end) is muted
      // (issue #1137): its innermost subpatcher mutes its contents, or an
      // outer one mutes its nested subpatchers. Bit values are
      // gSubpatcher::MUTE_CONTENTS (1) and MUTE_NESTED (2). Audio thread;
      // one load per enclosing subpatcher, none at the top level.
      bool Muted(std::uint32_t begin, std::uint32_t end) const {
        if (begin == end) return false;
        if ((muteChain[begin]->load(std::memory_order_acquire) & 1U) != 0U) return true;
        for (std::uint32_t k = begin + 1; k < end; k++) {
          if ((muteChain[k]->load(std::memory_order_acquire) & 2U) != 0U) return true;
        }
        return false;
      }
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
