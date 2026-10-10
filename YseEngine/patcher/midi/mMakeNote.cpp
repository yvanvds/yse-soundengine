// `.makenote` (issue #538). See mMakeNote.h for the design; this file is the
// grammar, the pending set and the release.
// No platform guard, deliberately — see the header.
#include "mMakeNote.h"

#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include "../time/timeValue.h"

#include <cstddef>
#include <cstdint>

using namespace YSE::PATCHER;

#define className mMakeNote

namespace {

  // A float duration as the milliseconds it means. Negatives and NaN become 0 —
  // the shortest note there is, which the scheduler's one-block floor then turns
  // into "released next block" — and anything past the int range saturates
  // rather than being cast, since casting a float outside that range is
  // undefined behaviour. `.pipe`, `.delay`, `.mtr` and `.seq` decide the same
  // question the same way.
  int MillisFromFloat(float value) {
    // Written as a failed `>` rather than `<=` so a NaN takes this branch too.
    if (!(value > 0.f)) return 0;
    if (value >= 2147483647.f) return 2147483647;
    return (int)value;
  }

  // The bounds of one whitespace-separated token, found in place: a substr here
  // would allocate on whichever thread the message arrived on, and that is
  // routinely the audio callback.
  bool NextToken(const std::string& text, std::size_t from, std::size_t& begin, std::size_t& end) {
    begin = from;
    while (begin < text.size() && IsSelectorSeparator(text[begin]))
      begin++;
    end = begin;
    while (end < text.size() && !IsSelectorSeparator(text[end]))
      end++;
    return end > begin;
  }

  constexpr char kPitchInletDoc[] =
      "A pitch plays a note with the stored velocity and duration. A list \"pitch velocity\" sets "
      "the velocity first. \"stop\" releases every sounding note; \"clear\" forgets them.";

  constexpr char kVelocityInletDoc[] =
      "Velocity for the notes that follow; sends nothing. Clamped to 0-127. With 0, the pair "
      "passes through and no release is scheduled.";

  constexpr char kDurationInletDoc[] =
      "Duration in milliseconds for the notes that follow; sends nothing. A negative value counts "
      "as 0. Note values and ticks are not supported.";

} // namespace

