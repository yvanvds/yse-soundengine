/*
  yse_bus.h — host tap onto the global named bus (issue #389).

  Lets a host (Phi via dart-yse, Python ctypes, …) subscribe to a bus address
  *prefix* and receive an (address, value) frame for every publish whose
  address starts with that prefix — e.g. a "phi.ctl." tap sees every
  live-coding control verb a script publishes. Multiple taps may be live at
  once, each with its own callback + user_data.

  This is host-only surface: the script-facing subscribe (`yse.on()`) stays
  exact-match with no wildcards, per the DSL spec (docs/design/
  live_coding_dsl.md §"Address grammar"). Prefix matching happens inside the
  engine's control-thread dispatch, so a tap adds zero audio-thread cost.

  Delivery: the callback fires on the thread that drives yse_system_update()
  (the control thread). Publishes made on the control thread itself are
  delivered synchronously inside the publish; publishes from any other thread
  (audio callback, script thread, timer thread) are queued and delivered
  during the next yse_system_update() tick.

  Convention (see yse_c_internal.hpp): void functions taking a handle are
  null-safe no-ops on a NULL handle.

  Self-contained C header: depends only on yse_common.h.
*/

#ifndef YSE_C_BUS_H_INCLUDED
#define YSE_C_BUS_H_INCLUDED

#include "yse_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Owned — release with yse_bus_tap_destroy. */
typedef struct YseBusTap YseBusTap;

/* Payload kind of a bus value. The bus carries five kinds: bang (a valueless
   trigger, published e.g. by a patcher gSend bang outlet), int, float, string,
   and float list. Bus int/float are 32-bit. */
typedef enum YseBusValueKind {
  YSE_BUS_BANG = 0,
  YSE_BUS_INT = 1,
  YSE_BUS_FLOAT = 2,
  YSE_BUS_STRING = 3,
  YSE_BUS_LIST = 4
} YseBusValueKind;

/* Receives one (address, value) frame per matching publish. Exactly one of
   the payload parameters is meaningful, selected by `kind`:

     YSE_BUS_BANG   — no payload (i, f, str, list are all zero/NULL)
     YSE_BUS_INT    — `i`
     YSE_BUS_FLOAT  — `f`
     YSE_BUS_STRING — `str` (NUL-terminated UTF-8)
     YSE_BUS_LIST   — `list` + `list_len` (list may be NULL when list_len
                      is 0)

   `address`, `str` and `list` are owned by the engine and valid ONLY for the
   duration of the call — copy anything you need to retain before returning.
   There is no free function (same contract as yse_script_error_cb). */
typedef void(YSE_C_CALLBACK* yse_bus_tap_cb)(const char* address, YseBusValueKind kind, int i,
                                             float f, const char* str, const float* list,
                                             size_t list_len, void* user_data);

/* Subscribe `cb` to every bus publish whose address starts with `prefix`
   (plain byte-wise prefix match; an empty string matches every address).
   Returns NULL — with the reason in yse_last_error() — if `prefix` or `cb`
   is NULL, or if the engine is not initialised: create taps after
   yse_system_init / yse_system_init_offline.

   Lifecycle: yse_system_close() invalidates every live tap — the engine-side
   subscription dies with the bus, and no callback fires after close returns.
   The YseBusTap handle itself stays safe to destroy (before or after a
   re-init), but it does not reattach: re-create taps after the next init.

   Threading: call create and destroy on the control thread (the one driving
   yse_system_update()). That guarantees no callback fires after destroy
   returns. */
YSE_C_API YseBusTap* yse_bus_tap_create(const char* prefix, yse_bus_tap_cb cb, void* user_data);

/* Unsubscribe and release the tap. Null-safe no-op. */
YSE_C_API void yse_bus_tap_destroy(YseBusTap* tap);

#ifdef __cplusplus
}
#endif

#endif /* YSE_C_BUS_H_INCLUDED */
