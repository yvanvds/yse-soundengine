/*
  yse_patcher.h — Max/MSP-style modular DSP/event graph.
  C ABI mirror of YseEngine/patcher/{patcher,pHandle,pObjectList}.hpp.

  A YsePatcher owns its YsePHandle objects — never destroy handles
  directly, use yse_patcher_delete_object().

  oscHandler (outbound message callback) is not yet wrapped — audio-
  thread callback plumbing lands in M8 alongside the io / log
  callbacks.

  Object type identifiers are the same strings YSE::OBJ exposes
  ("~sine", ".+", "~lp", etc.). See patcher/pObjectList.hpp upstream.

  Convention: every void-returning function in this header is a null-safe
  no-op when called with a NULL handle (patcher or phandle). Status
  queries return 0 / false / NULL on NULL.
*/

#ifndef YSE_C_PATCHER_H_INCLUDED
#define YSE_C_PATCHER_H_INCLUDED

#include "yse_common.h"
#include "yse_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Owned — release with yse_patcher_destroy. */
typedef struct YsePatcher YsePatcher;
/* Borrowed — owned by the parent YsePatcher. Release with
   yse_patcher_delete_object(patcher, handle); never call a destroy on
   the handle directly. */
typedef struct YsePHandle YsePHandle;

/* ─── patcher lifecycle ────────────────────────────────────────────── */

YSE_C_API YsePatcher* yse_patcher_create(void);
YSE_C_API void yse_patcher_destroy(YsePatcher* p);

YSE_C_API void yse_patcher_init(YsePatcher* p, int main_outputs);

/* ─── object management ───────────────────────────────────────────── */

YSE_C_API YsePHandle* yse_patcher_create_object(YsePatcher* p, const char* type, const char* args);
YSE_C_API void yse_patcher_delete_object(YsePatcher* p, YsePHandle* obj);
YSE_C_API void yse_patcher_clear(YsePatcher* p);

YSE_C_API void yse_patcher_connect(YsePatcher* p, YsePHandle* from, int outlet, YsePHandle* to,
                                   int inlet);
YSE_C_API void yse_patcher_disconnect(YsePatcher* p, YsePHandle* from, int outlet, YsePHandle* to,
                                      int inlet);

YSE_C_API int yse_patcher_is_valid_object(const char* type);

/* ─── persistence ─────────────────────────────────────────────────── */

YSE_C_API size_t yse_patcher_dump_json(YsePatcher* p, char* buf, size_t cap);
YSE_C_API void yse_patcher_parse_json(YsePatcher* p, const char* content);

/* ─── enumeration ─────────────────────────────────────────────────── */

YSE_C_API unsigned int yse_patcher_objects(YsePatcher* p);
YSE_C_API YsePHandle* yse_patcher_get_handle_from_list(YsePatcher* p, unsigned int idx);
YSE_C_API YsePHandle* yse_patcher_get_handle_from_id(YsePatcher* p, unsigned int id);

/* ─── message I/O ─────────────────────────────────────────────────── */

YSE_C_API int yse_patcher_pass_bang(YsePatcher* p, const char* to);
YSE_C_API int yse_patcher_pass_int(YsePatcher* p, int value, const char* to);
YSE_C_API int yse_patcher_pass_float(YsePatcher* p, float value, const char* to);
YSE_C_API int yse_patcher_pass_string(YsePatcher* p, const char* value, const char* to);

/* ─── handle accessors ───────────────────────────────────────────── */

YSE_C_API size_t yse_phandle_get_type(YsePHandle* h, char* buf, size_t cap);
YSE_C_API size_t yse_phandle_get_name(YsePHandle* h, char* buf, size_t cap);
YSE_C_API size_t yse_phandle_get_params(YsePHandle* h, char* buf, size_t cap);
YSE_C_API size_t yse_phandle_get_gui_value(YsePHandle* h, char* buf, size_t cap);
YSE_C_API size_t yse_phandle_get_gui_property(YsePHandle* h, const char* key, char* buf,
                                              size_t cap);
