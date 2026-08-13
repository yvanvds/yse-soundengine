/*
  ==============================================================================

    logImplementation.h
    Created: 28 Jan 2014 4:13:48pm
    Author:  yvan

  ==============================================================================
*/

#ifndef LOGIMPLEMENTATION_H_INCLUDED
#define LOGIMPLEMENTATION_H_INCLUDED

#include "../headers/enums.hpp"
#include "../headers/types.hpp"
#include "../log.hpp"
#include <atomic>
#include <fstream>
#include <mutex>
#include <string>

namespace YSE {
  namespace INTERNAL {
    /**
     *  @brief The engine's one log sink: a log file, or a host-installed
     *         `logHandler` that replaces it.
     *
     *  ### Threading (issue #820)
     *
     *  Every engine thread that can report something reaches this object — the
     *  control thread, the slow-pool file loader, the MIDI hub, the render
     *  workers on their startup path. `logMessage()` used to write the shared
     *  `std::ofstream` (or call the handler) with nothing holding them apart,
     *  so two threads could interleave inside the same `filebuf` — TSan caught
     *  the loader and the control thread in one, and a host handler with any
     *  state of its own (a test's recording vector, say) was corrupted the same
     *  way. `sinkMutex` serialises the whole delivery, including the call into
     *  the handler, so one line is one line whoever emits it.
     *
     *  Two consequences worth stating:
     *
     *  - A `logHandler` is called with the mutex held, and therefore **must not
     *    log back** from `AddMessage()` — a handler that re-enters the log
     *    deadlocks. It is already the case that such a handler recurses
     *    forever, so this narrows nothing in practice.
     *  - The lock is not on the audio callback path, and taking it does not put
     *    it there. Nothing on the callback reaches this class: an RT producer
     *    goes through `rtLogQueue`, whose `drain()` runs on the control thread
     *    (issue #546), precisely because formatting and writing a line
     *    allocates and does I/O. That invariant is what makes a mutex the right
     *    answer here rather than a second lock-free hop.
     */
    class logImplementation {
    public:
      ERROR_LEVEL getLevel();
      void setLevel(ERROR_LEVEL value);
      void setHandler(logHandler* handler);
      const std::string& getLogfile();
      void setLogfile(const char* path);

      void emit(ERROR_CODE value, const std::string& info = "");
      void logMessage(const std::string& message);

      logImplementation();
      ~logImplementation();

    private:
      const char* errorToText(ERROR_CODE value);

      // Held for the whole of one line's delivery — the handler call included.
      // See the class notes: this is the sink's lock, not a general lock on the
      // object.
      std::mutex sinkMutex;

      logHandler* handler; // guarded by sinkMutex
      std::ofstream logFile; // guarded by sinkMutex

      // Read on every emit from every engine thread and written by any of them
      // through setLevel(), but it gates the message rather than delivering it:
      // an atomic keeps the filter itself race-free without dragging the level
      // check inside the sink's lock.
      std::atomic<ERROR_LEVEL> level;

      // Configuration, not sink state: written by setLogfile() under the lock
      // (it swaps the stream) and read back by getLogfile(). getLogfile()
      // returns a reference that outlives any lock this class could take, so
      // synchronising the read here would buy the caller nothing — setting the
      // log file is a control-thread, between-runs operation.
      std::string logFileName;

      Bool toDebugger;
    };

    logImplementation& LogImpl();

    /**
     *  Exception-safe log entry point for destructors and other noexcept
     *  contexts (issue #433). This is the only form of logging a destructor
     *  may use.
     *
     *  A destructor is implicitly noexcept, so the house guard
     *  `catch (...) { LogImpl().emit(...); }` was not itself safe: `emit()`
     *  builds a std::string and hands it to a possibly user-supplied sink, and
     *  either can throw straight back out of the catch block and call
     *  std::terminate() — the exact failure the guard was added to prevent.
     *
     *  Two properties make this one safe where `emit()` is not:
     *
     *  - It takes a `const char*`, not a `const std::string&`. Nothing is
     *    allocated in the caller's frame, so no part of the call expression
     *    can throw from outside the internal try block. `info` may be nullptr.
     *  - It is inert after the logger's own static teardown. Both this and the
     *    callers that need it are reached through function-local statics whose
     *    relative destruction order is unspecified, so a call arriving after
     *    ~logImplementation() is dropped rather than touching a destroyed
     *    object — the use-after-free class issue #298 fixed and the ASan
     *    lifecycle gate guards. That is what lets ~MIDI::outSender() report a
     *    failure at all, where PR #432 had to stay silent.
     *
     *  It allocates, and since #820 it also takes the sink's mutex for the
     *  moment the line is written, exactly as `emit()` does — this is the same
     *  path, entered safely, not a cheaper one. Neither matters here: this path
     *  is teardown-only and never the audio callback, which reaches the log
     *  through `rtLogQueue` instead.
     */
    void EmitNoThrow(ERROR_CODE value, const char* info) noexcept;
  } // namespace INTERNAL
} // namespace YSE

#endif // LOGIMPLEMENTATION_H_INCLUDED
