#pragma once
#include <vector>

namespace YSE {
  namespace PATCHER {

    class pObject;
    struct inlet;

    // Immutable, compiled snapshot of a patcher's topology (issue #226).
    //
    // Built off the audio thread from the objects' control-thread wiring and
    // published to the audio thread through a single
    // ``std::atomic<const GraphState*>`` pointer swap. Once published a
    // GraphState is never mutated; ``Calculate`` pins one snapshot for the
    // duration of a block and resolves every send/readiness query against it,
    // so the audio thread never touches the live (mutable) object wiring and
    // needs no lock.
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
    };

  } // namespace PATCHER
} // namespace YSE
