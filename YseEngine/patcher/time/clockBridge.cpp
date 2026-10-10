#include "clockBridge.h"
#include "../../clock/clockManager.h"
#include "../../internal/global.h"
#include <cstring>
#include <string>

using namespace YSE::PATCHER;

clockBridge::clockBridge() : entries_(new Entry[CAPACITY]) {
  // Control thread, once: everything a bind needs afterwards already exists.
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    entries_[i].job.owner = this;
    entries_[i].job.slot = &entries_[i];
  }
}

clockBridge::~clockBridge() {
  // Join every resolve job here, while this object is still whole, rather than
  // leaving it to ~threadPoolJob. That join runs in the *base* destructor —
  // after ~resolveJob has already reset the vtable — so a worker that picks the
  // job up inside that window calls threadPoolJob::run(), which is pure
  // virtual. Two more windows close with it: ~unique_ptr nulls `entries_`
  // before it destroys the elements, and ~Entry destroys the fields a running
  // job reads. Control thread: the patcher's destructor, where the audio
  // thread is already stopped and nothing can arm a new binding.
  //
  // It is also where each slot's share of its clock (issue #707) is released:
  // joining first means no resolve job can still be taking one.
  WaitIdle();
}

void clockBridge::resolveJob::run() {
  owner->RunSlot(*slot);
}

bool clockBridge::NameIs(const char* stored, const char* text, std::size_t length) {
  for (std::size_t i = 0; i < length; ++i) {
    if (stored[i] == '\0' || stored[i] != text[i]) return false;
  }
  return stored[length] == '\0';
}

clockBridge::Handle clockBridge::Bind(const char* name, std::size_t length) {
  if (name == nullptr || length == 0 || length >= NAME_CAPACITY) {
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return 0;
  }

  // A name already bound gives back its handle: objects naming the same clock
  // share one slot, which is what makes CAPACITY a bound on clock *names*
  // rather than on objects. A BOUND slot's name never changes again, so this
  // read needs nothing but the acquire that saw the state.
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    if (entries_[i].state.load(std::memory_order_acquire) != STATE_BOUND) continue;
    if (NameIs(entries_[i].name, name, length)) return (Handle)(i + 1);
  }

  // One CAS attempt per slot, CAPACITY slots: a bounded, lock-free walk. A
  // failed CAS means another thread just claimed that slot — skip it rather
  // than retry, so no handler ever spins here.
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    Entry& e = entries_[i];
    if (e.state.load(std::memory_order_relaxed) != STATE_FREE) continue;
    std::uint32_t expected = STATE_FREE;
    if (!e.state.compare_exchange_strong(expected, STATE_CLAIMED, std::memory_order_acquire,
                                         std::memory_order_relaxed)) {
      continue;
    }

    // The slot is ours; nothing else reads or writes its name while it is
    // CLAIMED. A bounded memcpy into storage that already exists — no
    // allocation, which is what lets this run on the audio thread.
    std::memcpy(e.name, name, length);
    e.name[length] = '\0';

    // Publish. The release pairs with the acquire load in the job and in every
    // reader, which is what makes the name above visible to them.
    e.state.store(STATE_BOUND, std::memory_order_release);
    ArmResolve(i);
    return (Handle)(i + 1);
  }

  dropped_.fetch_add(1, std::memory_order_relaxed);
  return 0;
}

void clockBridge::ArmResolve(std::size_t index) {
  Entry& e = entries_[index];
  // A job already on the pool will read the (unchanging) name anyway, so a
  // second push would only duplicate work. isQueued is the same guard
  // clockManager::update uses before enqueuing its own delete job from the
  // audio callback.
  if (e.job.isQueued()) return;
  INTERNAL::Global().addSlowJob(&e.job);
  // addSlowJob is a no-op on a pool that was never started or has been shut
  // down, and drops on a full background ring. Nothing has to be undone here
  // — the binding simply stays unresolved and Poll tries again.
}

