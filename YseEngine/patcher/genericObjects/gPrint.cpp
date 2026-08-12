// `.print` (issue #546). See gPrint.h for the design; this file is the line
// formatting, the per-object budget, and the hand-off to the engine log's
// real-time on-ramp.
#include "gPrint.h"

#include <cstring>

#include "../../internal/rtLogQueue.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gPrint

namespace {

  // The word a bang prints — Max's `print` "prints the word bang".
  constexpr char kBangText[] = "bang";
  constexpr std::size_t kBangLength = sizeof(kBangText) - 1;

  // What is written when the message did not fit the record. Three characters,
  // which is why the cut leaves room for exactly three.
  constexpr std::size_t kEllipsisLength = 3;

  constexpr char kInletDoc[] =
      "Everything that arrives here is written to the engine log as one line, prefixed with this "
      "object's name — Max's 'the print object prints, in the Max window, any message it receives "
      "in its inlet'. There is no Max window here, so the destination is the log a host already "
      "reads, redirects with YSE::Log().setLogfile() and intercepts with YSE::Log().setHandler(). "
      "A bang prints the word 'bang'; an int prints its digits; a float prints its shortest "
      "spelling that reads back as the same float, always with a decimal point, so a float stays "
      "visibly a float; a list and a message box's text print verbatim, exactly as the cord "
      "carried them. Nothing is ever sent onward — this object has no outlet, which is Max's "
      "shape. Nothing is logged from here either: the line is formatted into a stack buffer and "
      "pushed onto a bounded lock-free queue, because a message handler runs on whichever thread "
      "dispatched the message and in-patcher delivery dispatches on the audio thread. The line "
      "reaches the log on the host's next System::update() tick. A line longer than the record is "
      "cut and marked with a trailing '...'. Two bounds can refuse a message and both say so: "
      "this object's own budget of lines per drain tick, which posts one notice the first time it "
      "bites in a tick, and the shared queue's capacity, whose overflow the next drain reports as "
      "a single count. Dropping under flood is the only alternative to blocking the audio thread, "
      "but a silent drop would make a debugging instrument lie about the patch it is being used "
      "to debug.";

  constexpr char kNameDoc[] =
      "Max's argument list, in Max's order. The first token is the name printed in front of every "
      "line — Max's 'the first argument sets the name that appears in the Max window when the "
      "object prints something. The default name is print'. It is what makes two of these in one "
      "patch tellable apart. A name longer than 32 characters is cut, since the whole line has to "
      "fit a fixed record and a name that ate it would leave no room for the message it is "
      "labelling.";

  constexpr char kLimitDoc[] =
      "The most lines this object emits between two drain ticks. This one is the YSE patcher's "
      "and not Max's: Max's print runs in Max's scheduler, while a .print wired into a per-block "
      "path here is driven by the audio callback, which at 44.1 kHz and 512-sample blocks is 86 "
      "lines a second from one object before a patch does anything unusual. The queue is shared, "
      "so without a budget the first such object fills it and every other .print in the patch "
      "goes dark; the limit is what keeps one noisy object from silencing the rest. Clamped to "
      "1-256 — 256 being the queue's own capacity, past which asking for more cannot buy "
      "anything. A non-numeric second argument leaves the default of 16 standing rather than "
      "failing, since a creation argument arriving from a saved patch must never be able to break "
      "loading it. Refused messages are counted and the first refusal in a tick posts a notice, "
      "so a budget that bites is visible rather than silent.";

} // namespace

