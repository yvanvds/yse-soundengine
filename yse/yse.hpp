/*
  ==============================================================================

    yse.hpp
    Created: 27 Jan 2014 7:15:01pm
    Author:  yvan

  ==============================================================================
*/

#ifndef YSE_HPP_INCLUDED
#define YSE_HPP_INCLUDED

#include "classes.hpp"
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

//templates
#include "templates/interfaceObject.hpp"



// utilities
#include "utils/misc.hpp"
#include "utils/vector.hpp"

// primary objects
#include "system.hpp"
#include "log.hpp"
#include "channel/channelInterface.hpp"
#include "sound/soundInterface.hpp"
#include "reverb/reverbInterface.hpp"
#include "listener.hpp"



#endif  // YSE_HPP_INCLUDED
