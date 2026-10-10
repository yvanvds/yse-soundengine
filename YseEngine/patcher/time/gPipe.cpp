#include "gPipe.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include "timeValue.h"
#include <cstddef>
#include <cstdint>

using namespace YSE::PATCHER;

#define className gPipe

namespace {

  // A float delay time as the milliseconds it means. Negatives and NaN become
  // 0 — the shortest wait there is, which the scheduler's one-block floor then
  // turns into "next block" — and anything past the int range saturates rather
  // than being cast, since casting a float outside that range is undefined
  // behaviour. `.delay`, `.mtr` and `.seq` decide the same question the same
  // way.
  int MillisFromFloat(float value) {
    // Written as a failed `>` rather than `<=` so a NaN takes this branch too.
    if (!(value > 0.f)) return 0;
    if (value >= 2147483647.f) return 2147483647;
    return (int)value;
  }

  constexpr char kHotInletDoc[] =
      "The value inlet. An int, a float or a list arriving here is queued and comes back out the "
      "outlet a delay later, as the same kind of message it went in as — Max's 'pipe delays a "
      "number, list, or symbol by a specified amount of time'. Unlike .delay, values queue rather "
      "than replace each other: a second value does not cancel the first, so ten numbers in are "
      "ten numbers out, each one its own delay after it arrived. A list whose whole text is one "
      "number arrives as that number, an int or a float by its spelling, so a .m 5 reaches an int "
      "inlet downstream; anything else is carried whole as text. The message 'clear' drops every "
      "value waiting, sending none of them, and 'flush' sends them all immediately in the order "
      "they arrived — Max's two commands. Those two words are therefore commands rather than "
      "delayable text, as they are in Max. There is no bang method here, as Max's pipe has none. A "
      "value refused because the pending set is full is dropped and counted rather than sent early "
      "or logged; see the object description.";

  constexpr char kTimeInletDoc[] =
      "Sets the delay in milliseconds for values arriving *after* it. Values already waiting keep "
      "the delay they were queued with, which is what makes the queue a queue rather than a set of "
      "deadlines that all move together. Ints, floats and a list whose leading token is a number "
      "all set it; a negative or NaN time counts as 0, which still defers by one audio block "
      "rather than sending immediately. Max's tempo-relative time syntax is not read here — this "
      "object has no clock to measure a beat against — and a note value ('4nd') or tick count "
      "('1440 ticks') is refused outright rather than misread as the milliseconds it is not, "
      "leaving the delay where it was.";

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

  // ANY rather than a fixed type: the object hands back whichever of int, float
  // and text it was given, and a .pipe that retyped its payload would not be
  // delaying the message a patch sent.
  ADD_OUT_ANY;

  ADD_PARAM(delaytime);
  delaytime = DEFAULT_DELAY;

  // The one allocation the object ever performs, and it happens here rather
  // than on an arrival: a queued list is assigned into storage that is already
  // long enough, on whichever thread the value came in on.
  for (Slot& slot : slots)
    slot.text.reserve(TEXT_CAPACITY);

