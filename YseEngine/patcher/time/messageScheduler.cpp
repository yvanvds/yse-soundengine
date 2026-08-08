#include "messageScheduler.h"
#include "../../headers/constants.hpp"
#include "../graphState.h"
#include "../inlet.h"
#include "../pObject.h"
#include <cstring>

using namespace YSE::PATCHER;

namespace {
  // Handle layout: low 8 bits hold (slot index + 1) — CAPACITY is 128, so the
  // +1 keeps 0 free to mean "refused" — and the remaining bits hold the slot
  // generation the arm observed. Cancel/Pending rebuild the exact stateGen word
  // the handle refers to, so a stale handle (delivered, cancelled, or the slot
  // since reused at a higher generation) simply fails its comparison.
  constexpr std::uint64_t kIndexMask = 0xff;
  constexpr unsigned int kGenShift = 8;
} // namespace

messageScheduler::messageScheduler(const std::atomic<std::uint64_t>& blockClock)
  : clock_(blockClock) {
  // Pre-size the delivery scratch so no LIST delivery allocates on the audio
  // thread — the same treatment the #225 value queue's scratch gets.
  listScratch_.reserve(TEXT_CAPACITY);
}

std::uint64_t messageScheduler::BlocksForMillis(int delayMs) {
  // Live SAMPLERATE, read at arm time rather than cached at construction — the
  // #637 rule: state derived from the sample rate is derived where it is used.
  // Ceil so a delay is never shortened by quantisation; floor of one block so a
  // deferred message is never delivered inside the dispatch that armed it.
  if (delayMs <= 0) return 1;
  const std::uint64_t samples =
      (std::uint64_t)delayMs * (std::uint64_t)SAMPLERATE / (std::uint64_t)1000;
  const std::uint64_t blocks =
      (samples + (std::uint64_t)STANDARD_BUFFERSIZE - 1) / (std::uint64_t)STANDARD_BUFFERSIZE;
  return blocks < 1 ? 1 : blocks;
}

int messageScheduler::MillisForBlocks(std::uint64_t blocks) {
  // Live SAMPLERATE for the same reason BlocksForMillis reads it live (#637):
  // the rate is negotiated with the device and may change under a running
  // patcher. Truncating rather than ceiling — this measures an interval that
  // has already happened, where rounding up would lengthen every gap by most
  // of a block.
  if (blocks == 0) return 0;
  const std::uint64_t rate = (std::uint64_t)SAMPLERATE;
  if (rate == 0) return 0;
  const std::uint64_t ms = blocks * (std::uint64_t)STANDARD_BUFFERSIZE * (std::uint64_t)1000 / rate;
  return ms > (std::uint64_t)2147483647 ? 2147483647 : (int)ms;
}

messageScheduler::Handle messageScheduler::Arm(pObject* target, int tag, int delayMs,
                                               DEFERRED_KIND kind, int intValue, float floatValue,
                                               const char* text, std::size_t length) {
  if (target == nullptr) return 0;
  if (kind == DEFERRED_KIND::LIST && (text == nullptr || length >= TEXT_CAPACITY)) {
    // Over-long text cannot ride the inline slot. Refuse rather than truncate
    // silently or allocate; counted, not logged, because this thread may be the
    // audio callback.
    dropped_.fetch_add(1, std::memory_order_relaxed);
    return 0;
  }

  // One CAS attempt per slot, CAPACITY slots: a bounded, lock-free walk. A
  // failed CAS means another thread just claimed that slot — skip it rather
  // than retry, so no handler ever spins here.
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    Entry& e = entries_[i];
    std::uint64_t cur = e.stateGen.load(std::memory_order_relaxed);
    if ((cur & STATE_MASK) != STATE_FREE) continue;
    const std::uint64_t gen = (cur >> 2) + 1;
    if (!e.stateGen.compare_exchange_strong(cur, (gen << 2) | STATE_CLAIMED,
                                            std::memory_order_acquire, std::memory_order_relaxed)) {
      continue;
    }

    // The slot is ours; nothing else reads or writes its payload while it is
    // CLAIMED. dueBlock and seq are atomics only because the drain's scan reads
    // them speculatively — see the header.
    e.dueBlock.store(clock_.load(std::memory_order_acquire) + BlocksForMillis(delayMs),
                     std::memory_order_relaxed);
    e.seq.store(nextSeq_.fetch_add(1, std::memory_order_relaxed), std::memory_order_relaxed);
    e.target = target;
    e.targetId = target->GetID();
    e.tag = tag;
    e.kind = kind;
    e.intValue = intValue;
    e.floatValue = floatValue;
    if (kind == DEFERRED_KIND::LIST) {
      std::memcpy(e.text, text, length);
      e.textLength = (std::uint32_t)length;
    } else {
      e.textLength = 0;
    }

    // Publish. The release pairs with the drain's acquire load / CAS, which is
    // what makes every plain payload field above visible to the delivering
    // thread.
    e.stateGen.store((gen << 2) | STATE_ARMED, std::memory_order_release);
    return (gen << kGenShift) | (std::uint64_t)(i + 1);
  }

  dropped_.fetch_add(1, std::memory_order_relaxed);
  return 0;
}

messageScheduler::Handle messageScheduler::ScheduleBang(pObject* target, int tag, int delayMs) {
  return Arm(target, tag, delayMs, DEFERRED_KIND::BANG, 0, 0.f, nullptr, 0);
}

