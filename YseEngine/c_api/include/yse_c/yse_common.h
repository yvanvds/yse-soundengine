/*
  yse_common.h — shared types, status codes, error reporting.
  Self-contained C header: no transitive includes outside <stddef.h>.
  Consumed by Dart ffigen and any other C ABI client.
*/

#ifndef YSE_C_COMMON_H_INCLUDED
#define YSE_C_COMMON_H_INCLUDED

#include <stddef.h>

#if defined(_WIN32) || defined(_WIN64)
#if defined(YSE_DLL_BUILD)
#define YSE_C_API __declspec(dllexport)
#elif defined(YSE_DLL)
#define YSE_C_API __declspec(dllimport)
#else
#define YSE_C_API
#endif
#else
#if defined(YSE_DLL_BUILD)
#define YSE_C_API __attribute__((visibility("default")))
#else
#define YSE_C_API
#endif
#endif

/* Calling-convention marker for callback function pointer typedefs that
   the engine invokes across the C ABI. Made explicit on Windows so the
   ABI is unambiguous across compilers (MSVC, Clang, MinGW); a no-op on
   x64 / ARM64 / non-Windows where only one C calling convention exists. */
#if defined(_WIN32) || defined(_WIN64)
#define YSE_C_CALLBACK __cdecl
#else
#define YSE_C_CALLBACK
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum YseStatus {
  YSE_OK = 0,
  YSE_ERR_GENERIC = 1,
  YSE_ERR_NOT_INITIALIZED = 2,
  YSE_ERR_INVALID_HANDLE = 3,
  YSE_ERR_INVALID_ARGUMENT = 4,
  YSE_ERR_FILE_NOT_FOUND = 5,
  YSE_ERR_AUDIO_DEVICE = 6,
  YSE_ERR_MIDI = 7,
  YSE_ERR_EXCEPTION = 8
} YseStatus;

typedef struct yse_pos_t {
  float x;
  float y;
  float z;
} yse_pos_t;

YSE_C_API const char* yse_version(void);

/* Returns the last error message recorded on the calling thread (the
   slot is thread-local). The returned pointer is valid until the next
   yse_* call from the same thread; copy the string if you need to hold
   onto it. Empty string when no error has been recorded. */
YSE_C_API const char* yse_last_error(void);
YSE_C_API void yse_clear_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