CONSTRUCT() {
  // The name and the budget are built by ParseParams, so a saved `.print bass 4`
  // comes back as that object. The clear callback is what makes `SetParams("")`
  // return it to Max's no-argument shape rather than leaving the previous name
  // standing.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(creationArgs);

  // One inlet, as Max has: every message and every value arrives here.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_INT_IN(IntIn);
  REG_FLOAT_IN(FloatIn);
  REG_LIST_IN(ListIn);

  // No outlets. Max's print has none, and an outlet would turn a probe into a
  // participant: a patch that had to wire this object's output somewhere could
  // no longer be left in place once it was working.

  // Build the queue here, on the control thread, rather than letting the first
  // line this object posts allocate its ring on a render thread. One call, and
  // the only reason it is in the constructor.
  (void)YSE::INTERNAL::RtLog().capacity();

  ADD_DESCRIPTION(
      "Writes whatever arrives at its inlet to the engine log, one line per message, with a "
      "settable name in front of it — Max's 'print', which prints 'any message in the Max "
      "window'. Max: 'the print object prints, in the Max window, any message it receives in its "
      "inlet.' This patcher is headless and there is no Max window, so the destination is the "
      "engine log: the thing a host already reads, already redirects with "
      "YSE::Log().setLogfile(), and already intercepts with YSE::Log().setHandler(), so every "
      "tool a host has for watching the engine talk watches this object too with no new plumbing "
      "at either end. It is the patcher's missing debugging affordance — until it existed there "
      "was no way to see what was happening inside a graph, and a patch that misbehaved had to be "
      "diagnosed from the audio coming out of it. It is not .capture, the other half of the same "
      "job: this says what is passing now, one line at a time, as it passes, while .capture keeps "
      "the last N values so a patch can be run, stopped, and only then asked what it saw. The "
      "first creation argument is the name — Max's 'the first argument sets the name that appears "
      "in the Max window', default 'print'. The second is this patcher's and not Max's: the most "
      "lines the object emits per drain tick, default 16, clamped to 1-256. A bang prints the "
      "word 'bang', an int its digits, a float its shortest round-tripping spelling with a "
      "decimal point so it stays visibly a float, and a list or a message box's text verbatim. "
      "There is no outlet, which is Max's shape and also the right one: an outlet would turn a "
      "probe into a participant. Nothing is ever logged from the message path. A handler runs on "
      "whichever thread dispatched the message and in-patcher delivery dispatches on the audio "
      "thread, where building a std::string and then writing a file or calling into a host "
      "handler is exactly what the engine's real-time rules forbid — so the line is formatted "
      "into a stack buffer and pushed onto a bounded lock-free queue whose control-thread drain, "
      "running from System::update(), hands it to the log. That is the named bus's audio-thread "
      "path with text in the record instead of a variant, and it means a line reaches the log on "
      "the host's next tick rather than instantly. Nothing on any message path allocates, locks "
      "or blocks, and Calculate() does nothing. Two bounds can refuse a message and both report: "
      "this object's per-tick budget, whose first refusal in a tick posts a notice and whose "
      "refusals are all counted, and the shared queue's capacity, whose overflow the next drain "
      "names as a single count. Dropping under flood is correct — the alternative is blocking the "
      "audio thread, which is not an answer — but a silent drop would make a debugging instrument "
      "lie about the patch it is being used to debug. A line longer than the record is cut and "
      "marked with '...', the opposite of .capture's refuse-whole rule and deliberately so: "
      ".capture stores a symbol, where half of one is a different symbol, while this produces a "
      "line to read, where the first 240 characters are almost all of the information and silence "
      "is none of it. Only the creation arguments persist across a save; the lines already sent "
      "belong to the log rather than to the patch.");
  ADD_CATEGORY(pCategory::GENERIC);

  INLET_DOC(0, "in", kInletDoc, "any");
  PARAM_DOC("name", "print", kNameDoc, "any symbol, up to 32 characters");
  PARAM_DOC("lines", "16", kLimitDoc, "1-256");
}

// ─── creation arguments ───────────────────────────────────────────────────────

int gPrint::ClampLines(int asked) {
  if (asked < MIN_LINES_PER_TICK) return MIN_LINES_PER_TICK;
  if (asked > MAX_LINES_PER_TICK) return MAX_LINES_PER_TICK;
  return asked;
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave Max's
  // no-argument object behind rather than one still holding the previous name.
  creationArgs.clear();
  name = DEFAULT_NAME;
  linesPerTick = DEFAULT_LINES_PER_TICK;
}

PARM_PARSE() {
  name = DEFAULT_NAME;
  linesPerTick = DEFAULT_LINES_PER_TICK;

  // Parameters::Set splits on single spaces, so a run of them yields empty
  // tokens; an empty token names nothing and is not a number.
  std::size_t taken = 0;
  for (const std::string& token : creationArgs) {
    if (token.empty()) continue;

    if (taken == 0) {
      // Max's name argument. Cut rather than refused: a name is a label, and a
      // label that was too long is still the label the author meant.
      name = token.size() > NAME_CAPACITY ? token.substr(0, NAME_CAPACITY) : token;
      taken++;
      continue;
    }

    // The budget. Read strictly — ReadNumericToken answers "is this token a
    // number at all", where a loose reader would take `4voices` as 4 — and a
    // token that is not one leaves the default standing rather than throwing,
    // which is what stops a hand-edited or newer saved patch from failing to
    // load.
    float number = 0.f;
    if (ReadNumericToken(token, number)) {
      linesPerTick = ClampLines(ExprToInt(number));
    }
    break;
  }
}

// ─── the line ─────────────────────────────────────────────────────────────────

