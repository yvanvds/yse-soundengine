# Embedded Python (`YSE_ENABLE_PYTHON`)

How the optional embedded CPython runtime behind the live-coding DSL is sourced, isolated, threaded
and bound (epic #119; infrastructure #124, `yse` module #126). The DSL surface itself is specified
in [live_coding_dsl.md](live_coding_dsl.md). Moved out of PROJECT_OVERVIEW.md by issue #890.

- **Sourcing (Option C).** `cmake/YsePython.cmake` locates a system / prebuilt libpython via
  `find_package(Python3 ≥ 3.10 COMPONENTS Development.Embed)` and links it into `yse_objects`. On
  MSYS2 Clang64 discovery is anchored to the toolchain's own Python (the `clang++` prefix) so the
  ABI-matched `libpython3.x.dll.a` is used rather than a registry MSVC install. This **deviates**
  from the issue's "FetchContent + static + build-time-frozen stdlib" wording: CPython ships no
  CMake build and does not build under Clang64. Practical consequences: linkage is whatever the
  platform provides (typically **shared** libpython — deployments must ship/locate it at runtime);
  the interpreter **version follows the host** (not a pinned 3.12.x).

- **Runtime isolation instead of a build-time freeze.** `INTERNAL::ScriptRuntime`
  (`YseEngine/python/`) boots the interpreter with an *isolated* `PyConfig` — no environment, no
  user site, signal handlers off (the `Py_InitializeEx(0)` intent), and `site_import = 0` so
  site-packages (and therefore any third-party package) is never importable. `PyConfig.home` is
  anchored to the located install (`YSE_PYTHON_HOME`) so the standard library still resolves. Net:
  the epic's "no third-party packages" tenet holds; "curated frozen subset / empty `sys.path`"
  becomes "full stdlib of the located interpreter, isolated from site-packages".

- **Lifecycle & threading.** The runtime is a process-global owned in `global.cpp` (kept out of
  `global.h` so the header carries no Python type and no macro-dependent layout). `system::init`
  boots it after the audio device opens (`startScripting`), `system::update` wakes it once per tick
  (`wakeScripting`), `system::close` finalizes it before the device closes (`stopScripting`). A
  dedicated script thread (subclassing `INTERNAL::thread`) holds the GIL on wake and services two
  lock-free SPSC queues — inbound `EvalRequest` (source `exec`'d in `__main__`) and outbound
  `EvalResult` (status + `traceback.format_exception` text, formatted by the shared
  `python/py_traceback.h`). The user-facing `yse_python_run_script` C API is issue #125.

- **`yse` module + DSL (issue [#126](https://github.com/yvanvds/yse-soundengine/issues/126)).**
  `python/yse_module.cpp` binds the live-coding surface with **pybind11** (header-only, fetched at
  `v2.13.6` via `FetchContent` with `PYBIND11_NOPYTHON`; libpython comes from Option C above).
  `PYBIND11_EMBEDDED_MODULE(yse, …)` registers the module with `PyImport_AppendInittab` at
  static-init time, before `Py_Initialize`. The module exposes `send` / `on` / `unsubscribe` /
  `latch` / `schedule` / `tick` / `cancel_all` per `docs/design/live_coding_dsl.md`, routing values
  through `INTERNAL::NamedBus` (#121). DSL state (atomic tick, generation counter, subscription +
  schedule registries, a mutex-guarded cross-thread callback queue) lives in `yse_module.cpp` behind
  the Python-free `python/dsl_runtime.h` seam that `scriptRuntime.cpp` / `global.cpp` call (`reset`
  / `advanceTick` on the main thread; `beginGeneration` / `ensureBound` / `onWake` / `shutdown` on
  the script thread under the GIL). The TU defines `PYBIND11_SIMPLE_GIL_MANAGEMENT` so pybind's GIL
  helpers route through `PyGILState_Ensure` and interoperate with the runtime's raw GIL calls —
  without it, pybind's default GIL management attaches a second thread state and aborts (`non-NULL
  old thread state`).
