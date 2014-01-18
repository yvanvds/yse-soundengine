/*

    IMPORTANT! This file is auto-generated each time you save your
    project - if you alter its contents, your changes may be overwritten!

    There's a section below where you can add your own custom code safely, and the
    Introjucer will preserve the contents of that block, but the best way to change
    any of these definitions is by using the Introjucer's project settings.

    Any commented-out settings will assume their default values.

*/

#ifndef __JUCE_APPCONFIG_HSAFBS__
#define __JUCE_APPCONFIG_HSAFBS__

//==============================================================================
// [BEGIN_USER_CODE_SECTION]

// (You can add your own code in this section, and the Introjucer will not overwrite it)

// [END_USER_CODE_SECTION]

//==============================================================================
#define JUCE_MODULE_AVAILABLE_juce_audio_basics       1
#define JUCE_MODULE_AVAILABLE_juce_audio_devices      1
#define JUCE_MODULE_AVAILABLE_juce_audio_formats      1
#define JUCE_MODULE_AVAILABLE_juce_core               1
#define JUCE_MODULE_AVAILABLE_juce_events             1

//==============================================================================
// juce_audio_devices flags:

#ifndef    JUCE_ASIO
 #define   JUCE_ASIO 1
#endif

#ifndef    JUCE_WASAPI
 #define   JUCE_WASAPI 1
#endif

#ifndef    JUCE_DIRECTSOUND
 #define   JUCE_DIRECTSOUND 1
#endif

#ifndef    JUCE_ALSA
 #define   JUCE_ALSA 1
#endif

#ifndef    JUCE_JACK
 #define   JUCE_JACK 1
#endif

#ifndef    JUCE_USE_ANDROID_OPENSLES
 #define   JUCE_USE_ANDROID_OPENSLES 1
#endif

#ifndef    JUCE_USE_CDREADER
 #define   JUCE_USE_CDREADER 0
#endif

#ifndef    JUCE_USE_CDBURNER
 #define   JUCE_USE_CDBURNER 0
#endif

//==============================================================================
// juce_audio_formats flags:

#ifndef    JUCE_USE_FLAC
 #define   JUCE_USE_FLAC 1
#endif

#ifndef    JUCE_USE_OGGVORBIS
 #define   JUCE_USE_OGGVORBIS 1
#endif

#ifndef    JUCE_USE_MP3AUDIOFORMAT
 #define   JUCE_USE_MP3AUDIOFORMAT 0
#endif

#ifndef    JUCE_USE_LAME_AUDIO_FORMAT
 #define   JUCE_USE_LAME_AUDIO_FORMAT 1
#endif

#ifndef    JUCE_USE_WINDOWS_MEDIA_FORMAT
 #define   JUCE_USE_WINDOWS_MEDIA_FORMAT 1
#endif

//==============================================================================
// juce_core flags:

#ifndef    JUCE_FORCE_DEBUG
 //#define JUCE_FORCE_DEBUG
#endif

#ifndef    JUCE_LOG_ASSERTIONS
 //#define JUCE_LOG_ASSERTIONS
#endif

#ifndef    JUCE_CHECK_MEMORY_LEAKS
 #define   JUCE_CHECK_MEMORY_LEAKS 1
#endif

#ifndef    JUCE_DONT_AUTOLINK_TO_WIN32_LIBRARIES
 //#define JUCE_DONT_AUTOLINK_TO_WIN32_LIBRARIES
#endif

#ifndef    JUCE_INCLUDE_ZLIB_CODE
 //#define JUCE_INCLUDE_ZLIB_CODE
#endif


#endif  // __JUCE_APPCONFIG_HSAFBS__
