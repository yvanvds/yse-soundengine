/*
  yse_channel.h — mixer-tree channels.
  C ABI mirror of YseEngine/channel/channelInterface.hpp (YSE::channel + pre-built
  free-function accessors ChannelMaster, ChannelFX, ChannelMusic, ChannelAmbient,
  ChannelVoice, ChannelGui).

  Convention: every void-returning function in this header is a null-safe
  no-op when called with a NULL handle. Status queries (is_valid, get_*)
  return 0 / false / NULL on NULL.
*/

#ifndef YSE_C_CHANNEL_H_INCLUDED
#define YSE_C_CHANNEL_H_INCLUDED

#include "yse_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Owned via yse_channel_create / _create_with_sends / _create_return —
   release with yse_channel_destroy. Borrowed via the yse_channel_master /
   _fx / _music / _ambient / _voice / _gui pre-built accessors — never
   destroy those. */
typedef struct YseChannel YseChannel;

/* Forward declaration — see yse_dsp_modules.h for ownership (a channel
   insert effect is owned by the caller, never by the channel). */
typedef struct YseDspObject YseDspObject;

/* Pre-built channels — borrowed pointers, never destroy. */
YSE_C_API YseChannel* yse_channel_master(void);
YSE_C_API YseChannel* yse_channel_fx(void);
YSE_C_API YseChannel* yse_channel_music(void);
YSE_C_API YseChannel* yse_channel_ambient(void);
YSE_C_API YseChannel* yse_channel_voice(void);
YSE_C_API YseChannel* yse_channel_gui(void);

/* User-created channels. yse_channel_create allocates a channel with the
   default 4 aux-send slots; yse_channel_create_with_sends chooses the slot
   count (sized once off the audio thread and never resized — raise it for a
   channel that fans out to many return buses). Both return NULL and set
   yse_last_error() on a NULL name/parent. */
YSE_C_API YseChannel* yse_channel_create(const char* name, YseChannel* parent);
YSE_C_API YseChannel* yse_channel_create_with_sends(const char* name, YseChannel* parent,
                                                    int send_slots);
YSE_C_API void yse_channel_destroy(YseChannel* ch);

/* ─── send/return buses (issue #165; design docs/design/send_return_buses.md) ──
 *
 * A return bus is an ordinary channel (it keeps set_dsp inserts, attach_reverb,
 * set_volume, and metering) excluded from the normal mix tree; other channels
 * route scaled copies of their signal into it and its output folds into the
 * master mix after the source tree — the classic aux-send topology. A return
 * may itself send into another return (an acyclic delay→reverb chain); cycles
 * are rejected at wiring time.
 *
 * yse_channel_create_return allocates and flags a return in one call (the C
 * mirror of makeReturn) — call it INSTEAD of yse_channel_create. Destroy it
 * with yse_channel_destroy like any channel. */
YSE_C_API YseChannel* yse_channel_create_return(const char* name, int send_slots);

/* 1 if the channel is a send/return bus, else 0 (also 0 on NULL). */
YSE_C_API int yse_channel_is_return(YseChannel* ch);

/* Wire send slot `slot` (in [0, send_slots)) of `ch` to `return_bus` at
 * `level`. Post-fader by default; pass pre_fader != 0 for a cue-style send
 * independent of the channel fader. Illegal wirings (target not a return, a
 * self-send, a return→return edge that would close a cycle, an out-of-range
 * slot) are rejected on the calling thread and logged — never reaching the
 * audio thread. Null-safe no-op. */
YSE_C_API void yse_channel_send(YseChannel* ch, int slot, YseChannel* return_bus, float level,
                                int pre_fader);

/* Set a send slot's level, ramped and click-free. Safe to call every control
 * tick — send levels are designed as modulation targets, so continuous writes
 * fuse into the per-block ramp without zippering. Null-safe no-op. */
YSE_C_API void yse_channel_set_send_level(YseChannel* ch, int slot, float level);

/* Detach send slot `slot`, fully disconnecting it from its return. Null-safe
 * no-op. */
YSE_C_API void yse_channel_clear_send(YseChannel* ch, int slot);

/* Current target level of send slot `slot`, or 0 if unset/invalid/NULL. */
YSE_C_API float yse_channel_get_send_level(YseChannel* ch, int slot);

/* ─── channel insert DSP (issue #159) ─────────────────────────────────────
 *
 * Attach a pre-fader insert effect chain to this channel — the DAW "insert"
 * slot, mirroring yse_sound_set_dsp at the channel level. The effect processes
 * the channel's summed output in place, before reverb and the channel volume.
 * Chain effects with yse_dsp_object_link. Pass NULL to detach. The channel
 * takes no ownership: the YseDspObject must outlive the channel or be detached
 * first. Null-safe no-op. */
YSE_C_API void yse_channel_set_dsp(YseChannel* ch, YseDspObject* dsp);

/* The currently attached insert effect chain head, or NULL if none/NULL. */
YSE_C_API YseDspObject* yse_channel_get_dsp(YseChannel* ch);

YSE_C_API void yse_channel_set_volume(YseChannel* ch, float value);
YSE_C_API float yse_channel_get_volume(YseChannel* ch);
YSE_C_API void yse_channel_move_to(YseChannel* ch, YseChannel* parent);
YSE_C_API void yse_channel_attach_reverb(YseChannel* ch);
YSE_C_API void yse_channel_set_virtual(YseChannel* ch, int value);
YSE_C_API int yse_channel_get_virtual(YseChannel* ch);
YSE_C_API int yse_channel_is_valid(YseChannel* ch);
YSE_C_API const char* yse_channel_get_name(YseChannel* ch);

/* Output peak metering — see channelInterface.hpp for semantics.
 *
 * "Pre" reads the peak measured at the end of dsp() (after reverb/underwater FX,
 * before the channel volume is applied); "Post" reads the peak measured
 * immediately after adjustVolume() — what listeners hear. Per-output overloads
 * take an index in [0, yse_channel_get_num_outputs()); out-of-range indices
 * return 0. dB getters convert the linear value on the fly with a -120 dB
 * floor for silence. All getters return 0 on a NULL or invalid channel. */
YSE_C_API int yse_channel_get_num_outputs(YseChannel* ch);

YSE_C_API float yse_channel_get_peak_linear_pre(YseChannel* ch);
YSE_C_API float yse_channel_get_peak_linear_post(YseChannel* ch);
YSE_C_API float yse_channel_get_peak_db_pre(YseChannel* ch);
YSE_C_API float yse_channel_get_peak_db_post(YseChannel* ch);

YSE_C_API float yse_channel_get_peak_linear_pre_output(YseChannel* ch, int output_idx);
YSE_C_API float yse_channel_get_peak_linear_post_output(YseChannel* ch, int output_idx);
YSE_C_API float yse_channel_get_peak_db_pre_output(YseChannel* ch, int output_idx);
YSE_C_API float yse_channel_get_peak_db_post_output(YseChannel* ch, int output_idx);

#ifdef __cplusplus
}
#endif

#endif