  ADD_DESCRIPTION(
      "Delays numbers, lists and symbols. A value in the left inlet comes back out the outlet a "
      "settable number of milliseconds later, as the same kind of message it went in as. This is "
      ".delay for data, and the difference that matters is the queue: Max's delay holds one bang "
      "and 'the first bang is forgotten' when a second arrives, while a .pipe holds many values at "
      "once and each one keeps its own deadline. Ten numbers into a .pipe 500 are ten numbers out "
      "500 ms later in the order they went in, which is what makes echoes, delayed parameter "
      "changes and every 'do this to that value in N ms' pattern one object instead of one .delay "
      "per value. A number in the right inlet sets the delay for values arriving after it and "
      "leaves values already waiting alone. 'clear' drops everything pending without sending it "
      "and 'flush' sends it all immediately in arrival order, which are Max's two commands. The "
      "wait runs on the patcher's deferred-message scheduler (issue #628) rather than on the timer "
      "thread behind .metro, so queueing a value allocates nothing and takes no lock, and a "
      "released value arrives inside the patcher's own dispatch as one fresh logical event rather "
      "than as an unrelated stimulus. That clock is the block counter, so it stops when the engine "
      "does and a paused patch holds every pending value where it stands, and its resolution is "
      "one audio block: a delay of 0 still defers to the next block rather than firing now, which "
      "is Max's behaviour and the reason a pipe wired back into itself is a fast delay line "
      "instead of a stack overflow. The pending set is bounded and pre-allocated at 64 values per "
      "object, each holding up to 256 characters of text, and beyond it sits the patcher-wide "
      "limit of 128 pending messages shared with .delay, .qlist, .mtr and .seq. A value that does "
      "not fit is dropped and counted rather than sent early or written to the log: sending it "
      "early would break the object's one guarantee at exactly the moment the patch is at its "
      "resource limit and would let a self-feeding delay line recurse on the audio thread, and "
      "logging would allocate on whichever thread the value arrived on, which is routinely the "
      "audio callback. The count is the report, and it is readable at any time. Unlike Max's pipe "
      "there is one data inlet rather than one per creation argument: a list is already a single "
      "value here and travels whole, so it is delayed whole and spread with .spray, .bondo or "
      ".route afterwards if a patch wants it spread. Milliseconds are the only unit — a note value "
      "or tick count in the time inlet is refused rather than misread as milliseconds, there being "
      "no clock here to measure a beat against. Calculate() does nothing and no message path "
      "allocates, locks or blocks.");
  ADD_CATEGORY(pCategory::TIME);
  INLET_DOC(0, "value", kHotInletDoc, "int, float, list, 'clear', 'flush'");
  INLET_DOC(1, "time", kTimeInletDoc, "0+ ms");
  OUTLET_DOC(0, "out", "The delayed value, one audio block or more after the value that caused it.",
             "any");
  PARAM_DOC("delaytime", "0",
            "The initial delay in milliseconds — Max's creation argument and its delaytime "
            "attribute, whose documented default is 0. A delay of 0 is not the same as no delay: "
            "the value still comes back on the next audio block, as its own event, which is what "
            "Max's pipe 0 is for. The right inlet overwrites it afterwards, for values arriving "
            "after that. A negative value counts as 0. Values pending when the parameter changes "
            "keep the delay they were queued with.",
            "0+ ms");
}

int gPipe::DelayTime() const {
  const Int ms = delaytime.load();
  return ms > 0 ? (int)ms : 0;
}

void gPipe::EmitDirect(Held kind, int intValue, float floatValue, const char* text,
                       std::size_t length, YSE::THREAD thread) {
  switch (kind) {
  case Held::INT:
    outputs[0].SendInt(intValue, thread);
    break;
  case Held::FLOAT:
    outputs[0].SendFloat(floatValue, thread);
    break;
  case Held::LIST:
    // The one place a std::string is built rather than reused: a standalone
    // object has no patcher, so this is not an audio-thread path by
    // construction — nothing is rendering.
    outputs[0].SendList(std::string(text, length), thread);
    break;
  }
}

void gPipe::Emit(const Slot& slot, YSE::THREAD thread) {
  switch (slot.held) {
  case Held::INT:
    outputs[0].SendInt(slot.intValue, thread);
    break;
  case Held::FLOAT:
    outputs[0].SendFloat(slot.floatValue, thread);
    break;
  case Held::LIST:
    outputs[0].SendList(slot.text, thread);
    break;
  }
}

