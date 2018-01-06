/*
  ==============================================================================

    yse.hpp
    Created: 27 Jan 2014 7:15:01pm
    Author:  yvan

  ==============================================================================
*/

#ifndef YSE_HPP_INCLUDED
#define YSE_HPP_INCLUDED

#define YSE_SOUND

#include <atomic>

// helpers
#include "headers/types.hpp"
#include "headers/defines.hpp"
#include "headers/enums.hpp"
#include "headers/constants.hpp"

// dsp
#include "dsp/buffer.hpp"
#include "dsp/drawableBuffer.hpp"
#include "dsp/fileBuffer.hpp"
#include "dsp/wavetable.hpp"
#include "dsp/sample_functions.hpp"
#include "dsp/delay.hpp"
#include "dsp/dspObject.hpp"
#include "dsp/filters.hpp"
#include "dsp/math.hpp"
#include "dsp/oscillators.hpp"
#include "dsp/ramp.hpp"
#include "dsp/envelope.hpp"
#include "dsp/ADSRenvelope.hpp"
#include "dsp/lfo.hpp"

#include "dsp/modules/hilbert.hpp"
#include "dsp/modules/ringModulator.hpp"
#include "dsp/modules/sineWave.hpp"
#include "dsp/modules/granulator.hpp"
#include "dsp/modules/phaser.hpp"
#include "dsp/modules/delay/basicDelay.hpp"
#include "dsp/modules/delay/highpassDelay.hpp"
#include "dsp/modules/delay/lowpassDelay.hpp"

#include "dsp/modules/filters/highpass.hpp"
#include "dsp/modules/filters/lowpass.hpp"
#include "dsp/modules/filters/bandpass.hpp"
#include "dsp/modules/fm/difference.hpp"
#include "dsp/modules/filters/sweep.hpp"

// patcher
#include "patcher/pObjectList.hpp"
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"

// utilities
#include "utils/misc.hpp"
#include "utils/vector.hpp"

// primary objects
#include "system.hpp"
#include "log.hpp"

#include "channel/channel.hpp"
#include "channel/channelInterface.hpp"

//#include "sound/sound.hpp"
#include "sound/soundInterface.hpp"

//#include "synth/synth.hpp"
//#include "synth/synthInterface.hpp"
//#include "synth/dspVoice.hpp"

#include "midi/midifile.hpp"

#include "player/player.hpp"
#include "player/playerInterface.hpp"

#include "music/note.hpp"
#include "music/pNote.hpp"

#include "music/scale/scale.hpp"
#include "music/scale/scaleInterface.hpp"

#include "music/motif/motif.hpp"
#include "music/motif/motifInterface.hpp"

#include "reverb/reverb.hpp"
#include "reverb/reverbInterface.hpp"

#include "device/device.hpp"
#include "device/deviceInterface.hpp"
#include "device/deviceSetup.hpp"

#include "listener.hpp"
#include "io.hpp"
#include "BufferIO.hpp"


#endif  // YSE_HPP_INCLUDED
