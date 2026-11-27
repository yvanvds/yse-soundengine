/*
  yse_enums_check.cpp — compile-time drift guard between the C enum mirror
  (yse_c/yse_enums.h) and the underlying YSE engine enums.

  Folded into yse_objects but exports nothing. Any static_assert failure
  here means yse_enums.h is out of sync with the engine — update one
  side or the other and re-build. See issue #61.
*/

#include "yse_c/yse_enums.h"

#include "../headers/enums.hpp"
#include "../dsp/lfo.hpp"
#include "../dsp/modules/filters/sweep.hpp"
#include "../dsp/modules/delay/basicDelay.hpp"
#include "../dsp/modules/chorus.hpp"
#include "../dsp/modules/parametricEQ.hpp"
#include "../dsp/modules/compressor.hpp"
#include "../dsp/filters/onePole.hpp"
#include "../dsp/filters/stateVariableFilter.hpp"
#include "../dsp/filters/filterCoefficients.hpp"
#include "../dsp/filters/filterDesign.hpp"
#include "../dsp/filters/filterBands.hpp"
#include "../dsp/generators/adsr.hpp"
#include "../dsp/generators/curveSegments.hpp"
#include "../dsp/generators/phasorRate.hpp"
#include "../dsp/generators/phasorSubdiv.hpp"
#include "../dsp/generators/phasorWhat.hpp"
#include "../dsp/generators/sampleHoldMemory.hpp"
#include "../dsp/generators/signalStash.hpp"
#include "../dsp/generators/snowfall.hpp"
#include "../dsp/generators/stepCounter.hpp"
#include "../dsp/generators/stepFunction.hpp"
#include "../dsp/generators/triggeredRamp.hpp"
#include "../dsp/generators/zigzag.hpp"
#include "../patcher/pEnums.h"
#include "../synth/positionHandler.hpp"
#include "../synth/positionHandlers.hpp"
#include "../synth/vaVoice.hpp"

#include <type_traits>

namespace {

#define YSE_ASSERT_ENUM(c_value, cpp_value)                                                        \
  static_assert(static_cast<int>(c_value) == static_cast<int>(cpp_value),                          \
                "C ABI enum drift: " #c_value " != " #cpp_value                                    \
                " — update yse_c/yse_enums.h or the engine enum")

  // Count guard: YSE_ASSERT_ENUM only catches renumbering — a value appended to
  // an engine enum still compiles and silently falls through the C mappings
  // (issue #912). Each mirrored engine enum therefore ends in a *_COUNT_
  // sentinel, pinned here to one past the C mirror's last value, so adding an
  // engine value fails the build until yse_enums.h (and this assert) catch up.
#define YSE_ASSERT_ENUM_COUNT(cpp_count, c_last)                                                   \
  static_assert(static_cast<int>(cpp_count) == static_cast<int>(c_last) + 1,                       \
                "C ABI enum drift: " #cpp_count " != " #c_last                                     \
                " + 1 — a value was added to the engine enum; mirror it in yse_c/yse_enums.h, "    \
                "point this assert at the new last value, and update the C API mappings")

  // YseChannelType ↔ YSE::CHANNEL_TYPE
  YSE_ASSERT_ENUM(YSE_CT_AUTO, YSE::CT_AUTO);
  YSE_ASSERT_ENUM(YSE_CT_MONO, YSE::CT_MONO);
  YSE_ASSERT_ENUM(YSE_CT_STEREO, YSE::CT_STEREO);
  YSE_ASSERT_ENUM(YSE_CT_QUAD, YSE::CT_QUAD);
  YSE_ASSERT_ENUM(YSE_CT_51, YSE::CT_51);
  YSE_ASSERT_ENUM(YSE_CT_51SIDE, YSE::CT_51SIDE);
  YSE_ASSERT_ENUM(YSE_CT_61, YSE::CT_61);
  YSE_ASSERT_ENUM(YSE_CT_71, YSE::CT_71);
  YSE_ASSERT_ENUM(YSE_CT_CUSTOM, YSE::CT_CUSTOM);
  YSE_ASSERT_ENUM_COUNT(YSE::CT_COUNT_, YSE_CT_CUSTOM);

  // YseReconnectState ↔ YSE::RECONNECT_STATE
  YSE_ASSERT_ENUM(YSE_RS_NONE, YSE::RS_NONE);
  YSE_ASSERT_ENUM(YSE_RS_RECONNECTING, YSE::RS_RECONNECTING);
  YSE_ASSERT_ENUM(YSE_RS_RESTORED, YSE::RS_RESTORED);
  YSE_ASSERT_ENUM(YSE_RS_FELL_BACK_TO_DEFAULT, YSE::RS_FELL_BACK_TO_DEFAULT);
  YSE_ASSERT_ENUM(YSE_RS_FAILED, YSE::RS_FAILED);
  YSE_ASSERT_ENUM_COUNT(YSE::RS_COUNT_, YSE_RS_FAILED);

