/*
  yse_sound.h — playable audio source.
  C ABI mirror of YseEngine/sound/soundInterface.hpp (YSE::sound).
  M1 scope: file-based create only. Buffer/DSP-source/patcher overloads land in M3/M5.

  Convention: every void-returning function in this header is a null-safe
  no-op when called with a NULL handle. Status queries return 0 / false
  on NULL. Loads that can fail return YseStatus and populate
  yse_last_error() on failure.
*/

#ifndef YSE_C_SOUND_H_INCLUDED
#define YSE_C_SOUND_H_INCLUDED

#include "yse_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Owned — release with yse_sound_destroy. */
typedef struct YseSound      YseSound;
/* Forward declarations — see yse_channel.h / yse_dsp.h / yse_dsp_modules.h /
   yse_patcher.h for ownership semantics. */
typedef struct YseChannel    YseChannel;
typedef struct YseDspBuffer  YseDspBuffer;
typedef struct YseDspObject  YseDspObject;
typedef struct YsePatcher    YsePatcher;

YSE_C_API YseSound*  yse_sound_create(void);
YSE_C_API void       yse_sound_destroy(YseSound* s);

/* Initialize a sound from a file on disk. Must be called once after
   yse_sound_create() and before any other method. */
YSE_C_API YseStatus  yse_sound_load_file(
    YseSound* s, const char* filename, YseChannel* ch,
    int loop, float volume, int streaming);

/* Initialize a sound from an in-memory DSP buffer. The buffer must outlive
   the sound — the engine keeps a reference to it for as long as the sound
   is live. Same lifetime contract as the C++ overload. */
YSE_C_API YseStatus  yse_sound_load_buffer(
    YseSound* s, YseDspBuffer* buf, YseChannel* ch,
    int loop, float volume);

/* Initialize a sound from a patcher graph. The patcher must outlive the
   sound. */
YSE_C_API YseStatus  yse_sound_load_patcher(
    YseSound* s, YsePatcher* patch, YseChannel* ch, float volume);

YSE_C_API int        yse_sound_is_valid(YseSound* s);
YSE_C_API int        yse_sound_is_ready(YseSound* s);
YSE_C_API int        yse_sound_is_streaming(YseSound* s);

/* Transport. */
YSE_C_API void       yse_sound_play(YseSound* s);
YSE_C_API void       yse_sound_pause(YseSound* s);
YSE_C_API void       yse_sound_stop(YseSound* s);
YSE_C_API void       yse_sound_toggle(YseSound* s);
YSE_C_API void       yse_sound_restart(YseSound* s);
YSE_C_API int        yse_sound_is_playing(YseSound* s);
YSE_C_API int        yse_sound_is_paused(YseSound* s);
YSE_C_API int        yse_sound_is_stopped(YseSound* s);

/* 3D + mixing. */
YSE_C_API void       yse_sound_set_pos(YseSound* s, const yse_pos_t* p);
YSE_C_API yse_pos_t  yse_sound_get_pos(YseSound* s);
YSE_C_API void       yse_sound_set_volume(YseSound* s, float v, unsigned int fade_ms);
YSE_C_API float      yse_sound_get_volume(YseSound* s);
YSE_C_API void       yse_sound_set_speed(YseSound* s, float v);
YSE_C_API float      yse_sound_get_speed(YseSound* s);
YSE_C_API void       yse_sound_set_size(YseSound* s, float v);
YSE_C_API float      yse_sound_get_size(YseSound* s);
YSE_C_API void       yse_sound_set_spread(YseSound* s, float v);
YSE_C_API float      yse_sound_get_spread(YseSound* s);
YSE_C_API void       yse_sound_set_looping(YseSound* s, int v);
YSE_C_API int        yse_sound_get_looping(YseSound* s);
YSE_C_API void       yse_sound_set_relative(YseSound* s, int v);
YSE_C_API int        yse_sound_get_relative(YseSound* s);
YSE_C_API void       yse_sound_set_doppler(YseSound* s, int v);
YSE_C_API int        yse_sound_get_doppler(YseSound* s);
YSE_C_API void       yse_sound_set_pan2d(YseSound* s, int v);
YSE_C_API int        yse_sound_get_pan2d(YseSound* s);
YSE_C_API void       yse_sound_set_occlusion(YseSound* s, int v);
YSE_C_API int        yse_sound_get_occlusion(YseSound* s);

YSE_C_API void       yse_sound_fade_and_stop(YseSound* s, unsigned int time_ms);

/* Playhead. */
YSE_C_API void       yse_sound_set_time(YseSound* s, float samples);
YSE_C_API float      yse_sound_get_time(YseSound* s);
YSE_C_API unsigned int yse_sound_length(YseSound* s);

YSE_C_API void       yse_sound_move_to(YseSound* s, YseChannel* target);

/* DSP effect chain. The dspObject pointer is borrowed; the engine reads it
   on the audio thread. Must outlive the sound. Pass NULL to clear. */
YSE_C_API void          yse_sound_set_dsp(YseSound* s, YseDspObject* dsp);
YSE_C_API YseDspObject* yse_sound_get_dsp(YseSound* s);

#ifdef __cplusplus
}
#endif

#endif