YSE_C_API void yse_phandle_set_gui_property(YsePHandle* h, const char* key, const char* value);

YSE_C_API void yse_phandle_set_bang(YsePHandle* h, unsigned int inlet);
YSE_C_API void yse_phandle_set_int(YsePHandle* h, unsigned int inlet, int value);
YSE_C_API void yse_phandle_set_float(YsePHandle* h, unsigned int inlet, float value);
YSE_C_API void yse_phandle_set_list(YsePHandle* h, unsigned int inlet, const char* value);
YSE_C_API void yse_phandle_set_params(YsePHandle* h, const char* args);

YSE_C_API int yse_phandle_get_inputs(YsePHandle* h);
YSE_C_API int yse_phandle_get_outputs(YsePHandle* h);
YSE_C_API int yse_phandle_is_dsp_input(YsePHandle* h, unsigned int inlet);
YSE_C_API YseOutType yse_phandle_output_data_type(YsePHandle* h, unsigned int pin);
YSE_C_API unsigned int yse_phandle_get_id(YsePHandle* h);

YSE_C_API unsigned int yse_phandle_get_connections(YsePHandle* h, unsigned int outlet);
YSE_C_API unsigned int yse_phandle_get_connection_target(YsePHandle* h, unsigned int outlet,
                                                         unsigned int connection);
YSE_C_API unsigned int yse_phandle_get_connection_target_inlet(YsePHandle* h, unsigned int outlet,
                                                               unsigned int connection);

/* ─── registry metadata ──────────────────────────────────────────────
   Read-only access to the in-code documentation captured by the
   pRegistry (issue #102). Lets a binding generate its own node palette
   or documentation site without forking the registry.

   These functions are main-thread only. They are NOT RT-safe and must
   not be called from the audio callback.

   String returns point at engine-owned storage with a lifetime tied to
   the process; the cache is built lazily on the first metadata call and
   never freed. The pointers stay valid for the rest of the process and
   may be safely cached by the binding.

   The exception is yse_patcher_get_metadata_json(), which allocates a
   fresh buffer per call; the caller must release it with
   yse_free_string(). The JSON layout is identical to the snapshot
   emitted by `yse.py dump-patcher-meta` (modulo object order — both
   iterate the registry's lexicographic order).

   Lookups by an unknown type_name return 0 / "" / YSE_PCAT_UNSET /
   YSE_OUT_INVALID. NULL out-pointers in *_get_inlet_info /
   *_get_outlet_info / *_get_param_info are skipped, so callers can
   ignore fields they don't need. */

YSE_C_API int yse_patcher_get_type_count(void);
YSE_C_API const char* yse_patcher_get_type_name(int index);

YSE_C_API const char* yse_patcher_get_type_description(const char* type_name);
YSE_C_API YsePCategory yse_patcher_get_type_category(const char* type_name);
YSE_C_API int yse_patcher_get_type_is_dsp(const char* type_name);

YSE_C_API int yse_patcher_get_inlet_count(const char* type_name);
YSE_C_API void yse_patcher_get_inlet_info(const char* type_name, int idx, const char** label,
                                          const char** doc, const char** range,
                                          unsigned int* accepts_bitmask);

YSE_C_API int yse_patcher_get_outlet_count(const char* type_name);
YSE_C_API void yse_patcher_get_outlet_info(const char* type_name, int idx, const char** label,
                                           const char** doc, const char** range, YseOutType* type);

YSE_C_API int yse_patcher_get_param_count(const char* type_name);
YSE_C_API void yse_patcher_get_param_info(const char* type_name, int idx, const char** name,
                                          const char** doc, const char** default_value,
                                          const char** range);

/* Returns a fresh malloc'd JSON snapshot of every registered object's
   metadata. One-stop shop for bindings that want to cache the metadata
   or regenerate their own reference at build time. The caller must
   release the buffer with yse_free_string(); returns NULL on
   allocation failure. */
YSE_C_API char* yse_patcher_get_metadata_json(void);
YSE_C_API void yse_free_string(char* s);

#ifdef __cplusplus
}
#endif

#endif
