/*
  ==============================================================================

    scriptRuntime.h
    Created: 2026-06-21

    Embedded-CPython script runtime (issue #124, epic #119).

    Owns the interpreter lifecycle and the dedicated script thread that holds
    the GIL on wake. Two lock-free SPSC queues carry work across threads:

      inbound  (main  -> script) : EvalRequest  { source to exec in __main__ }
      outbound (script -> main)  : EvalResult   { status + traceback }

    This is infrastructure only — no user-facing API and no `yse` module yet
    (those are issues #125 and #126). The public surface here exists so the
    C API (#125) and the doctest suite can drive evaluation without reaching
    into the worker.

    The header carries no <Python.h> dependency: the interpreter is opaque
    here (mainThreadState_ is a void* PyThreadState) so consumers and tests
    can include it without the Python include path.

  ==============================================================================
*/

#ifndef YSE_PYTHON_SCRIPTRUNTIME_H
#define YSE_PYTHON_SCRIPTRUNTIME_H

#include <condition_variable>
#include <mutex>
#include <string>

#include "../internal/thread.h"
#include "../utils/lfQueue.hpp"

namespace YSE {
  namespace INTERNAL {

    // Result of evaluating one EvalRequest. `Error` deliberately avoids the
    // identifier `ERROR`, which <Windows.h> defines as a macro.
    enum class EvalStatus { Ok, Error };

    struct EvalRequest {
      std::string source;
    };

    struct EvalResult {
      EvalStatus status = EvalStatus::Ok;
      // For Error: the string produced by traceback.format_exception (or a
      // "<TypeName>: <message>" fallback if the traceback module is
      // unavailable). Empty for Ok.
      std::string traceback;
    };

    class ScriptRuntime : public thread {
    public:
      ScriptRuntime();
      ~ScriptRuntime() override;

      ScriptRuntime(const ScriptRuntime&) = delete;
      ScriptRuntime& operator=(const ScriptRuntime&) = delete;

      // Boot the interpreter (if this is the first runtime in the process)
      // and launch the worker thread. Safe to call once; a second call while
      // already started is a no-op.
      void start();

      // Join the worker (after a final drain of pending requests) and, if this
      // runtime booted the interpreter, finalize it. Idempotent.
      void stop();

      // Producer = main thread. Enqueue `source` to be exec'd in the __main__
      // namespace on the script thread. Wakes the worker.
      void pushEval(std::string source);

      // One wake per system::update() tick so future scheduled work (the
      // yse.schedule countdowns landing in #126/#127) can advance. The wake
      // mechanism exists from here even though nothing schedules yet.
      void wake();

      // Consumer = main thread. Move the next completed result into `out`.
      // Returns false if none is ready.
      bool tryPopResult(EvalResult& out);

      // Worker body — invoked on the script thread by thread::start(). Public
      // only because the base class dispatches to it; do not call directly.
      void run() override;

    private:
      EvalResult evaluate(const std::string& source); // GIL must be held

      lfQueue<EvalRequest> inbound_;
      lfQueue<EvalResult> outbound_;

      std::mutex mutex_;
      std::condition_variable cv_;
      bool pendingWake_ = false;
      bool stopRequested_ = false;

      bool started_ = false;
      bool ownsInterpreter_ = false;
      void* mainThreadState_ = nullptr; // PyThreadState* saved after init
    };

  } // namespace INTERNAL
} // namespace YSE

#endif // YSE_PYTHON_SCRIPTRUNTIME_H