  // YseErrorLevel ↔ YSE::ERROR_LEVEL
  YSE_ASSERT_ENUM(YSE_EL_NONE, YSE::EL_NONE);
  YSE_ASSERT_ENUM(YSE_EL_ERROR, YSE::EL_ERROR);
  YSE_ASSERT_ENUM(YSE_EL_WARNING, YSE::EL_WARNING);
  YSE_ASSERT_ENUM(YSE_EL_DEBUG, YSE::EL_DEBUG);
  YSE_ASSERT_ENUM_COUNT(YSE::EL_COUNT_, YSE_EL_DEBUG);

  // YseOutType ↔ YSE::OUT_TYPE
  YSE_ASSERT_ENUM(YSE_OUT_INVALID, YSE::INVALID);
  YSE_ASSERT_ENUM(YSE_OUT_BANG, YSE::BANG);
  YSE_ASSERT_ENUM(YSE_OUT_FLOAT, YSE::FLOAT);
  YSE_ASSERT_ENUM(YSE_OUT_INT, YSE::INT);
  YSE_ASSERT_ENUM(YSE_OUT_BUFFER, YSE::BUFFER);
  YSE_ASSERT_ENUM(YSE_OUT_LIST, YSE::LIST);
  YSE_ASSERT_ENUM(YSE_OUT_ANY, YSE::ANY);
  YSE_ASSERT_ENUM_COUNT(YSE::OUT_TYPE_COUNT_, YSE_OUT_ANY);

  // YseLfoType ↔ YSE::DSP::LFO_TYPE
  YSE_ASSERT_ENUM(YSE_LFO_NONE, YSE::DSP::LFO_NONE);
  YSE_ASSERT_ENUM(YSE_LFO_SAW, YSE::DSP::LFO_SAW);
  YSE_ASSERT_ENUM(YSE_LFO_SAW_REVERSED, YSE::DSP::LFO_SAW_REVERSED);
  YSE_ASSERT_ENUM(YSE_LFO_TRIANGLE, YSE::DSP::LFO_TRIANGLE);
  YSE_ASSERT_ENUM(YSE_LFO_SINE, YSE::DSP::LFO_SINE);
  YSE_ASSERT_ENUM(YSE_LFO_SQUARE, YSE::DSP::LFO_SQUARE);
  YSE_ASSERT_ENUM(YSE_LFO_RANDOM, YSE::DSP::LFO_RANDOM);
  YSE_ASSERT_ENUM_COUNT(YSE::DSP::LFO_COUNT_, YSE_LFO_RANDOM);

  // YseVaWaveform ↔ YSE::SYNTH::VA_WAVEFORM
  YSE_ASSERT_ENUM(YSE_VA_SAW, YSE::SYNTH::VA_SAW);
  YSE_ASSERT_ENUM(YSE_VA_PULSE, YSE::SYNTH::VA_PULSE);
  YSE_ASSERT_ENUM(YSE_VA_TRIANGLE, YSE::SYNTH::VA_TRIANGLE);
  YSE_ASSERT_ENUM(YSE_VA_SINE, YSE::SYNTH::VA_SINE);
  YSE_ASSERT_ENUM(YSE_VA_NOISE, YSE::SYNTH::VA_NOISE);
  YSE_ASSERT_ENUM(YSE_VA_WAVETABLE, YSE::SYNTH::VA_WAVETABLE);
  YSE_ASSERT_ENUM_COUNT(YSE::SYNTH::VA_WAVEFORM_COUNT_, YSE_VA_WAVETABLE);

  // YseDspSweepShape ↔ YSE::DSP::MODULES::sweepFilter::SHAPE
  YSE_ASSERT_ENUM(YSE_SWEEP_TRIANGLE, YSE::DSP::MODULES::sweepFilter::TRIANGLE);
  YSE_ASSERT_ENUM(YSE_SWEEP_SAW, YSE::DSP::MODULES::sweepFilter::SAW);
  YSE_ASSERT_ENUM(YSE_SWEEP_SQUARE, YSE::DSP::MODULES::sweepFilter::SQUARE);
  YSE_ASSERT_ENUM_COUNT(YSE::DSP::MODULES::sweepFilter::SHAPE_COUNT_, YSE_SWEEP_SQUARE);

  // YseDspDelayTap ↔ YSE::DSP::MODULES::basicDelay::DELAY_NR
  YSE_ASSERT_ENUM(YSE_DELAY_TAP_FIRST, YSE::DSP::MODULES::basicDelay::FIRST);
  YSE_ASSERT_ENUM(YSE_DELAY_TAP_SECOND, YSE::DSP::MODULES::basicDelay::SECOND);
  YSE_ASSERT_ENUM(YSE_DELAY_TAP_THIRD, YSE::DSP::MODULES::basicDelay::THIRD);
  YSE_ASSERT_ENUM_COUNT(YSE::DSP::MODULES::basicDelay::DELAY_NR_COUNT_, YSE_DELAY_TAP_THIRD);

