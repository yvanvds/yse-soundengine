/*
  ==============================================================================

    dsl_runtime.h
    Created: 2026-06-22

    Engine-side hooks for the `yse` live-coding module (issue #126, epic #119).

    These free functions are the seam between the script runtime / engine
    lifecycle (which know nothing about Python) and the binding layer in
    yse_module.cpp (which owns the interpreter-facing state: the tick counter,
    the generation counter, the subscription and schedule registries, and the
    cross-thread callback queue). The header carries no <Python.h> dependency so
    scriptRuntime.cpp and global.cpp can call it without the Python include path.

    Thread contract (mirrors docs/design/live_coding_dsl.md "Threading model"):
      - advanceTick / reset run on the MAIN thread (system::update / init).
      - beginGeneration / ensureBound / onWake / shutdown run on the SCRIPT
        thread with the GIL held (driven from ScriptRuntime::run / ::stop).

  ==============================================================================
*/

#ifndef YSE_PYTHON_DSL_RUNTIME_H
#define YSE_PYTHON_DSL_RUNTIME_H

#include <string>
#include <vector>

namespace YSE {
  namespace INTERNAL {
    namespace dsl {

      // Main thread, once per system::update(): advance the monotonic engine
      // tick that `yse.tick` reports and that `yse.schedule` counts against.
      void advanceTick();

      // Main thread, from System::init via startScripting(): reset the tick to
      // 0 and the generation to its initial value for a fresh session.
      void reset();

      // Script thread (GIL held): open a new generation. Called before exec-ing
      // each submitted script so handles it creates are tagged with the new
      // generation and survive the next yse.cancel_all().
      void beginGeneration();

      // Script thread (GIL held): import the `yse` module and bind it into the
      // __main__ namespace so scripts can call `yse.send(...)` without an
      // explicit import. Idempotent per interpreter instance.
      void ensureBound();

      // Script thread (GIL held): fire every matured schedule and deliver every
      // queued bus callback to its Python handler. Returns the formatted
      // traceback of each handler that raised (empty when all succeed) so the
      // caller can route them through the #125 error sink.
      std::vector<std::string> onWake();

      // Script thread (GIL held): drop every subscription and schedule and
      // release the Python objects they hold. Called before Py_FinalizeEx.
      void shutdown();

    } // namespace dsl
  } // namespace INTERNAL
} // namespace YSE

#endif // YSE_PYTHON_DSL_RUNTIME_H
