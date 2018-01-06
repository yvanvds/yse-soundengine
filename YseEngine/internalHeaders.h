/*
  ==============================================================================

    internalHeaders.h
    Created: 26 Mar 2014 7:05:35pm
    Author:  yvan

  ==============================================================================
*/

#include<assert.h>

#include "yse.hpp"

#include "channel/channelImplementation.h"
#include "channel/channelManager.h"
#include "channel/channelMessage.h"

#include "reverb/reverbImplementation.h"
#include "reverb/reverbManager.h"
#include "reverb/reverbMessage.h"

#include "sound/soundImplementation.h"
#include "sound/soundManager.h"
#include "sound/soundMessage.h"

#include "synth/synthImplementation.h"
#include "synth/synthManager.h"
#include "synth/synthMessage.h"

#include "player/playerImplementation.h"
#include "player/playerManager.h"
#include "player/playerMessage.h"

#include "music/scale/scaleImplementation.h"
#include "music/scale/scaleManager.h"
#include "music/scale/scaleMessage.h"

#include "music/motif/motifImplementation.h"
#include "music/motif/motifManager.h"
#include "music/motif/motifMessage.h"

#include "midi/midifileImplementation.h"
#include "midi/midifileManager.h"

#include "device/deviceManager.h"
#if PORTAUDIO_BACKEND
#include "device/portaudioDeviceManager.h"
#endif

#if YSE_ANDROID
#include "device/androidDeviceManager.h"
#endif

#include "implementations/listenerImplementation.h"
#include "implementations/logImplementation.h"

#include "internal/global.h"
#include "internal/reverbDSP.h"
#include "internal/settings.h"

#include "internal/abstractSoundFile.h"

#if JUCE_BACKEND
#include "internal/juceSoundFile.h"
#endif

#if LIBSOUNDFILE_BACKEND
#include "internal/lsfSoundfile.h"
#endif

#include "internal/time.h"
#include "internal/underWaterEffect.h"
#include "internal/virtualFinder.h"
#include "internal/customFileReader.h"

#include "dsp/math_functions.h"

#include "internal/AudioTest.h"


