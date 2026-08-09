#include "gRateLimit.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include "timeValue.h"
#include <cstddef>
#include <cstdint>

using namespace YSE::PATCHER;

#define className gRateLimitBase

namespace {

  // A float interval as the milliseconds it means. Negatives and NaN become 0 —
  // no limiting, which is Max's default — and anything past the int range
  // saturates rather than being cast, since casting a float outside that range
  // is undefined behaviour. `.delay`, `.pipe`, `.mtr` and `.seq` decide the same
  // question the same way.
  int MillisFromFloat(float value) {
    // Written as a failed `>` rather than `<=` so a NaN takes this branch too.
    if (!(value > 0.f)) return 0;
    if (value >= 2147483647.f) return 2147483647;
    return (int)value;
  }

  constexpr char kTimeInletDoc[] =
      "Sets the minimum time between outputs, in milliseconds — Max's 'the number is stored as the "
      "minimum amount of time, in milliseconds, between successive outputs'. Ints, floats and a "
      "list whose leading token is a number all set it; a negative or NaN time counts as 0, which "
      "means no limiting at all rather than a one-block wait. The new interval applies to the next "
      "message to arrive and is measured from the last output, so shortening it can open the "
      "window immediately. Max's tempo-relative time syntax is not read here — this object has no "
      "clock to measure a beat against — and a note value ('4nd') or tick count ('1440 ticks') is "
      "refused outright rather than misread as the milliseconds it is not, leaving the interval "
      "where it was. There is no bang method on this inlet.";

} // namespace

gRateLimitBase::gRateLimitBase() : pObject(false) {
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  ADD_IN_1;
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  // ANY rather than a fixed type: the object hands back whichever of bang, int,
  // float and text it was given, and a limiter that retyped its payload would
  // not be limiting the message a patch sent.
  ADD_OUT_ANY;

  ADD_PARAM(interval);
  interval = DEFAULT_INTERVAL;

  ADD_CATEGORY(pCategory::TIME);
}

void gRateLimitBase::Document(const char* summary, const char* inletDoc, const char* outletDoc,
                              const char* paramDoc) {
  ADD_DESCRIPTION(summary);
  INLET_DOC(0, "in", inletDoc, "bang, int, float, list");
  INLET_DOC(1, "interval", kTimeInletDoc, "0+ ms");
  OUTLET_DOC(0, "out", outletDoc, "any");
  PARAM_DOC("interval", "0", paramDoc, "0+ ms");
}

int gRateLimitBase::Interval() const {
  const Int ms = interval.load();
  return ms > 0 ? (int)ms : 0;
}

std::uint64_t gRateLimitBase::NowBlock() const {
  // A standalone object has no patcher and so no clock. Everything then measures
  // at 0 and passes straight through, which is the only honest answer: there is
  // no time for an interval to be measured against. `.mtr`'s and `.seq`'s
  // arrangement.
  const messageScheduler* scheduler = Scheduler();
  return scheduler == nullptr ? 0 : scheduler->Now();
}

int gRateLimitBase::SinceLastOutput() const {
  if (!hasOutput.load(std::memory_order_relaxed)) return -1;
  if (Scheduler() == nullptr) return -1;
  const std::uint64_t now = NowBlock();
  const std::uint64_t last = lastBlock.load(std::memory_order_relaxed);
  // The comparison is defensive: the block clock is monotonic, but an object
  // that output before the patcher started would otherwise read a gap backwards.
  return messageScheduler::MillisForBlocks(now > last ? now - last : 0);
}

void gRateLimitBase::Emit(Held kind, int intValue, float floatValue, const std::string* text,
                          YSE::THREAD thread) {
  // The window is opened *before* the send rather than after. A send runs the
  // whole subgraph behind the outlet, which may come back into this object, and
  // a re-entrant message must measure itself against the window this output just
  // started — not against the previous one, which would let it straight through.
  lastBlock.store(NowBlock(), std::memory_order_relaxed);
  hasOutput.store(true, std::memory_order_relaxed);

  switch (kind) {
  case Held::BANG:
    outputs[0].SendBang(thread);
    break;
  case Held::INT:
    outputs[0].SendInt(intValue, thread);
    break;
  case Held::FLOAT:
    outputs[0].SendFloat(floatValue, thread);
    break;
  case Held::LIST:
    if (text != nullptr) outputs[0].SendList(*text, thread);
    break;
  }
}

