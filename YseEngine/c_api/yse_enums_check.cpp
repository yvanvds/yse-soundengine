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
#include "../patcher/pEnums.h"
#include "../synth/positionHandler.hpp"
#include "../synth/positionHandlers.hpp"

#include <type_traits>

namespace {

#define YSE_ASSERT_ENUM(c_value, cpp_value)                                                        \
  static_assert(static_cast<int>(c_value) == static_cast<int>(cpp_value),                          \
                "C ABI enum drift: " #c_value " != " #cpp_value                                    \
                " — update yse_c/yse_enums.h or the engine enum")

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

  // YseErrorLevel ↔ YSE::ERROR_LEVEL
  YSE_ASSERT_ENUM(YSE_EL_NONE, YSE::EL_NONE);
  YSE_ASSERT_ENUM(YSE_EL_ERROR, YSE::EL_ERROR);
  YSE_ASSERT_ENUM(YSE_EL_WARNING, YSE::EL_WARNING);
  YSE_ASSERT_ENUM(YSE_EL_DEBUG, YSE::EL_DEBUG);

  // YseOutType ↔ YSE::OUT_TYPE
  YSE_ASSERT_ENUM(YSE_OUT_INVALID, YSE::INVALID);
  YSE_ASSERT_ENUM(YSE_OUT_BANG, YSE::BANG);
  YSE_ASSERT_ENUM(YSE_OUT_FLOAT, YSE::FLOAT);
  YSE_ASSERT_ENUM(YSE_OUT_INT, YSE::INT);
  YSE_ASSERT_ENUM(YSE_OUT_BUFFER, YSE::BUFFER);
  YSE_ASSERT_ENUM(YSE_OUT_LIST, YSE::LIST);
  YSE_ASSERT_ENUM(YSE_OUT_ANY, YSE::ANY);

  // YseLfoType ↔ YSE::DSP::LFO_TYPE
  YSE_ASSERT_ENUM(YSE_LFO_NONE, YSE::DSP::LFO_NONE);
  YSE_ASSERT_ENUM(YSE_LFO_SAW, YSE::DSP::LFO_SAW);
  YSE_ASSERT_ENUM(YSE_LFO_SAW_REVERSED, YSE::DSP::LFO_SAW_REVERSED);
  YSE_ASSERT_ENUM(YSE_LFO_TRIANGLE, YSE::DSP::LFO_TRIANGLE);
  YSE_ASSERT_ENUM(YSE_LFO_SINE, YSE::DSP::LFO_SINE);
  YSE_ASSERT_ENUM(YSE_LFO_SQUARE, YSE::DSP::LFO_SQUARE);
  YSE_ASSERT_ENUM(YSE_LFO_RANDOM, YSE::DSP::LFO_RANDOM);

  // YseDspSweepShape ↔ YSE::DSP::MODULES::sweepFilter::SHAPE
  YSE_ASSERT_ENUM(YSE_SWEEP_TRIANGLE, YSE::DSP::MODULES::sweepFilter::TRIANGLE);
  YSE_ASSERT_ENUM(YSE_SWEEP_SAW, YSE::DSP::MODULES::sweepFilter::SAW);
  YSE_ASSERT_ENUM(YSE_SWEEP_SQUARE, YSE::DSP::MODULES::sweepFilter::SQUARE);

  // YseDspDelayTap ↔ YSE::DSP::MODULES::basicDelay::DELAY_NR
  YSE_ASSERT_ENUM(YSE_DELAY_TAP_FIRST, YSE::DSP::MODULES::basicDelay::FIRST);
  YSE_ASSERT_ENUM(YSE_DELAY_TAP_SECOND, YSE::DSP::MODULES::basicDelay::SECOND);
  YSE_ASSERT_ENUM(YSE_DELAY_TAP_THIRD, YSE::DSP::MODULES::basicDelay::THIRD);

