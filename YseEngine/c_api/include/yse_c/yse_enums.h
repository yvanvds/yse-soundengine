/**
  @file
  yse_enums.h — C mirrors of YSE::enums.

  Values MUST stay in lockstep with YseEngine/headers/enums.hpp.
  The file is hand-mirrored; yse_enums_check.cpp static_asserts every value
  against the engine, so a drifted value fails the build.
*/

#ifndef YSE_C_ENUMS_H_INCLUDED
#define YSE_C_ENUMS_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

/** Mirrors YSE::CHANNEL_TYPE in headers/enums.hpp. */
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

/** Mirrors YSE::RECONNECT_STATE in headers/enums.hpp: what the
   yse_system_auto_reconnect() watchdog last did (issue #1027). */
typedef enum YseReconnectState {
  YSE_RS_NONE = 0, /**< No attempt since init, a device open, or the last auto_reconnect call. */
  YSE_RS_RECONNECTING = 1, /**< The last attempt opened nothing; the watchdog tries again. */
  YSE_RS_RESTORED = 2, /**< The last attempt reopened the session's own device. */
  YSE_RS_FELL_BACK_TO_DEFAULT = 3, /**< The default output device was opened instead. */
  YSE_RS_FAILED = 4 /**< The attempt limit was reached; the watchdog stopped trying. */
} YseReconnectState;

/** Mirrors YSE::ERROR_LEVEL in headers/enums.hpp. */
typedef enum YseErrorLevel {
  YSE_EL_NONE = 0,
  YSE_EL_ERROR = 1,
  YSE_EL_WARNING = 2,
  YSE_EL_DEBUG = 3
} YseErrorLevel;

/** Mirrors YSE::OUT_TYPE in headers/enums.hpp (patcher outlet data type). */
typedef enum YseOutType {
  YSE_OUT_INVALID = 0,
  YSE_OUT_BANG = 1,
  YSE_OUT_FLOAT = 2,
  YSE_OUT_INT = 3,
  YSE_OUT_BUFFER = 4,
  YSE_OUT_LIST = 5,
  YSE_OUT_ANY = 6
} YseOutType;

/** Mirrors YSE::DSP::LFO_TYPE in dsp/lfo.hpp. */
typedef enum YseLfoType {
  YSE_LFO_NONE = 0,
  YSE_LFO_SAW = 1,
  YSE_LFO_SAW_REVERSED = 2,
  YSE_LFO_TRIANGLE = 3,
  YSE_LFO_SINE = 4,
  YSE_LFO_SQUARE = 5,
  YSE_LFO_RANDOM = 6
} YseLfoType;

/** Mirrors YSE::SYNTH::VA_WAVEFORM in synth/vaVoice.hpp — the oscillator
   waveform selector for the virtual-analog voice (yse_synth_va_set_osc_wave). */
typedef enum YseVaWaveform {
  YSE_VA_SAW = 0, /**< Band-limited sawtooth. */
  YSE_VA_PULSE = 1, /**< Band-limited pulse with variable width (PWM). */
  YSE_VA_TRIANGLE = 2, /**< Band-limited triangle. */
  YSE_VA_SINE = 3, /**< Sine. */
  YSE_VA_NOISE = 4, /**< White noise. */
  YSE_VA_WAVETABLE = 5 /**< Morph across the wavetable bank. */
} YseVaWaveform;

/** Mirrors YSE::DSP::MODULES::sweepFilter::SHAPE. */
typedef enum YseDspSweepShape {
  YSE_SWEEP_TRIANGLE = 0,
  YSE_SWEEP_SAW = 1,
  YSE_SWEEP_SQUARE = 2
} YseDspSweepShape;

/** Mirrors YSE::DSP::MODULES::basicDelay::DELAY_NR. */
typedef enum YseDspDelayTap {
  YSE_DELAY_TAP_FIRST = 0,
  YSE_DELAY_TAP_SECOND = 1,
  YSE_DELAY_TAP_THIRD = 2
} YseDspDelayTap;

