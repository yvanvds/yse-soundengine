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
#include <string>
#include <fstream>

namespace YSE {
  namespace INTERNAL {
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
      logHandler* handler;
      ERROR_LEVEL level;
      std::ofstream logFile;
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
     *  Never locks and never blocks. It allocates only on the path that
     *  actually formats and emits a message, exactly as `emit()` does — and
     *  that path is teardown-only, never the audio callback.
     */
    void EmitNoThrow(ERROR_CODE value, const char* info) noexcept;
  } // namespace INTERNAL
} // namespace YSE

#endif // LOGIMPLEMENTATION_H_INCLUDED
