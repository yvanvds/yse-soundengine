/*
  yse_channel.h — mixer-tree channels.
  C ABI mirror of YseEngine/channel/channelInterface.hpp (YSE::channel + pre-built
  free-function accessors ChannelMaster, ChannelFX, ChannelMusic, ChannelAmbient,
  ChannelVoice, ChannelGui).
*/

#ifndef YSE_C_CHANNEL_H_INCLUDED
#define YSE_C_CHANNEL_H_INCLUDED

#include "yse_common.h"

#ifdef __cplusplus
extern "C" {
#endif

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
YSE_C_API void        yse_channel_destroy(YseChannel* ch);

YSE_C_API void        yse_channel_set_volume(YseChannel* ch, float value);
YSE_C_API float       yse_channel_get_volume(YseChannel* ch);
YSE_C_API void        yse_channel_move_to(YseChannel* ch, YseChannel* parent);
YSE_C_API void        yse_channel_attach_reverb(YseChannel* ch);
YSE_C_API void        yse_channel_set_virtual(YseChannel* ch, int value);
YSE_C_API int         yse_channel_get_virtual(YseChannel* ch);
YSE_C_API int         yse_channel_is_valid(YseChannel* ch);
YSE_C_API const char* yse_channel_get_name(YseChannel* ch);

#ifdef __cplusplus
}
#endif

#endif