/** Mirrors YSE::DSP::MODULES::chorusMode (chorus.hpp) — the chorus/flanger
   topology switch. */
typedef enum YseChorusMode {
  YSE_CHORUS_MODE_CHORUS = 0, /**< Longer base delay, wide slow sweep. */
  YSE_CHORUS_MODE_FLANGER = 1 /**< Short base delay, feedback comb. */
} YseChorusMode;

/** Mirrors YSE::DSP::MODULES::eqBand (parametricEQ.hpp) — the four fixed
   bands of the parametric EQ. YSE_EQ_BAND_COUNT is the sentinel count. */
typedef enum YseEqBand {
  YSE_EQ_LOW_SHELF = 0,
  YSE_EQ_PEAK_1 = 1,
  YSE_EQ_PEAK_2 = 2,
  YSE_EQ_HIGH_SHELF = 3,
  YSE_EQ_BAND_COUNT = 4
} YseEqBand;

/** Mirrors YSE::DSP::onePole::FREQUENCY_MODE and
   YSE::DSP::stateVariableFilter::FREQUENCY_MODE (filters/onePole.hpp,
   filters/stateVariableFilter.hpp) — how a filter processor reads its
   frequency (issue #1173). */
typedef enum YseDspFrequencyMode {
  YSE_DSP_FREQUENCY_HZ = 0, /**< A frequency in Hz. */
  YSE_DSP_FREQUENCY_LINEAR = 1, /**< 0..1 spanning 0 .. a quarter of the sample rate. */
  YSE_DSP_FREQUENCY_RADIANS = 2 /**< 0..1 read as the filter's frequency coefficient. */
} YseDspFrequencyMode;

/** Mirrors YSE::DSP::filterCoefficients::TYPE (filters/filterCoefficients.hpp)
   — the biquad designs yse_dsp_filter_coefficients_* compute (issue #1173).
   The GAIN* types scale the response by the linear gain. */
typedef enum YseDspFilterType {
  YSE_DSP_FILTER_OFF = 0,
  YSE_DSP_FILTER_LOWPASS = 1,
  YSE_DSP_FILTER_HIGHPASS = 2,
  YSE_DSP_FILTER_BANDPASS = 3,
  YSE_DSP_FILTER_BANDSTOP = 4,
  YSE_DSP_FILTER_PEAKNOTCH = 5,
  YSE_DSP_FILTER_LOWSHELF = 6,
  YSE_DSP_FILTER_HIGHSHELF = 7,
  YSE_DSP_FILTER_RESONANT = 8,
  YSE_DSP_FILTER_ALLPASS = 9,
  YSE_DSP_FILTER_GAINLPASS = 10,
  YSE_DSP_FILTER_GAINHPASS = 11,
  YSE_DSP_FILTER_GAINBPASS = 12,
  YSE_DSP_FILTER_GAINBSTOP = 13,
  YSE_DSP_FILTER_GAINRESONANT = 14,
  YSE_DSP_FILTER_GAINAPASS = 15,
  YSE_DSP_FILTER_TYPE_COUNT = 16 /**< sentinel — number of types; keep last. */
} YseDspFilterType;

/** Mirrors YSE::DSP::filterDesign::RESPONSE (filters/filterDesign.hpp) — the
   shape of a designed filter's response (issue #1296). */
typedef enum YseDspDesignResponse {
  YSE_DSP_DESIGN_LOWPASS = 0,
  YSE_DSP_DESIGN_HIGHPASS = 1,
  YSE_DSP_DESIGN_BANDPASS = 2,
  YSE_DSP_DESIGN_BANDSTOP = 3
} YseDspDesignResponse;

/** Mirrors YSE::DSP::filterDesign::TOPOLOGY (filters/filterDesign.hpp) — the
   family of the analog prototype a filter is designed from (issue #1296). */
