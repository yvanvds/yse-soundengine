/*
  ==============================================================================

    rtLogQueue.h
    Created: 2026-08-12

    Real-time on-ramp to the engine log (issue #546).

  ==============================================================================
*/

#ifndef RT_LOG_QUEUE_H_INCLUDED
#define RT_LOG_QUEUE_H_INCLUDED

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "../utils/mpmcQueue.hpp"

namespace YSE {
  namespace INTERNAL {

    /**
     *  @brief A bounded lock-free way onto the engine log for code that may be
     *         running on a render thread (issue #546).
     *
     *  ### Why this exists, and why it is not a second log
     *
     *  The engine already has exactly one place log lines go —
     *  `logImplementation`, which writes the log file or hands the line to a
     *  host-installed `logHandler`. Both of those allocate a std::string and one
     *  of them does file I/O, so neither may be reached from the audio callback.
     *
     *  This is not another channel; it is the *on-ramp* to that one. A producer
     *  copies its line into a fixed-size record and pushes it onto a bounded
     *  lock-free queue; `drain()` runs on the control thread — once per
     *  `system::update()` tick — and hands every line it finds to
     *  `logImplementation::emit(E_APP_MESSAGE, ...)`, the same entry point
     *  `YSE::Log().sendMessage()` uses for application-level text. Everything
     *  downstream of the drain (the level filter, the log file, the host
     *  handler, the Android sink) is therefore unchanged and unaware.
     *
     *  This is the pattern `NamedBus`'s `T_DSP` path already uses for values;
     *  the difference is that a log line is text, so the record carries the
     *  characters inline rather than a variant.
     *
     *  ### The bound, and what happens at it
     *
     *  `kQueueCapacity` lines fit between two drains. A `post()` that finds the
     *  queue full **fails and says so** rather than blocking, allocating or
     *  growing — those are the three things the audio thread may not do, and a
     *  log call is never worth any of them. Dropping is counted, and the next
     *  drain emits one line naming how many were lost since the last report, so
     *  a flood shows up in the log as a hole with a number on it rather than as
     *  silence.
     *
     *  The bound is also the process-wide *rate limit*: at most
     *  `kQueueCapacity` lines per drain tick reach the log, whatever the patch
     *  does. A producer that wants a tighter, per-object budget builds it on
     *  `tick()` — see `.print`.
     *
     *  ### Threads
     *
     *  `post()` is safe from any thread, including the audio callback: it copies
     *  into a stack-free fixed record and pushes onto a `mpmcQueue`, so it
     *  neither allocates, locks nor blocks. `drain()` is control-thread only —
     *  it is the one that allocates and writes.
     *
     *  One caveat producers must respect: `RtLog()` is a function-local static,
     *  so the *first* call to it constructs the queue and allocates its ring.
     *  Anything that can post from a render thread must therefore touch
     *  `RtLog()` once on the control thread first — `.print` does it in its
     *  constructor.
     */
    class rtLogQueue {
    public:
      /** @brief Longest line a record can carry, in characters, terminator
       *         excluded. Chosen so one record plus the queue cell's sequence
       *         counter is exactly four cache lines. */
      static constexpr std::size_t kLineCapacity = 247;

      /** @brief Lines that fit between two drains. Also the process-wide
       *         ceiling on log lines per `system::update()` tick. */
      static constexpr std::size_t kQueueCapacity = 256;

      /**
       *  @brief Queue one line for the log. Safe on any thread, audio callback
       *         included: no allocation, no lock, no I/O.
       *
       *  @param text    the line; need not be NUL-terminated, @p length decides.
       *  @param length  characters to take. Anything past `kLineCapacity` is
       *                 truncated — a producer that cares about the tail should
       *                 shorten it itself and mark the cut, the way `.print`
       *                 does.
       *  @return false when the queue was full, in which case the line is lost
       *          and counted in `dropped()`.
       */
      bool post(const char* text, std::size_t length);

      /**
       *  @brief Hand every queued line to the engine log, then report anything
       *         dropped since the last call. Control thread only — this is the
       *         end of the queue that allocates.
       *
       *  Also bumps `tick()`, whether or not anything was waiting.
       *
       *  @return how many lines were emitted, the overflow report excluded.
       */
      std::size_t drain();

      /**
       *  @brief Monotonic drain counter, bumped once per `drain()`.
       *
       *  A producer uses it as a coarse clock with no clock in it: "has the
       *  control thread been past since I last looked?" is the only question a
       *  per-producer rate limit needs, and answering it costs one relaxed
       *  load. Starts at 1, so 0 is safe as "never seen a tick".
       */
      std::uint64_t tick() const {
        return tick_.load(std::memory_order_relaxed);
      }

      /** @brief Lines lost to a full queue since the process started.
       *         Monotonic; diagnostics and tests. */
      std::uint64_t dropped() const {
        return dropped_.load(std::memory_order_relaxed);
      }

      /** @brief Lines that fit at once — `kQueueCapacity` rounded up to the
       *         queue's power of two. */
      std::size_t capacity() const {
        return queue_.capacity();
      }

    private:
      // One line, inline. A fixed footprint is what lets the record ride the
      // lock-free queue by value, which is what keeps `post()` free of the heap.
      struct line {
        char text[kLineCapacity + 1];
      };

      YSE::mpmcQueue<line> queue_{kQueueCapacity};

      // Lines lost to a full queue, and how many of those have already been
      // named in a report. The difference is what the next drain prints, so a
      // burst is reported once rather than once per lost line.
      std::atomic<std::uint64_t> dropped_{0};
      std::atomic<std::uint64_t> reported_{0};

      // See tick(). Starts at 1 so a producer's "never seen a tick" sentinel
      // can be 0.
      std::atomic<std::uint64_t> tick_{1};
    };

    /** @brief The process-wide queue. First call constructs it and allocates
     *         its ring — see the class notes on why that call must not be the
     *         audio thread's. */
    rtLogQueue& RtLog();

  } // namespace INTERNAL
} // namespace YSE

#endif // RT_LOG_QUEUE_H_INCLUDED
