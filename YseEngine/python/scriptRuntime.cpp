/*
  ==============================================================================

    scriptRuntime.cpp
    Created: 2026-06-21

    Embedded-CPython script runtime (issue #124, epic #119).

    Sourcing is Option C (see cmake/YsePython.cmake): a system / prebuilt
    libpython located via find_package, isolated at runtime with PyConfig
    rather than frozen at build time. Concretely the interpreter is booted
    with an *isolated* config (no environment, no user site), with signal
    handlers disabled (the Py_InitializeEx(0) intent) and site_import turned
    off so site-packages — and therefore any third-party package — is never
    importable. PyConfig.home is anchored to the located install (YSE_PYTHON_HOME)
    so the standard library still resolves. This honours the epic's "no
    third-party packages" tenet while deviating from its "build-time frozen
    subset / empty sys.path" wording, which Option C cannot provide.

  ==============================================================================
*/

#if YSE_ENABLE_PYTHON

// Python.h must precede standard headers per the C-API documentation.
#include <Python.h>

#include <cstdio>
#include <utility>

#include "scriptRuntime.h"

namespace YSE {
  namespace INTERNAL {

    namespace {

      // Format the *current* Python exception the way the DSL spec mandates:
      // exactly what traceback.format_exception produces. Falls back to
      // "<TypeName>: <message>" if the traceback module cannot be imported, so
      // the type name and message survive even in a degraded interpreter.
      // The GIL must be held; clears the error indicator before returning.
      std::string formatCurrentException() {
        PyObject *type = nullptr, *value = nullptr, *tb = nullptr;
        PyErr_Fetch(&type, &value, &tb);
        PyErr_NormalizeException(&type, &value, &tb);
        if (tb != nullptr && value != nullptr) {
          PyException_SetTraceback(value, tb);
        }

        std::string out;

        // Preferred path: traceback.format_exception(value) -> list[str].
        // Single-argument form (3.10+) reads the traceback off the exception.
        if (value != nullptr) {
          PyObject* tbmod = PyImport_ImportModule("traceback");
          if (tbmod != nullptr) {
            PyObject* lines =
                PyObject_CallMethod(tbmod, "format_exception", "O", value);
            if (lines != nullptr) {
              PyObject* sep = PyUnicode_FromString("");
              PyObject* joined = sep ? PyUnicode_Join(sep, lines) : nullptr;
              if (joined != nullptr) {
                const char* utf8 = PyUnicode_AsUTF8(joined);
                if (utf8 != nullptr) out = utf8;
                Py_DECREF(joined);
              }
              Py_XDECREF(sep);
              Py_DECREF(lines);
            }
            Py_DECREF(tbmod);
          }
        }

        // Fallback: reconstruct "<TypeName>: <message>" directly.
        if (out.empty() && value != nullptr) {
          const char* tname = Py_TYPE(value)->tp_name;
          PyObject* str = PyObject_Str(value);
          const char* msg = str ? PyUnicode_AsUTF8(str) : nullptr;
          out = std::string(tname ? tname : "Exception");
          if (msg != nullptr && msg[0] != '\0') {
            out += ": ";
            out += msg;
          }
          Py_XDECREF(str);
        }

        Py_XDECREF(type);
        Py_XDECREF(value);
        Py_XDECREF(tb);
        PyErr_Clear();
        return out;
      }

    } // namespace

    ScriptRuntime::ScriptRuntime() = default;

    ScriptRuntime::~ScriptRuntime() {
      // Ensure the worker is joined and the interpreter finalized before the
      // base thread destructor asserts !isRunning().
      if (started_) stop();
    }

