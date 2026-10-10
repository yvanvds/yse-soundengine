#pragma once
// Stages the window issues #990 and #992 are about: a manager's delete request
// raised while its previous delete job is already running and has walked past
// the impl the request is for.
//
// Every manager with a slow-pool delete job used to clear its `runDelete`
// request whether or not it could enqueue the job, and isQueued() stays true
// while a job *runs*. A running job that had already passed the newly marked
// impl then finished without it, and the dropped request left that impl in the
// canonical list until some other object of the same kind was released. The
// CHANNEL case (#990) staged this inline; the six managers #992 fixed the same
// way share this helper instead of six copies of it.
//
// The caller sets the stage and checks the outcome:
//   - create a large bulk of objects, then `x` *last*, so `x` heads the
//     canonical list (every manager emplaces at the front) and is the first
//     entry the delete job walks;
//   - bring all of them into the manager's audio-thread working list;
//   - call this, then wait (bounded) for the canonical list to drop back to
//     the count sampled before the bulk was created.
//
// What this does, with the background pool's one worker and its FIFO ring:
//   - `releaseBulk` runs with the worker parked, the next tick marks the bulk
//     OBJECT_DELETE, and the tick after that queues the delete job behind the
//     blocker;
//   - `releaseX` runs while the worker is still parked. Only the audio-thread
//     tick marks an impl, so `x` is not yet OBJECT_DELETE when the job reaches
//     it. (Releasing it here rather than after unparking matters for CLOCK,
//     whose destroyClock() takes the list mutex the running job holds.)
//   - unparking hands the worker straight to the delete job, which walks past
//     `x` first and then spends a long time destroying the bulk;
//   - the next tick marks `x`, and the tick after that finds the job still
//     running — the tick that used to drop the request.
//
// A fixed engine reclaims `x` whatever the interleaving, so the timing here
// cannot make a case fail spuriously; it only decides whether an unfixed
// engine is caught. The bulk size is what buys the margin: the job has to
// outlast the spin below plus two ticks.

#include <chrono>

#include "patcher/pool_blocker.hpp"

namespace TestHelpers {

  // Give the worker time to pick up the delete job and walk past the list
  // head, which it does within microseconds of Unpark(). A spin rather than
  // sleep_for: on Windows a sleep can overshoot by a whole timer quantum
  // (~15 ms), longer than some of the managers' delete jobs.
  inline void spinFor(std::chrono::microseconds d) {
    const auto until = std::chrono::steady_clock::now() + d;
    while (std::chrono::steady_clock::now() < until) {}
  }

  // Returns false only if the pool could not be parked; the caller REQUIREs it.
  template <typename Tick, typename ReleaseBulk, typename ReleaseX>
  bool releaseDuringInFlightDelete(Tick tick, ReleaseBulk releaseBulk, ReleaseX releaseX) {
    PoolBlocker blocker;
    if (!blocker.Park()) return false;
    releaseBulk();
    tick(); // marks the bulk OBJECT_DELETE, raising the delete request
    tick(); // queues the delete job behind the blocker
    releaseX();
    blocker.Unpark();
    spinFor(std::chrono::microseconds(50));
    tick(); // marks x while the job is busy with the bulk
    tick(); // the job is still in flight: x's request must survive this tick
    return true;
  }

} // namespace TestHelpers
