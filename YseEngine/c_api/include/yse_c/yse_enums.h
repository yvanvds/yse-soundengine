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

/* Mirrors YSE::OUT_TYPE in headers/enums.hpp (patcher outlet data type). */
typedef enum YseOutType {
  YSE_OUT_INVALID = 0,
  YSE_OUT_BANG    = 1,
  YSE_OUT_FLOAT   = 2,
  YSE_OUT_INT     = 3,
  YSE_OUT_BUFFER  = 4,
  YSE_OUT_LIST    = 5,
  YSE_OUT_ANY     = 6
} YseOutType;

/* Mirrors YSE::DSP::LFO_TYPE in dsp/lfo.hpp. */
typedef enum YseLfoType {
  YSE_LFO_NONE          = 0,
  YSE_LFO_SAW           = 1,
  YSE_LFO_SAW_REVERSED  = 2,
  YSE_LFO_TRIANGLE      = 3,
  YSE_LFO_SINE          = 4,
  YSE_LFO_SQUARE        = 5,
  YSE_LFO_RANDOM        = 6
} YseLfoType;

/* Mirrors YSE::DSP::MODULES::sweepFilter::SHAPE. */
typedef enum YseDspSweepShape {
  YSE_SWEEP_TRIANGLE = 0,
  YSE_SWEEP_SAW      = 1,
  YSE_SWEEP_SQUARE   = 2
} YseDspSweepShape;

/* Mirrors YSE::DSP::MODULES::basicDelay::DELAY_NR. */
typedef enum YseDspDelayTap {
  YSE_DELAY_TAP_FIRST  = 0,
  YSE_DELAY_TAP_SECOND = 1,
  YSE_DELAY_TAP_THIRD  = 2
} YseDspDelayTap;

/* Mirrors YSE::REVERB_PRESET in headers/enums.hpp. */
typedef enum YseReverbPreset {
  YSE_REVERB_OFF        = 0,
  YSE_REVERB_GENERIC    = 1,
  YSE_REVERB_PADDED     = 2,
  YSE_REVERB_ROOM       = 3,
  YSE_REVERB_BATHROOM   = 4,
  YSE_REVERB_STONEROOM  = 5,
  YSE_REVERB_LARGEROOM  = 6,
  YSE_REVERB_HALL       = 7,
  YSE_REVERB_CAVE       = 8,
  YSE_REVERB_SEWERPIPE  = 9,
  YSE_REVERB_UNDERWATER = 10
} YseReverbPreset;

#ifdef __cplusplus
}
#endif

#endif
