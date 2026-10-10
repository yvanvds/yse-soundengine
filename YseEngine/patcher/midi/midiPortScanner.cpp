#include "headers/defines.hpp"
// See the matching guard in midiPortScanner.h.
#if YSE_ENABLE_MIDI_DEVICE
#include "midiPortScanner.h"
#include "../../internal/global.h"
#include "../../midi/midiDeviceManager.h"

#include <cstring>
#include <string>
#include <thread>

using namespace YSE::PATCHER;

namespace {

  // One direction's ports into `names`, returning how many were stored.
  // Control thread or the background pool: every call here allocates a
  // std::string inside RtMidi and takes the device manager's mutex.
  int FillNames(bool input,
                char names[midiPortSnapshot::PORTS_MAX][midiPortSnapshot::NAME_CAPACITY]) {
    YSE::MIDI::deviceManager& devices = YSE::MIDI::DeviceManager();
    const unsigned int available =
        input ? devices.getNumMidiInDevices() : devices.getNumMidiOutDevices();

    int stored = 0;
    for (unsigned int i = 0; i < available && stored < midiPortSnapshot::PORTS_MAX; i++) {
      const std::string name =
          input ? devices.getMidiInDeviceName(i) : devices.getMidiOutDeviceName(i);
      // Truncated rather than refused: a patch that can see fifteen useful
      // characters of a name can still pick the right port, and a name the
      // scan dropped would leave a hole in the index sequence.
      std::size_t length = name.size();
      if (length > midiPortSnapshot::NAME_CAPACITY - 1)
        length = midiPortSnapshot::NAME_CAPACITY - 1;
      std::memcpy(names[stored], name.data(), length);
      names[stored][length] = '\0';
      stored++;
    }
    return stored;
  }

} // namespace

midiPortScanner& YSE::PATCHER::MidiPortScanner() {
  static midiPortScanner s;
  return s;
}

midiPortScanner::midiPortScanner() : entries_(new Entry[CAPACITY]) {
  // Control thread, once: everything a request needs afterwards already exists.
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    entries_[i].job.owner = this;
    entries_[i].job.slot = &entries_[i];
  }
}

midiPortScanner::~midiPortScanner() {
  // Join every scan job here, while this object is still whole, rather than
  // leaving it to ~threadPoolJob. That join runs in the *base* destructor —
  // after ~scanJob has already reset the vtable — so a worker that picks the
  // job up inside that window would call threadPoolJob::run(), which is pure
  // virtual. clockBridge's destructor closes the same three windows.
  WaitIdle();
}

void midiPortScanner::scanJob::run() {
  owner->RunSlot(*slot);
}

void midiPortScanner::ScanNow(midiPortSnapshot& into) {
  into.count[midiPortSnapshot::DIR_INPUT] =
      FillNames(true, into.names[midiPortSnapshot::DIR_INPUT]);
  into.count[midiPortSnapshot::DIR_OUTPUT] =
      FillNames(false, into.names[midiPortSnapshot::DIR_OUTPUT]);
}

midiPortScanner::Entry* midiPortScanner::Slot(Handle handle) {
  if (handle == 0 || handle > CAPACITY) return nullptr;
  return &entries_[handle - 1];
}

const midiPortScanner::Entry* midiPortScanner::Slot(Handle handle) const {
  if (handle == 0 || handle > CAPACITY) return nullptr;
  return &entries_[handle - 1];
}

midiPortScanner::Handle midiPortScanner::Claim() {
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
    // A slot handed back by a previous owner may still carry its `again`.
    e.again.store(false, std::memory_order_relaxed);
    return static_cast<Handle>(i + 1);
  }

  dropped_.fetch_add(1, std::memory_order_relaxed);
  return 0;
}