void gRateLimitBase::Offer(Held kind, int intValue, float floatValue, const std::string* text,
                           YSE::THREAD thread) {
  if (Scheduler() == nullptr) {
    // No patcher, no clock, no "since the previous output". See the header.
    Emit(kind, intValue, floatValue, text, thread);
    return;
  }

  // Max: "provided that a certain minimum time has elapsed since the previous
  // output". Before the first output there is no previous one to measure
  // against, so the first message always passes.
  if (!hasOutput.load(std::memory_order_relaxed)) {
    Emit(kind, intValue, floatValue, text, thread);
    return;
  }

  const int limit = Interval();
  const std::uint64_t now = NowBlock();
  const std::uint64_t last = lastBlock.load(std::memory_order_relaxed);
  const int elapsed = messageScheduler::MillisForBlocks(now > last ? now - last : 0);

  // `>=` rather than `>`, so an interval of 0 — Max's default, and no limiting —
  // passes everything: a rate limiter set to no limit is a wire.
  if (elapsed >= limit) {
    Emit(kind, intValue, floatValue, text, thread);
    return;
  }

  // Too soon. What that means is the whole difference between the two objects.
  Blocked(kind, intValue, floatValue, text, limit - elapsed, thread);
}

BANG_IN(BangIn) {
  // Registered on inlet 0 only, which is where Max documents it: "performs the
  // same function as an anything message applied to the passing of bang
  // messages". A bang is a message like any other here — it is rate-limited and
  // comes back out as a bang.
  if (inlet != 0) return;
  Offer(Held::BANG, 0, 0.f, nullptr, thread);
}

INT_IN(IntIn) {
  if (inlet == 0) {
    Offer(Held::INT, value, 0.f, nullptr, thread);
    return;
  }
  // Max's right inlet: "the number is stored as the minimum amount of time, in
  // milliseconds, between successive outputs."
  interval = value;
}

FLOAT_IN(FloatIn) {
  if (inlet == 0) {
    Offer(Held::FLOAT, 0, value, nullptr, thread);
    return;
  }
  interval = MillisFromFloat(value);
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
    // A message whose whole text is one number goes on as that number, an int or
    // a float by its spelling — the leading-token test `.bondo`, `.trigger` and
    // `.pipe` already use, applied here so a limited value reaches the same
    // inlets an unlimited one would have. Anything else travels whole, as text.
    std::size_t after = end;
    while (after < value.size() && IsSelectorSeparator(value[after]))
      after++;
    float number = 0.f;
    if (after == value.size() && ReadNumericToken(value.c_str() + begin, length, number)) {
      if (TokenLooksLikeFloat(value.c_str() + begin, length)) {
        Offer(Held::FLOAT, 0, number, nullptr, thread);
      } else {
        // ReadNumericToken only promised a finite float, so the token may still
        // be wider than an int: truncate through the range-checked conversion
        // rather than casting.
        Offer(Held::INT, ExprToInt(number), 0.f, nullptr, thread);
      }
      return;
    }

    Offer(Held::LIST, 0, 0.f, &value, thread);
    return;
  }

  // The interval inlet. Max's tempo-relative syntax is read *first* and refused
  // as a whole, because its tick spelling starts with a number: `1440 ticks`
  // taken for its leading token would silently become 1440 ms, which is the
  // mistake `.clocker` made before #725. This object has no clock to measure a
  // beat against, so the honest answer is to leave the interval where it was.
  double beats = 0.0;
  if (ReadBeatTime(value.c_str() + begin, value.size() - begin, beats)) return;

  float number = 0.f;
  if (!ReadNumericToken(value.c_str() + begin, length, number)) return;
  interval = MillisFromFloat(number);
}

// ─── .speedlim — drop what arrives too soon ──────────────────────────────────

