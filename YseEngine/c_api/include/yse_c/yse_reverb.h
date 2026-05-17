/*
  yse_reverb.h — positioned reverb zones + global reverb.
  C ABI mirror of YseEngine/reverb/reverbInterface.hpp.

  Two kinds of reverb handles exist:
    - Owned:    yse_reverb_create() → YseReverb*, paired with yse_reverb_destroy().
                Use yse_reverb_set_position() to drop the zone in the scene.
    - Borrowed: yse_system_get_global_reverb() → YseReverb*. Never destroy.
                The global reverb is the fallback wherever no positioned zone
                reaches; rolled-off positioned zones mix against it.
*/

#ifndef YSE_C_REVERB_H_INCLUDED
#define YSE_C_REVERB_H_INCLUDED

#include "yse_common.h"
#include "yse_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Owned via yse_reverb_create — release with yse_reverb_destroy.
   Borrowed via yse_system_get_global_reverb — never destroy that. */
typedef struct YseReverb YseReverb;

/* Owned reverb zone — yse_reverb_create() runs both the C++ constructor
   and reverb::create() so the handle is ready to configure immediately. */
YSE_C_API YseReverb* yse_reverb_create(void);
YSE_C_API void       yse_reverb_destroy(YseReverb* rev);

YSE_C_API int        yse_reverb_is_valid(YseReverb* rev);

/* Position + audible footprint. */
YSE_C_API void       yse_reverb_set_position(YseReverb* rev, const yse_pos_t* p);
YSE_C_API yse_pos_t  yse_reverb_get_position(YseReverb* rev);
YSE_C_API void       yse_reverb_set_size(YseReverb* rev, float v);
YSE_C_API float      yse_reverb_get_size(YseReverb* rev);
YSE_C_API void       yse_reverb_set_roll_off(YseReverb* rev, float v);
YSE_C_API float      yse_reverb_get_roll_off(YseReverb* rev);
YSE_C_API void       yse_reverb_set_active(YseReverb* rev, int on);
YSE_C_API int        yse_reverb_get_active(YseReverb* rev);

/* Tail shape. */
YSE_C_API void       yse_reverb_set_room_size(YseReverb* rev, float v);
YSE_C_API float      yse_reverb_get_room_size(YseReverb* rev);
YSE_C_API void       yse_reverb_set_damping(YseReverb* rev, float v);
YSE_C_API float      yse_reverb_get_damping(YseReverb* rev);
YSE_C_API void       yse_reverb_set_dry_wet_balance(YseReverb* rev, float dry, float wet);
YSE_C_API float      yse_reverb_get_dry(YseReverb* rev);
YSE_C_API float      yse_reverb_get_wet(YseReverb* rev);
YSE_C_API void       yse_reverb_set_modulation(YseReverb* rev, float frequency, float width);
YSE_C_API float      yse_reverb_get_modulation_frequency(YseReverb* rev);
YSE_C_API float      yse_reverb_get_modulation_width(YseReverb* rev);

/* Early reflections (4 slots, index 0..3). */
YSE_C_API void       yse_reverb_set_reflection(YseReverb* rev, int reflection, int time, float gain);
YSE_C_API int        yse_reverb_get_reflection_time(YseReverb* rev, int reflection);
YSE_C_API float      yse_reverb_get_reflection_gain(YseReverb* rev, int reflection);

YSE_C_API void       yse_reverb_set_preset(YseReverb* rev, YseReverbPreset preset);

#ifdef __cplusplus
}
#endif

#endif
