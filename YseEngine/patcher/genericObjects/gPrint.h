#pragma once
#include "../pObject.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief `.print` — send whatever arrives to the engine log, with a name in
     *         front of it (issue #546). Max's `print`.
     *
     *  ### What it is for
     *
     *  Max: "print any message in the Max window", and "the `print` object
     *  prints, in the Max window, any message it receives in its inlet."
     *
     *  This patcher is headless and there is no Max window, so the destination
     *  is the engine log — the thing a host already reads, already redirects
     *  with `YSE::Log().setLogfile()`, and already intercepts with
     *  `YSE::Log().setHandler()`. Everything a host has built to watch the
     *  engine talk therefore watches this object too, with no new plumbing at
     *  either end.
     *
     *  It is the patcher's missing debugging affordance and the reason issue
     *  #546 calls it the highest-value one left: until it existed there was no
     *  way to see what was happening *inside* a graph. A patch that misbehaved
     *  had to be diagnosed from the audio coming out of it.
     *
     *  ### It is not `.capture`
     *
     *  `.capture` (#496) is the other half of the same job and the two are
     *  spelled against each other in both headers. This says what is passing
     *  **now**, one line at a time, as it passes; `.capture` keeps the last N
     *  values so a patch can be run, stopped, and only then asked what it saw.
     *  A trace you have to read while it scrolls and a trace you read afterwards
     *  answer different questions, which is why Max has both.
     *
     *  ### The name, and the second argument Max does not have
     *
     *  Max: "the first argument sets the name that appears in the Max window
     *  when the object prints something. The default name is `print`." So
     *  `.print bass` prefixes every line with `bass: `, which is what makes two
     *  of these in one patch tellable apart.
     *
     *  The **second** argument is this patcher's and not Max's: the most lines
     *  this object will emit per drain tick, default `DEFAULT_LINES_PER_TICK`.
     *  Max can afford not to have one because Max's `print` runs in Max's
     *  scheduler; here a `.print` wired into a per-block path is being driven by
     *  the audio callback, and at 44.1 kHz with 512-sample blocks that is 86
     *  lines a second from *one* object before a patch does anything unusual.
     *  Without a budget the first such patch fills the queue and every other
     *  `.print` in the patch goes dark — so the limit is what keeps one noisy
     *  object from silencing the rest. It is clamped to
     *  [`MIN_LINES_PER_TICK`, `MAX_LINES_PER_TICK`]; a non-numeric second
     *  argument leaves the default in place rather than throwing, since a
     *  creation argument arriving from a saved patch must never be able to break
     *  loading it.
     *
     *  ### What a line looks like
     *
     *  `<name>: <message>`, and the message is what arrived:
     *
     *  - a bang prints `bang` — Max's `print` "prints the word bang";
     *  - an int prints its digits, a float its shortest round-tripping
     *    spelling with a decimal point, so a float stays visibly a float;
     *  - a list prints its text verbatim, exactly as the cord carried it;
     *  - a message box's text (the `SetMessage` path) prints verbatim too,
     *    which is Max's `anything` method.
     *
     *  A line longer than the queue's record is cut and marked with a trailing
     *  `...`. That is the opposite of `.capture`'s refuse-whole rule, and
     *  deliberately: `.capture` stores a *symbol*, where half of one is a
     *  different symbol, while this produces a *line to read*, where the first
     *  240 characters are almost all of the information and silence is none of
     *  it.
     *
     *  ### Real-time behaviour — the whole of the design
     *
     *  A message handler runs on whichever thread dispatched the message, and
     *  in-patcher delivery dispatches on `T_DSP` — routinely the audio callback.
     *  Writing the log from there is exactly what the engine's real-time rules
     *  forbid: `logImplementation` builds a std::string and then either writes a
     *  file or calls into host code that may do anything at all.
     *
     *  So the object never logs. It **formats into a stack buffer and pushes**
     *  onto `INTERNAL::RtLog()`, the bounded lock-free queue whose control-thread
     *  drain hands the line to the log — `NamedBus`'s `T_DSP` path for values,
     *  with text in the record instead of a variant. The drain runs from
     *  `system::update()`, so a line reaches the log on the host's next tick.
     *  Nothing on the message path allocates, locks, or blocks; `Calculate()`
     *  does nothing at all.
     *
     *  The queue is a function-local static, so the *first* touch of it
     *  allocates its ring. The constructor touches it once on the control
     *  thread for exactly that reason — an object that only reached the queue
     *  from a render thread would allocate there the first time it printed.
     *
     *  ### Flooding, and why the drops are visible
     *
     *  Two bounds, and both report:
     *
     *  - **This object's budget**: at most `LinesPerTick()` lines between two
     *    drains. The first message refused in a tick posts one line saying so,
     *    so a rate-limited `.print` reads as a marked gap rather than as a
     *    working object that has quietly stopped. `Dropped()` counts every
     *    refusal for diagnostics and tests.
     *  - **The queue's capacity**: shared by every producer in the process. A
     *    push that finds it full is refused, counted, and named by the next
     *    drain in a single overflow line.
     *
     *  Dropping under flood is the correct answer — the alternative is blocking
     *  the audio thread, which is not an answer — but a silent drop would make
     *  the object lie about the patch it is being used to debug, which is the
     *  one thing a debugging instrument must never do.
     *
     *  The budget is enforced with relaxed atomics and is deliberately
     *  *approximate*: two threads printing into the same object in the same tick
     *  may between them get one or two lines past the limit. Making it exact
     *  would need a read-modify-write per message on a path whose whole purpose
     *  is to be cheap, to buy an accuracy nobody can use — the limit is a flood
     *  guard, not a quota.
     *
     *  ### What persists
     *
     *  The creation arguments, because they are creation arguments. Nothing
     *  else: the object holds no state a patch can read back, and the lines it
     *  has already sent belong to the log rather than to the patch.
     */
    PATCHER_CLASS(gPrint, YSE::OBJ::G_PRINT)
    _DO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(BangIn)
    _INT_IN(IntIn)
    _FLOAT_IN(FloatIn)
    _LIST_IN(ListIn)

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief The name in front of every line when no argument gives one —
     *         Max's "the default name is `print`". */
    static constexpr const char* DEFAULT_NAME = "print";

    /** @brief Longest name that is used as given. A longer one is cut, since
     *         the whole line has to fit a fixed record and a name that ate it
     *         would leave no room for the message it is labelling. */
    static constexpr std::size_t NAME_CAPACITY = 32;

    /** @brief Lines per drain tick with no second argument. Enough for a
     *         control-rate patch to print freely and far below what a
     *         per-block path would produce. */
    static constexpr int DEFAULT_LINES_PER_TICK = 16;

    /** @brief Fewest lines a second argument can ask for. Zero is not offered:
     *         an object that printed nothing is a deleted object, spelled
     *         confusingly. */
    static constexpr int MIN_LINES_PER_TICK = 1;

    /** @brief Most lines a second argument can ask for — the queue's own
     *         capacity, since asking for more than fits between two drains
     *         cannot buy anything. */
    static constexpr int MAX_LINES_PER_TICK = 256;

    /** @brief The name this object puts in front of every line. */
    const std::string& Name() const {
      return name;
    }

    /** @brief This object's budget, in lines per drain tick. */
    int LinesPerTick() const {
      return linesPerTick;
    }

    /** @brief Messages this object refused — the budget and a full queue
     *         together. Monotonic, readable from any thread; diagnostics and
     *         tests. The log says the same thing, which is the point. */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief Lines this object has handed to the queue. Diagnostics and
     *         tests; what reaches the log is this minus whatever the queue
     *         itself had to refuse. */
    std::uint64_t Posted() const {
      return posted.load(std::memory_order_relaxed);
    }

  private:
    // Format `<name>: <body>` into the queue's record size and push it. The one
    // path every inlet ends at; takes the budget first and reports its own
    // refusals. RT-safe on every branch.
    void Post(const char* body, std::size_t bodyLength);

    // Push a line that is exempt from the budget — currently only the notice
    // that the budget has bitten, which would otherwise be the message the
    // budget refuses. RT-safe.
    void PostRaw(const char* body, std::size_t bodyLength);

    // Claim one line of this tick's budget, rolling the counter when the drain
    // has been past since the last message. False when the budget is spent, in
    // which case the caller drops. Approximate under concurrency on purpose —
    // see the class notes. RT-safe: three relaxed atomics.
    bool ClaimBudget();

    // Whichever of `linesPerTick` and `MIN/MAX_LINES_PER_TICK` the creation
    // argument lands between.
    static int ClampLines(int asked);

    // The name, from creation argument 1. Control thread only after
    // construction: a live SetParams re-parse rebuilds the object rather than
    // writing this, because Parameters::NeedsRebuild() is true for any object
    // holding a LIST parameter. That is what makes reading it from a render
    // thread safe.
    std::string name = DEFAULT_NAME;

    // The budget, from creation argument 2. Written on the same control-thread
    // path as `name` and read the same way.
    int linesPerTick = DEFAULT_LINES_PER_TICK;

    // The drain tick `usedThisTick` belongs to. A message that finds them
    // disagreeing is the first of a new tick and resets the counter. 0 is the
    // "never seen a tick" sentinel; rtLogQueue::tick() starts at 1.
    std::atomic<std::uint64_t> budgetTick{0};
    std::atomic<int> usedThisTick{0};

    // The tick whose budget notice has already been posted, so a flood costs
    // one notice per tick rather than one per refused message. Claimed with a
    // single exchange, so exactly one caller posts it.
    std::atomic<std::uint64_t> noticeTick{0};

    std::atomic<std::uint64_t> dropped{0};
    std::atomic<std::uint64_t> posted{0};

    // The creation arguments, as tokens. Control thread only: written by
    // Parameters::Set, read by ParseParams(), never by a message handler.
    std::vector<std::string> creationArgs;
  };

} // namespace PATCHER
} // namespace YSE