gSpeedlim::gSpeedlim() : gRateLimitBase() {
  Document(
      "Limits the rate of message throughput by **dropping** what arrives too soon. A message in "
      "the left inlet goes straight out the outlet if at least the interval has elapsed since the "
      "last one this object let out, and is discarded if it has not — nothing is stored and "
      "nothing is deferred, so what comes out of a burst is its *first* message, at the moment it "
      "arrived. That is the leading-edge throttle: 'redraw at most 20 times a second', 'one "
      "trigger per beat however hard the pad is hit', 'stop this sensor from flooding the patch'. "
      "Its sibling .qlim is the other answer to the same problem and holds the newest value "
      "instead of dropping it — reach for .qlim whenever losing the last value of a gesture would "
      "leave something set to the wrong number, and for this object whenever a skipped message "
      "costs nothing. Everything passes through as the kind of message it went in as: a bang out "
      "for a bang in, an int for an int, text for text, so the limiter can sit anywhere in a chain "
      "without retyping what flows through it. A number in the right inlet sets the interval in "
      "milliseconds; 0, the Max default, is no limiting at all and every message passes. The first "
      "message after creation always passes, there being no previous output to measure against. "
      "Elapsed time is measured on the patcher's block counter, the same clock the deferred-"
      "message scheduler waits on, so it stops when the engine does and its resolution is one "
      "audio block: two messages arriving in the same dispatch are zero milliseconds apart however "
      "far apart they really were. Dropped messages are counted rather than logged, a log line "
      "being an allocation on whichever thread the message arrived on and that thread routinely "
      "being the audio callback. A standalone object outside any patcher has no clock at all and "
      "passes everything. Note that this is deliberately *not* Max's own speedlim/qlim split: Max "
      "documents the same holding behaviour for both names and separates them by scheduler "
      "priority — qlim being 'an interrupt safe replacement' for Jitter traffic — which is a "
      "distinction a headless patcher with one dispatch model cannot express and which would leave "
      "the two objects identical. Issue #508 gives the two names the two overflow policies "
      "instead. Calculate() does nothing and no message path allocates, locks or blocks.",
      "The message inlet. A bang, int, float or list arriving here goes straight out the outlet "
      "when the interval has elapsed since the last output, and is dropped and counted when it has "
      "not. Max's 'the message is passed out the outlet, provided that a certain minimum time has "
      "elapsed since the previous output' — with this object's answer to the other case, which is "
      "to discard it rather than hold it. A list whose whole text is one number passes as that "
      "number, an int or a float by its spelling, so a .m 5 reaches an int inlet downstream; "
      "anything else is carried whole as text. There are no command words: every message is data.",
      "The messages that got through, unchanged and as the kind of message they went in as. "
      "Silent for anything dropped.",
      "The initial minimum time between outputs in milliseconds — Max's creation argument, whose "
      "documented default is 0: 'if there is no argument, the minimum time is 0 milliseconds', "
      "which limits nothing. The right inlet overwrites it afterwards. A negative value counts as "
      "0.");
}

void gSpeedlim::Blocked(Held kind, int intValue, float floatValue, const std::string* text,
                        int waitMs, YSE::THREAD thread) {
  // The whole object. There is nothing to store and nothing to arm: the message
  // arrived inside the window and is gone, counted on the drop counter so a host
  // can see how much of the stream this object is removing.
  (void)kind;
  (void)intValue;
  (void)floatValue;
  (void)text;
  (void)waitMs;
  (void)thread;
  CountDrop();
}

// ─── .qlim — hold the newest and send it when the window opens ───────────────

gQlim::gQlim() : gRateLimitBase() {
  // The one allocation the object ever performs, and it happens here rather than
  // on an arrival: a held list is assigned into storage that is already long
  // enough, on whichever thread the message came in on.
  text.reserve(TEXT_CAPACITY);

  Document(
      "Limits the rate of message throughput by **holding** the most recent message and sending it "
      "when the window opens. A message in the left inlet goes straight out the outlet if at least "
      "the interval has elapsed since the last one this object let out; if it has not, the message "
      "is kept and sent the moment the interval is up. A newer message arriving while one waits "
      "replaces it, which is Max's usurp attribute in its default state — 'the most recently "
      "received message replaces any currently queued message' — so what comes out of a burst is "
      "its *last* message, one interval after the previous output. That is what makes this the "
      "right object for anything whose value matters: a fader driving a filter cutoff ends up "
      "where the fader stopped rather than wherever the message that happened to fit the window "
      "left it. Its sibling .speedlim is the other answer and drops what arrives too soon instead "
      "— cheaper, since it never defers anything, and correct whenever a skipped message costs "
      "nothing. Everything passes through as the kind of message it went in as: a bang out for a "
      "bang in, an int for an int, text for text. A number in the right inlet sets the interval in "
      "milliseconds; 0, the Max default, is no limiting at all and every message passes straight "
      "through without ever being held. The first message after creation always passes, there "
      "being no previous output to measure against. The wait runs on the patcher's deferred-"
      "message scheduler (issue #628) rather than on the timer thread behind .metro, so holding a "
      "message allocates nothing and takes no lock, and the released message arrives inside the "
      "patcher's own dispatch as one fresh logical event rather than as an unrelated stimulus. "
      "That clock is the block counter, so it stops when the engine does — a paused patch holds "
      "the waiting message where it stands — and its resolution is one audio block. Exactly one "
      "message is held, because usurp says so; this is not .pipe, which queues every value and "
      "delivers all of them. The single slot is pre-allocated with the object and carries up to "
      "256 characters of text, and a message that cannot be held — over-long text, a full "
      "patcher-wide scheduler, or the narrow race with a delivery already in progress — is dropped "
      "and counted rather than sent early or written to the log. A standalone object outside any "
      "patcher has no clock at all and passes everything. Note that this is deliberately not a "
      "reproduction of Max's own speedlim/qlim split: Max documents this same holding behaviour "
      "for both names and separates them by scheduler priority — qlim being 'an interrupt safe "
      "replacement' for Jitter traffic — which a headless patcher with one dispatch model cannot "
      "express and which would leave the two objects identical. Issue #508 gives the two names the "
      "two overflow policies instead, and this one is Max's. Calculate() does nothing and no "
      "message path allocates, locks or blocks.",
      "The message inlet. A bang, int, float or list arriving here goes straight out the outlet "
      "when the interval has elapsed since the last output, and is held until it has when it has "
      "not — Max's 'otherwise, the message is held until that amount of time has passed (or until "
      "it is overwritten by another incoming message)'. Only the newest waiting message survives, "
      "which is Max's usurp; a message replaced this way is not counted as dropped, that being "
      "usurp working rather than a refusal. A list whose whole text is one number is carried as "
      "that number, an int or a float by its spelling, so a .m 5 reaches an int inlet downstream; "
      "anything else is carried whole as text, up to 256 characters, beyond which it cannot be "
      "held and is refused. There are no command words: every message is data.",
      "The messages that got through, unchanged and as the kind of message they went in as. A held "
      "message arrives one interval after the previous output, inside the patcher's own dispatch.",
      "The initial minimum time between outputs in milliseconds — Max's creation argument, whose "
      "documented default is 0: 'if there is no argument, the minimum time is 0 milliseconds', "
      "which limits nothing and holds nothing. The right inlet overwrites it afterwards. A "
      "negative value counts as 0. A message already waiting keeps the deadline it was held with.");
}

