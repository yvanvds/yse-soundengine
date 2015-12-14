# Automatically generated makefile, created by the Introjucer
# Don't edit this file! Your changes will be overwritten when you re-save the Introjucer project!

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
    LOCAL_ARM_MODE := arm
endif

LOCAL_MODULE := libyse
LOCAL_SRC_FILES := \
  ../../../../yse/player/playerImplementation.cpp\
  ../../../../yse/player/playerInterface.cpp\
  ../../../../yse/player/playerManager.cpp\
  ../../../../yse/midi/midifile.cpp\
  ../../../../yse/midi/midifileImplementation.cpp\
  ../../../../yse/midi/midifileManager.cpp\
  ../../../../yse/synth/dspSound.cpp\
  ../../../../yse/synth/dspVoice.cpp\
  ../../../../yse/synth/dspVoiceInternal.cpp\
  ../../../../yse/synth/samplerSound.cpp\
  ../../../../yse/synth/synthImplementation.cpp\
  ../../../../yse/synth/synthInterface.cpp\
  ../../../../yse/synth/synthManager.cpp\
  ../../../../yse/channel/channelImplementation.cpp\
  ../../../../yse/channel/channelInterface.cpp\
  ../../../../yse/channel/channelManager.cpp\
  ../../../../yse/device/deviceInterface.cpp\
  ../../../../yse/device/deviceManager.cpp\
  ../../../../yse/device/deviceSetup.cpp\
  ../../../../yse/dsp/fourier/fft.cpp\
  ../../../../yse/dsp/fourier/mayer.cpp\
  ../../../../yse/dsp/modules/delay/basicDelay.cpp\
  ../../../../yse/dsp/modules/delay/highpassDelay.cpp\
  ../../../../yse/dsp/modules/delay/lowpassDelay.cpp\
  ../../../../yse/dsp/modules/filters/bandpass.cpp\
  ../../../../yse/dsp/modules/filters/highpass.cpp\
  ../../../../yse/dsp/modules/filters/lowpass.cpp\
  ../../../../yse/dsp/modules/filters/sweep.cpp\
  ../../../../yse/dsp/modules/fm/difference.cpp\
  ../../../../yse/dsp/modules/hilbert.cpp\
  ../../../../yse/dsp/modules/phaser.cpp\
  ../../../../yse/dsp/modules/ringModulator.cpp\
  ../../../../yse/dsp/modules/sineWave.cpp\
  ../../../../yse/dsp/ADSRenvelope.cpp\
  ../../../../yse/dsp/buffer.cpp\
  ../../../../yse/dsp/delay.cpp\
  ../../../../yse/dsp/drawableBuffer.cpp\
  ../../../../yse/dsp/dspObject.cpp\
  ../../../../yse/dsp/envelope.cpp\
  ../../../../yse/dsp/fileBuffer.cpp\
  ../../../../yse/dsp/filters.cpp\
  ../../../../yse/dsp/modules/granulator.cpp\
  ../../../../yse/dsp/interpolate4.cpp\
  ../../../../yse/dsp/lfo.cpp\
  ../../../../yse/dsp/math.cpp\
  ../../../../yse/dsp/math_functions.cpp\
  ../../../../yse/dsp/oscillators.cpp\
  ../../../../yse/dsp/ramp.cpp\
  ../../../../yse/dsp/rawFilters.cpp\
  ../../../../yse/dsp/sample_functions.cpp\
  ../../../../yse/dsp/wavetable.cpp\
  ../../../../yse/implementations/listenerImplementation.cpp\
  ../../../../yse/implementations/logImplementation.cpp\
  ../../../../yse/internal/customFileReader.cpp\
  ../../../../yse/internal/global.cpp\
  ../../../../yse/internal/reverbDSP.cpp\
  ../../../../yse/internal/settings.cpp\
  ../../../../yse/internal/soundFile.cpp\
  ../../../../yse/internal/thread.cpp\
  ../../../../yse/internal/threadPool.cpp\
  ../../../../yse/internal/time.cpp\
  ../../../../yse/internal/underWaterEffect.cpp\
  ../../../../yse/internal/virtualFinder.cpp\
  ../../../../yse/music/motif/motifImplementation.cpp\
  ../../../../yse/music/motif/motifInterface.cpp\
  ../../../../yse/music/motif/motifManager.cpp\
  ../../../../yse/music/scale/scaleImplementation.cpp\
  ../../../../yse/music/scale/scaleInterface.cpp\
  ../../../../yse/music/scale/scaleManager.cpp\
  ../../../../yse/music/pNote.cpp\
  ../../../../yse/music/chord.cpp\
  ../../../../yse/music/note.cpp\
  ../../../../yse/reverb/reverbImplementation.cpp\
  ../../../../yse/reverb/reverbInterface.cpp\
  ../../../../yse/reverb/reverbManager.cpp\
  ../../../../yse/sound/soundImplementation.cpp\
  ../../../../yse/sound/soundInterface.cpp\
  ../../../../yse/sound/soundManager.cpp\
  ../../../../yse/utils/interpolators.cpp\
  ../../../../yse/utils/vector.cpp\
  ../../../../yse/io.cpp\
  ../../../../yse/listener.cpp\
  ../../../../yse/log.cpp\
  ../../../../yse/system.cpp\
  ../../../JuceLibraryCode/modules/juce_audio_basics/juce_audio_basics.cpp\
  ../../../JuceLibraryCode/modules/juce_audio_devices/juce_audio_devices.cpp\
  ../../../JuceLibraryCode/modules/juce_audio_formats/juce_audio_formats.cpp\
  ../../../JuceLibraryCode/modules/juce_core/juce_core.cpp\
  ../../../JuceLibraryCode/modules/juce_events/juce_events.cpp\