  // YseChorusMode ↔ YSE::DSP::MODULES::chorusMode
  YSE_ASSERT_ENUM(YSE_CHORUS_MODE_CHORUS, YSE::DSP::MODULES::MODE_CHORUS);
  YSE_ASSERT_ENUM(YSE_CHORUS_MODE_FLANGER, YSE::DSP::MODULES::MODE_FLANGER);

  // YseEqBand ↔ YSE::DSP::MODULES::eqBand
  YSE_ASSERT_ENUM(YSE_EQ_LOW_SHELF, YSE::DSP::MODULES::EQ_LOW_SHELF);
  YSE_ASSERT_ENUM(YSE_EQ_PEAK_1, YSE::DSP::MODULES::EQ_PEAK_1);
  YSE_ASSERT_ENUM(YSE_EQ_PEAK_2, YSE::DSP::MODULES::EQ_PEAK_2);
  YSE_ASSERT_ENUM(YSE_EQ_HIGH_SHELF, YSE::DSP::MODULES::EQ_HIGH_SHELF);
  YSE_ASSERT_ENUM(YSE_EQ_BAND_COUNT, YSE::DSP::MODULES::EQ_BAND_COUNT);

  // YseCompressorDetector ↔ YSE::DSP::MODULES::compressorDetector
  YSE_ASSERT_ENUM(YSE_COMPRESSOR_DETECT_PEAK, YSE::DSP::MODULES::DETECT_PEAK);
  YSE_ASSERT_ENUM(YSE_COMPRESSOR_DETECT_RMS, YSE::DSP::MODULES::DETECT_RMS);

  // YsePCategory ↔ YSE::PATCHER::pCategory
  YSE_ASSERT_ENUM(YSE_PCAT_UNSET, YSE::PATCHER::pCategory::UNSET);
  YSE_ASSERT_ENUM(YSE_PCAT_OSC, YSE::PATCHER::pCategory::OSC);
  YSE_ASSERT_ENUM(YSE_PCAT_FILTER, YSE::PATCHER::pCategory::FILTER);
  YSE_ASSERT_ENUM(YSE_PCAT_MATH, YSE::PATCHER::pCategory::MATH);
  YSE_ASSERT_ENUM(YSE_PCAT_GENERIC, YSE::PATCHER::pCategory::GENERIC);
  YSE_ASSERT_ENUM(YSE_PCAT_GUI, YSE::PATCHER::pCategory::GUI);
  YSE_ASSERT_ENUM(YSE_PCAT_TIME, YSE::PATCHER::pCategory::TIME);
  YSE_ASSERT_ENUM(YSE_PCAT_MIDI, YSE::PATCHER::pCategory::MIDI);

  // YseInletAccepts ↔ YSE::PATCHER::InletType
  YSE_ASSERT_ENUM(YSE_IN_ACCEPTS_BUFFER, YSE::PATCHER::IT_BUFFER);
  YSE_ASSERT_ENUM(YSE_IN_ACCEPTS_FLOAT, YSE::PATCHER::IT_FLOAT);
  YSE_ASSERT_ENUM(YSE_IN_ACCEPTS_INT, YSE::PATCHER::IT_INT);
  YSE_ASSERT_ENUM(YSE_IN_ACCEPTS_BANG, YSE::PATCHER::IT_BANG);
  YSE_ASSERT_ENUM(YSE_IN_ACCEPTS_LIST, YSE::PATCHER::IT_LIST);

  // YseSynthHandlerParam ↔ YSE::SYNTH::HandlerParamIndex
  YSE_ASSERT_ENUM(YSE_HANDLER_PARAM_CENTER_X, YSE::SYNTH::HP_CENTER_X);
  YSE_ASSERT_ENUM(YSE_HANDLER_PARAM_CENTER_Y, YSE::SYNTH::HP_CENTER_Y);
  YSE_ASSERT_ENUM(YSE_HANDLER_PARAM_CENTER_Z, YSE::SYNTH::HP_CENTER_Z);

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

#undef YSE_ASSERT_ENUM

} // namespace