void gQlim::Blocked(Held kind, int intValue_, float floatValue_, const std::string* text_,
                    int waitMs, YSE::THREAD thread) {
  (void)thread;
  messageScheduler* scheduler = Scheduler();
  // Unreachable in practice — a standalone object never gets here, having no
  // clock and so no closed window — but a hold with nothing to arm it on would
  // be a message that never comes out.
  if (scheduler == nullptr) {
    CountDrop();
    return;
  }

  // Longer than the slot holds. Refused rather than truncated (a message this
  // object silently shortened would be a different message) and rather than
  // allocated for, since this may be the audio callback.
  if (kind == Held::LIST && (text_ == nullptr || text_->size() > TEXT_CAPACITY)) {
    CountDrop();
    return;
  }

  // FREE is a fresh hold and ARMED is an usurp; CLAIMED or FIRING means another
  // thread is writing the slot or a delivery is sending it, and this message is
  // dropped rather than made to spin on a path the audio callback takes. See the
  // class notes.
  std::uint8_t state = slot.load(std::memory_order_acquire);
  if (state != STATE_FREE && state != STATE_ARMED) {
    CountDrop();
    return;
  }
  const bool usurped = state == STATE_ARMED;
  if (!slot.compare_exchange_strong(state, STATE_CLAIMED, std::memory_order_acq_rel,
                                    std::memory_order_relaxed)) {
    CountDrop();
    return;
  }

  // Written under CLAIMED, which no other thread will move: the payload is never
  // touched concurrently.
  held = kind;
  intValue = intValue_;
  floatValue = floatValue_;
  if (kind == Held::LIST) text.assign(*text_);

  slot.store(STATE_ARMED, std::memory_order_release);

  // An usurp keeps the deadline the replaced message was armed with, which is
  // what makes the output land one interval after the *previous output* rather
  // than one interval after the last thing a flood happened to send.
  if (usurped) return;

  const messageScheduler::Handle handle = scheduler->ScheduleBang(this, 0, waitMs);
  if (handle == 0) {
    // The patcher-wide pending set is full. Give the slot back — unless a
    // concurrent arrival has usurped it in the meantime, in which case that
    // message is now waiting on nothing and goes with it.
    std::uint8_t expected = STATE_ARMED;
    if (slot.compare_exchange_strong(expected, STATE_FREE, std::memory_order_acq_rel,
                                     std::memory_order_relaxed)) {
      CountDrop();
    }
  }
}

void gQlim::DeliverDeferred(const deferredMessage& msg, YSE::THREAD thread) {
  (void)msg;
  // The window has opened. The scheduler wraps this in a fresh
  // messageEventScope, so everything the released message goes on to cause is
  // one logical event (#628) — the causal chain the message came in on, resumed
  // rather than replaced, which is the whole reason this object does not use
  // TimerThread.
  std::uint8_t expected = STATE_ARMED;
  if (!slot.compare_exchange_strong(expected, STATE_FIRING, std::memory_order_acq_rel,
                                    std::memory_order_relaxed)) {
    return;
  }

  // The tag is passed straight through, as `.delay` and `.pipe` pass it: T_GUI
  // means "let the block's own traversal render what this caused", which is the
  // right reading for an outlet send.
  Emit(held, intValue, floatValue, &text, thread);
  // Freed only once the send has finished, so nothing can overwrite the text the
  // send is reading.
  slot.store(STATE_FREE, std::memory_order_release);
}
