/*
  yse_python.h — C ABI scripting surface (issue #125, epic #119).

  Lets a host (Phi via dart-yse, Python ctypes, …) submit scripts to the
  embedded CPython interpreter and receive errors asynchronously. The three
  symbols ship in every build with identical signatures; only their behaviour
  differs between a YSE_ENABLE_PYTHON=ON and =OFF library, so a binding can be
  written once and link against either.

  Self-contained C header: depends only on yse_common.h.
*/

#ifndef YSE_C_PYTHON_H_INCLUDED
#define YSE_C_PYTHON_H_INCLUDED

#include "yse_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Compile-time feature query. Returns 1 if the library was built with
   YSE_ENABLE_PYTHON=ON, else 0. Safe to call regardless of build
   configuration or engine state. */
YSE_C_API int yse_python_enabled(void);

/* Submit a UTF-8 script for asynchronous evaluation on the script thread.
   Returns immediately. The engine takes a copy; the caller may free `src`
   on return. A NULL `src` is a no-op.

   On a YSE_ENABLE_PYTHON=OFF build, the registered error callback (if any) is
   invoked synchronously, before this returns, with the literal string
   "YSE compiled without YSE_ENABLE_PYTHON". */
YSE_C_API void yse_run_script(const char* src);

/* Callback that receives formatted tracebacks from uncaught Python exceptions
   and syntax errors. `traceback_utf8` is owned by the engine and valid only
   for the duration of the call — copy it if you need to retain it. */
typedef void(YSE_C_CALLBACK* yse_script_error_cb)(const char* traceback_utf8, void* userdata);

/* Register the error callback. Pass NULL to clear. Thread-safe via atomic
   swap (the project's callback-bridge convention): installing or replacing
   the callback while the engine is dispatching a traceback is safe, and the
   previous callback never receives a later traceback.

   On an ON build the callback fires on the host thread that drives
   yse_system_update(), once per failed script. Works identically on an OFF
   build (it just stores the pointer), so a host may register unconditionally. */
YSE_C_API void yse_set_script_error_callback(yse_script_error_cb cb, void* userdata);

#ifdef __cplusplus
}
#endif

#endif /* YSE_C_PYTHON_H_INCLUDED */
