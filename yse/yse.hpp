/*
  ==============================================================================

    yse.hpp
    Created: 27 Jan 2014 7:15:01pm
    Author:  yvan

  ==============================================================================
*/

#ifndef YSE_HPP_INCLUDED
#define YSE_HPP_INCLUDED

#include <atomic>

// helpers
#include "headers/types.hpp"
#include "headers/defines.hpp"
#include "headers/enums.hpp"
#include "headers/constants.hpp"

// dsp
#include "dsp/sample.hpp"
#include "dsp/delay.hpp"
#include "dsp/dspObject.hpp"
#include "dsp/filters.hpp"
#include "dsp/math.hpp"
#include "dsp/oscillators.hpp"
#include "dsp/ramp.hpp"

#include "dsp/modules/hilbert.hpp"
#include "dsp/modules/ringModulator.hpp"
#include "dsp/modules/sineWave.hpp"

// utilities
#include "utils/misc.hpp"
#include "utils/vector.hpp"

// primary objects
#include "system.hpp"
#include "log.hpp"
#include "channel/channel.hpp"
#include "channel/channelInterface.hpp"
#include "sound/sound.hpp"
#include "sound/soundInterface.hpp"
#include "synth/synth.hpp"
#include "synth/synthInterface.hpp"
#include "synth/dspVoice.hpp"
#include "midi/midifile.hpp"
#include "reverb/reverb.hpp"
#include "reverb/reverbInterface.hpp"
#include "device/device.hpp"
#include "device/deviceInterface.hpp"
#include "device/deviceSetup.hpp"
#include "listener.hpp"
#include "io.hpp"



#endif  // YSE_HPP_INCLUDED
