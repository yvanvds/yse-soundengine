#pragma once
#include "../pObject.h"

#include <atomic>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief `.inlet` — one inlet of the subpatcher it lives in (issue #545).
     *         Max's `inlet`.
     *
     *  ### What it is
     *
     *  A pass-through with a number. Put it inside a `patcher` object, give it
     *  index N, and inlet N of that subpatcher is this object: whatever the
     *  parent patch sends to the subpatcher's inlet N comes out of this
     *  object's outlet, inside the subpatcher.
     *
     *  ### How the boundary actually works
     *
     *  There is no boundary at run time. `patcher.Connect(source, 0, sub, 2)`
     *  is resolved on the control thread by
     *  `patcherImplementation::ConnectUnlocked`, which looks up the `.inlet`
     *  object with index 2 among the subpatcher's contents and records an
     *  ordinary cord from `source`'s outlet to **this object's inlet 0**. The
     *  compiled graph therefore contains a plain edge between two plain
     *  objects, and the render traversal crosses a subpatcher boundary at
     *  exactly the cost of one pass-through object — the same cost Max charges
     *  for the same object, and independent of how deeply the subpatcher is
     *  nested. See gSubpatcher.h for the model this is half of.
     *
     *  That is also why this object has an inlet at all, where Max's `inlet`
     *  appears to have none. Max hides it because a patcher window draws the
     *  boundary for you; headless, the cord has to land somewhere, and landing
     *  it on a real inlet is what lets `Disconnect`, `UnwireFromPeers`,
     *  `DumpJSON` and the graph compiler treat it as the ordinary edge it is.
     *
     *  ### What crosses
     *
     *  Bang, int, float and list — the four kinds a patch cord carries between
     *  control objects, and exactly what `.gate`, `.switch` and every other
     *  routing object in this patcher forwards. Audio-rate signals do **not**
     *  cross: this is a control-rate object, and MSP-style signal inlets are a
     *  separate pass (issue #764). A `SetMessage` message is not forwarded
     *  either, for the reason `.loadbang` gives for not accepting one — the
     *  signature carries no `THREAD` tag, so forwarding would mean inventing
     *  one.
     *
     *  ### Index
     *
     *  One creation argument, the boundary inlet number, default 0. Changing it
     *  live re-points which boundary inlet *future* connections resolve to; it
     *  does not move cords that already exist, which stay attached to this
     *  object because that is where they were recorded. Two `.inlet` objects
     *  claiming the same index in one subpatcher is not an error — the first
     *  one found wins, and which that is is unspecified. Give them distinct
     *  numbers.
     *
     *  ### Real-time behaviour
     *
     *  Every handler is one `outlet::Send*`. `Calculate()` does nothing.
     *  Nothing here allocates, locks or blocks on any thread.
     */
    PATCHER_CLASS(gInlet, YSE::OBJ::G_INLET)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    /**
     *  @brief Which inlet of the enclosing subpatcher this object is.
     *
     *  Read on the control thread by `patcherImplementation`'s boundary
     *  resolution while a live `SetParams` may be storing a new value from the
     *  audio thread's param drain (issue #234), which is why it is atomic.
     */
    int Index() const {
      return index.load(std::memory_order_relaxed);
    }

  private:
    std::atomic<int> index{0};
  };

} // namespace PATCHER
} // namespace YSE
