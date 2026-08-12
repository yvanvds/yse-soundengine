#pragma once
#include "../pObject.h"

#include <atomic>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief `~outlet` — one **audio-rate** outlet of the subpatcher it lives
     *         in (issue #764). The signal half of `.outlet`.
     *
     *  ### What it is
     *
     *  `~inlet` read the other way round. Put it inside a `patcher` object, give
     *  it index N, wire the encapsulated DSP graph into its inlet, and outlet N
     *  of that subpatcher is this object: the signal that arrives here leaves
     *  the subpatcher and reaches whatever the parent patch connected to outlet
     *  N. A `~sine` inside a subpatcher can therefore reach a `~dac` outside it,
     *  which is the acceptance criterion #764 was filed for.
     *
     *  ### What the render does at the boundary
     *
     *  Forwards a pointer. `Calculate()` sends on the exact `DSP::buffer*` that
     *  arrived on its inlet, so the object *outside* the subpatcher is handed
     *  the same buffer the object *inside* it computed into — no copy, no buffer
     *  of this object's own, no indirection added to the audio path. See
     *  dInlet.h, which gives the full argument for both halves, and
     *  gSubpatcher.h for the model.
     *
     *  ### Index
     *
     *  One creation argument, the boundary outlet number, default 0, in the
     *  index space shared with `.outlet` — a subpatcher has one set of outlet
     *  pins and outlet N is one pin whichever rate is behind it. The same
     *  duplicate-index and live-edit notes as `.outlet` apply.
     *
     *  ### Rate mismatch
     *
     *  This inlet takes a buffer and nothing else; a message wired into it is
     *  dropped rather than refused, as on any DSP object's signal inlet. Use
     *  `.outlet` for the pins that carry messages.
     *
     *  ### Real-time behaviour
     *
     *  `Calculate()` is one null check and one send. Nothing here allocates,
     *  locks or blocks on any thread.
     */
    PATCHER_CLASS(dOutlet, YSE::OBJ::D_OUTLET)
    _NO_MESSAGES
    _DO_CALCULATE
    _DO_RESET

    _BUFFER_IN(SetInBuffer)

    /**
     *  @brief Which outlet of the enclosing subpatcher this object is.
     *
     *  Atomic for the reason `gOutlet::Index()` is: read on the control thread
     *  during boundary resolution while a live `SetParams` may be storing a new
     *  value from the audio thread's param drain (issue #234).
     */
    int Index() const {
      return index.load(std::memory_order_relaxed);
    }

  private:
    std::atomic<int> index{0};

    // Borrowed for the duration of one block — see dInlet.h.
    DSP::buffer* in;
  };

} // namespace PATCHER
} // namespace YSE
