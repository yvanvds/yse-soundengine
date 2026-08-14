// The array family's JSON-parse bridge (issue #797) — dictParser's slot state
// machine (#771) over the other store type. See arrayParser.h for what differs
// and dictParser.h for the full design argument; this file is the state
// machine and one background-pool parse.
#include "arrayParser.h"

#include "../../internal/global.h"

#include <cstring>
#include <thread>

using namespace YSE::PATCHER;

arrayParser& YSE::PATCHER::ArrayParser() {
  static arrayParser p;
  return p;
}

arrayParser::arrayParser() : entries_(new Entry[CAPACITY]) {
  // Control thread, once: everything a submit needs afterwards already exists.
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    entries_[i].job.owner = this;
    entries_[i].job.slot = &entries_[i];
  }
}

arrayParser::~arrayParser() {
  // Join every parse job here, while this object is still whole, rather than
  // leaving it to ~threadPoolJob: that join runs in the *base* destructor —
  // after ~parseJob has already reset the vtable — so a worker picking the
  // job up inside that window would call a pure virtual. dictParser's
  // destructor closes the same window.
  WaitIdle();
}

void arrayParser::parseJob::run() {
  owner->RunSlot(*slot);
}

arrayParser::Entry* arrayParser::Slot(Handle handle) {
  if (handle == 0 || handle > CAPACITY) return nullptr;
  return &entries_[handle - 1];
}

const arrayParser::Entry* arrayParser::Slot(Handle handle) const {
  if (handle == 0 || handle > CAPACITY) return nullptr;
  return &entries_[handle - 1];
}

arrayParser::Handle arrayParser::Claim() {
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
    // The staging store, built the first time this slot is claimed — the
    // control thread, since the only caller is an object constructor. A
    // re-claimed slot reuses what its previous owner paid for.
    if (e.staged == nullptr) e.staged = std::make_unique<arrayStore>();
    return static_cast<Handle>(i + 1);
  }

  dropped_.fetch_add(1, std::memory_order_relaxed);
  return 0;
}

