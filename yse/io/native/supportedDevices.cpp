/*
  ==============================================================================

    supportedDevices.cpp
    Created: 13 Jun 2014 4:40:38pm
    Author:  yvan

  ==============================================================================
*/

#include "../../headers/defines.hpp"

#if YSE_WINDOWS
#include "windows_comSmartPtr.h"

  #if YSE_WASAPI
  #include "windowsWASAPI.cpp"
  #endif

  #if YSE_DIRECTSOUND
  #include "windowsDirectSound.cpp"
  #endif

  #if YSE_ASIO
  #include "windowsASIO.cpp"
  #endif

#elif YSE_LINUX

#if YSE_ALSA
#include "linuxALSA.cpp"
#endif

#if YSE_JACK
#include "linuxJack.cpp"
#endif

#elif YSE_MAC
// A mac is just an overpriced linux anyway...
#include "linuxCoreAudio.cpp"

#elif YSE_IOS
#include "iosAudio.cpp"

#elif YSE_ANDROID
#include "androidAudio.cpp"
#endif