CONSTRUCT() {
  ADD_IN_0;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_IN_2;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  // Two int outlets rather than one list: this is the shape every note sender
  // downstream takes — `.noteon` and `.xnoteout` have a pitch inlet and a
  // velocity inlet — so the pair is wired across rather than packed and
  // unpacked.
  ADD_OUT_INT;
  ADD_OUT_INT;

  ADD_PARAM(velocity);
  ADD_PARAM(duration);
  velocity = DEFAULT_VELOCITY;
  duration = DEFAULT_DURATION;

  ADD_DESCRIPTION(
      "Plays a note: sends a pitch and velocity now, and the same pitch with velocity 0 "
      "after a duration in milliseconds.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(0, "pitch", kPitchInletDoc, "int, float, list, 'stop', 'clear'");
  INLET_DOC(1, "velocity", kVelocityInletDoc, "0-127");
  INLET_DOC(2, "duration", kDurationInletDoc, "0+ ms");
  OUTLET_DOC(0, "pitch", "The pitch, when the note plays and again when it is released. Sent last.",
             "int");
  OUTLET_DOC(1, "velocity", "The velocity when the note plays, 0 when it is released.", "0-127");
  PARAM_DOC("velocity", "0",
            "Starting velocity. With 0, notes pass through and no release is scheduled, so set "
            "it or send a velocity before playing.",
            "0-127");
  PARAM_DOC("duration", "0",
            "Starting duration in milliseconds. 0 releases each note on the next audio block.",
            "0+ ms");
}

int mMakeNote::Velocity() const {
  const Int v = velocity.load();
  if (v < 0) return 0;
  return v > MAX_VELOCITY ? MAX_VELOCITY : (int)v;
}

int mMakeNote::Duration() const {
  const Int ms = duration.load();
  return ms > 0 ? (int)ms : 0;
}

void mMakeNote::Emit(int pitch, int noteVelocity, YSE::THREAD thread) {
  // Right to left, and load-bearing: every note sender downstream takes its
  // velocity on a cold inlet and its pitch on a hot one, so a pitch sent first
  // would be sent with the previous note's velocity.
  outputs[1].SendInt(noteVelocity, thread);
  outputs[0].SendInt(pitch, thread);
}

void mMakeNote::Play(int pitch, YSE::THREAD thread) {
  const int noteVelocity = Velocity();

  // A note-on with velocity 0 *is* a release in MIDI. Max would queue a second,
  // identical release behind it; spending a slot on that would let a patch that
  // plays releases through a `.makenote` exhaust the pending set with notes
  // that were never sounding.
  if (noteVelocity == 0) {
    Emit(pitch, 0, thread);
    return;
  }

  messageScheduler* scheduler = Scheduler();
  if (scheduler == nullptr) {
    // A standalone object has no patcher and so no clock: "a duration later"
    // has no referent, and the alternatives are now or never. Never would leave
    // a note hanging, which is the one thing this object must not do, so the
    // release follows the attack at once. `.pipe`'s answer to the same dead end,
    // and the one that keeps a standalone object testable rather than a source
    // of stuck notes.
    Emit(pitch, noteVelocity, thread);
    Emit(pitch, 0, thread);
    return;
  }

  // Claim a slot: a bounded walk with at most one CAS attempt per slot — no
  // lock, no allocation, no syscall — so a note may be played from whichever
  // thread is dispatching, the audio callback included.
  std::size_t index = CAPACITY;
  std::uint64_t claimed = 0;
  for (std::size_t i = 0; i < CAPACITY; i++) {
    std::uint64_t state = slots[i].stateGen.load(std::memory_order_acquire);
    if ((state & STATE_MASK) != STATE_FREE) continue;
    const std::uint64_t next = (((state >> 2) + 1) << 2) | STATE_CLAIMED;
    if (slots[i].stateGen.compare_exchange_strong(state, next, std::memory_order_acq_rel,
                                                  std::memory_order_relaxed)) {
      index = i;
      claimed = next;
      break;
    }
  }
  if (index == CAPACITY) {
    // The pending set is full, so this note cannot be released. The *whole*
    // note is refused rather than just its release: an attack with no release
    // is the hanging note this object exists to prevent. Counted, not logged —
    // see the header.
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  Slot& slot = slots[index];
  // Written under CLAIMED, which no other thread will move: the pitch is never
  // touched concurrently.
  slot.pitch = pitch;
  slot.seq = nextSeq.fetch_add(1, std::memory_order_relaxed);
  slot.handle.store(0, std::memory_order_relaxed);

  // The attack goes out here, while the slot is still CLAIMED and so before the
  // release can possibly be delivered, and the order is load-bearing. Published
  // first, the release could in principle be delivered by an audio thread that
  // ran a whole block while this one was preempted — a note-off ahead of its
  // note-on, which is the stuck note in its purest form. A `stop` re-entered
  // from this very send misses the slot for the same reason, which costs one
  // note a release it will get on schedule anyway.
  Emit(pitch, noteVelocity, thread);

  const std::uint64_t generation = claimed >> 2;
  const int tag = (int)(((generation & TAG_GEN_MASK) << TAG_INDEX_BITS) | (std::uint64_t)index);
  const std::uint64_t armed = (generation << 2) | STATE_ARMED;

  // Published *before* the message is armed, and that order is load-bearing
  // too: the scheduler's deadline floor is one block, so the very next
  // Calculate on the audio thread may deliver this message while this thread is
  // still here. A slot published afterwards would fail that delivery's CAS and
  // the note would sound for ever.
  pendingCount.fetch_add(1, std::memory_order_relaxed);
  slot.stateGen.store(armed, std::memory_order_release);

  // Only a bang is armed: the pitch is the slot's, so the scheduler carries
  // nothing but the deadline and the tag that names the slot. That is also what
  // lets `stop` find the note again, which a payload handed to the scheduler
  // could not be.
  const messageScheduler::Handle handle = scheduler->ScheduleBang(this, tag, Duration());
  if (handle == 0) {
    // The patcher-wide pending set is full while this object's is not — and the
    // attack has already gone out, so the release cannot be dropped with the
    // note the way a full slot table drops it. It goes out now instead: a note
    // shorter than it was asked to be rather than one that never ends. Unless a
    // `stop` has taken the slot in the meantime, in which case the release has
    // already been sent and the slot is no longer ours.
    std::uint64_t expected = armed;
    if (slot.stateGen.compare_exchange_strong(expected, (generation << 2) | STATE_FIRING,
                                              std::memory_order_acq_rel,
                                              std::memory_order_relaxed)) {
      pendingCount.fetch_sub(1, std::memory_order_relaxed);
      dropped.fetch_add(1, std::memory_order_relaxed);
      Emit(slot.pitch, 0, thread);
      slot.stateGen.store((generation << 2) | STATE_FREE, std::memory_order_release);
    }
    return;
  }
  // Best effort by construction — see the Slot::handle comment.
  slot.handle.store(handle, std::memory_order_relaxed);
}

void mMakeNote::Release(bool emit, YSE::THREAD thread) {
  // Take every armed slot first, in one bounded walk, and only then emit: a
  // send runs the whole subgraph behind the outlets, which may come back into
  // this object, and a walk that emitted as it went could meet a slot armed by
  // its own output.
  std::size_t taken[CAPACITY];
  std::uint64_t order[CAPACITY];
  std::size_t count = 0;

  messageScheduler* scheduler = Scheduler();

  for (std::size_t i = 0; i < CAPACITY; i++) {
    std::uint64_t state = slots[i].stateGen.load(std::memory_order_acquire);
    if ((state & STATE_MASK) != STATE_ARMED) continue;
    const std::uint64_t firing = (state & ~STATE_MASK) | STATE_FIRING;
    if (!slots[i].stateGen.compare_exchange_strong(state, firing, std::memory_order_acq_rel,
                                                   std::memory_order_relaxed)) {
      // A delivery won the same slot; that note is being released through the
      // scheduler instead, which is the honest resolution of the race.
      continue;
    }
    pendingCount.fetch_sub(1, std::memory_order_relaxed);

    // Hand the patcher-wide budget back early. A cancel that loses its race
    // leaves a message that comes due, finds the slot gone, and sends nothing.
    const messageScheduler::Handle handle = slots[i].handle.exchange(0, std::memory_order_relaxed);
    if (scheduler != nullptr && handle != 0) scheduler->Cancel(handle);

    // Insertion sort by play ticket, in place, over a stack array of at most
    // CAPACITY entries: a `stop` releases in the order the notes were played,
    // which is the order they would have been released in had nobody
    // interrupted, and the slot table is not in that order.
    std::size_t at = count;
    while (at > 0 && order[at - 1] > slots[i].seq) {
      order[at] = order[at - 1];
      taken[at] = taken[at - 1];
      at--;
    }
    order[at] = slots[i].seq;
    taken[at] = i;
    count++;
  }

  for (std::size_t n = 0; n < count; n++) {
    Slot& slot = slots[taken[n]];
    if (emit) Emit(slot.pitch, 0, thread);
    // Freed only once the send has finished, so nothing can overwrite the pitch
    // the send is reading.
    slot.stateGen.store(slot.stateGen.load(std::memory_order_relaxed) & ~STATE_MASK,
                        std::memory_order_release);
  }
}

void mMakeNote::Teardown(YSE::THREAD thread) {
  // The patcher's stop pass (issue #758), and literally a `stop`: every
  // sounding note released in play order, down cords that are still wired
  // because the pass runs before any of them is taken apart. Bounded,
  // allocation-free and lock-free, exactly as the message is.
  Release(true, thread);
}

void mMakeNote::DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) {
  // A note's duration has elapsed. The scheduler wraps this in a fresh
  // messageEventScope (#628), so the release and everything it goes on to cause
  // is one logical event — the note-on's causal chain resumed rather than
  // replaced, which is the whole reason this object does not use TimerThread.
  const std::size_t index = (std::size_t)(msg.tag & TAG_INDEX_MASK);
  if (index >= CAPACITY) return;
  const std::uint64_t generation = ((std::uint64_t)msg.tag >> TAG_INDEX_BITS) & TAG_GEN_MASK;

  Slot& slot = slots[index];
  std::uint64_t state = slot.stateGen.load(std::memory_order_acquire);
  // Not armed, or armed for a *later* note than the one this message names:
  // `stop` or `clear` took this one, and the slot has since been reused. The
  // generation is what keeps a recycled slot from impersonating it.
  if ((state & STATE_MASK) != STATE_ARMED) return;
  if (((state >> 2) & TAG_GEN_MASK) != generation) return;

  const std::uint64_t firing = (state & ~STATE_MASK) | STATE_FIRING;
  if (!slot.stateGen.compare_exchange_strong(state, firing, std::memory_order_acq_rel,
                                             std::memory_order_relaxed)) {
    return;
  }
  pendingCount.fetch_sub(1, std::memory_order_relaxed);
  slot.handle.store(0, std::memory_order_relaxed);

  Emit(slot.pitch, 0, thread);
  slot.stateGen.store(firing & ~STATE_MASK, std::memory_order_release);
}

INT_IN(IntIn) {
  if (inlet == 0) {
    Play(value, thread);
    return;
  }
  // Max's middle and right inlets: the velocity and the duration for notes
  // played *after* them. Notes already sounding are not revoiced or retimed,
  // which falls out of not touching their slots.
  if (inlet == 1) {
    velocity = value;
    return;
  }
  duration = value;
}

FLOAT_IN(FloatIn) {
  if (inlet == 0) {
    // Max truncates a float pitch to an int. ExprToInt rather than a cast: a
    // value outside the int range is undefined behaviour to cast.
    Play(ExprToInt(value), thread);
    return;
  }
  if (inlet == 1) {
    velocity = ExprToInt(value);
    return;
  }
  duration = MillisFromFloat(value);
}

LIST_IN(ListIn) {
  std::size_t begin = 0;
  std::size_t end = 0;
  if (!NextToken(value, 0, begin, end)) return;
  const std::size_t length = end - begin;

  if (inlet == 0) {
    // Max's two commands, left inlet only. Both are words rather than pitches,
    // as they are in Max, so neither can be played.
    if (length == 4 && value.compare(begin, length, "stop", 4) == 0) {
      Release(true, thread);
      return;
    }
    if (length == 5 && value.compare(begin, length, "clear", 5) == 0) {
      Release(false, thread);
      return;
    }

    float pitch = 0.f;
    if (!ReadNumericToken(value.c_str() + begin, length, pitch)) return;

    // Max's list distributes across the inlets right to left, so a second
    // element is the velocity and it is stored *before* the pitch plays —
    // exactly as if it had arrived at the middle inlet first. Further elements
    // are ignored: Max's third is a channel, and there is no channel outlet
    // here. A single-token numeric list is simply a pitch, which is how a `.m
    // 60` reaches this inlet.
    std::size_t secondBegin = 0;
    std::size_t secondEnd = 0;
    if (NextToken(value, end, secondBegin, secondEnd)) {
      float number = 0.f;
      if (ReadNumericToken(value.c_str() + secondBegin, secondEnd - secondBegin, number)) {
        velocity = ExprToInt(number);
      }
    }

    Play(ExprToInt(pitch), thread);
    return;
  }

  if (inlet == 1) {
    float number = 0.f;
    if (!ReadNumericToken(value.c_str() + begin, length, number)) return;
    velocity = ExprToInt(number);
    return;
  }

  // The duration inlet. Max's tempo-relative syntax is read *first* and refused
  // as a whole, because its tick spelling starts with a number: `1440 ticks`
  // taken for its leading token would silently become 1440 ms, which is the
  // mistake `.clocker` made before #725. This object has no clock to measure a
  // beat against, so the honest answer is to leave the duration where it was.
  double beats = 0.0;
  if (ReadBeatTime(value.c_str() + begin, value.size() - begin, beats)) return;

  float number = 0.f;
  if (!ReadNumericToken(value.c_str() + begin, length, number)) return;
  duration = MillisFromFloat(number);
}

#undef className
