/*
  yse_sound.h — playable audio source.
  C ABI mirror of YseEngine/sound/soundInterface.hpp (YSE::sound).
  A sound loads from a file, a YseDspBuffer, a YseDspMultiBuffer, or a
  YsePatcher. The
  dspSourceObject overload (a user-written DSP source) is not wrapped: its
  callback runs on the audio thread and needs a design of its own.

  Convention: every void-returning function in this header is a null-safe
  no-op when called with a NULL handle. Status queries return 0 / false
  on NULL. Loads that can fail return YseStatus and populate
  yse_last_error() on failure.

  The same rule covers a valid handle that has not been loaded yet, or whose
  yse_sound_load_* call failed: setters and transport calls are no-ops and
  queries return 0 / false until a load succeeds (issue #579). Check
  yse_sound_is_valid() to tell the two apart.
*/

#ifndef YSE_C_SOUND_H_INCLUDED
#define YSE_C_SOUND_H_INCLUDED

#include "yse_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Owned — release with yse_sound_destroy. */
typedef struct YseSound YseSound;
/* Forward declarations — see yse_channel.h / yse_dsp.h / yse_dsp_modules.h /
   yse_patcher.h for ownership semantics. */
typedef struct YseChannel YseChannel;
typedef struct YseDspBuffer YseDspBuffer;
typedef struct YseDspMultiBuffer YseDspMultiBuffer;
typedef struct YseDspObject YseDspObject;
typedef struct YsePatcher YsePatcher;

YSE_C_API YseSound* yse_sound_create(void);
YSE_C_API void yse_sound_destroy(YseSound* s);

/* Initialize a sound from a file on disk. Must be called once after
   yse_sound_create() and before any other method.

   The `loop` and `volume` arguments of the load functions below seed the
   sound's state: after a successful load, yse_sound_get_looping() and
   yse_sound_get_volume() report them back until a setter overrides them
   (issue #583). A failed load leaves the sound at its defaults. */
YSE_C_API YseStatus yse_sound_load_file(YseSound* s, const char* filename, YseChannel* ch, int loop,
                                        float volume, int streaming);

/* Initialize a sound from an in-memory DSP buffer. The buffer must outlive
   the sound — the engine keeps a reference to it for as long as the sound
   is live. Same lifetime contract as the C++ overload. */
YSE_C_API YseStatus yse_sound_load_buffer(YseSound* s, YseDspBuffer* buf, YseChannel* ch, int loop,
                                          float volume);

/* Initialize a sound from an in-memory multichannel buffer (the
   sound::create(MULTICHANNELBUFFER&) overload, issue #909): channel i of `mb`
   feeds the sound's channel i, so a two-channel buffer plays as a stereo
   source. Build `mb` with yse_dsp_multi_buffer_create() (see yse_dsp.h). The
   engine reads `mb` for as long as the sound plays, so it must outlive the
   sound — the same lifetime contract as yse_sound_load_buffer(). */
YSE_C_API YseStatus yse_sound_load_multi_buffer(YseSound* s, YseDspMultiBuffer* mb, YseChannel* ch,
                                                int loop, float volume);

/* Initialize a sound from a patcher graph. The patcher must outlive the
   sound. */
YSE_C_API YseStatus yse_sound_load_patcher(YseSound* s, YsePatcher* patch, YseChannel* ch,
                                           float volume);

YSE_C_API int yse_sound_is_valid(YseSound* s);
YSE_C_API int yse_sound_is_ready(YseSound* s);
YSE_C_API int yse_sound_is_streaming(YseSound* s);

/* Assign a bus-addressable name to the sound (mirrors YSE::sound::name, issue
   #905). Once named "foo", the sound subscribes to the global named bus
   addresses sound.foo.volume and sound.foo.speed (float, also accept int) and
   sound.foo.position (list of 3 floats), so a host can drive it with
   yse_bus_publish_*. Values that arrive before a load succeeds are dropped.
   NULL or "" clears the name and removes the subscriptions; renaming
   re-subscribes under the new name. Names are unique per sound: a duplicate
   is rejected and logged engine-side (first registration wins — no error
   return, matching yse_synth_set_name). Only effective while the engine is
   between init and close. */
YSE_C_API void yse_sound_set_name(YseSound* s, const char* name);

/* Transport. */
YSE_C_API void yse_sound_play(YseSound* s);
YSE_C_API void yse_sound_pause(YseSound* s);
YSE_C_API void yse_sound_stop(YseSound* s);
YSE_C_API void yse_sound_toggle(YseSound* s);
YSE_C_API void yse_sound_restart(YseSound* s);
YSE_C_API int yse_sound_is_playing(YseSound* s);
YSE_C_API int yse_sound_is_paused(YseSound* s);
YSE_C_API int yse_sound_is_stopped(YseSound* s);

/* 3D + mixing. */
YSE_C_API void yse_sound_set_pos(YseSound* s, const yse_pos_t* p);
YSE_C_API yse_pos_t yse_sound_get_pos(YseSound* s);
YSE_C_API void yse_sound_set_volume(YseSound* s, float v, unsigned int fade_ms);
YSE_C_API float yse_sound_get_volume(YseSound* s);
YSE_C_API void yse_sound_set_speed(YseSound* s, float v);
YSE_C_API float yse_sound_get_speed(YseSound* s);
YSE_C_API void yse_sound_set_size(YseSound* s, float v);
YSE_C_API float yse_sound_get_size(YseSound* s);
YSE_C_API void yse_sound_set_spread(YseSound* s, float v);
YSE_C_API float yse_sound_get_spread(YseSound* s);
YSE_C_API void yse_sound_set_looping(YseSound* s, int v);
YSE_C_API int yse_sound_get_looping(YseSound* s);
YSE_C_API void yse_sound_set_relative(YseSound* s, int v);
YSE_C_API int yse_sound_get_relative(YseSound* s);
YSE_C_API void yse_sound_set_doppler(YseSound* s, int v);
YSE_C_API int yse_sound_get_doppler(YseSound* s);
YSE_C_API void yse_sound_set_pan2d(YseSound* s, int v);
YSE_C_API int yse_sound_get_pan2d(YseSound* s);
YSE_C_API void yse_sound_set_occlusion(YseSound* s, int v);
YSE_C_API int yse_sound_get_occlusion(YseSound* s);

YSE_C_API void yse_sound_fade_and_stop(YseSound* s, unsigned int time_ms);

/* Playhead. */
YSE_C_API void yse_sound_set_time(YseSound* s, float samples);
YSE_C_API float yse_sound_get_time(YseSound* s);
YSE_C_API unsigned int yse_sound_length(YseSound* s);

YSE_C_API void yse_sound_move_to(YseSound* s, YseChannel* target);

/* DSP effect chain. The dspObject pointer is borrowed; the engine reads it
   on the audio thread. Must outlive the sound. Pass NULL to clear. */
YSE_C_API void yse_sound_set_dsp(YseSound* s, YseDspObject* dsp);
YSE_C_API YseDspObject* yse_sound_get_dsp(YseSound* s);

#ifdef __cplusplus
}
#endif

#endif