typedef enum YseDspDesignTopology {
  YSE_DSP_DESIGN_BUTTERWORTH = 0, /**< Flat passband, monotonic stopband. */
  YSE_DSP_DESIGN_CHEBYSHEV_1 = 1, /**< Passband ripple, the steepest transition. */
  YSE_DSP_DESIGN_CHEBYSHEV_2 = 2 /**< Flat passband, stopband ripple. */
} YseDspDesignTopology;

/** Mirrors YSE::DSP::filterDesign::UNITS (filters/filterDesign.hpp) — how a
   filter design reads its frequencies (issue #1296). */
typedef enum YseDspDesignUnits {
  YSE_DSP_DESIGN_HERTZ = 0, /**< Hz, against the design's sample rate. */
  YSE_DSP_DESIGN_NORMALIZED = 1, /**< 0 .. 1, where 1 is the Nyquist frequency. */
  YSE_DSP_DESIGN_RADIANS = 2 /**< 0 .. pi, where pi is the Nyquist frequency. */
} YseDspDesignUnits;

/** Mirrors YSE::DSP::filterBands::MODE (filters/filterBands.hpp) — what one
   band of a filter editor is (issue #1296). LOWPASS .. ALLPASS share their
   numbers with YseDspFilterType; DISPLAY is a band given as coefficients. */
typedef enum YseDspBandMode {
  YSE_DSP_BAND_DISPLAY = 0,
  YSE_DSP_BAND_LOWPASS = 1,
  YSE_DSP_BAND_HIGHPASS = 2,
  YSE_DSP_BAND_BANDPASS = 3,
  YSE_DSP_BAND_BANDSTOP = 4,
  YSE_DSP_BAND_PEAKNOTCH = 5,
  YSE_DSP_BAND_LOWSHELF = 6,
  YSE_DSP_BAND_HIGHSHELF = 7,
  YSE_DSP_BAND_RESONANT = 8,
  YSE_DSP_BAND_ALLPASS = 9,
  YSE_DSP_BAND_MODE_COUNT = 10 /**< sentinel — number of modes; keep last. */
} YseDspBandMode;

/** Mirrors YSE::DSP::adsr::STAGE (generators/adsr.hpp) — the segment an ADSR
   envelope is in (issue #1298). */
typedef enum YseDspAdsrStage {
  YSE_DSP_ADSR_IDLE = 0, /**< At rest, output 0. */
  YSE_DSP_ADSR_ATTACK = 1,
  YSE_DSP_ADSR_DECAY = 2,
  YSE_DSP_ADSR_SUSTAIN = 3,
  YSE_DSP_ADSR_RELEASE = 4,
  YSE_DSP_ADSR_RETRIGGER = 5 /**< The ramp down to 0 before a restart. */
} YseDspAdsrStage;

/** Mirrors YSE::DSP::curveSegments::SHAPE (generators/curveSegments.hpp) —
   the curve formula of a curved segment (issue #1298). */
typedef enum YseDspCurveShape {
  YSE_DSP_CURVE_POWER = 0, /**< f^((1 + c) / (1 - c)), the .function curve. */
  YSE_DSP_CURVE_EXPONENTIAL = 1 /**< A true exponential. */
} YseDspCurveShape;

/** Mirrors YSE::DSP::zigzag::LOOP (generators/zigzag.hpp) — what a zigzag
   does at the end of its range (issue #1298). */
typedef enum YseDspZigzagLoop {
  YSE_DSP_ZIGZAG_LOOP_OFF = 0, /**< Stop. */
  YSE_DSP_ZIGZAG_LOOP_ON = 1, /**< Start again from the other end. */
  YSE_DSP_ZIGZAG_LOOP_PENDULUM = 2 /**< Turn round. */
} YseDspZigzagLoop;

/** Mirrors YSE::DSP::zigzag::MODE (generators/zigzag.hpp) — what a bang and
   an input signal do (issue #1298). */
