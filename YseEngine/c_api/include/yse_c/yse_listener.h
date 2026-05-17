/*
  yse_listener.h — 3D listener position and orientation.
  C ABI mirror of YseEngine/listener.hpp (YSE::listener + YSE::Listener() singleton).
*/

#ifndef YSE_C_LISTENER_H_INCLUDED
#define YSE_C_LISTENER_H_INCLUDED

#include "yse_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct YseListener YseListener;

/* Borrowed singleton pointer — never destroy. */
YSE_C_API YseListener* yse_listener_get(void);

YSE_C_API void         yse_listener_set_pos(YseListener* l, const yse_pos_t* p);
YSE_C_API yse_pos_t    yse_listener_get_pos(YseListener* l);
YSE_C_API yse_pos_t    yse_listener_get_vel(YseListener* l);
YSE_C_API yse_pos_t    yse_listener_get_forward(YseListener* l);
YSE_C_API yse_pos_t    yse_listener_get_upward(YseListener* l);

YSE_C_API void         yse_listener_set_orient(
    YseListener* l, const yse_pos_t* forward, const yse_pos_t* up);

#ifdef __cplusplus
}
#endif

#endif
