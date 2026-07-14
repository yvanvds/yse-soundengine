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

/* Borrowed singleton — owned by the engine, never destroy.
   Obtain via yse_system_get(). */
typedef struct YseSystem YseSystem;
/* Forward declarations — see yse_channel.h / yse_reverb.h / yse_device.h
   for ownership semantics. */
typedef struct YseChannel YseChannel;
typedef struct YseReverb YseReverb;
typedef struct YseDevice YseDevice;
typedef struct YseDeviceSetup YseDeviceSetup;

/* Borrowed singleton pointer — never destroy. */
YSE_C_API YseSystem* yse_system_get(void);

/* Lifecycle. */
YSE_C_API YseStatus yse_system_init(YseSystem* sys);
YSE_C_API YseStatus yse_system_init_offline(YseSystem* sys);
YSE_C_API void yse_system_render_offline(YseSystem* sys, int blocks);
YSE_C_API void yse_system_update(YseSystem* sys);
YSE_C_API void yse_system_close(YseSystem* sys);
YSE_C_API void yse_system_pause(YseSystem* sys);
YSE_C_API void yse_system_resume(YseSystem* sys);

/* Diagnostics. */
YSE_C_API int yse_system_missed_callbacks(YseSystem* sys);
YSE_C_API float yse_system_cpu_load(YseSystem* sys);

/* Engine session sample rate in Hz. Stays constant for the lifetime of an
   init()/close() session, including across pause/resume cycles where the
   live "active" rate transiently drops to 0. Returns 0 before init(). Use
   this for sample-count-driven scheduling that must outlive a pause; use
   yse_system_get_active_sample_rate() for live device-state UI. */
YSE_C_API double yse_system_get_sample_rate(YseSystem* sys);

/* Live state of the currently open audio device. Returns 0 when no device
   is open (pre-init, after close, or initOffline path). Buffer size is the
   device's frames-per-callback, NOT the engine block size. Output latency
   is in samples; convert to ms with (latency / sample_rate) * 1000. */
YSE_C_API double yse_system_get_active_sample_rate(YseSystem* sys);
YSE_C_API int yse_system_get_active_buffer_size(YseSystem* sys);
YSE_C_API int yse_system_get_active_output_latency(YseSystem* sys);

/* Convenience. */
YSE_C_API void yse_system_sleep(YseSystem* sys, unsigned int ms);

/* Settings. */
YSE_C_API void yse_system_set_max_sounds(YseSystem* sys, int value);
YSE_C_API int yse_system_get_max_sounds(YseSystem* sys);
YSE_C_API void yse_system_audio_test(YseSystem* sys, int on);
YSE_C_API void yse_system_auto_reconnect(YseSystem* sys, int on, int delay_ms);

/* Devices. Returned YseDevice* pointers are borrowed from the engine and
   must not be destroyed. See yse_device.h for the descriptor accessors. */
YSE_C_API unsigned int yse_system_num_devices(YseSystem* sys);
YSE_C_API YseDevice* yse_system_get_device(YseSystem* sys, unsigned int idx);
YSE_C_API YseStatus yse_system_open_device(YseSystem* sys, const YseDeviceSetup* setup,
                                           YseChannelType layout);
YSE_C_API void yse_system_close_current_device(YseSystem* sys);
YSE_C_API size_t yse_system_default_device(YseSystem* sys, char* buf, size_t cap);
YSE_C_API size_t yse_system_default_host(YseSystem* sys, char* buf, size_t cap);

/* MIDI devices (Windows / Linux only — Android builds report 0). */
YSE_C_API unsigned int yse_system_num_midi_in_devices(YseSystem* sys);
YSE_C_API unsigned int yse_system_num_midi_out_devices(YseSystem* sys);
YSE_C_API size_t yse_system_midi_in_device_name(YseSystem* sys, unsigned int id, char* buf,
                                                size_t cap);
YSE_C_API size_t yse_system_midi_out_device_name(YseSystem* sys, unsigned int id, char* buf,
                                                 size_t cap);

/* Domain clocks (issue #249). A set of named musical (beat) clocks, each a
   beat accumulator derived from the audio callback: every audio block a clock
   advances by blockSeconds * tempo / 60 at its current tempo, so beat position
   is the running integral of tempo (no absolute-time schedule). All clocks
   derive from the single sample clock, keeping polytemporal relationships
   exact. Tempo is a playable, rampable control and is not clamped: 0 pauses a
   clock, a negative tempo runs it backwards.

   Threading: create/destroy/set_tempo are control-thread calls; beat_position
   and current_tempo may be read from the UI thread at frame rate. None run on
   or block the audio callback. `name` is a NUL-terminated UTF-8 string. */

/* Returns 1 on success, 0 if `sys`/`name` is NULL, the name is empty, or a
   live clock already owns the name (first registration wins). */
YSE_C_API int yse_system_create_clock(YseSystem* sys, const char* name, float initial_tempo);

/* Destroy the named clock. No-op for an unknown name or NULL args. */
YSE_C_API void yse_system_destroy_clock(YseSystem* sys, const char* name);

/* Returns 1 if a live clock with `name` exists, else 0. */
YSE_C_API int yse_system_clock_exists(YseSystem* sys, const char* name);

/* Ramp the named clock's tempo toward `bpm` over `ramp_seconds` (0 = instant).
   No-op for an unknown name or NULL args. */
YSE_C_API void yse_system_set_tempo(YseSystem* sys, const char* name, float bpm,
                                    float ramp_seconds);

/* Current beat position (running integral of tempo) of the named clock, or 0
   for an unknown name or NULL args. */
YSE_C_API double yse_system_beat_position(YseSystem* sys, const char* name);

/* Current tempo in BPM of the named clock, or 0 for an unknown name or NULL
   args. */
YSE_C_API float yse_system_current_tempo(YseSystem* sys, const char* name);

/* Global reverb — fallback wherever no positioned reverb zone reaches.
   Returned pointer is borrowed; never destroy. */
YSE_C_API YseReverb* yse_system_get_global_reverb(YseSystem* sys);

/* Underwater effect: routes a channel through the built-in low-pass /
   pitch-shift "underwater" filter. Depth is in [0.0, 1.0]. */
YSE_C_API void yse_system_underwater_fx(YseSystem* sys, const YseChannel* target);
YSE_C_API void yse_system_set_underwater_depth(YseSystem* sys, float depth);

#ifdef __cplusplus
}
#endif

#endif
