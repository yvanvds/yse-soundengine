after updating the juce project, some changes have to be made to the android build files.

In Application.mk add:

APP_ABI      := armeabi armeabi-v7a x86
NDK_TOOLCHAIN_VERSION=4.8

in Android.mk replace the module name:

LOCAL_MODULE := yse_dll


In Juce Modules/Juce_audio_devices/native/juce_android_OpenSL.cpp

in the constructor, comment out and change:

//AndroidAudioIODevice javaDevice (String::empty);

inputLatency  = 50;
outputLatency = 50;