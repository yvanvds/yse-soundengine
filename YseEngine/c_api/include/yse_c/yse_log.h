/*
  yse_log.h — engine logging.
  C ABI mirror of YseEngine/log.hpp.

  Two ways to consume log output:

    1. Default file sink (set with yse_log_set_logfile / inspect with
       yse_log_get_logfile).
    2. C callback installed via yse_log_set_callback. The callback is
       invoked from whichever thread emitted the log entry — keep it
       cheap and re-entrant. Pass NULL to restore the default file sink.

  Logging is only active between yse_system_init() and yse_system_close().
*/

#ifndef YSE_C_LOG_H_INCLUDED
#define YSE_C_LOG_H_INCLUDED

#include "yse_common.h"
#include "yse_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct YseLog YseLog;

YSE_C_API YseLog*       yse_log_get(void);

YSE_C_API void          yse_log_send_message(YseLog* log, const char* msg);
YSE_C_API void          yse_log_set_level(YseLog* log, YseErrorLevel level);
YSE_C_API YseErrorLevel yse_log_get_level(YseLog* log);
YSE_C_API void          yse_log_set_logfile(YseLog* log, const char* path);
YSE_C_API size_t        yse_log_get_logfile(YseLog* log, char* buf, size_t cap);

/* The receiver OWNS msg and must release it with yse_log_free_message
   when finished. The pointer is allocated with malloc by the bridge so
   any C client can free() it directly if preferred. */
typedef void (YSE_C_CALLBACK *YseLogCallback)(char* msg, void* user_data);

/* Replaces the default file sink. Pass NULL for cb to restore the default.
   user_data is opaque — forwarded to every callback invocation. */
YSE_C_API void          yse_log_set_callback(YseLog* log, YseLogCallback cb, void* user_data);

/* Release a message string previously delivered to a YseLogCallback. */
YSE_C_API void          yse_log_free_message(char* msg);

#ifdef __cplusplus
}
#endif

#endif
