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

typedef struct YsePatcher YsePatcher;
typedef struct YsePHandle YsePHandle;

/* ─── patcher lifecycle ────────────────────────────────────────────── */

YSE_C_API YsePatcher* yse_patcher_create(void);
YSE_C_API void        yse_patcher_destroy(YsePatcher* p);

YSE_C_API void        yse_patcher_init(YsePatcher* p, int main_outputs);

/* ─── object management ───────────────────────────────────────────── */

YSE_C_API YsePHandle* yse_patcher_create_object(YsePatcher* p, const char* type, const char* args);
YSE_C_API void        yse_patcher_delete_object(YsePatcher* p, YsePHandle* obj);
YSE_C_API void        yse_patcher_clear(YsePatcher* p);

YSE_C_API void        yse_patcher_connect(YsePatcher* p, YsePHandle* from, int outlet, YsePHandle* to, int inlet);
YSE_C_API void        yse_patcher_disconnect(YsePatcher* p, YsePHandle* from, int outlet, YsePHandle* to, int inlet);

YSE_C_API int         yse_patcher_is_valid_object(const char* type);

/* ─── persistence ─────────────────────────────────────────────────── */

YSE_C_API size_t        yse_patcher_dump_json(YsePatcher* p, char* buf, size_t cap);
YSE_C_API void          yse_patcher_parse_json(YsePatcher* p, const char* content);

/* ─── enumeration ─────────────────────────────────────────────────── */

YSE_C_API unsigned int  yse_patcher_objects(YsePatcher* p);
YSE_C_API YsePHandle*   yse_patcher_get_handle_from_list(YsePatcher* p, unsigned int idx);
YSE_C_API YsePHandle*   yse_patcher_get_handle_from_id(YsePatcher* p, unsigned int id);

/* ─── message I/O ─────────────────────────────────────────────────── */

YSE_C_API int  yse_patcher_pass_bang(YsePatcher* p, const char* to);
YSE_C_API int  yse_patcher_pass_int(YsePatcher* p, int value, const char* to);
YSE_C_API int  yse_patcher_pass_float(YsePatcher* p, float value, const char* to);
YSE_C_API int  yse_patcher_pass_string(YsePatcher* p, const char* value, const char* to);

/* ─── handle accessors ───────────────────────────────────────────── */

YSE_C_API size_t       yse_phandle_get_type(YsePHandle* h, char* buf, size_t cap);
YSE_C_API size_t       yse_phandle_get_name(YsePHandle* h, char* buf, size_t cap);
YSE_C_API size_t       yse_phandle_get_params(YsePHandle* h, char* buf, size_t cap);
YSE_C_API size_t       yse_phandle_get_gui_value(YsePHandle* h, char* buf, size_t cap);
YSE_C_API size_t       yse_phandle_get_gui_property(YsePHandle* h, const char* key, char* buf, size_t cap);
YSE_C_API void         yse_phandle_set_gui_property(YsePHandle* h, const char* key, const char* value);

YSE_C_API void         yse_phandle_set_bang(YsePHandle* h, unsigned int inlet);
YSE_C_API void         yse_phandle_set_int(YsePHandle* h, unsigned int inlet, int value);
YSE_C_API void         yse_phandle_set_float(YsePHandle* h, unsigned int inlet, float value);
YSE_C_API void         yse_phandle_set_list(YsePHandle* h, unsigned int inlet, const char* value);
YSE_C_API void         yse_phandle_set_params(YsePHandle* h, const char* args);

YSE_C_API int          yse_phandle_get_inputs(YsePHandle* h);
YSE_C_API int          yse_phandle_get_outputs(YsePHandle* h);
YSE_C_API int          yse_phandle_is_dsp_input(YsePHandle* h, unsigned int inlet);
YSE_C_API YseOutType   yse_phandle_output_data_type(YsePHandle* h, unsigned int pin);
YSE_C_API unsigned int yse_phandle_get_id(YsePHandle* h);

YSE_C_API unsigned int yse_phandle_get_connections(YsePHandle* h, unsigned int outlet);
YSE_C_API unsigned int yse_phandle_get_connection_target(YsePHandle* h, unsigned int outlet, unsigned int connection);
YSE_C_API unsigned int yse_phandle_get_connection_target_inlet(YsePHandle* h, unsigned int outlet, unsigned int connection);

#ifdef __cplusplus
}
#endif

#endif