void arrayParser::Release(Handle handle) {
  Entry* e = Slot(handle);
  if (e == nullptr) return;

  for (;;) {
    const std::uint32_t current = e->state.load(std::memory_order_acquire);
    if (current == STATE_FREE) return;
    if (current == STATE_CLAIMED || current == STATE_PARSING) {
      // The two states in which another thread is writing this slot — a
      // Submit mid-copy, or the worker mid-parse. Waiting them out is bounded
      // by one document, and it cannot deadlock: a reclaimer that is itself
      // on the pool worker cannot also be executing that slot's job —
      // timerBridge::Release's reasoning, as dictParser applies it.
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

bool arrayParser::Submit(Handle handle, const char* text, std::size_t length) {
  Entry* e = Slot(handle);
  if (e == nullptr) return false;
  if (text == nullptr || length > TEXT_CAPACITY - 1) return false;

  // The one writer: winning this CAS is what licenses the copy below. A slot
  // that is anywhere else in its lifecycle — parsing, or holding a result —
  // refuses rather than queueing: a document is a payload, and remembering
  // the ask without the bytes would re-parse the old text as the new one.
  std::uint32_t expected = STATE_IDLE;
  if (!e->state.compare_exchange_strong(expected, STATE_CLAIMED, std::memory_order_acq_rel,
                                        std::memory_order_relaxed)) {
    return false;
  }

  std::memcpy(e->text, text, length);
  e->text[length] = '\0';
  e->textLength = length;

  // The publish: everything the job reads is written above this store.
  e->state.store(STATE_ARMED, std::memory_order_release);
  Arm(*e);
  return true;
}

bool arrayParser::HasResult(Handle handle) {
  Entry* e = Slot(handle);
  if (e == nullptr) return false;

  const std::uint32_t current = e->state.load(std::memory_order_acquire);
  if (current == STATE_ARMED) {
    // Recovery: addSlowJob is a no-op on a pool that was never started or has
    // been shut down, and drops on a full background ring, so an arm can
    // silently not have landed. Re-arming is one atomic load in the common
    // case (the job *is* queued) — dictParser::HasResult's recovery.
    Arm(*e);
    return false;
  }
  return current == STATE_READY;
}

bool arrayParser::Consume(Handle handle, arrayStore& into, bool& ok) {
  Entry* e = Slot(handle);
  if (e == nullptr) return false;

  if (e->state.load(std::memory_order_acquire) != STATE_READY) return false;

  // The acquire above pairs with the job's release store into `state`, so
  // every element the parse staged is visible here. Nothing can be writing
  // the slot in READY — the caller is its owner and the job is done — which
  // is what makes these plain copies safe rather than merely unlikely to
  // tear.
  ok = e->ok;
  if (ok) {
    // Replace the caller's store whole: bounded assigns into elements both
    // stores reserved at construction, no allocation. Elements above `count`
    // are dead by contract — `count` is the one authority, gArray's rule.
    const arrayStore& staged = *e->staged;
    into.count = staged.count;
    for (std::size_t i = 0; i < staged.count; i++) {
      into.elements[i].assign(staged.elements[i]);
    }
  }
  e->state.store(STATE_IDLE, std::memory_order_release);
  return true;
}

void arrayParser::Arm(Entry& e) {
  // A job already on the pool will read the slot anyway, so a second push
  // would only duplicate work. isQueued is the guard clockBridge, dictParser
  // and midiPortScanner all use before enqueuing from the audio callback.
  if (e.job.isQueued()) return;
  INTERNAL::Global().addSlowJob(&e.job);
}

void arrayParser::RunSlot(Entry& e) {
  std::uint32_t expected = STATE_ARMED;
  if (!e.state.compare_exchange_strong(expected, STATE_PARSING, std::memory_order_acq_rel,
                                       std::memory_order_relaxed)) {
    // Released, or already consumed and idle again. Nothing to do — and, more
    // to the point, nothing this job may write.
    return;
  }

  // Background pool: the one place in the array family nlohmann's parser runs
  // on a live document. Non-throwing on purpose — a malformed document is an
  // outcome to report, not an exception to unwind through the pool worker.
  const nlohmann::json parsed =
      nlohmann::json::parse(e.text, e.text + e.textLength, nullptr, false);

  // An array is a JSON array; anything else — malformed text, a bare number,
  // an object, an `array <name>` reference wired here by mistake — is a
  // failed parse, not an empty array. What does parse is read by
  // ArrayFromJson, the type's one reader, so element capacities, nested
  // documents, null and the element bound all follow gArray's stated rules.
  e.ok = parsed.is_array();
  if (e.ok) {
    ArrayFromJson(parsed, *e.staged);
  }

  // Cannot fail: PARSING is a state Release waits out rather than claiming,
  // so no other thread can have moved the slot out from under us.
  expected = STATE_PARSING;
  e.state.compare_exchange_strong(expected, STATE_READY, std::memory_order_acq_rel,
                                  std::memory_order_relaxed);
}

bool arrayParser::Busy(Handle handle) const {
  const Entry* e = Slot(handle);
  if (e == nullptr) return false;
  const std::uint32_t current = e->state.load(std::memory_order_acquire);
  return current == STATE_CLAIMED || current == STATE_ARMED || current == STATE_PARSING ||
         current == STATE_READY;
}

std::uint64_t arrayParser::Dropped() const {
  return dropped_.load(std::memory_order_relaxed);
}

void arrayParser::WaitIdle() {
  // Arm first: a slot whose push never reached the pool would otherwise be
  // joined instantly and still be waiting for a parse that nobody will run.
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    if (entries_[i].state.load(std::memory_order_acquire) == STATE_ARMED) Arm(entries_[i]);
  }
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    entries_[i].job.join();
  }
}
