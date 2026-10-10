/**
  @file
  yse_reverb.h — positioned reverb zones + global reverb.
  C ABI mirror of YseEngine/reverb/reverbInterface.hpp.

  Two kinds of reverb handles exist:
    - Owned:    yse_reverb_create() → YseReverb*, paired with yse_reverb_destroy().
                Use yse_reverb_set_position() to drop the zone in the scene.
    - Borrowed: yse_system_get_global_reverb() → YseReverb*. Never destroy.
                The global reverb is the fallback wherever no positioned zone
                reaches; rolled-off positioned zones mix against it.

  Convention: every void-returning function in this header is a null-safe
  no-op when called with a NULL handle. Status queries return 0 / false on
  NULL.
*/

#ifndef YSE_C_REVERB_H_INCLUDED
#define YSE_C_REVERB_H_INCLUDED

#include "yse_common.h"
#include "yse_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef YSE_C_HANDLE_YseReverb
#define YSE_C_HANDLE_YseReverb
/** Owned via yse_reverb_create — release with yse_reverb_destroy.
   Borrowed via yse_system_get_global_reverb — never destroy that. */
typedef struct YseReverb YseReverb;
#endif

/** Owned reverb zone — yse_reverb_create() runs both the C++ constructor
   and reverb::create() so the handle is ready to configure immediately. */
YSE_C_API YseReverb* yse_reverb_create(void);
YSE_C_API void yse_reverb_destroy(YseReverb* rev);

YSE_C_API int yse_reverb_is_valid(YseReverb* rev);

/** Position + audible footprint. */
YSE_C_API void yse_reverb_set_position(YseReverb* rev, const yse_pos_t* p);
YSE_C_API yse_pos_t yse_reverb_get_position(YseReverb* rev);
YSE_C_API void yse_reverb_set_size(YseReverb* rev, float v);
YSE_C_API float yse_reverb_get_size(YseReverb* rev);
YSE_C_API void yse_reverb_set_roll_off(YseReverb* rev, float v);
YSE_C_API float yse_reverb_get_roll_off(YseReverb* rev);
YSE_C_API void yse_reverb_set_active(YseReverb* rev, int on);
YSE_C_API int yse_reverb_get_active(YseReverb* rev);

/** Tail shape. */
YSE_C_API void yse_reverb_set_room_size(YseReverb* rev, float v);
YSE_C_API float yse_reverb_get_room_size(YseReverb* rev);
YSE_C_API void yse_reverb_set_damping(YseReverb* rev, float v);
YSE_C_API float yse_reverb_get_damping(YseReverb* rev);
YSE_C_API void yse_reverb_set_dry_wet_balance(YseReverb* rev, float dry, float wet);
YSE_C_API float yse_reverb_get_dry(YseReverb* rev);
YSE_C_API float yse_reverb_get_wet(YseReverb* rev);
YSE_C_API void yse_reverb_set_modulation(YseReverb* rev, float frequency, float width);
YSE_C_API float yse_reverb_get_modulation_frequency(YseReverb* rev);
YSE_C_API float yse_reverb_get_modulation_width(YseReverb* rev);

/** Early reflections (4 slots, index 0..3). */
YSE_C_API void yse_reverb_set_reflection(YseReverb* rev, int reflection, int time, float gain);
YSE_C_API int yse_reverb_get_reflection_time(YseReverb* rev, int reflection);
YSE_C_API float yse_reverb_get_reflection_gain(YseReverb* rev, int reflection);

YSE_C_API void yse_reverb_set_preset(YseReverb* rev, YseReverbPreset preset);

/* ─── preset table + interpolation (issue #909) ───────────────────────────
   Mirrors YSE::REVERB::getPresetValues / morph (reverb/reverbPresets.hpp). No
   handle and no engine session needed: these read the shared preset table and
   blend parameter sets, e.g. to author custom endpoints for the morphing
   reverb (yse_dsp_modules.h) or to drive a zone with
   yse_reverb_set_room_size() et al. */

/** Plain-old-data mirror of YSE::REVERB::presetValues — one complete reverb
   parameter set: the payload of a named preset and the custom endpoint type
   of the morphing reverb. Fields are copied one by one across the ABI; the
   layout is not assumed to match the engine struct. */
typedef struct YseReverbPresetValues {
  float roomsize; /**< simulated room size, [0, 1] */
  float damp; /**< high-frequency damping, [0, 1] */
  float dry; /**< unprocessed level, [0, 1] */
  float wet; /**< reverberated level, [0, 1] */
  float mod_frequency; /**< tail modulation rate, Hz (0 = off) */
  float mod_width; /**< tail modulation depth (0 = off) */
  float early_time[4]; /**< early reflection delays, samples, [0, 2999] */
  float early_gain[4]; /**< early reflection gains, [0, 1] */
} YseReverbPresetValues;

/** Write the parameter set of a named preset into *out — the exact values
   yse_reverb_set_preset() applies. A preset outside the enum yields the
   YSE_REVERB_OFF values. NULL out is a no-op. */
YSE_C_API void yse_reverb_preset_get_values(YseReverbPreset preset, YseReverbPresetValues* out);

/** Linear blend of two parameter sets into *out: every field is
   a + (b - a) * t with t clamped to [0, 1], so t = 0 gives a and t = 1 gives
   b. `out` may alias `a` or `b`. NULL out is a no-op; a NULL a or b
   zero-fills *out. */
YSE_C_API void yse_reverb_preset_morph(const YseReverbPresetValues* a,
                                       const YseReverbPresetValues* b, float t,
                                       YseReverbPresetValues* out);

#ifdef __cplusplus
}
#endif

#endif
