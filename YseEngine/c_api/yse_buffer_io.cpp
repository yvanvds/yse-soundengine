#include "yse_c/yse_buffer_io.h"
#include "yse_c_internal.hpp"

#include "../BufferIO.hpp"

#include <exception>

namespace {
  inline YSE::BufferIO* to_cpp(YseBufferIO* io) {
    return reinterpret_cast<YSE::BufferIO*>(io);
  }
}

extern "C" {

YSE_C_API YseBufferIO* yse_buffer_io_create(int store_copy) {
  try { return reinterpret_cast<YseBufferIO*>(new YSE::BufferIO(store_copy != 0)); }
  catch (const std::exception& e) { yse_c::set_last_error(e.what()); return nullptr; }
  catch (...) { yse_c::set_last_error("buffer_io_create: unknown C++ exception"); return nullptr; }
}

YSE_C_API void yse_buffer_io_destroy(YseBufferIO* io) {
  if (io) delete to_cpp(io);
}

YSE_C_API void yse_buffer_io_set_active(YseBufferIO* io, int on) {
  if (io) to_cpp(io)->SetActive(on != 0);
}
YSE_C_API int yse_buffer_io_get_active(YseBufferIO* io) {
  return io && to_cpp(io)->GetActive() ? 1 : 0;
}

YSE_C_API int yse_buffer_io_name_exists(YseBufferIO* io, const char* id) {
  if (!io || !id) return 0;
  return to_cpp(io)->BufferNameExists(id) ? 1 : 0;
}

YSE_C_API int yse_buffer_io_add(YseBufferIO* io, const char* id, char* buffer, int length) {
  if (!io || !id || !buffer || length <= 0) return 0;
  return to_cpp(io)->AddBuffer(id, buffer, length) ? 1 : 0;
}

YSE_C_API int yse_buffer_io_remove_by_name(YseBufferIO* io, const char* id) {
  if (!io || !id) return 0;
  return to_cpp(io)->RemoveBufferByName(id) ? 1 : 0;
}

} // extern "C"