messageScheduler::Handle messageScheduler::ScheduleInt(pObject* target, int tag, int delayMs,
                                                       int value) {
  return Arm(target, tag, delayMs, DEFERRED_KIND::INT, value, 0.f, nullptr, 0);
}

messageScheduler::Handle messageScheduler::ScheduleFloat(pObject* target, int tag, int delayMs,
                                                         float value) {
  return Arm(target, tag, delayMs, DEFERRED_KIND::FLOAT, 0, value, nullptr, 0);
}

messageScheduler::Handle messageScheduler::ScheduleList(pObject* target, int tag, int delayMs,
                                                        const char* text, std::size_t length) {
  return Arm(target, tag, delayMs, DEFERRED_KIND::LIST, 0, 0.f, text, length);
}

bool messageScheduler::Cancel(Handle handle) {
  if (handle == 0) return false;
  const std::size_t index = (std::size_t)(handle & kIndexMask);
  if (index < 1 || index > CAPACITY) return false;
  const std::uint64_t gen = handle >> kGenShift;
  std::uint64_t expected = (gen << 2) | STATE_ARMED;
  // Only the exact armed generation can be cancelled; anything else — already
  // delivered, already cancelled, slot reused — fails and touches nothing.
  return entries_[index - 1].stateGen.compare_exchange_strong(
      expected, (gen << 2) | STATE_FREE, std::memory_order_acq_rel, std::memory_order_relaxed);
}

bool messageScheduler::Pending(Handle handle) const {
  if (handle == 0) return false;
  const std::size_t index = (std::size_t)(handle & kIndexMask);
  if (index < 1 || index > CAPACITY) return false;
  const std::uint64_t gen = handle >> kGenShift;
  return entries_[index - 1].stateGen.load(std::memory_order_acquire) == ((gen << 2) | STATE_ARMED);
}

void messageScheduler::DeliverDue(const GraphState* graph, YSE::THREAD thread) {
  const std::uint64_t now = clock_.load(std::memory_order_acquire);

  // Snapshot the due set first, then deliver — a delivery can arm new messages
  // (a chained deferral) and those must wait for their own deadline, which the
  // one-block floor guarantees anyway; snapshotting keeps the walk bounded.
  struct DueRef {
    std::size_t index;
    std::uint64_t stateGen;
    std::uint64_t seq;
  };
  DueRef due[CAPACITY];
  std::size_t count = 0;

  for (std::size_t i = 0; i < CAPACITY; ++i) {
    const std::uint64_t sg = entries_[i].stateGen.load(std::memory_order_acquire);
    if ((sg & STATE_MASK) != STATE_ARMED) continue;
    if (entries_[i].dueBlock.load(std::memory_order_relaxed) > now) continue;
    const std::uint64_t seq = entries_[i].seq.load(std::memory_order_relaxed);
    // Insertion sort by arm order — at most CAPACITY entries, stack storage,
    // no allocation.
    std::size_t at = count;
    while (at > 0 && due[at - 1].seq > seq) {
      due[at] = due[at - 1];
      --at;
    }
    due[at] = {i, sg, seq};
    ++count;
  }

  for (std::size_t d = 0; d < count; ++d) {
    Entry& e = entries_[due[d].index];
    std::uint64_t expected = due[d].stateGen;
    const std::uint64_t gen = expected & ~STATE_MASK;
    // Claim delivery of exactly the arming we scanned; a concurrent Cancel (or
    // cancel-and-re-arm) since then fails this CAS and the slot is not ours.
    if (!e.stateGen.compare_exchange_strong(expected, gen | STATE_FIRING, std::memory_order_acq_rel,
                                            std::memory_order_relaxed)) {
      continue;
    }

    // FIRING: arms skip non-FREE slots and Cancel needs ARMED, so the payload
    // is stable — copy it out, then free the slot before dispatching so the
    // delivery itself may arm without finding the table artificially full.
    pObject* target = e.target;
    const unsigned int targetId = e.targetId;
    const int tag = e.tag;
    const DEFERRED_KIND kind = e.kind;
    const int intValue = e.intValue;
    const float floatValue = e.floatValue;
    if (kind == DEFERRED_KIND::LIST) {
      // assign() into the reserved scratch reuses its buffer — no allocation.
      listScratch_.assign(e.text, e.textLength);
    } else {
      listScratch_.clear();
    }
    e.stateGen.store(gen | STATE_FREE, std::memory_order_release);

    if (graph == nullptr) continue;

    // Re-resolve the target against the pinned snapshot: pointer *and*
    // construction-time id must match, so a deleted or replaced object's
    // pending message is dropped, and a recycled allocation at the same
    // address cannot impersonate it. Only the snapshot's own (live) pointers
    // are ever dereferenced.
    for (pObject* obj : graph->objects) {
      if (obj != target || obj->GetID() != targetId) continue;
      // The dispatch frame the deferral exists for: everything this delivery
      // causes shares one fresh logical-event id (#471), exactly as if a
      // scheduler tick were the stimulus.
      messageEventScope frame;
      const deferredMessage msg{tag, kind, intValue, floatValue, listScratch_};
      obj->DeliverDeferred(msg, thread);
      break;
    }
  }
}

std::size_t messageScheduler::PendingCount() const {
  std::size_t count = 0;
  for (std::size_t i = 0; i < CAPACITY; ++i) {
    if ((entries_[i].stateGen.load(std::memory_order_acquire) & STATE_MASK) == STATE_ARMED) {
      ++count;
    }
  }
  return count;
}

std::uint64_t messageScheduler::Dropped() const {
  return dropped_.load(std::memory_order_relaxed);
}
