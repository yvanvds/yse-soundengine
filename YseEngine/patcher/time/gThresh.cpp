#include "gThresh.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include "timeValue.h"
#include <cstddef>
#include <cstdint>

using namespace YSE::PATCHER;

#define className gThreshBase

namespace {

  // A float time as the milliseconds it means. Negatives and NaN become 0 — no
  // wait at all, which the scheduler's one-block floor then turns into "next
  // block" — and anything past the int range saturates rather than being cast,
  // since casting a float outside that range is undefined behaviour. `.pipe`,
  // `.qlim`, `.delay` and `.mtr` decide the same question the same way.
  int MillisFromFloat(float value) {
    // Written as a failed `>` rather than `<=` so a NaN takes this branch too.
    if (!(value > 0.f)) return 0;
    if (value >= 2147483647.f) return 2147483647;
    return (int)value;
  }

  // Atoms in a piece of list text — what `Items()` counts, and the honest unit:
  // Max's thresh appends "the entire list" and a three-atom list makes the
  // stored list three longer.
  std::size_t CountTokens(const char* text, std::size_t length) {
    std::size_t count = 0;
    std::size_t i = 0;
    while (i < length) {
      while (i < length && IsSelectorSeparator(text[i]))
        i++;
      if (i >= length) break;
      count++;
      while (i < length && !IsSelectorSeparator(text[i]))
        i++;
    }
    return count;
  }

  // True when the whole of `text` is one token that is wholly a number, which
  // is then written to `value` with the spelling it was given. The
  // leading-token test `.bondo`, `.trigger`, `.pipe` and `.qlim` already use,
  // applied to a finished group so a collection of one reaches the same inlets
  // the single uncollected value would have.
  bool SingleNumber(const char* text, std::size_t length, float& value, bool& isFloat) {
    for (std::size_t i = 0; i < length; i++)
      if (IsSelectorSeparator(text[i])) return false;
    if (!ReadNumericToken(text, length, value)) return false;
    isFloat = TokenLooksLikeFloat(text, length);
    return true;
  }

} // namespace

gThreshBase::gThreshBase() : pObject(false) {
  ADD_IN_0;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  // ANY rather than LIST: a group of one number comes out as that number, an
  // int or a float by its spelling, and this patcher does no coercion at an
  // inlet — a grouping object that always retyped its output to a list would
  // stop a single collected value from reaching the `.i` a patch wired it to.
  ADD_OUT_ANY;

  ADD_PARAM(threshold);

  // The one allocation either object ever performs, and it happens here rather
  // than on an arrival: a collected value is appended into storage that is
  // already long enough, on whichever thread the value came in on — routinely
  // the audio callback.
  buffer.reserve(TEXT_CAPACITY);

  ADD_CATEGORY(pCategory::TIME);
}

void gThreshBase::Document(const char* summary, const char* inletDoc, const char* thresholdInletDoc,
                           const char* outletDoc, const char* thresholdDefault,
                           const char* thresholdParamDoc) {
  ADD_DESCRIPTION(summary);
  INLET_DOC(0, "in", inletDoc, "");
  INLET_DOC(1, "threshold", thresholdInletDoc, "0+ ms");
  OUTLET_DOC(0, "out", outletDoc, "any");
  // The default differs per object — Max gives thresh 10 ms and quickthresh 40 —
  // so it comes in from the subclass rather than being a literal here.
  PARAM_DOC("threshold", thresholdDefault, thresholdParamDoc, "0+ ms");
}

int gThreshBase::ClampedMillis(const aInt& value) {
  const Int ms = value.load();
  return ms > 0 ? (int)ms : 0;
}

int gThreshBase::Threshold() const {
  return ClampedMillis(threshold);
}

int gThreshBase::MillisSince(std::uint64_t block) const {
  const messageScheduler* scheduler = Scheduler();
  if (scheduler == nullptr) return 0;
  const std::uint64_t now = scheduler->Now();
  // The comparison is defensive: the block clock is monotonic, but a group
  // opened before the patcher started would otherwise read a gap backwards.
  return messageScheduler::MillisForBlocks(now > block ? now - block : 0);
}

