#include "yse_c/yse_patcher.h"
#include "yse_c_internal.hpp"

#include "../patcher/patcher.hpp"
#include "../patcher/pHandle.hpp"
#include "../patcher/pObject.h"
#include "../patcher/pRegistry.h"
#include "../patcher/pEnums.h"
#include "../patcher/inlet.h"
#include "../patcher/outlet.h"
#include "../patcher/parameters.h"
#include "../headers/enums.hpp"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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

  // ── Registry metadata cache ─────────────────────────────────────────
  // Built lazily on the first metadata API call by instantiating every
  // registered object once and copying its in-code doc fields into a
  // process-static structure. Strings are owned by the cache; their
  // c_str() pointers stay valid until process exit, which is the
  // lifetime contract the header documents.
  //
  // Main-thread only — std::call_once / function-local static
  // initialisation is fine here because none of these calls are reached
  // from the audio callback.

  struct InletMeta {
    std::string label;
    std::string doc;
    std::string range;
    unsigned int accepts;
  };
  struct OutletMeta {
    std::string label;
    std::string doc;
    std::string range;
    YseOutType type;
  };
  struct ParamMeta {
    std::string name;
    std::string defaultValue;
    std::string doc;
    std::string range;
  };
  struct TypeMeta {
    std::string name;
    std::string description;
    YsePCategory category;
    bool isDsp;
    std::vector<InletMeta> inlets;
    std::vector<OutletMeta> outlets;
    std::vector<ParamMeta> params;
  };

  YsePCategory mapCategory(YSE::PATCHER::pCategory c) {
    using YSE::PATCHER::pCategory;
    switch (c) {
    case pCategory::UNSET:
      return YSE_PCAT_UNSET;
    case pCategory::OSC:
      return YSE_PCAT_OSC;
    case pCategory::FILTER:
      return YSE_PCAT_FILTER;
    case pCategory::MATH:
      return YSE_PCAT_MATH;
    case pCategory::GENERIC:
      return YSE_PCAT_GENERIC;
    case pCategory::GUI:
      return YSE_PCAT_GUI;
    case pCategory::TIME:
      return YSE_PCAT_TIME;
    case pCategory::MIDI:
      return YSE_PCAT_MIDI;
    }
    return YSE_PCAT_UNSET;
  }

  const char* categoryName(YsePCategory c) {
    switch (c) {
    case YSE_PCAT_UNSET:
      return "UNSET";
    case YSE_PCAT_OSC:
      return "OSC";
    case YSE_PCAT_FILTER:
      return "FILTER";
    case YSE_PCAT_MATH:
      return "MATH";
    case YSE_PCAT_GENERIC:
      return "GENERIC";
    case YSE_PCAT_GUI:
      return "GUI";
    case YSE_PCAT_TIME:
      return "TIME";
    case YSE_PCAT_MIDI:
      return "MIDI";
    }
    return "UNKNOWN";
  }

  const char* outTypeName(YseOutType t) {
    switch (t) {
    case YSE_OUT_INVALID:
      return "INVALID";
    case YSE_OUT_BANG:
      return "BANG";
    case YSE_OUT_FLOAT:
      return "FLOAT";
    case YSE_OUT_INT:
      return "INT";
    case YSE_OUT_BUFFER:
      return "BUFFER";
    case YSE_OUT_LIST:
      return "LIST";
    case YSE_OUT_ANY:
      return "ANY";
    }
    return "UNKNOWN";
  }

  struct MetaCache {
    std::vector<TypeMeta> types;
    std::unordered_map<std::string, size_t> indexByName;
  };

  const MetaCache& buildCache() {
    static const MetaCache cache = [] {
      MetaCache c;
      auto names = YSE::PATCHER::Register().AllNames();
      c.types.reserve(names.size());
      for (const auto& name : names) {
        std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(name));
        if (!obj) continue;

        TypeMeta tm;
        tm.name = name;
        tm.description = obj->GetDescription();
        tm.category = mapCategory(obj->GetCategory());
        tm.isDsp = obj->IsDSPObject();

        tm.inlets.reserve(static_cast<size_t>(obj->NumInputs()));
        for (int i = 0; i < obj->NumInputs(); ++i) {
          auto* port = obj->GetInlet(i);
          InletMeta im;
          if (port) {
            im.label = port->GetDocLabel();
            im.doc = port->GetDocDescription();
            im.range = port->GetRange();
            im.accepts = port->GetAcceptedTypes();
          } else {
            im.accepts = 0;
          }
          tm.inlets.push_back(std::move(im));
        }

        tm.outlets.reserve(static_cast<size_t>(obj->NumOutputs()));
        for (int i = 0; i < obj->NumOutputs(); ++i) {
          auto* port = obj->GetOutlet(i);
          OutletMeta om;
          om.type = static_cast<YseOutType>(obj->GetOutputType(static_cast<unsigned int>(i)));
          if (port) {
            om.label = port->GetDocLabel();
            om.doc = port->GetDocDescription();
            om.range = port->GetRange();
          }
          tm.outlets.push_back(std::move(om));
        }

        const auto& paramDocs = obj->GetParamDocs();
        tm.params.reserve(paramDocs.size());
        for (const auto& pd : paramDocs) {
          ParamMeta pm;
          pm.name = pd.name;
          pm.defaultValue = pd.defaultValue;
          pm.doc = pd.doc;
          pm.range = pd.range;
          tm.params.push_back(std::move(pm));
        }

        c.indexByName.emplace(tm.name, c.types.size());
        c.types.push_back(std::move(tm));
      }
      return c;
    }();
    return cache;
  }

  const TypeMeta* findType(const char* type_name) {
    if (!type_name) return nullptr;
    const auto& cache = buildCache();
    auto it = cache.indexByName.find(type_name);
    if (it == cache.indexByName.end()) return nullptr;
    return &cache.types[it->second];
  }

} // namespace