typedef enum YseDspZigzagMode {
  YSE_DSP_ZIGZAG_START = 0, /**< A bang jumps to the start; the input is an index. */
  YSE_DSP_ZIGZAG_TRIGGER = 1, /**< The same bang; a falling input is a bang. */
  YSE_DSP_ZIGZAG_STEP = 2 /**< A bang steps to the next point; a falling input is a bang. */
} YseDspZigzagMode;

/** Mirrors YSE::DSP::triggeredRamp::durationMode (generators/triggeredRamp.hpp)
   — when a triggered ramp reads its duration (issue #1298). */
typedef enum YseDspRampDurationMode {
  YSE_DSP_RAMP_DURATION_AT_START = 0, /**< At the trigger, fixed for the ramp. */
  YSE_DSP_RAMP_DURATION_CONTINUOUS = 1 /**< At every sample, rescaling what is left. */
} YseDspRampDurationMode;

/** Mirrors YSE::DSP::phasorRate::syncMode (generators/phasorRate.hpp) — when
   a time-scaled ramp is brought back in phase (issue #1299). */
typedef enum YseDspRateSync {
  YSE_DSP_RATE_SYNC_CYCLE = 0, /**< At every input wrap. */
  YSE_DSP_RATE_SYNC_LOCK = 1, /**< Whenever the multiplier changes. */
  YSE_DSP_RATE_SYNC_OFF = 2 /**< Never (the default). */
} YseDspRateSync;

/** Mirrors YSE::DSP::phasorSubdiv::silent (generators/phasorSubdiv.hpp) — the
   index output of a sub-ramp that does not play (issue #1299). */
typedef enum YseDspSubdivSilent {
  YSE_DSP_SUBDIV_SILENT_HOLD = 0, /**< The index of the last step that played. */
  YSE_DSP_SUBDIV_SILENT_MINUS_ONE = 1 /**< -1. */
} YseDspSubdivSilent;

/** Mirrors YSE::DSP::phasorSubdiv::update (generators/phasorSubdiv.hpp) —
   when a new subdivision or duration pattern lands (issue #1299). */
typedef enum YseDspSubdivUpdate {
  YSE_DSP_SUBDIV_UPDATE_NOW = 0, /**< On the next sample (the default). */
  YSE_DSP_SUBDIV_UPDATE_OUTPUT = 1, /**< When the next output step starts. */
  YSE_DSP_SUBDIV_UPDATE_INPUT = 2 /**< When the input next wraps. */
} YseDspSubdivUpdate;

/** Mirrors YSE::DSP::phasorWhat::TRIGGER_MODE (generators/phasorWhat.hpp) —
   how a ramp is matched against a phase value (issue #1299). */
typedef enum YseDspPhasorWhatMode {
  YSE_DSP_PHASOR_WHAT_ASCENDING = 0,
  YSE_DSP_PHASOR_WHAT_DESCENDING = 1,
  YSE_DSP_PHASOR_WHAT_BOTH = 2, /**< Either way (the default). */
  YSE_DSP_PHASOR_WHAT_EQUALS = 3
} YseDspPhasorWhatMode;

/** Mirrors YSE::DSP::stepCounter::DIRECTION (generators/stepCounter.hpp) —
   which jumps of the input count (issue #1299). */
typedef enum YseDspStepCounterDirection {
  YSE_DSP_STEP_COUNTER_BOTH = 0, /**< Rises and falls (the default). */
  YSE_DSP_STEP_COUNTER_UP = 1,
  YSE_DSP_STEP_COUNTER_DOWN = 2
} YseDspStepCounterDirection;

/** Mirrors YSE::DSP::stepCounter::SYNC (generators/stepCounter.hpp) — when a
   new count sequence takes effect (issue #1299). */
typedef enum YseDspStepCounterSync {
  YSE_DSP_STEP_COUNTER_IMMEDIATE = 0, /**< At once (the default). */
  YSE_DSP_STEP_COUNTER_AFTER_STEP = 1, /**< At the end of the step playing. */
  YSE_DSP_STEP_COUNTER_AFTER_SEQUENCE = 2 /**< At the end of the last step. */
} YseDspStepCounterSync;

