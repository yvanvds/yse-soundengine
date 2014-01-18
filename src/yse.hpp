#pragma once

// helpers
#include "headers/types.hpp"
#include "headers/defines.hpp"
#include "headers/enums.hpp"
#include "utils/misc.hpp"
#include "utils/vector.hpp"
#include "utils/error.hpp"
#include "backend/filesystem.hpp"

// primary objects
#include "system.hpp"
#include "channel.hpp"
#include "sound.hpp"
#include "playlist.hpp"
#include "reverb.hpp"
#include "listener.hpp"
#include "device.hpp"

/* ---------------------*/
/* ---- dsp section ----*/
/* ---------------------*/

#include "dsp/dsp.hpp"

// dsp primitives
#include "dsp/delay.hpp"
#include "dsp/filters.hpp"
#include "dsp/math.hpp"
#include "dsp/oscillators.hpp"
#include "dsp/ramp.hpp"
#include "dsp/sample.hpp"

// dsp modules
#include "dsp/modules/hilbert.hpp"
#include "dsp/modules/ringModulator.hpp"
#include "dsp/modules/sineWave.hpp"

// instruments
//#include "instruments/sampler.hpp"
//#include "instruments/sineSynth.hpp"

// music
//#include "music/chord.hpp"
//#include "music/note.hpp"
//#include "music/track.hpp"
