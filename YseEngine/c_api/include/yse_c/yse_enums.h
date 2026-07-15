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
  YSE_CT_AUTO = 0,
  YSE_CT_MONO = 1,
  YSE_CT_STEREO = 2,
  YSE_CT_QUAD = 3,
  YSE_CT_51 = 4,
  YSE_CT_51SIDE = 5,
  YSE_CT_61 = 6,
  YSE_CT_71 = 7,
  YSE_CT_CUSTOM = 8
} YseChannelType;

/* Mirrors YSE::ERROR_LEVEL in headers/enums.hpp. */
typedef enum YseErrorLevel {
  YSE_EL_NONE = 0,
  YSE_EL_ERROR = 1,
  YSE_EL_WARNING = 2,
  YSE_EL_DEBUG = 3
} YseErrorLevel;

/* Mirrors YSE::OUT_TYPE in headers/enums.hpp (patcher outlet data type). */
typedef enum YseOutType {
  YSE_OUT_INVALID = 0,
  YSE_OUT_BANG = 1,
  YSE_OUT_FLOAT = 2,
  YSE_OUT_INT = 3,
  YSE_OUT_BUFFER = 4,
  YSE_OUT_LIST = 5,
  YSE_OUT_ANY = 6
} YseOutType;

/* Mirrors YSE::DSP::LFO_TYPE in dsp/lfo.hpp. */
typedef enum YseLfoType {
  YSE_LFO_NONE = 0,
  YSE_LFO_SAW = 1,
  YSE_LFO_SAW_REVERSED = 2,
  YSE_LFO_TRIANGLE = 3,
  YSE_LFO_SINE = 4,
  YSE_LFO_SQUARE = 5,
  YSE_LFO_RANDOM = 6
} YseLfoType;

/* Mirrors YSE::SYNTH::VA_WAVEFORM in synth/vaVoice.hpp — the oscillator
   waveform selector for the virtual-analog voice (yse_synth_va_set_osc_wave). */
typedef enum YseVaWaveform {
  YSE_VA_SAW = 0, /* Band-limited sawtooth. */
  YSE_VA_PULSE = 1, /* Band-limited pulse with variable width (PWM). */
  YSE_VA_TRIANGLE = 2, /* Band-limited triangle. */
  YSE_VA_SINE = 3, /* Sine. */
  YSE_VA_NOISE = 4, /* White noise. */
  YSE_VA_WAVETABLE = 5 /* Morph across the wavetable bank. */
} YseVaWaveform;

/* Mirrors YSE::DSP::MODULES::sweepFilter::SHAPE. */
typedef enum YseDspSweepShape {
  YSE_SWEEP_TRIANGLE = 0,
  YSE_SWEEP_SAW = 1,
  YSE_SWEEP_SQUARE = 2
} YseDspSweepShape;

/* Mirrors YSE::DSP::MODULES::basicDelay::DELAY_NR. */
typedef enum YseDspDelayTap {
  YSE_DELAY_TAP_FIRST = 0,
  YSE_DELAY_TAP_SECOND = 1,
  YSE_DELAY_TAP_THIRD = 2
} YseDspDelayTap;

/* Mirrors YSE::DSP::MODULES::chorusMode (chorus.hpp) — the chorus/flanger
   topology switch. */
typedef enum YseChorusMode {
  YSE_CHORUS_MODE_CHORUS = 0, /* Longer base delay, wide slow sweep. */
  YSE_CHORUS_MODE_FLANGER = 1 /* Short base delay, feedback comb. */
} YseChorusMode;

/* Mirrors YSE::DSP::MODULES::eqBand (parametricEQ.hpp) — the four fixed
   bands of the parametric EQ. YSE_EQ_BAND_COUNT is the sentinel count. */
typedef enum YseEqBand {
  YSE_EQ_LOW_SHELF = 0,
  YSE_EQ_PEAK_1 = 1,
  YSE_EQ_PEAK_2 = 2,
  YSE_EQ_HIGH_SHELF = 3,
  YSE_EQ_BAND_COUNT = 4
} YseEqBand;

