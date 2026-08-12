#pragma once
#include "../pObject.h"

#include <atomic>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief `.outlet` — one outlet of the subpatcher it lives in (issue
     *         #545). Max's `outlet`.
     *
     *  ### What it is
     *
     *  `.inlet` read the other way round. Put it inside a `patcher` object,
     *  give it index N, wire the encapsulated graph into its inlet, and outlet
     *  N of that subpatcher is this object: what arrives here leaves the
     *  subpatcher and reaches whatever the parent patch connected to outlet N.
     *
     *  ### How the boundary actually works
     *
     *  Symmetrically to `.inlet`, and with the same consequence.
     *  `patcher.Connect(sub, 1, dest, 0)` is resolved on the control thread by
     *  `patcherImplementation::ConnectUnlocked`, which finds the `.outlet`
     *  object with index 1 among the subpatcher's contents and records an
     *  ordinary cord from **this object's outlet 0** to `dest`'s inlet 0. No
     *  subpatcher appears in the compiled graph, and the render pays one
     *  pass-through object per crossing regardless of nesting depth. See
     *  gSubpatcher.h for the model.
     *
     *  Max's `outlet` appears to have no outlet for the same reason its `inlet`
     *  appears to have no inlet — the window draws the boundary. Headless, the
     *  cord has to leave from a real outlet, which is what keeps a boundary
     *  edge indistinguishable from any other edge to everything that handles
     *  edges.
     *
     *  ### What crosses
     *
     *  Bang, int, float and list, unchanged in kind. Audio-rate signals do not
     *  (issue #764), and `SetMessage` is not forwarded — see gInlet.h, which
     *  gives the reasoning for both.
     *
     *  ### Index
     *
     *  One creation argument, the boundary outlet number, default 0. The same
     *  live-edit and duplicate-index notes as `.inlet` apply.
     *
     *  ### Real-time behaviour
     *
     *  Every handler is one `outlet::Send*`. `Calculate()` does nothing.
     *  Nothing here allocates, locks or blocks on any thread.
     */
    PATCHER_CLASS(gOutlet, YSE::OBJ::G_OUTLET)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    /**
     *  @brief Which outlet of the enclosing subpatcher this object is.
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
  };

} // namespace PATCHER
} // namespace YSE
