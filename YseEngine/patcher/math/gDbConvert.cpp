#include "gDbConvert.h"
#include "../pObjectList.hpp"
#include "../../dsp/math_functions.h"

using namespace YSE::PATCHER;

// The engine's scalar converters are already clamped at both ends —
// rmsToDb() returns 0 for a non-positive amplitude and never goes negative,
// dbToRms() returns 0 for a non-positive level and saturates its input at
// 485 dB — so neither can produce a non-finite result and no extra guard is
// needed here. Both are a log/exp on a register: no allocation, no lock, no
// I/O.

gAToDb::gAToDb() : gUnaryMathBase(&YSE::DSP::rmsToDb) {
  Document("Control-rate linear amplitude to decibels. Emits the input amplitude expressed on the "
           "engine's decibel scale whenever inlet 0 fires. That scale is the one used by the "
           "rmsToDb DSP module and by the engine's own volume reporting: amplitude 1.0 is 100 dB, "
           "amplitude 0 is 0 dB, and the result never goes negative. Subtract 100 to read it as "
           "conventional dBFS.",
           "Linear amplitude to convert — fires the evaluation. Zero and negative values emit 0.",
           "0.0 and up", "The amplitude in decibels, with 100 dB meaning an amplitude of 1.0.",
           "0-485 dB");
}

gDbToA::gDbToA() : gUnaryMathBase(&YSE::DSP::dbToRms) {
  Document("Control-rate decibels to linear amplitude. Emits the input decibel level expressed as "
           "a linear amplitude whenever inlet 0 fires — the exact inverse of .atodb, so 100 dB "
           "gives an amplitude of 1.0 and 0 dB gives 0. Input is clamped at 485 dB to keep the "
           "result finite.",
           "Decibel level to convert — fires the evaluation. Zero and negative values emit 0.",
           "0-485 dB", "The linear amplitude, with an amplitude of 1.0 meaning 100 dB.",
           "0.0 and up");
}