/* Mirrors YSE::DSP::MODULES::compressorDetector (compressor.hpp) — the
   level-detector mode. */
typedef enum YseCompressorDetector {
  YSE_COMPRESSOR_DETECT_PEAK = 0, /* Instantaneous linked peak. */
  YSE_COMPRESSOR_DETECT_RMS = 1 /* Short mean-square window. */
} YseCompressorDetector;

/* Mirrors YSE::PATCHER::pCategory in patcher/pEnums.h. The patcher
   registry exposes this through the metadata API so binding-side
   documentation generators can group objects without inspecting the
   engine source. */
typedef enum YsePCategory {
  YSE_PCAT_UNSET = 0,
  YSE_PCAT_OSC = 1,
  YSE_PCAT_FILTER = 2,
  YSE_PCAT_MATH = 3,
  YSE_PCAT_GENERIC = 4,
  YSE_PCAT_GUI = 5,
  YSE_PCAT_TIME = 6,
  YSE_PCAT_MIDI = 7
} YsePCategory;

/* Bitmask of message types an inlet accepts; mirrors
   YSE::PATCHER::InletType. OR the flags together; check with `&`. */
typedef enum YseInletAccepts {
  YSE_IN_ACCEPTS_BUFFER = 1u << 0,
  YSE_IN_ACCEPTS_FLOAT = 1u << 1,
  YSE_IN_ACCEPTS_INT = 1u << 2,
  YSE_IN_ACCEPTS_BANG = 1u << 3,
  YSE_IN_ACCEPTS_LIST = 1u << 4
} YseInletAccepts;

/* Selects which built-in per-note position handler
   yse_synth_set_position_handler attaches. Mirrors the three shipped handler
   classes in synth/positionHandlers.hpp (staticHandler / randomSpreadHandler /
   orbitHandler). This is a C-API-owned dispatch selector: the engine has no
   single enum of handler kinds (each is a distinct class), so its drift guard
   is structural, not a value mirror — the exhaustive dispatch in yse_synth.cpp
   plus the YSE_POSITION_HANDLER_COUNT sentinel and the is_base_of asserts in
   yse_enums_check.cpp. Keep YSE_POSITION_HANDLER_COUNT last. */
typedef enum YseSynthPositionHandler {
  YSE_POSITION_HANDLER_STATIC = 0, /* staticHandler — one fixed position. */
  YSE_POSITION_HANDLER_RANDOM_SPREAD = 1, /* randomSpreadHandler — seeded scatter. */
  YSE_POSITION_HANDLER_ORBIT = 2, /* orbitHandler — the swarm handler. */
  YSE_POSITION_HANDLER_COUNT = 3 /* sentinel — number of kinds; keep last. */
} YseSynthPositionHandler;

/* Shared handler-parameter indices the built-in handlers read for their
   steerable centre; mirrors YSE::SYNTH::HandlerParamIndex in
   synth/positionHandlers.hpp. Pass one to yse_synth_handler_param to move the
   swarm / spread centre at runtime. Indices 0..2 are the centre X / Y / Z;
   higher indices (up to the engine's block size) are free for custom use. */
typedef enum YseSynthHandlerParam {
  YSE_HANDLER_PARAM_CENTER_X = 0,
  YSE_HANDLER_PARAM_CENTER_Y = 1,
  YSE_HANDLER_PARAM_CENTER_Z = 2
} YseSynthHandlerParam;

/* Mirrors YSE::REVERB_PRESET in headers/enums.hpp. */
typedef enum YseReverbPreset {
  YSE_REVERB_OFF = 0,
  YSE_REVERB_GENERIC = 1,
  YSE_REVERB_PADDED = 2,
  YSE_REVERB_ROOM = 3,
  YSE_REVERB_BATHROOM = 4,
  YSE_REVERB_STONEROOM = 5,
  YSE_REVERB_LARGEROOM = 6,
  YSE_REVERB_HALL = 7,
  YSE_REVERB_CAVE = 8,
  YSE_REVERB_SEWERPIPE = 9,
  YSE_REVERB_UNDERWATER = 10
} YseReverbPreset;

#ifdef __cplusplus
}
#endif

#endif
