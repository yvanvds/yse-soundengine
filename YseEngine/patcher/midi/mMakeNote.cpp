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
      "The pitch, and the trigger. An int or a float arriving here is sent straight out the left "
      "outlet with the stored velocity out the right one, and the same pitch is sent again with "
      "velocity 0 once the duration has elapsed — Max's 'a note-on message paired with a velocity "
      "value followed by a note-off message after a specified amount of time'. A float pitch is "
      "truncated to an int, as it is in Max. A list is Max's inlet distribution written on one "
      "cord: the first element is the pitch and a second element sets the velocity first, so "
      "'60 100' plays pitch 60 at velocity 100 and leaves the velocity at 100 for the notes after "
      "it. Further elements are ignored, there being no channel outlet here. A single-token "
      "numeric list is the number it spells, so a .m 60 reaches this inlet as a pitch. The message "
      "'stop' releases every note this object is holding immediately, in the order they were "
      "played, and 'clear' forgets them without sending anything — Max's two commands, and words "
      "rather than pitches. There is no bang method, as Max's makenote has none.";

  constexpr char kVelocityInletDoc[] =
      "Sets the velocity for pitches arriving *after* it — Max's middle inlet. Notes already "
      "sounding are unaffected, their release always being velocity 0. Ints, floats and a list "
      "whose leading token is a number all set it; a float is truncated. Values outside MIDI's "
      "0-127 are clamped when the velocity is read, because this value decides whether a release "
      "is scheduled at all. A velocity of 0 is a release in MIDI, so the pair goes out unchanged "
      "and nothing is scheduled behind it.";

  constexpr char kDurationInletDoc[] =
      "Sets how long notes played *after* it will sound, in milliseconds — Max's right inlet. "
      "Notes already sounding keep the duration they were played with, which is what lets a patch "
      "shorten the duration under a long note without cutting it off. Ints, floats and a list "
      "whose leading token is a number all set it; a negative or NaN duration counts as 0, which "
      "still releases on the next audio block rather than inside the same dispatch. Max's "
      "tempo-relative time syntax is not read here — this object has no clock to measure a beat "
      "against — and a note value ('4nd') or tick count ('1440 ticks') is refused outright rather "
      "than misread as the milliseconds it is not, leaving the duration where it was.";

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
      "Plays a note: a pitch and a velocity out now, and the same pitch with velocity 0 a duration "
      "later. This is the object that lets a patch play a note at all without hanging one. Every "
      "MIDI sender downstream of it — .noteon, .midiformat, the .x*out family — is a stateless "
      "formatter that sends what it is handed and remembers nothing, so a patch that plays a note "
      "has to send its own release, and forgetting one is the single most common way to strand a "
      "synth. .midiflush is the cure for that after the fact; this is the prevention, because the "
      "release is scheduled at the same instant as the attack. A pitch in the left inlet plays; "
      "the middle inlet sets the velocity and the right one the duration in milliseconds, both for "
      "notes played after them, so a note already sounding is never retimed or revoiced under a "
      "patch's feet. A list in the left inlet is Max's inlet distribution on one cord: '60 100' is "
      "pitch 60 at velocity 100. The velocity goes out the right outlet before the pitch goes out "
      "the left, Max's outlets firing right to left, which here is not cosmetic — every sender "
      "downstream takes pitch on a hot inlet and velocity on a cold one, so a pitch sent first "
      "would carry the previous note's velocity. 'stop' releases everything sounding immediately "
      "in the order it was played and 'clear' forgets it without sending anything, which are Max's "
      "two commands. The wait runs on the patcher's deferred-message scheduler (issue #628) rather "
      "than on the timer thread behind .metro, so playing a note allocates nothing and takes no "
      "lock, and the release arrives inside the patcher's own dispatch as the continuation of the "
      "note-on rather than as an unrelated stimulus. That clock is the block counter, so it stops "
      "when the engine does and a paused patch holds its pending releases where they stand instead "
      "of firing them in a burst on resume, and its resolution is one audio block: a duration of 0 "
      "releases on the next block rather than inside the same dispatch, which is what keeps a "
      ".makenote wired back into its own inlet a fast trill instead of a stack overflow. At most "
      "64 notes may sound at once per object, pre-allocated with it, and beyond that sits the "
      "patcher-wide limit of 128 pending messages shared with .pipe, .delay, .qlist, .mtr and "
      ".seq. The two overflows are answered differently and both are counted rather than logged, "
      "the refusing thread being routinely the audio callback: a full pending set refuses the "
      "whole note, attack included, because an attack this object has already proved it cannot "
      "release is the hanging note it exists to prevent, while a full patcher-wide budget is only "
      "discovered after the attack has gone out and so releases the note immediately instead — a "
      "note shorter than it was asked to be rather than one that never ends. Unlike Max there are "
      "always three inlets and two outlets: Max grows a channel pair when given three creation "
      "arguments, and ports here are built before the arguments are parsed, but every sender "
      "downstream already takes a channel of its own. Max's repeatmode is not implemented — this "
      "object is poly, Max's default, so a repeated pitch is a second note with its own deadline — "
      "and milliseconds are the only unit, a note value or tick count in the duration inlet being "
      "refused rather than misread as milliseconds. A velocity of 0 is already a release in MIDI, "
      "so it passes through and schedules nothing. A patcher cleared or destroyed while notes are "
      "sounding releases them, and so does deleting the object on its own: the patcher stops every "
      "object before it unwires any of them, and this object's stop is 'stop' (issue #758). "
      "Calculate() does nothing and no message path allocates, locks or blocks.");
  ADD_CATEGORY(pCategory::MIDI);

  INLET_DOC(0, "pitch", kPitchInletDoc, "int, float, list, 'stop', 'clear'");
  INLET_DOC(1, "velocity", kVelocityInletDoc, "0-127");
  INLET_DOC(2, "duration", kDurationInletDoc, "0+ ms");
  OUTLET_DOC(0, "pitch",
             "The note's pitch, sent once when the note is played and again when it is released. "
             "The same value both times, which is what lets a note-off find the note it belongs "
             "to. Sent after the velocity, Max's outlets firing right to left.",
             "int");
  OUTLET_DOC(1, "velocity",
             "The note's velocity: the stored velocity when the note is played, and 0 when it is "
             "released. Sent before the pitch so a sender downstream has it in hand by the time "
             "the pitch reaches its hot inlet.",
             "0-127");
  PARAM_DOC("velocity", "0",
            "The initial velocity — Max's first creation argument, whose documented default is 0. "
            "The middle inlet overwrites it afterwards, for notes played after that. Clamped to "
            "MIDI's 0-127 when it is read, and a velocity of 0 sends the pair without scheduling a "
            "release, a note-on with velocity 0 already being a release in MIDI.",
            "0-127");
  PARAM_DOC("duration", "0",
            "The initial note length in milliseconds — Max's second creation argument, whose "
            "documented default is immediate. A duration of 0 is not the same as no note: the "
            "release still comes on the next audio block, as its own event. The right inlet "
            "overwrites it afterwards, for notes played after that. A negative value counts as 0. "
            "Notes already sounding keep the duration they were played with.",
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
