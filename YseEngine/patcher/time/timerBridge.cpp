#include "timerBridge.h"
#include "../../internal/global.h"

using namespace YSE::PATCHER;

timerBridge& YSE::PATCHER::TimerBridge() {
  static timerBridge s;
  return s;
}

timerBridge::timerBridge() : entries_(new Entry[CAPACITY]) {
  // Touch the timer singleton here so it is constructed *before* this one and
  // therefore destroyed *after* it. Without that, the first `TimerThread()` in
  // the process would be the one inside Reconcile — built second, torn down
  // first — and ~timerBridge's own ClearTimer sweep below would run against a
  // destroyed singleton. Cheap: timerThread's constructor spawns nothing, the
  // worker starting on the first Add.
  (void)TimerThread();
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    entries_[i].job.owner = this;
    entries_[i].job.slot = &entries_[i];
  }
}

timerBridge::~timerBridge() {
  // Join every job here, while this object is still whole, rather than leaving
  // it to ~threadPoolJob: that join runs in the *base* destructor, after
  // ~reconcileJob has already reset the vtable, so a worker picking the job up
  // inside that window would call the pure-virtual threadPoolJob::run().
  // ~unique_ptr nulling `entries_` before destroying the elements is the same
  // window clockBridge closes this way (#706).
  WaitIdle();
  // Whatever is still armed belongs to a timer that outlives this table only at
  // process exit; drop it so no callback survives into static teardown.
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    Entry& e = entries_[i];
    std::scoped_lock lock(e.mtx);
    if (e.armedId != timerThread::noTimer) {
      TimerThread().ClearTimer(e.armedId);
      e.armedId = timerThread::noTimer;
    }
  }
}

void timerBridge::reconcileJob::run() {
  owner->RunSlot(*slot);
}

timerBridge::Entry* timerBridge::Slot(Handle handle) {
  if (handle == 0 || handle > CAPACITY) return nullptr;
  return &entries_[handle - 1];
}

const timerBridge::Entry* timerBridge::Slot(Handle handle) const {
  if (handle == 0 || handle > CAPACITY) return nullptr;
  return &entries_[handle - 1];
}

timerBridge::Handle timerBridge::Claim(Callback fn, void* ctx) {
  if (fn == nullptr) {
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return 0;
  }

  // One CAS attempt per slot, CAPACITY slots: a bounded, lock-free walk. A
  // failed CAS means another thread just took that slot — skip it rather than
  // retry, so no caller ever spins here. clockBridge::Bind's rule.
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    Entry& e = entries_[i];
    if (e.state.load(std::memory_order_relaxed) != STATE_FREE) continue;
    std::uint32_t expected = STATE_FREE;
    if (!e.state.compare_exchange_strong(expected, STATE_CLAIMED, std::memory_order_acquire,
                                         std::memory_order_relaxed)) {
      continue;
    }

    // Ours, and nothing else reads these while the slot is CLAIMED. The wanted
    // state is reset here rather than in Release so a slot handed on to a new
    // owner starts from "stopped" whatever the previous one left behind — and
    // so a job still queued from that previous owner finds nothing to do.
    e.fn = fn;
    e.ctx = ctx;
    e.wantOn.store(false, std::memory_order_relaxed);
    e.wantPeriod.store(1, std::memory_order_relaxed);
    e.startSeq.store(0, std::memory_order_relaxed);
    // A push that never took — a pool that was down, a full background ring —
    // leaves `pending` set with no job to clear it, and every later request
    // would then coalesce into a job that does not exist. Nobody is issuing
    // requests against a slot that is still FREE, so this is the safe place to
    // heal it: if there really is a job queued (a stale one from the previous
    // owner), it clears the flag itself and this must not.
    if (!e.job.isQueued()) e.pending.store(false, std::memory_order_relaxed);

    // Publish; the release pairs with the acquire in Reconcile.
    e.state.store(STATE_BUSY, std::memory_order_release);
    return (Handle)(i + 1);
  }

  dropped_.fetch_add(1, std::memory_order_relaxed);
  return 0;
}

void timerBridge::Release(Handle handle) {
  Entry* e = Slot(handle);
  if (e == nullptr) return;

  {
    std::scoped_lock lock(e->mtx);
    // Stop wanting anything *before* the state goes back to FREE, so a job that
    // is already queued for this slot has nothing to act on even if it observes
    // the two stores out of order.
    e->wantOn.store(false, std::memory_order_relaxed);
    // ClearTimer blocks until an in-flight callback returns on the timer
    // worker. That handshake is the point: it is what lets the owner's fields
    // die once this returns. Never the audio thread — this is a destructor.
    if (e->armedId != timerThread::noTimer) {
      TimerThread().ClearTimer(e->armedId);
      e->armedId = timerThread::noTimer;
    }
    e->armedSeq = 0;
    e->armedPeriod = 0;
    e->fn = nullptr;
    e->ctx = nullptr;
    e->state.store(STATE_FREE, std::memory_order_release);
  }

  // Only heal `pending` when there is demonstrably no job left to clear it: a
  // queued one will do that itself, and clearing the flag under it would let a
  // re-claim push a second copy of a job that is already in the ring. The owner
  // is being destroyed, so nothing is racing this read.
  if (!e->job.isQueued()) e->pending.store(false, std::memory_order_relaxed);
}

void timerBridge::WantRun(Entry& e, bool on) {
  e.wantOn.store(on, std::memory_order_relaxed);
  // Released last, and read first by the reconciler, so the stores above are
  // visible to anyone who sees this sequence. A start and a stop both bump it,
  // which is what makes a restart-while-running a genuine re-phase rather than
  // a state the reconciler reads as "unchanged".
  e.startSeq.fetch_add(1, std::memory_order_release);
  e.reqSeq.fetch_add(1, std::memory_order_release);
}

