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
/* Restarts the device this session already had. A session brought up with
   yse_system_init_offline() has none, so this is a no-op there rather than an
   open of the platform default; yse_system_open_device() is the deliberate way
   to give such a session a device (issue #719). */
YSE_C_API void yse_system_resume(YseSystem* sys);

/* Diagnostics.

   yse_system_missed_callbacks() counts consecutive yse_system_update() ticks
   during which the device delivered no audio callback; 0 means audio is
   flowing right now. A stream that has just been started reads non-zero until
   its first callback lands (the backend's start call returns before the device
   runs), so poll this until it reaches 0 to wait for a device to come up — a
   value that keeps climbing is a starved audio thread or a disconnected
   device. yse_system_auto_reconnect() does not act on this counter; it
   distinguishes start-up from a stall itself (issue #681). */
YSE_C_API int yse_system_missed_callbacks(YseSystem* sys);
YSE_C_API float yse_system_cpu_load(YseSystem* sys);

/* Request the audio sample rate for the next engine session (issue #646).
   The sample rate is an application setting, fixed per session: call this
   before yse_system_init / yse_system_init_offline. The device may refuse or
   negotiate a different rate — the negotiated result stays authoritative and
   is reported by yse_system_get_sample_rate(); a refused request is a log
   line plus the negotiated fallback, never an error. Offline / headless
   sessions run exactly at the requested rate. Pass 0 to clear the request
   (the backend then opens at the device default; the pre-negotiation initial
   value is 48000). Changing a running session's rate means yse_system_close()
   followed by a new init; calling this mid-session only stores the request
   for the next session. The request survives close. */
YSE_C_API void yse_system_request_sample_rate(YseSystem* sys, unsigned int rate_hz);

/* Currently requested sample rate in Hz, or 0 when no request is set. */
YSE_C_API unsigned int yse_system_get_requested_sample_rate(YseSystem* sys);

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

/* Re-open the audio output stream when the device stops delivering callbacks
   (headphones unplugged, device removed). delay_ms is what its name has always
   said and now is: milliseconds of silence before an attempt, and the interval
   between further attempts while the device stays unavailable. Negative values
   are clamped to 0. Up to and including v2.4.0 the value was compared against a
   count of yse_system_update() calls instead, so the same number meant a
   different wait per host (issue #681).

   A stream that has been started but has not delivered its first callback yet
   is not stalled, and is never torn down for it: it gets a half-second start-up
   grace regardless of delay_ms. */
YSE_C_API void yse_system_auto_reconnect(YseSystem* sys, int on, int delay_ms);

/* Devices. Returned YseDevice* pointers are borrowed from the engine and
   must not be destroyed. See yse_device.h for the descriptor accessors.

   yse_system_get_device() is bound-checked: an idx at or beyond
   yse_system_num_devices() returns NULL and sets the last error, so probing
   or iterating with a stale count is safe. An offline or headless session
   enumerates no devices at all, which makes index 0 out of range. */
YSE_C_API unsigned int yse_system_num_devices(YseSystem* sys);
YSE_C_API YseDevice* yse_system_get_device(YseSystem* sys, unsigned int idx);
YSE_C_API YseStatus yse_system_open_device(YseSystem* sys, const YseDeviceSetup* setup,
                                           YseChannelType layout);
YSE_C_API void yse_system_close_current_device(YseSystem* sys);

/* Set the speaker layout without opening a device (issue #668).

   yse_system_open_device() derives the layout from the device it actually
   opened, so a session with no device — yse_system_init_offline(), headless
   CI, yse_system_render_offline() benchmarks — otherwise stays on the stereo
   layout init installs. Call this after init/init_offline (initialisation
   installs the stereo default itself, so an earlier call is overwritten); the
   next audio callback or render_offline block picks the new layout up and
   resizes the mixer to match.

   `outputs` is the number of output channels and must be at least 1; a
   zero-output layout would silence the engine, so it is refused with a log
   line. YSE_CT_AUTO derives a layout from `outputs`; YSE_CT_CUSTOM allocates
   `outputs` speakers whose positions are set afterwards. */
YSE_C_API void yse_system_set_channel_configuration(YseSystem* sys, YseChannelType layout,
                                                    int outputs);
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

/* Underwater effect: attaches the engine's underwater insert module to the
   channel's insert slot (the same slot yse_channel_set_dsp uses; the two
   replace each other). Depth is the listener's distance below the water
   surface in world units: <= 0 disables the effect, the low-passed
   position-neutral treatment fades in above 1 and saturates at 5. A positive
   depth also enables the built-in underwater reverb zone. */
YSE_C_API void yse_system_underwater_fx(YseSystem* sys, const YseChannel* target);
YSE_C_API void yse_system_set_underwater_depth(YseSystem* sys, float depth);

#ifdef __cplusplus
}
#endif

#endif
