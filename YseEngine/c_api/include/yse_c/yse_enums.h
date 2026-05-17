/*
  yse_enums.h — C mirrors of YSE::enums.

  Values MUST stay in lockstep with YseEngine/headers/enums.hpp.
  A generator (tools/gen_c_enums.py) will replace this hand-written file
  in a later milestone; for now the M1 surface only needs YseChannelType.
*/

#ifndef YSE_C_ENUMS_H_INCLUDED
#define YSE_C_ENUMS_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

/* Mirrors YSE::CHANNEL_TYPE in headers/enums.hpp. */
typedef enum YseChannelType {
  YSE_CT_AUTO    = 0,
  YSE_CT_MONO    = 1,
  YSE_CT_STEREO  = 2,
  YSE_CT_QUAD    = 3,
  YSE_CT_51      = 4,
  YSE_CT_51SIDE  = 5,
  YSE_CT_61      = 6,
  YSE_CT_71      = 7,
  YSE_CT_CUSTOM  = 8
} YseChannelType;

#ifdef __cplusplus
}
#endif

#endif