  // YseChorusMode ↔ YSE::DSP::MODULES::chorusMode
  YSE_ASSERT_ENUM(YSE_CHORUS_MODE_CHORUS, YSE::DSP::MODULES::MODE_CHORUS);
  YSE_ASSERT_ENUM(YSE_CHORUS_MODE_FLANGER, YSE::DSP::MODULES::MODE_FLANGER);
  YSE_ASSERT_ENUM_COUNT(YSE::DSP::MODULES::MODE_COUNT_, YSE_CHORUS_MODE_FLANGER);

  // YseEqBand ↔ YSE::DSP::MODULES::eqBand
  YSE_ASSERT_ENUM(YSE_EQ_LOW_SHELF, YSE::DSP::MODULES::EQ_LOW_SHELF);
  YSE_ASSERT_ENUM(YSE_EQ_PEAK_1, YSE::DSP::MODULES::EQ_PEAK_1);
  YSE_ASSERT_ENUM(YSE_EQ_PEAK_2, YSE::DSP::MODULES::EQ_PEAK_2);
  YSE_ASSERT_ENUM(YSE_EQ_HIGH_SHELF, YSE::DSP::MODULES::EQ_HIGH_SHELF);
  YSE_ASSERT_ENUM(YSE_EQ_BAND_COUNT, YSE::DSP::MODULES::EQ_BAND_COUNT);
  // eqBand already ends in a count sentinel, mirrored and value-asserted above.

  // YseCompressorDetector ↔ YSE::DSP::MODULES::compressorDetector
  YSE_ASSERT_ENUM(YSE_COMPRESSOR_DETECT_PEAK, YSE::DSP::MODULES::DETECT_PEAK);
  YSE_ASSERT_ENUM(YSE_COMPRESSOR_DETECT_RMS, YSE::DSP::MODULES::DETECT_RMS);
  YSE_ASSERT_ENUM_COUNT(YSE::DSP::MODULES::DETECT_COUNT_, YSE_COMPRESSOR_DETECT_RMS);

  // YsePCategory ↔ YSE::PATCHER::pCategory
  YSE_ASSERT_ENUM(YSE_PCAT_UNSET, YSE::PATCHER::pCategory::UNSET);
  YSE_ASSERT_ENUM(YSE_PCAT_OSC, YSE::PATCHER::pCategory::OSC);
  YSE_ASSERT_ENUM(YSE_PCAT_FILTER, YSE::PATCHER::pCategory::FILTER);
  YSE_ASSERT_ENUM(YSE_PCAT_MATH, YSE::PATCHER::pCategory::MATH);
  YSE_ASSERT_ENUM(YSE_PCAT_GENERIC, YSE::PATCHER::pCategory::GENERIC);
  YSE_ASSERT_ENUM(YSE_PCAT_GUI, YSE::PATCHER::pCategory::GUI);
  YSE_ASSERT_ENUM(YSE_PCAT_TIME, YSE::PATCHER::pCategory::TIME);
  YSE_ASSERT_ENUM(YSE_PCAT_MIDI, YSE::PATCHER::pCategory::MIDI);
  YSE_ASSERT_ENUM(YSE_PCAT_ROUTING, YSE::PATCHER::pCategory::ROUTING);
  YSE_ASSERT_ENUM(YSE_PCAT_CONTROL, YSE::PATCHER::pCategory::CONTROL);
  YSE_ASSERT_ENUM(YSE_PCAT_LIST, YSE::PATCHER::pCategory::LIST);
  YSE_ASSERT_ENUM(YSE_PCAT_STRING, YSE::PATCHER::pCategory::STRING);
  YSE_ASSERT_ENUM(YSE_PCAT_COLLECTION, YSE::PATCHER::pCategory::COLLECTION);
  YSE_ASSERT_ENUM(YSE_PCAT_DICT, YSE::PATCHER::pCategory::DICT);
  YSE_ASSERT_ENUM(YSE_PCAT_ARRAY, YSE::PATCHER::pCategory::ARRAY);
  YSE_ASSERT_ENUM(YSE_PCAT_RANDOM, YSE::PATCHER::pCategory::RANDOM);
  YSE_ASSERT_ENUM(YSE_PCAT_IO, YSE::PATCHER::pCategory::IO);
  YSE_ASSERT_ENUM(YSE_PCAT_ENCAPSULATION, YSE::PATCHER::pCategory::ENCAPSULATION);
  YSE_ASSERT_ENUM(YSE_PCAT_SEQUENCE, YSE::PATCHER::pCategory::SEQUENCE);
  YSE_ASSERT_ENUM(YSE_PCAT_ANALYSIS, YSE::PATCHER::pCategory::ANALYSIS);
  YSE_ASSERT_ENUM(YSE_PCAT_DYNAMICS, YSE::PATCHER::pCategory::DYNAMICS);
  YSE_ASSERT_ENUM(YSE_PCAT_SAMPLING, YSE::PATCHER::pCategory::SAMPLING);
  YSE_ASSERT_ENUM(YSE_PCAT_SPECTRAL, YSE::PATCHER::pCategory::SPECTRAL);
  YSE_ASSERT_ENUM(YSE_PCAT_DELAY, YSE::PATCHER::pCategory::DELAY);
  YSE_ASSERT_ENUM_COUNT(YSE::PATCHER::pCategory::COUNT_, YSE_PCAT_DELAY);

