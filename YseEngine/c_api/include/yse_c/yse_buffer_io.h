/*
  yse_buffer_io.h — feed sound files from in-memory byte buffers.
  C ABI mirror of YseEngine/BufferIO.hpp.

  Register bytes under string IDs, then load sounds by passing those
  IDs where a file path would normally go. Used for asset packs and
  bundled-resource scenarios where the OS file system isn't available
  (Android APKs, encrypted asset bundles, etc.).
*/

#ifndef YSE_C_BUFFER_IO_H_INCLUDED
#define YSE_C_BUFFER_IO_H_INCLUDED

#include "yse_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Owned — release with yse_buffer_io_destroy. */
typedef struct YseBufferIO YseBufferIO;

/* store_copy=1 copies the supplied bytes; store_copy=0 keeps a pointer
   to the caller's buffer (which must outlive the registration). */
YSE_C_API YseBufferIO* yse_buffer_io_create(int store_copy);
YSE_C_API void         yse_buffer_io_destroy(YseBufferIO* io);

YSE_C_API void         yse_buffer_io_set_active(YseBufferIO* io, int on);
YSE_C_API int          yse_buffer_io_get_active(YseBufferIO* io);

YSE_C_API int          yse_buffer_io_name_exists(YseBufferIO* io, const char* id);
YSE_C_API int          yse_buffer_io_add(YseBufferIO* io, const char* id, char* buffer, int length);
YSE_C_API int          yse_buffer_io_remove_by_name(YseBufferIO* io, const char* id);

#ifdef __cplusplus
}
#endif

#endif
