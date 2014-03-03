# Automatically generated makefile, created by the Introjucer
# Don't edit this file! Your changes will be overwritten when you re-save the Introjucer project!

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm
LOCAL_MODULE := juce_jni
LOCAL_SRC_FILES := \
  ../../../../yse/dsp/delay.cpp\
  ../../../../yse/dsp/dspObject.cpp\
  ../../../../yse/dsp/filters.cpp\
  ../../../../yse/dsp/math.cpp\
  ../../../../yse/dsp/math_functions.cpp\
  ../../../../yse/dsp/modules/hilbert.cpp\
  ../../../../yse/dsp/modules/ringModulator.cpp\
  ../../../../yse/dsp/modules/sineWave.cpp\
  ../../../../yse/dsp/oscillators.cpp\
  ../../../../yse/dsp/ramp.cpp\
  ../../../../yse/dsp/sample.cpp\
  ../../../../yse/implementations/channelImplementation.cpp\
  ../../../../yse/implementations/listenerImplementation.cpp\
  ../../../../yse/implementations/logImplementation.cpp\
  ../../../../yse/implementations/soundImplementation.cpp\
  ../../../../yse/internal/channelManager.cpp\
  ../../../../yse/internal/deviceManager.cpp\
  ../../../../yse/internal/global.cpp\
  ../../../../yse/internal/reverbDSP.cpp\
  ../../../../yse/internal/reverbManager.cpp\
  ../../../../yse/internal/settings.cpp\
  ../../../../yse/internal/soundFile.cpp\
  ../../../../yse/internal/soundManager.cpp\
  ../../../../yse/internal/time.cpp\
  ../../../../yse/internal/underWaterEffect.cpp\
  ../../../../yse/utils/vector.cpp\
  ../../../../yse/channel.cpp\
  ../../../../yse/listener.cpp\
  ../../../../yse/log.cpp\
  ../../../../yse/reverb.cpp\
  ../../../../yse/sound.cpp\
  ../../../../yse/system.cpp\
  ../../../Source/parts/ChannelTreeItem.cpp\
  ../../../Source/parts/yseTimerThread.cpp\
  ../../../Source/parts/draggedComponent.cpp\
  ../../../Source/basic.cpp\
  ../../../Source/basic3D.cpp\
  ../../../Source/cpuLoad.cpp\
  ../../../Source/movingChannels.cpp\
  ../../../Source/Main.cpp\
  ../../../Source/MainComponent.cpp\
  ../../../Source/tabs.cpp\
  ../../../Source/YSEObjects.cpp\
  ../../../JuceLibraryCode/modules/juce_audio_basics/juce_audio_basics.cpp\
  ../../../JuceLibraryCode/modules/juce_audio_devices/juce_audio_devices.cpp\
  ../../../JuceLibraryCode/modules/juce_audio_formats/juce_audio_formats.cpp\
  ../../../JuceLibraryCode/modules/juce_core/juce_core.cpp\
  ../../../JuceLibraryCode/modules/juce_cryptography/juce_cryptography.cpp\
  ../../../JuceLibraryCode/modules/juce_data_structures/juce_data_structures.cpp\
  ../../../JuceLibraryCode/modules/juce_events/juce_events.cpp\
  ../../../JuceLibraryCode/modules/juce_graphics/juce_graphics.cpp\
  ../../../JuceLibraryCode/modules/juce_gui_basics/juce_gui_basics.cpp\
  ../../../JuceLibraryCode/modules/juce_gui_extra/juce_gui_extra.cpp\

ifeq ($(CONFIG),Debug)
  LOCAL_CPPFLAGS += -fsigned-char -fexceptions -frtti -g -I "../../JuceLibraryCode" -I "../../JuceLibraryCode/modules" -O0 -D "JUCE_ANDROID=1" -D "JUCE_ANDROID_API_VERSION=8" -D "JUCE_ANDROID_ACTIVITY_CLASSNAME=com_mute_ysedemo_YseDemo" -D JUCE_ANDROID_ACTIVITY_CLASSPATH=\"com/mute/ysedemo/YseDemo\" -D "DEBUG=1" -D "_DEBUG=1" -D "JUCER_ANDROID_7F0E4A25=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000"
  LOCAL_LDLIBS := -llog -lGLESv2
  LOCAL_CFLAGS += -fsigned-char -fexceptions -frtti -g -I "../../JuceLibraryCode" -I "../../JuceLibraryCode/modules" -O0 -D "JUCE_ANDROID=1" -D "JUCE_ANDROID_API_VERSION=8" -D "JUCE_ANDROID_ACTIVITY_CLASSNAME=com_mute_ysedemo_YseDemo" -D JUCE_ANDROID_ACTIVITY_CLASSPATH=\"com/mute/ysedemo/YseDemo\" -D "DEBUG=1" -D "_DEBUG=1" -D "JUCER_ANDROID_7F0E4A25=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000"
  LOCAL_LDLIBS := -llog -lGLESv2
else
  LOCAL_CPPFLAGS += -fsigned-char -fexceptions -frtti -I "../../JuceLibraryCode" -I "../../JuceLibraryCode/modules" -Os -D "JUCE_ANDROID=1" -D "JUCE_ANDROID_API_VERSION=8" -D "JUCE_ANDROID_ACTIVITY_CLASSNAME=com_mute_ysedemo_YseDemo" -D JUCE_ANDROID_ACTIVITY_CLASSPATH=\"com/mute/ysedemo/YseDemo\" -D "NDEBUG=1" -D "JUCER_ANDROID_7F0E4A25=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000"
  LOCAL_LDLIBS := -llog -lGLESv2
  LOCAL_CFLAGS += -fsigned-char -fexceptions -frtti -I "../../JuceLibraryCode" -I "../../JuceLibraryCode/modules" -Os -D "JUCE_ANDROID=1" -D "JUCE_ANDROID_API_VERSION=8" -D "JUCE_ANDROID_ACTIVITY_CLASSNAME=com_mute_ysedemo_YseDemo" -D JUCE_ANDROID_ACTIVITY_CLASSPATH=\"com/mute/ysedemo/YseDemo\" -D "NDEBUG=1" -D "JUCER_ANDROID_7F0E4A25=1" -D "JUCE_APP_VERSION=1.0.0" -D "JUCE_APP_VERSION_HEX=0x10000"
  LOCAL_LDLIBS := -llog -lGLESv2
endif

include $(BUILD_SHARED_LIBRARY)