bool gPrint::ClaimBudget() {
  const std::uint64_t now = YSE::INTERNAL::RtLog().tick();

  // A message that finds the counter tagged with an older tick is the first of
  // a new one. Rolling it here rather than from the drain is what keeps the
  // drain from having to know which objects exist.
  if (budgetTick.load(std::memory_order_relaxed) != now) {
    budgetTick.store(now, std::memory_order_relaxed);
    usedThisTick.store(0, std::memory_order_relaxed);
  }

  // Load, compare, store — deliberately not a fetch_add. Two threads printing
  // into the same object in the same tick may between them get a line or two
  // past the limit, which is a flood guard behaving approximately rather than a
  // quota being violated; a read-modify-write per message would buy an accuracy
  // nobody can use on a path whose whole purpose is to be cheap. It also cannot
  // run away: the counter is never incremented once it has reached the limit,
  // so it can never overflow however long the flood lasts.
  const int used = usedThisTick.load(std::memory_order_relaxed);
  if (used >= linesPerTick) return false;
  usedThisTick.store(used + 1, std::memory_order_relaxed);
  return true;
}

void gPrint::PostRaw(const char* body, std::size_t bodyLength) {
  // The whole line on the stack. Nothing here allocates, which is the point:
  // this runs on whichever thread the message arrived on.
  char line[YSE::INTERNAL::rtLogQueue::kLineCapacity + 1];
  std::size_t written = 0;

  const std::size_t nameLength = name.size() < NAME_CAPACITY ? name.size() : NAME_CAPACITY;
  if (nameLength > 0) {
    std::memcpy(line, name.data(), nameLength);
    written = nameLength;
  }
  line[written++] = ':';
  line[written++] = ' ';

  // At most NAME_CAPACITY + 2 characters are spoken for, so there is always
  // room for a body and for the ellipsis that marks a cut one.
  const std::size_t room = YSE::INTERNAL::rtLogQueue::kLineCapacity - written;
  if (bodyLength <= room) {
    if (bodyLength > 0) std::memcpy(line + written, body, bodyLength);
    written += bodyLength;
  } else {
    const std::size_t keep = room - kEllipsisLength;
    std::memcpy(line + written, body, keep);
    written += keep;
    line[written++] = '.';
    line[written++] = '.';
    line[written++] = '.';
  }
  line[written] = '\0';

  if (YSE::INTERNAL::RtLog().post(line, written)) {
    posted.fetch_add(1, std::memory_order_relaxed);
  } else {
    // The queue was full. It counts and reports the loss itself; this counter is
    // what lets a test ask *this* object how much it lost.
    dropped.fetch_add(1, std::memory_order_relaxed);
  }
}

void gPrint::Post(const char* body, std::size_t bodyLength) {
  if (ClaimBudget()) {
    PostRaw(body, bodyLength);
    return;
  }

  dropped.fetch_add(1, std::memory_order_relaxed);

  // One notice per tick, claimed with a single exchange so exactly one caller
  // writes it however many threads are flooding. Exempt from the budget it is
  // reporting on, which it would otherwise be the first casualty of.
  const std::uint64_t now = YSE::INTERNAL::RtLog().tick();
  if (noticeTick.exchange(now, std::memory_order_relaxed) == now) return;

  // 20 characters of head, at most FORMAT_INT_WIDTH of number and 36 of tail —
  // 67, so 96 is room and then some.
  char notice[96];
  std::size_t n = 0;
  const char head[] = "rate limit reached (";
  std::memcpy(notice, head, sizeof(head) - 1);
  n += sizeof(head) - 1;
  n += WriteInt(linesPerTick, notice + n);
  const char tail[] = " per tick); further messages dropped";
  std::memcpy(notice + n, tail, sizeof(tail) - 1);
  n += sizeof(tail) - 1;

  PostRaw(notice, n);
}

// ─── inlets ───────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  (void)thread;
  // Max's print "prints the word bang".
  Post(kBangText, kBangLength);
}

INT_IN(IntIn) {
  (void)inlet;
  (void)thread;
  char digits[FORMAT_INT_WIDTH];
  const std::size_t n = WriteInt(value, digits);
  Post(digits, n);
}

FLOAT_IN(FloatIn) {
  (void)inlet;
  (void)thread;
  // ExprFormatValue rather than std::to_string: it neither allocates nor reads
  // locale state, and it gives a float the shortest spelling that reads back as
  // the same float — so 0.3f prints as "0.3" rather than "0.300000012" — while
  // always carrying a decimal point, so a float stays visibly a float.
  char text[kExprValueTextMax];
  const int n = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  Post(text, (std::size_t)n);
}

LIST_IN(ListIn) {
  (void)inlet;
  (void)thread;
  // Verbatim: the cord carried text, and a trace that re-spelled what it saw
  // would answer a different question from the one the patch asked.
  Post(value.c_str(), value.size());
}

MESSAGES() {
  (void)value;
  // Max's `anything` method — a message box's text arrives here rather than at
  // the list handler, because gMessage sends through outlet::SendMessage.
  Post(message.c_str(), message.size());
}

#undef className