extern "C" {

YSE_C_API YsePatcher* yse_patcher_create(void) {
  try {
    return reinterpret_cast<YsePatcher*>(new YSE::patcher());
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("patcher_create: unknown C++ exception");
    return nullptr;
  }
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
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("patcher_create_object: unknown C++ exception");
    return nullptr;
  }
}

YSE_C_API void yse_patcher_delete_object(YsePatcher* p, YsePHandle* obj) {
  if (!p || !obj) return;
  to_cpp(p)->DeleteObject(to_cpp(obj));
}

YSE_C_API void yse_patcher_clear(YsePatcher* p) {
  if (p) to_cpp(p)->Clear();
}

YSE_C_API void yse_patcher_connect(YsePatcher* p, YsePHandle* from, int outlet, YsePHandle* to,
                                   int inlet) {
  if (!p || !from || !to) return;
  to_cpp(p)->Connect(to_cpp(from), outlet, to_cpp(to), inlet);
}

YSE_C_API void yse_patcher_disconnect(YsePatcher* p, YsePHandle* from, int outlet, YsePHandle* to,
                                      int inlet) {
  if (!p || !from || !to) return;
  to_cpp(p)->Disconnect(to_cpp(from), outlet, to_cpp(to), inlet);
}

YSE_C_API int yse_patcher_is_valid_object(const char* type) {
  if (!type) return 0;
  return YSE::patcher::IsValidObject(type) ? 1 : 0;
}

YSE_C_API size_t yse_patcher_dump_json(YsePatcher* p, char* buf, size_t cap) {
  if (!p) {
    if (buf && cap > 0) buf[0] = '\0';
    return 0;
  }
  try {
    return copy_string(to_cpp(p)->DumpJSON(), buf, cap);
  } catch (const std::exception&) {
    if (buf && cap > 0) buf[0] = '\0';
    return 0;
  }
}