/** Mirrors YSE::DSP::stepFunction::SYNC (generators/stepFunction.hpp) — when
   a new breakpoint pattern takes effect (issue #1299). */
typedef enum YseDspStepFunctionSync {
  YSE_DSP_STEP_FUNCTION_IMMEDIATE = 0, /**< On the next sample (the default). */
  YSE_DSP_STEP_FUNCTION_INPUT_RESET = 1, /**< When the input next wraps. */
  YSE_DSP_STEP_FUNCTION_SEQUENCE_END = 2 /**< When the sequence completes. */
} YseDspStepFunctionSync;

/** Mirrors YSE::DSP::sampleHoldMemory::DIRECTION and
   YSE::DSP::signalStash::DIRECTION (generators/sampleHoldMemory.hpp,
   signalStash.hpp), which share their numbers — how a read position moves
   on an advance (issue #1299). */
typedef enum YseDspMemoryDirection {
  YSE_DSP_MEMORY_FORWARD = 0, /**< One place on (the default). */
  YSE_DSP_MEMORY_REVERSE = 1, /**< One place back. */
  YSE_DSP_MEMORY_RANDOM = 2, /**< Any place, with replacement. */
  YSE_DSP_MEMORY_URN = 3 /**< Each place once per round, in random order. */
} YseDspMemoryDirection;

/** Mirrors YSE::DSP::sampleHoldMemory::MODE (generators/sampleHoldMemory.hpp)
   — how the advance trigger is read (issue #1299). */
typedef enum YseDspSashMode {
  YSE_DSP_SASH_LEVEL = 0, /**< A rise past the level advances (the default). */
  YSE_DSP_SASH_INDEX = 1, /**< The trigger is the read position. */
  YSE_DSP_SASH_SUBDIVISION = 2 /**< The trigger is a phase across the memory. */
} YseDspSashMode;

/** Mirrors YSE::DSP::signalStash::TRIGGER_MODE (generators/signalStash.hpp)
   — when a stash trigger fires (issue #1299). */
typedef enum YseDspStashTrigger {
  YSE_DSP_STASH_TRIGGER_ASCENDING = 0, /**< From at or below the level to above (the default). */
  YSE_DSP_STASH_TRIGGER_DESCENDING = 1,
  YSE_DSP_STASH_TRIGGER_BOTH = 2,
  YSE_DSP_STASH_TRIGGER_EQUALS = 3,
  YSE_DSP_STASH_TRIGGER_ZERO_TO_NONZERO = 4,
  YSE_DSP_STASH_TRIGGER_NONZERO_TO_ZERO = 5,
  YSE_DSP_STASH_TRIGGER_PHASE = 6 /**< Passing the level as a 0..1 phase. */
} YseDspStashTrigger;

/** Mirrors YSE::DSP::signalStash::MODE (generators/signalStash.hpp) — where
   a recording goes, or what the output reads (issue #1299). */
typedef enum YseDspStashMode {
  YSE_DSP_STASH_NEXT = 0, /**< The write or read position (the default). */
  YSE_DSP_STASH_INDEX = 1, /**< The place an index names. */
  YSE_DSP_STASH_NORMALIZED = 2 /**< An index as a 0..1 share of the size. */
} YseDspStashMode;

/** Mirrors YSE::DSP::signalStash::RANGE (generators/signalStash.hpp) — how
   an index outside the memory is read (issue #1299). */
typedef enum YseDspStashRange {
  YSE_DSP_STASH_RANGE_ZERO = 0,
  YSE_DSP_STASH_RANGE_EXTEND = 1,
  YSE_DSP_STASH_RANGE_WRAP = 2, /**< The default. */
  YSE_DSP_STASH_RANGE_KEEP = 3
} YseDspStashRange;

/** Mirrors YSE::DSP::signalStash::INTERPOLATION (generators/signalStash.hpp)
   — how an index between places is read (issue #1299). */