bool gThreshBase::Arm(int delayMs) {
  messageScheduler* scheduler = Scheduler();
  if (scheduler == nullptr) return false;
  // The tag names the group, not the slot: a deadline that comes due for a
  // group already sent finds a generation that has moved on and does nothing.
  const int tag = (int)(generation.load(std::memory_order_acquire) & TAG_MASK);
  const messageScheduler::Handle armed = scheduler->ScheduleBang(this, tag, delayMs);
  if (armed == 0) return false;
  handle.store(armed, std::memory_order_relaxed);
  return true;
}

void gThreshBase::SetTime(int inlet, int millis) {
  if (inlet == 1) threshold = millis;
}

bool gThreshBase::Command(const std::string&, std::size_t, std::size_t, YSE::THREAD) {
  return false;
}

void gThreshBase::EmitBuffer(YSE::THREAD thread) {
  if (buffer.empty()) return;
  float number = 0.f;
  bool isFloat = false;
  if (SingleNumber(buffer.c_str(), buffer.size(), number, isFloat)) {
    if (isFloat) {
      outputs[0].SendFloat(number, thread);
    } else {
      // ReadNumericToken only promised a finite float, so the token may still
      // be wider than an int: truncate through the range-checked conversion
      // rather than casting.
      outputs[0].SendInt(ExprToInt(number), thread);
    }
    return;
  }
  // By reference, out of the buffer the constructor reserved: the send that
  // closes a group allocates nothing.
  outputs[0].SendList(buffer, thread);
}

void gThreshBase::EmitDirect(const char* text, std::size_t length, YSE::THREAD thread) {
  float number = 0.f;
  bool isFloat = false;
  if (SingleNumber(text, length, number, isFloat)) {
    if (isFloat) {
      outputs[0].SendFloat(number, thread);
    } else {
      outputs[0].SendInt(ExprToInt(number), thread);
    }
    return;
  }
  // The one place a std::string is built rather than reused: this path is only
  // reached by a standalone object, which has no patcher, so it is not an
  // audio-thread path by construction — nothing is rendering.
  outputs[0].SendList(std::string(text, length), thread);
}

