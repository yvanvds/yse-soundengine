#pragma once
#include "../pObject.h"

#include <atomic>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief `~inlet` — one **audio-rate** inlet of the subpatcher it lives in
     *         (issue #764). The signal half of `.inlet`.
     *
     *  ### What it is
     *
     *  `.inlet` carrying a buffer instead of a message. Put it inside a
     *  `patcher` object, give it index N, and inlet N of that subpatcher is this
     *  object: the signal the parent patch wires into the subpatcher's inlet N
     *  comes out of this object's outlet, inside the subpatcher. A `~adc`
     *  outside a subpatcher can reach a `~lp` inside it, which is the half of
     *  the boundary #545 deliberately left out.
     *
     *  ### What the render actually does at the boundary
     *
     *  **It forwards a pointer, and that is the whole of it.** This patcher's
     *  signal path is already pointer-passing rather than slot assignment: a
     *  DSP object computes into a buffer it owns and hands its address down the
     *  cord with `outlet::SendBuffer`, and every reader — `~+`'s operands,
     *  `~dac`'s channels — stores that address and reads through it. A
     *  pass-through therefore has nothing to copy. `Calculate()` sends on the
     *  exact `DSP::buffer*` that arrived, so the object downstream of the
     *  boundary is handed the *same buffer the upstream source produced*, not a
     *  copy of it and not a handle to one.
     *
     *  So crossing a subpatcher boundary with a signal costs one `SetBuffer`
     *  (a store and a ready flag), one `Calculate()` and one `SendBuffer`
     *  fan-out — no buffer copy, no extra buffer allocated, no level of
     *  indirection added to the audio path, and nothing that scales with block
     *  size. It is the same cost the control-rate boundary charges for a
     *  message, and like it, independent of nesting depth: the compiled graph
     *  holds a plain DSP edge between two plain objects, and depth is not
     *  something the render can see. See gSubpatcher.h for the model both
     *  halves are built on.
     *
     *  ### How the boundary is resolved, and why the index space is shared
     *
     *  Exactly as for `.inlet`: `patcher.Connect(source, 0, sub, 2)` is resolved
     *  on the control thread by `patcherImplementation::ConnectUnlocked`, which
     *  finds the boundary object claiming inlet 2 among the subpatcher's
     *  contents and records an ordinary cord to **this object's inlet 0**.
     *
     *  What is new is that the lookup considers `.inlet` and `~inlet`
     *  *together*, and it has to. A subpatcher has one set of inlet pins,
     *  numbered by the parent; "inlet 2" is a single pin, and whether it carries
     *  a signal or a message is a property of what is behind it, not a second
     *  numbering. Two index spaces would make `Connect(source, 0, sub, 2)`
     *  ambiguous and `SubpatcherInlets` unanswerable. So `~inlet 2` and
     *  `.inlet 2` in one subpatcher are two objects claiming the same pin — the
     *  same duplicate-index situation `.inlet` already documents, with the same
     *  answer: it is not an error, the first one found wins, which that is is
     *  unspecified, and the fix is to give them distinct numbers.
     *
     *  ### Why this is a separate object rather than a signal-capable `.inlet`
     *
     *  Because `IsDSPObject()` is not a detail. It decides whether the object is
     *  a DSP start point, whether its inlets accept a `T_GUI` dispatch, and what
     *  every palette, binding and metadata consumer is told the object *is*. An
     *  object that answered both would be lying to one of them. The rest of this
     *  patcher splits the two rates the same way (`.+` vs `~+`), and the issue
     *  that scoped this out of #545 reached the same conclusion.
     *
     *  ### Rate mismatch
     *
     *  Wiring a control-rate outlet to this object's inlet is not refused and
     *  does nothing: the inlet registers only a buffer handler, so a float or a
     *  bang arriving there is dropped, exactly as it is for any other DSP
     *  object's signal inlet. Use `.inlet` for the pins that carry messages —
     *  a subpatcher is free to have some of each.
     *
     *  ### Index
     *
     *  One creation argument, the boundary inlet number, default 0. The same
     *  live-edit note as `.inlet`: changing it re-points which boundary inlet
     *  *future* connections resolve to and does not move cords that already
     *  exist.
     *
     *  ### Real-time behaviour
     *
     *  `Calculate()` is one null check and one send. Nothing here allocates,
     *  locks or blocks on any thread, and the object holds no buffer of its own
     *  to allocate in the first place.
     */
    PATCHER_CLASS(dInlet, YSE::OBJ::D_INLET)
    _NO_MESSAGES
    _DO_CALCULATE
    _DO_RESET

    _BUFFER_IN(SetInBuffer)

    /**
     *  @brief Which inlet of the enclosing subpatcher this object is.
     *
     *  Atomic for the reason `gInlet::Index()` is: read on the control thread
     *  during boundary resolution while a live `SetParams` may be storing a new
     *  value from the audio thread's param drain (issue #234).
     */
    int Index() const {
      return index.load(std::memory_order_relaxed);
    }

  private:
    std::atomic<int> index{0};

    // The buffer that arrived this block, forwarded unchanged. A borrowed
    // pointer into the *upstream* object's own buffer, valid for exactly as
    // long as the block that delivered it — which is why `ResetDSP` nulls it at
    // the top of every block and why this object owns no buffer of its own.
    DSP::buffer* in;
  };

} // namespace PATCHER
} // namespace YSE