typedef enum YseDspStashInterpolation {
  YSE_DSP_STASH_TRUNCATE = 0, /**< The default. */
  YSE_DSP_STASH_LINEAR = 1,
  YSE_DSP_STASH_ROUND = 2
} YseDspStashInterpolation;

/** Mirrors YSE::DSP::snowfall::BOUNDARY_MODE (generators/snowfall.hpp) —
   what a particle's dimension does at a boundary (issue #1299). */
typedef enum YseDspSnowfallBoundary {
  YSE_DSP_SNOWFALL_BOUNCE = 0, /**< Reflect (the default). */
  YSE_DSP_SNOWFALL_END = 1, /**< End the particle. */
  YSE_DSP_SNOWFALL_WRAP = 2, /**< Continue from the other boundary. */
  YSE_DSP_SNOWFALL_CLIP = 3 /**< Stay at the boundary. */
} YseDspSnowfallBoundary;

/** Mirrors YSE::DSP::snowfall::END_MODE (generators/snowfall.hpp) — the
   output once a particle ends (issue #1299). */
typedef enum YseDspSnowfallEnd {
  YSE_DSP_SNOWFALL_END_VALUE = 0, /**< The end value (the default). */
  YSE_DSP_SNOWFALL_CURRENT_VALUE = 1 /**< Hold the last output. */
} YseDspSnowfallEnd;

/** Mirrors YSE::DSP::snowfall::START_MODE (generators/snowfall.hpp) — where
   a particle starts (issue #1299). */
typedef enum YseDspSnowfallStart {
  YSE_DSP_SNOWFALL_START_INITIAL = 0, /**< Around the initial value (the default). */
  YSE_DSP_SNOWFALL_START_CURRENT = 1 /**< Where the last particle left. */
} YseDspSnowfallStart;

/** Mirrors YSE::DSP::MODULES::compressorDetector (compressor.hpp) — the
   level-detector mode. */
typedef enum YseCompressorDetector {
  YSE_COMPRESSOR_DETECT_PEAK = 0, /**< Instantaneous linked peak. */
  YSE_COMPRESSOR_DETECT_RMS = 1 /**< Short mean-square window. */
} YseCompressorDetector;

/** Mirrors YSE::PATCHER::pCategory in patcher/pEnums.h. The patcher
   registry exposes this through the metadata API so binding-side
   documentation generators can group objects without inspecting the
   engine source. Values are only ever appended, so a number a host stored
   keeps its meaning. GENERIC is the fallback for an object that fits no
   other category. */
typedef enum YsePCategory {
  YSE_PCAT_UNSET = 0,
  YSE_PCAT_OSC = 1, /**< Signal generators (~sine, ~noise, ~line, ...). */
  YSE_PCAT_FILTER = 2, /**< Filters. */
  YSE_PCAT_MATH = 3, /**< Arithmetic, conversion, analysis. */
  YSE_PCAT_GENERIC = 4, /**< Uncategorisable fallback. */
  YSE_PCAT_GUI = 5, /**< User-facing controls and message boxes. */
  YSE_PCAT_TIME = 6, /**< Timing utilities (.metro, .delay, .line, ...). */
  YSE_PCAT_MIDI = 7, /**< MIDI formatting, parsing and device I/O. */
  YSE_PCAT_ROUTING = 8, /**< Steering messages (.gate, .route, .sel, ...). */
  YSE_PCAT_CONTROL = 9, /**< Control flow and ordering (.trigger, .uzi, ...). */
  YSE_PCAT_LIST = 10, /**< Building and taking apart lists (.pack, .zl, ...). */
  YSE_PCAT_STRING = 11, /**< Symbols and text (.sprintf, .regexp, ...). */
  YSE_PCAT_COLLECTION = 12, /**< Stores (.coll, .table, .bag, ...). */
  YSE_PCAT_DICT = 13, /**< The .dict family. */
  YSE_PCAT_ARRAY = 14, /**< The .array family. */
  YSE_PCAT_RANDOM = 15, /**< Random and probabilistic sources. */
  YSE_PCAT_IO = 16, /**< Audio and host I/O (~adc, ~dac, .s, .r, .print, ...). */
  YSE_PCAT_ENCAPSULATION = 17, /**< Subpatchers and their inlets/outlets. */
  YSE_PCAT_SEQUENCE = 18, /**< Timed recorders / players (.seq, .mtr, .qlist). */
  YSE_PCAT_ANALYSIS = 19, /**< Signal analysis and metering (~snapshot, ~peakamp, ~zerox, ...). */
  YSE_PCAT_DYNAMICS = 20, /**< Dynamics processing (~compressor, ~limiter, ~gate, ...). */
  YSE_PCAT_SAMPLING = 21, /**< Recording into and playing from buffers (~play, ~groove, ...). */
  YSE_PCAT_SPECTRAL = 22, /**< FFT and frequency-domain processing (~fft, ~pfft, ...). */
  YSE_PCAT_DELAY = 23 /**< Delay lines and feedback structures (~tapin, ~tapout, ~comb, ...). */
} YsePCategory;

