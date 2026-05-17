/*
  yse_system.h — engine lifecycle, audio device control, global settings.
  C ABI mirror of YseEngine/system.hpp (YSE::system class + YSE::System() singleton accessor).
*/

#ifndef YSE_C_SYSTEM_H_INCLUDED
#define YSE_C_SYSTEM_H_INCLUDED

#include "yse_common.h"
#include "yse_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct YseSystem  YseSystem;
typedef struct YseChannel YseChannel;
typedef struct YseReverb  YseReverb;
typedef struct YseDevice  YseDevice;
typedef struct YseDeviceSetup YseDeviceSetup;

/* Borrowed singleton pointer — never destroy. */
YSE_C_API YseSystem* yse_system_get(void);

/* Lifecycle. */
YSE_C_API YseStatus  yse_system_init(YseSystem* sys);
YSE_C_API YseStatus  yse_system_init_offline(YseSystem* sys);
YSE_C_API void       yse_system_render_offline(YseSystem* sys, int blocks);
YSE_C_API void       yse_system_update(YseSystem* sys);
YSE_C_API void       yse_system_close(YseSystem* sys);
YSE_C_API void       yse_system_pause(YseSystem* sys);
YSE_C_API void       yse_system_resume(YseSystem* sys);

/* Diagnostics. */
YSE_C_API int        yse_system_missed_callbacks(YseSystem* sys);
YSE_C_API float      yse_system_cpu_load(YseSystem* sys);

/* Convenience. */
YSE_C_API void       yse_system_sleep(YseSystem* sys, unsigned int ms);

/* Settings. */
YSE_C_API void       yse_system_set_max_sounds(YseSystem* sys, int value);
YSE_C_API int        yse_system_get_max_sounds(YseSystem* sys);
YSE_C_API void       yse_system_audio_test(YseSystem* sys, int on);
YSE_C_API void       yse_system_auto_reconnect(YseSystem* sys, int on, int delay_ms);

/* Devices. Returned YseDevice* pointers are borrowed from the engine and
   must not be destroyed. See yse_device.h for the descriptor accessors. */
YSE_C_API unsigned int yse_system_num_devices(YseSystem* sys);
YSE_C_API YseDevice*   yse_system_get_device(YseSystem* sys, unsigned int idx);
YSE_C_API YseStatus    yse_system_open_device(YseSystem* sys, const YseDeviceSetup* setup, YseChannelType layout);
YSE_C_API void         yse_system_close_current_device(YseSystem* sys);
YSE_C_API size_t       yse_system_default_device(YseSystem* sys, char* buf, size_t cap);
YSE_C_API size_t       yse_system_default_host(YseSystem* sys, char* buf, size_t cap);

/* Global reverb — fallback wherever no positioned reverb zone reaches.
   Returned pointer is borrowed; never destroy. */
YSE_C_API YseReverb*   yse_system_get_global_reverb(YseSystem* sys);

/* Underwater effect: routes a channel through the built-in low-pass /
   pitch-shift "underwater" filter. Depth is in [0.0, 1.0]. */
YSE_C_API void         yse_system_underwater_fx(YseSystem* sys, const YseChannel* target);
YSE_C_API void         yse_system_set_underwater_depth(YseSystem* sys, float depth);

#ifdef __cplusplus
}
#endif

#endif