YSE_C_API void yse_patcher_parse_json(YsePatcher* p, const char* content) {
  if (!p || !content) return;
  try {
    to_cpp(p)->ParseJSON(content);
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
  } catch (...) {
    yse_c::set_last_error("patcher_parse_json: unknown C++ exception");
  }
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
  if (!h) {
    if (buf && cap > 0) buf[0] = '\0';
    return 0;
  }
  const char* s = to_cpp(h)->Type();
  return copy_string(s ? std::string(s) : std::string(), buf, cap);
}
YSE_C_API size_t yse_phandle_get_name(YsePHandle* h, char* buf, size_t cap) {
  if (!h) {
    if (buf && cap > 0) buf[0] = '\0';
    return 0;
  }
  return copy_string(to_cpp(h)->GetName(), buf, cap);
}
YSE_C_API size_t yse_phandle_get_params(YsePHandle* h, char* buf, size_t cap) {
  if (!h) {
    if (buf && cap > 0) buf[0] = '\0';
    return 0;
  }
  return copy_string(to_cpp(h)->GetParams(), buf, cap);
}
YSE_C_API size_t yse_phandle_get_gui_value(YsePHandle* h, char* buf, size_t cap) {
  if (!h) {
    if (buf && cap > 0) buf[0] = '\0';
    return 0;
  }
  return copy_string(to_cpp(h)->GetGuiValue(), buf, cap);
}
YSE_C_API size_t yse_phandle_get_gui_property(YsePHandle* h, const char* key, char* buf,
                                              size_t cap) {
  if (!h || !key) {
    if (buf && cap > 0) buf[0] = '\0';
    return 0;
  }
  return copy_string(to_cpp(h)->GetGuiProperty(key), buf, cap);
}
YSE_C_API void yse_phandle_set_gui_property(YsePHandle* h, const char* key, const char* value) {
  if (!h || !key) return;
  to_cpp(h)->SetGuiProperty(key, value ? std::string(value) : std::string());
}

YSE_C_API void yse_phandle_set_bang(YsePHandle* h, unsigned int inlet) {
  if (h) to_cpp(h)->SetBang(inlet);
}
YSE_C_API void yse_phandle_set_int(YsePHandle* h, unsigned int inlet, int v) {
  if (h) to_cpp(h)->SetIntData(inlet, v);
}
YSE_C_API void yse_phandle_set_float(YsePHandle* h, unsigned int inlet, float v) {
  if (h) to_cpp(h)->SetFloatData(inlet, v);
}
YSE_C_API void yse_phandle_set_list(YsePHandle* h, unsigned int inlet, const char* v) {
  if (h && v) to_cpp(h)->SetListData(inlet, v);
}
YSE_C_API void yse_phandle_set_params(YsePHandle* h, const char* args) {
  if (h && args) to_cpp(h)->SetParams(args);
}

YSE_C_API int yse_phandle_get_inputs(YsePHandle* h) {
  return h ? to_cpp(h)->GetInputs() : 0;
}
YSE_C_API int yse_phandle_get_outputs(YsePHandle* h) {
  return h ? to_cpp(h)->GetOutputs() : 0;
}
YSE_C_API int yse_phandle_is_dsp_input(YsePHandle* h, unsigned int inlet) {
  return h && to_cpp(h)->IsDSPInput(inlet) ? 1 : 0;
}
YSE_C_API YseOutType yse_phandle_output_data_type(YsePHandle* h, unsigned int pin) {
  return h ? static_cast<YseOutType>(to_cpp(h)->OutputDataType(pin)) : YSE_OUT_INVALID;
}
YSE_C_API unsigned int yse_phandle_get_id(YsePHandle* h) {
  return h ? to_cpp(h)->GetID() : 0;
}