/** Bitmask of message types an inlet accepts; mirrors
   YSE::PATCHER::InletType. OR the flags together; check with `&`. */
typedef enum YseInletAccepts {
  YSE_IN_ACCEPTS_BUFFER = 1u << 0,
  YSE_IN_ACCEPTS_FLOAT = 1u << 1,
  YSE_IN_ACCEPTS_INT = 1u << 2,
  YSE_IN_ACCEPTS_BANG = 1u << 3,
  YSE_IN_ACCEPTS_LIST = 1u << 4
} YseInletAccepts;

/** Selects which built-in per-note position handler
   yse_synth_set_position_handler attaches. Mirrors the three shipped handler
   classes in synth/positionHandlers.hpp (staticHandler / randomSpreadHandler /
   orbitHandler). This is a C-API-owned dispatch selector: the engine has no
   single enum of handler kinds (each is a distinct class), so its drift guard
   is structural, not a value mirror — the exhaustive dispatch in yse_synth.cpp
   plus the YSE_POSITION_HANDLER_COUNT sentinel and the is_base_of asserts in
   yse_enums_check.cpp. Keep YSE_POSITION_HANDLER_COUNT last. */
typedef enum YseSynthPositionHandler {
  YSE_POSITION_HANDLER_STATIC = 0, /**< staticHandler — one fixed position. */
  YSE_POSITION_HANDLER_RANDOM_SPREAD = 1, /**< randomSpreadHandler — seeded scatter. */
  YSE_POSITION_HANDLER_ORBIT = 2, /**< orbitHandler — the swarm handler. */
  YSE_POSITION_HANDLER_COUNT = 3 /**< sentinel — number of kinds; keep last. */
} YseSynthPositionHandler;

/** Shared handler-parameter indices the built-in handlers read for their
   steerable centre; mirrors YSE::SYNTH::HandlerParamIndex in
   synth/positionHandlers.hpp. Pass one to yse_synth_handler_param to move the
   swarm / spread centre at runtime. Indices 0..2 are the centre X / Y / Z;
   higher indices (up to the engine's block size) are free for custom use. */
typedef enum YseSynthHandlerParam {
  YSE_HANDLER_PARAM_CENTER_X = 0,
  YSE_HANDLER_PARAM_CENTER_Y = 1,
  YSE_HANDLER_PARAM_CENTER_Z = 2
} YseSynthHandlerParam;

/** Mirrors YSE::REVERB_PRESET in headers/enums.hpp. */
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

/** Mirrors YSE::PATH_ACCESS in headers/enums.hpp: what a patch asks to do
   with a file a YsePatcherPathResolverCallback resolves (issue #1255). */
typedef enum YsePathAccess { YSE_PATH_READ = 0, YSE_PATH_WRITE = 1 } YsePathAccess;

#ifdef __cplusplus
}
#endif

#endif
