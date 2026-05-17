#include "yse_c/yse_patcher.h"
#include "yse_c_internal.hpp"

#include "../patcher/patcher.hpp"
#include "../patcher/pHandle.hpp"

#include <cstring>
#include <exception>
#include <string>

namespace {
  inline YSE::patcher* to_cpp(YsePatcher* p) {
    return reinterpret_cast<YSE::patcher*>(p);
  }
  inline YSE::pHandle* to_cpp(YsePHandle* h) {
    return reinterpret_cast<YSE::pHandle*>(h);
  }
  inline YsePHandle* to_c(YSE::pHandle* h) {
    return reinterpret_cast<YsePHandle*>(h);
  }

  size_t copy_string(const std::string& src, char* buf, size_t cap) {
    if (buf != nullptr && cap > 0) {
      const size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
      std::memcpy(buf, src.data(), n);
      buf[n] = '\0';
    }
    return src.size();
  }
}

extern "C" {

YSE_C_API YsePatcher* yse_patcher_create(void) {
  try { return reinterpret_cast<YsePatcher*>(new YSE::patcher()); }
  catch (const std::exception& e) { yse_c::set_last_error(e.what()); return nullptr; }
  catch (...) { yse_c::set_last_error("patcher_create: unknown C++ exception"); return nullptr; }
}

YSE_C_API void yse_patcher_destroy(YsePatcher* p) {
  if (!p) return;
  delete to_cpp(p);
}

YSE_C_API void yse_patcher_init(YsePatcher* p, int main_outputs) {
  if (!p) return;
  to_cpp(p)->create(main_outputs);
}

YSE_C_API YsePHandle* yse_patcher_create_object(YsePatcher* p, const char* type, const char* args) {
  if (!p || !type) return nullptr;
  try {
    return to_c(to_cpp(p)->CreateObject(type, args ? std::string(args) : std::string()));
  } catch (const std::exception& e) { yse_c::set_last_error(e.what()); return nullptr; }
    catch (...) { yse_c::set_last_error("patcher_create_object: unknown C++ exception"); return nullptr; }
}

YSE_C_API void yse_patcher_delete_object(YsePatcher* p, YsePHandle* obj) {
  if (!p || !obj) return;
  to_cpp(p)->DeleteObject(to_cpp(obj));
}

YSE_C_API void yse_patcher_clear(YsePatcher* p) {
  if (p) to_cpp(p)->Clear();
}

YSE_C_API void yse_patcher_connect(YsePatcher* p, YsePHandle* from, int outlet, YsePHandle* to, int inlet) {
  if (!p || !from || !to) return;
  to_cpp(p)->Connect(to_cpp(from), outlet, to_cpp(to), inlet);
}

YSE_C_API void yse_patcher_disconnect(YsePatcher* p, YsePHandle* from, int outlet, YsePHandle* to, int inlet) {
  if (!p || !from || !to) return;
  to_cpp(p)->Disconnect(to_cpp(from), outlet, to_cpp(to), inlet);
}

YSE_C_API int yse_patcher_is_valid_object(const char* type) {
  if (!type) return 0;
  return YSE::patcher::IsValidObject(type) ? 1 : 0;
}

YSE_C_API size_t yse_patcher_dump_json(YsePatcher* p, char* buf, size_t cap) {
  if (!p) { if (buf && cap > 0) buf[0] = '\0'; return 0; }
  try {
    return copy_string(to_cpp(p)->DumpJSON(), buf, cap);
  } catch (const std::exception&) {
    if (buf && cap > 0) buf[0] = '\0';
    return 0;
  }
}

YSE_C_API void yse_patcher_parse_json(YsePatcher* p, const char* content) {
  if (!p || !content) return;
  try { to_cpp(p)->ParseJSON(content); }
  catch (const std::exception& e) { yse_c::set_last_error(e.what()); }
  catch (...) { yse_c::set_last_error("patcher_parse_json: unknown C++ exception"); }
}

YSE_C_API unsigned int yse_patcher_objects(YsePatcher* p) {
  return p ? to_cpp(p)->Objects() : 0;
}
YSE_C_API YsePHandle* yse_patcher_get_handle_from_list(YsePatcher* p, unsigned int idx) {
  return p ? to_c(to_cpp(p)->GetHandleFromList(idx)) : nullptr;
}
YSE_C_API YsePHandle* yse_patcher_get_handle_from_id(YsePatcher* p, unsigned int id) {
  return p ? to_c(to_cpp(p)->GetHandleFromID(id)) : nullptr;
}

YSE_C_API int yse_patcher_pass_bang(YsePatcher* p, const char* to) {
  if (!p || !to) return 0;
  return to_cpp(p)->PassBang(to) ? 1 : 0;
}
YSE_C_API int yse_patcher_pass_int(YsePatcher* p, int value, const char* to) {
  if (!p || !to) return 0;
  return to_cpp(p)->PassData(value, std::string(to)) ? 1 : 0;
}
YSE_C_API int yse_patcher_pass_float(YsePatcher* p, float value, const char* to) {
  if (!p || !to) return 0;
  return to_cpp(p)->PassData(value, std::string(to)) ? 1 : 0;
}
YSE_C_API int yse_patcher_pass_string(YsePatcher* p, const char* value, const char* to) {
  if (!p || !to || !value) return 0;
  return to_cpp(p)->PassData(std::string(value), std::string(to)) ? 1 : 0;
}

// ─── handle accessors ────────────────────────────────────────────────

YSE_C_API size_t yse_phandle_get_type(YsePHandle* h, char* buf, size_t cap) {
  if (!h) { if (buf && cap > 0) buf[0] = '\0'; return 0; }
  const char* s = to_cpp(h)->Type();
  return copy_string(s ? std::string(s) : std::string(), buf, cap);
}
YSE_C_API size_t yse_phandle_get_name(YsePHandle* h, char* buf, size_t cap) {
  if (!h) { if (buf && cap > 0) buf[0] = '\0'; return 0; }
  return copy_string(to_cpp(h)->GetName(), buf, cap);
}
YSE_C_API size_t yse_phandle_get_params(YsePHandle* h, char* buf, size_t cap) {
  if (!h) { if (buf && cap > 0) buf[0] = '\0'; return 0; }
  return copy_string(to_cpp(h)->GetParams(), buf, cap);
}
YSE_C_API size_t yse_phandle_get_gui_value(YsePHandle* h, char* buf, size_t cap) {
  if (!h) { if (buf && cap > 0) buf[0] = '\0'; return 0; }
  return copy_string(to_cpp(h)->GetGuiValue(), buf, cap);
}
YSE_C_API size_t yse_phandle_get_gui_property(YsePHandle* h, const char* key, char* buf, size_t cap) {
  if (!h || !key) { if (buf && cap > 0) buf[0] = '\0'; return 0; }
  return copy_string(to_cpp(h)->GetGuiProperty(key), buf, cap);
}
YSE_C_API void yse_phandle_set_gui_property(YsePHandle* h, const char* key, const char* value) {
  if (!h || !key) return;
  to_cpp(h)->SetGuiProperty(key, value ? std::string(value) : std::string());
}

YSE_C_API void yse_phandle_set_bang(YsePHandle* h, unsigned int inlet)             { if (h) to_cpp(h)->SetBang(inlet); }
YSE_C_API void yse_phandle_set_int(YsePHandle* h, unsigned int inlet, int v)       { if (h) to_cpp(h)->SetIntData(inlet, v); }
YSE_C_API void yse_phandle_set_float(YsePHandle* h, unsigned int inlet, float v)   { if (h) to_cpp(h)->SetFloatData(inlet, v); }
YSE_C_API void yse_phandle_set_list(YsePHandle* h, unsigned int inlet, const char* v) {
  if (h && v) to_cpp(h)->SetListData(inlet, v);
}
YSE_C_API void yse_phandle_set_params(YsePHandle* h, const char* args) {
  if (h && args) to_cpp(h)->SetParams(args);
}

YSE_C_API int yse_phandle_get_inputs(YsePHandle* h)                              { return h ? to_cpp(h)->GetInputs()  : 0; }
YSE_C_API int yse_phandle_get_outputs(YsePHandle* h)                             { return h ? to_cpp(h)->GetOutputs() : 0; }
YSE_C_API int yse_phandle_is_dsp_input(YsePHandle* h, unsigned int inlet)        { return h && to_cpp(h)->IsDSPInput(inlet) ? 1 : 0; }
YSE_C_API YseOutType yse_phandle_output_data_type(YsePHandle* h, unsigned int pin) {
  return h ? static_cast<YseOutType>(to_cpp(h)->OutputDataType(pin)) : YSE_OUT_INVALID;
}
YSE_C_API unsigned int yse_phandle_get_id(YsePHandle* h) { return h ? to_cpp(h)->GetID() : 0; }

YSE_C_API unsigned int yse_phandle_get_connections(YsePHandle* h, unsigned int outlet) {
  return h ? to_cpp(h)->GetConnections(outlet) : 0;
}
YSE_C_API unsigned int yse_phandle_get_connection_target(YsePHandle* h, unsigned int outlet, unsigned int connection) {
  return h ? to_cpp(h)->GetConnectionTarget(outlet, connection) : 0;
}
YSE_C_API unsigned int yse_phandle_get_connection_target_inlet(YsePHandle* h, unsigned int outlet, unsigned int connection) {
  return h ? to_cpp(h)->GetConnectionTargetInlet(outlet, connection) : 0;
}

} // extern "C"
