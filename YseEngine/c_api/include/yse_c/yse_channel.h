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

/* Owned via yse_channel_create — release with yse_channel_destroy.
   Borrowed via the yse_channel_master / _fx / _music / _ambient /
   _voice / _gui pre-built accessors — never destroy those. */
typedef struct YseChannel YseChannel;

/* Pre-built channels — borrowed pointers, never destroy. */
YSE_C_API YseChannel* yse_channel_master(void);
YSE_C_API YseChannel* yse_channel_fx(void);
YSE_C_API YseChannel* yse_channel_music(void);
YSE_C_API YseChannel* yse_channel_ambient(void);
YSE_C_API YseChannel* yse_channel_voice(void);
YSE_C_API YseChannel* yse_channel_gui(void);

/* User-created channels. */
YSE_C_API YseChannel* yse_channel_create(const char* name, YseChannel* parent);
YSE_C_API void yse_channel_destroy(YseChannel* ch);

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
