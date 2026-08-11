#include "headers/defines.hpp"
// See the matching guard in midiPortOpener.h.
#if YSE_ENABLE_MIDI_DEVICE
#include "midiPortOpener.h"
#include "../../internal/global.h"
#include "../../midi/device.hpp"

#include <thread>

using namespace YSE::PATCHER;

midiPortOpener& YSE::PATCHER::MidiPortOpener() {
  static midiPortOpener s;
  return s;
}

midiPortOpener::midiPortOpener() : entries_(new Entry[CAPACITY]) {
  // Control thread, once: everything a request needs afterwards already exists.
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    entries_[i].job.owner = this;
    entries_[i].job.slot = &entries_[i];
  }
}

midiPortOpener::~midiPortOpener() {
  // Join every open job here, while this object is still whole, rather than
  // leaving it to ~threadPoolJob. That join runs in the *base* destructor —
  // after ~openJob has already reset the vtable — so a worker that picks the
  // job up inside that window would call threadPoolJob::run(), which is pure
  // virtual. midiPortScanner's destructor closes the same three windows.
  WaitIdle();
}

void midiPortOpener::openJob::run() {
  owner->RunSlot(*slot);
}

midiPortOpener::Entry* midiPortOpener::Slot(Handle handle) {
  if (handle == 0 || handle > CAPACITY) return nullptr;
  return &entries_[handle - 1];
}

const midiPortOpener::Entry* midiPortOpener::Slot(Handle handle) const {
  if (handle == 0 || handle > CAPACITY) return nullptr;
  return &entries_[handle - 1];
}

midiPortOpener::Handle midiPortOpener::Claim() {
  // One CAS attempt per slot, CAPACITY slots: a bounded, lock-free walk. A
  // failed CAS means another thread just took that slot — skip it rather than
  // retry, so no caller ever spins here.
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    Entry& e = entries_[i];
    if (e.state.load(std::memory_order_relaxed) != STATE_FREE) continue;
    std::uint32_t expected = STATE_FREE;
    if (!e.state.compare_exchange_strong(expected, STATE_IDLE, std::memory_order_acq_rel,
                                         std::memory_order_relaxed)) {
      continue;
    }
    // A slot handed back by a previous owner still points at that owner's
    // midiOut. Nothing reads it before the next Request writes it, but leaving
    // a dead pointer lying in a live slot is how a later change gets it wrong.
    e.target = nullptr;
    e.port = 0;
    return static_cast<Handle>(i + 1);
  }

  dropped_.fetch_add(1, std::memory_order_relaxed);
  return 0;
}

void midiPortOpener::Release(Handle handle) {
  Entry* e = Slot(handle);
  if (e == nullptr) return;

  for (;;) {
    const std::uint32_t current = e->state.load(std::memory_order_acquire);
    if (current == STATE_FREE) return;
    if (current == STATE_OPENING) {
      // The one state in which a worker is reaching into the owner's midiOut.
      // Waiting it out is bounded by a single openPort, and it cannot deadlock:
      // a reclaimer that is itself on a pool worker cannot also be executing
      // that job. This is midiPortScanner::Release's reasoning and handshake.
      std::this_thread::yield();
      continue;
    }
    std::uint32_t expected = current;
    if (e->state.compare_exchange_strong(expected, STATE_FREE, std::memory_order_acq_rel,
                                         std::memory_order_relaxed)) {
      // A job still queued for this slot finds it FREE when it runs and does
      // nothing, so there is nothing to cancel and nothing to join.
      return;
    }
  }
}

bool midiPortOpener::Request(Handle handle, YSE::midiOut* into, unsigned int port) {
  Entry* e = Slot(handle);
  if (e == nullptr || into == nullptr) return false;

  std::uint32_t expected = STATE_IDLE;
  if (e->state.compare_exchange_strong(expected, STATE_SETUP, std::memory_order_acq_rel,
                                       std::memory_order_relaxed)) {
    // SETUP is the one state no job acts on: the slot is claimed but not yet
    // armed, so nothing on the pool can be reading these two fields while they
    // are written. The release store below is what hands them over.
    e->target = into;
    e->port = port;
    e->state.store(STATE_ARMED, std::memory_order_release);
    Arm(*e);
    return true;
  }

  if (expected == STATE_FREE) return false;

  if (expected == STATE_ARMED) {
    // Recovery rather than a second ask: addSlowJob is a no-op on a pool that
    // was never started or has been shut down, and drops on a full background
    // ring, so an arm can silently not have landed. isQueued makes a redundant
    // re-arm one atomic load, which is what makes asking on every message
    // affordable on the audio callback.
    Arm(*e);
  }
  return true;
}

bool midiPortOpener::Consume(Handle handle) {
  Entry* e = Slot(handle);
  if (e == nullptr) return false;

  if (e->state.load(std::memory_order_acquire) != STATE_READY) return false;

  // The acquire above pairs with the job's release store into `state`, so the
  // port pointer `midiOut::create` wrote is visible to this thread. That is the
  // whole publication: the caller sends on its own midiOut from here on.
  e->state.store(STATE_IDLE, std::memory_order_release);
  return true;
}

void midiPortOpener::Arm(Entry& e) {
  // A job already on the pool will read the slot anyway, so a second push would
  // only duplicate work. isQueued is the same guard clockBridge::ArmResolve
  // uses before enqueuing from the audio callback.
  if (e.job.isQueued()) return;
  INTERNAL::Global().addSlowJob(&e.job);
}

void midiPortOpener::RunSlot(Entry& e) {
  std::uint32_t expected = STATE_ARMED;
  if (!e.state.compare_exchange_strong(expected, STATE_OPENING, std::memory_order_acq_rel,
                                       std::memory_order_relaxed)) {
    // Released, or already collected and idle again. Nothing to do — and, more
    // to the point, nothing this job may touch.
    return;
  }

  // Background pool, which is the entire point of this class: an allocation, a
  // driver call that may block, a map insert and MIDI::deviceManager's mutex.
  if (e.target != nullptr) e.target->create(e.port);
  opened_.fetch_add(1, std::memory_order_relaxed);

  // Cannot fail: OPENING is the one state Release waits out rather than
  // claiming, so no other thread can have moved the slot out from under us.
  expected = STATE_OPENING;
  e.state.compare_exchange_strong(expected, STATE_READY, std::memory_order_acq_rel,
                                  std::memory_order_relaxed);
}

bool midiPortOpener::Busy(Handle handle) const {
  const Entry* e = Slot(handle);
  if (e == nullptr) return false;
  const std::uint32_t current = e->state.load(std::memory_order_acquire);
  return current == STATE_SETUP || current == STATE_ARMED || current == STATE_OPENING ||
         current == STATE_READY;
}

bool midiPortOpener::Settled(Handle handle) const {
  const Entry* e = Slot(handle);
  if (e == nullptr) return false;
  return e->state.load(std::memory_order_acquire) == STATE_READY;
}

std::uint64_t midiPortOpener::Dropped() const {
  return dropped_.load(std::memory_order_relaxed);
}

std::uint64_t midiPortOpener::Opened() const {
  return opened_.load(std::memory_order_relaxed);
}

void midiPortOpener::WaitIdle() {
  // Arm first: a slot whose push never reached the pool would otherwise be
  // joined instantly and still be waiting for an open that nobody will run.
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    if (entries_[i].state.load(std::memory_order_acquire) == STATE_ARMED) Arm(entries_[i]);
  }
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    entries_[i].job.join();
  }
}

#endif