YSE_C_API unsigned int yse_phandle_get_connections(YsePHandle* h, unsigned int outlet) {
  return h ? to_cpp(h)->GetConnections(outlet) : 0;
}
YSE_C_API unsigned int yse_phandle_get_connection_target(YsePHandle* h, unsigned int outlet,
                                                         unsigned int connection) {
  return h ? to_cpp(h)->GetConnectionTarget(outlet, connection) : 0;
}
YSE_C_API unsigned int yse_phandle_get_connection_target_inlet(YsePHandle* h, unsigned int outlet,
                                                               unsigned int connection) {
  return h ? to_cpp(h)->GetConnectionTargetInlet(outlet, connection) : 0;
}

// ─── registry metadata ───────────────────────────────────────────────

YSE_C_API int yse_patcher_get_type_count(void) {
  return static_cast<int>(buildCache().types.size());
}

YSE_C_API const char* yse_patcher_get_type_name(int index) {
  const auto& cache = buildCache();
  if (index < 0 || static_cast<size_t>(index) >= cache.types.size()) return "";
  return cache.types[static_cast<size_t>(index)].name.c_str();
}

YSE_C_API const char* yse_patcher_get_type_description(const char* type_name) {
  const TypeMeta* t = findType(type_name);
  return t ? t->description.c_str() : "";
}

YSE_C_API YsePCategory yse_patcher_get_type_category(const char* type_name) {
  const TypeMeta* t = findType(type_name);
  return t ? t->category : YSE_PCAT_UNSET;
}

YSE_C_API int yse_patcher_get_type_is_dsp(const char* type_name) {
  const TypeMeta* t = findType(type_name);
  return (t && t->isDsp) ? 1 : 0;
}

YSE_C_API int yse_patcher_get_inlet_count(const char* type_name) {
  const TypeMeta* t = findType(type_name);
  return t ? static_cast<int>(t->inlets.size()) : 0;
}

YSE_C_API void yse_patcher_get_inlet_info(const char* type_name, int idx, const char** label,
                                          const char** doc, const char** range,
                                          unsigned int* accepts_bitmask) {
  const TypeMeta* t = findType(type_name);
  if (!t || idx < 0 || static_cast<size_t>(idx) >= t->inlets.size()) {
    if (label) *label = "";
    if (doc) *doc = "";
    if (range) *range = "";
    if (accepts_bitmask) *accepts_bitmask = 0;
    return;
  }
  const InletMeta& in = t->inlets[static_cast<size_t>(idx)];
  if (label) *label = in.label.c_str();
  if (doc) *doc = in.doc.c_str();
  if (range) *range = in.range.c_str();
  if (accepts_bitmask) *accepts_bitmask = in.accepts;
}

YSE_C_API int yse_patcher_get_outlet_count(const char* type_name) {
  const TypeMeta* t = findType(type_name);
  return t ? static_cast<int>(t->outlets.size()) : 0;
}

YSE_C_API void yse_patcher_get_outlet_info(const char* type_name, int idx, const char** label,
                                           const char** doc, const char** range, YseOutType* type) {
  const TypeMeta* t = findType(type_name);
  if (!t || idx < 0 || static_cast<size_t>(idx) >= t->outlets.size()) {
    if (label) *label = "";
    if (doc) *doc = "";
    if (range) *range = "";
    if (type) *type = YSE_OUT_INVALID;
    return;
  }
  const OutletMeta& out = t->outlets[static_cast<size_t>(idx)];
  if (label) *label = out.label.c_str();
  if (doc) *doc = out.doc.c_str();
  if (range) *range = out.range.c_str();
  if (type) *type = out.type;
}

YSE_C_API int yse_patcher_get_param_count(const char* type_name) {
  const TypeMeta* t = findType(type_name);
  return t ? static_cast<int>(t->params.size()) : 0;
}