  // YseInletAccepts ↔ YSE::PATCHER::InletType
  YSE_ASSERT_ENUM(YSE_IN_ACCEPTS_BUFFER, YSE::PATCHER::IT_BUFFER);
  YSE_ASSERT_ENUM(YSE_IN_ACCEPTS_FLOAT, YSE::PATCHER::IT_FLOAT);
  YSE_ASSERT_ENUM(YSE_IN_ACCEPTS_INT, YSE::PATCHER::IT_INT);
  YSE_ASSERT_ENUM(YSE_IN_ACCEPTS_BANG, YSE::PATCHER::IT_BANG);
  YSE_ASSERT_ENUM(YSE_IN_ACCEPTS_LIST, YSE::PATCHER::IT_LIST);
  // InletType is a bitmask, not a sequence: a count sentinel cannot track an
  // added flag, so only the per-bit asserts above guard it.

  // YseSynthHandlerParam ↔ YSE::SYNTH::HandlerParamIndex
  YSE_ASSERT_ENUM(YSE_HANDLER_PARAM_CENTER_X, YSE::SYNTH::HP_CENTER_X);
  YSE_ASSERT_ENUM(YSE_HANDLER_PARAM_CENTER_Y, YSE::SYNTH::HP_CENTER_Y);
  YSE_ASSERT_ENUM(YSE_HANDLER_PARAM_CENTER_Z, YSE::SYNTH::HP_CENTER_Z);
  YSE_ASSERT_ENUM_COUNT(YSE::SYNTH::HP_COUNT_, YSE_HANDLER_PARAM_CENTER_Z);

  // YseSynthPositionHandler has no single engine kind-enum to value-mirror (each
  // built-in is a distinct class in positionHandlers.hpp), so guard it
  // structurally: the shipped handler classes must exist and derive from
  // positionHandler, and the count sentinel must match the number of kinds the
  // dispatch in yse_synth.cpp maps. Adding a built-in must update both sides.
  static_assert(std::is_base_of<YSE::SYNTH::positionHandler, YSE::SYNTH::staticHandler>::value,
                "C ABI drift: YSE::SYNTH::staticHandler must derive from positionHandler");
  static_assert(
      std::is_base_of<YSE::SYNTH::positionHandler, YSE::SYNTH::randomSpreadHandler>::value,
      "C ABI drift: YSE::SYNTH::randomSpreadHandler must derive from positionHandler");
  static_assert(std::is_base_of<YSE::SYNTH::positionHandler, YSE::SYNTH::orbitHandler>::value,
                "C ABI drift: YSE::SYNTH::orbitHandler must derive from positionHandler");
  static_assert(YSE_POSITION_HANDLER_COUNT == 3,
                "C ABI drift: YseSynthPositionHandler no longer matches the built-in handler set "
                "— update yse_enums.h and the dispatch in yse_synth.cpp");

  // YseReverbPreset ↔ YSE::REVERB_PRESET
  YSE_ASSERT_ENUM(YSE_REVERB_OFF, YSE::REVERB_OFF);
  YSE_ASSERT_ENUM(YSE_REVERB_GENERIC, YSE::REVERB_GENERIC);
  YSE_ASSERT_ENUM(YSE_REVERB_PADDED, YSE::REVERB_PADDED);
  YSE_ASSERT_ENUM(YSE_REVERB_ROOM, YSE::REVERB_ROOM);
  YSE_ASSERT_ENUM(YSE_REVERB_BATHROOM, YSE::REVERB_BATHROOM);
  YSE_ASSERT_ENUM(YSE_REVERB_STONEROOM, YSE::REVERB_STONEROOM);
  YSE_ASSERT_ENUM(YSE_REVERB_LARGEROOM, YSE::REVERB_LARGEROOM);
  YSE_ASSERT_ENUM(YSE_REVERB_HALL, YSE::REVERB_HALL);
  YSE_ASSERT_ENUM(YSE_REVERB_CAVE, YSE::REVERB_CAVE);
  YSE_ASSERT_ENUM(YSE_REVERB_SEWERPIPE, YSE::REVERB_SEWERPIPE);
  YSE_ASSERT_ENUM(YSE_REVERB_UNDERWATER, YSE::REVERB_UNDERWATER);
  YSE_ASSERT_ENUM_COUNT(YSE::REVERB_COUNT_, YSE_REVERB_UNDERWATER);

  // YsePathAccess ↔ YSE::PATH_ACCESS (issue #1255)
  YSE_ASSERT_ENUM(YSE_PATH_READ, YSE::PA_READ);
  YSE_ASSERT_ENUM(YSE_PATH_WRITE, YSE::PA_WRITE);
  YSE_ASSERT_ENUM_COUNT(YSE::PATH_ACCESS_COUNT_, YSE_PATH_WRITE);

