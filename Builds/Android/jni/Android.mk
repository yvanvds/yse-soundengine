# Automatically generated makefile, created by the Introjucer
# Don't edit this file! Your changes will be overwritten when you re-save the Introjucer project!

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm
LOCAL_MODULE := juce_jni
LOCAL_SRC_FILES := \
  ../../../src/backend/filesysImpl.cpp\
  ../../../src/backend/filesystem.cpp\
  ../../../src/backend/opensl.cpp\
  ../../../src/backend/pa.cpp\
  ../../../src/backend/reverbbackend.cpp\
  ../../../src/backend/soundfile.cpp\
  ../../../src/backend/soundLoader.cpp\
  ../../../src/backend/ysetime.cpp\
  ../../../src/dsp/delay.cpp\
  ../../../src/dsp/dsp.cpp\
  ../../../src/dsp/filters.cpp\
  ../../../src/dsp/math.cpp\
  ../../../src/dsp/math_functions.cpp\
  ../../../src/dsp/modules/hilbert.cpp\
  ../../../src/dsp/modules/ringModulator.cpp\
  ../../../src/dsp/modules/sineWave.cpp\
  ../../../src/dsp/oscillators.cpp\
  ../../../src/dsp/ramp.cpp\
  ../../../src/dsp/sample.cpp\
  ../../../src/instruments/instrument.cpp\
  ../../../src/instruments/sampler.cpp\
  ../../../src/instruments/sineSynth.cpp\
  ../../../src/internal/channelimpl.cpp\
  ../../../src/internal/instruments/baseInstrumentImpl.cpp\
  ../../../src/internal/instruments/samplerImpl.cpp\
  ../../../src/internal/instruments/sineSynthImpl.cpp\
  ../../../src/internal/internalObjects.cpp\
  ../../../src/internal/listenerimpl.cpp\
  ../../../src/internal/music/globalTrack.cpp\
  ../../../src/internal/music/trackImpl.cpp\
  ../../../src/internal/playlistimpl.cpp\
  ../../../src/internal/reverbimpl.cpp\
  ../../../src/internal/settings.cpp\
  ../../../src/internal/soundimpl.cpp\
  ../../../src/music/chord.cpp\
  ../../../src/music/note.cpp\
  ../../../src/music/track.cpp\
  ../../../src/utils/error.cpp\
  ../../../src/utils/guard.cpp\
  ../../../src/utils/memory.cpp\
  ../../../src/utils/misc.cpp\
  ../../../src/utils/vector.cpp\
  ../../../src/channel.cpp\
  ../../../src/device.cpp\
  ../../../src/listener.cpp\
  ../../../src/playlist.cpp\
  ../../../src/reverb.cpp\
  ../../../src/sound.cpp\
  ../../../src/speakers.cpp\
  ../../../src/system.cpp\
  ../../../JuceLibraryCode/modules/juce_audio_basics/juce_audio_basics.cpp\
  ../../../JuceLibraryCode/modules/juce_audio_devices/juce_audio_devices.cpp\
  ../../../JuceLibraryCode/modules/juce_audio_formats/juce_audio_formats.cpp\
  ../../../JuceLibraryCode/modules/juce_core/juce_core.cpp\
  ../../../JuceLibraryCode/modules/juce_events/juce_events.cpp\

ifeq ($(CONFIG),Debug)
  LOCAL_CPPFLAGS += -fsigned-char -fexceptions -frtti -g -I "../../JuceLibraryCode" -I "../../JuceLibraryCode/modules" -O0 -D "JUCE_ANDROID=1" -D "JUCE_ANDROID_API_VERSION=8" -D "JUCE_ANDROID_ACTIVITY_CLASSNAME=com_yourcompany_ysesoundengine_ysesoundengine" -D JUCE_ANDROID_ACTIVITY_CLASSPATH=\"com/yourcompany/ysesoundengine/ysesoundengine\" -D "DEBUG=1" -D "_DEBUG=1" -D "JUCER_ANDROID_7F0E4A25=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000"
  LOCAL_LDLIBS := -llog -lGLESv2
  LOCAL_CFLAGS += -fsigned-char -fexceptions -frtti -g -I "../../JuceLibraryCode" -I "../../JuceLibraryCode/modules" -O0 -D "JUCE_ANDROID=1" -D "JUCE_ANDROID_API_VERSION=8" -D "JUCE_ANDROID_ACTIVITY_CLASSNAME=com_yourcompany_ysesoundengine_ysesoundengine" -D JUCE_ANDROID_ACTIVITY_CLASSPATH=\"com/yourcompany/ysesoundengine/ysesoundengine\" -D "DEBUG=1" -D "_DEBUG=1" -D "JUCER_ANDROID_7F0E4A25=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000"
  LOCAL_LDLIBS := -llog -lGLESv2
else
  LOCAL_CPPFLAGS += -fsigned-char -fexceptions -frtti -I "../../JuceLibraryCode" -I "../../JuceLibraryCode/modules" -Os -D "JUCE_ANDROID=1" -D "JUCE_ANDROID_API_VERSION=8" -D "JUCE_ANDROID_ACTIVITY_CLASSNAME=com_yourcompany_ysesoundengine_ysesoundengine" -D JUCE_ANDROID_ACTIVITY_CLASSPATH=\"com/yourcompany/ysesoundengine/ysesoundengine\" -D "NDEBUG=1" -D "JUCER_ANDROID_7F0E4A25=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000"
  LOCAL_LDLIBS := -llog -lGLESv2
  LOCAL_CFLAGS += -fsigned-char -fexceptions -frtti -I "../../JuceLibraryCode" -I "../../JuceLibraryCode/modules" -Os -D "JUCE_ANDROID=1" -D "JUCE_ANDROID_API_VERSION=8" -D "JUCE_ANDROID_ACTIVITY_CLASSNAME=com_yourcompany_ysesoundengine_ysesoundengine" -D JUCE_ANDROID_ACTIVITY_CLASSPATH=\"com/yourcompany/ysesoundengine/ysesoundengine\" -D "NDEBUG=1" -D "JUCER_ANDROID_7F0E4A25=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000"
  LOCAL_LDLIBS := -llog -lGLESv2
endif

include $(BUILD_SHARED_LIBRARY)