    void ScriptRuntime::start() {
      if (started_) return;

      if (Py_IsInitialized() == 0) {
        PyConfig config;
        PyConfig_InitIsolatedConfig(&config);   // isolated: no env, no user site
        config.install_signal_handlers = 0;     // == Py_InitializeEx(0)
        config.site_import = 0;                  // exclude site-packages

#ifdef YSE_PYTHON_HOME
        PyStatus hst =
            PyConfig_SetBytesString(&config, &config.home, YSE_PYTHON_HOME);
        if (PyStatus_Exception(hst)) {
          std::fprintf(stderr, "[yse] Python home config failed: %s\n",
                       hst.err_msg ? hst.err_msg : "unknown");
          PyConfig_Clear(&config);
          return;
        }
#endif

        PyStatus st = Py_InitializeFromConfig(&config);
        PyConfig_Clear(&config);
        if (PyStatus_Exception(st)) {
          std::fprintf(stderr, "[yse] Python init failed: %s\n",
                       st.err_msg ? st.err_msg : "unknown");
          return;
        }

        ownsInterpreter_ = true;
        // Release the GIL the initializing thread now holds. The worker
        // acquires it via PyGILState_Ensure() on each wake.
        mainThreadState_ = PyEval_SaveThread();
      } else {
        // Another ScriptRuntime already booted the process interpreter; attach
        // without re-initializing or claiming finalization ownership.
        ownsInterpreter_ = false;
      }

      stopRequested_ = false;
      started_ = true;
      thread::start();  // launches run() on the script thread
    }

    void ScriptRuntime::stop() {
      if (!started_) return;

      // Ask the worker to drain remaining requests, then exit.
      {
        std::lock_guard<std::mutex> lock(mutex_);
        stopRequested_ = true;
        pendingWake_ = true;
      }
      cv_.notify_one();
      thread::stop();  // sets the base exit flag and joins the worker

      if (ownsInterpreter_) {
        // Re-acquire the GIL on this (the initializing) thread, then finalize.
        PyEval_RestoreThread(static_cast<PyThreadState*>(mainThreadState_));
        Py_FinalizeEx();
        mainThreadState_ = nullptr;
        ownsInterpreter_ = false;
      }

      started_ = false;
    }

    void ScriptRuntime::pushEval(std::string source) {
      inbound_.push(EvalRequest{std::move(source)});
      {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingWake_ = true;
      }
      cv_.notify_one();
    }

    void ScriptRuntime::wake() {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingWake_ = true;
      }
      cv_.notify_one();
    }

    bool ScriptRuntime::tryPopResult(EvalResult& out) {
      return outbound_.try_pop(out);
    }

    EvalResult ScriptRuntime::evaluate(const std::string& source) {
      EvalResult result;

      PyObject* mainModule = PyImport_AddModule("__main__");  // borrowed
      if (mainModule == nullptr) {
        result.status = EvalStatus::Error;
        result.traceback = formatCurrentException();
        if (result.traceback.empty()) result.traceback = "internal: no __main__";
        return result;
      }
      PyObject* globals = PyModule_GetDict(mainModule);  // borrowed

      // Py_file_input accepts multi-statement source (e.g. "result = 1 + 1").
      PyObject* ret =
          PyRun_String(source.c_str(), Py_file_input, globals, globals);
      if (ret != nullptr) {
        Py_DECREF(ret);
        result.status = EvalStatus::Ok;
        return result;
      }

      result.status = EvalStatus::Error;
      result.traceback = formatCurrentException();
      return result;
    }

    void ScriptRuntime::run() {
      for (;;) {
        {
          std::unique_lock<std::mutex> lock(mutex_);
          cv_.wait(lock, [this] {
            return pendingWake_ || stopRequested_ || threadShouldExit();
          });
          pendingWake_ = false;
        }

        // Drain inbound under the GIL — also on the shutdown wake, so requests
        // queued just before stop() are not silently dropped.
        PyGILState_STATE gil = PyGILState_Ensure();
        EvalRequest request;
        while (inbound_.try_pop(request)) {
          outbound_.push(evaluate(request.source));
        }
        PyGILState_Release(gil);

        if (stopRequested_ || threadShouldExit()) break;
      }
    }

  } // namespace INTERNAL
} // namespace YSE

#endif  // YSE_ENABLE_PYTHON