void gPipe::Enqueue(Held kind, int intValue, float floatValue, const char* text, std::size_t length,
                    YSE::THREAD thread) {
  messageScheduler* scheduler = Scheduler();
  if (scheduler == nullptr) {
    // A standalone object has no patcher and so no clock: "later" has no
    // referent here, and the only alternatives are now or never. `.delay`'s
    // answer to the same dead end, and the one that keeps a standalone object
    // testable rather than a black hole.
    EmitDirect(kind, intValue, floatValue, text, length, thread);
    return;
  }

  // Longer than a slot holds. Refused rather than truncated (a value this
  // object silently shortened would be a different message) and rather than
  // allocated for, since this may be the audio callback.
  if (kind == Held::LIST && length > TEXT_CAPACITY) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  // Claim a slot: a bounded walk with at most one CAS attempt per slot — no
  // lock, no allocation, no syscall — so a value may be queued from whichever
  // thread is dispatching.
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
    // The object's own pending set is full. Counted, not logged and not sent
    // early — see the header.
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  Slot& slot = slots[index];
  // Written under CLAIMED, which no other thread will move: the payload is
  // never touched concurrently.
  slot.held = kind;
  slot.intValue = intValue;
  slot.floatValue = floatValue;
  if (kind == Held::LIST) slot.text.assign(text, length);
  slot.seq = nextSeq.fetch_add(1, std::memory_order_relaxed);
  slot.handle.store(0, std::memory_order_relaxed);

  const std::uint64_t generation = claimed >> 2;
  const int tag = (int)(((generation & TAG_GEN_MASK) << TAG_INDEX_BITS) | (std::uint64_t)index);
  const std::uint64_t armed = (generation << 2) | STATE_ARMED;

  // Published *before* the message is armed, and the order is load-bearing:
  // the scheduler's deadline floor is one block, so the very next Calculate on
  // the audio thread may deliver this message while this thread is still
  // here. A slot published afterwards would fail that delivery's CAS and the
  // value would wait for ever.
  pendingCount.fetch_add(1, std::memory_order_relaxed);
  slot.stateGen.store(armed, std::memory_order_release);

  // Only a bang is armed: the payload is the slot's, so the scheduler carries
  // nothing but the deadline and the tag that names the slot. That is also
  // what lets `flush` find the value again, which a payload handed to the
  // scheduler could not be.
  const messageScheduler::Handle handle = scheduler->ScheduleBang(this, tag, DelayTime());
  if (handle == 0) {
    // The patcher-wide pending set is full while this object's is not. Give
    // the slot back — unless a `flush` has taken it in the meantime, in which
    // case the value has already gone out and the slot is no longer ours.
    std::uint64_t expected = armed;
    if (slot.stateGen.compare_exchange_strong(expected, (generation << 2) | STATE_FREE,
                                              std::memory_order_acq_rel,
                                              std::memory_order_relaxed)) {
      pendingCount.fetch_sub(1, std::memory_order_relaxed);
      dropped.fetch_add(1, std::memory_order_relaxed);
    }
    return;
  }
  // Best effort by construction — see the Slot::handle comment.
  slot.handle.store(handle, std::memory_order_relaxed);
}

void gPipe::Release(bool emit, YSE::THREAD thread) {
  // Take every armed slot first, in one bounded walk, and only then emit: a
  // send runs the whole subgraph behind the outlet, which may come back into
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
      // A delivery won the same slot; that value is on its way out through the
      // scheduler instead, which is the honest resolution of the race.
      continue;
    }
    pendingCount.fetch_sub(1, std::memory_order_relaxed);

    // Hand the patcher-wide budget back early. A cancel that loses its race
    // leaves a message that comes due, finds the slot gone, and sends nothing.
    const messageScheduler::Handle handle = slots[i].handle.exchange(0, std::memory_order_relaxed);
    if (scheduler != nullptr && handle != 0) scheduler->Cancel(handle);

    // Insertion sort by arrival ticket, in place, over a stack array of at
    // most CAPACITY entries: Max's flush emits "in the order they were
    // received", and the slot table is not in that order.
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
    if (emit) Emit(slot, thread);
    // Freed only once the send has finished, so nothing can overwrite the text
    // the send is reading.
    slot.stateGen.store(slot.stateGen.load(std::memory_order_relaxed) & ~STATE_MASK,
                        std::memory_order_release);
  }
}