void gThreshBase::Collect(const char* text, std::size_t length, YSE::THREAD thread) {
  // Trimmed in place: a substr here would allocate on whichever thread the
  // message arrived on. The buffer holds single-space-separated atoms, which is
  // what makes the one-token test at the end of a group a test of the whole
  // buffer.
  while (length > 0 && IsSelectorSeparator(*text)) {
    text++;
    length--;
  }
  while (length > 0 && IsSelectorSeparator(text[length - 1]))
    length--;
  if (length == 0) return;

  messageScheduler* scheduler = Scheduler();
  if (scheduler == nullptr) {
    // A standalone object has no patcher and so no clock: "close together" has
    // no referent, and every value is its own group. `.pipe`'s and `.qlim`'s
    // answer to the same dead end, and the one that keeps a standalone object
    // testable rather than a black hole that swallows everything it is sent.
    EmitDirect(text, length, thread);
    return;
  }

  // EMPTY opens a group and OPEN appends to one; BUSY or FIRING means another
  // thread is writing the buffer or a send is reading it, and this value is
  // dropped rather than made to spin on a path the audio callback takes. See
  // the class notes.
  std::uint8_t state_ = state.load(std::memory_order_acquire);
  if (state_ != STATE_EMPTY && state_ != STATE_OPEN) {
    CountDrop();
    return;
  }
  const bool opening = state_ == STATE_EMPTY;
  if (!state.compare_exchange_strong(state_, STATE_BUSY, std::memory_order_acq_rel,
                                     std::memory_order_relaxed)) {
    CountDrop();
    return;
  }

  // Longer than the buffer holds. Refused rather than truncated (a group this
  // object silently shortened would be a different list) and rather than
  // allocated for — and the values already collected are kept, so an over-long
  // group loses its tail and not its head.
  if ((opening ? length : buffer.size() + 1 + length) > TEXT_CAPACITY) {
    state.store(opening ? STATE_EMPTY : STATE_OPEN, std::memory_order_release);
    CountDrop();
    return;
  }

  // Written under BUSY, which no other thread will move: the buffer is never
  // touched concurrently.
  if (opening) {
    buffer.assign(text, length);
    items.store(CountTokens(text, length), std::memory_order_relaxed);
    ResetGroup();
  } else {
    buffer.push_back(' ');
    buffer.append(text, length);
    items.fetch_add(CountTokens(text, length), std::memory_order_relaxed);
  }
  // What a gap is measured from, and what the fudge zone is measured against.
  lastBlock.store(scheduler->Now(), std::memory_order_relaxed);
  state.store(STATE_OPEN, std::memory_order_release);

  // A group is armed once, when it opens. `.thresh`'s reset-on-every-item is
  // done by measuring the gap again at the deadline rather than by cancelling
  // and re-arming per value, which would cost a scheduler round trip per value
  // on the audio thread for the same answer.
  if (!opening) return;
  if (Arm(Threshold())) return;

  // The patcher-wide pending set is full, so this group has no way to ever
  // close. Give it back and count the refusal rather than open a group nothing
  // will ever send — and rather than emit the value immediately, which would
  // let an object wired back into its own inlet recurse on the audio thread.
  std::uint8_t open = STATE_OPEN;
  if (state.compare_exchange_strong(open, STATE_BUSY, std::memory_order_acq_rel,
                                    std::memory_order_relaxed)) {
    buffer.clear();
    items.store(0, std::memory_order_relaxed);
    state.store(STATE_EMPTY, std::memory_order_release);
    CountDrop();
  }
}

