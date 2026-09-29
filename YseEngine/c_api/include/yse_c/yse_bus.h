/**
  @file
  yse_bus.h — host access to the global named bus: prefix taps (issue #389),
  plus publish and exact-address subscribe (issue #904).

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

#ifndef YSE_C_HANDLE_YseBusTap
#define YSE_C_HANDLE_YseBusTap
/** Owned — release with yse_bus_tap_destroy. */
typedef struct YseBusTap YseBusTap;
#endif

/** Payload kind of a bus value. The bus carries five kinds: bang (a valueless
   trigger, published e.g. by a patcher gSend bang outlet), int, float, string,
   and float list. Bus int/float are 32-bit. */
typedef enum YseBusValueKind {
  YSE_BUS_BANG = 0,
  YSE_BUS_INT = 1,
  YSE_BUS_FLOAT = 2,
  YSE_BUS_STRING = 3,
  YSE_BUS_LIST = 4
} YseBusValueKind;

/** Receives one (address, value) frame per matching publish. Exactly one of
   the payload parameters is meaningful, selected by `kind`:

     YSE_BUS_BANG   — no payload (i, f, str, list are all zero/NULL)
     YSE_BUS_INT    — `i`
     YSE_BUS_FLOAT  — `f`
     YSE_BUS_STRING — `str` (NUL-terminated UTF-8)
     YSE_BUS_LIST   — `list` + `list_len` (list may be NULL when list_len
                      is 0)

   `address`, `str` and `list` are owned by the engine and valid ONLY for the
   duration of the call — copy anything you need to retain before returning.
   There is no free function (same contract as YseScriptErrorCallback). */
typedef void(YSE_C_CALLBACK* YseBusTapCallback)(const char* address, YseBusValueKind kind, int i,
                                                float f, const char* str, const float* list,
                                                size_t list_len, void* user_data);

/** Subscribe `cb` to every bus publish whose address starts with `prefix`
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
YSE_C_API YseBusTap* yse_bus_tap_create(const char* prefix, YseBusTapCallback cb, void* user_data);

/** Unsubscribe and release the tap. Null-safe no-op. */
YSE_C_API void yse_bus_tap_destroy(YseBusTap* tap);

/** ── Publish (issue #904) ─────────────────────────────────────────────────

   Publish a value to one exact bus address — the same path a script's
   yse.send() takes, so it reaches everything a script can: a named synth's
   "synth.<name>.note", a patcher's .r on "patcher.<name>.<slot>", a named
   channel's "channel.<name>.volume", exact subscriptions and prefix taps.

   Thread attribution: the publish is tagged as a control-rate (non-audio)
   publish, like a script's. On the control thread (the one driving
   yse_system_update()) it dispatches synchronously — every subscriber and
   tap has run when the call returns. From any other host thread it is
   parked in the bus's control inbox (a short mutex-guarded append, no
   dispatch) and delivered during the next yse_system_update(). Do not call
   these from inside a real-time audio callback, and do not publish from
   another thread concurrently with yse_system_close().

   Returns YSE_ERR_NOT_INITIALIZED when the engine is down,
   YSE_ERR_INVALID_ARGUMENT for a NULL or empty address (or a NULL string /
   a NULL list with a non-zero count), YSE_ERR_EXCEPTION on an internal
   failure — each with the reason in yse_last_error(). A publish no one
   listens to is not an error. */
YSE_C_API YseStatus yse_bus_publish_bang(const char* address);
YSE_C_API YseStatus yse_bus_publish_int(const char* address, int value);
YSE_C_API YseStatus yse_bus_publish_float(const char* address, float value);
/** `value` is NUL-terminated UTF-8, copied before the call returns. */
YSE_C_API YseStatus yse_bus_publish_string(const char* address, const char* value);
/** `values` is copied before the call returns; it may be NULL when `count` is
   0 (an empty list). */
YSE_C_API YseStatus yse_bus_publish_list(const char* address, const float* values, size_t count);

/* ── Exact subscription (issue #904) ──────────────────────────────────────

   Unlike a tap, a subscription matches ONE address exactly: subscribing to
   "patcher.lead.cutoff" does not see "patcher.lead.cutoff2". No glob or
   wildcard forms (the DSL spec rules them out). */

#ifndef YSE_C_HANDLE_YseBusSub
#define YSE_C_HANDLE_YseBusSub
/** Owned — release with yse_bus_unsubscribe. */
typedef struct YseBusSub YseBusSub;
#endif

/** Same frame contract as YseBusTapCallback: `address` is the subscribed
   address, exactly one payload parameter is meaningful per `kind`, and every
   pointer is engine-owned and valid only for the duration of the call. */
typedef void(YSE_C_CALLBACK* YseBusSubCallback)(const char* address, YseBusValueKind kind, int i,
                                                float f, const char* str, const float* list,
                                                size_t list_len, void* user_data);

/** Subscribe `cb` to every publish on exactly `address`. Returns NULL — with
   the reason in yse_last_error() — if `address` is NULL or empty, `cb` is
   NULL, or the engine is not initialised.

   Delivery, lifecycle and threading follow the tap rules above: callbacks
   fire on the control thread (inline for a control-thread publish, else
   during yse_system_update()); yse_system_close() invalidates every live
   subscription (the handle stays safe to release, but does not reattach
   after a re-init); call subscribe and unsubscribe on the control thread,
   which guarantees no callback fires after unsubscribe returns. */
YSE_C_API YseBusSub* yse_bus_subscribe(const char* address, YseBusSubCallback cb, void* user_data);

/** Unsubscribe and release the subscription. Null-safe no-op. */
YSE_C_API void yse_bus_unsubscribe(YseBusSub* sub);

#ifdef __cplusplus
}
#endif

#endif /* YSE_C_BUS_H_INCLUDED */