  // YseDspFrequencyMode <-> onePole / stateVariableFilter FREQUENCY_MODE (#1173)
  YSE_ASSERT_ENUM(YSE_DSP_FREQUENCY_HZ, YSE::DSP::onePole::HZ);
  YSE_ASSERT_ENUM(YSE_DSP_FREQUENCY_LINEAR, YSE::DSP::onePole::LINEAR);
  YSE_ASSERT_ENUM(YSE_DSP_FREQUENCY_RADIANS, YSE::DSP::onePole::RADIANS);
  YSE_ASSERT_ENUM(YSE_DSP_FREQUENCY_HZ, YSE::DSP::stateVariableFilter::HZ);
  YSE_ASSERT_ENUM(YSE_DSP_FREQUENCY_LINEAR, YSE::DSP::stateVariableFilter::LINEAR);
  YSE_ASSERT_ENUM(YSE_DSP_FREQUENCY_RADIANS, YSE::DSP::stateVariableFilter::RADIANS);

  // YseDspFilterType <-> filterCoefficients::TYPE (#1173)
  YSE_ASSERT_ENUM(YSE_DSP_FILTER_OFF, YSE::DSP::filterCoefficients::OFF);
  YSE_ASSERT_ENUM(YSE_DSP_FILTER_LOWPASS, YSE::DSP::filterCoefficients::LOWPASS);
  YSE_ASSERT_ENUM(YSE_DSP_FILTER_HIGHPASS, YSE::DSP::filterCoefficients::HIGHPASS);
  YSE_ASSERT_ENUM(YSE_DSP_FILTER_BANDPASS, YSE::DSP::filterCoefficients::BANDPASS);
  YSE_ASSERT_ENUM(YSE_DSP_FILTER_BANDSTOP, YSE::DSP::filterCoefficients::BANDSTOP);
  YSE_ASSERT_ENUM(YSE_DSP_FILTER_PEAKNOTCH, YSE::DSP::filterCoefficients::PEAKNOTCH);
  YSE_ASSERT_ENUM(YSE_DSP_FILTER_LOWSHELF, YSE::DSP::filterCoefficients::LOWSHELF);
  YSE_ASSERT_ENUM(YSE_DSP_FILTER_HIGHSHELF, YSE::DSP::filterCoefficients::HIGHSHELF);
  YSE_ASSERT_ENUM(YSE_DSP_FILTER_RESONANT, YSE::DSP::filterCoefficients::RESONANT);
  YSE_ASSERT_ENUM(YSE_DSP_FILTER_ALLPASS, YSE::DSP::filterCoefficients::ALLPASS);
  YSE_ASSERT_ENUM(YSE_DSP_FILTER_GAINLPASS, YSE::DSP::filterCoefficients::GAINLPASS);
  YSE_ASSERT_ENUM(YSE_DSP_FILTER_GAINHPASS, YSE::DSP::filterCoefficients::GAINHPASS);
  YSE_ASSERT_ENUM(YSE_DSP_FILTER_GAINBPASS, YSE::DSP::filterCoefficients::GAINBPASS);
  YSE_ASSERT_ENUM(YSE_DSP_FILTER_GAINBSTOP, YSE::DSP::filterCoefficients::GAINBSTOP);
  YSE_ASSERT_ENUM(YSE_DSP_FILTER_GAINRESONANT, YSE::DSP::filterCoefficients::GAINRESONANT);
  YSE_ASSERT_ENUM(YSE_DSP_FILTER_GAINAPASS, YSE::DSP::filterCoefficients::GAINAPASS);
  YSE_ASSERT_ENUM(YSE_DSP_FILTER_TYPE_COUNT, YSE::DSP::filterCoefficients::TYPE_COUNT);

  // YseDspDesignResponse / Topology / Units <-> filterDesign enums (#1296).
  // The engine enums are enum class without a count, so the last value is
  // pinned and a value appended there is caught by review, not by the build.
  YSE_ASSERT_ENUM(YSE_DSP_DESIGN_LOWPASS, YSE::DSP::filterDesign::RESPONSE::LOWPASS);
  YSE_ASSERT_ENUM(YSE_DSP_DESIGN_HIGHPASS, YSE::DSP::filterDesign::RESPONSE::HIGHPASS);
  YSE_ASSERT_ENUM(YSE_DSP_DESIGN_BANDPASS, YSE::DSP::filterDesign::RESPONSE::BANDPASS);
  YSE_ASSERT_ENUM(YSE_DSP_DESIGN_BANDSTOP, YSE::DSP::filterDesign::RESPONSE::BANDSTOP);
  YSE_ASSERT_ENUM(YSE_DSP_DESIGN_BUTTERWORTH, YSE::DSP::filterDesign::TOPOLOGY::BUTTERWORTH);
  YSE_ASSERT_ENUM(YSE_DSP_DESIGN_CHEBYSHEV_1, YSE::DSP::filterDesign::TOPOLOGY::CHEBYSHEV_1);
  YSE_ASSERT_ENUM(YSE_DSP_DESIGN_CHEBYSHEV_2, YSE::DSP::filterDesign::TOPOLOGY::CHEBYSHEV_2);
  YSE_ASSERT_ENUM(YSE_DSP_DESIGN_HERTZ, YSE::DSP::filterDesign::UNITS::HERTZ);
  YSE_ASSERT_ENUM(YSE_DSP_DESIGN_NORMALIZED, YSE::DSP::filterDesign::UNITS::NORMALIZED);
  YSE_ASSERT_ENUM(YSE_DSP_DESIGN_RADIANS, YSE::DSP::filterDesign::UNITS::RADIANS);

