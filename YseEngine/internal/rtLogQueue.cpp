/*
  ==============================================================================

    rtLogQueue.cpp
    Created: 2026-08-12

    See rtLogQueue.h for the design (issue #546).

  ==============================================================================
*/

#include "rtLogQueue.h"

#include <cstring>
#include <string>

#include "../implementations/logImplementation.h"

YSE::INTERNAL::rtLogQueue& YSE::INTERNAL::RtLog() {
  static rtLogQueue impl;
  return impl;
}

bool YSE::INTERNAL::rtLogQueue::post(const char* text, std::size_t length) {
  if (text == nullptr) return false;

  // Built on the caller's stack and copied in whole: no heap on this path, on
  // any thread. A line longer than the record is cut here rather than refused —
  // a producer that wants a visible marker on the cut adds it before calling.
  line record;
  const std::size_t take = length < kLineCapacity ? length : kLineCapacity;
  if (take > 0) std::memcpy(record.text, text, take);
  record.text[take] = '\0';

  if (queue_.try_push(record)) return true;

  // Full. Refused rather than blocked or grown; the count is what the next
  // drain turns into a visible report.
  dropped_.fetch_add(1, std::memory_order_relaxed);
  return false;
}

std::size_t YSE::INTERNAL::rtLogQueue::drain() {
  std::size_t emitted = 0;

  line record;
  while (queue_.try_pop(record)) {
    // E_APP_MESSAGE is what YSE::Log().sendMessage() already uses for
    // application-level text: it is grouped with the errors, so it survives
    // every level above EL_NONE rather than disappearing in a release build
    // where the default level is EL_ERROR. A debugging print that only worked
    // in debug builds would be the wrong half of the feature.
    LogImpl().emit(E_APP_MESSAGE, record.text);
    emitted++;
  }

  // One report per burst, not one per lost line: `reported_` remembers how much
  // of the loss has already been named.
  const std::uint64_t lost = dropped_.load(std::memory_order_relaxed);
  const std::uint64_t seen = reported_.load(std::memory_order_relaxed);
  if (lost > seen) {
    reported_.store(lost, std::memory_order_relaxed);
    // E_APP_MESSAGE and not E_WARNING, for two reasons that point the same way.
    // It belongs in the *same stream* as the lines it is accounting for, so it
    // reads as the hole it is describing rather than as an unrelated engine
    // complaint arriving somewhere else. And E_WARNING sorts above
    // E_WARNING_MESSAGES, so `emit` drops it at the EL_ERROR level the engine
    // runs at by default — a loss report that only appeared once the log level
    // had been raised would be missing exactly when the log is being read to
    // find out what happened.
    LogImpl().emit(E_APP_MESSAGE, "real-time log queue overflow: " + std::to_string(lost - seen) +
                                      " line(s) dropped");
  }

  // Bumped unconditionally: it is the producers' "the control thread has been
  // past" signal, and a tick in which nothing was waiting is still a tick.
  tick_.fetch_add(1, std::memory_order_release);
  return emitted;
}