YSE_C_API void yse_patcher_get_param_info(const char* type_name, int idx, const char** name,
                                          const char** doc, const char** default_value,
                                          const char** range) {
  const TypeMeta* t = findType(type_name);
  if (!t || idx < 0 || static_cast<size_t>(idx) >= t->params.size()) {
    if (name) *name = "";
    if (doc) *doc = "";
    if (default_value) *default_value = "";
    if (range) *range = "";
    return;
  }
  const ParamMeta& pm = t->params[static_cast<size_t>(idx)];
  if (name) *name = pm.name.c_str();
  if (doc) *doc = pm.doc.c_str();
  if (default_value) *default_value = pm.defaultValue.c_str();
  if (range) *range = pm.range.c_str();
}

YSE_C_API char* yse_patcher_get_metadata_json(void) {
  // Mirrors tools/dump_patcher_metadata/main.cpp exactly so binding-side
  // consumers can swap between the file-on-disk snapshot and the in-memory
  // string without noticing the difference.
  try {
    nlohmann::json root = nlohmann::json::object();
    const auto& cache = buildCache();
    for (const auto& tm : cache.types) {
      nlohmann::json entry = nlohmann::json::object();
      entry["name"] = tm.name;
      entry["category"] = categoryName(tm.category);
      entry["is_dsp"] = tm.isDsp;
      entry["description"] = tm.description;

      nlohmann::json inlets = nlohmann::json::array();
      for (size_t i = 0; i < tm.inlets.size(); ++i) {
        const auto& in = tm.inlets[i];
        nlohmann::json p = nlohmann::json::object();
        p["index"] = static_cast<int>(i);
        p["label"] = in.label;
        p["doc"] = in.doc;
        p["range"] = in.range;
        nlohmann::json accepts = nlohmann::json::array();
        if (in.accepts & YSE_IN_ACCEPTS_BUFFER) accepts.push_back("BUFFER");
        if (in.accepts & YSE_IN_ACCEPTS_FLOAT) accepts.push_back("FLOAT");
        if (in.accepts & YSE_IN_ACCEPTS_INT) accepts.push_back("INT");
        if (in.accepts & YSE_IN_ACCEPTS_BANG) accepts.push_back("BANG");
        if (in.accepts & YSE_IN_ACCEPTS_LIST) accepts.push_back("LIST");
        p["accepts"] = std::move(accepts);
        inlets.push_back(std::move(p));
      }
      entry["inlets"] = std::move(inlets);

      nlohmann::json outlets = nlohmann::json::array();
      for (size_t i = 0; i < tm.outlets.size(); ++i) {
        const auto& out = tm.outlets[i];
        nlohmann::json p = nlohmann::json::object();
        p["index"] = static_cast<int>(i);
        p["label"] = out.label;
        p["doc"] = out.doc;
        p["range"] = out.range;
        p["type"] = outTypeName(out.type);
        outlets.push_back(std::move(p));
      }
      entry["outlets"] = std::move(outlets);

      nlohmann::json params = nlohmann::json::array();
      for (const auto& pm : tm.params) {
        nlohmann::json p = nlohmann::json::object();
        p["name"] = pm.name;
        p["default"] = pm.defaultValue;
        p["doc"] = pm.doc;
        p["range"] = pm.range;
        params.push_back(std::move(p));
      }
      entry["params"] = std::move(params);

      root[tm.name] = std::move(entry);
    }
    const std::string serialized = root.dump(2);
    char* buf = static_cast<char*>(std::malloc(serialized.size() + 1));
    if (!buf) {
      yse_c::set_last_error("yse_patcher_get_metadata_json: out of memory");
      return nullptr;
    }
    std::memcpy(buf, serialized.data(), serialized.size());
    buf[serialized.size()] = '\0';
    return buf;
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return nullptr;
  } catch (...) {
    yse_c::set_last_error("yse_patcher_get_metadata_json: unknown C++ exception");
    return nullptr;
  }
}

YSE_C_API void yse_free_string(char* s) {
  std::free(s);
}

} // extern "C"