  // YseDspBandMode <-> filterBands::MODE (#1296)
  YSE_ASSERT_ENUM(YSE_DSP_BAND_DISPLAY, YSE::DSP::filterBands::DISPLAY);
  YSE_ASSERT_ENUM(YSE_DSP_BAND_LOWPASS, YSE::DSP::filterBands::LOWPASS);
  YSE_ASSERT_ENUM(YSE_DSP_BAND_HIGHPASS, YSE::DSP::filterBands::HIGHPASS);
  YSE_ASSERT_ENUM(YSE_DSP_BAND_BANDPASS, YSE::DSP::filterBands::BANDPASS);
  YSE_ASSERT_ENUM(YSE_DSP_BAND_BANDSTOP, YSE::DSP::filterBands::BANDSTOP);
  YSE_ASSERT_ENUM(YSE_DSP_BAND_PEAKNOTCH, YSE::DSP::filterBands::PEAKNOTCH);
  YSE_ASSERT_ENUM(YSE_DSP_BAND_LOWSHELF, YSE::DSP::filterBands::LOWSHELF);
  YSE_ASSERT_ENUM(YSE_DSP_BAND_HIGHSHELF, YSE::DSP::filterBands::HIGHSHELF);
  YSE_ASSERT_ENUM(YSE_DSP_BAND_RESONANT, YSE::DSP::filterBands::RESONANT);
  YSE_ASSERT_ENUM(YSE_DSP_BAND_ALLPASS, YSE::DSP::filterBands::ALLPASS);
  YSE_ASSERT_ENUM(YSE_DSP_BAND_MODE_COUNT, YSE::DSP::filterBands::MODE_COUNT);
  YSE_ASSERT_ENUM_COUNT(YSE::DSP::filterBands::MODE_COUNT, YSE_DSP_BAND_ALLPASS);

  // The #1298 envelope and sequencer enums. None of the engine enums has a
  // count, so the last value is pinned and a value appended there is caught
  // by review, not by the build.
  // YseDspAdsrStage <-> adsr::STAGE
  YSE_ASSERT_ENUM(YSE_DSP_ADSR_IDLE, YSE::DSP::adsr::IDLE);
  YSE_ASSERT_ENUM(YSE_DSP_ADSR_ATTACK, YSE::DSP::adsr::ATTACK);
  YSE_ASSERT_ENUM(YSE_DSP_ADSR_DECAY, YSE::DSP::adsr::DECAY);
  YSE_ASSERT_ENUM(YSE_DSP_ADSR_SUSTAIN, YSE::DSP::adsr::SUSTAIN);
  YSE_ASSERT_ENUM(YSE_DSP_ADSR_RELEASE, YSE::DSP::adsr::RELEASE);
  YSE_ASSERT_ENUM(YSE_DSP_ADSR_RETRIGGER, YSE::DSP::adsr::RETRIGGER);
  // YseDspCurveShape <-> curveSegments::SHAPE
  YSE_ASSERT_ENUM(YSE_DSP_CURVE_POWER, YSE::DSP::curveSegments::POWER);
  YSE_ASSERT_ENUM(YSE_DSP_CURVE_EXPONENTIAL, YSE::DSP::curveSegments::EXPONENTIAL);
  // YseDspZigzagLoop / YseDspZigzagMode <-> zigzag::LOOP / MODE
  YSE_ASSERT_ENUM(YSE_DSP_ZIGZAG_LOOP_OFF, YSE::DSP::zigzag::LOOP_OFF);
  YSE_ASSERT_ENUM(YSE_DSP_ZIGZAG_LOOP_ON, YSE::DSP::zigzag::LOOP_ON);
  YSE_ASSERT_ENUM(YSE_DSP_ZIGZAG_LOOP_PENDULUM, YSE::DSP::zigzag::LOOP_PENDULUM);
  YSE_ASSERT_ENUM(YSE_DSP_ZIGZAG_START, YSE::DSP::zigzag::START);
  YSE_ASSERT_ENUM(YSE_DSP_ZIGZAG_TRIGGER, YSE::DSP::zigzag::TRIGGER);
  YSE_ASSERT_ENUM(YSE_DSP_ZIGZAG_STEP, YSE::DSP::zigzag::STEP);
  // YseDspRampDurationMode <-> triggeredRamp::durationMode
  YSE_ASSERT_ENUM(YSE_DSP_RAMP_DURATION_AT_START, YSE::DSP::triggeredRamp::durationMode::atStart);
  YSE_ASSERT_ENUM(YSE_DSP_RAMP_DURATION_CONTINUOUS,
                  YSE::DSP::triggeredRamp::durationMode::continuous);

