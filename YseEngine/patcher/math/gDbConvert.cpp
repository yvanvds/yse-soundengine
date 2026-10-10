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

// The engine's decibel scale puts an amplitude of 1.0 at 100 dB — the scale
// rmsToDb() uses for the engine's own volume reporting — rather than at 0 dBFS.
gAToDb::gAToDb() : gUnaryMathBase(&YSE::DSP::rmsToDb) {
  Document("Converts a linear amplitude to decibels on the engine's scale, where an amplitude of "
           "1 is 100 dB and nothing goes below 0 dB. Subtract 100 for decibels relative to full "
           "scale.",
           "Linear amplitude; sends the result. 0 and below send 0.", "0.0 and up",
           "The amplitude in decibels; 100 dB is an amplitude of 1.", "0-485 dB");
}

gDbToA::gDbToA() : gUnaryMathBase(&YSE::DSP::dbToRms) {
  Document("Converts decibels on the engine's scale to a linear amplitude: 100 dB is an "
           "amplitude of 1, and 0 dB or less gives 0. The inverse of .atodb.",
           "Decibels; sends the result. 0 and below send 0; above 485 counts as 485.", "0-485 dB",
           "The linear amplitude; 1 means 100 dB.", "0.0 and up");
}