void gThreshBase::CollectInt(int value, YSE::THREAD thread) {
  char text[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
  if (written <= 0) return;
  Collect(text, (std::size_t)written, thread);
}

void gThreshBase::CollectFloat(float value, YSE::THREAD thread) {
  char text[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  if (written <= 0) return;
  Collect(text, (std::size_t)written, thread);
}

void gThreshBase::Flush(YSE::THREAD thread) {
  std::uint8_t open = STATE_OPEN;
  if (!state.compare_exchange_strong(open, STATE_FIRING, std::memory_order_acq_rel,
                                     std::memory_order_relaxed)) {
    // No open group, or a deadline and a bang raced for the same one and the
    // other won. Either way this group has already been dealt with.
    return;
  }

  // The group is over: no deadline armed for it can act any more. The bump is
  // what makes that true, and the cancel is best effort on top of it — a cancel
  // that loses its race leaves a message that comes due, finds a generation
  // that has moved on, and does nothing.
  generation.fetch_add(1, std::memory_order_acq_rel);
  const messageScheduler::Handle armed = handle.exchange(0, std::memory_order_relaxed);
  messageScheduler* scheduler = Scheduler();
  if (scheduler != nullptr && armed != 0) scheduler->Cancel(armed);

  EmitBuffer(thread);

  // Emptied only once the send has finished, so nothing can overwrite the text
  // the send is reading. That is also what makes a value arriving from inside
  // the send find FIRING and be counted rather than land in a group that is
  // half gone.
  buffer.clear();
  items.store(0, std::memory_order_relaxed);
  state.store(STATE_EMPTY, std::memory_order_release);
}

void gThreshBase::DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) {
  // A group's deadline has come due. The scheduler wraps this in a fresh
  // messageEventScope, so everything the emitted list goes on to cause is one
  // logical event (#628) — the causal chain the values came in on, resumed
  // rather than replaced, which is the whole reason neither object uses
  // TimerThread.
  if ((std::uint32_t)msg.tag != (generation.load(std::memory_order_acquire) & TAG_MASK)) return;
  handle.store(0, std::memory_order_relaxed);
  if (state.load(std::memory_order_acquire) != STATE_OPEN) return;
  OnDeadline(thread);
}

INT_IN(IntIn) {
  if (inlet == 0) {
    CollectInt(value, thread);
    return;
  }
  SetTime(inlet, value);
}

FLOAT_IN(FloatIn) {
  if (inlet == 0) {
    CollectFloat(value, thread);
    return;
  }
  SetTime(inlet, MillisFromFloat(value));
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

  if (inlet == 0) {
    if (Command(value, begin, end, thread)) return;
    // Max: "The entire list is appended to the list stored in thresh." A list
    // is one value here and travels whole, so the whole of it joins the group
    // rather than only its leading token.
    Collect(value.c_str() + begin, value.size() - begin, thread);
    return;
  }

  // A time inlet. Max's tempo-relative syntax is read *first* and refused as a
  // whole, because its tick spelling starts with a number: `1440 ticks` taken
  // for its leading token would silently become 1440 ms, which is the mistake
  // `.clocker` made before #725. Neither object has a clock to measure a beat
  // against, so the honest answer is to leave the time where it was.
  double beats = 0.0;
  if (ReadBeatTime(value.c_str() + begin, value.size() - begin, beats)) return;

  float number = 0.f;
  if (!ReadNumericToken(value.c_str() + begin, end - begin, number)) return;
  SetTime(inlet, MillisFromFloat(number));
}

// ─── .thresh — close the group on a gap ──────────────────────────────────────

#undef className
#define className gThresh

gThresh::gThresh() : gThreshBase() {
  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  threshold = DEFAULT_THRESHOLD;

  Document(
      "Combines the values that arrive close together into one list, sending the group as soon as "
      "the input goes quiet. Every int, float or list in the left inlet is appended to a list this "
      "object is building, and every one of them restarts the clock — Max's 'collects items into a "
      "list if they appear within a certain specifiable amount of time; each time an item arrives, "
      "the time is reset'. When nothing has arrived for the threshold, the list goes out the "
      "outlet "
      "and the next value starts a fresh one. That makes this the object for 'wait until they have "
      "stopped, then tell me what they sent': a burst of controller values becomes one list, a "
      "handful of note numbers becomes one chord, a scattering of symbols becomes one message. A "
      "stream that never pauses never closes a group, which is not a fault but what 'received "
      "close "
      "together' means when everything is close together — reach for the sibling .quickthresh when "
      "the list has to come out at a moment the patch can predict, since its window runs from the "
      "first value of a group rather than from the last. The threshold is a number in the right "
      "inlet or the creation argument, in milliseconds; Max's default is 10. A group of a single "
      "number comes out as that number, an int or a float by its spelling, so it still reaches an "
      "int inlet downstream; anything longer comes out as list text, which is what a list is here. "
      "A whole list arriving in the left inlet is appended whole, as Max's is. The wait runs on "
      "the "
      "patcher's deferred-message scheduler (issue #628) rather than on the timer thread behind "
      ".metro, so collecting a value allocates nothing and takes no lock, and the emitted list "
      "arrives inside the patcher's own dispatch as one logical event caused by the values that "
      "went into it. That clock is the block counter, so it stops when the engine does — a paused "
      "patch holds an open group where it stands — and its resolution is one audio block: values "
      "arriving in the same dispatch are zero milliseconds apart however far apart they really "
      "were, and a threshold of 0 still groups everything that arrived in the same block. The "
      "accumulation buffer is bounded and pre-allocated at 256 characters; a value that would take "
      "a group past it is dropped and counted rather than logged, an over-long group losing its "
      "tail and keeping everything collected so far. Milliseconds are the only unit — a note value "
      "or tick count in the threshold inlet is refused rather than misread as milliseconds, there "
      "being no clock here to measure a beat against. Calculate() does nothing and no message path "
      "allocates, locks or blocks.",
      "The value inlet. An int, a float or a list arriving here joins the list this object is "
      "building and restarts the gap timer; a whole list is appended whole, as Max's 'the entire "
      "list is appended to the list stored in thresh'. When the threshold passes with nothing new, "
      "the collected list goes out and the next value starts a fresh group. A group of one numeric "
      "token leaves as that number rather than as a list of one, so it reaches the inlets an "
      "uncollected value would have reached. There are no command words: every message is data, "
      "and "
      "there is no bang method, as Max's thresh has none.",
      "Sets the gap that ends a group, in milliseconds — Max's 'the number is stored as the time, "
      "in milliseconds, to wait before sending out the compiled list of numbers'. Ints, floats and "
      "a list whose leading token is a number all set it; a negative or NaN time counts as 0, "
      "which "
      "still groups everything arriving in the same audio block, that being the scheduler's "
      "deadline floor. The new threshold is measured against the group's most recent value, so "
      "shortening it can close an open group at its next deadline. Max's tempo-relative time "
      "syntax "
      "is not read here — this object has no clock to measure a beat against — and a note value "
      "('4nd') or tick count ('1440 ticks') is refused outright rather than misread as the "
      "milliseconds it is not, leaving the threshold where it was. There is no bang method on this "
      "inlet.",
      "The collected group, sent when the input has been quiet for the threshold: list text for a "
      "group of more than one atom, and the number itself for a group of one numeric token.",
      "10",
      "The initial gap that ends a group, in milliseconds — Max's creation argument, whose "
      "documented default is 10: 'if no argument is present, the initial value is 10 "
      "milliseconds'. "
      "The right inlet overwrites it afterwards. A negative value counts as 0.");
}

void gThresh::OnDeadline(YSE::THREAD thread) {
  const int limit = Threshold();
  const int idle = MillisSince(LastBlock());
  if (idle < limit) {
    // Max: "each time an item arrives, the time is reset." Measured again from
    // the most recent value rather than cancelled and re-armed per value, which
    // would cost a scheduler round trip per value for the same answer.
    if (Arm(limit - idle)) return;
    // The patcher-wide pending set is full. This group already holds real
    // values, so it is sent now and the shortened wait counted: a group cut
    // short is a smaller loss than a group that never comes out at all.
    CountDrop();
  }
  Flush(thread);
}

// ─── .quickthresh — close the group on a window from the first value ─────────

#undef className
#define className gQuickthresh

gQuickthresh::gQuickthresh() : gThreshBase() {
  // inputs.back() is still the value inlet the base built: Max's bang, which
  // "will reset quickthresh and output the notes in its buffer".
  REG_BANG_IN(BangIn);

  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_IN_2;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_IN_3;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_PARAM(fudge);
  ADD_PARAM(extension);

  threshold = DEFAULT_THRESHOLD;
  fudge = DEFAULT_FUDGE;
  extension = DEFAULT_EXTENSION;

  Document(
      "Fast chord detection: collects the values that arrive close together into one list on a "
      "fixed window that starts at the first of them. Max's 'combines numbers when they are "
      "received close together; quickthresh is a faster, low-latency alternative to thresh that is "
      "optimized for chord detection'. The window is the whole difference from the sibling "
      ".thresh, "
      "whose clock is restarted by every value and which therefore only sends a group once the "
      "input has gone quiet. Here the group is sent a known time after it started however many "
      "values arrive inside it, which is what a chord needs: three notes struck together arrive as "
      "three ints milliseconds apart, and nothing downstream can treat them as a chord until they "
      "have been put back together — soon, and at a predictable moment, not whenever the player "
      "next lifts their hands. Sloppy playing is what the other two times are for: if a value "
      "lands "
      "in the last 'fudge' milliseconds of the window, the window is extended by the extension "
      "time "
      "to catch the note that was nearly in time — Max's 'if any notes are played within this "
      "amount of time at the end of the base thresh time, the threshold is extended'. That happens "
      "at most once per group, so a group lasts at most threshold plus extension and a continuous "
      "stream cannot push the list out indefinitely, which is the bound that makes this object "
      "predictable. Max's defaults are the defaults here: 40 ms base, 10 ms fudge, 20 ms "
      "extension, "
      "set by the three creation arguments, by the second, third and fourth inlets, or all at once "
      "by the message 'set threshold fudge extension'. A bang closes the group and sends it "
      "immediately — Max's 'bang will reset quickthresh and output the notes in its buffer'. A "
      "group of a single number comes out as that number, an int or a float by its spelling, so it "
      "still reaches an int inlet downstream; anything longer comes out as list text. The wait "
      "runs "
      "on the patcher's deferred-message scheduler (issue #628) rather than on the timer thread "
      "behind .metro, so collecting a value allocates nothing and takes no lock, and the emitted "
      "list arrives inside the patcher's own dispatch as one logical event caused by the values "
      "that went into it. That clock is the block counter, so it stops when the engine does — a "
      "paused patch holds an open group where it stands — and its resolution is one audio block, "
      "which is also the resolution of the fudge test: at the default 10 ms and a 128-sample block "
      "only the last few blocks of the window count. The accumulation buffer is bounded and "
      "pre-allocated at 256 characters; a value that would take a group past it is dropped and "
      "counted rather than logged, an over-long group losing its tail and keeping everything "
      "collected so far. Milliseconds are the only unit — a note value or tick count in a time "
      "inlet is refused rather than misread as milliseconds, there being no clock here to measure "
      "a "
      "beat against. Calculate() does nothing and no message path allocates, locks or blocks.",
      "The value inlet. An int, a float or a list arriving here joins the list this object is "
      "building; the first one opens a window that runs for the base threshold whatever arrives "
      "inside it, and a value landing in the last 'fudge' milliseconds of that window extends it "
      "once by the extension time. A whole list is appended whole. A bang closes the group and "
      "sends it now, whatever the window had left — Max's 'bang will reset quickthresh and output "
      "the notes in its buffer'. The message 'set', followed by up to three millisecond values, "
      "writes the base threshold, the fudge time and the extension at once; like Max's it is a "
      "command word rather than data, so it cannot be collected as text. A group of one numeric "
      "token leaves as that number rather than as a list of one.",
      "Sets the base threshold in milliseconds — Max's 'all values received in the left inlet "
      "within this time period are collected into a list'. Ints, floats and a list whose leading "
      "token is a number all set it; a negative or NaN time counts as 0, which still groups "
      "everything arriving in the same audio block, that being the scheduler's deadline floor. A "
      "window already running keeps the deadline it opened with. Max's tempo-relative time syntax "
      "is not read here — this object has no clock to measure a beat against — and a note value "
      "('4nd') or tick count ('1440 ticks') is refused outright rather than misread as the "
      "milliseconds it is not, leaving the threshold where it was. There is no bang method on this "
      "inlet.",
      "The collected group, sent when its window closes or when a bang cuts it short: list text "
      "for "
      "a group of more than one atom, and the number itself for a group of one numeric token.",
      "40",
      "The initial base threshold in milliseconds — Max's first creation argument, whose "
      "documented "
      "default is 40: 'the default value for the base threshold is 40 ms'. The second inlet "
      "overwrites it afterwards. A negative value counts as 0.");

  INLET_DOC(2, "fudge",
            "Sets the fudge time in milliseconds — Max's 'if there are any incoming values within "
            "this amount of time at the end of the base thresh time, the threshold is extended to "
            "allow more values to be added to the list'. It is the grace zone at the *end* of the "
            "window, not an addition to it: what it buys is the extension time, and only when a "
            "value actually lands inside it. A fudge of 0 disables the extension entirely, and one "
            "shorter than an audio block means 'a value arriving in the same dispatch the deadline "
            "fell in', that being the resolution of the block clock this is measured on. Ints, "
            "floats and a list whose leading token is a number all set it; a negative or NaN time "
            "counts as 0, and a note value or tick count is refused rather than misread as "
            "milliseconds.",
            "0+ ms");
  INLET_DOC(3, "extension",
            "Sets the extension in milliseconds — Max's 'an extension of the base thresh time, "
            "which is used if values arrive in the object's inlet in the fudge time zone'. It is "
            "applied at most once per group, which is what keeps a group to at most threshold plus "
            "extension however long the stream goes on; a continuous stream cannot postpone the "
            "list the way it can with .thresh. An extension of 0 leaves the window at the base "
            "threshold whatever lands in the fudge zone. Ints, floats and a list whose leading "
            "token is a number all set it; a negative or NaN time counts as 0, and a note value or "
            "tick count is refused rather than misread as milliseconds.",
            "0+ ms");

  PARAM_DOC("fudge", "10",
            "The initial fudge time in milliseconds — Max's second creation argument, whose "
            "documented default is 10: 'if not provided, the default value is 10 ms'. The third "
            "inlet and the 'set' message overwrite it afterwards. A negative value counts as 0, "
            "which disables the extension.",
            "0+ ms");
  PARAM_DOC(
      "extension", "20",
      "The initial extension in milliseconds — Max's third creation argument, whose "
      "documented default is 20: 'the default value is 20 ms'. The fourth inlet and the 'set' "
      "message overwrite it afterwards. A negative value counts as 0, which leaves the window "
      "at the base threshold.",
      "0+ ms");
}

int gQuickthresh::Fudge() const {
  return ClampedMillis(fudge);
}

int gQuickthresh::Extension() const {
  return ClampedMillis(extension);
}

void gQuickthresh::ResetGroup() {
  extended.store(false, std::memory_order_relaxed);
}

void gQuickthresh::SetTime(int inlet, int millis) {
  switch (inlet) {
  case 1:
    threshold = millis;
    break;
  case 2:
    fudge = millis;
    break;
  case 3:
    extension = millis;
    break;
  default:
    break;
  }
}

bool gQuickthresh::Command(const std::string& value, std::size_t begin, std::size_t end,
                           YSE::THREAD) {
  // Max's one message word: "the word set, followed by three millisecond
  // values, can be used to set the three threshold parameter values." A word
  // rather than data, as it is in Max, so it cannot be collected as text.
  const std::size_t length = end - begin;
  if (length != 3 || value.compare(begin, length, "set", 3) != 0) return false;

  int times[3] = {0, 0, 0};
  const int read = ReadIntList(value, end, times, 3);
  // Fewer than three values write only what was given: a `set 100` is a base
  // threshold and says nothing about the other two.
  if (read > 0) threshold = times[0];
  if (read > 1) fudge = times[1];
  if (read > 2) extension = times[2];
  return true;
}

BANG_IN(BangIn) {
  // Max: "bang will reset quickthresh and output the notes in its buffer."
  // Registered on the value inlet only, which is where Max documents it.
  if (inlet != 0) return;
  Flush(thread);
}

void gQuickthresh::OnDeadline(YSE::THREAD thread) {
  if (!extended.load(std::memory_order_relaxed)) {
    const int fudgeMs = Fudge();
    const int extensionMs = Extension();
    // "If any notes are played within this amount of time at the end of the
    // base thresh time, the threshold is extended." The deadline *is* the end
    // of the window, so the test is simply how long ago the last value arrived.
    if (fudgeMs > 0 && extensionMs > 0 && MillisSince(LastBlock()) < fudgeMs) {
      // Once per group, which is what bounds a group at threshold + extension.
      extended.store(true, std::memory_order_relaxed);
      if (Arm(extensionMs)) return;
      // The patcher-wide pending set is full. The group already holds real
      // values, so it goes out now and the lost extension is counted.
      CountDrop();
    }
  }
  Flush(thread);
}