void gPipe::DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) {
  // A value's wait has elapsed. The scheduler wraps this in a fresh
  // messageEventScope, so everything the released value goes on to cause is one
  // logical event (#628) — the causal chain the value came in on, resumed
  // rather than replaced, which is the whole reason this object does not use
  // TimerThread.
  const std::size_t index = (std::size_t)(msg.tag & TAG_INDEX_MASK);
  if (index >= CAPACITY) return;
  const std::uint64_t generation = ((std::uint64_t)msg.tag >> TAG_INDEX_BITS) & TAG_GEN_MASK;

  Slot& slot = slots[index];
  std::uint64_t state = slot.stateGen.load(std::memory_order_acquire);
  // Not armed, or armed for a *later* value than the one this message names:
  // `clear` or `flush` took this one, and the slot has since been reused. The
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

  // The tag is passed straight through, as `.delay` passes it: T_GUI means
  // "let the block's own traversal render what this caused", which is the right
  // reading for an outlet send.
  Emit(slot, thread);
  slot.stateGen.store(firing & ~STATE_MASK, std::memory_order_release);
}

INT_IN(IntIn) {
  if (inlet == 0) {
    Enqueue(Held::INT, value, 0.f, nullptr, 0, thread);
    return;
  }
  // Max's right inlet: the delay for values received *after* it. Values already
  // waiting are not retimed, which falls out of not touching their slots.
  delaytime = value;
}

FLOAT_IN(FloatIn) {
  if (inlet == 0) {
    Enqueue(Held::FLOAT, 0, value, nullptr, 0, thread);
    return;
  }
  delaytime = MillisFromFloat(value);
}

LIST_IN(ListIn) {
  // The leading token, matched in place: a substr here would allocate on
  // whichever thread the message arrived on.
  std::size_t begin = 0;
  while (begin < value.size() && IsSelectorSeparator(value[begin]))
    begin++;
  std::size_t end = begin;
  while (end < value.size() && !IsSelectorSeparator(value[end]))
    end++;
  if (end <= begin) return;
  const std::size_t length = end - begin;

  if (inlet == 0) {
    // Max's two commands, left inlet only. Both are words rather than data, as
    // they are in Max, so neither can be delayed as text.
    if (length == 5 && value.compare(begin, length, "clear", 5) == 0) {
      Release(false, thread);
      return;
    }
    if (length == 5 && value.compare(begin, length, "flush", 5) == 0) {
      Release(true, thread);
      return;
    }

    // A message whose whole text is one number is queued as that number, an int
    // or a float by its spelling — the leading-token test `.bondo` and
    // `.trigger` already use, applied here so a delayed value reaches the same
    // inlets the undelayed one would have. Anything else is carried whole.
    std::size_t after = end;
    while (after < value.size() && IsSelectorSeparator(value[after]))
      after++;
    float number = 0.f;
    if (after == value.size() && ReadNumericToken(value.c_str() + begin, length, number)) {
      if (TokenLooksLikeFloat(value.c_str() + begin, length)) {
        Enqueue(Held::FLOAT, 0, number, nullptr, 0, thread);
      } else {
        // ReadNumericToken only promised a finite float, so the token may still
        // be wider than an int: truncate through the range-checked conversion
        // rather than casting.
        Enqueue(Held::INT, ExprToInt(number), 0.f, nullptr, 0, thread);
      }
      return;
    }

    Enqueue(Held::LIST, 0, 0.f, value.c_str(), value.size(), thread);
    return;
  }

  // The time inlet. Max's tempo-relative syntax is read *first* and refused as
  // a whole, because its tick spelling starts with a number: `1440 ticks` taken
  // for its leading token would silently become 1440 ms, which is the mistake
  // `.clocker` made before #725. This object has no clock to measure a beat
  // against, so the honest answer is to leave the delay where it was.
  double beats = 0.0;
  if (ReadBeatTime(value.c_str() + begin, value.size() - begin, beats)) return;

  float number = 0.f;
  if (!ReadNumericToken(value.c_str() + begin, length, number)) return;
  delaytime = MillisFromFloat(number);
}
