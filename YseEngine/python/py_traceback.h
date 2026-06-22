/*
  ==============================================================================

    py_traceback.h
    Created: 2026-06-22

    Shared formatter for the *current* Python exception (epic #119).

    Both the script runtime (top-level eval failures, issue #124/#125) and the
    `yse` module's callback dispatch (yse.on / yse.schedule handler failures,
    issue #126) must surface tracebacks in exactly one format: what
    traceback.format_exception produces. Keeping the formatter here means the
    two error paths cannot drift.

    Python.h must precede standard headers per the C-API documentation, so this
    header includes it first and is only ever included from translation units
    compiled with YSE_ENABLE_PYTHON.

  ==============================================================================
*/

#ifndef YSE_PYTHON_PY_TRACEBACK_H
#define YSE_PYTHON_PY_TRACEBACK_H

#if YSE_ENABLE_PYTHON

#include <Python.h>

#include <string>

namespace YSE {
  namespace INTERNAL {

    // Format the *current* Python exception the way the DSL spec mandates:
    // exactly what traceback.format_exception produces. Falls back to
    // "<TypeName>: <message>" if the traceback module cannot be imported, so
    // the type name and message survive even in a degraded interpreter.
    // The GIL must be held; clears the error indicator before returning.
    inline std::string formatCurrentException() {
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

  } // namespace INTERNAL
} // namespace YSE

#endif  // YSE_ENABLE_PYTHON

#endif  // YSE_PYTHON_PY_TRACEBACK_H