void midiPortScanner::Release(Handle handle) {
  Entry* e = Slot(handle);
  if (e == nullptr) return;

  for (;;) {
    const std::uint32_t current = e->state.load(std::memory_order_acquire);
    if (current == STATE_FREE) return;
    if (current == STATE_SCANNING) {
      // The one state in which a worker is writing this slot's table. Waiting
      // it out is bounded by a single enumeration, and it cannot deadlock: a
      // reclaimer that is itself on the pool worker cannot also be executing
      // that job. This is timerBridge::Release's reasoning, and its handshake.
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

bool midiPortScanner::Request(Handle handle) {
  Entry* e = Slot(handle);
  if (e == nullptr) return false;

  std::uint32_t expected = STATE_IDLE;
  if (e->state.compare_exchange_strong(expected, STATE_ARMED, std::memory_order_acq_rel,
                                       std::memory_order_relaxed)) {
    Arm(*e);
    return true;
  }

  if (expected == STATE_FREE) return false;

  // A scan is in flight, or one has finished and is waiting to be consumed.
  // Remember the ask; Consume re-arms on its way out, so a controller plugged
  // in between two refreshes is still picked up. Worst case this costs one
  // redundant scan, which is cheaper than a lost one.
  e->again.store(true, std::memory_order_release);
  return true;
}

bool midiPortScanner::Consume(Handle handle, midiPortSnapshot& into) {
  Entry* e = Slot(handle);
  if (e == nullptr) return false;

  const std::uint32_t current = e->state.load(std::memory_order_acquire);
  if (current == STATE_ARMED) {
    // Recovery: addSlowJob is a no-op on a pool that was never started or has
    // been shut down, and drops on a full background ring, so an arm can
    // silently not have landed. Re-arming is one atomic load in the common
    // case (the job *is* queued), which is what makes this affordable on the
    // audio thread once per block.
    Arm(*e);
    return false;
  }
  if (current != STATE_READY) return false;

  // The acquire above pairs with the job's release store into `state`, so
  // every byte the scan wrote is visible here. Nothing can be writing the
  // table in READY, which is what makes this plain copy safe rather than
  // merely unlikely to tear.
  into = e->table;
  e->state.store(STATE_IDLE, std::memory_order_release);

  if (e->again.exchange(false, std::memory_order_acq_rel)) {
    Request(handle);
  }
  return true;
}

void midiPortScanner::Arm(Entry& e) {
  // A job already on the pool will read the slot anyway, so a second push would
  // only duplicate work. isQueued is the same guard clockBridge::ArmResolve
  // uses before enqueuing from the audio callback.
  if (e.job.isQueued()) return;
  INTERNAL::Global().addSlowJob(&e.job);
}

void midiPortScanner::RunSlot(Entry& e) {
  std::uint32_t expected = STATE_ARMED;
  if (!e.state.compare_exchange_strong(expected, STATE_SCANNING, std::memory_order_acq_rel,
                                       std::memory_order_relaxed)) {
    // Released, or already consumed and idle again. Nothing to do — and, more
    // to the point, nothing this job may write.
    return;
  }

  ScanNow(e.table);

  // Cannot fail: SCANNING is the one state Release waits out rather than
  // claiming, so no other thread can have moved the slot out from under us.
  expected = STATE_SCANNING;
  e.state.compare_exchange_strong(expected, STATE_READY, std::memory_order_acq_rel,
                                  std::memory_order_relaxed);
}

bool midiPortScanner::Busy(Handle handle) const {
  const Entry* e = Slot(handle);
  if (e == nullptr) return false;
  const std::uint32_t current = e->state.load(std::memory_order_acquire);
  return current == STATE_ARMED || current == STATE_SCANNING || current == STATE_READY;
}

std::uint64_t midiPortScanner::Dropped() const {
  return dropped_.load(std::memory_order_relaxed);
}

void midiPortScanner::WaitIdle() {
  // Arm first: a slot whose push never reached the pool would otherwise be
  // joined instantly and still be waiting for a scan that nobody will run.
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    if (entries_[i].state.load(std::memory_order_acquire) == STATE_ARMED) Arm(entries_[i]);
  }
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    entries_[i].job.join();
  }
}

#endif