ifeq ($(NDK_DEBUG),1)
  LOCAL_CPPFLAGS += -fsigned-char -fexceptions -frtti -g -I "../../JuceLibraryCode" -I "../../JuceLibraryCode/modules" -O0 -std=c++11 -std=gnu++11 -D "JUCE_ANDROID=1" -D "JUCE_ANDROID_API_VERSION=9" -D "JUCE_ANDROID_ACTIVITY_CLASSNAME=com_mute_yse_static" -D JUCE_ANDROID_ACTIVITY_CLASSPATH=\"com/mute/yse_static\" -D "DEBUG=1" -D "_DEBUG=1" -D "JUCE_CHECK_MEMORY_LEAKS=0" -D "JUCER_ANDROID_7F0E4A25=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000"
  LOCAL_LDLIBS := -llog -lGLESv2 -landroid -lEGL
  LOCAL_CFLAGS += -fsigned-char -fexceptions -frtti -g -I "../../JuceLibraryCode" -I "../../JuceLibraryCode/modules" -O0 -std=c++11 -std=gnu++11 -D "JUCE_ANDROID=1" -D "JUCE_ANDROID_API_VERSION=9" -D "JUCE_ANDROID_ACTIVITY_CLASSNAME=com_mute_yse_static" -D JUCE_ANDROID_ACTIVITY_CLASSPATH=\"com/mute/yse_static\" -D "DEBUG=1" -D "_DEBUG=1" -D "JUCE_CHECK_MEMORY_LEAKS=0" -D "JUCER_ANDROID_7F0E4A25=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000"
  LOCAL_LDLIBS := -llog -lGLESv2 -landroid -lEGL
else
  LOCAL_CPPFLAGS += -fsigned-char -fexceptions -frtti -I "../../JuceLibraryCode" -I "../../JuceLibraryCode/modules" -O3 -std=c++11 -std=gnu++11 -D "JUCE_ANDROID=1" -D "JUCE_ANDROID_API_VERSION=9" -D "JUCE_ANDROID_ACTIVITY_CLASSNAME=com_mute_yse_static" -D JUCE_ANDROID_ACTIVITY_CLASSPATH=\"com/mute/yse_static\" -D "NDEBUG=1" -D "JUCE_CHECK_MEMORY_LEAKS=0" -D "JUCER_ANDROID_7F0E4A25=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000"
  LOCAL_LDLIBS := -llog -lGLESv2 -landroid -lEGL
  LOCAL_CFLAGS += -fsigned-char -fexceptions -frtti -I "../../JuceLibraryCode" -I "../../JuceLibraryCode/modules" -O3 -std=c++11 -std=gnu++11 -D "JUCE_ANDROID=1" -D "JUCE_ANDROID_API_VERSION=9" -D "JUCE_ANDROID_ACTIVITY_CLASSNAME=com_mute_yse_static" -D JUCE_ANDROID_ACTIVITY_CLASSPATH=\"com/mute/yse_static\" -D "NDEBUG=1" -D "JUCE_CHECK_MEMORY_LEAKS=0" -D "JUCER_ANDROID_7F0E4A25=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000"
  LOCAL_LDLIBS := -llog -lGLESv2 -landroid -lEGL
endif

include $(BUILD_SHARED_LIBRARY)