void timerBridge::WantStart(Entry& e, timerThread::millisec periodMs) {
  e.wantPeriod.store(periodMs < 1 ? 1 : periodMs, std::memory_order_relaxed);
  WantRun(e, true);
}

bool timerBridge::WantPeriod(Entry& e, timerThread::millisec periodMs) {
  const timerThread::millisec ms = periodMs < 1 ? 1 : periodMs;
  if (e.wantPeriod.load(std::memory_order_relaxed) == ms) return false;
  e.wantPeriod.store(ms, std::memory_order_relaxed);
  // No `startSeq` bump: this is a retime, not a restart, so the reconciler
  // reaches its SetPeriod branch and the phase survives (issue #625).
  e.reqSeq.fetch_add(1, std::memory_order_release);
  return true;
}

void timerBridge::ArmJob(Entry& e) {
  // Only the requester that flips `pending` false->true pushes, so the same job
  // pointer is never in the ring twice. The reconciler clears the flag when it
  // is ready to be pushed again — see RunSlot for how the window between "the
  // job stopped looking" and "the job is queueable again" is closed.
  if (e.pending.exchange(true, std::memory_order_acq_rel)) return;
  INTERNAL::Global().addSlowJob(&e.job);
  // addSlowJob is a no-op on a pool that was never started or has been shut
  // down, and asserts-and-drops on a full background ring. Nothing is undone
  // here: the slot keeps its wanted state, the next request finds `pending`
  // already set, and Release resets the slot whatever happened. A pool that is
  // down also means an engine that is not rendering, so the audio-thread caller
  // this path exists for cannot be running either.
}

void timerBridge::Reconcile(Entry& e) {
  if (e.state.load(std::memory_order_acquire) != STATE_BUSY) return;

  const std::uint64_t seq = e.startSeq.load(std::memory_order_acquire);
  const bool on = e.wantOn.load(std::memory_order_relaxed);
  const std::int64_t period = e.wantPeriod.load(std::memory_order_relaxed);

  if (seq != e.armedSeq) {
    // A start or a stop: the run this timer belongs to has been replaced, so
    // the timer is replaced with it rather than retimed. That is what makes a
    // restart a genuine re-phase.
    if (e.armedId != timerThread::noTimer) {
      TimerThread().ClearTimer(e.armedId);
      e.armedId = timerThread::noTimer;
    }
    e.armedSeq = seq;
    if (on) {
      Callback fn = e.fn;
      void* ctx = e.ctx;
      if (fn != nullptr) {
        // The std::function is built here — on the pool or on the control
        // thread — and never by the requester, which is the point of taking a
        // function pointer and a context instead of a callable.
        e.armedId = TimerThread().Add(period, period, [fn, ctx] { fn(ctx); });
        e.armedPeriod = period;
      }
    }
    return;
  }

  // Same run, new interval: retime without re-triggering (issue #625).
  if (e.armedId != timerThread::noTimer && period != e.armedPeriod) {
    TimerThread().SetPeriod(e.armedId, period);
    e.armedPeriod = period;
  }
}

void timerBridge::RunSlot(Entry& e) {
  for (;;) {
    const std::uint64_t seen = e.reqSeq.load(std::memory_order_acquire);
    {
      std::scoped_lock lock(e.mtx);
      Reconcile(e);
    }
    // Queueable again. Anything that arrives from here on either finds the flag
    // clear and pushes, or is caught by the re-read below.
    e.pending.store(false, std::memory_order_release);
    if (e.reqSeq.load(std::memory_order_acquire) == seen) return;
    // A request landed while we were working. Take the flag back and go round
    // again — unless a requester beat us to it, in which case its push will run
    // this job afresh and there is nothing left here to do.
    if (e.pending.exchange(true, std::memory_order_acq_rel)) return;
  }
}

void timerBridge::RequestStart(Handle handle, timerThread::millisec periodMs) {
  Entry* e = Slot(handle);
  if (e == nullptr) return;
  WantStart(*e, periodMs);
  ArmJob(*e);
}

void timerBridge::RequestStop(Handle handle) {
  Entry* e = Slot(handle);
  if (e == nullptr) return;
  WantRun(*e, false);
  ArmJob(*e);
}

void timerBridge::RequestPeriod(Handle handle, timerThread::millisec periodMs) {
  Entry* e = Slot(handle);
  if (e == nullptr) return;
  if (!WantPeriod(*e, periodMs)) return;
  ArmJob(*e);
}

void timerBridge::ApplyStart(Handle handle, timerThread::millisec periodMs) {
  Entry* e = Slot(handle);
  if (e == nullptr) return;
  WantStart(*e, periodMs);
  std::scoped_lock lock(e->mtx);
  Reconcile(*e);
}

void timerBridge::ApplyStop(Handle handle) {
  Entry* e = Slot(handle);
  if (e == nullptr) return;
  WantRun(*e, false);
  std::scoped_lock lock(e->mtx);
  Reconcile(*e);
}

void timerBridge::ApplyPeriod(Handle handle, timerThread::millisec periodMs) {
  Entry* e = Slot(handle);
  if (e == nullptr) return;
  if (!WantPeriod(*e, periodMs)) return;
  std::scoped_lock lock(e->mtx);
  Reconcile(*e);
}

std::uint64_t timerBridge::Dropped() const {
  return dropped_.load(std::memory_order_relaxed);
}

void timerBridge::WaitIdle() {
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    entries_[i].job.join();
  }
}
