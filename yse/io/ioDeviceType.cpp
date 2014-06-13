/*
  ==============================================================================

    ioDeviceType.cpp
    Created: 12 Jun 2014 8:09:31pm
    Author:  yvan

  ==============================================================================
*/

#include "ioDeviceType.h"
#include "../headers/defines.hpp"

YSE::IO::ioDeviceType::ioDeviceType(const std::string & name) : typeName(name) {

}

YSE::IO::ioDeviceType::~ioDeviceType() {

}

#if ! YSE_MAC 
YSE::IO::ioDeviceType * YSE::IO::ioDeviceType::createCoreAudio() { return nullptr; }
#endif

#if ! YSE_IOS
YSE::IO::ioDeviceType * YSE::IO::ioDeviceType::createIosAudio() { return nullptr; }
#endif

#if ! (YSE_WINDOWS && YSE_WASAPI) 
YSE::IO::ioDeviceType * YSE::IO::ioDeviceType::createWASAPI() { return nullptr; }
#endif

#if ! (YSE_WINDOWS && YSE_DIRECTSOUND) 
YSE::IO::ioDeviceType * YSE::IO::ioDeviceType::createDirectSound() { return nullptr; }
#endif

#if ! (YSE_WINDOWS && YSE_ASIO) 
YSE::IO::ioDeviceType * YSE::IO::ioDeviceType::createASIO() { return nullptr; }
#endif

#if ! (YSE_LINUX && YSE_ALSA) 
YSE::IO::ioDeviceType * YSE::IO::ioDeviceType::createALSA() { return nullptr; }
#endif

#if ! (YSE_LINUX && YSE_JACK) 
YSE::IO::ioDeviceType * YSE::IO::ioDeviceType::createJACK() { return nullptr; }
#endif

#if ! YSE_ANDROID 
YSE::IO::ioDeviceType * YSE::IO::ioDeviceType::createAndroid() { return nullptr; }
#endif

#if ! (YSE_ANDROID && YSE_OPENSLES) 
YSE::IO::ioDeviceType * YSE::IO::ioDeviceType::createOpenSLES() { return nullptr; }
#endif