void clockBridge::RunSlot(Entry& e) {
  if (e.state.load(std::memory_order_acquire) != STATE_BOUND) return;
  if (e.clock.load(std::memory_order_acquire) != nullptr) return;

  // The one thing that needs the clock manager's mutex, done on the one thread
  // that may take it. `name` is stable — a BOUND slot never rewrites it — so
  // constructing the std::string the lookup wants is safe here even though it
  // allocates: this is the background pool, not a message handler.
  //
  // What comes back is a share of the clock's lifetime, not a borrowed pointer
  // (issue #707): keeping it is what lets a binding that is never released
  // survive a destroyClock. Taking the share is also the one refcount operation
  // in this file, and it happens here — on the pool — never on a read path.
  std::shared_ptr<CLOCK::domainClock> found = CLOCK::Manager().lookup(std::string(e.name));
  if (found == nullptr) return; // unknown (or not created yet) — Poll retries

  // Both written before the clock pointer, so anyone who sees a non-null clock
  // also sees the baseline that goes with it — and the share that keeps it alive.
  e.resolveBeat.store(found->beatPosition(), std::memory_order_relaxed);
  e.owned = std::move(found);
  e.clock.store(e.owned.get(), std::memory_order_release);
}

bool clockBridge::Beat(Handle handle, double& beat) const {
  if (handle == 0 || handle > CAPACITY) return false;
  const CLOCK::domainClock* clock = entries_[handle - 1].clock.load(std::memory_order_acquire);
  if (clock == nullptr) return false;
  beat = clock->beatPosition();
  return true;
}

bool clockBridge::Tempo(Handle handle, float& bpm) const {
  if (handle == 0 || handle > CAPACITY) return false;
  const CLOCK::domainClock* clock = entries_[handle - 1].clock.load(std::memory_order_acquire);
  if (clock == nullptr) return false;
  bpm = clock->currentTempo();
  return true;
}

bool clockBridge::RequestTempo(Handle handle, float bpm, float rampSeconds) {
  if (handle == 0 || handle > CAPACITY) return false;
  CLOCK::domainClock* clock = entries_[handle - 1].clock.load(std::memory_order_acquire);
  if (clock == nullptr) return false;
  // Three atomic stores into the clock's request slot; the clock consumes them
  // on its next `update`. No lock, no allocation — which is what makes this the
  // route a message handler that turns out to be on the audio callback takes.
  clock->requestTempo(bpm, rampSeconds);
  return true;
}

double clockBridge::ResolveBeat(Handle handle) const {
  if (handle == 0 || handle > CAPACITY) return 0.0;
  const Entry& e = entries_[handle - 1];
  if (e.clock.load(std::memory_order_acquire) == nullptr) return 0.0;
  return e.resolveBeat.load(std::memory_order_relaxed);
}

bool clockBridge::Resolved(Handle handle) const {
  if (handle == 0 || handle > CAPACITY) return false;
  return entries_[handle - 1].clock.load(std::memory_order_acquire) != nullptr;
}

const char* clockBridge::NameOf(Handle handle) const {
  if (handle == 0 || handle > CAPACITY) return "";
  if (entries_[handle - 1].state.load(std::memory_order_acquire) != STATE_BOUND) return "";
  return entries_[handle - 1].name;
}

void clockBridge::Poll(std::uint64_t block) {
  // Rate-limited so a name that will never exist costs one bounded walk every
  // RESOLVE_INTERVAL_BLOCKS instead of one every block. Audio thread only, so
  // the counter needs no synchronisation.
  if (block < nextPollBlock_) return;
  nextPollBlock_ = block + RESOLVE_INTERVAL_BLOCKS;

  for (std::size_t i = 0; i < CAPACITY; ++i) {
    Entry& e = entries_[i];
    if (e.state.load(std::memory_order_acquire) != STATE_BOUND) continue;
    if (e.clock.load(std::memory_order_acquire) != nullptr) continue;
    ArmResolve(i);
  }
}

std::size_t clockBridge::BoundCount() const {
  std::size_t count = 0;
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    if (entries_[i].state.load(std::memory_order_acquire) == STATE_BOUND) ++count;
  }
  return count;
}

std::uint64_t clockBridge::Dropped() const {
  return dropped_.load(std::memory_order_relaxed);
}

void clockBridge::WaitIdle() {
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    entries_[i].job.join();
  }
}