  // The #1299 phasor-toolkit enums. None of the engine enums has a count, so
  // the last value is pinned and a value appended there is caught by review,
  // not by the build.
  // YseDspRateSync <-> phasorRate::syncMode
  YSE_ASSERT_ENUM(YSE_DSP_RATE_SYNC_CYCLE, YSE::DSP::phasorRate::syncMode::cycle);
  YSE_ASSERT_ENUM(YSE_DSP_RATE_SYNC_LOCK, YSE::DSP::phasorRate::syncMode::lock);
  YSE_ASSERT_ENUM(YSE_DSP_RATE_SYNC_OFF, YSE::DSP::phasorRate::syncMode::off);
  // YseDspSubdivSilent / YseDspSubdivUpdate <-> phasorSubdiv::silent / update
  YSE_ASSERT_ENUM(YSE_DSP_SUBDIV_SILENT_HOLD, YSE::DSP::phasorSubdiv::silent::hold);
  YSE_ASSERT_ENUM(YSE_DSP_SUBDIV_SILENT_MINUS_ONE, YSE::DSP::phasorSubdiv::silent::minusOne);
  YSE_ASSERT_ENUM(YSE_DSP_SUBDIV_UPDATE_NOW, YSE::DSP::phasorSubdiv::update::now);
  YSE_ASSERT_ENUM(YSE_DSP_SUBDIV_UPDATE_OUTPUT, YSE::DSP::phasorSubdiv::update::output);
  YSE_ASSERT_ENUM(YSE_DSP_SUBDIV_UPDATE_INPUT, YSE::DSP::phasorSubdiv::update::input);
  // YseDspPhasorWhatMode <-> phasorWhat::TRIGGER_MODE
  YSE_ASSERT_ENUM(YSE_DSP_PHASOR_WHAT_ASCENDING, YSE::DSP::phasorWhat::ASCENDING);
  YSE_ASSERT_ENUM(YSE_DSP_PHASOR_WHAT_DESCENDING, YSE::DSP::phasorWhat::DESCENDING);
  YSE_ASSERT_ENUM(YSE_DSP_PHASOR_WHAT_BOTH, YSE::DSP::phasorWhat::BOTH);
  YSE_ASSERT_ENUM(YSE_DSP_PHASOR_WHAT_EQUALS, YSE::DSP::phasorWhat::EQUALS);
  // YseDspStepCounterDirection / YseDspStepCounterSync <-> stepCounter
  YSE_ASSERT_ENUM(YSE_DSP_STEP_COUNTER_BOTH, YSE::DSP::stepCounter::BOTH);
  YSE_ASSERT_ENUM(YSE_DSP_STEP_COUNTER_UP, YSE::DSP::stepCounter::UP);
  YSE_ASSERT_ENUM(YSE_DSP_STEP_COUNTER_DOWN, YSE::DSP::stepCounter::DOWN);
  YSE_ASSERT_ENUM(YSE_DSP_STEP_COUNTER_IMMEDIATE, YSE::DSP::stepCounter::IMMEDIATE);
  YSE_ASSERT_ENUM(YSE_DSP_STEP_COUNTER_AFTER_STEP, YSE::DSP::stepCounter::AFTER_STEP);
  YSE_ASSERT_ENUM(YSE_DSP_STEP_COUNTER_AFTER_SEQUENCE, YSE::DSP::stepCounter::AFTER_SEQUENCE);
  // YseDspStepFunctionSync <-> stepFunction::SYNC
  YSE_ASSERT_ENUM(YSE_DSP_STEP_FUNCTION_IMMEDIATE, YSE::DSP::stepFunction::IMMEDIATE);
  YSE_ASSERT_ENUM(YSE_DSP_STEP_FUNCTION_INPUT_RESET, YSE::DSP::stepFunction::INPUT_RESET);
  YSE_ASSERT_ENUM(YSE_DSP_STEP_FUNCTION_SEQUENCE_END, YSE::DSP::stepFunction::SEQUENCE_END);
  // YseDspMemoryDirection <-> sampleHoldMemory::DIRECTION and
  // signalStash::DIRECTION: one C enum for two engine enums.
  YSE_ASSERT_ENUM(YSE_DSP_MEMORY_FORWARD, YSE::DSP::sampleHoldMemory::FORWARD);
  YSE_ASSERT_ENUM(YSE_DSP_MEMORY_REVERSE, YSE::DSP::sampleHoldMemory::REVERSE);
  YSE_ASSERT_ENUM(YSE_DSP_MEMORY_RANDOM, YSE::DSP::sampleHoldMemory::RANDOM);
  YSE_ASSERT_ENUM(YSE_DSP_MEMORY_URN, YSE::DSP::sampleHoldMemory::URN);
  YSE_ASSERT_ENUM(YSE_DSP_MEMORY_FORWARD, YSE::DSP::signalStash::FORWARD);
  YSE_ASSERT_ENUM(YSE_DSP_MEMORY_REVERSE, YSE::DSP::signalStash::REVERSE);
  YSE_ASSERT_ENUM(YSE_DSP_MEMORY_RANDOM, YSE::DSP::signalStash::RANDOM);
  YSE_ASSERT_ENUM(YSE_DSP_MEMORY_URN, YSE::DSP::signalStash::URN);
  // YseDspSashMode <-> sampleHoldMemory::MODE
  YSE_ASSERT_ENUM(YSE_DSP_SASH_LEVEL, YSE::DSP::sampleHoldMemory::LEVEL);
  YSE_ASSERT_ENUM(YSE_DSP_SASH_INDEX, YSE::DSP::sampleHoldMemory::INDEX);
  YSE_ASSERT_ENUM(YSE_DSP_SASH_SUBDIVISION, YSE::DSP::sampleHoldMemory::SUBDIVISION);
  // YseDspStashTrigger / Mode / Range / Interpolation <-> signalStash
  YSE_ASSERT_ENUM(YSE_DSP_STASH_TRIGGER_ASCENDING, YSE::DSP::signalStash::ASCENDING);
  YSE_ASSERT_ENUM(YSE_DSP_STASH_TRIGGER_DESCENDING, YSE::DSP::signalStash::DESCENDING);
  YSE_ASSERT_ENUM(YSE_DSP_STASH_TRIGGER_BOTH, YSE::DSP::signalStash::BOTH);
  YSE_ASSERT_ENUM(YSE_DSP_STASH_TRIGGER_EQUALS, YSE::DSP::signalStash::EQUALS);
  YSE_ASSERT_ENUM(YSE_DSP_STASH_TRIGGER_ZERO_TO_NONZERO, YSE::DSP::signalStash::ZERO_TO_NONZERO);
  YSE_ASSERT_ENUM(YSE_DSP_STASH_TRIGGER_NONZERO_TO_ZERO, YSE::DSP::signalStash::NONZERO_TO_ZERO);
  YSE_ASSERT_ENUM(YSE_DSP_STASH_TRIGGER_PHASE, YSE::DSP::signalStash::PHASE);
  YSE_ASSERT_ENUM(YSE_DSP_STASH_NEXT, YSE::DSP::signalStash::NEXT);
  YSE_ASSERT_ENUM(YSE_DSP_STASH_INDEX, YSE::DSP::signalStash::INDEX);
  YSE_ASSERT_ENUM(YSE_DSP_STASH_NORMALIZED, YSE::DSP::signalStash::NORMALIZED);
  YSE_ASSERT_ENUM(YSE_DSP_STASH_RANGE_ZERO, YSE::DSP::signalStash::ZERO);
  YSE_ASSERT_ENUM(YSE_DSP_STASH_RANGE_EXTEND, YSE::DSP::signalStash::EXTEND);
  YSE_ASSERT_ENUM(YSE_DSP_STASH_RANGE_WRAP, YSE::DSP::signalStash::WRAP);
  YSE_ASSERT_ENUM(YSE_DSP_STASH_RANGE_KEEP, YSE::DSP::signalStash::KEEP);
  YSE_ASSERT_ENUM(YSE_DSP_STASH_TRUNCATE, YSE::DSP::signalStash::TRUNCATE);
  YSE_ASSERT_ENUM(YSE_DSP_STASH_LINEAR, YSE::DSP::signalStash::LINEAR);
  YSE_ASSERT_ENUM(YSE_DSP_STASH_ROUND, YSE::DSP::signalStash::ROUND);
  // YseDspSnowfallBoundary / End / Start <-> snowfall
  YSE_ASSERT_ENUM(YSE_DSP_SNOWFALL_BOUNCE, YSE::DSP::snowfall::BOUNCE);
  YSE_ASSERT_ENUM(YSE_DSP_SNOWFALL_END, YSE::DSP::snowfall::END);
  YSE_ASSERT_ENUM(YSE_DSP_SNOWFALL_WRAP, YSE::DSP::snowfall::WRAP);
  YSE_ASSERT_ENUM(YSE_DSP_SNOWFALL_CLIP, YSE::DSP::snowfall::CLIP);
  YSE_ASSERT_ENUM(YSE_DSP_SNOWFALL_END_VALUE, YSE::DSP::snowfall::END_VALUE);
  YSE_ASSERT_ENUM(YSE_DSP_SNOWFALL_CURRENT_VALUE, YSE::DSP::snowfall::CURRENT_VALUE);
  YSE_ASSERT_ENUM(YSE_DSP_SNOWFALL_START_INITIAL, YSE::DSP::snowfall::INITIAL);
  YSE_ASSERT_ENUM(YSE_DSP_SNOWFALL_START_CURRENT, YSE::DSP::snowfall::CURRENT);

#undef YSE_ASSERT_ENUM_COUNT
#undef YSE_ASSERT_ENUM

} // namespace
